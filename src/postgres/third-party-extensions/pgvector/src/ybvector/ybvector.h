/*--------------------------------------------------------------------------
 *
 * ybvector.h
 *	  Public header file for Yugabyte Vector Index access method.
 *
 * Copyright (c) YugabyteDB, Inc.
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
 *			src/include/access/ybvector.h
 *--------------------------------------------------------------------------
 */

#pragma once

#include "access/amapi.h"
#include "access/yb_scan.h"

#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"

#define YB_VECTOR_DIST_PROCNUM 1

typedef struct YbVectorScanOpaqueData
{
	/* The internal YB scan descriptor for the underlying Select statement. */
	YbScanDesc yb_scan_desc;

	/* The query vector on which we will do ANN. */
	Datum query_vector;

	/* Pushed down limit on the number of results. */
	int32_t limit;

	/* Has the first result been output yet? */
	bool first;
} YbVectorScanOpaqueData;

typedef YbVectorScanOpaqueData *YbVectorScanOpaque;

/* ybvector.c */
extern IndexBuildResult *ybvectorbuild(Relation heap, Relation index,
									struct IndexInfo *indexInfo);
extern void ybvectorbuildempty(Relation index);
extern IndexBulkDeleteResult *ybvectorbulkdelete(IndexVacuumInfo *info,
											  IndexBulkDeleteResult *stats,
											  IndexBulkDeleteCallback callback,
											  void *callback_state);
extern IndexBulkDeleteResult *ybvectorvacuumcleanup(IndexVacuumInfo *info,
												 IndexBulkDeleteResult *stats);
extern void ybvectorcostestimate(struct PlannerInfo *root,
							  struct IndexPath *path,
							  double loop_count,
							  Cost *indexStartupCost,
							  Cost *indexTotalCost,
							  Selectivity *indexSelectivity,
							  double *indexCorrelation,
							  double *indexPages);
extern bytea *ybvectoroptions(Datum reloptions, bool validate);
extern bool ybvectorvalidate(Oid opclassoid);
extern bool ybvectorcanreturn(Relation index, int attno);
extern IndexScanDesc ybvectorbeginscan(Relation rel, int nkeys, int norderbys);
extern void ybvectorrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
						ScanKey orderbys, int norderbys);
extern bool ybvectorgettuple(IndexScanDesc scan, ScanDirection dir);
extern void ybvectorendscan(IndexScanDesc scan);
extern bool ybvectorinsert(Relation rel, Datum *values, bool *isnull,
						Datum ybctid, Relation heapRel,
						IndexUniqueCheck checkUnique,
						struct IndexInfo *indexInfo, bool shared_insert);
extern void ybvectordelete(Relation rel, Datum *values, bool *isnull,
						Datum ybctid, Relation heapRel,
						struct IndexInfo *indexInfo);
extern IndexBuildResult *ybvectorbackfill(Relation heap, Relation index,
									   struct IndexInfo *indexInfo,
									   struct YbBackfillInfo *bfinfo,
									   struct YbPgExecOutParam *bfresult);
extern bool ybvectormightrecheck(Relation heapRelation, Relation indexRelation,
							  bool xs_want_itup, ScanKey keys, int nkeys);

IndexAmRoutine *makeBaseYbVectorHandler();

void
bindVectorIndexOptions(YBCPgStatement handle,
					   IndexInfo *indexInfo,
					   TupleDesc indexTupleDesc,
					   YbPgVectorIdxType ybpg_idx_type);
