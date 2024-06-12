/*-------------------------------------------------------------------------
 *
 * ybvectorutil.c
 *	  utility routines for the Yugabyte vector index access method.
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
 *		third-party-extensions/pgvector/ybvector/ybvectorutil.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "nodes/pathnodes.h"

bytea *
ybvectoroptions(Datum reloptions, bool validate)
{
	return NULL;
}

IndexBulkDeleteResult *
ybvectorvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	elog(WARNING, "Unexpected index cleanup via vacuum");
	return NULL;
}

/*
 * ybvectorcostestimate
 *
 *		Estimate the cost of scanning a vector index.
 *
 *		Not implemented yet.
 */
void
ybvectorcostestimate(PlannerInfo *root, IndexPath *path,
					 double loop_count, Cost *indexStartupCost,
					 Cost *indexTotalCost, Selectivity *indexSelectivity,
					 double *indexCorrelation, double *indexPages)
{
	return;
}

/*
 * ybvectorvalidate
 *
 *		Validate the vector index.
 *
 *		Not implemented yet.
 */
bool
ybvectorvalidate(Oid opclassoid)
{
	return true;
}

/*
 * ybvectorcanreturn
 *
 *		Allow index only scans on vector indexes.
 *
 */
bool
ybvectorcanreturn(Relation index, int attno)
{
	return true;
}
