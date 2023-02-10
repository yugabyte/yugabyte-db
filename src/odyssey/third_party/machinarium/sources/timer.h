#ifndef MM_TIMER_H
#define MM_TIMER_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_timer mm_timer_t;

typedef void (*mm_timer_callback_t)(mm_timer_t *);

struct mm_timer {
	int active;
	uint64_t timeout;
	uint32_t interval;
	int seq;
	mm_timer_callback_t callback;
	void *arg;
	void *clock;
};

static inline void mm_timer_init(mm_timer_t *timer, mm_timer_callback_t cb,
				 void *arg, uint32_t interval)
{
	timer->active = 0;
	timer->interval = interval;
	timer->timeout = 0;
	timer->seq = 0;
	timer->callback = cb;
	timer->arg = arg;
	timer->clock = NULL;
}

#endif /* MM_TIMER_H */
