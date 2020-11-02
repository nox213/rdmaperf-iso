#ifndef _MONITOR_H
#define _MONITOR_H

#include <stdint.h>

int monitor(void);
void init_resource(void);

struct task_info {
	int nr_task;
	uint64_t total_bandwidth;
	uint64_t total_qps;
};

struct config {
	uint64_t total_bandwidth;	
	uint64_t total_qps;	
	char *config_path;
};

int init_task_info(struct task_info *t_info, uint64_t total_bandwidth, uint64_t total_qps);
void parse_options(int argc, char *argv[]);

#endif
