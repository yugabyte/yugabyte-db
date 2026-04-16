#ifndef MM_TASK_MGR_H
#define MM_TASK_MGR_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_taskmgr mm_taskmgr_t;

struct mm_taskmgr {
	int workers_count;
	int *workers;
	mm_channel_t channel;
	mm_event_t event;
};

void mm_taskmgr_init(mm_taskmgr_t *);
int mm_taskmgr_start(mm_taskmgr_t *, int);
void mm_taskmgr_stop(mm_taskmgr_t *);
int mm_taskmgr_new(mm_taskmgr_t *, mm_task_function_t, void *, uint32_t);

#endif /* MM_TASK_MGR_H */
