
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

MACHINE_API machine_iov_t *machine_iov_create(void)
{
	mm_iov_t *iov = malloc(sizeof(mm_iov_t));
	if (iov == NULL) {
		mm_errno_set(ENOMEM);
		return NULL;
	}
	mm_iov_init(iov);
	return (machine_iov_t *)iov;
}

MACHINE_API void machine_iov_free(machine_iov_t *obj)
{
	mm_iov_t *iov = mm_cast(mm_iov_t *, obj);
	mm_iov_free(iov);
	free(obj);
}

MACHINE_API int machine_iov_add_pointer(machine_iov_t *obj, void *pointer,
					int size)
{
	mm_iov_t *iov = mm_cast(mm_iov_t *, obj);
	int rc;
	rc = mm_iov_add_pointer(iov, pointer, size);
	if (rc == -1)
		mm_errno_set(ENOMEM);
	return rc;
}

MACHINE_API int machine_iov_add(machine_iov_t *obj, machine_msg_t *msg)
{
	mm_iov_t *iov = mm_cast(mm_iov_t *, obj);
	int rc;
	rc = mm_iov_add(iov, (mm_msg_t *)msg);
	if (rc == -1)
		mm_errno_set(ENOMEM);
	return rc;
}

MACHINE_API int machine_iov_pending(machine_iov_t *obj)
{
	mm_iov_t *iov = mm_cast(mm_iov_t *, obj);
	return mm_iov_pending(iov);
}
