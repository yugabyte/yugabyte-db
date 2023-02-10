
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

MACHINE_API machine_msg_t *machine_msg_create(int reserve)
{
	mm_msg_t *msg = mm_msgcache_pop(&mm_self->msg_cache);
	if (msg == NULL) {
		return NULL;
	}

	msg->type = 0;
	if (reserve > 0) {
		int rc;
		rc = mm_buf_ensure(&msg->data, reserve);
		if (rc == -1) {
			mm_msg_unref(&mm_self->msg_cache, msg);
			return NULL;
		}
		mm_buf_advance(&msg->data, reserve);
	}
	return (machine_msg_t *)msg;
}

MACHINE_API machine_msg_t *machine_msg_create_or_advance(machine_msg_t *obj,
							 int size)
{
	if (obj == NULL) {
		return machine_msg_create(size);
	}

	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	int rc;
	rc = mm_buf_ensure(&msg->data, size);
	if (rc == -1) {
		return NULL;
	}

	mm_buf_advance(&msg->data, size);
	return obj;
}

MACHINE_API inline void machine_msg_free(machine_msg_t *obj)
{
	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	mm_msgcache_push(&mm_self->msg_cache, msg);
}

MACHINE_API inline void machine_msg_set_type(machine_msg_t *obj, int type)
{
	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	msg->type = type;
}

MACHINE_API inline int machine_msg_type(machine_msg_t *obj)
{
	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	return msg->type;
}

MACHINE_API inline void *machine_msg_data(machine_msg_t *obj)
{
	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	return msg->data.start;
}

MACHINE_API int machine_msg_size(machine_msg_t *obj)
{
	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	return mm_buf_used(&msg->data);
}

MACHINE_API int machine_msg_write(machine_msg_t *obj, void *buf, int size)
{
	mm_msg_t *msg = mm_cast(mm_msg_t *, obj);
	int rc;
	if (buf == NULL) {
		rc = mm_buf_ensure(&msg->data, size);
		if (rc == -1)
			return -1;
		mm_buf_advance(&msg->data, size);
		return 0;
	}
	rc = mm_buf_add(&msg->data, buf, size);
	return rc;
}
