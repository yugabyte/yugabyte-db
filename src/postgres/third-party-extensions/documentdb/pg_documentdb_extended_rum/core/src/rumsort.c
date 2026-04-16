/*-------------------------------------------------------------------------
 *
 * rumsort.c
 *	  Generalized tuple sorting routines.
 *
 * This module handles sorting of RumSortItem or RumScanItem structures.
 * It contains copy of static functions from
 * src/backend/utils/sort/tuplesort.c.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"
#include "rumsort.h"

#include "commands/tablespace.h"
#include "executor/executor.h"
#include "utils/logtape.h"
#include "utils/pg_rusage.h"
#include "utils/tuplesort.h"

#include "pg_documentdb_rum.h"                /* RumItem */

#if PG_VERSION_NUM >= 160000

/*
 * After allocating a public interface for Tuplesortstate, no need to include
 * source code from pg-core.
 */
#elif PG_VERSION_NUM >= 150000
#include "tuplesort/tuplesort15.c"
#endif

/*
 * In case of using custom compare function we should store function pointer in
 * sort stare in order to use it later.
 */

#if PG_VERSION_NUM >= 160000

/*
 * After allocating a public interface for Tuplesortstate we may use
 * TuplesortPublic->arg filed to store pointer to the compare function.
 */

/* GUC variables */
#ifdef TRACE_SORT
extern PGDLLIMPORT bool trace_sort;
#endif

/* All memory management should be inside Tuplesortstate module. */
#define USEMEM(state, amt) do { } while (0)

#else /* PG_VERSION_NUM >= 160000 */

/*
 * We need extra field in a state structure but we should not modify struct
 * RumTuplesortstate which is inherited from Tuplesortstate core function.
 */
typedef struct RumTuplesortstateExt
{
	RumTuplesortstate ts;
	FmgrInfo *cmp;
}           RumTuplesortstateExt;
#endif /* PG_VERSION_NUM < 160000 */

static int comparetup_rum(const SortTuple *a, const SortTuple *b,
						  RumTuplesortstate *state, bool compareItemPointer);
static int comparetup_rum_true(const SortTuple *a, const SortTuple *b,
							   RumTuplesortstate *state);
static int comparetup_rum_false(const SortTuple *a, const SortTuple *b,
								RumTuplesortstate *state);
static int comparetup_rumitem(const SortTuple *a, const SortTuple *b,
							  RumTuplesortstate *state);
static void copytup_rum(RumTuplesortstate *state, SortTuple *stup, void *tup);
static void copytup_rumitem(RumTuplesortstate *state, SortTuple *stup,
							void *tup);
static void * rum_tuplesort_getrum_internal(RumTuplesortstate *state,
											bool forward, bool *should_free);

/*
 * Tuplesortstate handling should be done through this macro.
 */
#if PG_VERSION_NUM >= 160000
#   define TSS_GET(state) TuplesortstateGetPublic((state))
#else
#   define TSS_GET(state) (state)
#endif

/*
 * Logical tape handling should be done through this macro.
 */
#if PG_VERSION_NUM >= 150000
#define LT_TYPE LogicalTape *
#define LT_ARG tape
#define TAPE(state, LT_ARG) LT_ARG
#else
#define LT_TYPE int
#define LT_ARG tapenum
#define TAPE(state, LT_ARG) state->tapeset, LT_ARG
#endif

/*
 * Just for convenience and uniformity.
 */
#if PG_VERSION_NUM >= 110000
#define tuplesort_begin_common(x, y) tuplesort_begin_common((x), NULL, (y))
#endif

/*
 * Trace log wrapper.
 */
#ifdef TRACE_SORT
#   define LOG_SORT(...) \
	if (trace_sort) \
		ereport(LOG, errmsg_internal(__VA_ARGS__))
#else
#   define LOG_SORT(...) \
	{ }
#endif

static inline int
compare_rum_itempointer(ItemPointerData p1, ItemPointerData p2)
{
	if (p1.ip_blkid.bi_hi < p2.ip_blkid.bi_hi)
	{
		return -1;
	}
	else if (p1.ip_blkid.bi_hi > p2.ip_blkid.bi_hi)
	{
		return 1;
	}

	if (p1.ip_blkid.bi_lo < p2.ip_blkid.bi_lo)
	{
		return -1;
	}
	else if (p1.ip_blkid.bi_lo > p2.ip_blkid.bi_lo)
	{
		return 1;
	}

	if (p1.ip_posid < p2.ip_posid)
	{
		return -1;
	}
	else if (p1.ip_posid > p2.ip_posid)
	{
		return 1;
	}

	return 0;
}


static int
comparetup_rum(const SortTuple *a, const SortTuple *b,
			   RumTuplesortstate *state, bool compareItemPointer)
{
	RumSortItem *i1,
				*i2;
	float8 v1 = DatumGetFloat8(a->datum1);
	float8 v2 = DatumGetFloat8(b->datum1);
	int i;

	if (v1 < v2)
	{
		return -1;
	}
	else if (v1 > v2)
	{
		return 1;
	}

	i1 = (RumSortItem *) a->tuple;
	i2 = (RumSortItem *) b->tuple;

	for (i = 1; i < TSS_GET(state)->nKeys; i++)
	{
		if (i1->data[i] < i2->data[i])
		{
			return -1;
		}
		else if (i1->data[i] > i2->data[i])
		{
			return 1;
		}
	}

	if (!compareItemPointer)
	{
		return 0;
	}

	/*
	 * If key values are equal, we sort on ItemPointer.
	 */
	return compare_rum_itempointer(i1->iptr, i2->iptr);
}


static int
comparetup_rum_true(const SortTuple *a, const SortTuple *b,
					RumTuplesortstate *state)
{
	return comparetup_rum(a, b, state, true);
}


static int
comparetup_rum_false(const SortTuple *a, const SortTuple *b,
					 RumTuplesortstate *state)
{
	return comparetup_rum(a, b, state, false);
}


static inline FmgrInfo *
comparetup_rumitem_custom_fun(RumTuplesortstate *state)
{
#if PG_VERSION_NUM >= 160000
	return (FmgrInfo *) TSS_GET(state)->arg;
#else
	return ((RumTuplesortstateExt *) state)->cmp;
#endif
}


static int
comparetup_rumitem(const SortTuple *a, const SortTuple *b,
				   RumTuplesortstate *state)
{
	RumItem *i1,
			*i2;
	FmgrInfo *cmp;

	/* Extract RumItem from RumScanItem */
	i1 = (RumItem *) a->tuple;
	i2 = (RumItem *) b->tuple;

	cmp = comparetup_rumitem_custom_fun(state);
	if (cmp != NULL)
	{
		if (i1->addInfoIsNull || i2->addInfoIsNull)
		{
			if (!(i1->addInfoIsNull && i2->addInfoIsNull))
			{
				return (i1->addInfoIsNull) ? 1 : -1;
			}

			/* go to itempointer compare */
		}
		else
		{
			int r;

			r = DatumGetInt32(FunctionCall2(cmp,
											i1->addInfo,
											i2->addInfo));

			if (r != 0)
			{
				return r;
			}
		}
	}

	/*
	 * If key values are equal, we sort on ItemPointer.
	 */
	return compare_rum_itempointer(i1->iptr, i2->iptr);
}


static int
comparetup_rumitem_minimal(const SortTuple *a, const SortTuple *b,
						   RumTuplesortstate *state)
{
	ItemPointerData *i1,
					*i2;

	/* Extract item */
	i1 = (ItemPointerData *) a->tuple;
	i2 = (ItemPointerData *) b->tuple;

	/*
	 * we sort on ItemPointer.
	 */
	return compare_rum_itempointer(*i1, *i2);
}


static void
copytup_rum(RumTuplesortstate *state, SortTuple *stup, void *tup)
{
	RumSortItem *item = (RumSortItem *) tup;
	int nKeys = TSS_GET(state)->nKeys;

	stup->datum1 = Float8GetDatum(nKeys > 0 ? item->data[0] : 0);
	stup->isnull1 = false;
	stup->tuple = tup;
	USEMEM(state, GetMemoryChunkSpace(tup));
}


static void
copytup_rumitem(RumTuplesortstate *state, SortTuple *stup, void *tup)
{
	stup->isnull1 = true;
	stup->tuple = palloc(sizeof(RumScanItem));
	memcpy(stup->tuple, tup, sizeof(RumScanItem));
	USEMEM(state, GetMemoryChunkSpace(stup->tuple));
}


static void
copytup_rumitem_minimal(RumTuplesortstate *state, SortTuple *stup, void *tup)
{
	stup->isnull1 = true;
	stup->tuple = palloc(sizeof(ItemPointerData));
	memcpy(stup->tuple, tup, sizeof(ItemPointerData));
	USEMEM(state, GetMemoryChunkSpace(stup->tuple));
}


static void readtup_rum(RumTuplesortstate *state, SortTuple *stup,
						LT_TYPE LT_ARG, unsigned int len);

static void readtup_rumitem(RumTuplesortstate *state, SortTuple *stup,
							LT_TYPE LT_ARG, unsigned int len);
static void readtup_rumitem_minimal(RumTuplesortstate *state, SortTuple *stup, LT_TYPE
									LT_ARG,
									unsigned int len);

static Size
rum_item_size(RumTuplesortstate *state)
{
	if (TSS_GET(state)->readtup == readtup_rum)
	{
		return RumSortItemSize(TSS_GET(state)->nKeys);
	}
	else if (TSS_GET(state)->readtup == readtup_rumitem_minimal)
	{
		return sizeof(ItemPointerData);
	}
	else if (TSS_GET(state)->readtup == readtup_rumitem)
	{
		return sizeof(RumScanItem);
	}

	elog(FATAL, "Unknown RUM state");
	return 0;   /* keep compiler quiet */
}


static void
writetup_rum_internal(RumTuplesortstate *state, LT_TYPE LT_ARG,
					  SortTuple *stup)
{
	void *item = stup->tuple;
	size_t size = rum_item_size(state);
	unsigned int writtenlen = size + sizeof(unsigned int);
	bool randomAccess;

	LogicalTapeWrite(TAPE(state, LT_ARG),
					 (void *) &writtenlen, sizeof(writtenlen));
	LogicalTapeWrite(TAPE(state, LT_ARG),
					 (void *) item, size);

	randomAccess =
#       if PG_VERSION_NUM >= 150000
		(TSS_GET(state)->sortopt & TUPLESORT_RANDOMACCESS) != 0;
#       else
		TSS_GET(state)->randomAccess;
#       endif

	if (randomAccess)
	{
		LogicalTapeWrite(TAPE(TSS_GET(state), LT_ARG), (void *) &writtenlen,
						 sizeof(writtenlen));
	}
}


static void
writetup_rum(RumTuplesortstate *state, LT_TYPE LT_ARG, SortTuple *stup)
{
	writetup_rum_internal(state, LT_ARG, stup);
}


static void
writetup_rumitem(RumTuplesortstate *state, LT_TYPE LT_ARG, SortTuple *stup)
{
	writetup_rum_internal(state, LT_ARG, stup);
}


static void
readtup_rum_internal(RumTuplesortstate *state, SortTuple *stup,
					 LT_TYPE LT_ARG, unsigned int len, bool is_item)
{
	unsigned int tuplen = len - sizeof(unsigned int);
	size_t size = rum_item_size(state);
	void *item = palloc(size);

	Assert(tuplen == size);

	USEMEM(state, GetMemoryChunkSpace(item));

#if PG_VERSION_NUM >= 150000
	LogicalTapeReadExact(LT_ARG, item, size);
#else
	LogicalTapeReadExact(TSS_GET(state)->tapeset, LT_ARG, item, size);
#endif
	stup->tuple = item;
	stup->isnull1 = is_item;

	if (!is_item)
	{
		stup->datum1 = Float8GetDatum(TSS_GET(state)->nKeys > 0 ?
									  ((RumSortItem *) item)->data[0] : 0);
	}
#if PG_VERSION_NUM >= 150000
	if (TSS_GET(state)->sortopt & TUPLESORT_RANDOMACCESS)   /* need trailing
	                                                         * length word? */
	{
		LogicalTapeReadExact(LT_ARG, &tuplen, sizeof(tuplen));
	}
#else
	if (TSS_GET(state)->randomAccess)
	{
		LogicalTapeReadExact(TSS_GET(state)->tapeset, LT_ARG, &tuplen,
							 sizeof(tuplen));
	}
#endif
}


static void
readtup_rum(RumTuplesortstate *state, SortTuple *stup, LT_TYPE LT_ARG,
			unsigned int len)
{
	readtup_rum_internal(state, stup, LT_ARG, len, false);
}


static void
readtup_rumitem(RumTuplesortstate *state, SortTuple *stup, LT_TYPE LT_ARG,
				unsigned int len)
{
	readtup_rum_internal(state, stup, LT_ARG, len, true);
}


static void
readtup_rumitem_minimal(RumTuplesortstate *state, SortTuple *stup, LT_TYPE LT_ARG,
						unsigned int len)
{
	readtup_rum_internal(state, stup, LT_ARG, len, true);
}


RumTuplesortstate *
rum_tuplesort_begin_rum(int workMem, int nKeys, bool randomAccess,
						bool compareItemPointer)
{
#if PG_VERSION_NUM >= 150000
	RumTuplesortstate *state = tuplesort_begin_common(workMem,
													  randomAccess ?
													  TUPLESORT_RANDOMACCESS :
													  TUPLESORT_NONE);
#else
	RumTuplesortstate *state = tuplesort_begin_common(workMem, randomAccess);
#endif
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TSS_GET(state)->sortcontext);

	LOG_SORT("begin rum sort: nKeys = %d, workMem = %d, randomAccess = %c",
			 nKeys, workMem, randomAccess ? 't' : 'f');

	TSS_GET(state)->nKeys = nKeys;
	TSS_GET(state)->comparetup = compareItemPointer ? comparetup_rum_true :
								 comparetup_rum_false;
	TSS_GET(state)->writetup = writetup_rum;
	TSS_GET(state)->readtup = readtup_rum;

	MemoryContextSwitchTo(oldcontext);

	return state;
}


RumTuplesortstate *
rum_tuplesort_begin_rumitem(int workMem, FmgrInfo *cmp)
{
#if PG_VERSION_NUM >= 160000
	RumTuplesortstate *state = tuplesort_begin_common(workMem, false);
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TSS_GET(state)->sortcontext);

	LOG_SORT("begin rumitem sort: workMem = %d", workMem);

	TSS_GET(state)->comparetup = comparetup_rumitem;
	TSS_GET(state)->writetup = writetup_rumitem;
	TSS_GET(state)->readtup = readtup_rumitem;
	TSS_GET(state)->arg = cmp;

	MemoryContextSwitchTo(oldcontext);

	return state;
#else
	RumTuplesortstate *state = tuplesort_begin_common(workMem, false);
	RumTuplesortstateExt *rs;
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TSS_GET(state)->sortcontext);

	/* Allocate extended state in the same context as state */
	rs = palloc(sizeof(*rs));

	LOG_SORT("begin rumitem sort: workMem = %d", workMem);

	rs->cmp = cmp;
	TSS_GET(state)->comparetup = comparetup_rumitem;
	TSS_GET(state)->writetup = writetup_rumitem;
	TSS_GET(state)->readtup = readtup_rumitem;
	memcpy(&rs->ts, state, sizeof(RumTuplesortstate));
	pfree(state);               /* just to be sure *state isn't used anywhere
	                             * else */

	MemoryContextSwitchTo(oldcontext);

	return (RumTuplesortstate *) rs;
#endif
}


RumTuplesortstate *
rum_tuplesort_begin_rumitem_minimal(int workMem,
									FmgrInfo *cmp)
{
#if PG_VERSION_NUM >= 160000
	RumTuplesortstate *state = tuplesort_begin_common(workMem, false);
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TSS_GET(state)->sortcontext);

	LOG_SORT("begin rumitem sort: workMem = %d", workMem);

	TSS_GET(state)->comparetup = comparetup_rumitem_minimal;
	TSS_GET(state)->writetup = writetup_rumitem;
	TSS_GET(state)->readtup = readtup_rumitem_minimal;
	TSS_GET(state)->arg = cmp;

	MemoryContextSwitchTo(oldcontext);

	return state;
#else
	RumTuplesortstate *state = tuplesort_begin_common(workMem, false);
	RumTuplesortstateExt *rs;
	MemoryContext oldcontext;

	oldcontext = MemoryContextSwitchTo(TSS_GET(state)->sortcontext);

	/* Allocate extended state in the same context as state */
	rs = palloc(sizeof(*rs));

	LOG_SORT("begin rumitem sort: workMem = %d", workMem);

	rs->cmp = cmp;
	TSS_GET(state)->comparetup = comparetup_rumitem_minimal;
	TSS_GET(state)->writetup = writetup_rumitem;
	TSS_GET(state)->readtup = readtup_rumitem_minimal;
	memcpy(&rs->ts, state, sizeof(RumTuplesortstate));
	pfree(state);               /* just to be sure *state isn't used anywhere
	                             * else */

	MemoryContextSwitchTo(oldcontext);

	return (RumTuplesortstate *) rs;
#endif
}


/*
 * rum_tuplesort_end
 *
 *	Release resources and clean up.
 *
 * NOTE: after calling this, any pointers returned by rum_tuplesort_getXXX are
 * pointing to garbage.  Be careful not to attempt to use or free such
 * pointers afterwards!
 */
void
rum_tuplesort_end(RumTuplesortstate *state)
{
#if PG_VERSION_NUM < 150000 && PG_VERSION_NUM >= 130000
	tuplesort_free(state);
#elif PG_VERSION_NUM < 160000

	/* rum_tuplesort_begin* for PG < 16 copies the RumTuplesortstate
	 * inside the Sort context. Consequently, we can't free and delete
	 * the sort context and then delete the main context. We need to skip
	 * deleting the sort context, and then as a last step delete the main context
	 * which would free the sort context as well (as a child).
	 * This is skipped for PG16+
	 */
	bool deleteSortContext = false;
	tuplesort_free_with_options(state, deleteSortContext);
	MemoryContextDelete(state->maincontext);
#else
	tuplesort_end(state);
#endif
}


/*
 * Get sort state memory context.  Currently it is used only to allocate
 * RumSortItem.
 */
MemoryContext
rum_tuplesort_get_memorycontext(RumTuplesortstate *state)
{
	return TSS_GET(state)->sortcontext;
}


void
rum_tuplesort_putrum(RumTuplesortstate *state, RumSortItem *item)
{
	MemoryContext oldcontext;
	SortTuple stup;
#if PG_VERSION_NUM >= 170000
	MinimalTuple tuple = (MinimalTuple) item;
	Size tuplen;
	TuplesortPublic *base = TuplesortstateGetPublic((TuplesortPublic *) state);
#endif

	oldcontext = MemoryContextSwitchTo(rum_tuplesort_get_memorycontext(state));
	copytup_rum(state, &stup, item);

#if PG_VERSION_NUM >= 170000

	/* GetMemoryChunkSpace is not supported for bump contexts */
	if (TupleSortUseBumpTupleCxt(base->sortopt))
	{
		tuplen = MAXALIGN(tuple->t_len);
	}
	else
	{
		tuplen = GetMemoryChunkSpace(tuple);
	}
	tuplesort_puttuple_common(state, &stup, false, tuplen);
#elif PG_VERSION_NUM >= 160000
	tuplesort_puttuple_common(state, &stup, false);
#else
	puttuple_common(state, &stup);
#endif

	MemoryContextSwitchTo(oldcontext);
}


void
rum_tuplesort_putrumitem(RumTuplesortstate *state, RumScanItem *item)
{
	MemoryContext oldcontext;
	SortTuple stup;
#if PG_VERSION_NUM >= 170000
	MinimalTuple tuple = (MinimalTuple) item;
	Size tuplen;
	TuplesortPublic *base = TuplesortstateGetPublic((TuplesortPublic *) state);
#endif

	oldcontext = MemoryContextSwitchTo(rum_tuplesort_get_memorycontext(state));
	copytup_rumitem(state, &stup, item);

#if PG_VERSION_NUM >= 170000

	/* GetMemoryChunkSpace is not supported for bump contexts */
	if (TupleSortUseBumpTupleCxt(base->sortopt))
	{
		tuplen = MAXALIGN(tuple->t_len);
	}
	else
	{
		tuplen = GetMemoryChunkSpace(tuple);
	}
	tuplesort_puttuple_common(state, &stup, false, tuplen);
#elif PG_VERSION_NUM >= 160000
	tuplesort_puttuple_common(state, &stup, false);
#else
	puttuple_common(state, &stup);
#endif

	MemoryContextSwitchTo(oldcontext);
}


void
rum_tuplesort_putrumitem_minimal(RumTuplesortstate *state, struct ItemPointerData *item)
{
	MemoryContext oldcontext;
	SortTuple stup;
#if PG_VERSION_NUM >= 170000
	Size tuplen;
	TuplesortPublic *base = TuplesortstateGetPublic((TuplesortPublic *) state);
#endif
	oldcontext = MemoryContextSwitchTo(rum_tuplesort_get_memorycontext(state));
	copytup_rumitem_minimal(state, &stup, item);

#if PG_VERSION_NUM >= 170000

	/* GetMemoryChunkSpace is not supported for bump contexts */
	if (TupleSortUseBumpTupleCxt(base->sortopt))
	{
		tuplen = MAXALIGN(sizeof(ItemPointerData));
	}
	else
	{
		tuplen = GetMemoryChunkSpace(item);
	}
	tuplesort_puttuple_common(state, &stup, false, tuplen);
#elif PG_VERSION_NUM >= 160000
	tuplesort_puttuple_common(state, &stup, false);
#else
	puttuple_common(state, &stup);
#endif

	MemoryContextSwitchTo(oldcontext);
}


void
rum_tuplesort_performsort(RumTuplesortstate *state)
{
	tuplesort_performsort(state);
}


/*
 * Internal routine to fetch the next index tuple in either forward or back
 * direction. Returns NULL if no more tuples. Returned tuple belongs to
 * tuplesort memory context. Caller may not rely on tuple remaining valid after
 * any further manipulation of tuplesort.
 *
 * If *should_free is set, the caller must pfree stup.tuple when done with it.
 *
 * NOTE: in PG 10 and newer tuple is always allocated tuple in tuplesort context
 * and should not be freed by caller.
 */
static void *
rum_tuplesort_getrum_internal(RumTuplesortstate *state, bool forward,
							  bool *should_free)
{
#if PG_VERSION_NUM >= 100000
	*should_free = false;
	return (RumSortItem *) tuplesort_getindextuple(state, forward);
#else
	return (RumSortItem *) tuplesort_getindextuple(state, forward, should_free);
#endif
}


RumSortItem *
rum_tuplesort_getrum(RumTuplesortstate *state, bool forward, bool *should_free)
{
	return (RumSortItem *) rum_tuplesort_getrum_internal(state, forward,
														 should_free);
}


RumScanItem *
rum_tuplesort_getrumitem(RumTuplesortstate *state, bool forward,
						 bool *should_free)
{
	return (RumScanItem *) rum_tuplesort_getrum_internal(state, forward,
														 should_free);
}


ItemPointerData *
rum_tuplesort_getrumitem_minimal(RumTuplesortstate *state,
								 bool forward,
								 bool *should_free)
{
	return (ItemPointerData *) rum_tuplesort_getrum_internal(state, forward,
															 should_free);
}
