#ifndef __TOKEN__H

#define __TOKEN__H

#define NR_BUCKET 16

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct bucket {
	/* last accessed time */
	uint64_t time;
};

struct token_bucket {
	struct bucket buckets[NR_BUCKET];

	uint64_t rate;
	uint64_t burst_size;

	volatile double time_per_token;
	volatile double time_per_burst;

	int next_bucket;
};

#ifdef __cplusplus
extern "C" {
#endif

int init_token_bucket(struct token_bucket *tb, uint64_t rate, uint64_t burst_size);
void wait_for_token(const uint64_t tokens, int start);
void wait_for_request_token(int start);

#ifdef __cplusplus
}
#endif

bool consume(struct token_bucket *tb, const uint64_t tokens, int start) __attribute__((optimize("-O3")));

static inline double per_bucket_rate(uint64_t rate)
{
	return (double) rate / NR_BUCKET;
}

static inline void set_rate(struct token_bucket *tb, uint64_t rate)
{
	tb->rate = rate;
	tb->time_per_token = (double) 1000000000 / per_bucket_rate(rate);
	tb->time_per_burst = ((double) tb->burst_size / NR_BUCKET) * tb->time_per_token;
}

static inline void up_rate(struct token_bucket *tb, uint64_t rate)
{
	tb->rate = tb->rate + rate;
	tb->time_per_token = (double) 1000000000 / per_bucket_rate(tb->rate);
	tb->time_per_burst = ((double) tb->burst_size / NR_BUCKET) * tb->time_per_token;
}

static inline void down_rate(struct token_bucket *tb, uint64_t rate)
{
	tb->rate = tb->rate - rate;
	tb->time_per_token = (double) 1000000000 / per_bucket_rate(tb->rate);
	tb->time_per_burst = ((double) tb->burst_size / NR_BUCKET) * tb->time_per_token;
}

static inline int get_base_bucket(struct token_bucket *tb)
{
	tb->next_bucket = (tb->next_bucket + 1) % NR_BUCKET;
	return tb->next_bucket;
}

static inline uint64_t get_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t) ts.tv_sec * 1000000000 + (uint64_t) ts.tv_nsec;
}

#endif
