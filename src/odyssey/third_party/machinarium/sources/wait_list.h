#ifndef MM_WAIT_LIST_H
#define MM_WAIT_LIST_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#define MM_SLEEPY_NO_CORO_ID (~0ULL)

typedef struct mm_sleepy {
	mm_event_t event;
	mm_list_t link;

	// we can store coroutine id and we will, in case of some debugging
	uint64_t coro_id;

	uint64_t released;
} mm_sleepy_t;

typedef struct mm_wait_list {
	mm_sleeplock_t lock;
	mm_list_t sleepies;
	uint64_t sleepies_count;
} mm_wait_list_t;

mm_wait_list_t *mm_wait_list_create();
void mm_wait_list_destroy(mm_wait_list_t *wait_list);

int mm_wait_list_wait(mm_wait_list_t *wait_list, uint32_t timeout_ms);
void mm_wait_list_notify(mm_wait_list_t *wait_list);

#endif /* MM_WAIT_LIST_H */
