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
#include "catalog/pg_inherits.h"
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
	YbPgInheritsCacheEntry child_cache_entry;
	/*
	 * If seqno > 0, it is a scan for a specific seqno
	 * otherwise it is a general scan by child rel oid.
	 */
	int			seqno;
	ListCell   *current_tuple;
} YbChildScanData;

typedef struct YbChildScanData *YbChildScan;

typedef struct YbParentScanData
{
	YbSysScanBaseData base;
	YbPgInheritsCacheEntry parent_cache_entry;
	List	   *tuples;
	ListCell   *current_tuple;
} YbParentScanData;

typedef struct YbParentScanData *YbParentScan;

static HeapTuple
yb_lookup_cache_get_next(YbSysScanBase child_scan)
{
	YbChildScan scan = (void *) child_scan;

	if (!scan->child_cache_entry || scan->seqno == -1)
		return NULL;

	if (scan->seqno > 0)
	{
		/*
		 * Scan for a specific entry (childoid, seqno) over all entries for childoid
		 */
		ListCell   *lc = NULL;

		foreach(lc, scan->child_cache_entry->tuples)
		{
			HeapTuple	tuple = (HeapTuple) lfirst(lc);
			Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(tuple);

			if (form->inhseqno == scan->seqno)
			{
				scan->seqno = -1;	/* return NULL next time */
				return tuple;
			}
		}
		scan->seqno = -1;
		return NULL;
	}

	if (scan->seqno == 0)
	{
		ListCell   *ret = scan->current_tuple;

		if (ret == NULL)
			return NULL;
		HeapTuple	tuple = (HeapTuple) lfirst(ret);
		Form_pg_inherits form = (Form_pg_inherits) GETSTRUCT(tuple);

		elog(DEBUG3,
			 "child scan for all seqno returns list tuple parent %d child %d seqno %d",
			 form->inhparent, form->inhrelid, form->inhseqno);
		scan->current_tuple = lnext(scan->child_cache_entry->tuples, scan->current_tuple);
		return tuple;
	}

	return NULL;
}

static void
yb_lookup_cache_end_scan(YbSysScanBase child_scan)
{
	YbChildScan scan = (void *) child_scan;

	if (scan->child_cache_entry)
		ReleaseYbPgInheritsCacheEntry(scan->child_cache_entry);
}


static HeapTuple
yb_parent_get_next(YbSysScanBase parent_scan)
{
	YbParentScan scan = (void *) parent_scan;
	ListCell   *ret = scan->current_tuple;

	if (ret == NULL)
		return NULL;
	scan->current_tuple = lnext(scan->tuples, scan->current_tuple);
	return lfirst(ret);
}

static void
yb_parent_end_scan(YbSysScanBase parent_scan)
{
	YbParentScan scan = (void *) parent_scan;

	Assert(scan->parent_cache_entry);
	ReleaseYbPgInheritsCacheEntry(scan->parent_cache_entry);
}

static YbSysScanVirtualTable yb_parent_scan = {
	.next = &yb_parent_get_next,
	.end = &yb_parent_end_scan
};
static YbSysScanVirtualTable yb_child_scan = {
	.next = &yb_lookup_cache_get_next,
	.end = &yb_lookup_cache_end_scan
};

static YbSysScanBase
YbInitSysScanDesc(YbSysScanBase scan, YbSysScanVirtualTable *vtable)
{
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
		elog(DEBUG3, "lookup by parent for oid %d ",
			 DatumGetObjectId(key[0].sk_argument));

		/*
		 * This is a lookup based on the parentrelid (inhparent).
		 */
		Assert(nkeys == 1);
		YbParentScan scan = palloc0(sizeof(YbParentScanData));

		scan->parent_cache_entry =
			GetYbPgInheritsCacheEntryByParent(DatumGetObjectId(key[0].sk_argument));
		scan->tuples = scan->parent_cache_entry->tuples;
		scan->current_tuple = list_head(scan->tuples);
		return YbInitSysScanDesc(&scan->base, &yb_parent_scan);
	}

	/*
	 * If this is not a lookup request based on the parent relid, it must be
	 * based on the child (inhrelid).
	 */
	Assert(key[0].sk_attno == Anum_pg_inherits_inhrelid);

	YbChildScan scan = palloc0(sizeof(YbChildScanData));

	scan->seqno = -1;
	scan->child_cache_entry = NULL;
	scan->current_tuple = NULL;
	if (nkeys == 2)
	{
		/*
		 * This should be a lookup for tuples with inhlreid and inhseqno set
		 * to these two values
		 */
		Assert(key[1].sk_attno == Anum_pg_inherits_inhseqno);
		scan->seqno = DatumGetInt32(key[1].sk_argument);
		elog(DEBUG3, "lookup by child oid %d and seqno %d",
			 DatumGetObjectId(key[0].sk_argument),
			 scan->seqno);

	}
	else
	{

		elog(DEBUG3, "lookup by child oid %d", DatumGetObjectId(key[0].sk_argument));
		/*
		 * This is a request to lookup all tuples in pg_inherits matching a given
		 * child relid.
		 */
		Assert(nkeys == 1);
		scan->seqno = 0;
	}

	scan->child_cache_entry = GetYbPgInheritsCacheEntryByChild(DatumGetObjectId(key[0].sk_argument));
	if (scan->child_cache_entry && scan->child_cache_entry->tuples && scan->seqno == 0)
	{
		scan->current_tuple = list_head(scan->child_cache_entry->tuples);
	}
	return YbInitSysScanDesc(&scan->base, &yb_child_scan);
}
