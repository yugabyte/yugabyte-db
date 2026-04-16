
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

#if !defined(__aarch64__) && !defined(__amd64) && !defined(__i386) && !defined(__powerpc64__)
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
#elif defined(__powerpc64__)
	"\t.text\n"
	"\t.globl mm_context_swap\n"
	"\t.type mm_context_swap,@function\n"
	"mm_context_swap:\n"
	"\t.cfi_startproc\n"
	/* Save callee-saved registers */
	"\tstd 1, 0(3)\n"      /* Save stack pointer to old context */
	"\tstd 2, 8(3)\n"      /* Save TOC pointer */
	"\tmflr 0\n"
	"\tstd 0, 16(3)\n"     /* Save link register */
	"\tstd 14, 24(3)\n"
	"\tstd 15, 32(3)\n"
	"\tstd 16, 40(3)\n"
	"\tstd 17, 48(3)\n"
	"\tstd 18, 56(3)\n"
	"\tstd 19, 64(3)\n"
	"\tstd 20, 72(3)\n"
	"\tstd 21, 80(3)\n"
	"\tstd 22, 88(3)\n"
	"\tstd 23, 96(3)\n"
	"\tstd 24, 104(3)\n"
	"\tstd 25, 112(3)\n"
	"\tstd 26, 120(3)\n"
	"\tstd 27, 128(3)\n"
	"\tstd 28, 136(3)\n"
	"\tstd 29, 144(3)\n"
	"\tstd 30, 152(3)\n"
	"\tstd 31, 160(3)\n"
	/* Restore from new context */
	"\tld 1, 0(4)\n"       /* Restore stack pointer */
	"\tld 2, 8(4)\n"       /* Restore TOC pointer */
	"\tld 0, 16(4)\n"      /* Restore link register */
	"\tmtlr 0\n"
	"\tld 14, 24(4)\n"
	"\tld 15, 32(4)\n"
	"\tld 16, 40(4)\n"
	"\tld 17, 48(4)\n"
	"\tld 18, 56(4)\n"
	"\tld 19, 64(4)\n"
	"\tld 20, 72(4)\n"
	"\tld 21, 80(4)\n"
	"\tld 22, 88(4)\n"
	"\tld 23, 96(4)\n"
	"\tld 24, 104(4)\n"
	"\tld 25, 112(4)\n"
	"\tld 26, 120(4)\n"
	"\tld 27, 128(4)\n"
	"\tld 28, 136(4)\n"
	"\tld 29, 144(4)\n"
	"\tld 30, 152(4)\n"
	"\tld 31, 160(4)\n"
	"\tblr\n"
	"\t.cfi_endproc\n"
#endif
);

