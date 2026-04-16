#ifndef MM_EVENT_MGR_H
#define MM_EVENT_MGR_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_eventmgr_t mm_eventmgr_t;

struct mm_eventmgr_t {
	mm_fd_t fd;
	mm_sleeplock_t lock;
	mm_list_t list_ready;
	mm_list_t list_wait;
	int count_ready;
	int count_wait;
};

int mm_eventmgr_init(mm_eventmgr_t *, mm_loop_t *);
void mm_eventmgr_free(mm_eventmgr_t *, mm_loop_t *);
void mm_eventmgr_add(mm_eventmgr_t *, mm_event_t *);
int mm_eventmgr_wait(mm_eventmgr_t *, mm_event_t *, uint32_t);
int mm_eventmgr_signal(mm_event_t *);
void mm_eventmgr_wakeup(int);

#endif /* MM_EVENT_MGR_H */
