#ifndef MM_THREAD_H
#define MM_THREAD_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_thread mm_thread_t;

typedef void *(*mm_thread_function_t)(void *);

struct mm_thread {
	pthread_t id;
	mm_thread_function_t function;
	void *arg;
};

int mm_thread_create(mm_thread_t *, int, mm_thread_function_t, void *);
int mm_thread_join(mm_thread_t *);
int mm_thread_set_name(mm_thread_t *, char *);
int mm_thread_disable_cancel(void);

#endif /* MM_THREAD_H */
