#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "qp_cache.h"
#include "resource_table.h"

extern struct resource *my_res;


static inline bool cache_find(struct qp_cache *cache, uint32_t value)
{
	return cache->entry[value];
}

static inline int cache_insert(struct qp_cache *cache, uint32_t value)
{
	int space;
loop:
	while (!(space = cache->space))
		;
	pthread_spin_lock(&cache->cache_lock);
	if (cache_find(cache, value))
		goto unlock;

	while (!(space = cache->space))
		;

	cache->space--;
	cache->entry[value] = true;

unlock:
	pthread_spin_unlock(&cache->cache_lock);

	return 0;
}


static inline int free_cache(struct qp_cache *cache)
{
	pthread_spin_destroy(&cache->cache_lock);
	return 0;
}


int cache_find_or_insert(uint32_t value)
{
	if (cache_find(&(my_res->cache), value))
		return 0;

	cache_insert(&(my_res->cache), value);
	add_history(&(my_res->ht), value);

	return 0;
}
