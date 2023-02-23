
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

int mm_loop_init(mm_loop_t *loop)
{
	loop->poll = mm_epoll_if.create();
	if (loop->poll == NULL)
		return -1;
	mm_clock_init(&loop->clock);
	mm_clock_update(&loop->clock);
	memset(&loop->idle, 0, sizeof(loop->idle));
	return 0;
}

int mm_loop_shutdown(mm_loop_t *loop)
{
	mm_clock_free(&loop->clock);
	int rc;
	rc = loop->poll->iface->shutdown(loop->poll);
	if (rc == -1)
		return -1;
	loop->poll->iface->free(loop->poll);
	return 0;
}

int mm_loop_step(mm_loop_t *loop)
{
	/* update clock time */
	mm_clock_reset(&loop->clock);
	if (loop->clock.active)
		mm_clock_update(&loop->clock);

	/* run idle callback */
	int rc;
	if (loop->idle.callback) {
		rc = loop->idle.callback(&loop->idle);
		if (!rc)
			return 0;
	}

	/* get minimal timer timeout */
	int timeout = UINT32_MAX;
	mm_timer_t *next;
	next = mm_clock_timer_min(&loop->clock);
	if (next) {
		int64_t diff = next->timeout - loop->clock.time_ms;
		if (diff <= 0)
			timeout = 0;
		else
			timeout = diff;
	}

	/* run timers */
	mm_clock_step(&loop->clock);

	/* poll for events */
	rc = loop->poll->iface->step(loop->poll, timeout);
	if (rc == -1)
		return -1;

	return 0;
}
