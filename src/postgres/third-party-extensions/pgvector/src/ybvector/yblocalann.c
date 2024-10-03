/*--------------------------------------------------------------------------
 *
 * ybdummyann.c
 *	  Implementation of Yugabyte Vector Index dummy ANN access method. This
 *	  access method is meant for internal testing only and does not yield
 *	  ordered results.
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
 *		third-party-extensions/pgvector/ybvector/ybdummyann.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "ybvector.h"
#include "commands/ybccmds.h"

bool
yblocalannrcanreturn(Relation index, int attno)
{
	return false;
}

bool
yblocalanninsert(Relation index, Datum *values, bool *isnull, Datum ybctid,
			Relation heap, IndexUniqueCheck checkUnique,
			struct IndexInfo *indexInfo, bool shared_insert)
{
	return false;
}

void
yblocalanndelete(Relation index, Datum *values, bool *isnull, Datum ybctid,
			Relation heap, struct IndexInfo *indexInfo)
{
}

IndexBuildResult *
yblocalannbackfill(Relation heap, Relation index, struct IndexInfo *indexInfo,
			  struct YbBackfillInfo *bfinfo, struct YbPgExecOutParam *bfresult)
{
	return NULL;
}

IndexBuildResult *
yblocalannbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	IndexBuildResult *result = palloc0(sizeof(IndexBuildResult));
	result->heap_tuples = 0;
	result->index_tuples = 0;
	return result;
}

/*
 * yblocalannhandler handler function: return
 * IndexAmRoutine with access method parameters and callbacks.
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(yblocalannhandler);
Datum
yblocalannhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeBaseYbVectorHandler();
	amroutine->yb_amiscoveredbymaintable = true;

	amroutine->ambuild = yblocalannbuild;
	amroutine->amcanreturn = yblocalannrcanreturn;
	amroutine->yb_aminsert = yblocalanninsert;
	amroutine->yb_amdelete = yblocalanndelete;
	amroutine->yb_ambackfill = yblocalannbackfill;
	return (Datum) amroutine;
}
