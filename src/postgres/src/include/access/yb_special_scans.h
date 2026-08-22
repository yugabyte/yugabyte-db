/*-------------------------------------------------------------------------
 *
 * yb_special_scans.h
 *	  Specialized YB scan entry points.
 *
 *	  Scan APIs for consumers outside the executor scan nodes:
 *	  catalog scans (systable lookups), ANALYZE row sampling, and
 *	  single-row fetch by ybctid.  Unlike the interim accessors in
 *	  yb_table_scan.h, these are permanent AM-bypass surfaces: their
 *	  callers have no tableam callback to route through.
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
 * src/include/access/yb_special_scans.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/genam.h"
#include "access/htup.h"
#include "utils/relcache.h"
#include "yb/yql/pggate/ybc_pggate.h"

/*
 * Catalog scans (yb_catalog_scan.c): systable scan implementations
 * used by genam.c and the catalog readers.
 */
extern SysScanDesc ybc_systable_beginscan(Relation relation,
										  Oid indexId,
										  bool indexOK,
										  Snapshot snapshot,
										  int nkeys,
										  ScanKey key);

extern SysScanDesc ybc_systable_begin_default_scan(Relation relation,
												   Oid indexId,
												   bool indexOK,
												   Snapshot snapshot,
												   int nkeys,
												   ScanKey key);

/*
 * ANALYZE row sampling (yb_sample_scan.c).
 */
typedef struct YbSampleData
{
	/* The handle for the internal YB Sample statement. */
	YbcPgStatement handle;
	YbcPgExecParameters exec_params;

	Relation	relation;
	int			targrows;		/* # of rows to collect */
	double		liverows;		/* # live rows seen */
	double		deadrows;		/* # dead rows seen */
} YbSampleData;

typedef struct YbSampleData *YbSample;

extern YbSample ybBeginSample(Relation rel, int targrows);
extern bool ybSampleNextBlock(YbSample ybSample);
extern int	ybFetchSample(YbSample ybSample, HeapTuple *rows);

/*
 * Single-row fetch by ybctid (yb_ybctid_scan.c).
 */
extern bool YbFetchHeapTuple(Relation relation, Datum ybctid,
							 HeapTuple *tuple);
