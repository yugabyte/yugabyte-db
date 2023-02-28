
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static void mm_eventmgr_on_read(mm_fd_t *handle)
{
	mm_eventmgr_t *mgr = handle->on_read_arg;

	uint64_t id;
	int rc;
	rc = mm_socket_read(mgr->fd.fd, &id, sizeof(id));
	(void)rc;
	assert(rc == sizeof(id));

	/* wakeup event waiters */
	mm_sleeplock_lock(&mgr->lock);

	if (!mgr->count_ready) {
		mm_sleeplock_unlock(&mgr->lock);
		return;
	}

	mm_list_t *i;
	mm_list_foreach(&mgr->list_ready, i)
	{
		mm_event_t *event;
		event = mm_container_of(i, mm_event_t, link);
		assert(event->state == MM_EVENT_READY);
		event->state = MM_EVENT_ACTIVE;
		mm_scheduler_wakeup(&mm_self->scheduler, event->call.coroutine);
	}
	mm_list_init(&mgr->list_ready);
	mgr->count_ready = 0;

	mm_sleeplock_unlock(&mgr->lock);
}

int mm_eventmgr_init(mm_eventmgr_t *mgr, mm_loop_t *loop)
{
	mm_sleeplock_init(&mgr->lock);

	mm_list_init(&mgr->list_ready);
	mm_list_init(&mgr->list_wait);
	mgr->count_ready = 0;
	mgr->count_wait = 0;

	memset(&mgr->fd, 0, sizeof(mgr->fd));
	mgr->fd.fd = mm_socket_eventfd(0);
	if (mgr->fd.fd == -1)
		return -1;
	int rc;
	rc = mm_loop_add(loop, &mgr->fd, 0);
	if (rc == -1) {
		close(mgr->fd.fd);
		mgr->fd.fd = -1;
		return -1;
	}
	rc = mm_loop_read(loop, &mgr->fd, mm_eventmgr_on_read, mgr);
	if (rc == -1) {
		mm_loop_delete(loop, &mgr->fd);
		close(mgr->fd.fd);
		mgr->fd.fd = -1;
		return -1;
	}
	return 0;
}

void mm_eventmgr_free(mm_eventmgr_t *mgr, mm_loop_t *loop)
{
	if (mgr->fd.fd == -1)
		return;
	mm_loop_delete(loop, &mgr->fd);
	close(mgr->fd.fd);
	mgr->fd.fd = -1;
}

void mm_eventmgr_add(mm_eventmgr_t *mgr, mm_event_t *event)
{
	mm_list_init(&event->link);
	event->state = MM_EVENT_WAIT;
	event->event_mgr = mgr;

	/* add event to wait list */
	mm_sleeplock_lock(&mgr->lock);

	mm_list_append(&mgr->list_wait, &event->link);
	mgr->count_wait++;

	mm_sleeplock_unlock(&mgr->lock);
}

int mm_eventmgr_wait(mm_eventmgr_t *mgr, mm_event_t *event, uint32_t time_ms)
{
	/* wait for event */
	mm_call(&event->call, MM_CALL_EVENT, time_ms);

	/* maybe remove from wait list */
	mm_sleeplock_lock(&mgr->lock);

	int complete = 0;
	switch (event->state) {
	case MM_EVENT_WAIT:
		mm_list_unlink(&event->link);
		mgr->count_wait--;
		break;
	case MM_EVENT_READY:
		mm_list_unlink(&event->link);
		mgr->count_ready--;
		break;
	case MM_EVENT_ACTIVE:
		complete = 1;
		break;
	case MM_EVENT_NONE:
		assert(0);
		break;
	}
	event->state = MM_EVENT_NONE;

	mm_sleeplock_unlock(&mgr->lock);
	return complete;
}

int mm_eventmgr_signal(mm_event_t *event)
{
	mm_eventmgr_t *mgr = event->event_mgr;

	mm_sleeplock_lock(&mgr->lock);

	if (event->state == MM_EVENT_NONE || event->state == MM_EVENT_ACTIVE) {
		mm_sleeplock_unlock(&mgr->lock);
		return 0;
	}
	int fd = 0;
	if (mgr->count_ready == 0)
		fd = mgr->fd.fd;
	assert(event->state == MM_EVENT_WAIT);
	mm_list_unlink(&event->link);
	mgr->count_wait--;

	event->state = MM_EVENT_READY;
	mm_list_append(&mgr->list_ready, &event->link);
	mgr->count_ready++;

	mm_sleeplock_unlock(&mgr->lock);
	return fd;
}

void mm_eventmgr_wakeup(int fd)
{
	uint64_t id = 1;
	int rc;
	rc = mm_socket_write(fd, &id, sizeof(id));
	(void)rc;
	assert(rc == sizeof(id));
}
