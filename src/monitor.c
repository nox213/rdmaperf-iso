#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <pthread.h>
#include <math.h>

#include "misc.h"
#include "monitor.h"

#define INTERVAL 100000UL

#define TOTAL_BW 7000000000
#define INIT_BANDWIDTH 1000000000
#define BURST_SIZE (8 * (1 << 20))   // in bytes

#define INIT_REQUEST_RATE 4000000

#define MONITOR_CPU 6
#define PERF_CPU 7

#define MIN(x, y) ((x) < (y) ? (x) : (y))
#define MAX(x, y) ((x) > (y) ? (x) : (y))

struct config option;
struct resource *r_table;
struct task_info t_info;

/* next for qp cache */
double *slack;
enum resource_direction *r_dir;

int main(int argc, char *argv[])
{
	int i;
	char str[200];
	char *tok;
	size_t len;
	FILE *config_fp = NULL;
	int shm_fd;

	parse_options(argc, argv);
	srand(time(NULL));

	config_fp = fopen(option.config_path, "r");
	if (!config_fp)
		err_sys("fail to open config file");

	shm_fd = shm_open("/resource_table", O_RDWR | O_CREAT, 0644);
	ftruncate(shm_fd, TABLE_SIZE * sizeof(struct resource));
	r_table = mmap(NULL, TABLE_SIZE * sizeof(struct resource), PROT_READ | PROT_WRITE, 
			MAP_SHARED, shm_fd, 0);
	if (!r_table)
		goto unlink;
	memset(r_table, 0, sizeof(struct resource) * TABLE_SIZE);

	init_task_info(&t_info, option.total_bandwidth, option.total_qps);
	while (fgets(str, sizeof(str), config_fp)) {
		len = strlen(str);
		str[len-1] = '\0';

		tok = strtok(str, " ");
		i = atoi(tok);

		tok = strtok(NULL, " ");
		strncpy(r_table[i].task_name, tok, sizeof(r_table[i].task_name));
		r_table[i].task_name[sizeof(r_table[i].task_name)-1] = '\0';

		tok = strtok(NULL, " ");
		if (strcmp(tok, "latency") == 0)
			r_table[i].type = LATENCY;
		else
			r_table[i].type = SECONDARY;

		if (i != 0)
			r_table[i].type = SECONDARY;

		if (r_table[i].type == LATENCY) {
			tok = strtok(NULL, " ");
			r_table[i].stat.qos = strtoull(tok, NULL, 10);
		}

		r_table[i].cache_limit = false;

		printf("id: %d, name: %s, type: %d qos: %u\n", 
				i, r_table[i].task_name, r_table[i].type, r_table[i].stat.qos);
		t_info.nr_task++;	
	}
	init_token_bucket(&r_table[0].tb, INIT_BANDWIDTH, BURST_SIZE);
	init_token_bucket(&r_table[0].request_tb, INIT_REQUEST_RATE, 64);

	printf("# of tasks: %d\n", t_info.nr_task);

	slack = malloc(sizeof(double) * t_info.nr_task);
	if (!slack) {
		fprintf(stderr, "fail to malloc\n");
		goto unlink;
	}
	memset(slack, 0, sizeof(double) * t_info.nr_task);

	r_dir = malloc(sizeof(enum resource_direction) * t_info.nr_task);
	if (!r_dir) {
		fprintf(stderr, "fail to malloc\n");
		goto free_slack;
	}
	memset(r_dir, 0, sizeof(enum resource_direction) * t_info.nr_task);

	fclose(config_fp);
	init_resource();
	printf("init done\n");

//	daemonize(argv[0]);
	monitor();

free_dir:
	free(r_dir);
free_slack:
	free(slack);
unlink:
	shm_unlink("resource_table");

	return 0;
}

void parse_options(int argc, char *argv[])
{
	int c;

	option.total_bandwidth = 7000000000;
	option.total_qps = 100;
	opterr = 0; 
	while ((c = getopt(argc, argv, "b:q:p:n:")) != EOF) {
		switch (c) {
			case 'b':
				option.total_bandwidth = strtoull(optarg, NULL, 10);
				break;
			case 'q':
				option.total_qps = atoi(optarg);
				break;
			case 'p':
				option.config_path = optarg;
				break;
			case 'n':
				option.node_config_path = optarg;
				break;
			case '?':
				fprintf(stderr, "unrecognized option: -%c", optopt);
				break;
		}
	}

	printf("total bandwidth: %lu\n", option.total_bandwidth);
	printf("total qps: %lu\n", option.total_qps);
}

int init_task_info(struct task_info *t_info, uint64_t total_bandwidth, uint64_t total_qps)
{
	t_info->nr_task = 0;
	t_info->total_bandwidth = total_bandwidth;
	t_info->total_qps = total_qps;

	return 0;
}

void init_resource(void)
{
	int i;

	syslog(LOG_INFO, "init resource %d", t_info.nr_task);
	for (i = 0; i < t_info.nr_task; i++) {
		r_table[i].allocated_bandwidth = INIT_BANDWIDTH;
		r_table[i].allocated_qps = 50;
		r_table[i].on = false;
	}
}

int monitor(void)
{
	int i;
	int cnt = 0, ret = 0;

	cpu_set_t   cpuset;
	pthread_t perf_thread, cache_thread, self;
	pthread_attr_t       attr;


	CPU_ZERO(&cpuset);
	CPU_SET(MONITOR_CPU, &cpuset);

	self = pthread_self ();
	ret  = pthread_setaffinity_np (self, sizeof(cpu_set_t), &cpuset);

	if (ret < 0) {
		fprintf(stderr, "failed to set thread affinity in %s\n", __func__);
		return ret;
	}

	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_JOINABLE);

	memset(&perf_thread, 0, sizeof(pthread_t));
	ret = pthread_create(&perf_thread, &attr, network_monitor, (void *) INTERVAL);
	if (ret < 0) {
		fprintf(stderr, "error on creating a thread in %s\n", __func__);
		goto free;
	}

	ret = pthread_create(&cache_thread, &attr, nic_cache_monitor, (void *) INTERVAL);
	if (ret < 0) {
		fprintf(stderr, "error on create cache monitoring thread %s\n", __func__);
		goto free;
	}

	while (true) {
		sleep(5);
		if (!r_table[0].on)
			continue;

		cal_slack();

		if (slack[0] < 0.05) {
		}
		else if (slack[0] > 0.25) {
		}
	}

free:
	pthread_attr_destroy    (&attr);

	return ret;
}

void *nic_cache_monitor(void *args)
{
	int i, ret;
	cpu_set_t   cpuset;
	pthread_t perf_thread, self;
	pthread_attr_t       attr;

	CPU_ZERO(&cpuset);
	CPU_SET(PERF_CPU + 1, &cpuset);

	self = pthread_self ();
	ret  = pthread_setaffinity_np (self, sizeof(cpu_set_t), &cpuset);

	while (true) {
		usleep(5000);
	}
}

void *network_monitor(void *args)
{
	int ret, i;
	const int interval = 5;
	const double buffer = 0.25;
	uint64_t bw_usage, be_bw;

	cpu_set_t   cpuset;
	pthread_t perf_thread, self;
	pthread_attr_t       attr;

	CPU_ZERO(&cpuset);
	CPU_SET(PERF_CPU, &cpuset);

	self = pthread_self ();
	ret  = pthread_setaffinity_np (self, sizeof(cpu_set_t), &cpuset);

	if (ret < 0) {
		fprintf(stderr, "failed to set thread affinity in %s\n", __func__);
		return (void *) ret;
	}

	while (true) {
		sleep(interval);
		bw_usage = get_bw(&r_table[0]);
		printf("bw usage: %ld\n", bw_usage);
		be_bw = TOTAL_BW - (bw_usage * 1000000)
			- MAX((TOTAL_BW * buffer), ((bw_usage * 1000000) * 0.1));
		set_rate(&r_table[0].tb, be_bw);
		printf("be bw: %lu\n", be_bw);
	}
}

void cal_slack(void)
{
	int i;
	int lat, diff;

	for (i = 0; i < t_info.nr_task; i++) {
		if (is_primary_task(&r_table[i])) {
			lat = get_tail_lat(&r_table[i]);
			diff = r_table[i].stat.qos - lat;
			if (diff < 0)
				diff = 0;
			slack[i] = diff / (double) r_table[i].stat.qos;
		}
	}
}

int find_min_slack(void)
{
	int i, min_i;
	double min;

	min_i = -1;
	min = 100;
	for (i = 0; i < t_info.nr_task; i++) {
		if (!r_table[i].on || r_table[i].type == SECONDARY)
			continue;

		if (min > slack[i]) {
			min = slack[i];
			min_i = i;
		}
	}

	return min_i;
}

int find_max_slack(void)
{
	int i, max_i;
	double max;

	max_i = -1;
	max = -100;
	for (i = 0; i < t_info.nr_task; i++) {
		if (!r_table[i].on || r_table[i].type == SECONDARY)
			continue;

		if (max < slack[i]) {
			max = slack[i];
			max_i = i;
		}
	}

	return max_i;
}

int retrieve_resource(double ratio)
{
	uint64_t rate;
	struct resource *res;


	res = &r_table[0];
	rate = ceil(res->tb.rate * ratio);
	down_rate(&res->tb, rate);
	printf("rate: %lu in %s\n", rate, __func__);

	return 0;
}

int alloc_resource(double ratio)
{
	uint64_t rate;
	struct resource *res;

	res = &r_table[0];
	rate = ceil(res->tb.rate * ratio);
	up_rate(&res->tb, rate);
	printf("rate: %lu in %s\n", res->tb.rate, __func__);

	return 0;
}

/* find victim by absolute usage */
int find_victim(void)
{
	struct qp_cache *tmp;
	int i, max_i;
	int usage = 0;

	max_i = -1;
	for (i = 0; i < t_info.nr_task; i++) {
		if (!r_table[i].on || r_dir[i] == ALLOC)
			continue;

		if (r_table[i].type == LATENCY)
			continue;

		tmp = &r_table[i].cache;
		if (tmp->usage > usage) {
			usage = tmp->usage;
			max_i = i;
		}
	}

	return max_i;
}

/*find reciever by relative usage */
int find_reciever(void)
{
	struct qp_cache *tmp;
	int i, max_i;
	double usage, max_usage;

	max_i = -1;
	max_usage = 0;
	for (i = 0; i < t_info.nr_task; i++) {
		if (!r_table[i].on || r_dir[i] == RET)
			continue;

		if (r_table[i].type == LATENCY)
			continue;

		tmp = &r_table[i].cache;
		usage = tmp->usage;
		if (usage < 0.7)
			continue;
		if (usage > max_usage) {
			max_usage = usage;
			max_i = i;
		}
	}

	return max_i;
}
