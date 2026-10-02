/*-------------------------------------------------------------------------
 *
 * yb_test_memory_errors.c
 *		Functions that deliberately corrupt or leak memory, so tests can check
 *		that the corruption or leak is detected.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * IDENTIFICATION
 *		src/test/modules/yb_test_memory_errors/yb_test_memory_errors.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <stdlib.h>

#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(yb_test_write_past_chunk_end);
PG_FUNCTION_INFO_V1(yb_test_heap_buffer_overflow);
PG_FUNCTION_INFO_V1(yb_test_leak_malloc);
PG_FUNCTION_INFO_V1(yb_test_use_after_pfree);

static void
require_address_sanitizer(void)
{
#ifndef ADDRESS_SANITIZER
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("this function requires an AddressSanitizer build")));
#endif
}

/*
 * Allocate a chunk in a new context of the given kind (aset, generation or
 * slab), and overwrite the MEMORY_CONTEXT_CHECKING sentinel byte just past the
 * chunk's requested size.  The write stays within the chunk's allocated size,
 * so only the sentinel check can see it.  That check runs when the chunk is
 * freed if free_chunk is true, and otherwise when the context is deleted.
 */
Datum
yb_test_write_past_chunk_end(PG_FUNCTION_ARGS)
{
	char	   *kind = text_to_cstring(PG_GETARG_TEXT_PP(0));
	bool		free_chunk = PG_GETARG_BOOL(1);
	MemoryContext context;
	char	   *volatile p;

#ifndef MEMORY_CONTEXT_CHECKING
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("this function requires a build with MEMORY_CONTEXT_CHECKING")));
#endif
	if (strcmp(kind, "aset") == 0)
		context = AllocSetContextCreate(CurrentMemoryContext, "yb_test_aset",
										ALLOCSET_DEFAULT_SIZES);
	else if (strcmp(kind, "generation") == 0)
		context = GenerationContextCreate(CurrentMemoryContext,
										  "yb_test_generation",
										  ALLOCSET_DEFAULT_SIZES);
	else if (strcmp(kind, "slab") == 0)
		context = SlabContextCreate(CurrentMemoryContext, "yb_test_slab",
									SLAB_DEFAULT_BLOCK_SIZE, 60);
	else
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unknown memory context kind \"%s\"", kind)));

	p = MemoryContextAlloc(context, 60);
	p[60] = 'x';
	if (free_chunk)
		pfree((void *) p);
	MemoryContextDelete(context);
	PG_RETURN_VOID();
}

/*
 * Write one byte past the end of a malloc'd block.  The pointer is volatile so
 * that UBSan's object-size check cannot see the overflow and abort before ASAN
 * reports it.
 */
Datum
yb_test_heap_buffer_overflow(PG_FUNCTION_ARGS)
{
	char	   *volatile p;

	require_address_sanitizer();
	p = malloc(64);
	p[64] = 'x';
	free((void *) p);
	PG_RETURN_VOID();
}

static pg_noinline void
leak_malloc_blocks(void)
{
	for (int i = 0; i < 100; i++)
	{
		void	   *p = malloc(1000);

		memset(p, 0, 1000);
		/* Keep the compiler from eliding the allocation. */
		__asm__ volatile("" : : "r"(p) : "memory");
	}
}

/*
 * Leak malloc'd memory for LeakSanitizer to report when the backend exits.
 * Many blocks are leaked so that a stale copy of one pointer left on the stack
 * cannot hide all of them.
 */
Datum
yb_test_leak_malloc(PG_FUNCTION_ARGS)
{
	require_address_sanitizer();
	leak_malloc_blocks();
	PG_RETURN_VOID();
}

/*
 * Read a small palloc chunk after freeing it.  AddressSanitizer can only see
 * this when each chunk is its own malloc'd block, as in ASAN builds.
 */
Datum
yb_test_use_after_pfree(PG_FUNCTION_ARGS)
{
	char	   *volatile p;
	char		c;

	require_address_sanitizer();
	p = palloc(16);
	pfree((void *) p);
	c = p[0];
	PG_RETURN_INT32(c);
}
