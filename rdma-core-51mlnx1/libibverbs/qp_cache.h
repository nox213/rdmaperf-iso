#ifndef __QP_CACHE_H
#define __QP_CACHE_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define CACHE_SIZE 500

struct qp_cache {
	int entry[CACHE_SIZE];
	int entry_size;
	volatile int usage;
	pthread_spinlock_t cache_lock;
};

#ifdef __cplusplus
extern "C" {
#endif

int cache_find_or_insert(uint32_t value);

#ifdef __cplusplus
}
#endif

static inline int init_qp_cache(struct qp_cache *cache)
{
	cache->usage = 0;
	cache->entry_size = CACHE_SIZE;
	memset(cache->entry, 0, sizeof(cache->entry[0]) * CACHE_SIZE);
	pthread_spin_init(&(cache->cache_lock), PTHREAD_PROCESS_PRIVATE);
	return 0;
}

static inline int cache_delete(struct qp_cache *cache, uint32_t value)
{
	pthread_spin_lock(&cache->cache_lock);
	cache->entry[value] = 0;
	cache->usage--;
	pthread_spin_unlock(&cache->cache_lock);

	return 0;
}

static inline int cache_flush(struct qp_cache *cache)
{
	memset(cache->entry, 0, sizeof(cache->entry[0]) * CACHE_SIZE);
	cache->usage = 0;
	return 0;
}


#endif
