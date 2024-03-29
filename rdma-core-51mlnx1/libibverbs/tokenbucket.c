#include "tokenbucket.h"
#include "resource_table.h"

extern struct resource *my_res;
extern struct token_bucket *global_tb;
extern struct token_bucket *request_tb;

int init_token_bucket(struct token_bucket *tb, uint64_t rate, uint64_t burst_size)
{
	int i;

	tb->rate = rate;
	tb->time_per_token = (double) 1000000000 / ((double) rate / NR_BUCKET);
	tb->time_per_burst = ((double) burst_size / NR_BUCKET) * tb->time_per_token;
	tb->burst_size = burst_size;
	tb->next_bucket = -1;

	for (i = 0; i < NR_BUCKET; i++)
		tb->buckets[i].time = 0;

	return 0;
}

void wait_for_token(const uint64_t tokens, int start)
{
	int i, iteration;
	uint64_t small_token, rem, small_burst_size;
	
	small_burst_size = (double) global_tb->burst_size / NR_BUCKET;
	if (tokens <= small_burst_size) {
		small_token = tokens;
		rem = 0;
		iteration = 1;
	}
	else {
		small_token = small_burst_size;
		rem = tokens % small_burst_size;
		iteration = tokens / small_burst_size;
	}

	for (i = 0; i < iteration; i++) {
		while (!consume(global_tb, small_token, start))
			start = (start + 1) % NR_BUCKET;
	}
	if (rem) {
		while (!consume(global_tb, rem, start))
			start = (start + 1) % NR_BUCKET;
	}
}

void wait_for_request_token(int start)
{
	while (!consume(request_tb, 1, start))
		start = (start + 1) % NR_BUCKET;
}

bool consume(struct token_bucket *tb, const uint64_t tokens, int bucket)
{
	uint64_t now = get_now();
	uint64_t time_needed = tokens * tb->time_per_token;
	uint64_t min_time = now - tb->time_per_burst;
	uint64_t old_time = tb->buckets[bucket].time;
	uint64_t new_time = old_time;

	if (min_time > old_time)
		new_time = min_time;

	for (;;) {
		new_time += time_needed;
		if (new_time > now) {
			return false;
		}

		/* gcc extension */
		if (__sync_bool_compare_and_swap(&tb->buckets[bucket].time, 
					old_time, new_time)) {
			return true;
		}
		old_time = tb->buckets[bucket].time;
		new_time = old_time;
	}

	return false;
}

