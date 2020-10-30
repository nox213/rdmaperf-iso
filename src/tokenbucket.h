#ifndef __TOKEN__H

#define __TOKEN__H

#define BURST_PER_QP 4000000   // in bytes
#define MAX_RATE 400000000 /* 7GB */

#include <time.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

struct token_bucket {
	uint64_t time;
	uint64_t time_per_token;
	uint64_t time_per_burst;
};

int init_token_bucket(struct token_bucket *tb, uint64_t rate, uint64_t burst_size);
bool consume(struct token_bucket *tb, const uint64_t tokens) __attribute__((optimize("-O3")));

static inline uint64_t get_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return (uint64_t) ts.tv_sec * 1000000000 + (uint64_t) ts.tv_nsec;
}

#endif
