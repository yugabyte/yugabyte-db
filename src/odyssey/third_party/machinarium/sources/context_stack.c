
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

#ifdef HAVE_VALGRIND
#include <valgrind/valgrind.h>
#endif

int mm_contextstack_create(mm_contextstack_t *stack, size_t size,
			   size_t size_guard)
{
	char *base;
	base = mmap(0, size_guard + size, PROT_READ | PROT_WRITE | PROT_EXEC,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (base == MAP_FAILED)
		return -1;
	mprotect(base, size_guard, PROT_NONE);
	base += size_guard;
	stack->pointer = base;
	stack->size = size;
	stack->size_guard = size_guard;
#ifdef HAVE_VALGRIND
	stack->valgrind_stack = VALGRIND_STACK_REGISTER(
		stack->pointer, stack->pointer + stack->size);
#endif
	return 0;
}

void mm_contextstack_free(mm_contextstack_t *stack)
{
	if (stack->pointer == NULL)
		return;
#ifdef HAVE_VALGRIND
	VALGRIND_STACK_DEREGISTER(stack->valgrind_stack);
#endif
	char *base = stack->pointer - stack->size_guard;
	munmap(base, stack->size_guard + stack->size);
}
