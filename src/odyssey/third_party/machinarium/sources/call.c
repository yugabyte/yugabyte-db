
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static void mm_call_timer_cb(mm_timer_t *handle)
{
	mm_call_t *call = handle->arg;
	call->timedout = 1;
	call->status = ETIMEDOUT;
	if (call->coroutine)
		mm_scheduler_wakeup(&mm_self->scheduler, call->coroutine);
}

static void mm_call_cancel_cb(void *obj, void *arg)
{
	mm_call_t *call = arg;
	(void)obj;
	call->status = ECANCELED;
	if (call->coroutine)
		mm_scheduler_wakeup(&mm_self->scheduler, call->coroutine);
}

void mm_call(mm_call_t *call, mm_calltype_t type, uint32_t time_ms)
{
	mm_scheduler_t *scheduler;
	scheduler = &mm_self->scheduler;
	mm_clock_t *clock;
	clock = &mm_self->loop.clock;

	mm_coroutine_t *coroutine;
	coroutine = mm_scheduler_current(scheduler);

	coroutine->call_ptr = call;
	call->coroutine = coroutine;
	call->type = type;
	call->cancel_function = mm_call_cancel_cb;
	call->arg = call;
	call->timedout = 0;
	call->status = 0;
	if (mm_coroutine_is_cancelled(coroutine)) {
		call->status = ECANCELED;
		call->type = MM_CALL_NONE;
		call->timedout = 0;
		call->coroutine = NULL;
		call->cancel_function = NULL;
		call->arg = NULL;
		coroutine->call_ptr = NULL;
		return;
	}

	mm_timer_init(&call->timer, mm_call_timer_cb, call, time_ms);
	if (time_ms != UINT32_MAX)
		mm_timer_start(clock, &call->timer);
	mm_scheduler_yield(scheduler);
	mm_timer_stop(&call->timer);

	call->type = MM_CALL_NONE;
	call->coroutine = NULL;
	call->cancel_function = NULL;
	call->arg = NULL;
	coroutine->call_ptr = NULL;

	mm_errno_set(call->status);
}
