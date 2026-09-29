/*-------------------------------------------------------------------------
 *
 * yb_tuplecache.c
 *	  Implementation of YugabyteDB tuple cache for cache preloading.
 *
 * Copyright (c) YugabyteDB, Inc.
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
 * src/backend/utils/cache/yb_tuplecache.c
 *
 *-------------------------------------------------------------------------
 */

#include "utils/yb_tuplecache.h"

#include "access/heapam.h"
#include "access/genam.h"
#include "access/htup_details.h"
#include "utils/memutils.h"

/*
 * Fetch the next row of a catalog preload scan into row_cxt, first discarding
 * the previous row.  A decoded catalog row leaves behind the per-column copies
 * pggate makes while decoding it, several times the size of the tuple itself,
 * so a full-catalog scan must not accumulate them.  The returned row stays
 * valid until the next call, so a scan that finds the end of one relation's
 * rows by reading the next relation's first row can still pass that row on.
 */
HeapTuple
YbSystableGetNextInContext(SysScanDesc scandesc, MemoryContext row_cxt)
{
	MemoryContext oldcxt;
	HeapTuple	htup;

	MemoryContextReset(row_cxt);
	oldcxt = MemoryContextSwitchTo(row_cxt);
	htup = systable_getnext(scandesc);
	MemoryContextSwitchTo(oldcxt);
	return htup;
}

void
YbLoadTupleCache(YbTupleCache *cache, Oid relid,
				 YbTupleCacheKeyExtractor key_extractor, const char *cache_name)
{
	Assert(!(cache->rel || cache->data));
	cache->rel = heap_open(relid, AccessShareLock);
	HASHCTL ctl = {0};
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(YbTupleCacheEntry);
	cache->data = hash_create(cache_name, 32, &ctl, HASH_ELEM | HASH_BLOBS);

	SysScanDesc scandesc = systable_beginscan(
		cache->rel, InvalidOid, false /* indexOk */, NULL, 0, NULL);

	YbTupleCacheEntry *entry = NULL;
	HeapTuple htup;
	MemoryContext row_cxt = AllocSetContextCreate(CurrentMemoryContext,
												  "tuple cache row",
												  ALLOCSET_DEFAULT_SIZES);

	while (HeapTupleIsValid(htup = YbSystableGetNextInContext(scandesc,
															  row_cxt)))
	{
		/* The next fetch resets row_cxt */
		htup = heap_copytuple(htup);

		Oid key = key_extractor(htup);
		if (!entry || entry->key != key)
		{
			bool found = false;
			entry = hash_search(cache->data, &key, HASH_ENTER, &found);

			if (!found)
				entry->tuples = NULL;
		}
		entry->tuples = lappend(entry->tuples, htup);
	}
	MemoryContextDelete(row_cxt);
	systable_endscan(scandesc);
}

void
YbCleanupTupleCache(YbTupleCache *cache)
{
	if (!cache->rel)
		return;

	if (cache->data)
	{
		hash_destroy(cache->data);
		cache->data = NULL;
	}

	heap_close(cache->rel, AccessShareLock);
	cache->rel = NULL;
}

YbTupleCacheIterator
YbTupleCacheIteratorBegin(const YbTupleCache *cache, const void *key_ptr)
{
	YbTupleCacheIterator iter = palloc(sizeof(struct YbTupleCacheIteratorData));
	const YbTupleCacheEntry *entry = hash_search(cache->data, key_ptr, HASH_FIND, NULL);
	iter->list = entry != NULL ? entry->tuples : NIL;
	iter->current = list_head(iter->list);
	return iter;
}

HeapTuple YbTupleCacheIteratorGetNext(YbTupleCacheIterator iter)
{
	if (iter->current == NULL)
		return NULL;

	HeapTuple tuple = lfirst(iter->current);
	iter->current = lnext(iter->current);
	return tuple;
}

void YbTupleCacheIteratorEnd(YbTupleCacheIterator iter)
{
	pfree(iter);
}
