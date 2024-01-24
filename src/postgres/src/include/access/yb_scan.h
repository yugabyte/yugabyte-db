/*--------------------------------------------------------------------------------------------------
 *
 * yb_scan.h
 *	  prototypes for yb_access/yb_scan.c
 *
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
 * src/include/access/yb_scan.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "skey.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/itup.h"
#include "nodes/relation.h"
#include "utils/catcache.h"
#include "utils/resowner.h"
#include "utils/sampling.h"
#include "utils/snapshot.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"

/*
 * SCAN PLAN - Two structures.
 * - "struct YbScanPlanData" contains variables that are used during preparing statement.
 * - "struct YbScanDescData" contains variables that are used thru out the life of the statement.
 *
 * YugaByte ScanPlan has two different lists.
 *   Binding list:
 *   - This list is used to receive the user-given data for key columns (WHERE clause).
 *   - "sk_attno" is the INDEX's attnum for the key columns that are used to query data.
 *     In YugaByte, primary index attnum is the same as column attnum in the UserTable.
 *   - "bind_desc" is the description of the index columns (count, types, ...).
 *   - The bind lists don't need to be included in the description as they are only needed
 *     during setup.
 *   Target list:
 *   - This list identifies the user-wanted data to be fetched from the database.
 *   - "target_desc" contains the description of the target columns (count, types, ...).
 *   - The target fields are used for double-checking key values after selecting data
 *     from database. It should be removed once we no longer need to double-check the key values.
 */
typedef struct YbScanDescData
{
#define YB_MAX_SCAN_KEYS (INDEX_MAX_KEYS * 2) /* A pair of lower/upper bounds per column max */

	/* The handle for the internal YB Select statement. */
	YBCPgStatement handle;
	bool is_exec_done;

	Relation relation;
	Relation index;

	/*
	 * ScanKey could be one of two types:
	 *  - key which represents the yb_hash_code function.
	 *  - otherwise
	 * hash_code_keys holds the first type; keys holds the second.
	 */
	ScanKey keys[YB_MAX_SCAN_KEYS];
	/* Number of elements in the above array. */
	int nkeys;
	/*
	 * List of ScanKey for keys which represent the yb_hash_code function.
	 * Prefer List over array because this is likely to have zero or a few
	 * elements in most cases.
	 */
	List *hash_code_keys;

	/*
	 * True if all ordinary (non-yb_hash_code) keys are bound to pggate.  There
	 * could be false negatives: it could say false when they are in fact all
	 * bound.
	 */
	bool all_ordinary_keys_bound;

	TupleDesc target_desc;
	AttrNumber target_key_attnums[YB_MAX_SCAN_KEYS];

	/* Kept query-plan control to pass it to PgGate during preparation */
	YBCPgPrepareParameters prepare_params;

	/*
	 * Kept execution control to pass it to PgGate.
	 * - When YBC-index-scan layer is called by Postgres IndexScan functions, it will read the
	 *   "yb_exec_params" from Postgres IndexScan and kept the info in this attribute.
	 *
	 * - YBC-index-scan in-turn will passes this attribute to PgGate to control the index-scan
	 *   execution in YB tablet server.
	 */
	YBCPgExecParameters *exec_params;

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
	bool quit_scan;
} YbScanDescData;

typedef struct YbScanDescData *YbScanDesc;

extern void ybc_free_ybscan(YbScanDesc ybscan);

/*
 * Access to YB-stored system catalogs (mirroring API from genam.c)
 * We ignore the index id and always do a regular YugaByte scan (Postgres
 * would do either heap scan or index scan depending on the params).
 */
extern SysScanDesc ybc_systable_beginscan(Relation relation,
										  Oid indexId,
										  bool indexOK,
										  Snapshot snapshot,
										  int nkeys,
										  ScanKey key);
extern HeapTuple ybc_systable_getnext(SysScanDesc scanDesc);
extern void ybc_systable_endscan(SysScanDesc scan_desc);

/*
 * Access to YB-stored system catalogs (mirroring API from heapam.c)
 * We will do a YugaByte scan instead of a heap scan.
 */
extern HeapScanDesc ybc_heap_beginscan(Relation relation,
                                       Snapshot snapshot,
                                       int nkeys,
                                       ScanKey key,
									   bool temp_snap);
extern HeapTuple ybc_heap_getnext(HeapScanDesc scanDesc);
extern void ybc_heap_endscan(HeapScanDesc scanDesc);
extern HeapScanDesc ybc_remote_beginscan(Relation relation,
										 Snapshot snapshot,
										 Scan *pg_scan_plan,
										 PushdownExprs *pushdown,
										 List *aggrefs,
										 YBCPgExecParameters *exec_params);

/* Add targets to the given statement. */
extern void YbDmlAppendTargetSystem(AttrNumber attnum, YBCPgStatement handle);
extern void YbDmlAppendTargetRegular(TupleDesc tupdesc, AttrNumber attnum,
									 YBCPgStatement handle);
extern void YbDmlAppendTargetsAggregate(List *aggrefs, TupleDesc tupdesc,
										Relation index, bool xs_want_itup,
										YBCPgStatement handle);
extern void YbDmlAppendTargets(List *colrefs, YBCPgStatement handle);
/* Add quals to the given statement. */
extern void YbDmlAppendQuals(List *quals, bool is_primary,
							 YBCPgStatement handle);
/* Add column references to the given statement. */
extern void YbDmlAppendColumnRefs(List *colrefs, bool is_primary,
								  YBCPgStatement handle);

/*
 * The ybc_idx API is used to process the following SELECT.
 *   SELECT data FROM heapRelation WHERE rowid IN
 *     ( SELECT rowid FROM indexRelation WHERE key = given_value )
 */
extern YbScanDesc ybcBeginScan(Relation relation,
							   Relation index,
							   bool xs_want_itup,
							   int nkeys,
							   ScanKey key,
							   Scan *pg_scan_plan,
							   PushdownExprs *rel_pushdown,
							   PushdownExprs *idx_pushdown,
							   List *aggrefs,
							   int distinct_prefixlen,
							   YBCPgExecParameters *exec_params,
							   bool is_internal_scan);

/* Returns whether the given populated ybScan needs PG recheck. */
extern bool YbNeedsPgRecheck(YbScanDesc ybScan);
/*
 * Used in Agg node init phase to determine whether YB preliminary check or PG
 * recheck may be needed.
 */
extern bool YbPredetermineNeedsRecheck(Relation relation,
									   Relation index,
									   bool xs_want_itup,
									   ScanKey keys,
									   int nkeys);

HeapTuple ybc_getnext_heaptuple(YbScanDesc ybScan, bool is_forward_scan, bool *recheck);
IndexTuple ybc_getnext_indextuple(YbScanDesc ybScan, bool is_forward_scan, bool *recheck);
bool ybc_getnext_aggslot(IndexScanDesc scan, YBCPgStatement handle,
						 bool index_only_scan);

Oid ybc_get_attcollation(TupleDesc bind_desc, AttrNumber attnum);

/* Number of rows assumed for a YB table if no size estimates exist */
#define YBC_DEFAULT_NUM_ROWS  1000

#define YBC_SINGLE_ROW_SELECTIVITY	(1.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_SINGLE_KEY_SELECTIVITY	(10.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_HASH_SCAN_SELECTIVITY	(100.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_FULL_SCAN_SELECTIVITY	1.0

/*
 * For a partial index the index predicate will filter away some rows.
 * TODO: Evaluate this based on the predicate itself and table stats.
 */
#define YBC_PARTIAL_IDX_PRED_SELECTIVITY 0.8

/*
 * Backwards scans are more expensive in DocDB.
 */
#define YBC_BACKWARDS_SCAN_COST_FACTOR 1.1

/*
 * Uncovered indexes will require extra RPCs to the main table to retrieve the
 * values for all required columns. These requests are now batched in PgGate
 * so the extra cost should be relatively low in general.
 */
#define YBC_UNCOVERED_INDEX_COST_FACTOR 1.1

extern void ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
							bool is_backwards_scan, bool is_seq_scan, bool is_uncovered_idx_scan,
							Cost *startup_cost, Cost *total_cost, Oid index_tablespace_oid);
extern void ybcIndexCostEstimate(struct PlannerInfo *root, IndexPath *path,
								 			Selectivity *selectivity, Cost *startup_cost,
											Cost *total_cost);

/*
 * Fetch a single tuple by the ybctid.
 */
extern HeapTuple YBCFetchTuple(Relation relation, Datum ybctid);
extern HTSU_Result YBCLockTuple(Relation relation, Datum ybctid, RowMarkType mode,
												 LockWaitPolicy wait_policy, EState* estate);

/*
 * ANALYZE support: sampling of table data
 */
typedef struct YbSampleData
{
	/* The handle for the internal YB Sample statement. */
	YBCPgStatement handle;

	Relation	relation;
	int			targrows;	/* # of rows to collect */
	double		liverows;	/* # live rows seen */
	double		deadrows;	/* # dead rows seen */
} YbSampleData;

typedef struct YbSampleData *YbSample;

YbSample ybBeginSample(Relation rel, int targrows);
bool ybSampleNextBlock(YbSample ybSample);
int ybFetchSample(YbSample ybSample, HeapTuple *rows);
TupleTableSlot *ybFetchNext(YBCPgStatement handle,
			TupleTableSlot *slot, Oid relid);
