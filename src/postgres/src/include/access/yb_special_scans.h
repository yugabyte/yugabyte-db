/*-------------------------------------------------------------------------
 *
 * yb_special_scans.h
 *	  Specialized YB scan entry points.
 *
 *	  Scan APIs for consumers outside the executor scan nodes:
 *	  catalog scans (systable lookups), ANALYZE row sampling, and
 *	  single-row fetch by ybctid.  Unlike the interim accessors in
 *	  yb_table_scan.h, these stay until an AM interface fits them.
 *
 *	  Each section below is the API of one implementation file and
 *	  says why no AM interface replaces it.  A contract that belongs
 *	  to the callers' subsystem, such as the format of the rows
 *	  ANALYZE samples, goes in a header of that subsystem, not here.
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
 * Catalog scans (yb_catalog_scan.c): ybc_systable_beginscan() implements
 * genam's systable_beginscan() and is called only from genam.c.
 * ybc_systable_begin_default_scan() exists so that yb_inheritscache.c can fill
 * the pg_inherits cache.  How these scans read is intricate.  A change that
 * needs more from this API should not add to it but first rework it by routing
 * catalog scans through upstream's systable_* path, as #36174 describes.
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
 * acquire_sample_rows() for YB relations.  tableam's analyze callbacks do not
 * fit, because they read the block numbers that the caller's BlockSampler
 * chooses, while YB's sampler reads the table in its own blocks, runs of rows
 * from one partition, and selects the sample rows below PostgreSQL.
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
 * heap_fetch() in heapam.c, keyed by ybctid in place of a heap TID, so
 * tableam's tuple_fetch_row_version() does not fit.
 */
extern bool YbFetchHeapTuple(Relation relation, Datum ybctid,
							 HeapTuple *tuple);
