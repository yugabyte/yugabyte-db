/*--------------------------------------------------------------------------
 *
 * ybgin.h
 *	  Public header file for Yugabyte Generalized Inverted Index access method.
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
 *			src/include/access/ybgin.h
 *--------------------------------------------------------------------------
 */

#pragma once

#include "access/amapi.h"
#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"

/* ybgin.c */
extern IndexBuildResult *ybginbuild(Relation heap, Relation index,
									struct IndexInfo *indexInfo);
extern void ybginbuildempty(Relation index);
extern IndexBulkDeleteResult *ybginbulkdelete(IndexVacuumInfo *info,
											  IndexBulkDeleteResult *stats,
											  IndexBulkDeleteCallback callback,
											  void *callback_state);
extern IndexBulkDeleteResult *ybginvacuumcleanup(IndexVacuumInfo *info,
												 IndexBulkDeleteResult *stats);
extern void ybgincostestimate(struct PlannerInfo *root,
							  struct IndexPath *path,
							  double loop_count,
							  Cost *indexStartupCost,
							  Cost *indexTotalCost,
							  Selectivity *indexSelectivity,
							  double *indexCorrelation,
							  double *indexPages);
extern bytea *ybginoptions(Datum reloptions, bool validate);
extern bool ybginvalidate(Oid opclassoid);
extern IndexScanDesc ybginbeginscan(Relation rel, int nkeys, int norderbys);
extern void ybginrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,
						ScanKey orderbys, int norderbys);
extern bool ybgingettuple(IndexScanDesc scan, ScanDirection dir);
extern void ybginendscan(IndexScanDesc scan);
extern bool ybgininsert(Relation rel, Datum *values, bool *isnull,
						ItemPointer heap_tid, Relation heapRel,
						IndexUniqueCheck checkUnique,
						struct IndexInfo *indexInfo, bool shared_insert);
extern void ybgindelete(Relation rel, Datum *values, bool *isnull,
						Datum ybctid, Relation heapRel,
						struct IndexInfo *indexInfo);
extern IndexBuildResult *ybginbackfill(Relation heap, Relation index,
									   struct IndexInfo *indexInfo,
									   struct YbBackfillInfo *bfinfo,
									   struct YbPgExecOutParam *bfresult);
extern bool ybginmightrecheck(Relation heapRelation, Relation indexRelation,
							  bool xs_want_itup, ScanKey keys, int nkeys);
