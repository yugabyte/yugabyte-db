/*--------------------------------------------------------------------------------------------------
 *
 * yb_inheritscache.c
 *		Contains the implementation for the YB pg_inherits cache. This cache is a hash table where
 *		the key is the parent oid and the value is a list of child tuples. Looking up the cache
 *		through the parent oid is O(1) operation. Lookup through the child oid is O(n) operation.
 *      The O(n) operation is acceptable because lookup via the child oid is comparatively rare and
 *      not worth creating another cache indexed by the child id.
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
#include "catalog/pg_inherits.h"
#include "catalog/pg_inherits_d.h"
#include "common/hashfn.h"
#include "storage/lockdefs.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/relcache.h"
#include "utils/resowner_private.h"
#include "utils/syscache.h"
#include "utils/yb_inheritscache.h"
#include "yb/yql/pggate/ybc_gflags.h"

/*
 *   Parent oid -> list<child tuples>
 */
static HTAB *YbPgInheritsCacheByParent;

/*
 *   Child oid -> list<parent oids>
 */
static HTAB *YbPgInheritsCacheByChild;

static bool fully_loaded = false;

static void
FindChildren(Oid parentOid, List **childTuples)
{
	MemoryContext oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

	*childTuples = NIL;

	Relation	relation = table_open(InheritsRelationId, AccessShareLock);
	ScanKeyData key[1];

	ScanKeyInit(&key[0],
				Anum_pg_inherits_inhparent,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(parentOid));

	elog(DEBUG3, "FindChildren for parentOid %d", parentOid);
	SysScanDesc scan = ybc_systable_begin_default_scan(relation,
													   InheritsParentIndexId,
													   true, NULL, 1, key);

	HeapTuple	inheritsTuple = NULL;

	while ((inheritsTuple = systable_getnext(scan)) != NULL)
	{
		HeapTuple	copy_inheritsTuple = heap_copytuple(inheritsTuple);

		*childTuples = lappend(*childTuples, copy_inheritsTuple);
		elog(DEBUG3, "Found child %d for parent %d",
			 ((Form_pg_inherits) GETSTRUCT(inheritsTuple))->inhrelid,
			 parentOid);
	}

	systable_endscan(scan);
	table_close(relation, AccessShareLock);
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

static void
GetChildCacheEntryMiss(Oid relid, YbPgInheritsCacheEntry entry)
{
	Relation	relation = table_open(InheritsRelationId, AccessShareLock);
	ScanKeyData key[1];

	ScanKeyInit(&key[0],
				Anum_pg_inherits_inhrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relid));
	SysScanDesc scan = ybc_systable_begin_default_scan(relation,
													   InheritsRelidSeqnoIndexId,
													   true, NULL, 1, key);

	HeapTuple	inheritsTuple = NULL;

	while ((inheritsTuple = systable_getnext(scan)) != NULL)
	{
		Assert(((Form_pg_inherits) GETSTRUCT(inheritsTuple))->inhrelid == relid);
		MemoryContext oldcxt = MemoryContextSwitchTo(CacheMemoryContext);
		HeapTuple	copy_inheritsTuple = heap_copytuple(inheritsTuple);

		elog(DEBUG3, "Found parent %d for child %d",
			 ((Form_pg_inherits) GETSTRUCT(inheritsTuple))->inhparent,
			 relid);
		entry->tuples = lappend(entry->tuples, copy_inheritsTuple);
		MemoryContextSwitchTo(oldcxt);
	}
	systable_endscan(scan);
	table_close(relation, AccessShareLock);

}

void
YbInitPgInheritsCache()
{
	Assert(YbPgInheritsCacheByParent == NULL);
	Assert(YbPgInheritsCacheByChild == NULL);

	HASHCTL		ctl = {0};

	ctl.keysize = sizeof(Oid);
	ctl.entrysize = sizeof(YbPgInheritsCacheEntryData);
	ctl.hcxt = CacheMemoryContext;
	/*
	 * It is hard to estimate how many partitioned tables there will be, so
	 * start with a small size and let it grow as needed.
	*/
	YbPgInheritsCacheByParent =
		hash_create("YbPgInheritsCacheByParent", 8, &ctl,
					HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);

	YbPgInheritsCacheByChild =
		hash_create("YbPgInheritsCacheByChild", 8, &ctl,
					HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);


	CacheRegisterRelcacheCallback(YbPgInheritsCacheRelCallback, (Datum) 0);
	elog(DEBUG3, "Initialized YbPgInherits cache");
}

void
YbPreloadPgInheritsCache()
{
	elog(yb_debug_log_catcache_events ? LOG : DEBUG3, "YbPgInheritsCache: preload started.");
	Assert(YbPgInheritsCacheByParent);
	Assert(YbPgInheritsCacheByChild);
	Relation	relation = table_open(InheritsRelationId, AccessShareLock);
	HeapTuple	inheritsTuple;

	SysScanDesc scan = ybc_systable_begin_default_scan(relation,
													   InheritsParentIndexId,
													   true, NULL, 0, NULL);

	YbPgInheritsCacheEntry entry = NULL;
	Oid			parentOid = InvalidOid;

	while ((inheritsTuple = systable_getnext(scan)) != NULL)
	{
		Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(inheritsTuple);

		elog(DEBUG3,
			 "Preloading pg_inherits cache for parent %d, child %d curr "
			 "parentOid %d", form->inhparent, form->inhrelid, parentOid);

		/* insert into the parent -> child cache */
		if (parentOid != form->inhparent)
		{
			parentOid = form->inhparent;
			bool		found = false;

			entry = hash_search(YbPgInheritsCacheByParent, (void *) &parentOid,
								HASH_ENTER, &found);
			Assert(!found);
			entry->tuples = NIL;
			entry->refcount = 1;
			entry->oid = parentOid;
		}
		MemoryContext oldcxt = MemoryContextSwitchTo(CacheMemoryContext);
		HeapTuple	copy_inheritsTuple = heap_copytuple(inheritsTuple);

		entry->tuples = lappend(entry->tuples, copy_inheritsTuple);

		/* insert into the child -> parent cache */
		bool		foundChildEntry = false;
		YbPgInheritsCacheEntry childCacheEntry = hash_search(YbPgInheritsCacheByChild,
															 (void *) &(form->inhrelid), HASH_ENTER,
															 &foundChildEntry);

		if (!foundChildEntry)
		{
			childCacheEntry->oid = form->inhrelid;
			childCacheEntry->refcount = 1;
			childCacheEntry->tuples = NULL;
		}
		HeapTuple	childCopy_inheritsTuple = heap_copytuple(inheritsTuple);

		childCacheEntry->tuples = lappend(childCacheEntry->tuples, childCopy_inheritsTuple);
		MemoryContextSwitchTo(oldcxt);
	}
	systable_endscan(scan);
	table_close(relation, AccessShareLock);

	if (*YBCGetGFlags()->ysql_enable_neghit_full_inheritscache &&
		YbNeedAdditionalCatalogTables() && !YbUseMinimalCatalogCachesPreload())
	{
		fully_loaded = true;
	}

	elog(yb_debug_log_catcache_events ? LOG : DEBUG3,
		 "YbPgInheritsCache: preload complete. Parent cache has %ld entries, "
		 " child cache has %ld entries.",
		 hash_get_num_entries(YbPgInheritsCacheByParent),
		 hash_get_num_entries(YbPgInheritsCacheByChild));
}

YbPgInheritsCacheEntry
GetYbPgInheritsCacheEntryByParent(Oid parentOid)
{
	Assert(YbPgInheritsCacheByParent);
	bool		found = false;
	YbPgInheritsCacheEntry entry = hash_search(YbPgInheritsCacheByParent,
											   (void *) &parentOid, HASH_ENTER,
											   &found);

	if (!found)
	{
		entry->oid = parentOid;
		entry->tuples = NIL;
		entry->refcount = 1;
		/* If we are fully loaded, we don't need to attempt a lookup */
		if (fully_loaded)
		{
			elog(DEBUG3, "YbPgInheritsCacheByParent neg hit for parentOid %d", parentOid);
		}
		else
		{
			elog(yb_debug_log_catcache_events ? LOG : DEBUG3,
				 "YbPgInheritsCacheByParent miss for parent %d", parentOid);
			YbNumCatalogCacheMisses++;
			YbNumCatalogCacheTableMisses[YbAdhocCacheTable_pg_inherits]++;
			FindChildren(parentOid, &entry->tuples);
		}
	}
	else
	{
		elog(DEBUG3, "YbPgInheritsCacheByParent hit for parentOid %d", parentOid);
	}
	YbPgInheritsIncrementReferenceCount(entry);
	return entry;
}

YbPgInheritsCacheEntry
GetYbPgInheritsCacheEntryByChild(Oid relid)
{
	bool		found = false;

	Assert(YbPgInheritsCacheByChild);
	YbPgInheritsCacheEntry entry = hash_search(YbPgInheritsCacheByChild,
											   (void *) &relid, HASH_ENTER,
											   &found);

	if (!found)
	{
		entry->oid = relid;
		entry->refcount = 1;
		entry->tuples = NIL;
		/* If we are fully loaded, we don't need to attempt a lookup */
		if (fully_loaded)
		{
			elog(DEBUG3, "YbPgInheritsCacheByChild neg hit for oid %d", relid);
		}
		else
		{
			YbNumCatalogCacheMisses++;
			YbNumCatalogCacheTableMisses[YbAdhocCacheTable_pg_inherits]++;
			elog(yb_debug_log_catcache_events ? LOG : DEBUG3,
				 "YbPgInheritsCacheByChild miss for oid %d", relid);
			GetChildCacheEntryMiss(relid, entry);
		}
	}
	else
	{
		elog(DEBUG3, "YbPgInheritsCacheByChild hit for oid %d", relid);
	}
	YbPgInheritsIncrementReferenceCount(entry);
	return entry;
}

void
ReleaseYbPgInheritsCacheEntry(YbPgInheritsCacheEntry entry)
{
	elog(DEBUG3,
		 "ReleaseYbPgInheritsCacheEntry for oid %d", entry->oid);

	YbPgInheritsDecrementReferenceCount(entry);
}


void
YbPgInheritsCacheDelete(YbPgInheritsCacheEntry entry, bool isParentEntry)
{
	elog(DEBUG3,
		 "YbPgInheritsCacheDelete for oid %d in %s cache",
		 entry->oid, isParentEntry ? "parent" : "child");
	Assert(entry->refcount == 1);
	list_free_deep(entry->tuples);
	if (isParentEntry)
	{
		Assert(YbPgInheritsCacheByParent);
		if (hash_search(YbPgInheritsCacheByParent,
						(void *) &entry->oid,
						HASH_REMOVE,
						NULL) == NULL)
			elog(ERROR, "Hash table corrupted. Relid %d", entry->oid);
	}
	else
	{
		Assert(YbPgInheritsCacheByChild);
		if (hash_search(YbPgInheritsCacheByChild,
						(void *) &entry->oid,
						HASH_REMOVE,
						NULL) == NULL)
			elog(ERROR, "Hash table corrupted. Relid %d", entry->oid);

	}
}

void
YbPgInheritsCacheInvalidateImpl(Oid relid, bool isParentEntry)
{
	fully_loaded = false;

	HASH_SEQ_STATUS status;
	YbPgInheritsCacheEntry entry;

	if (isParentEntry)
	{
		Assert(YbPgInheritsCacheByParent);
		hash_seq_init(&status, YbPgInheritsCacheByParent);
	}
	else
	{
		Assert(YbPgInheritsCacheByChild);
		hash_seq_init(&status, YbPgInheritsCacheByChild);
	}

	while ((entry = (YbPgInheritsCacheEntry) hash_seq_search(&status)) != NULL)
	{
		/*
		* Invalidate any entries where this oid is either the entry oid or in the tuple list
		*/
		if (relid == InvalidOid || entry->oid == relid)
		{
			elog(DEBUG3,
				 "YbPgInheritsCacheInvalidate: Invalidating %d in %s cache",
				 entry->oid, isParentEntry ? "parent" : "child");

			/*
			 * TODO: is it ok to delete hash entries while iterating through
			 * the hash map?
			 */
			YbPgInheritsCacheDelete(entry, isParentEntry);
			continue;
		}

		ListCell   *lc;

		foreach(lc, entry->tuples)
		{
			HeapTuple	tuple = (HeapTuple) lfirst(lc);
			Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(tuple);

			if (form->inhrelid == relid)
			{
				elog(DEBUG3,
					 "YbPgInheritsCacheInvalidate: Invalidating cache entry for %d"
					 " in %s cache due to invalidation of tuple entry %d",
					 form->inhparent, isParentEntry ? "parent" : "child", relid);
				YbPgInheritsCacheDelete(entry, isParentEntry);
				break;
			}
		}
	}


}

void
YbPgInheritsCacheInvalidate(Oid relid)
{

	elog(DEBUG3, "YbPgInheritsCacheInvalidate: invalidate request for %d", relid);
	YbPgInheritsCacheInvalidateImpl(relid, true);
	YbPgInheritsCacheInvalidateImpl(relid, false);
}
