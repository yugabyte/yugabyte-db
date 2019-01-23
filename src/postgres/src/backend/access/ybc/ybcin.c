/*--------------------------------------------------------------------------------------------------
 *
 * ybcin.c
 *	  Implementation of YugaByte indexes.
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
 * src/backend/access/ybc/ybcin.c
 *
 * TODO: currently this file contains skeleton index access methods. They will be implemented in
 * coming revisions.
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/ybcin.h"
#include "catalog/index.h"
#include "utils/rel.h"

/* --------------------------------------------------------------------------------------------- */

IndexBuildResult *
ybcinbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	Assert(!index->rd_index->indisprimary);

	/*
	 * Return statistics
	 */
	IndexBuildResult *result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));

	result->heap_tuples  = 0;
	result->index_tuples = 0;
	return result;
}

void
ybcinbuildempty(Relation index)
{
}

bool
ybcininsert(Relation index, Datum *values, bool *isnull, ItemPointer ht_ctid, Relation heap,
			IndexUniqueCheck checkUnique, struct IndexInfo *indexInfo)
{
	Assert(!index->rd_index->indisprimary);

	return false;
}

IndexBulkDeleteResult *
ybcinbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of index via vacuum");
	return NULL;
}

IndexBulkDeleteResult *
ybcinvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	YBC_LOG_WARNING("Unexpected index cleanup via vacuum");
	return NULL;
}

/* --------------------------------------------------------------------------------------------- */

bool ybcincanreturn(Relation index, int attno)
{
	return false;
}

void
ybcincostestimate(struct PlannerInfo *root, struct IndexPath *path, double loop_count,
				  Cost *indexStartupCost, Cost *indexTotalCost, Selectivity *indexSelectivity,
				  double *indexCorrelation, double *indexPages)
{
}

bytea *
ybcinoptions(Datum reloptions, bool validate)
{
	return NULL;
}

bool
ybcinproperty(Oid index_oid, int attno, IndexAMProperty prop, const char *propname,
			  bool *res, bool *isnull)
{
	return false;	
}

bool
ybcinvalidate(Oid opclassoid)
{
	return true;
}

/* --------------------------------------------------------------------------------------------- */

IndexScanDesc
ybcinbeginscan(Relation rel, int nkeys, int norderbys)
{
	return NULL;
}

void 
ybcinrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,	ScanKey orderbys, int norderbys)
{
}

bool
ybcingettuple(IndexScanDesc scan, ScanDirection dir)
{
	return false;
}

int64
ybcingetbitmap(IndexScanDesc scan, TIDBitmap *tbm)
{
	return 0;
}

void 
ybcinendscan(IndexScanDesc scan)
{
}

/* --------------------------------------------------------------------------------------------- */

void 
ybcinmarkpos(IndexScanDesc scan)
{
}

void 
ybcinrestrpos(IndexScanDesc scan)
{
}
