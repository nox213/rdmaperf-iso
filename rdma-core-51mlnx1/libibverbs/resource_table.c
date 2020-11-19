#include "resource_table.h"

extern struct resource *my_res;

void report_latency(uint32_t median, uint32_t tail)
{
	__atomic_store(&my_res->stat.cur_median, &median, __ATOMIC_RELAXED);
	__atomic_store(&my_res->stat.cur_tail, &tail, __ATOMIC_RELAXED);
}

enum task_type my_task_type(void)
{
	return my_res->type;
}
