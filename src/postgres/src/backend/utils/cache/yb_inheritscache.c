/*--------------------------------------------------------------------------------------------------
 *
 * yb_inheritscache.c
 *		Contains the implementation for the YB pg_inherits cache. This cache is a hash table where
 *		the key is the parent oid and the value is a list of child tuples. Looking up the cache
 *		through the parent oid is O(1) operation. Lookup through the child oid is O(n) operation.
 *      The O(n) operation is acceptable because lookup via the child oid is comparatively rare and
 *      not worth creating another cache indexed by the child id.
 * Copyright (c) YugaByte, Inc.
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
 * src/backend/utils/cache/yb_inheritscache.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup_details.h"
#include "access/yb_scan.h"
#include "catalog/indexing.h"
#include "catalog/pg_inherits_d.h"
#include "catalog/pg_inherits.h"
#include "storage/lockdefs.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/resowner_private.h"
#include "utils/yb_inheritscache.h"

static HTAB *YbPgInheritsCache;

static void
FindChildren(Oid parentOid, List **childTuples)
{
	MemoryContext oldcxt = MemoryContextSwitchTo(CacheMemoryContext);
	*childTuples = NIL;

	Relation relation = heap_open(InheritsRelationId, AccessShareLock);
	ScanKeyData key[1];
	ScanKeyInit(&key[0],
				Anum_pg_inherits_inhparent,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(parentOid));

	elog(DEBUG3, "FindChildren for parentOid %d", parentOid);
	SysScanDesc scan = ybc_systable_begin_default_scan(
		relation, InheritsParentIndexId, true, NULL, 1, key);

	HeapTuple inheritsTuple = NULL;
	while ((inheritsTuple = systable_getnext(scan)) != NULL)
	{
		HeapTuple copy_inheritsTuple = heap_copytuple(inheritsTuple);
		*childTuples = lappend(*childTuples, copy_inheritsTuple);
		elog(DEBUG3, "Found child %d for parent %d",
			((Form_pg_inherits) GETSTRUCT(inheritsTuple))->inhrelid,
			parentOid);
	}

	systable_endscan(scan);
	heap_close(relation, AccessShareLock);
	MemoryContextSwitchTo(oldcxt);
}

static void
YbPgInheritsCacheRelCallback(Datum arg, Oid relid)
{
	elog(DEBUG1, "YbPgInheritsCacheRelCallback for relid %d", relid);
	YbPgInheritsCacheInvalidate(relid);
}

static void
YbPgInheritsIncrementReferenceCount(YbPgInheritsCacheEntry entry)
{
	entry->refcount++;
	if (IsBootstrapProcessingMode())
		return;
	ResourceOwnerEnlargeYbPgInheritsRefs(CurrentResourceOwner);
	ResourceOwnerRememberYbPgInheritsRef(CurrentResourceOwner, entry);
}

static void
YbPgInheritsDecrementReferenceCount(YbPgInheritsCacheEntry entry)
{
	/*
	 * The refcount should always be greater than 1. It should move to zero
	 * only when the entry is invalidated from the cache for any reason.
	 * Currently usages of tuples from the pg_inherits table does not extend
	 * beyond a possible cache invalidation event, therefore it is never
	 * possible for a client to call ReleaseYbPgInheritsCacheEntry after it has
	 * been invalidated. The refcount and assertions are used to future proof
	 * the above assumption.
	*/
	Assert(entry->refcount >= 1);
	--entry->refcount;
	if (!IsBootstrapProcessingMode())
		ResourceOwnerForgetYbPgInheritsRef(CurrentResourceOwner, entry);
}

static Oid
YbGetParentRelid(Oid relid)
{
	Relation relation = heap_open(InheritsRelationId, AccessShareLock);
	ScanKeyData key[2];

	/*
	 * First find the parent of the given child.
	*/
	ScanKeyInit(&key[0],
                Anum_pg_inherits_inhrelid,
                BTEqualStrategyNumber, F_OIDEQ,
                ObjectIdGetDatum(relid));
    ScanKeyInit(&key[1],
                Anum_pg_inherits_inhseqno,
                BTEqualStrategyNumber, F_INT4EQ,
                Int32GetDatum(1));
	SysScanDesc scan = ybc_systable_begin_default_scan(
		relation, InheritsRelidSeqnoIndexId, true, NULL, 2, key);

	HeapTuple inheritsTuple = NULL;
	Oid result = InvalidOid;

	while ((inheritsTuple = systable_getnext(scan)) != NULL)
	{
		Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(inheritsTuple);
		result = form->inhparent;
		break;
	}
	systable_endscan(scan);
	heap_close(relation, AccessShareLock);

	return result;
}

static YbPgInheritsCacheChildEntry
YbGetChildCacheEntry(YbPgInheritsCacheEntry entry, Oid relid)
{
	ListCell *lc;
	foreach(lc, entry->childTuples)
	{
		HeapTuple tuple = (HeapTuple) lfirst(lc);
		Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(tuple);
		if (form->inhrelid == relid)
		{
			elog(DEBUG3, "YbGetChildCacheEntry hit for relid %d", relid);

			YbPgInheritsCacheChildEntry result =
				palloc(sizeof(YbPgInheritsCacheChildEntryData));
			result->cacheEntry = entry;
			YbPgInheritsIncrementReferenceCount(entry);
			result->childTuple = tuple;
			result->childrelid = relid;
			return result;
		}
	}
	return NULL;
}

static YbPgInheritsCacheChildEntry
GetYbChildCacheEntryMiss(Oid relid)
{
	elog(DEBUG3,
		"GetYbPgInheritsChildCacheEntry miss for relid %d", relid);

	Oid parentOid = YbGetParentRelid(relid);

	if (!OidIsValid(parentOid))
		return NULL;

	elog(DEBUG3,
		"YbPgInheritsCache: Found parent %d for child %d", parentOid, relid);

	/*
	 * Populate the cache with the parent of this child tuple.
	*/
	YbPgInheritsCacheEntry elem = GetYbPgInheritsCacheEntry(parentOid);
	if (elem == NULL)
	{
		/*
		 * The previous scan found the parent table for this child, but now we
		 * are unable to find the parent table. This can only happen if
		 * somehow the parent and this child was dropped between these two
		 * scans.
		*/
		elog(ERROR, "Possible concurrent DDL: Unable to find parent %d for "
					"child %d", parentOid, relid);
	}
	YbPgInheritsCacheChildEntry result = YbGetChildCacheEntry(elem, relid);
	ReleaseYbPgInheritsCacheEntry(elem);
	return result;
}

void
YbInitPgInheritsCache()
{
	Assert(YbPgInheritsCache == NULL);
	HASHCTL ctl = {0};
	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(YbPgInheritsCacheEntryData);
	ctl.hcxt = CacheMemoryContext;
	/*
	 * It is hard to estimate how many partitioned tables there will be, so
	 * start with a small size and let it grow as needed.
	*/
	YbPgInheritsCache =
		hash_create("YbPgInheritsCache", 8, &ctl,
					HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

	CacheRegisterRelcacheCallback(YbPgInheritsCacheRelCallback, (Datum) 0);
	elog(DEBUG3, "Initialized YbPgInherits cache");
}

void
YbPreloadPgInheritsCache()
{
	Assert(YbPgInheritsCache);
	Relation relation = heap_open(InheritsRelationId, AccessShareLock);
	HeapTuple	inheritsTuple;

	SysScanDesc scan = ybc_systable_begin_default_scan(
		relation, InheritsParentIndexId, true, NULL, 0, NULL);

	YbPgInheritsCacheEntry entry = NULL;
	Oid parentOid = InvalidOid;
	while ((inheritsTuple = systable_getnext(scan)) != NULL)
	{
		Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(inheritsTuple);
		elog(DEBUG3,
			"Preloading pg_inherits cache for parent %d, child %d curr "
			"parentOid %d", form->inhparent, form->inhrelid, parentOid);

		if (parentOid != form->inhparent)
		{
			parentOid = form->inhparent;
			bool found = false;
			entry = hash_search(
				YbPgInheritsCache, (void *)&parentOid, HASH_ENTER, &found);
			Assert(!found);
			entry->childTuples = NIL;
			entry->refcount = 1;
			entry->parentOid = parentOid;
		}
		MemoryContext oldcxt = MemoryContextSwitchTo(CacheMemoryContext);
		HeapTuple copy_inheritsTuple = heap_copytuple(inheritsTuple);
		entry->childTuples = lappend(entry->childTuples, copy_inheritsTuple);
		MemoryContextSwitchTo(oldcxt);
	}
	systable_endscan(scan);
	heap_close(relation, AccessShareLock);
}

YbPgInheritsCacheEntry
GetYbPgInheritsCacheEntry(Oid parentOid)
{
	Assert(YbPgInheritsCache);
	bool found = false;
	YbPgInheritsCacheEntry entry = hash_search(
		YbPgInheritsCache, (void *)&parentOid, HASH_ENTER, &found);
	if (!found)
	{
		elog(DEBUG3, "YbPgInheritsCache miss for parent %d", parentOid);
		entry->parentOid = parentOid;
		FindChildren(parentOid, &entry->childTuples);
		entry->refcount = 1;
	}
	else
	{
		elog(DEBUG3, "YbPgInheritsCache hit for parentOid %d", parentOid);
	}
	YbPgInheritsIncrementReferenceCount(entry);
	return entry;
}

YbPgInheritsCacheChildEntry
GetYbPgInheritsChildCacheEntry(Oid relid)
{
	Assert(YbPgInheritsCache);

	elog(DEBUG3, "GetYbPgInheritsChildCacheEntry for relid %d", relid);

	HASH_SEQ_STATUS status;
	YbPgInheritsCacheEntry elem;
	/*
	 * The cache is indexed by parent oid, so we need to iterate over the entire
	 * hash table to find the child tuple.
	*/
	hash_seq_init(&status, YbPgInheritsCache);
	while ((elem = (YbPgInheritsCacheEntry)hash_seq_search(&status)) != NULL)
	{
		YbPgInheritsCacheChildEntry result = YbGetChildCacheEntry(elem, relid);
		if (result)
		{
			hash_seq_term(&status);
			return result;
		}
	}
	return GetYbChildCacheEntryMiss(relid);
}

void
ReleaseYbPgInheritsCacheEntry(YbPgInheritsCacheEntry entry)
{
	elog(DEBUG3,
		"ReleaseYbPgInheritsCacheEntry for parentOid %d", entry->parentOid);

	YbPgInheritsDecrementReferenceCount(entry);
}

void
ReleaseYbPgInheritsChildEntry(YbPgInheritsCacheChildEntry entry)
{
	elog(DEBUG3,
		"ReleaseYbPgInheritsChildEntry for relid %d", entry->childrelid);
	ReleaseYbPgInheritsCacheEntry(entry->cacheEntry);
	pfree(entry);
}

void
YbPgInheritsCacheDelete(YbPgInheritsCacheEntry entry)
{
	elog(DEBUG3,
		"YbPgInheritsCacheDelete for parentOid %d", entry->parentOid);
	Assert(YbPgInheritsCache);
	Assert(entry->refcount == 1);
	list_free_deep(entry->childTuples);
	if (hash_search(YbPgInheritsCache,
					(void *)&entry->parentOid,
					HASH_REMOVE,
					NULL) == NULL)
		elog(ERROR, "Hash table corrupted. Relid %d", entry->parentOid);
}

void
YbPgInheritsCacheInvalidate(Oid relid)
{
	HASH_SEQ_STATUS status;
	YbPgInheritsCacheEntry entry;

	Assert(YbPgInheritsCache);

	hash_seq_init(&status, YbPgInheritsCache);

	while ((entry = (YbPgInheritsCacheEntry)hash_seq_search(&status)) != NULL)
	{
		/*
		 * If relid is InvalidOid, then we are invalidating the entire cache.
		 * Otherwise, we are invalidating the cache entry for the given relid.
		 * We could have a partitioned table that is a child of another
		 * partitioned table, so we need look for the invalidated relid in both
		 * the parentoid and child list.
		*/
		if (relid == InvalidOid || entry->parentOid == relid)
		{
			elog(DEBUG3,
				"YbPgInheritsCacheInvalidate: Invalidating parent %d",
				entry->parentOid);
			YbPgInheritsCacheDelete(entry);
			continue;
		}

		ListCell *lc;
		foreach(lc, entry->childTuples)
		{
			HeapTuple tuple = (HeapTuple) lfirst(lc);
			Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(tuple);
			if (form->inhrelid == relid)
			{
				elog(DEBUG3,
					"YbPgInheritsCacheInvalidate: Invalidating parent %d "
					"for child %d", form->inhparent, relid);
				YbPgInheritsCacheDelete(entry);
				break;
			}
		}
	}
}
