#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "qp_cache.h"

int init_qp_cache(struct qp_cache *cache, int capacity)
{
	cache->space = capacity;
	cache->capacity = capacity;
	pthread_spin_init(cache->cache_lock, PTHREAD_PROCESS_PRIVATE);
	return 0;
}
bool cache_find(struct qp_cache *cache, uint32_t value)
{
	if (cache->entry[value])
		return true;
	else
		return false;
}
int cache_insert(struct qp_cache *cache, uint32_t value)
{
loop:
	while (!cache->space)
		;
	pthread_spin_lock(cache->cache_lock);
	if (!cache->space) {
		pthread_spin_unlock(cache->cache_lock);
		goto loop;
	}
	cache->space--;
	pthread_spin_unlock(cache->cache_lock);

	return 0;
	
}
int cache_delete(struct qp_cache *cache, uint32_t value)
{
	if (cache->space == cache->capacity)
		return -1;
	pthread_spin_lock(cache->cache_lock);
	cache->space++;
	cache->entry[value] = false;
	pthread_spin_unlock(cache->cache_lock);

	return 0;
}
int free_cache(struct qp_cache *cache)
{
	pthread_spin_destroy(cache->cache_lock);
	return 0;
}
