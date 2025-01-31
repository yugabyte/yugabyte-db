/*--------------------------------------------------------------------------
 *
 * ybhnsw.c
 *	  Access method for Yugabyte Vector Index implementation using HNSW.
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
 *		third-party-extensions/pgvector/ybvector/ybhnsw.c
 *--------------------------------------------------------------------------
 */

#include "postgres.h"

#include "ybvector.h"
#include "commands/yb_cmds.h"

static void
ybhnswbindcolumnschema(YbcPgStatement handle,
					   IndexInfo *indexInfo,
					   TupleDesc indexTupleDesc,
					   int16 *coloptions)
{
	bindVectorIndexOptions(handle, indexInfo, indexTupleDesc, YB_VEC_HNSW);
	YBCBindCreateIndexColumns(handle, indexInfo, indexTupleDesc, coloptions, 0);
}


/*
 * ybusearchhandler handler function: return
 * IndexAmRoutine with access method parameters and callbacks.
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(ybhnswhandler);
Datum
ybhnswhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine =
		makeBaseYbVectorHandler(true /* is_copartitioned */);
	amroutine->yb_ambindschema = ybhnswbindcolumnschema;

	PG_RETURN_POINTER(amroutine);
}
