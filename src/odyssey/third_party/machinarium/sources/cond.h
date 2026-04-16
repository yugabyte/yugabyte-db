#ifndef MM_COND_H
#define MM_COND_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_cond mm_cond_t;

struct mm_cond {
	uint64_t signal;
	mm_call_t call;
	mm_cond_t *propagate;
};

static inline void mm_cond_init(mm_cond_t *cond)
{
	cond->propagate = NULL;
	cond->signal = 0;
	memset(&cond->call, 0, sizeof(cond->call));
}

static inline void mm_cond_signal(mm_cond_t *cond, mm_scheduler_t *sched)
{
	if (cond->propagate)
		mm_cond_signal(cond->propagate, sched);
	if (cond->signal)
		return;
	cond->signal = 1;
	if (cond->call.type == MM_CALL_COND)
		mm_scheduler_wakeup(sched, cond->call.coroutine);
}

static inline int mm_cond_try(mm_cond_t *cond)
{
	int signal = cond->signal;
	if (signal)
		cond->signal = 0;
	return signal;
}

static inline int mm_cond_wait(mm_cond_t *cond, uint32_t time_ms)
{
	if (cond->signal) {
		cond->signal = 0;
		return 0;
	}
	mm_call(&cond->call, MM_CALL_COND, time_ms);
	if (cond->call.status != 0)
		return -1;
	return 0;
}

#endif /* MM_COND_H */
