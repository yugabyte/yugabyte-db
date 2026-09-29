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

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(yb_test_write_past_chunk_end);
PG_FUNCTION_INFO_V1(yb_test_heap_buffer_overflow);
PG_FUNCTION_INFO_V1(yb_test_leak_malloc);

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
 * Overwrite the MEMORY_CONTEXT_CHECKING sentinel byte just past the requested
 * size of a chunk, then free the chunk, which checks the sentinel.  The write
 * stays within the chunk's allocated size, so only the sentinel check can see
 * it.
 */
Datum
yb_test_write_past_chunk_end(PG_FUNCTION_ARGS)
{
	char	   *volatile p;

#ifndef MEMORY_CONTEXT_CHECKING
	ereport(ERROR,
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("this function requires a build with MEMORY_CONTEXT_CHECKING")));
#endif
	p = palloc(60);
	p[60] = 'x';
	pfree((void *) p);
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
