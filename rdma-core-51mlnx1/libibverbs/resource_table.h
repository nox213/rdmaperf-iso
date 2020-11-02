#ifndef __RESOURCE_TABLE__H

#define __RESOURCE_TABLE__H

#include <stdlib.h>
#include <errno.h>

#include "tokenbucket.h"
#include "qp_cache.h"

#define NR_BUCKET 30

enum task_type {
	LATENCY,
	SECONDARY,
	NONE,
};


struct history_table {
	uint32_t *access_history;
	uint32_t capacity;
	uint32_t head;
	uint32_t tail;
};

struct resource {
	uint64_t allocated_bandwidth;
	uint64_t allocated_qps;
	char task_name[40];
	enum task_type type;
	struct qp_cache cache;
	struct token_bucket tb[NR_BUCKET];
	struct history_table ht;
};



static inline int init_history_table(struct history_table *ht, uint32_t capacity)
{
	ht->head = ht->tail = 0;
	ht->capacity = capacity;
	ht->access_history = calloc(capacity, sizeof(uint32_t));
	if (!ht->access_history)
		return -ENOMEM;

	return 0;
}

static inline int free_history_table(struct history_table *ht, uint32_t capacity)
{
	free(ht->access_history);
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

#endif

