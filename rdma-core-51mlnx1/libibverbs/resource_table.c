#include "resource_table.h"

extern struct resource *my_res;

void report_latency(uint32_t median, uint32_t tail, uint64_t time_stamp)
{
	__atomic_store(&my_res->stat.cur_median, &median, __ATOMIC_RELAXED);
	__atomic_store(&my_res->stat.cur_tail, &tail, __ATOMIC_RELAXED);
	__atomic_store(&my_res->stat.time_stamp, &time_stamp, __ATOMIC_RELAXED);
}

void report_bw(uint64_t bandwidth, uint64_t time_stamp)
{
	__atomic_store(&my_res->stat.bandwidth, &bandwidth, __ATOMIC_RELAXED);
}

enum task_type my_task_type(void)
{
	return my_res->type;
}
