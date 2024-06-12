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

static void
ybdummyannbindcolumnschema(YBCPgStatement handle,
						   IndexInfo *indexInfo,
						   TupleDesc indexTupleDesc,
						   int16 *coloptions)
{
	elog(WARNING,
		 "ybdummyann is meant for internal-testing only and "
		 "does not yield ordered results");
	bindVectorIndexOptions(handle, indexInfo, indexTupleDesc, YB_VEC_DUMMY);
	YBCBindCreateIndexColumns(
		handle, indexInfo, indexTupleDesc, coloptions, 0);
}


/*
 * ybdummyannhandler handler function: return
 * IndexAmRoutine with access method parameters and callbacks.
 */
PGDLLEXPORT PG_FUNCTION_INFO_V1(ybdummyannhandler);
Datum
ybdummyannhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeBaseYbVectorHandler();
	amroutine->yb_ambindschema = ybdummyannbindcolumnschema;

	PG_RETURN_POINTER(amroutine);
}
