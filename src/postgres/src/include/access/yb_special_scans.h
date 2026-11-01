/*-------------------------------------------------------------------------
 *
 * yb_special_scans.h
 *	  Specialized YB scan entry points.
 *
 *	  Scan APIs for consumers outside the executor scan nodes:
 *	  catalog scans (systable lookups), ANALYZE row sampling, and
 *	  single-row fetch by ybctid.  Unlike the interim accessors in
 *	  yb_table_scan.h, these stay, because no AM interface fits
 *	  them: tableam's analyze callbacks assume block sampling while
 *	  DocDB samples rows itself, tuple_fetch_row_version() takes a
 *	  heap TID rather than a ybctid, and
 *	  ybc_systable_begin_default_scan() serves the pg_inherits cache
 *	  that systable_beginscan() reads from.
 *	  ybc_systable_beginscan() is different: it is YB's
 *	  implementation of genam's systable_beginscan(), called only
 *	  from genam.c.  #36174 tracks replacing it with upstream's
 *	  systable_* path on the lsm and heap access methods.
 *
 *	  Each section below is the API of one implementation file.  A
 *	  contract that belongs to the callers' subsystem, such as the
 *	  format of the rows ANALYZE samples, goes in a header of that
 *	  subsystem, not here.
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
 * Catalog scans (yb_catalog_scan.c): ybc_systable_beginscan() is the YB
 * backend of systable_beginscan() in genam.c, and
 * ybc_systable_begin_default_scan() serves YB-only catalog readers such as
 * yb_inheritscache.c.  The SysScanDesc they return follows genam's systable
 * scan contract.
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
 * ANALYZE row sampling (yb_sample_scan.c): the YB backend of
 * yb_acquire_sample_rows() in analyze.c, which takes the place of
 * acquire_sample_rows() for YB relations.
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

	/* Per-attnum width caps, or NULL to skip nothing. */
	int		   *width_caps;
} YbSampleData;

typedef struct YbSampleData *YbSample;

extern YbSample ybBeginSample(Relation rel, int targrows, int *width_caps);
extern bool ybSampleNextBlock(YbSample ybSample);
extern int	ybFetchSample(YbSample ybSample, HeapTuple *rows);

/*
 * Single-row fetch by ybctid (yb_ybctid_scan.c): the YB counterpart of
 * heap_fetch() in heapam.c, keyed by ybctid in place of a heap TID.  Unlike
 * heap_fetch(), it takes no snapshot and reads at pggate's current read point.
 */
extern bool YbFetchHeapTuple(Relation relation, Datum ybctid,
							 HeapTuple *tuple);
