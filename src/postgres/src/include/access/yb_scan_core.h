/*-------------------------------------------------------------------------
 *
 * yb_scan_core.h
 *	  Internal YB scan types and functions for the AM layer.
 *
 *	  This header is intended for access-method implementations
 *	  (heapam, yb_lsm, ybgin, ybvector) that need direct access
 *	  to YbOpaque and the low-level scan primitives.  Code above
 *	  the AM layer (executor, optimizer) should use yb_table_scan.h
 *	  or yb_table_scan_options.h instead.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/include/access/yb_scan_core.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "access/tupdesc.h"
#include "access/yb_scan_plan.h"
#include "yb/yql/pggate/ybc_pggate.h"

/* GUC: skip binding scan keys during tests (see yb_scan_core.c) */
extern bool yb_test_skip_binding_scan_keys;
extern bool yb_enable_advanced_index_cond_fold;

/*
 * YbOpaqueData contains variables that are used throughout
 * the life of the scan statement.  The companion structure
 * YbScanPlanData (in yb_scan_plan.h) holds variables used only
 * during statement preparation.
 */
typedef struct YbOpaqueData
{
	/* The handle for the internal YB Select statement. */
	YbcPgStatement handle;
	bool		is_exec_done;

	/*
	 * These fields are constant and initialized during YbBeginScan.
	 * "table" is the table (not index).  It is set even for Index Only Scan.
	 * "index" is the index, if applicable.  NULL otherwise.
	 */
	Relation	table;
	Relation	index;

	/*
	 * ScanKey could be one of two types:
	 *  - key searching on DocDB hash code (aka yb_hash_code pushdown).
	 *  - otherwise
	 * hash_code_keys holds the first type; keys holds the second.
	 */
	ScanKey		keys[YB_MAX_SCAN_KEYS];
	/* Number of elements in the above array. */
	int			nkeys;
	/*
	 * List of ScanKey for keys with YB_SK_SEARCHHASHCODE (yb_hash_code
	 * pushdown).  Remember, YB_SK_SEARCHHASHCODE is not set for all
	 * yb_hash_code expressions!
	 *
	 * Prefer List over array because this is likely to have zero or a few
	 * elements in most cases.
	 */
	List	   *hash_code_keys;

	/*
	 * True if any type of recheck (YB or PG) is needed because not all scan
	 * keys are bound.  There could be false positives: it could say true when
	 * recheck is actually not needed.
	 */
	bool		needs_recheck;

	/* Destination for queried data from Yugabyte database */
	TupleDesc	target_desc;
	AttrNumber	target_key_attnums[YB_MAX_SCAN_KEYS];

	/* Kept query-plan control to pass it to PgGate during preparation */
	YbcPgPrepareParameters prepare_params;

	/*
	 * Kept execution control to pass it to PgGate.
	 * - When YBC-index-scan layer is called by Postgres IndexScan functions, it will read the
	 *   "yb_exec_params" from Postgres IndexScan and kept the info in this attribute.
	 *
	 * - YBC-index-scan in-turn will passes this attribute to PgGate to control the index-scan
	 *   execution in YB tablet server.
	 */
	YbcPgExecParameters *exec_params;

	/*
	 * Flag used for bailing out from scan early. Currently used to bail out
	 * from scans where one of the bind conditions is:
	 *   - A comparison operator with null, e.g.: c = null, etc.
	 *   - A search array and is empty.
	 *     Consider an example query,
	 *       select c1,c2 from test
	 *       where c1 = XYZ AND c2 = ANY(ARRAY[]::integer[]);
	 *     The second bind condition c2 = ANY(ARRAY[]::integer[]) will never be
	 *     satisfied.
	 * Hence when, such condition is detected, we bail out from creating and
	 * sending a request to docDB.
	 */
	bool		quit_scan;

	struct YBParallelPartitionKeysData *pscan;
} YbOpaqueData;
typedef struct YbOpaqueData *YbOpaque;

/*
 * YB table scan descriptor. Follows the PostgreSQL convention of embedding
 * TableScanDescData as the first field (rs_base) so that a
 * YbTableScanDesc pointer can be safely cast to/from a
 * TableScanDesc pointer.
 * This is analogous to HeapScanDescData for the heap AM.
 */
typedef struct YbTableScanDescData
{
	TableScanDescData rs_base;	/* AM independent part of the descriptor */
	YbOpaque	ybscan;			/* YB-specific scan state */
} YbTableScanDescData;
typedef struct YbTableScanDescData *YbTableScanDesc;

extern void ybc_free_ybscan(YbOpaque ybscan);

/*
 * YB implementations of the heap AM scan lifecycle, called from
 * heapam.c / heapam_handler.c when the relation is YB-backed.
 */
extern TableScanDesc ybc_heap_beginscan(Relation relation,
										Snapshot snapshot,
										int nkeys,
										ScanKey key,
										uint32 flags,
										struct YbTableScanOptions *yb_options);
extern HeapTuple ybc_heap_getnext(TableScanDesc scanDesc);
extern bool ybc_heap_getnextslot(TableScanDesc scanDesc,
								 ScanDirection direction,
								 TupleTableSlot *slot);
extern void ybc_heap_endscan(TableScanDesc scanDesc);

/*
 * Low-level scan setup: builds the PgGate statement, binds scan
 * keys, and configures targets/pushdowns.  Used by both table
 * scans (via ybc_heap_beginscan) and index scans (via yb_lsm).
 */
extern YbOpaque YbBeginScan(Relation table,
							Relation index,
							bool xs_want_itup,
							int nkeys,
							ScanKey keys,
							Scan *pg_scan_plan,
							YbPushdownExprs *rel_pushdown,
							YbPushdownExprs *idx_pushdown,
							List *aggrefs,
							int distinct_prefixlen,
							YbcPgExecParameters *exec_params,
							bool is_internal_scan,
							bool fetch_ybctids_only);

/* Returns whether the given populated ybScan needs PG recheck. */
extern bool YbNeedsPgRecheck(YbOpaque ybScan);

/*
 * Used in Agg node init phase to determine whether YB recheck or PG recheck
 * may be needed.
 */
extern bool YbPredetermineNeedsRecheck(Scan *scan,
									   Relation relation,
									   Relation index,
									   bool xs_want_itup,
									   ScanKey keys,
									   int nkeys);

/*
 * Low-level tuple fetch functions operating on YbOpaque directly.
 * Used by AM implementations (yb_lsm, ybgin) and catalog scans
 * (yb_catalog_scan) that manage their own scan state.
 */
extern HeapTuple ybc_getnext_heaptuple(YbOpaque ybScan, ScanDirection dir);
extern IndexTuple ybc_getnext_indextuple(YbOpaque ybScan, ScanDirection dir);
extern bool ybc_getnext_aggslot(IndexScanDesc scan, YbcPgStatement handle,
								bool index_only_scan);
extern void ybFetchNext(YbcPgStatement handle, TupleTableSlot *slot,
						Oid relid);
extern bool yb_scan_apply_next_parallel_range(YbcPgStatement handle,
											  YbcPgExecParameters *exec_params,
											  struct YBParallelPartitionKeysData *pscan);
