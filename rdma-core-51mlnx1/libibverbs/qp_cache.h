#ifndef __QP_CACHE_H
#define __QP_CACHE_H

#include <stdbool.h>
#include <stdint.h>

struct qp_cache {
	bool entry[500];
	int capacity;
	int space;
	pthread_spinlock_t *cache_lock;
};

int init_qp_cache(struct qp_cache *cache, int capacity);
bool cache_find(struct qp_cache *cache, uint32_t value);
int cache_insert(struct qp_cache *cache, uint32_t value);
int cache_delete(struct qp_cache *cache, uint32_t value);
int free_cache(struct qp_cache *cache);

#endif
