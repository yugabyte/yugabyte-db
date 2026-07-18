#ifndef MM_CALL_H
#define MM_CALL_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_call mm_call_t;

typedef void (*mm_cancel_t)(void *, void *arg);

typedef enum {
	MM_CALL_NONE,
	MM_CALL_SIGNAL,
	MM_CALL_EVENT,
	MM_CALL_SLEEP,
	MM_CALL_COND,
	MM_CALL_CHANNEL,
	MM_CALL_CONNECT,
	MM_CALL_ACCEPT,
	MM_CALL_HANDSHAKE
} mm_calltype_t;

struct mm_call {
	mm_calltype_t type;
	mm_coroutine_t *coroutine;
	mm_timer_t timer;
	mm_cancel_t cancel_function;
	void *arg;
	void *data;
	int timedout;
	int status;
};

void mm_call(mm_call_t *, mm_calltype_t, uint32_t);

static inline int mm_call_is(mm_call_t *call, mm_calltype_t type)
{
	return call->type == type;
}

static inline int mm_call_is_active(mm_call_t *call)
{
	return call->type != MM_CALL_NONE;
}

static inline int mm_call_is_aborted(mm_call_t *call)
{
	return mm_call_is_active(call) && call->status != 0;
}

static inline void mm_call_cancel(mm_call_t *call, void *object)
{
	if (!mm_call_is_active(call))
		return;
	call->cancel_function(object, call->arg);
}

#endif /* MM_CALL_H */
