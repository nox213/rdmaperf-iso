#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/mman.h>

#include "misc.h"
#include "monitor.h"

struct config option;
struct resource *r_table;
struct task_info t_info;
int *pointer;

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

		if (r_table[i].type == LATENCY) {
			tok = strtok(NULL, " ");
			r_table[i].stat.qos = strtoull(tok, NULL, 10);
		}

		printf("id: %d, name: %s, type: %d\n qos: %lu", 
				i, r_table[i].task_name, r_table[i].type, r_table[i].stat.qos);
		t_info.nr_task++;	
	}

	printf("# of tasks: %d\n", t_info.nr_task);
	pointer = malloc(sizeof(int) * t_info.nr_task);
	if (!pointer) {
		fprintf(stderr, "fail to malloc\n");
		goto free;
	}
	
	memset(pointer, 0, sizeof(int) * t_info.nr_task);

	fclose(config_fp);
	init_resource();
	printf("init done\n");

//	daemonize(argv[0]);
	monitor();

free:
	free(pointer);
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
	r_table[1].allocated_qps = 50;
}

int monitor(void)
{
	int i;
	int cnt = 0;

	while (true) {
		usleep(10);
		for (i = 0; i < t_info.nr_task; i++) {
			if (r_table[i].on && (r_table[i].cache.space < 10)) {
				drop_entry(i);
				/*
				if (cnt % 10000 == 0)
					printf("%d\n", r_table[i].cache.space);
				cnt++;
				*/
			}
		}
	}

	return 0;
}


int drop_entry(int task_id)
{
	static bool error = false;
	int i, target, loop; 
	int entry_size = r_table[task_id].cache.entry_size;
	struct qp_cache *cache = &r_table[task_id].cache;

	target = -1;
	loop = -1;
	for (i = pointer[task_id]; loop < 3; i = (i + 1) % entry_size) {
		if (cache->entry[i] == 2) {
			cache->entry[i]--;
		}
		else if (cache->entry[i] == 1) {
			target = i;
			break;
		}
		if (i == pointer[task_id]) {
			loop++;
		}
	}

	if (target < 0)  {
		if (!error)
			printf("error task id: %d size: %d space: %d i: %d pointer: %d\n", 
					task_id, entry_size, cache->space, i, 
					pointer[task_id]);
		error = true;
		return -1;
	}

	pointer[task_id] = (target + 1) % entry_size;
	cache_delete(cache, target);

	return 0;
}

