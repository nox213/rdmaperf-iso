#ifndef _MONITOR_H
#define _MONITOR_H

int monitor(void);
int init_resource(void);

struct task_info {
	int nr_task;
	uint64_t total_bandwidth;
	uint64_t total_qps;
};

int init_task_info(struct task_info *t_info, uint64_t total_bandwidth, uint64_t total_qps);

#endif
