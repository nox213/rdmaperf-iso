/*
 * Copyright (c) 2019, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *	Redistribution and use in source and binary forms, with or
 *	without modification, are permitted provided that the following
 *	conditions are met:
 *
 *	- Redistributions of source code must retain the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *	- Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include "mlx5dv_dr.h"

#define DR_ICM_MODIFY_HDR_ALIGN_BASE	64
#define DR_ICM_SYNC_THRESHOLD_POOL (64 * 1024 * 1024)

struct dr_icm_pool {
	enum dr_icm_type	icm_type;
	struct mlx5dv_dr_domain	*dmn;
	enum dr_icm_chunk_size	max_log_chunk_sz;
	/* memory management */
	pthread_mutex_t		mutex;
	struct list_head	buddy_mem_list;
	uint64_t		hot_memory_size;
};

struct dr_icm_mr {
	struct ibv_mr		*mr;
	struct ibv_dm		*dm;
	uint64_t		icm_start_addr;
};

static int
dr_icm_allocate_aligned_dm(struct dr_icm_pool *pool,
			   struct dr_icm_mr *icm_mr,
			   struct ibv_alloc_dm_attr *dm_attr)
{
	struct mlx5dv_alloc_dm_attr mlx5_dm_attr = {};
	size_t log_align_base = 0;
	bool fallback = false;
	struct mlx5_dm *dm;
	size_t size;

	/* create dm/mr for this pool */
	size = dr_icm_pool_chunk_size_to_byte(pool->max_log_chunk_sz,
					      pool->icm_type);

	switch (pool->icm_type) {
	case DR_ICM_TYPE_STE:
		mlx5_dm_attr.type = MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM;
		/* Align base is the biggest chunk size */
		log_align_base = ilog32(size - 1);
		break;
	case DR_ICM_TYPE_MODIFY_ACTION:
		mlx5_dm_attr.type = MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM;
		/* Align base is 64B */
		log_align_base = ilog32(DR_ICM_MODIFY_HDR_ALIGN_BASE - 1);
		break;
	case DR_ICM_TYPE_MODIFY_HDR_PTRN:
		mlx5_dm_attr.type = MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_PATTERN_SW_ICM;
		/* Align base is 64B */
		log_align_base = ilog32(DR_ICM_MODIFY_HDR_ALIGN_BASE - 1);
		break;
	default:
		assert(false);
		errno = EINVAL;
		return errno;
	}

	dm_attr->length = size;

alloc_dm:
	icm_mr->dm = mlx5dv_alloc_dm(pool->dmn->ctx, dm_attr, &mlx5_dm_attr);
	if (!icm_mr->dm) {
		dr_dbg(pool->dmn, "Failed allocating DM\n");
		return errno;
	}

	dm = to_mdm(icm_mr->dm);
	icm_mr->icm_start_addr = dm->remote_va;

	if (icm_mr->icm_start_addr & ((1UL << log_align_base) - 1)) {
		uint64_t align_base;
		uint64_t align_diff;

		/* Fallback to previous implementation, ask for double size */
		dr_dbg(pool->dmn, "Got not aligned memory: %zu last_try: %d\n",
		       log_align_base, fallback);
		if (fallback) {
			align_base = 1UL << log_align_base;
			align_diff = icm_mr->icm_start_addr % align_base;
			/* increase the address to start from aligned size */
			icm_mr->icm_start_addr = icm_mr->icm_start_addr +
				(align_base - align_diff);
			return 0;
		}

		mlx5_free_dm(icm_mr->dm);
		/* retry to allocate, now double the size */
		dm_attr->length = size * 2;
		fallback = true;
		goto alloc_dm;
	}

	return 0;
}

static struct dr_icm_mr *
dr_icm_pool_mr_create(struct dr_icm_pool *pool)
{
	struct ibv_alloc_dm_attr dm_attr = {};
	struct dr_icm_mr *icm_mr;

	icm_mr = calloc(1, sizeof(struct dr_icm_mr));
	if (!icm_mr) {
		errno = ENOMEM;
		return NULL;
	}

	if (dr_icm_allocate_aligned_dm(pool, icm_mr, &dm_attr))
		goto free_icm_mr;

	/* Register device memory */
	icm_mr->mr = ibv_reg_dm_mr(pool->dmn->pd, icm_mr->dm, 0,
				   dm_attr.length,
				   IBV_ACCESS_ZERO_BASED |
				   IBV_ACCESS_REMOTE_WRITE |
				   IBV_ACCESS_LOCAL_WRITE |
				   IBV_ACCESS_REMOTE_READ);
	if (!icm_mr->mr) {
		dr_dbg(pool->dmn, "Failed DM registration\n");
		goto free_dm;
	}

	return icm_mr;

free_dm:
	mlx5_free_dm(icm_mr->dm);
free_icm_mr:
	free(icm_mr);
	return NULL;
}

static  void dr_icm_pool_mr_destroy(struct dr_icm_mr *icm_mr)
{
	ibv_dereg_mr(icm_mr->mr);
	mlx5_free_dm(icm_mr->dm);
	free(icm_mr);
}

static enum dr_icm_type
get_chunk_icm_type(struct dr_icm_chunk *chunk)
{
	return chunk->buddy_mem->pool->icm_type;
}

static int dr_icm_chunk_ste_init(struct dr_icm_chunk *chunk)
{
	chunk->ste_arr = calloc(chunk->num_of_entries, sizeof(struct dr_ste));
	if (!chunk->ste_arr) {
		errno = ENOMEM;
		return errno;
	}

	chunk->hw_ste_arr = calloc(chunk->num_of_entries, DR_STE_SIZE_REDUCED);
	if (!chunk->hw_ste_arr) {
		errno = ENOMEM;
		goto out_free_ste_arr;
	}

	chunk->miss_list = malloc(chunk->num_of_entries *
				  sizeof(struct list_head));
	if (!chunk->miss_list) {
		errno = ENOMEM;
		goto out_free_hw_ste_arr;
	}

	return 0;

out_free_hw_ste_arr:
	free(chunk->hw_ste_arr);
out_free_ste_arr:
	free(chunk->ste_arr);
	return errno;
}

static void dr_icm_chunk_ste_cleanup(struct dr_icm_chunk *chunk)
{
	free(chunk->miss_list);
	free(chunk->hw_ste_arr);
	free(chunk->ste_arr);
}

static void dr_icm_chunk_destroy(struct dr_icm_chunk *chunk)
{
	enum dr_icm_type icm_type = get_chunk_icm_type(chunk);

	list_del(&chunk->chunk_list);

	if (icm_type == DR_ICM_TYPE_STE)
		dr_icm_chunk_ste_cleanup(chunk);

	free(chunk);
}

static int dr_icm_buddy_create(struct dr_icm_pool *pool)
{
	struct dr_icm_buddy_mem *buddy;
	struct dr_icm_mr *icm_mr;

	icm_mr = dr_icm_pool_mr_create(pool);
	if (!icm_mr)
		return ENOMEM;

	buddy = calloc(1, sizeof(*buddy));
	if (!buddy) {
		errno = ENOMEM;
		goto free_mr;
	}

	if (dr_buddy_init(buddy, pool->max_log_chunk_sz))
		goto err_free_buddy;

	buddy->icm_mr = icm_mr;
	buddy->pool = pool;

	/* add it to the -start- of the list in order to search in it first */
	list_add(&pool->buddy_mem_list, &buddy->list_node);

	return 0;

err_free_buddy:
	free(buddy);
free_mr:
	dr_icm_pool_mr_destroy(icm_mr);
	return errno;
}

static void dr_icm_buddy_destroy(struct dr_icm_buddy_mem *buddy)
{
	struct dr_icm_chunk *chunk, *next;

	list_for_each_safe(&buddy->hot_list, chunk, next, chunk_list)
		dr_icm_chunk_destroy(chunk);

	list_for_each_safe(&buddy->used_list, chunk, next, chunk_list)
		dr_icm_chunk_destroy(chunk);

	dr_icm_pool_mr_destroy(buddy->icm_mr);

	dr_buddy_cleanup(buddy);

	free(buddy);
}

static struct dr_icm_chunk *
dr_icm_chunk_create(struct dr_icm_pool *pool,
		    enum dr_icm_chunk_size chunk_size,
		    struct dr_icm_buddy_mem *buddy_mem_pool,
		    int seg)
{
	struct dr_icm_chunk *chunk;
	int offset;

	chunk = calloc(1, sizeof(struct dr_icm_chunk));
	if (!chunk) {
		errno = ENOMEM;
		return NULL;
	}

	offset = dr_icm_pool_dm_type_to_entry_size(pool->icm_type) * seg;

	chunk->rkey = buddy_mem_pool->icm_mr->mr->rkey;
	chunk->mr_addr = (uintptr_t)buddy_mem_pool->icm_mr->mr->addr + offset;
	chunk->icm_addr = (uintptr_t)buddy_mem_pool->icm_mr->icm_start_addr + offset;
	chunk->num_of_entries = dr_icm_pool_chunk_size_to_entries(chunk_size);
	chunk->byte_size = dr_icm_pool_chunk_size_to_byte(chunk_size, pool->icm_type);
	chunk->seg = seg;

	if (pool->icm_type == DR_ICM_TYPE_STE && dr_icm_chunk_ste_init(chunk)) {
		dr_dbg(pool->dmn, "Failed init ste arrays: order: %d\n",
		       chunk_size)
		goto out_free_chunk;
	}

	buddy_mem_pool->used_memory += chunk->byte_size;
	chunk->buddy_mem = buddy_mem_pool;
	list_node_init(&chunk->chunk_list);

	/* chunk now is part of the used_list */
	list_add_tail(&buddy_mem_pool->used_list, &chunk->chunk_list);

	return chunk;

out_free_chunk:
	free(chunk);
	return NULL;
}

static bool dr_icm_pool_is_sync_required(struct dr_icm_pool *pool)
{
	if (pool->hot_memory_size > DR_ICM_SYNC_THRESHOLD_POOL)
		return true;

	return false;
}

static int dr_icm_pool_sync_all_buddy_pools(struct dr_icm_pool *pool)
{
	struct dr_icm_buddy_mem *buddy, *tmp_buddy;
	int err;

	err = dr_devx_sync_steering(pool->dmn->ctx);
	if (err) {
		dr_dbg(pool->dmn, "Failed devx sync hw\n");
		return err;
	}

	list_for_each_safe(&pool->buddy_mem_list, buddy, tmp_buddy, list_node) {
		struct dr_icm_chunk *chunk, *tmp_chunk;

		list_for_each_safe(&buddy->hot_list, chunk, tmp_chunk, chunk_list) {
			dr_buddy_free_mem(buddy, chunk->seg,
					  ilog32(chunk->num_of_entries - 1));
			buddy->used_memory -= chunk->byte_size;
			pool->hot_memory_size -= chunk->byte_size;
			dr_icm_chunk_destroy(chunk);
		}

		if ((pool->dmn->flags & DR_DOMAIN_FLAG_MEMORY_RECLAIM) &&
		    pool->icm_type == DR_ICM_TYPE_STE && !buddy->used_memory)
			dr_icm_buddy_destroy(buddy);
	}

	return 0;
}

static int dr_icm_handle_buddies_get_mem(struct dr_icm_pool *pool,
					 enum dr_icm_chunk_size chunk_size,
					 struct dr_icm_buddy_mem **buddy,
					 int *seg)
{
	struct dr_icm_buddy_mem *buddy_mem_pool;
	bool new_mem = false;
	int err = 0;

	*seg = -1;

	/* find the next free place from the buddy list */
	while (*seg == -1) {
		list_for_each(&pool->buddy_mem_list, buddy_mem_pool, list_node) {
			*seg = dr_buddy_alloc_mem(buddy_mem_pool, chunk_size);
			if (*seg != -1)
				goto found;

			if (new_mem) {
				/* We have new memory pool, first in the list */
				assert(false);
				dr_dbg(pool->dmn, "No memory for order: %d\n",
				       chunk_size);
				errno = ENOMEM;
				err = ENOMEM;
				goto out;
			}
		}
		/* no more available allocators in that pool, create new */
		err = dr_icm_buddy_create(pool);
		if (err)
			goto out;
		/* mark we have new memory, first in list */
		new_mem = true;
	}

found:
	*buddy = buddy_mem_pool;
out:
	return err;
}

/* Allocate an ICM chunk, each chunk holds a piece of ICM memory and
 * also memory used for HW STE management for optimisations.
 */
struct dr_icm_chunk *dr_icm_alloc_chunk(struct dr_icm_pool *pool,
					enum dr_icm_chunk_size chunk_size)
{
	struct dr_icm_buddy_mem *buddy;
	struct dr_icm_chunk *chunk = NULL;
	int ret;
	int seg;

	if (chunk_size > pool->max_log_chunk_sz) {
		errno = EINVAL;
		return NULL;
	}

	pthread_mutex_lock(&pool->mutex);
	/* find mem, get back the relevant buddy pool and seg in that mem */
	ret = dr_icm_handle_buddies_get_mem(pool, chunk_size, &buddy, &seg);
	if (ret)
		goto out;

	chunk = dr_icm_chunk_create(pool, chunk_size, buddy, seg);
	if (!chunk)
		goto out_err;

	goto out;

out_err:
	dr_buddy_free_mem(buddy, seg, chunk_size);
out:
	pthread_mutex_unlock(&pool->mutex);
	return chunk;
}

void dr_icm_free_chunk(struct dr_icm_chunk *chunk)
{
	struct dr_icm_buddy_mem *buddy = chunk->buddy_mem;

	/* move the memory to the waiting list AKA "hot" */
	pthread_mutex_lock(&buddy->pool->mutex);
	list_del_init(&chunk->chunk_list);
	list_add_tail(&buddy->hot_list, &chunk->chunk_list);
	buddy->pool->hot_memory_size += chunk->byte_size;

	/* Check if we have chunks that are waiting for sync-ste */
	if (dr_icm_pool_is_sync_required(buddy->pool))
		dr_icm_pool_sync_all_buddy_pools(buddy->pool);

	pthread_mutex_unlock(&buddy->pool->mutex);
}

struct dr_icm_pool *dr_icm_pool_create(struct mlx5dv_dr_domain *dmn,
				       enum dr_icm_type icm_type)
{
	struct dr_icm_pool *pool;

	pool = calloc(1, sizeof(struct dr_icm_pool));
	if (!pool) {
		errno = ENOMEM;
		return NULL;
	}

	switch (icm_type) {
	case DR_ICM_TYPE_STE:
		pool->max_log_chunk_sz = dmn->info.max_log_sw_icm_sz;
		break;
	case DR_ICM_TYPE_MODIFY_ACTION:
		pool->max_log_chunk_sz = dmn->info.max_log_action_icm_sz;
		break;
	case DR_ICM_TYPE_MODIFY_HDR_PTRN:
		pool->max_log_chunk_sz = dmn->info.max_log_modify_hdr_pattern_icm_sz;
		break;
	default:
		assert(false);
	}

	pool->dmn = dmn;
	pool->icm_type = icm_type;

	list_head_init(&pool->buddy_mem_list);

	pthread_mutex_init(&pool->mutex, NULL);

	return pool;
}

void dr_icm_pool_destroy(struct dr_icm_pool *pool)
{
	struct dr_icm_buddy_mem *buddy, *tmp_buddy;

	list_for_each_safe(&pool->buddy_mem_list, buddy, tmp_buddy, list_node)
		dr_icm_buddy_destroy(buddy);

	pthread_mutex_destroy(&pool->mutex);

	free(pool);
}

/* argument pool area */
struct dr_arg_pool {
	enum dr_arg_chunk_size	log_chunk_size;
	struct mlx5dv_dr_domain	*dmn;
	struct list_head	free_list;
	pthread_mutex_t		mutex;
};

struct dr_arg_pool_mngr {
	struct mlx5dv_dr_domain *dmn;
	struct dr_arg_pool *pools[DR_ARG_CHUNK_SIZE_MAX];
};

static int dr_arg_add_new_objects_to_pool(struct dr_arg_pool *pool)
{
	struct dr_arg_object *arg_obj, *tmp_arg;
	struct mlx5dv_devx_obj *devx_obj;
	uint16_t object_range;
	LIST_HEAD(cur_list);
	int num_of_objects;
	int i;

	object_range =
		pool->dmn->info.caps.log_header_modify_argument_granularity;

	num_of_objects = (1 << (object_range - pool->log_chunk_size));
	/* Only one devX object per range */
	devx_obj = dr_devx_create_modify_header_arg(pool->dmn->ctx,
						    object_range,
						    pool->dmn->pd_num);
	if (!devx_obj) {
		dr_dbg(pool->dmn, "failed allocating object with range: %d:\n",
		       object_range);
		return errno;
	}

	for (i = 0; i < num_of_objects; i++) {
		arg_obj = calloc(1, sizeof(struct dr_arg_pool));
		if (!arg_obj) {
			errno = ENOMEM;
			goto clean_arg_obj;
		}

		arg_obj->log_chunk_size = pool->log_chunk_size;

		list_add_tail(&cur_list, &arg_obj->list_node);

		arg_obj->obj = devx_obj;
		arg_obj->obj_offset = i * (1 << pool->log_chunk_size);
	}
	list_append_list(&pool->free_list, &cur_list);

	return 0;

clean_arg_obj:
	mlx5dv_devx_obj_destroy(devx_obj);
	list_for_each_safe(&cur_list, arg_obj, tmp_arg, list_node) {
		list_del(&arg_obj->list_node);
		free(arg_obj);
	}
	return errno;
}

static struct dr_arg_pool* dr_arg_pool_create(struct mlx5dv_dr_domain *dmn,
					      enum dr_arg_chunk_size chunk_size)
{
	struct dr_arg_pool *pool;

	pool = calloc(1, sizeof(struct dr_arg_pool));
	if (!pool) {
		errno = ENOMEM;
		return NULL;
	}

	pool->dmn = dmn;

	list_head_init(&pool->free_list);
	pthread_mutex_init(&pool->mutex, NULL);

	pool->log_chunk_size = chunk_size;
	if (dr_arg_add_new_objects_to_pool(pool))
		goto free_pool;

	return pool;

free_pool:
	free(pool);

	return NULL;
}

static void dr_arg_pool_destroy(struct dr_arg_pool *pool)
{
	struct dr_arg_object *tmp_arg;
	struct dr_arg_object *arg_obj;

	list_for_each_safe(&pool->free_list, arg_obj, tmp_arg, list_node) {
		list_del(&arg_obj->list_node);
		if (!arg_obj->obj_offset) /* the first in range */
			mlx5dv_devx_obj_destroy(arg_obj->obj);
		free(arg_obj);
	}

	pthread_mutex_destroy(&pool->mutex);

	free(pool);
}

static struct dr_arg_object* dr_arg_get_obj_from_pool(struct dr_arg_pool *pool)
{
	struct dr_arg_object *arg_obj = NULL;
	int ret;

	pthread_mutex_lock(&pool->mutex);
	if (list_empty(&pool->free_list)) {
		ret = dr_arg_add_new_objects_to_pool(pool);
		if (ret)
			goto out;
	}

	arg_obj = list_pop(&pool->free_list, struct dr_arg_object, list_node);
	if (!arg_obj)
		assert(false);

out:
	pthread_mutex_unlock(&pool->mutex);
	return arg_obj;
}

static void dr_arg_put_obj_in_pool(struct dr_arg_pool *pool,
				   struct dr_arg_object *arg_obj)
{
	pthread_mutex_lock(&pool->mutex);
	list_add(&pool->free_list, &arg_obj->list_node);
	pthread_mutex_unlock(&pool->mutex);
}

void dr_arg_put_obj(struct mlx5dv_dr_domain *dmn,
		    struct dr_arg_object *arg_obj)
{
	return dr_arg_put_obj_in_pool(dmn->modify_header_arg_pool_mngr->pools[arg_obj->log_chunk_size],
				      arg_obj);
}

struct dr_arg_object* dr_arg_get_obj(struct mlx5dv_dr_domain *dmn,
				     enum dr_arg_chunk_size size)
{
	if (size >= DR_ARG_CHUNK_SIZE_MAX)
		return NULL;

	return dr_arg_get_obj_from_pool(dmn->modify_header_arg_pool_mngr->pools[size]);
}

uint32_t dr_arg_get_object_id(struct dr_arg_object *arg_obj)
{
	return (arg_obj->obj->object_id + arg_obj->obj_offset);
}

struct dr_arg_pool_mngr* dr_arg_pool_mngr_create(struct mlx5dv_dr_domain *dmn)
{
	struct dr_arg_pool_mngr *pool_mngr;
	int i;

	pool_mngr = calloc(1, sizeof(struct dr_arg_pool_mngr));
	if (!pool_mngr) {
		errno = ENOMEM;
		return NULL;
	}

	pool_mngr->dmn = dmn;

	for (i = 0; i < DR_ARG_CHUNK_SIZE_MAX; i++) {
		pool_mngr->pools[i] = dr_arg_pool_create(dmn, i);
		if (!pool_mngr->pools[i])
			goto clean_pools;
	}

	return pool_mngr;

clean_pools:
	for (; i > 0; i--) {
		dr_arg_pool_destroy(pool_mngr->pools[i]);
	}
	free(pool_mngr);
	return NULL;
}

void dr_arg_pool_mngr_destroy(struct dr_arg_pool_mngr *pool_mngr)
{
	int i;

	for (i = 0; i < DR_ARG_CHUNK_SIZE_MAX; i++)
		dr_arg_pool_destroy(pool_mngr->pools[i]);

	free(pool_mngr);
}
