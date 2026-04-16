
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

void mm_coroutine_init(mm_coroutine_t *coroutine)
{
	memset(coroutine, 0, sizeof(mm_coroutine_t));
	coroutine->id = UINT64_MAX;
	coroutine->state = MM_CNEW;
	coroutine->errno_ = 0;
	coroutine->call_ptr = NULL;
	mm_list_init(&coroutine->joiners);
	mm_list_init(&coroutine->link);
	mm_list_init(&coroutine->link_join);
}

mm_coroutine_t *mm_coroutine_allocate(int stack_size, int stack_size_guard)
{
	mm_coroutine_t *coroutine;
	coroutine = malloc(sizeof(mm_coroutine_t));
	if (coroutine == NULL)
		return NULL;
	mm_coroutine_init(coroutine);
	int rc;
	rc = mm_contextstack_create(&coroutine->stack, stack_size,
				    stack_size_guard);
	if (rc == -1) {
		free(coroutine);
		return NULL;
	}
	return coroutine;
}

void mm_coroutine_free(mm_coroutine_t *coroutine)
{
	mm_contextstack_free(&coroutine->stack);
	free(coroutine);
}

void mm_coroutine_cancel(mm_coroutine_t *coroutine)
{
	if (coroutine->cancel)
		return;
	coroutine->cancel++;
	if (coroutine->call_ptr)
		mm_call_cancel(coroutine->call_ptr, coroutine);
}
