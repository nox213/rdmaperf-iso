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

#define TERM 100000UL

#define MONITOR_CPU 7
#define PERF_CPU 8

#define MIN(x, y) (x) < (y) ? (x) : (y)

struct config option;
struct resource *r_table;
struct task_info t_info;

/* next for qp cache */
int *next_pointer;
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

		/*
		if (i != 0)
			r_table[i].type = SECONDARY;
			*/

		if (r_table[i].type == LATENCY) {
			tok = strtok(NULL, " ");
			r_table[i].stat.qos = strtoull(tok, NULL, 10);
		}

		printf("id: %d, name: %s, type: %d qos: %u\n", 
				i, r_table[i].task_name, r_table[i].type, r_table[i].stat.qos);
		t_info.nr_task++;	
	}

	printf("# of tasks: %d\n", t_info.nr_task);
	next_pointer = malloc(sizeof(int) * t_info.nr_task);
	if (!next_pointer) {
		fprintf(stderr, "fail to malloc\n");
		goto unlink;
	}
	memset(next_pointer, 0, sizeof(int) * t_info.nr_task);

	slack = malloc(sizeof(double) * t_info.nr_task);
	if (!slack) {
		fprintf(stderr, "fail to malloc\n");
		goto free_next;
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
free_next:
	free(next_pointer);
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
		r_table[i].allocated_bandwidth = t_info.total_bandwidth / t_info.nr_task;
		r_table[i].allocated_qps = t_info.total_qps / t_info.nr_task;
		r_table[i].on = false;
	}
}

int monitor(void)
{
	int i;
	int cnt = 0, ret = 0;
	double remain = 0;

	cpu_set_t   cpuset;
	pthread_t perf_thread, self;
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
	ret = pthread_create(&perf_thread, &attr, performance_monitor, (void *) TERM);
	if (ret < 0) {
		fprintf(stderr, "error on creating a thread in %s\n", __func__);
		goto free;
	}

	while (true) {
		usleep(10);
		for (i = 0; i < t_info.nr_task; i++) {
			if (r_table[i].type == LATENCY)
				continue;

			remain = (double) r_table[i].cache.space / r_table[i].cache.capacity;
			if (r_table[i].on && remain < 0.5) {
				drop_entry(i);
			}
		}
	}

free:
	pthread_attr_destroy    (&attr);

	return ret;
}


int drop_entry(int task_id)
{
	static bool error = false;
	int i, target, loop; 
	int entry_size = r_table[task_id].cache.entry_size;
	struct qp_cache *cache = &r_table[task_id].cache;

	target = -1;
	loop = -1;
	for (i = next_pointer[task_id]; loop < 3; i = (i + 1) % entry_size) {
		if (cache->entry[i] == 2) {
			cache->entry[i]--;
		}
		else if (cache->entry[i] == 1) {
			target = i;
			break;
		}
		if (i == next_pointer[task_id]) {
			loop++;
		}
	}

	if (target < 0)  {
		if (!error)
			printf("error task id: %d size: %d space: %d i: %d next_pointer: %d\n", 
					task_id, entry_size, cache->space, i, 
					next_pointer[task_id]);
		error = true;
		return -1;
	}

	next_pointer[task_id] = (target + 1) % entry_size;
	cache_delete(cache, target);

	return 0;
}

void *performance_monitor(void *args)
{
	int min_i, max_i, ret, i;
	uint64_t term = (uint64_t) args;
	uint64_t prev_time_stamp;
	double prev_slack;

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
		usleep(2000);
		cal_slack();
		min_i = find_min_slack();
		max_i = find_max_slack();
		if (min_i < 0 || max_i < 0)
			continue;

		if (slack[min_i] < 0.05) {
			prev_time_stamp = get_time_stamp(&r_table[min_i]);
			prev_slack = slack[min_i];
			i = retrieve_resource(0.2);
			if (i < 0)
				continue;

			usleep(50000);
			while (prev_time_stamp != get_time_stamp(&r_table[min_i]))
					;

			cal_slack();
			if (prev_slack >= slack[min_i]) {
				alloc_resource(0.25);
				r_dir[i] = ALLOC; 
			}
		}
		else if (slack[max_i] > 0.20) {
			prev_time_stamp = get_time_stamp(&r_table[max_i]);
			i = alloc_resource(0.1);
			if (i < 0)
				continue;

			usleep(50000);
			while (prev_time_stamp != get_time_stamp(&r_table[max_i]))
					;

			cal_slack();
			if (slack[max_i] < 0.05) {
				retrieve_resource(0.1);
				r_dir[i] = RET;
			}
		}
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
		if (max < slack[i]) {
			max = slack[i];
			max_i = i;
		}
	}

	return max_i;
}

int retrieve_resource(double ratio)
{
	int num; 
	int i = find_victim();
	struct resource *res;

	if (i < 0)
		return i;

	res = &r_table[i];
	num = ceil(res->cache.capacity * ratio);
	reconfig_cache(&res->cache, num, DOWN);
	r_dir[i] = BOTH;
	printf("num:%d  cap: %d in %s\n", num, res->cache.capacity, __func__);

	return i;
}

int alloc_resource(double ratio)
{
	int num;
	int i = find_reciever();
	struct resource *res;

	if (i < 0)
		return i;

	res = &r_table[i];
	num = ceil(res->cache.capacity * ratio);
	reconfig_cache(&res->cache, num, UP);
	r_dir[i] = BOTH;
	printf("cap: %d in %s\n", res->cache.capacity, __func__);

	return i;
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
		if (tmp->capacity - tmp->space > usage) {
			usage = tmp->capacity - tmp->space;
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
		usage = (double) (tmp->capacity - tmp->space) / tmp->capacity;
		if (usage < 0.7)
			continue;
		if (usage > max_usage) {
			max_usage = usage;
			max_i = i;
		}
	}

	return max_i;
}
