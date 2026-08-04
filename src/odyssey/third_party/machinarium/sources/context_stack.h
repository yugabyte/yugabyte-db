#ifndef MM_CONTEXT_STACK_H
#define MM_CONTEXT_STACK_H

/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

typedef struct mm_contextstack mm_contextstack_t;

struct mm_contextstack {
	char *pointer;
	size_t size;
	size_t size_guard;
#ifdef HAVE_VALGRIND
	int valgrind_stack;
#endif
};

int mm_contextstack_create(mm_contextstack_t *, size_t, size_t);
void mm_contextstack_free(mm_contextstack_t *);

#endif /* MM_CONTEXT_STACK_H */
