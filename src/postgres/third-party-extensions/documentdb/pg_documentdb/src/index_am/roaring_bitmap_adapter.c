/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bitmap_utils/roaring_bitmap_adapter.c
 *
 * Implementation of the bitmap adapters for the extension use cases.
 * This is currently adapted for the Rum Index for deduplicating array entries.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <storage/itemptr.h>
#include "../roaring_bitmaps/roaring.h"
#include "index_am/roaring_bitmap_adapter.h"
#include "index_am/documentdb_rum.h"


typedef struct RoaringBitmapState
{
	roaring64_bitmap_t *bitmap;
} RoaringBitmapState;

static void * CreateRoaringBitmapState(void);
static bool RoaringBitmapStateAddTuple(void *state, ItemPointer tuple);
static void FreeRoaringBitmapState(void *state);
static void InstallRoaringMemoryHooks(void);

const RumIndexArrayStateFuncs RoaringStateFuncs = {
	.createState = CreateRoaringBitmapState,
	.addItem = RoaringBitmapStateAddTuple,
	.freeState = FreeRoaringBitmapState,
};

void
RegisterRoaringBitmapHooks(void)
{
	InstallRoaringMemoryHooks();
}


static void *
CreateRoaringBitmapState(void)
{
	RoaringBitmapState *state = palloc(sizeof(RoaringBitmapState));
	state->bitmap = roaring64_bitmap_create();
	return state;
}


static bool
RoaringBitmapStateAddTuple(void *state, ItemPointer tuple)
{
	RoaringBitmapState *bitmapState = (RoaringBitmapState *) state;
	uint64_t tupleValue = (((uint64_t) ItemPointerGetBlockNumber(tuple)) << 32) |
						  ItemPointerGetOffsetNumber(tuple);
	return roaring64_bitmap_add_checked(bitmapState->bitmap, tupleValue);
}


static void
FreeRoaringBitmapState(void *state)
{
	RoaringBitmapState *bitmapState = (RoaringBitmapState *) state;
	roaring64_bitmap_free(bitmapState->bitmap);
	pfree(bitmapState);
}


static void *
roaring_pg_malloc(size_t num_bytes)
{
	return palloc(num_bytes);
}


static void *
roaring_pg_calloc(size_t n_members, size_t num_bytes)
{
	/* TODO: Is this the best way to handle this? */
	return palloc0(n_members * num_bytes);
}


static void *
roaring_pg_realloc(void *mem, size_t num_bytes)
{
	if (mem == NULL)
	{
		return roaring_pg_malloc(num_bytes);
	}

	return repalloc(mem, num_bytes);
}


static void *
roaring_pg_aligned_alloc(size_t alignment, size_t num_bytes)
{
#if PG_VERSION_NUM >= 160000
	return palloc_aligned(num_bytes, alignment, 0);
#else
	return roaring_pg_malloc(num_bytes);
#endif
}


static void
roaring_pg_free(void *mem)
{
	if (mem != NULL)
	{
		pfree(mem);
	}
}


static void
InstallRoaringMemoryHooks(void)
{
	roaring_memory_t memory_hook = {
		.malloc = roaring_pg_malloc,
		.realloc = roaring_pg_realloc,
		.calloc = roaring_pg_calloc,
		.free = roaring_pg_free,
		.aligned_malloc = roaring_pg_aligned_alloc,
		.aligned_free = roaring_pg_free,
	};
	roaring_init_memory_hook(memory_hook);
}
