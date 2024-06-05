/*-------------------------------------------------------------------------
 *
 * ybtidbitmap.c
 *	  Yugabyte ybctid bitmap package
 *
 * This module provides a YbTIDBitmap data structure that is similar to the
 * TIDBitmap datastructures, but modified for Yugabyte. Postgres stores tuple
 * identifiers (TIDs), that identify a BlockNumber and OffsetNumber. When
 * Postgres approaches work_mem, PG Bitmap Scans convert to "lossy" storage,
 * where they remember only that a page of the heap needs to be visited, but not
 * the individual offsets on the page.
 *
 * Yugabyte has ybctids, which are binary data that don't convey any meaning
 * about how the rows are laid out. Since we can't organize ybctids by page, any
 * set datastructure will work. For simplicity, ybctids are stored in a C++ set.
 * When YB Bitmap Scans approach work_mem, we stop loading ybctids into the set
 * and switch to a full table scan. Since ybctids are larger than TIDs, Yugabyte
 * will hit work_mem before PG will.
 *
 * Portions Copyright (c) 2003-2018, PostgreSQL Global Development Group
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/nodes/ybtidbitmap.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>

#include "miscadmin.h"
#include "nodes/ybtidbitmap.h"
#include "utils/memutils.h"
#include "utils/palloc.h"
#include "yb/yql/pggate/ybc_pggate.h"

struct YbTBMIterator
{
	ConstSliceVector vector;	/* pointer to a C++ vector */
	size_t		index;			/* next element of the vector to be retrieved */
	size_t		vector_size; 	/* number of elements in the vector */
	YbTBMIterateResult output;	/* MUST BE LAST (because variable-size) */
};

/*
 * yb_tbm_create - create an initially-empty bitmap
 *
 * The bitmap will live in the memory context that is GetCurrentMemoryContext()
 * at the time of this call. It will be limited to (approximately) maxbytes
 */
YbTIDBitmap *
yb_tbm_create(long maxbytes)
{
	YbTIDBitmap  *ybtbm;

	/* Create the YbTIDBitmap struct and zero all its fields */
	ybtbm = makeNode(YbTIDBitmap);

	ybtbm->ybctid_set = YBCBitmapCreateSet();

	return ybtbm;
}

/*
 * yb_tbm_check_work_mem - account for the new bytes.
 *
 * Returns false if work_mem is exceeded, and true if the tuples were
 * successfully added.
 */
static bool
yb_tbm_check_work_mem(YbTIDBitmap *ybtbm, size_t new_bytes)
{
	ybtbm->bytes_consumed += new_bytes;

	/*
	 * We might exceed work_mem during the above insertion. However, we don't
	 * need to be exact, so just check again here.
	 */
	if (ybtbm->bytes_consumed > work_mem * 1024L)
	{
		yb_tbm_set_work_mem_exceeded(ybtbm);
		return false;
	}
	return true;
}

/*
 * yb_tbm_add_tuples - add some tuple IDs to a YbTIDBitmap
 *
 * Returns false if work_mem is exceeded, and true if the tuples were
 * successfully added.
 */
bool
yb_tbm_add_tuples(YbTIDBitmap *ybtbm, ConstSliceVector ybctids)
{
	if (ybtbm->work_mem_exceeded)
		return false;

	size_t new_bytes = YBCBitmapInsertYbctidsIntoSet(ybtbm->ybctid_set,
													 ybctids);
	ybtbm->nentries = YBCBitmapGetSetSize(ybtbm->ybctid_set);

	YbPgMemAddConsumption(new_bytes);
	return yb_tbm_check_work_mem(ybtbm, new_bytes);
}

/*
 * yb_tbm_union_and_free - set union
 *
 * a is modified in-place, b is freed
 */
void
yb_tbm_union_and_free(YbTIDBitmap *a, YbTIDBitmap *b)
{
	Assert(!a->iterating);

	if (a->work_mem_exceeded || b->work_mem_exceeded)
	{
		a->work_mem_exceeded = true;
		return;
	}

	/* Nothing to do if b is empty */
	if (b->nentries == 0)
		return;

	size_t added_size = YBCBitmapUnionSet(a->ybctid_set, b->ybctid_set);
	pfree(b);

	if (!yb_tbm_check_work_mem(a, added_size))
		return;

	a->nentries = YBCBitmapGetSetSize(a->ybctid_set);
}

/*
 * yb_tbm_intersect_and_free - set intersection
 *
 * a is modified in-place, b is freed
 */
void
yb_tbm_intersect_and_free(YbTIDBitmap *a, YbTIDBitmap *b)
{
	Assert(!a->iterating);

	if (a->work_mem_exceeded || b->work_mem_exceeded)
	{
		a->work_mem_exceeded = true;
		return;
	}

	/* Nothing to do if a is empty */
	if (a->nentries == 0)
		return;

	size_t total_bytes = a->bytes_consumed + b->bytes_consumed;
	size_t total_length = a->nentries + b->nentries;

	a->ybctid_set = YBCBitmapIntersectSet(a->ybctid_set, b->ybctid_set);
	a->nentries = YBCBitmapGetSetSize(a->ybctid_set);
	pfree(b);

	/*
	 * Update the estimate of a's consumed bytes assuming an equal proportion of
	 * each a and b.
	 */
	a->bytes_consumed = total_length == 0 ? 0
		: total_bytes * a->nentries / total_length;
}

/*
 * yb_tbm_is_empty - is a YbTIDBitmap completely empty?
 */
bool
yb_tbm_is_empty(const YbTIDBitmap *ybtbm)
{
	return (ybtbm->nentries == 0);
}

/*
 * yb_tbm_get_size - returns the number of entries in the bitmap
 *
 * if work_mem_exceeded is true, return 0 to avoid leaking variable state.
 */
size_t
yb_tbm_get_size(const YbTIDBitmap *tbm)
{
	if (tbm->work_mem_exceeded)
		return 0;
	return tbm->nentries;
}

YbTBMIterator *
yb_tbm_begin_iterate(YbTIDBitmap *ybtbm)
{
	YbTBMIterator *iterator = palloc(sizeof(YbTBMIterator));
	Assert(ybtbm->iterating != YB_TBM_ITERATING);

	iterator->index = 0;
	iterator->vector = YBCBitmapCopySetToVector(ybtbm->ybctid_set,
												&iterator->vector_size);
	ybtbm->iterating = YB_TBM_ITERATING;

	return iterator;
}

/*
 * yb_tbm_iterate - fetches the next `count` ybctids
 *
 * Allocates a new vector containing the ybctids from `iter->index` to
 * `iter->index + count`
 */
YbTBMIterateResult *
yb_tbm_iterate(YbTBMIterator *iter, int count)
{
	YbTBMIterateResult	*result = &(iter->output);
	if (iter->index >= iter->vector_size)
		return NULL;

	result->ybctid_vector = YBCBitmapGetVectorRange(iter->vector, iter->index,
													count);

	result->index = iter->index;
	iter->index += count;
	result->prefetched_index = iter->index;
	return result;
}

void
yb_tbm_end_iterate(YbTBMIterator *ybtbmiterator)
{
	YBCBitmapShallowDeleteVector(ybtbmiterator->vector);
	pfree(ybtbmiterator);
}

void
yb_tbm_free(YbTIDBitmap *ybtbm)
{
	YBCBitmapDeepDeleteSet(ybtbm->ybctid_set);
	pfree(ybtbm);
}

void
yb_tbm_free_iter_result(YbTBMIterateResult *iter)
{
	/*
	 * This vector's items point to the same allocations as the YbTIDBitmap.
	 * When we free the iterator, those allocations will be freed.
	 */
	YBCBitmapShallowDeleteVector(iter->ybctid_vector);
}

void yb_tbm_set_work_mem_exceeded(YbTIDBitmap *ybtbm)
{
	elog(NOTICE, "exceeded work_mem, switching to full table scan");
	ybtbm->work_mem_exceeded = true;
	ybtbm->nentries = 0;
}

size_t yb_tbm_get_average_bytes(YbTIDBitmap *ybtbm)
{
	if (ybtbm->nentries > 0 && ybtbm->bytes_consumed > 0)
		return ybtbm->bytes_consumed / ybtbm->nentries;
	else
		return 0;
}

/*
 * yb_tbm_calculate_entries
 *
 * Estimate number of ybctids we can store within maxbytes.
 */
long
yb_tbm_calculate_entries(double maxbytes, int32 ybctid_width)
{
	/*
	 * Estimate 3 pointers per element of the unordered_set
	 * (based on https://stackoverflow.com/a/37177838)
	 */
	return maxbytes / (ybctid_width + (3 * sizeof(int *)));
}
