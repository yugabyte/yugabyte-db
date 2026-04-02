/*-------------------------------------------------------------------------
 *
 * rumsort.h
 *	Generalized tuple sorting routines.
 *
 * This module handles sorting of RumSortItem or RumScanItem structures.
 * It contains copy of static functions from
 * src/backend/utils/sort/tuplesort.c.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2021, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#ifndef RUMSORT_H
#define RUMSORT_H

#include "postgres.h"
#include "fmgr.h"

#include "executor/tuptable.h"

/* RumTuplesortstate is an opaque type whose details are not known outside
 * rumsort.c.
 */
typedef struct Tuplesortstate RumTuplesortstate;
struct RumScanItem;

typedef struct
{
	ItemPointerData iptr;
	bool recheck;
	float8 data[FLEXIBLE_ARRAY_MEMBER];
}   RumSortItem;

#define RumSortItemSize(nKeys) (offsetof(RumSortItem, data) + (nKeys) * sizeof(float8))

extern MemoryContext rum_tuplesort_get_memorycontext(RumTuplesortstate *state);
extern RumTuplesortstate * rum_tuplesort_begin_rum(int workMem,
												   int nKeys, bool randomAccess, bool
												   compareItemPointer);
extern RumTuplesortstate * rum_tuplesort_begin_rumitem(int workMem,
													   FmgrInfo *cmp);
extern RumTuplesortstate * rum_tuplesort_begin_rumitem_minimal(int workMem,
															   FmgrInfo *cmp);

extern void rum_tuplesort_putrum(RumTuplesortstate *state, RumSortItem *item);
extern void rum_tuplesort_putrumitem(RumTuplesortstate *state, struct RumScanItem *item);
extern void rum_tuplesort_putrumitem_minimal(RumTuplesortstate *state, struct
											 ItemPointerData *item);

extern void rum_tuplesort_performsort(RumTuplesortstate *state);

extern RumSortItem * rum_tuplesort_getrum(RumTuplesortstate *state, bool forward,
										  bool *should_free);
extern struct RumScanItem * rum_tuplesort_getrumitem(RumTuplesortstate *state, bool
													 forward,
													 bool *should_free);

extern struct ItemPointerData * rum_tuplesort_getrumitem_minimal(RumTuplesortstate *state,
																 bool forward,
																 bool *should_free);

extern void rum_tuplesort_end(RumTuplesortstate *state);

#endif   /* RUMSORT_H */
