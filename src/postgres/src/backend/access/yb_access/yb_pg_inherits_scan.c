/*-------------------------------------------------------------------------
 *
 * yb_pg_inherits_scan.c
 *		This is an abstraction used for scanning the pg_inherits sys catalog
 *		table. We use a custom cache for pg_inherits, so that we can avoid trips
 *		to the YB-Master for every lookup. This cache lookup is abstracted under
 *		an interface similar to the system catalog scan interface.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *	  src/backend/access/yb_access/yb_pg_inherits_scan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "access/relscan.h"
#include "access/yb_pg_inherits_scan.h"
#include "access/yb_scan.h"
#include "catalog/indexing.h"
#include "catalog/pg_inherits_d.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/yb_inheritscache.h"

typedef struct YbChildScanData
{
	YbSysScanBaseData base;
	YbPgInheritsCacheChildEntry cache_entry;
	bool finished_processing;
} YbChildScanData;

typedef struct YbChildScanData *YbChildScan;

typedef struct YbParentScanData
{
	YbSysScanBaseData base;
	YbPgInheritsCacheEntry parent_cache_entry;
	List *tuples;
	ListCell *current_tuple;
} YbParentScanData;

typedef struct YbParentScanData *YbParentScan;

static HeapTuple
yb_lookup_cache_get_next(YbSysScanBase child_scan)
{
	YbChildScan scan = (void *)child_scan;
	if (scan->finished_processing || !scan->cache_entry)
		return NULL;
	// Only one row expected for child-based lookup. Set finished_processing
	// to true.
	scan->finished_processing = true;
	return scan->cache_entry->childTuple;
}

static void
yb_lookup_cache_end_scan(YbSysScanBase child_scan)
{
	YbChildScan scan = (void *)child_scan;
	if (scan->cache_entry)
		ReleaseYbPgInheritsChildEntry(scan->cache_entry);
}


static HeapTuple
yb_parent_get_next(YbSysScanBase parent_scan)
{
	YbParentScan scan = (void *)parent_scan;
	ListCell *ret = scan->current_tuple;
	if (ret == NULL)
		return NULL;
	scan->current_tuple = lnext(scan->tuples, scan->current_tuple);
	return lfirst(ret);
}

static void
yb_parent_end_scan(YbSysScanBase parent_scan)
{
	YbParentScan scan = (void *)parent_scan;
	Assert(scan->parent_cache_entry);
	ReleaseYbPgInheritsCacheEntry(scan->parent_cache_entry);
}

static YbSysScanVirtualTable yb_parent_scan =
	{.next = &yb_parent_get_next, .end = &yb_parent_end_scan};
static YbSysScanVirtualTable yb_child_scan =
	{.next = &yb_lookup_cache_get_next, .end = &yb_lookup_cache_end_scan};

static YbSysScanBase
YbInitSysScanDesc(YbSysScanBase scan, YbSysScanVirtualTable *vtable) {
	scan->vtable = vtable;
	return scan;
}

YbSysScanBase
yb_pg_inherits_beginscan(Relation inhrel, ScanKey key, int nkeys, Oid indexId)
{
	/*
	 * We only expect that this is a cache lookup based on the
	 * <parentrelid> or <childrelid> or<childrelid,inhseqno>. Verify that the
	 * keys match our expectations.
	 */
	Assert(key->sk_strategy == BTEqualStrategyNumber);
	Assert(key->sk_func.fn_oid == F_OIDEQ);

	if (key[0].sk_attno == Anum_pg_inherits_inhparent)
	{
		/*
		 * This is a lookup based on the parentrelid.
		 */
		Assert(nkeys == 1);
		YbParentScan scan = palloc0(sizeof(YbParentScanData));
		scan->parent_cache_entry =
			GetYbPgInheritsCacheEntry(DatumGetObjectId(key[0].sk_argument));
		scan->tuples = scan->parent_cache_entry->childTuples;
		scan->current_tuple = list_head(scan->tuples);
		return YbInitSysScanDesc(&scan->base, &yb_parent_scan);
	}

	/*
	 * If this is not a lookup request based on the parent relid, it must be
	 * based on the child.
	 */
	Assert(key[0].sk_attno == Anum_pg_inherits_inhrelid);

	if (nkeys == 2)
	{
		/*
		 * This should be a lookup for tuples with specific inhrelid and
		 * seq no. Verify that this indeed the case.
		 */
		Assert(key[1].sk_attno == Anum_pg_inherits_inhseqno);
		/*
		 * YB does not support inheritance. We only support native table partitioning. Therefore
		 * we only expect to see seqno == 1.
		*/
		Assert(DatumGetInt32(key[1].sk_argument) == 1);
	} else {
		/*
		 * This is a request to lookup all tuples in pg_inherits matching a given
		 * child relid.
		 */
		Assert(nkeys == 1);
	}

	YbChildScan scan = palloc0(sizeof(YbChildScanData));
	scan->cache_entry = GetYbPgInheritsChildCacheEntry(
		DatumGetObjectId(key[0].sk_argument));
	return YbInitSysScanDesc(&scan->base, &yb_child_scan);
}
