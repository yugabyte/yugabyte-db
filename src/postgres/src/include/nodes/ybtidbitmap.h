/*-------------------------------------------------------------------------
 *
 * ybtidbitmap.h
 *	  PostgreSQL tuple-id (TID) bitmap package
 *
 * This module provides bitmap data structures that are spiritually
 * similar to Bitmapsets, but are specially adapted to store sets of
 * tuple identifiers (TIDs), or ItemPointers.  In particular, the division
 * of an ItemPointer into BlockNumber and OffsetNumber is catered for.
 * Also, since we wish to be able to store very large tuple sets in
 * memory with this data structure, we support "lossy" storage, in which
 * we no longer remember individual tuple offsets on a page but only the
 * fact that a particular page needs to be visited.
 *
 *
 * Copyright (c) 2003-2018, PostgreSQL Global Development Group
 *
 * src/include/nodes/ybtidbitmap.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef YBTIDBITMAP_H
#define YBTIDBITMAP_H

#include "postgres.h"
#include "nodes/nodes.h"
#include "yb/yql/pggate/ybc_pggate.h"

/* YbTBMIterator is private */
typedef struct YbTBMIterator YbTBMIterator;

/*
 * Current iterating state of the YbTBM.
 */
typedef enum
{
	YB_TBM_NOT_ITERATING,		/* not yet converted to iterable vector */
	YB_TBM_ITERATING,			/* converted to iterable vector */
} YbTBMIteratingState;

/*
 * Here is the representation for a whole YbTIDBitmap:
 */
typedef struct YbTIDBitmap
{
	NodeTag		type;			/* to make it a valid Node */
	SliceSet	ybctid_set;		/* C++ set that contains my ybctids */
	int			nentries;		/* number of entries in the bitmap */
	YbTBMIteratingState iterating PG_USED_FOR_ASSERTS_ONLY;
								/* yb_tbm_begin_iterate called? */
	size_t		bytes_consumed;	/* sum of the size of the ybctids */
	bool		recheck;		/* recheck all entries in this set? */
	bool		work_mem_exceeded;	/* if bytes_consumed exceeds work_mem */
} YbTIDBitmap;

/* Result structure for tbm_iterate */
typedef struct
{
	ConstSliceVector	ybctid_vector;
	size_t				index;
	size_t				prefetched_index;
} YbTBMIterateResult;

/* function prototypes in nodes/tidbitmap.c */

extern YbTIDBitmap *yb_tbm_create(long maxbytes);

extern bool yb_tbm_add_tuples(YbTIDBitmap *ybtbm, ConstSliceVector ybctids);

extern void yb_tbm_union_and_free(YbTIDBitmap *a, YbTIDBitmap *b);
extern void yb_tbm_intersect_and_free(YbTIDBitmap *a, YbTIDBitmap *b);

extern bool yb_tbm_is_empty(const YbTIDBitmap *tbm);
extern size_t yb_tbm_get_size(const YbTIDBitmap *tbm);

extern YbTBMIterator *yb_tbm_begin_iterate(YbTIDBitmap *ybtbm);
extern YbTBMIterateResult *yb_tbm_iterate(YbTBMIterator *ybtbmiterator,
										  int count);
extern void yb_tbm_end_iterate(YbTBMIterator *ybtbmiterator);
extern void yb_tbm_free(YbTIDBitmap *ybtbm);
extern void yb_tbm_free_iter_result(YbTBMIterateResult *iter);

extern void yb_tbm_set_work_mem_exceeded(YbTIDBitmap *ybtbm);
extern size_t yb_tbm_get_average_bytes(YbTIDBitmap *ybtbm);

extern long yb_tbm_calculate_entries(double maxbytes, int32 ybctid_width);

#endif							/* TIDBITMAP_H */
