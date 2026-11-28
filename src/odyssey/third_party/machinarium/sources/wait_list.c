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
	mm_sleeplock_lock(&wait_list->lock);
	release_sleepy(wait_list, sleepy);
	mm_sleeplock_unlock(&wait_list->lock);
}

mm_wait_list_t *mm_wait_list_create(atomic_uint_fast64_t *word)
{
	mm_wait_list_t *wait_list = malloc(sizeof(mm_wait_list_t));
	if (wait_list == NULL) {
		return NULL;
	}
	memset(wait_list, 0, sizeof(mm_wait_list_t));

	mm_sleeplock_init(&wait_list->lock);

	mm_list_init(&wait_list->sleepies);
	mm_atomic_u64_set(&wait_list->sleepies_count, 0ULL);
	wait_list->word = word;

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
		return MACHINE_WAIT_LIST_ERR_TIMEOUT_OR_CANCEL;
	}

	return MACHINE_WAIT_LIST_SUCCESS;
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

int mm_wait_list_compare_wait(mm_wait_list_t *wait_list, uint64_t expected,
			      uint32_t timeout_ms)
{
	mm_sleeplock_lock(&wait_list->lock);

	if (atomic_load(wait_list->word) != expected) {
		mm_sleeplock_unlock(&wait_list->lock);

		return MACHINE_WAIT_LIST_ERR_AGAIN;
	}

	mm_sleepy_t this;
	init_sleepy(&this);

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

	int event_mgr_fd;
	event_mgr_fd = mm_eventmgr_signal(&sleepy->event);

	mm_sleeplock_unlock(&wait_list->lock);

	if (event_mgr_fd > 0) {
		mm_eventmgr_wakeup(event_mgr_fd);
	}
}

MACHINE_API machine_wait_list_t *
machine_wait_list_create(atomic_uint_fast64_t *word)
{
	mm_wait_list_t *wl;
	wl = mm_wait_list_create(word);
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

MACHINE_API int machine_wait_list_compare_wait(machine_wait_list_t *wait_list,
					       uint64_t value,
					       uint32_t timeout_ms)
{
	mm_wait_list_t *wl;
	wl = mm_cast(mm_wait_list_t *, wait_list);

	int rc;
	rc = mm_wait_list_compare_wait(wl, value, timeout_ms);

	return rc;
}

MACHINE_API void machine_wait_list_notify(machine_wait_list_t *wait_list)
{
	mm_wait_list_t *wl;
	wl = mm_cast(mm_wait_list_t *, wait_list);

	mm_wait_list_notify(wl);
}
