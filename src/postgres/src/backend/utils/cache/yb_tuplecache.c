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

#include "postgres.h"

#include "access/genam.h"
#include "access/table.h"
#include "utils/yb_tuplecache.h"

void
YbLoadTupleCache(YbTupleCache *cache, Oid relid,
				 YbTupleCacheKeyExtractor key_extractor, const char *cache_name)
{
	Assert(!(cache->rel || cache->data));
	cache->rel = table_open(relid, AccessShareLock);
	HASHCTL		ctl = {0};

	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(YbTupleCacheEntry);
	cache->data = hash_create(cache_name, 32, &ctl, HASH_ELEM | HASH_BLOBS);

	SysScanDesc scandesc = systable_beginscan(cache->rel, InvalidOid,
											  false /* indexOk */ , NULL, 0,
											  NULL);

	YbTupleCacheEntry *entry = NULL;
	HeapTuple	htup;

	while (HeapTupleIsValid(htup = systable_getnext(scandesc)))
	{
		Oid			key = key_extractor(htup);

		if (!entry || entry->key != key)
		{
			bool		found = false;

			entry = hash_search(cache->data, &key, HASH_ENTER, &found);

			if (!found)
				entry->tuples = NULL;
		}
		entry->tuples = lappend(entry->tuples, htup);
	}
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

	table_close(cache->rel, AccessShareLock);
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

HeapTuple
YbTupleCacheIteratorGetNext(YbTupleCacheIterator iter)
{
	if (iter->current == NULL)
		return NULL;

	HeapTuple	tuple = lfirst(iter->current);

	iter->current = lnext(iter->list, iter->current);
	return tuple;
}

void
YbTupleCacheIteratorEnd(YbTupleCacheIterator iter)
{
	pfree(iter);
}
