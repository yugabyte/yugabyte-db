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
#include "utils/catcache.h"
#include "utils/resowner.h"
#include "utils/snapshot.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"
#include "executor/ybcScan.h"

typedef struct YbSysScanDescData
{
	YbScanState state;
	int nkeys;
	ScanKey key;
} YbSysScanDescData;

typedef struct YbSysScanDescData *YbSysScanDesc;

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
 */
extern void ybc_index_beginscan(Relation relation,
								IndexScanDesc scan_desc,
								int nkeys,
								ScanKey key);
extern HeapTuple ybc_index_getnext(IndexScanDesc scan_desc);
extern void ybc_index_endscan(IndexScanDesc scan_desc);

/*
 * Fetch a single tuple by the ybctid.
 */
extern HeapTuple YBCFetchTuple(Relation relation, Datum ybctid);


#endif							/* YBCAM_H */
