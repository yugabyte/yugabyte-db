/*-------------------------------------------------------------------------
 *
 * ybvectorwrite.c
 *	  insert and delete routines for the Yugabyte vector index access method.
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
 *		third-party-extensions/pgvector/ybvector/ybvectorwrite.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "ybvector.h"

#include "access/genam.h"
#include "access/sysattr.h"
#include "access/yb_scan.h"
#include "c.h"
#include "catalog/index.h"
#include "catalog/pg_am.h"
#include "catalog/pg_type.h"
#include "catalog/yb_type.h"
#include "commands/ybccmds.h"
#include "executor/ybcModifyTable.h"
#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"
#include "pg_yb_utils.h"
#include "storage/off.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"

typedef struct {
	/* Settings */
	int			dimensions;
	Oid			collation;

	/* Support functions */
	FmgrInfo   *dist_procinfo;

	/* Statistics */
	double		indtuples;
	double		reltuples;

	uint64_t   *backfilltime;

	/* Memory context to build index tuples with. */
	MemoryContext tmpCtx;
} YbVectorBuildState;

/*
 * Binds vector index option during creation.
 */
void
bindVectorIndexOptions(YBCPgStatement handle,
					   IndexInfo *indexInfo,
					   TupleDesc indexTupleDesc,
					   YbPgVectorIdxType ybpg_idx_type)
{
	YbPgVectorIdxOptions options;
	options.idx_type = ybpg_idx_type;

	/*
	 * Hardcoded for now.
	 * TODO(tanuj): Pass down distance info from the used distance opclass.
	 */
	options.dist_type = YB_VEC_DIST_L2;

	/* We only support indexes with one vector attribute for now. */
	Assert(indexTupleDesc->natts == 1);

	/* Assuming vector is the first att */;
	options.dimensions = TupleDescAttr(indexTupleDesc, 0)->atttypmod;
	Assert(options.dimensions > 0);

	YBCPgCreateIndexSetVectorOptions(handle, &options);
}

/*
 * Utility method to set binds for index write statement.
 * Copied from ybginwrite.c.
 */
static void
doBindsForIdx(YBCPgStatement stmt,
			  void *indexstate,
			  Relation index,
			  Datum *values,
			  bool *isnull,
			  int n_bound_atts,
			  Datum ybbasectid,
			  bool ybctid_as_value,
			  bool is_for_delete)
{

	if (ybbasectid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("missing base table ybctid in index write request")));

	/*
	 * Supporting vector-only indexes for now. It shouldn't be too hard to
	 * extend this to other attributes later.
	 */
	Assert(n_bound_atts == 1);

	AttrNumber vec_attnum = 1;
	Oid			type_id = GetTypeId(vec_attnum,  RelationGetDescr(index));
	Oid 		collation_id = index->rd_indcollation[0];
	Datum		vector_val   = values[vec_attnum - 1];
	bool		is_null = isnull[vec_attnum - 1];

	YbBindDatumToColumn(stmt,
						YBIdxBaseTupleIdAttributeNumber,
						BYTEAOID,
						InvalidOid,
						ybbasectid,
						false /* is_null */,
						NULL /* null_type_entity */);

	/* If we are doing a delete there is no need to bind non-key columns. */
	if (!is_for_delete)
		YbBindDatumToColumn(
			stmt, vec_attnum, type_id, collation_id, vector_val, is_null,
				NULL /* null_type_entity */);
}

/*
 * Callback method to set binds for index write. Copied from
 * ybginwrite.c.
 */
static void
doBindsForIdxWrite(YBCPgStatement stmt,
				   void *indexstate,
				   Relation index,
				   Datum *values,
				   bool *isnull,
				   int n_bound_atts,
				   Datum ybbasectid,
				   bool ybctid_as_value)
{
	doBindsForIdx(stmt, indexstate, index, values, isnull, n_bound_atts,
				  ybbasectid, ybctid_as_value, false /* is_for_delete */);
}

/*
 * Callback method to set binds for index delete. Copied from
 * ybginwrite.c.
 */
static void
doBindsForIdxDelete(YBCPgStatement stmt,
				   void *indexstate,
				   Relation index,
				   Datum *values,
				   bool *isnull,
				   int n_bound_atts,
				   Datum ybbasectid,
				   bool ybctid_as_value)
{
	doBindsForIdx(stmt, indexstate, index, values, isnull, n_bound_atts,
				  ybbasectid, ybctid_as_value, true /* is_for_delete */);
}

/*
 * Extract entries and write values.
 *
 */
static int32
ybVectorTupleWrite(YbVectorBuildState *vectorstate, OffsetNumber attnum,
				   Relation index, Datum value, bool isNull,
				   Datum ybctid, uint64_t *backfilltime, bool isinsert)
{
	Datum vector;
	if (!isNull)
		vector = PointerGetDatum(PG_DETOAST_DATUM(value));

	/* Make sure that this is a single-column index. */
	Assert(RelationGetNumberOfAttributes(index) == 1);

	if (isinsert)
		YBCExecuteInsertIndex(index, &vector, &isNull, ybctid,
							  backfilltime /* backfill_write_time */,
							  doBindsForIdxWrite, (void *) vectorstate);
	else
	{
		Assert(!backfilltime);
		YBCExecuteDeleteIndex(index, &vector, &isNull, ybctid,
							  doBindsForIdxDelete, (void *) vectorstate);
	}

	return 1;
}

/*
 * Delete index entries for a single indexable item during "normal"
 * (non-fast-update) insertion
 */
static int32
ybvectorTupleDelete(YbVectorBuildState *vectorstate, OffsetNumber attnum,
				 Relation index, Datum value, bool isNull,
				 Datum ybctid)
{
	return ybVectorTupleWrite(vectorstate, attnum, index, value, isNull, ybctid,
						   NULL /* backfilltime */, false /* isinsert */);
}

/*
 * Insert index entries for a single indexable item during "normal"
 * (non-fast-update) insertion
 */
static int32
ybVectorTupleInsert(YbVectorBuildState *vectorstate, OffsetNumber attnum,
				 Relation index, Datum value, bool isNull,
				 Datum ybctid, uint64_t *backfilltime)
{
	return ybVectorTupleWrite(vectorstate, attnum, index, value, isNull,
							  ybctid, backfilltime, true /* isinsert */);
}

/*
 * Callback to insert index tuples after a base table tuple is retrieved.  See
 * similar ybcinbuildCallback.
 */
static void
ybvectorBuildCallback(Relation index, HeapTuple heapTuple, Datum *values,
				   bool *isnull, bool tupleIsAlive, void *state)
{
	YbVectorBuildState *buildstate = (YbVectorBuildState *) state;
	MemoryContext oldCtx;

	/*
	 * The inserted vector might be detoasted in here so switch to a
	 * temp context.
	 */
	oldCtx = MemoryContextSwitchTo(buildstate->tmpCtx);
	ybVectorTupleInsert(buildstate, 1, index, values[0], isnull[0],
		heapTuple->t_ybctid, buildstate->backfilltime);

	buildstate->indtuples += 1;

	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->tmpCtx);
}

/*
 * Parts copied from ivfbuild.c's InitBuildState
 */
static void
initVectorState(YbVectorBuildState *buildstate,
				Relation heap, Relation index, IndexInfo *indexInfo)
{
	buildstate->dimensions = TupleDescAttr(index->rd_att, 0)->atttypmod;

	/* Require column to have dimensions to be indexed */
	if (buildstate->dimensions < 0)
		elog(ERROR, "column does not have dimensions");

	buildstate->reltuples = 0;
	buildstate->indtuples = 0;

	/* Get support functions */
	buildstate->dist_procinfo =
		index_getprocinfo(index, 1, YB_VECTOR_DIST_PROCNUM);

	buildstate->collation = index->rd_indcollation[0];

	buildstate->tmpCtx = AllocSetContextCreate(CurrentMemoryContext,
											   "ann build temporary context",
											   ALLOCSET_DEFAULT_SIZES);
}

/*
 * Build code for both ybvectorbuild and ybvectorbackfill.
 */
static IndexBuildResult *
ybvectorBuildCommon(Relation heap, Relation index, struct IndexInfo *indexInfo,
				 struct YbBackfillInfo *bfinfo,
				 struct YbPgExecOutParam *bfresult)
{
	IndexBuildResult *result;
	double		reltuples;
	YbVectorBuildState buildstate;

	initVectorState(&buildstate, heap, index, indexInfo);
	buildstate.indtuples = 0;
	if (bfinfo)
		buildstate.backfilltime = &bfinfo->read_time;
	else
		buildstate.backfilltime = NULL;

	/*
	 * Do the heap scan.
	 */
	if (!bfinfo)
		reltuples = IndexBuildHeapScan(heap, index, indexInfo, true,
									   ybvectorBuildCallback, (void *) &buildstate,
									   NULL /* HeapScanDesc */);
	else
		reltuples = IndexBackfillHeapRangeScan(heap, index, indexInfo,
											   ybvectorBuildCallback,
											   (void *) &buildstate,
											   bfinfo,
											   bfresult);

	MemoryContextDelete(buildstate.tmpCtx);

	/*
	 * Return statistics
	 */
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));

	result->heap_tuples = reltuples;
	result->index_tuples = buildstate.indtuples;

	return result;
}
/*
 * Write code for both ybvectorinsert and ybvectordelete.
 */
static void
ybvectorWrite(Relation index, Datum *values, bool *isnull, Datum ybctid,
		   Relation heap, struct IndexInfo *indexInfo, bool isinsert)
{
	YbVectorBuildState   *vectorstate =
		(YbVectorBuildState *) indexInfo->ii_AmCache;
	MemoryContext oldCtx;
	MemoryContext writeCtx;

	/* Initialize VectorState cache if first call in this statement */
	if (vectorstate == NULL)
	{
		oldCtx = MemoryContextSwitchTo(indexInfo->ii_Context);
		vectorstate =
			(YbVectorBuildState *) palloc(sizeof(YbVectorBuildState));
		initVectorState(vectorstate, heap, index, indexInfo);
		indexInfo->ii_AmCache = (void *) vectorstate;
		MemoryContextSwitchTo(oldCtx);
	}

	writeCtx = AllocSetContextCreate(GetCurrentMemoryContext(),
									 "Ybvector write temporary context",
									 ALLOCSET_DEFAULT_SIZES);

	oldCtx = MemoryContextSwitchTo(writeCtx);

	int i = 0;

	if (isinsert)
		ybVectorTupleInsert(vectorstate, (OffsetNumber) (i + 1),
							index, values[i], isnull[i],
							ybctid, NULL /* backfilltime */);
	else
		ybvectorTupleDelete(vectorstate, (OffsetNumber) (i + 1),
							index, values[i], isnull[i],
							ybctid);

	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(writeCtx);
}

IndexBuildResult *
ybvectorbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	return ybvectorBuildCommon(heap, index, indexInfo,
							NULL /* bfinfo */, NULL /* bfresult */);
}

void
ybvectorbuildempty(Relation index)
{
	elog(WARNING, "Unexpected building of empty unlogged index");
}

IndexBulkDeleteResult *
ybvectorbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				IndexBulkDeleteCallback callback, void *callback_state)
{
	elog(WARNING, "Unexpected bulk delete of index via vacuum");
	return NULL;
}

bool
ybvectorinsert(Relation index, Datum *values, bool *isnull, Datum ybctid,
			Relation heap, IndexUniqueCheck checkUnique,
			struct IndexInfo *indexInfo, bool shared_insert)
{
	ybvectorWrite(index, values, isnull, ybctid, heap, indexInfo,
				  true /* isinsert */);

	/* index cannot be unique */
	return false;
}

void
ybvectordelete(Relation index, Datum *values, bool *isnull, Datum ybctid,
			Relation heap, struct IndexInfo *indexInfo)
{
	ybvectorWrite(index, values, isnull, ybctid, heap, indexInfo,
				  false /* isinsert */);
}

IndexBuildResult *
ybvectorbackfill(Relation heap, Relation index, struct IndexInfo *indexInfo,
			  struct YbBackfillInfo *bfinfo, struct YbPgExecOutParam *bfresult)
{
	return ybvectorBuildCommon(heap, index, indexInfo, bfinfo, bfresult);
}
