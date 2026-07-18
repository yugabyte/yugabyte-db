#ifndef MM_TASK_H
#define MM_TASK_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_task mm_task_t;

typedef void (*mm_task_function_t)(void *);

struct mm_task {
	mm_task_function_t function;
	void *arg;
	mm_event_t on_complete;
};

#endif /* MM_TASK_H */
