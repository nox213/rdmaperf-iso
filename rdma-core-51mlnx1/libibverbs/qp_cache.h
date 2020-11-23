#ifndef __QP_CACHE_H
#define __QP_CACHE_H

#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define CACHE_SIZE 500

enum direction {
	UP,
	DOWN,
};

struct qp_cache {
	int entry[CACHE_SIZE];
	int entry_size;
	int capacity;
	volatile int space;
	pthread_spinlock_t cache_lock;
};

#ifdef __cplusplus
extern "C" {
#endif

int cache_find_or_insert(uint32_t value);

#ifdef __cplusplus
}
#endif

static inline int init_qp_cache(struct qp_cache *cache, int capacity)
{
	cache->space = capacity;
	cache->capacity = capacity;
	cache->entry_size = CACHE_SIZE;
	memset(cache->entry, 0, sizeof(cache->entry[0]) * CACHE_SIZE);
	pthread_spin_init(&(cache->cache_lock), PTHREAD_PROCESS_PRIVATE);
	return 0;
}

static inline int cache_delete(struct qp_cache *cache, uint32_t value)
{
	if (cache->space == cache->capacity)
		return -1;
	pthread_spin_lock(&cache->cache_lock);
	cache->space++;
	cache->entry[value] = 0;
	pthread_spin_unlock(&cache->cache_lock);

	return 0;
}

static inline bool is_cache_full(struct qp_cache *cache)
{
	return cache->space == 0 ? true : false;
}

static inline int cache_flush(struct qp_cache *cache)
{
	memset(cache->entry, 0, sizeof(cache->entry[0]) * cache->capacity);
	cache->space = cache->capacity;
	return 0;
}

static inline int reconfig_cache(struct qp_cache *cache, int num, enum direction op)
{
	if (!cache)
		return -1;

	if ((cache->capacity < num) && (op == UP))
		return -1;

	pthread_spin_lock(&cache->cache_lock);
	if (op == UP)
		cache->capacity += num;
	else if (op == DOWN)
		cache->capacity -= num;
	cache_flush(cache);
	pthread_spin_unlock(&cache->cache_lock);
}

#endif
