#ifndef _MONITOR_H
#define _MONITOR_H

#include <stdint.h>
#include <time.h>

#include <rdmaperf-iso/resource_table.h>
#include <rdmaperf-iso/qp_cache.h>
#include <rdmaperf-iso/tokenbucket.h>

enum resource_direction {
	BOTH,
	ALLOC,
	RET,
};

struct task_info {
	int nr_task;
	uint64_t total_bandwidth;
	uint64_t total_qps;
};

struct config {
	uint64_t total_bandwidth;	
	uint64_t total_qps;	
	char *config_path;
	char *node_config_path;
};

int monitor(void);
void init_resource(void);

int init_task_info(struct task_info *t_info, uint64_t total_bandwidth, uint64_t total_qps);
void parse_options(int argc, char *argv[]);

void *network_monitor(void *args);
void *nic_cache_monitor(void *args);
void cal_slack(void);
int find_min_slack(void);
int find_max_slack(void);
int retrieve_resource(double ratio);
int alloc_resource(double ratio);
int find_victim(void);
int find_reciever(void);

static inline uint64_t compute_elapsed_us(struct timespec *prev, struct timespec *cur)
{
	return ((cur->tv_sec - prev->tv_sec) * 1000000000UL +
		(cur->tv_nsec - prev->tv_nsec)) / 1000;
}

static inline bool is_primary_task(struct resource *res)
{
	return (res->type == LATENCY && res->on) ? true : false;
}

static inline bool is_secondary_task(struct resource *res)
{
	return (res->type == SECONDARY && res->on) ? true : false;
}

static inline void wait_for_new_time_stamp(uint64_t *prev, struct resource *res)
{
		while (*prev == get_time_stamp(res))
			;
		*prev = get_time_stamp(res);
}

#endif
