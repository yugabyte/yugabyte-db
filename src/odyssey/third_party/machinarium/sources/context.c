
/*
 * machinarium.
 *
 * cooperative multitasking engine.
 */

#include <machinarium.h>
#include <machinarium_private.h>

typedef struct {
	mm_context_t *context_runner;
	mm_context_t *context;
	mm_context_function_t function;
	void *arg;
} mm_runner_t;

static __thread mm_runner_t runner;

static void mm_context_runner(void)
{
	/* save argument */
	volatile mm_runner_t runner_arg = runner;

	/* return to mm_context_create() */
	mm_context_swap(runner_arg.context, runner_arg.context_runner);

	runner_arg.function(runner_arg.arg);
	abort();
}

static inline void **mm_context_prepare(mm_contextstack_t *stack)
{
	void **sp;
	sp = (void **)(stack->pointer + stack->size);
#if __amd64
	*--sp = NULL;
	*--sp = (void *)mm_context_runner;
	/* for x86_64 we need to place return address on stack */
	sp -= 6;
	memset(sp, 0, sizeof(void *) * 6);
#elif __aarch64__
	/* for aarch64 we need to place return address in x30 reg */
	sp -= 16;
	memset(sp, 0, sizeof(void *) * 16);
	*(sp + 1) = (void *)mm_context_runner;
#else
	*--sp = NULL;
	*--sp = (void *)mm_context_runner;
	sp -= 4;
	memset(sp, 0, sizeof(void *) * 4);
#endif
	return sp;
}

void mm_context_create(mm_context_t *context, mm_contextstack_t *stack,
		       void (*function)(void *), void *arg)
{
	/* must be first */
	mm_context_t context_runner;

	/* prepare context runner */
	runner.context_runner = &context_runner;
	runner.context = context;
	runner.function = function;
	runner.arg = arg;

	/* prepare context */
	context->sp = mm_context_prepare(stack);

	/* execute runner: pass function and argument */
	mm_context_swap(&context_runner, context);
}

#if !defined(__aarch64__) && !defined(__amd64) && !defined(__i386)
#error unsupported architecture
#endif

asm(
#if __amd64
	"\t.text\n"
	"\t.globl mm_context_swap\n"
	"\t.type x,@function\n"
	"mm_context_swap:\n"
	"\tpushq %rbp\n"
	"\tpushq %rbx\n"
	"\tpushq %r12\n"
	"\tpushq %r13\n"
	"\tpushq %r14\n"
	"\tpushq %r15\n"
	"\tmovq %rsp, (%rdi)\n"
	"\tmovq (%rsi), %rsp\n"
	"\tpopq %r15\n"
	"\tpopq %r14\n"
	"\tpopq %r13\n"
	"\tpopq %r12\n"
	"\tpopq %rbx\n"
	"\tpopq %rbp\n"
	"\tret\n"
#elif __i386
	"\t.text\n"
	"\t.globl mm_context_swap\n"
	"\t.type x,@function\n"
	"mm_context_swap:\n"
	"\tpushl %ebp\n"
	"\tpushl %ebx\n"
	"\tpushl %esi\n"
	"\tpushl %edi\n"
	"\tmovl %esp, (%eax)\n"
	"\tmovl (%edx), %esp\n"
	"\tpopl %edi\n"
	"\tpopl %esi\n"
	"\tpopl %ebx\n"
	"\tpopl %ebp\n"
	"\tret\n"
#elif __aarch64__
	"\t.text\n"
	"\t.global mm_context_swap\n"
	"mm_context_swap:\n"
	"\tstp x8, x16, [sp, #-16]!\n"
	"\tstp x17, x18, [sp, #-16]!\n"
	"\tstp x19, x20, [sp, #-16]!\n"
	"\tstp x21, x22, [sp, #-16]!\n"
	"\tstp x23, x24, [sp, #-16]!\n"
	"\tstp x25, x26, [sp, #-16]!\n"
	"\tstp x27, x28, [sp, #-16]!\n"
	"\tstp x29, x30, [sp, #-16]!\n"
	"\tmov x3, sp\n"
	"\tstr x3, [x0]\n"
	"\tldr x3, [x1]\n"
	"\tmov sp, x3\n"
	"\tldp x29, x30, [sp], #16\n"
	"\tldp x27, x28, [sp], #16\n"
	"\tldp x25, x26, [sp], #16\n"
	"\tldp x23, x24, [sp], #16\n"
	"\tldp x21, x22, [sp], #16\n"
	"\tldp x19, x20, [sp], #16\n"
	"\tldp x17, x18, [sp], #16\n"
	"\tldp x8, x16, [sp], #16\n"
	"\tret x30\n"
#endif
);
