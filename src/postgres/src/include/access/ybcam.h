/*--------------------------------------------------------------------------------------------------
 *
 * ybcam.h
 *	  prototypes for ybc/ybcam.c
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
 * src/include/executor/ybcam.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#ifndef YBCAM_H
#define YBCAM_H

#include "postgres.h"

#include "skey.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "nodes/relation.h"
#include "utils/catcache.h"
#include "utils/resowner.h"
#include "utils/snapshot.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"

typedef struct YbScanDescData
{
#define YB_MAX_SCAN_KEYS (INDEX_MAX_KEYS * 2) /* A pair of lower/upper bounds per column max */

	int     nkeys;
	ScanKey key;

	/* Attribut numbers and tuple descriptor for the scan keys / target columns */
	AttrNumber sk_attno[YB_MAX_SCAN_KEYS];
	TupleDesc  tupdesc;

	/* The handle for the internal YB Select statement. */
	YBCPgStatement  handle;
	ResourceOwner   stmt_owner;
	bool			is_exec_done;

	Relation index;

	/* Kept execution control to pass it to PgGate.
	 * - When YBC-index-scan layer is called by Postgres IndexScan functions, it will read the
	 *   "yb_exec_params" from Postgres IndexScan and kept the info in this attribute.
	 *
	 * - YBC-index-scan in-turn will passes this attribute to PgGate to control the index-scan
	 *   execution in YB tablet server.
	 */
	YBCPgExecParameters *exec_params;
} YbScanDescData;

typedef struct YbScanDescData *YbScanDesc;

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

/*
 * Access to YB-stored index (mirroring API from indexam.c)
 * We will do a YugaByte scan instead of a heap scan.
 * When the index is the primary key, the base table is scanned instead.
 */
extern void ybc_pkey_beginscan(Relation relation,
							   Relation index,
							   IndexScanDesc scan_desc,
							   int nkeys,
							   ScanKey key);
extern HeapTuple ybc_pkey_getnext(IndexScanDesc scan_desc, bool is_forward_scan);
extern void ybc_pkey_endscan(IndexScanDesc scan_desc);

extern void ybc_index_beginscan(Relation index,
								IndexScanDesc scan_desc,
								int nkeys,
								ScanKey key);
extern IndexTuple ybc_index_getnext(IndexScanDesc scan_desc, bool is_forward_scan);
extern void ybc_index_endscan(IndexScanDesc scan_desc);

/* Number of rows assumed for a YB table if no size estimates exist */
#define YBC_DEFAULT_NUM_ROWS  1000

#define YBC_SINGLE_KEY_SELECTIVITY	(1.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_HASH_SCAN_SELECTIVITY	(10.0 / YBC_DEFAULT_NUM_ROWS)
#define YBC_FULL_SCAN_SELECTIVITY	1.0

extern void ybcCostEstimate(RelOptInfo *baserel, Selectivity selectivity,
							Cost *startup_cost, Cost *total_cost);
extern void ybcIndexCostEstimate(IndexPath *path, Selectivity *selectivity,
								 Cost *startup_cost, Cost *total_cost);

/*
 * Fetch a single tuple by the ybctid.
 */
extern HeapTuple YBCFetchTuple(Relation relation, Datum ybctid);


#endif							/* YBCAM_H */
