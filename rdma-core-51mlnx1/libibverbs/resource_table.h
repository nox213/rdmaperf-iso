#ifndef __RESOURCE_TABLE__H

#define __RESOURCE_TABLE__H

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "tokenbucket.h"
#include "qp_cache.h"

#define NR_BUCKET 30
#define TABLE_SIZE 100
#define HISTORY_LENGTH 100

enum task_type {
	LATENCY,
	SECONDARY,
	NONE,
};

struct task_stat {
	uint32_t qos;
	uint32_t cur_median;
	uint32_t cur_tail;
};


struct history_table {
	uint32_t access_history[HISTORY_LENGTH];
	uint32_t capacity;
	uint32_t head;
	uint32_t tail;
};

struct resource {
	bool on;

	uint64_t allocated_bandwidth;
	uint64_t allocated_qps;

	struct qp_cache cache;
	struct token_bucket tb[NR_BUCKET];
	struct history_table ht;

	struct task_stat stat;
	enum task_type type;
	char task_name[40];
};

static inline int init_history_table(struct history_table *ht)
{
	ht->head = ht->tail = 0;
	ht->capacity = HISTORY_LENGTH;
	memset(ht->access_history, -1, HISTORY_LENGTH * sizeof(uint32_t));

	return 0;
}

static inline int free_history_table(struct history_table *ht)
{
	return 0;
}

static inline void add_history(struct history_table *ht, uint32_t handle)
{
	int i = __atomic_fetch_add(&(ht->tail), 1, __ATOMIC_ACQUIRE);
	i %= ht->capacity;
	ht->access_history[i] = handle;
}

static inline void consume_history(struct history_table *ht)
{
	;
}

static inline uint32_t get_tail_lat(struct resource *res)
{
	return __atomic_load_n(&res->stat.cur_tail, __ATOMIC_RELAXED);
}

#ifdef __cplusplus
extern "C" {
#endif

void report_latency(uint32_t median, uint32_t tail);
enum task_type my_task_type(void);

#ifdef __cplusplus
}
#endif

#endif

