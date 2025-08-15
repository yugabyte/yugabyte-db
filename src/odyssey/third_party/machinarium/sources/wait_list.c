/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

static inline void release_sleepy(mm_wait_list_t *wait_list,
				  mm_sleepy_t *sleepy)
{
	mm_atomic_u64_once(&sleepy->released, {
		mm_list_unlink(&sleepy->link);
		mm_atomic_u64_dec(&wait_list->sleepies_count);
	});
}

static inline void init_sleepy(mm_sleepy_t *sleepy)
{
	if (mm_self != NULL && mm_self->scheduler.current != NULL) {
		sleepy->coro_id = mm_self->scheduler.current->id;
	} else {
		sleepy->coro_id = MM_SLEEPY_NO_CORO_ID;
	}

	mm_atomic_u64_set(&sleepy->released, 0ULL);

	mm_list_init(&sleepy->link);
	mm_eventmgr_add(&mm_self->event_mgr, &sleepy->event);
}

static inline void release_sleepy_with_lock(mm_wait_list_t *wait_list,
					    mm_sleepy_t *sleepy)
{
	// in case sleepy wasn't released from list due to timeout

	if (mm_atomic_u64_value(&sleepy->released) == 0) {
		mm_sleeplock_lock(&wait_list->lock);

		// need to check once again, in case some notify has released
		// this sleepy between first check and locking
		// once-again checking will be performed inside release_sleepy
		release_sleepy(wait_list, sleepy);

		mm_sleeplock_unlock(&wait_list->lock);
	}
}

mm_wait_list_t *mm_wait_list_create()
{
	mm_wait_list_t *wait_list = malloc(sizeof(mm_wait_list_t));
	if (wait_list == NULL) {
		return NULL;
	}
	memset(wait_list, 0, sizeof(mm_wait_list_t));

	mm_sleeplock_init(&wait_list->lock);

	mm_list_init(&wait_list->sleepies);
	mm_atomic_u64_set(&wait_list->sleepies_count, 0ULL);

	return wait_list;
}

void mm_wait_list_destroy(mm_wait_list_t *wait_list)
{
	mm_sleeplock_lock(&wait_list->lock);

	mm_list_t *i, *n;
	mm_list_foreach_safe(&wait_list->sleepies, i, n)
	{
		mm_sleepy_t *sleepy = mm_container_of(i, mm_sleepy_t, link);
		mm_call_cancel(&sleepy->event.call, NULL);

		release_sleepy(wait_list, sleepy);
	}

	mm_sleeplock_unlock(&wait_list->lock);

	free(wait_list);
}

static inline int wait_sleepy(mm_wait_list_t *wait_list, mm_sleepy_t *sleepy,
			      uint32_t timeout_ms)
{
	mm_eventmgr_wait(&mm_self->event_mgr, &sleepy->event, timeout_ms);

	release_sleepy_with_lock(wait_list, sleepy);

	// timeout or cancel
	if (sleepy->event.call.status != 0) {
		return 1;
	}

	return 0;
}

int mm_wait_list_wait(mm_wait_list_t *wait_list, uint32_t timeout_ms)
{
	mm_sleepy_t this;
	init_sleepy(&this);

	mm_sleeplock_lock(&wait_list->lock);

	mm_list_append(&wait_list->sleepies, &this.link);
	mm_atomic_u64_inc(&wait_list->sleepies_count);

	mm_sleeplock_unlock(&wait_list->lock);

	int rc;
	rc = wait_sleepy(wait_list, &this, timeout_ms);

	return rc;
}

void mm_wait_list_notify(mm_wait_list_t *wait_list)
{
	mm_sleeplock_lock(&wait_list->lock);

	if (mm_atomic_u64_value(&wait_list->sleepies_count) == 0ULL) {
		mm_sleeplock_unlock(&wait_list->lock);
		return;
	}

	mm_sleepy_t *sleepy;
	sleepy = mm_list_peek(wait_list->sleepies, mm_sleepy_t);

	release_sleepy(wait_list, sleepy);

	mm_sleeplock_unlock(&wait_list->lock);

	int event_mgr_fd;
	event_mgr_fd = mm_eventmgr_signal(&sleepy->event);
	if (event_mgr_fd > 0) {
		mm_eventmgr_wakeup(event_mgr_fd);
	}
}

MACHINE_API machine_wait_list_t *machine_wait_list_create()
{
	mm_wait_list_t *wl;
	wl = mm_wait_list_create();
	if (wl == NULL) {
		return NULL;
	}

	return mm_cast(machine_wait_list_t *, wl);
}

MACHINE_API void machine_wait_list_destroy(machine_wait_list_t *wait_list)
{
	mm_wait_list_t *wl;
	wl = mm_cast(mm_wait_list_t *, wait_list);

	mm_wait_list_destroy(wl);
}

MACHINE_API int machine_wait_list_wait(machine_wait_list_t *wait_list,
				       uint32_t timeout_ms)
{
	mm_wait_list_t *wl;
	wl = mm_cast(mm_wait_list_t *, wait_list);

	int rc;
	rc = mm_wait_list_wait(wl, timeout_ms);

	return rc;
}

MACHINE_API void machine_wait_list_notify(machine_wait_list_t *wait_list)
{
	mm_wait_list_t *wl;
	wl = mm_cast(mm_wait_list_t *, wait_list);

	mm_wait_list_notify(wl);
}
