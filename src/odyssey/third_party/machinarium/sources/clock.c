
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static int mm_clock_cmp(mm_timer_t *a, mm_timer_t *b)
{
	if (a->timeout == b->timeout) {
		if (a->seq == b->seq) {
			assert(a == b);
			return 0;
		}
		return (a->seq > b->seq) ? 1 : -1;
	} else if (a->timeout > b->timeout) {
		return 1;
	}
	return -1;
}

void mm_clock_init(mm_clock_t *clock)
{
	mm_buf_init(&clock->timers);
	clock->timers_count = 0;
	clock->timers_seq = 0;
	clock->active = 0;
	clock->time_ms = 0;
	clock->time_ns = 0;
	clock->time_us = 0;
	clock->time_sec = 0;
	clock->time_cached = 0;
}

void mm_clock_free(mm_clock_t *clock)
{
	mm_buf_free(&clock->timers);
}

// Binary search routine for timer in sorted array.
// We expect that there is at most only one occurrence of timer in list.
// If there is not timer in list, we must return first index greater than timer.
static int mm_clock_get_insert_position(mm_timer_t **list, int count,
					mm_timer_t *timer)
{
	int low = 0, high = count;
	while (low < high) {
		int med = low + (high - low) / 2;
		int cmp = mm_clock_cmp(timer, list[med]);
		if (cmp == 0)
			return med;
		if (cmp > 0) {
			low = med + 1;
		} else {
			high = med;
		}
	}
	return low;
}

static int mm_clock_list_is_sorted(mm_timer_t **list, int count)
{
	for (int i = 1; i < count; i++) {
		if (mm_clock_cmp(list[i - 1], list[i]) >= 0) {
			return 0;
		}
	}
	return 1;
}

int mm_clock_timer_add(mm_clock_t *clock, mm_timer_t *timer)
{
	int count = clock->timers_count;
	int rc;
	rc = mm_buf_ensure(&clock->timers, sizeof(mm_timer_t *));
	if (rc == -1)
		return -1;
	mm_timer_t **list;
	list = (mm_timer_t **)clock->timers.start;
	mm_buf_advance(&clock->timers, sizeof(mm_timer_t *));
	timer->seq = clock->timers_seq++;
	timer->timeout = clock->time_ms + timer->interval;
	timer->active = 1;
	timer->clock = clock;
	int insert_position = mm_clock_get_insert_position(list, count, timer);
	// We are last, or insert position is after us
	assert((insert_position == count) ||
	       mm_clock_cmp(list[insert_position], timer) > 0);
	memmove(list + insert_position + 1, list + insert_position,
		sizeof(mm_timer_t *) * (count - insert_position));
	list[insert_position] = timer;
	clock->timers_count = count + 1;
	assert(mm_clock_list_is_sorted(list, clock->timers_count));
	return 0;
}

int mm_clock_timer_del(mm_clock_t *clock, mm_timer_t *timer)
{
	if (!timer->active)
		return -1;
	assert(clock->timers_count >= 1);
	mm_timer_t **list;
	list = (mm_timer_t **)clock->timers.start;
	int i = mm_clock_get_insert_position(list, clock->timers_count, timer);
	// We should find timer
	assert(list[i] == timer);
	for (; i < clock->timers_count; i++) {
		if (list[i] != timer)
			continue;
		memmove(list + i, list + i + 1,
			sizeof(mm_timer_t *) * (clock->timers_count - i - 1));
		clock->timers.pos -= sizeof(mm_timer_t *);
		clock->timers_count -= 1;
		break;
	}
	assert(mm_clock_list_is_sorted(list, clock->timers_count));
	timer->active = 0;
	return 0;
}

mm_timer_t *mm_clock_timer_min(mm_clock_t *clock)
{
	if (clock->timers_count == 0)
		return NULL;
	mm_timer_t **list;
	list = (mm_timer_t **)clock->timers.start;
	return list[0];
}

int mm_clock_step(mm_clock_t *clock)
{
	if (clock->timers_count == 0)
		return 0;
	mm_timer_t **list;
	list = (mm_timer_t **)clock->timers.start;
	int timers_hit = 0;
	int i = 0;
	for (; i < clock->timers_count; i++) {
		mm_timer_t *timer = list[i];
		if (timer->timeout > clock->time_ms)
			break;
		timer->callback(timer);
		timer->active = 0;
		timers_hit++;
	}
	if (!timers_hit)
		return 0;
	int timers_left = clock->timers_count - timers_hit;
	if (timers_left == 0) {
		mm_buf_reset(&clock->timers);
		clock->timers_count = 0;
		return timers_hit;
	}
	memmove(list, list + timers_hit, sizeof(mm_timer_t *) * timers_left);
	clock->timers.pos -= sizeof(mm_timer_t *) * timers_hit;
	clock->timers_count -= timers_hit;
	return timers_hit;
}

static uint64_t mm_clock_gettime(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * (uint64_t)1e9 + t.tv_nsec;
}

static uint32_t mm_clock_gettimeofday(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec;
}

void mm_clock_update(mm_clock_t *clock)
{
	if (clock->time_cached)
		return;
	clock->time_ns = mm_clock_gettime();
	clock->time_ms = clock->time_ns / 1000000;
	clock->time_us = clock->time_ns / 1000;
	clock->time_sec = mm_clock_gettimeofday();
	clock->time_cached = 1;
}
