
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

__thread mm_machine_t *mm_self = NULL;

static int mm_idle_cb(mm_idle_t *handle)
{
	(void)handle;
	mm_scheduler_run(&mm_self->scheduler, &mm_self->coroutine_cache);
	return mm_scheduler_online(&mm_self->scheduler);
}

static inline void machine_free(mm_machine_t *machine)
{
	/* todo: check active timers and other allocated
	 *       resources */
	mm_msgcache_free(&machine->msg_cache);
	mm_coroutine_cache_free(&machine->coroutine_cache);
	mm_eventmgr_free(&machine->event_mgr, &machine->loop);
	mm_signalmgr_free(&machine->signal_mgr, &machine->loop);
	// mm_loop_shutdown(&machine->loop);
	mm_scheduler_free(&machine->scheduler);
}

static void *machine_main(void *arg)
{
	mm_machine_t *machine = arg;
	mm_self = machine;

	mm_thread_disable_cancel();

	/* set thread name */
	if (machine->name)
		mm_thread_set_name(&machine->thread, machine->name);

	mm_lrand48_seed();

	/* create main coroutine */
	int64_t id;
	id = machine_coroutine_create(machine->main, machine->main_arg);
	(void)id;

	/* run main loop */
	machine->online = 1;
	for (;;) {
		if (!(mm_scheduler_online(&machine->scheduler) &&
		      machine->online))
			break;
		mm_loop_step(&machine->loop);
	}

	machine->online = 0;
	machine_free(machine);
	return NULL;
}

MACHINE_API int64_t machine_create(char *name, machine_coroutine_t function,
				   void *arg)
{
	mm_machine_t *machine;
	machine = malloc(sizeof(*machine));
	if (machine == NULL)
		return -1;
	machine->online = 0;
	machine->id = 0;
	machine->main = function;
	machine->main_arg = arg;
	machine->server_tls_ctx = NULL;
	machine->client_tls_ctx = NULL;
	machine->name = NULL;
	if (name) {
		machine->name = strdup(name);
		if (machine->name == NULL) {
			free(machine);
			return -1;
		}
	}
	mm_list_init(&machine->link);

	mm_msgcache_init(&machine->msg_cache);
	mm_msgcache_set_gc_watermark(&machine->msg_cache,
				     machinarium.config.msg_cache_gc_size);

	mm_coroutine_cache_init(&machine->coroutine_cache,
				machinarium.config.stack_size *
					machinarium.config.page_size,
				machinarium.config.page_size,
				machinarium.config.coroutine_cache_size);

	mm_scheduler_init(&machine->scheduler);
	int rc;
	rc = mm_loop_init(&machine->loop);
	if (rc < 0) {
		mm_scheduler_free(&machine->scheduler);
		free(machine);
		return -1;
	}
	mm_loop_set_idle(&machine->loop, mm_idle_cb, NULL);
	rc = mm_eventmgr_init(&machine->event_mgr, &machine->loop);
	if (rc == -1) {
		mm_loop_shutdown(&machine->loop);
		mm_scheduler_free(&machine->scheduler);
		free(machine);
		return -1;
	}
	rc = mm_signalmgr_init(&machine->signal_mgr, &machine->loop);
	if (rc == -1) {
		mm_eventmgr_free(&machine->event_mgr, &machine->loop);
		mm_loop_shutdown(&machine->loop);
		mm_scheduler_free(&machine->scheduler);
		free(machine);
		return -1;
	}
	mm_machinemgr_add(&machinarium.machine_mgr, machine);
	rc = mm_thread_create(&machine->thread, PTHREAD_STACK_MIN, machine_main,
			      machine);
	if (rc == -1) {
		mm_machinemgr_delete(&machinarium.machine_mgr, machine);
		mm_eventmgr_free(&machine->event_mgr, &machine->loop);
		mm_loop_shutdown(&machine->loop);
		mm_scheduler_free(&machine->scheduler);
		free(machine);
		return -1;
	}
	return machine->id;
}

MACHINE_API int machine_wait(uint64_t machine_id)
{
	mm_machine_t *machine;
	machine = mm_machinemgr_delete_by_id(&machinarium.machine_mgr,
					     machine_id);
	if (machine == NULL)
		return -1;
	int rc;
	rc = mm_thread_join(&machine->thread);
	if (machine->name) {
		free(machine->name);
	}

	free(machine);
	return rc;
}

MACHINE_API int machine_stop(uint64_t machine_id)
{
	mm_machine_t *machine;
	machine = mm_machinemgr_delete_by_id(&machinarium.machine_mgr,
					     machine_id);
	if (machine == NULL)
		return -1;
	machine->online = 0;
	return 0;
}

MACHINE_API int machine_active(void)
{
	return mm_self->online;
}

MACHINE_API uint64_t machine_self(void)
{
	return mm_self->id;
}

MACHINE_API void **machine_thread_private(void)
{
	return &(mm_self->thread_global_private);
}

MACHINE_API void machine_stop_current(void)
{
	mm_self->online = 0;
}

MACHINE_API int64_t machine_coroutine_create(machine_coroutine_t function,
					     void *arg)
{
	mm_errno_set(0);
	mm_coroutine_t *coroutine;
	coroutine = mm_coroutine_cache_pop(&mm_self->coroutine_cache);
	if (coroutine == NULL) {
		mm_errno_set(ENOMEM);
		return -1;
	}
	mm_scheduler_new(&mm_self->scheduler, coroutine, function, arg);
	return coroutine->id;
}

MACHINE_API void machine_sleep(uint32_t time_ms)
{
	mm_errno_set(0);
	mm_call_t call;
	mm_call(&call, MM_CALL_SLEEP, time_ms);
}

MACHINE_API int machine_join(uint64_t coroutine_id)
{
	mm_errno_set(0);
	mm_coroutine_t *coroutine;
	coroutine = mm_scheduler_find(&mm_self->scheduler, coroutine_id);
	if (coroutine == NULL) {
		mm_errno_set(ENOENT);
		return -1;
	}
	mm_coroutine_t *waiter;
	waiter = mm_scheduler_current(&mm_self->scheduler);
	mm_scheduler_join(coroutine, waiter);
	mm_scheduler_yield(&mm_self->scheduler);
	return 0;
}

MACHINE_API int machine_cancel(uint64_t coroutine_id)
{
	mm_errno_set(0);
	mm_coroutine_t *coroutine;
	coroutine = mm_scheduler_find(&mm_self->scheduler, coroutine_id);
	if (coroutine == NULL) {
		mm_errno_set(ENOENT);
		return -1;
	}
	mm_coroutine_cancel(coroutine);
	return 0;
}

MACHINE_API int machine_cancelled(void)
{
	mm_coroutine_t *coroutine;
	coroutine = mm_scheduler_current(&mm_self->scheduler);
	if (coroutine == NULL)
		return -1;
	return coroutine->cancel > 0;
}

MACHINE_API int machine_timedout(void)
{
	return mm_errno_get() == ETIMEDOUT;
}

MACHINE_API int machine_errno(void)
{
	return mm_errno_get();
}

MACHINE_API uint64_t machine_time_ms(void)
{
	mm_clock_update(&mm_self->loop.clock);
	return mm_self->loop.clock.time_ms;
}

MACHINE_API uint64_t machine_time_us(void)
{
	mm_clock_update(&mm_self->loop.clock);
	return mm_self->loop.clock.time_us;
}

MACHINE_API uint32_t machine_timeofday_sec(void)
{
	mm_clock_update(&mm_self->loop.clock);
	return mm_self->loop.clock.time_sec;
}

MACHINE_API void
machine_stat(uint64_t *coroutine_count, uint64_t *coroutine_cache_count,
	     uint64_t *msg_allocated, uint64_t *msg_cache_count,
	     uint64_t *msg_cache_gc_count, uint64_t *msg_cache_size)
{
	mm_coroutine_cache_stat(&mm_self->coroutine_cache, coroutine_count,
				coroutine_cache_count);

	mm_msgcache_stat(&mm_self->msg_cache, msg_allocated, msg_cache_gc_count,
			 msg_cache_count, msg_cache_size);
}
