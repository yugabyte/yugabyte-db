/*-------------------------------------------------------------------------
 *
 * memutils.h
 *	  This file contains declarations for memory allocation utility
 *	  functions.  These are functions that are not quite widely used
 *	  enough to justify going in utils/palloc.h, but are still part
 *	  of the API of the memory management subsystem.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/memutils.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef MEMUTILS_H
#define MEMUTILS_H

#include "c.h"
#include "nodes/memnodes.h"

#include "yb/yql/pggate/util/ybc_util.h"

/*
 * MaxAllocSize, MaxAllocHugeSize
 *		Quasi-arbitrary limits on size of allocations.
 *
 * Note:
 *		There is no guarantee that smaller allocations will succeed, but
 *		larger requests will be summarily denied.
 *
 * palloc() enforces MaxAllocSize, chosen to correspond to the limiting size
 * of varlena objects under TOAST.  See VARSIZE_4B() and related macros in
 * postgres.h.  Many datatypes assume that any allocatable size can be
 * represented in a varlena header.  This limit also permits a caller to use
 * an "int" variable for an index into or length of an allocation.  Callers
 * careful to avoid these hazards can access the higher limit with
 * MemoryContextAllocHuge().  Both limits permit code to assume that it may
 * compute twice an allocation's size without overflow.
 */
#define MaxAllocSize	((Size) 0x3fffffff) /* 1 gigabyte - 1 */

#define AllocSizeIsValid(size)	((Size) (size) <= MaxAllocSize)

#define MaxAllocHugeSize	(SIZE_MAX / 2)

#define AllocHugeSizeIsValid(size)	((Size) (size) <= MaxAllocHugeSize)


/*
 * Standard top-level memory contexts.
 *
 * Only TopMemoryContext and ErrorContext are initialized by
 * MemoryContextInit() itself.
 */
extern PGDLLIMPORT MemoryContext TopMemoryContext;
extern PGDLLIMPORT MemoryContext ErrorContext;
extern PGDLLIMPORT MemoryContext PostmasterContext;
extern PGDLLIMPORT MemoryContext CacheMemoryContext;
extern PGDLLIMPORT MemoryContext MessageContext;
extern PGDLLIMPORT MemoryContext TopTransactionContext;
extern PGDLLIMPORT MemoryContext CurTransactionContext;

/* This is a transient link to the active portal's memory context: */
extern PGDLLIMPORT MemoryContext PortalContext;

/* Backwards compatibility macro */
#define MemoryContextResetAndDeleteChildren(ctx) MemoryContextReset(ctx)


/*
 * Memory-context-type-independent functions in mcxt.c
 */
extern void MemoryContextInit(void);
extern void MemoryContextReset(MemoryContext context);
extern void MemoryContextDelete(MemoryContext context);
extern void MemoryContextResetOnly(MemoryContext context);
extern void MemoryContextResetChildren(MemoryContext context);
extern void MemoryContextDeleteChildren(MemoryContext context);
extern void MemoryContextSetIdentifier(MemoryContext context, const char *id);
extern void MemoryContextSetParent(MemoryContext context,
								   MemoryContext new_parent);
extern Size GetMemoryChunkSpace(void *pointer);
extern MemoryContext MemoryContextGetParent(MemoryContext context);
extern bool MemoryContextIsEmpty(MemoryContext context);
extern Size MemoryContextMemAllocated(MemoryContext context, bool recurse);
extern void MemoryContextStats(MemoryContext context);
extern void MemoryContextStatsDetail(MemoryContext context, int max_children,
									 bool print_to_stderr);
extern void MemoryContextAllowInCriticalSection(MemoryContext context,
												bool allow);
extern int64 MemoryContextStatsUsage(MemoryContext context, int max_children);

#ifdef MEMORY_CONTEXT_CHECKING
extern void MemoryContextCheck(MemoryContext context);
#endif
extern bool MemoryContextContains(MemoryContext context, void *pointer);

/* Handy macro for copying and assigning context ID ... but note double eval */
#define MemoryContextCopyAndSetIdentifier(cxt, id) \
	MemoryContextSetIdentifier(cxt, MemoryContextStrdup(cxt, id))

/*
 * GetMemoryChunkContext
 *		Given a currently-allocated chunk, determine the context
 *		it belongs to.
 *
 * All chunks allocated by any memory context manager are required to be
 * preceded by the corresponding MemoryContext stored, without padding, in the
 * preceding sizeof(void*) bytes.  A currently-allocated chunk must contain a
 * backpointer to its owning context.  The backpointer is used by pfree() and
 * repalloc() to find the context to call.
 */
#ifndef FRONTEND
static inline MemoryContext
GetMemoryChunkContext(void *pointer)
{
	MemoryContext context;

	if (pointer == NULL)
	{
		YBC_LOG_ERROR_STACK_TRACE("GetMemoryChunkContext: null pointer");
	}
	/*
	 * Try to detect bogus pointers handed to us, poorly though we can.
	 * Presumably, a pointer that isn't MAXALIGNED isn't pointing at an
	 * allocated chunk.
	 */
	Assert(pointer != NULL);
	Assert(pointer == (void *) MAXALIGN(pointer));

	/*
	 * OK, it's probably safe to look at the context.
	 */
	context = *(MemoryContext *) (((char *) pointer) - sizeof(void *));

	AssertArg(MemoryContextIsValid(context));

	return context;
}
#endif

/*
 * This routine handles the context-type-independent part of memory
 * context creation.  It's intended to be called from context-type-
 * specific creation routines, and noplace else.
 */
extern void MemoryContextCreate(MemoryContext node,
								NodeTag tag,
								const MemoryContextMethods *methods,
								MemoryContext parent,
								const char *name);

extern void HandleLogMemoryContextInterrupt(void);
extern void ProcessLogMemoryContextInterrupt(void);

/*
 * Memory-context-type-specific functions
 */

/* aset.c */
extern MemoryContext AllocSetContextCreateInternal(MemoryContext parent,
												   const char *name,
												   Size minContextSize,
												   Size initBlockSize,
												   Size maxBlockSize);

/*
 * This wrapper macro exists to check for non-constant strings used as context
 * names; that's no longer supported.  (Use MemoryContextSetIdentifier if you
 * want to provide a variable identifier.)
 */
#ifdef HAVE__BUILTIN_CONSTANT_P
#define AllocSetContextCreate(parent, name, ...) \
	(StaticAssertExpr(__builtin_constant_p(name), \
					  "memory context names must be constant strings"), \
	 AllocSetContextCreateInternal(parent, name, __VA_ARGS__))
#else
#define AllocSetContextCreate \
	AllocSetContextCreateInternal
#endif

/* slab.c */
extern MemoryContext SlabContextCreate(MemoryContext parent,
									   const char *name,
									   Size blockSize,
									   Size chunkSize);

/* generation.c */
extern MemoryContext GenerationContextCreate(MemoryContext parent,
											 const char *name,
											 Size minContextSize,
											 Size initBlockSize,
											 Size maxBlockSize);

/*
 * Recommended default alloc parameters, suitable for "ordinary" contexts
 * that might hold quite a lot of data.
 */
#define ALLOCSET_DEFAULT_MINSIZE   0
#define ALLOCSET_DEFAULT_INITSIZE  (8 * 1024)
#define ALLOCSET_DEFAULT_MAXSIZE   (8 * 1024 * 1024)
#define ALLOCSET_DEFAULT_SIZES \
	ALLOCSET_DEFAULT_MINSIZE, ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE

/*
 * Recommended alloc parameters for "small" contexts that are never expected
 * to contain much data (for example, a context to contain a query plan).
 */
#define ALLOCSET_SMALL_MINSIZE	 0
#define ALLOCSET_SMALL_INITSIZE  (1 * 1024)
#define ALLOCSET_SMALL_MAXSIZE	 (8 * 1024)
#define ALLOCSET_SMALL_SIZES \
	ALLOCSET_SMALL_MINSIZE, ALLOCSET_SMALL_INITSIZE, ALLOCSET_SMALL_MAXSIZE

/*
 * Recommended alloc parameters for contexts that should start out small,
 * but might sometimes grow big.
 */
#define ALLOCSET_START_SMALL_SIZES \
	ALLOCSET_SMALL_MINSIZE, ALLOCSET_SMALL_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE

/*
 * Threshold above which a request in an AllocSet context is certain to be
 * allocated separately (and thereby have constant allocation overhead).
 * Few callers should be interested in this, but tuplesort/tuplestore need
 * to know it.
 */
#define ALLOCSET_SEPARATE_THRESHOLD  8192

#define SLAB_DEFAULT_BLOCK_SIZE		(8 * 1024)
#define SLAB_LARGE_BLOCK_SIZE		(8 * 1024 * 1024)

/*
 * Tracking memory consumption for both PG backend and pggate tcmalloc acutal
 * heap consumption.
 * Global accessible in one PG backend process.
 */
typedef struct YbPgMemTracker
{
	/*
	 * Current, at time of cutting Snapshot(), memory in bytes allocated by PG
	 * (pggate is not included in this field)
	 */
	Size		pg_cur_mem_bytes;
	/*
	 * The current allocated memory including PG, pggate and cached memory.
	 */
	int64_t		backend_cur_allocated_mem_bytes;
	/*
	 * The maximum memory ever allocated by current statement including PG and
	 * pggate
	 */
	Size		stmt_max_mem_bytes;
	/*
	 * The initial base memory already allocated by PG and paggate at the
	 * beginning of current statement.
	 * NOTE: Only set if yb_run_with_explain_analyze is true.
	 */
	Size		stmt_max_mem_base_bytes;

	/*
	 * A flag to tell if pggate is inititated. This is used to track the memory
	 * used by PG before pggate is started.
	 * Note: the design here is that this flag is a "link" to MemTracker in the
	 * pggate. It pushes down fundamental memory work to it, while this layer
	 * stays as light as possible in PG.
	 */
	bool		pggate_alive;
} YbPgMemTracker;

extern YbPgMemTracker PgMemTracker;

/*
 * Add memory consumption to PgMemTracker in bytes.
 * sz can be negative. In this case, the max values are not
 * updated.
 */
extern void YbPgMemAddConsumption(const Size sz);

/*
 * Substract the sz bytes from PgMemTracker. It doesn't update the maximum
 * values for the backend and stmt.
 */
extern void YbPgMemSubConsumption(const Size sz);

/*
 * Reset the PgMemTracker's stmt fields and make it ready to
 * track peak memory usage for a new statement.
 */
extern void YbPgMemResetStmtConsumption();

/*
 * Returns the resident set size (physical memory use) of a process
 * measured in bytes, or -1 if the value cannot be determined on this OS.
 */
extern int64_t YbPgGetCurRSSMemUsage(int pid);

#endif							/* MEMUTILS_H */
