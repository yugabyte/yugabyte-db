
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

MACHINE_API machine_cond_t *machine_cond_create(void)
{
	mm_cond_t *cond = malloc(sizeof(mm_cond_t));
	if (cond == NULL) {
		mm_errno_set(ENOMEM);
		return NULL;
	}
	mm_cond_init(cond);
	return (machine_cond_t *)cond;
}

MACHINE_API void machine_cond_free(machine_cond_t *obj)
{
	free(obj);
}

MACHINE_API void machine_cond_propagate(machine_cond_t *obj,
					machine_cond_t *prop)
{
	mm_cond_t *cond = mm_cast(mm_cond_t *, obj);
	mm_cond_t *propagate = mm_cast(mm_cond_t *, prop);
	cond->propagate = propagate;
	if (propagate && cond->signal)
		mm_cond_signal(propagate, &mm_self->scheduler);
}

MACHINE_API void machine_cond_signal(machine_cond_t *obj)
{
	mm_cond_t *cond = mm_cast(mm_cond_t *, obj);
	mm_cond_signal(cond, &mm_self->scheduler);
}

MACHINE_API int machine_cond_try(machine_cond_t *obj)
{
	mm_cond_t *cond = mm_cast(mm_cond_t *, obj);
	return mm_cond_try(cond);
}

MACHINE_API int machine_cond_wait(machine_cond_t *obj, uint32_t time_ms)
{
	mm_cond_t *cond = mm_cast(mm_cond_t *, obj);
	mm_errno_set(0);
	if (cond->call.type != MM_CALL_NONE) {
		mm_errno_set(EINPROGRESS);
		return -1;
	}
	return mm_cond_wait(cond, time_ms);
}
