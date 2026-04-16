#ifndef MM_CLOCK_H
#define MM_CLOCK_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_clock mm_clock_t;

struct mm_clock {
	int active;
	int time_cached;
	uint64_t time_ms;
	uint64_t time_us;
	uint64_t time_ns;
	uint32_t time_sec;
	mm_buf_t timers;
	int timers_count;
	int timers_seq;
};

void mm_clock_init(mm_clock_t *);
void mm_clock_free(mm_clock_t *);
void mm_clock_update(mm_clock_t *);
int mm_clock_step(mm_clock_t *);
int mm_clock_timer_add(mm_clock_t *, mm_timer_t *);
int mm_clock_timer_del(mm_clock_t *, mm_timer_t *);

mm_timer_t *mm_clock_timer_min(mm_clock_t *);

static inline void mm_clock_reset(mm_clock_t *clock)
{
	clock->time_cached = 0;
}

static inline void mm_timer_start(mm_clock_t *clock, mm_timer_t *timer)
{
	if (!clock->active)
		mm_clock_update(clock);
	clock->active = 1;
	mm_clock_timer_add(clock, timer);
}

static inline void mm_timer_stop(mm_timer_t *timer)
{
	if (!timer->active)
		return;
	mm_clock_t *clock = timer->clock;
	mm_clock_timer_del(clock, timer);
	if (clock->timers_count == 0)
		clock->active = 0;
}

#endif /* MM_CLOCK_H */
