#include "tokenbucket.h"

int init_token_bucket(struct token_bucket *tb, uint64_t rate, uint64_t burst_size)
{

	tb->time = 0;
	tb->time_per_token = 1000000000 / rate;
	tb->time_per_burst = burst_size * tb->time_per_token;

	return 0;
}

bool consume(struct token_bucket *tb, const uint64_t tokens)
{
	uint64_t now = get_now();
	uint64_t time_needed = tokens * tb->time_per_token;
	uint64_t min_time = now - tb->time_per_burst;
	uint64_t old_time = tb->time;
	uint64_t new_time = old_time;

	if (min_time > old_time)
		new_time = min_time;

	for (;;) {
		new_time += time_needed;
		if (new_time > now) {
			return false;
		}

		/* gcc extension */
		if (__sync_bool_compare_and_swap(&tb->time, 
					old_time, new_time)) {
			return true;
		}
		old_time = tb->time;
		new_time = old_time;
	}

	return false;
}

void set_rate(struct token_bucket *tb, uint64_t rate)
{
	tb->time_per_token = 1000000000 / rate;
}

