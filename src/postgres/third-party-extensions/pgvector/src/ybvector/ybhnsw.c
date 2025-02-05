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
#include "catalog/pg_opclass.h"
#include "commands/yb_cmds.h"
#include "utils/syscache.h"

static void
ybhnswbindcolumnschema(YbcPgStatement handle,
					   IndexInfo *indexInfo,
					   TupleDesc indexTupleDesc,
					   int16 *coloptions,
					   Oid *opclassOids)
{
	HeapTuple	ht_opc;
	Form_pg_opclass opcrec;

	Assert(indexInfo->ii_NumIndexKeyAttrs == 1);
	ht_opc = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclassOids[0]));
	if (!HeapTupleIsValid(ht_opc))
		elog(ERROR, "cache lookup failed for opclass %u", opclassOids[0]);
	opcrec = (Form_pg_opclass) GETSTRUCT(ht_opc);
	YbcPgVectorDistType dist_type;
	if (!strcmp(opcrec->opcname.data, "vector_l2_ops")) {
		dist_type = YB_VEC_DIST_L2;
	} else if (!strcmp(opcrec->opcname.data, "vector_ip_ops")) {
		dist_type = YB_VEC_DIST_IP;
	} else if (!strcmp(opcrec->opcname.data, "vector_cosine_ops")) {
		dist_type = YB_VEC_DIST_COSINE;
	} else {
		elog(ERROR, "unsupported vector index op class name %s",
			 opcrec->opcname.data);
	}
	ReleaseSysCache(ht_opc);

	bindVectorIndexOptions(
		handle, indexInfo, indexTupleDesc, YB_VEC_HNSW, dist_type);
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
