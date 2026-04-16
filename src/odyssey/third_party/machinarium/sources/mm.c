
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static int machinarium_stack_size = 0;
static int machinarium_pool_size = 0;
static int machinarium_coroutine_cache_size = 0;
static int machinarium_msg_cache_gc_size = 0;
static int machinarium_initialized = 0;
mm_t machinarium;

static inline size_t machinarium_page_size(void)
{
	return sysconf(_SC_PAGESIZE);
}

MACHINE_API void machinarium_set_stack_size(int size)
{
	machinarium_stack_size = size;
}

MACHINE_API void machinarium_set_pool_size(int size)
{
	machinarium_pool_size = size;
}

MACHINE_API void machinarium_set_coroutine_cache_size(int size)
{
	machinarium_coroutine_cache_size = size;
}

MACHINE_API void machinarium_set_msg_cache_gc_size(int size)
{
	machinarium_msg_cache_gc_size = size;
}

MACHINE_API int machinarium_init(void)
{
	if (machinarium_initialized)
		return -1;

	if (machinarium_stack_size <= 0)
		machinarium_stack_size = 4;

	if (machinarium_pool_size == 0)
		machinarium_pool_size = 1;

	machinarium.config.page_size = machinarium_page_size();
	machinarium.config.stack_size = machinarium_stack_size;
	machinarium.config.pool_size = machinarium_pool_size;
	machinarium.config.coroutine_cache_size =
		machinarium_coroutine_cache_size;
	machinarium.config.msg_cache_gc_size = machinarium_msg_cache_gc_size;

	mm_machinemgr_init(&machinarium.machine_mgr);
	mm_tls_engine_init();
	mm_taskmgr_init(&machinarium.task_mgr);
	mm_taskmgr_start(&machinarium.task_mgr, machinarium.config.pool_size);
	machinarium_initialized = 1;
	return 0;
}

MACHINE_API void machinarium_free(void)
{
	if (!machinarium_initialized)
		return;
	mm_taskmgr_stop(&machinarium.task_mgr);
	mm_machinemgr_free(&machinarium.machine_mgr);
	mm_tls_engine_free();
	machinarium_initialized = 0;
}
