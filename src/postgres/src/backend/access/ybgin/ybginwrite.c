/*-------------------------------------------------------------------------
 *
 * ybginwrite.c
 *	  insert and delete routines for the Yugabyte inverted index access method.
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
 *			src/backend/access/ybgin/ybginwrite.c
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/genam.h"
#include "access/sysattr.h"
#include "access/ybgin_private.h"
#include "c.h"
#include "catalog/index.h"
#include "catalog/pg_type_d.h"
#include "catalog/yb_type.h"
#include "executor/ybcModifyTable.h"
#include "nodes/execnodes.h"
#include "nodes/parsenodes.h"
#include "pg_yb_utils.h"
#include "storage/off.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"

/*
 * Parts copied from GinBuildState.  Differences:
 * - no buildStats because those are tied to postgres storage
 * - no tmpCtx because that's tied to bulk inserts, which we won't do because
 *   it seems to be particularly beneficial for postgres btrees and not for
 *   Yugabyte DocDB
 * - no accum for the same reason
 * - add backfilltime to both indicate that the build is for online index
 *   backfill and specify the write time for it
 */
typedef struct
{
	GinState	ginstate;
	double		indtuples;
	MemoryContext funcCtx;
	uint64_t   *backfilltime;
} YbginBuildState;

/*
 * Utility method to create constant.
 */
static void
newConstant(YBCPgStatement stmt,
			Oid type_id,
			Oid collation_id,
			Datum datum,
			bool is_null,
			YBCPgExpr *expr)
{
	const YBCPgTypeEntity *type_entity;

	if (is_null)
		type_entity = &YBCGinNullTypeEntity;
	else
		type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);

	YBCPgCollationInfo collation_info;
	YBGetCollationInfo(collation_id, type_entity, datum, is_null,
					   &collation_info);

	HandleYBStatus(YBCPgNewConstant(stmt, type_entity,
									collation_info.collate_is_valid_non_c,
									collation_info.sortkey,
									datum, is_null, expr));
}

/*
 * Utility method to bind const to column.
 */
static void
bindColumn(YBCPgStatement stmt,
		   int attr_num,
		   Oid type_id,
		   Oid collation_id,
		   Datum datum,
		   bool is_null)
{
	YBCPgExpr	expr;

	newConstant(stmt, type_id, collation_id, datum, is_null, &expr);
	HandleYBStatus(YBCPgDmlBindColumn(stmt, attr_num, expr));
}

/*
 * Utility method to set binds for index write statement.
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
	GinState *ginstate = (GinState *) indexstate;
	TupleDesc tupdesc = RelationGetDescr(index);

	if (ybbasectid == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("missing base table ybctid in index write request")));

	for (AttrNumber attnum = 1; attnum <= n_bound_atts; ++attnum)
	{
		Oid			type_id = GetTypeId(attnum, tupdesc);
		Oid			collation_id = YBEncodingCollation(stmt, attnum,
													   ginstate->supportCollation[attnum - 1]);
		Datum		value   = values[attnum - 1];
		bool		is_null = isnull[attnum - 1];

		bindColumn(stmt, attnum, type_id, collation_id, value, is_null);
	}

	/* Gin indexes cannot be unique. */
	Assert(!index->rd_index->indisunique);

	/* Write base ctid column because it is a key column. */
	bindColumn(stmt,
			   YBIdxBaseTupleIdAttributeNumber,
			   BYTEAOID,
			   InvalidOid,
			   ybbasectid,
			   false /* is_null */);
}

/*
 * Extract entries and write values.
 *
 * The first part here is identical to first part of ginHeapTupleInsert.
 */
static int32
ybginTupleWrite(GinState *ginstate, OffsetNumber attnum,
				Relation index, Datum value, bool isNull,
				ItemPointer tid, uint64_t *backfilltime,
				bool isinsert)
{
	Datum	   *entries;
	GinNullCategory *categories;
	int32		i,
				nentries;

	entries = ginExtractEntries(ginstate, attnum, value, isNull,
								&nentries, &categories);

	/* Make sure that this is a single-column index. */
	Assert(RelationGetNumberOfAttributes(index) == 1);

	for (i = 0; i < nentries; i++)
	{
		bool		isnull = categories[i] != 0;

		/*
		 * Pass the null category down using the spot where the data usually
		 * goes.
		 */
		if (categories[i] != GIN_CAT_NORM_KEY)
			entries[i] = categories[i];

		/* Assume single-column index for parameters values and isnull. */
		if (isinsert)
			YBCExecuteInsertIndex(index, &entries[i], &isnull, tid,
								  backfilltime /* backfill_write_time */,
								  doBindsForIdxWrite, (void *) ginstate);
		else
		{
			Assert(!backfilltime);
			YBCExecuteDeleteIndex(index, &entries[i], &isnull, YbItemPointerYbctid(tid),
								  doBindsForIdxWrite, (void *) ginstate);
		}
	}

	return nentries;
}

/*
 * Insert index entries for a single indexable item during "normal"
 * (non-fast-update) insertion
 */
static int32
ybginTupleInsert(GinState *ginstate, OffsetNumber attnum,
				 Relation index, Datum value, bool isNull,
				 ItemPointer tid, uint64_t *backfilltime)
{
	return ybginTupleWrite(ginstate, attnum, index, value, isNull, tid,
						   backfilltime, true /* isinsert */);
}

/*
 * Delete index entries for a single indexable item during "normal"
 * (non-fast-update) insertion
 */
static int32
ybginTupleDelete(GinState *ginstate, OffsetNumber attnum,
				 Relation index, Datum value, bool isNull,
				 ItemPointer tid)
{
	return ybginTupleWrite(ginstate, attnum, index, value, isNull, tid,
						   NULL /* backfilltime */, false /* isinsert */);
}

/*
 * Callback to insert index tuples after a base table tuple is retrieved.  See
 * similar ybcinbuildCallback.
 */
static void
ybginBuildCallback(Relation index, ItemPointer tid, Datum *values,
				   bool *isnull, bool tupleIsAlive, void *state)
{
	YbginBuildState *buildstate = (YbginBuildState *) state;
	GinState   *ginstate = &buildstate->ginstate;
	MemoryContext oldCtx;
	int			i;
	int32		nentries = 0;

	oldCtx = MemoryContextSwitchTo(buildstate->funcCtx);
	for (i = 0; i < ginstate->origTupdesc->natts; i++)
		nentries += ybginTupleInsert(ginstate, (OffsetNumber) (i + 1),
									 index, values[i], isnull[i],
									 tid,
									 buildstate->backfilltime);

	buildstate->indtuples += nentries;

	MemoryContextSwitchTo(oldCtx);
	MemoryContextReset(buildstate->funcCtx);
}

/*
 * Build code for both ybginbuild and ybginbackfill.
 *
 * Parts copied from ginbuild.  Differences are
 * - don't deal with postgres storage (e.g. buffers, pages, tmpCtx)
 * - additionally pass through backfill parameters
 * - name memory context Ybgin
 */
static IndexBuildResult *
ybginBuildCommon(Relation heap, Relation index, struct IndexInfo *indexInfo,
				 struct YbBackfillInfo *bfinfo,
				 struct YbPgExecOutParam *bfresult)
{
	IndexBuildResult *result;
	double		reltuples;
	YbginBuildState buildstate;

	initGinState(&buildstate.ginstate, index);
	buildstate.indtuples = 0;
	if (bfinfo)
		buildstate.backfilltime = &bfinfo->read_time;
	else
		buildstate.backfilltime = NULL;

	/*
	 * create a temporary memory context that is used for calling
	 * ginExtractEntries(), and can be reset after each tuple
	 */
	buildstate.funcCtx = AllocSetContextCreate(GetCurrentMemoryContext(),
											   "Ybgin build temporary context for user-defined function",
											   ALLOCSET_DEFAULT_SIZES);

	/*
	 * Do the heap scan.
	 */
	if (!bfinfo)
		reltuples = table_index_build_scan(heap, index, indexInfo, true, true,
										   ybginBuildCallback, (void *) &buildstate,
										   NULL /* HeapScanDesc */);
	else
		reltuples = IndexBackfillHeapRangeScan(heap, index, indexInfo,
											   ybginBuildCallback,
											   (void *) &buildstate,
											   bfinfo,
											   bfresult);

	MemoryContextDelete(buildstate.funcCtx);

	/*
	 * Return statistics
	 */
	result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));

	result->heap_tuples = reltuples;
	result->index_tuples = buildstate.indtuples;

	return result;
}

IndexBuildResult *
ybginbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	return ybginBuildCommon(heap, index, indexInfo,
							NULL /* bfinfo */, NULL /* bfresult */);
}

void
ybginbuildempty(Relation index)
{
	YBC_LOG_WARNING("Unexpected building of empty unlogged index");
}

IndexBulkDeleteResult *
ybginbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of index via vacuum");
	return NULL;
}

IndexBulkDeleteResult *
ybginvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	YBC_LOG_WARNING("Unexpected index cleanup via vacuum");
	return NULL;
}

/*
 * Write code for both ybgininsert and ybgindelete.
 *
 * Parts copied from gininsert.  Differences are
 * - don't copy fastupdate code since it's not supported
 * - additionally handle deletes
 * - name memory context Ybgin
 */
static void
ybginWrite(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid,
		   Relation heap, struct IndexInfo *indexInfo, bool isinsert)
{
	GinState   *ginstate = (GinState *) indexInfo->ii_AmCache;
	MemoryContext oldCtx;
	MemoryContext writeCtx;
	int			i;

	/* Initialize GinState cache if first call in this statement */
	if (ginstate == NULL)
	{
		oldCtx = MemoryContextSwitchTo(indexInfo->ii_Context);
		ginstate = (GinState *) palloc(sizeof(GinState));
		initGinState(ginstate, index);
		indexInfo->ii_AmCache = (void *) ginstate;
		MemoryContextSwitchTo(oldCtx);
	}

	writeCtx = AllocSetContextCreate(GetCurrentMemoryContext(),
									 "Ybgin write temporary context",
									 ALLOCSET_DEFAULT_SIZES);

	oldCtx = MemoryContextSwitchTo(writeCtx);

	if (GinGetUseFastUpdate(index))
		ereport(DEBUG2,
				(errmsg("fast update is not yet supported for ybgin")));
	for (i = 0; i < ginstate->origTupdesc->natts; i++)
	{
		if (isinsert)
			ybginTupleInsert(ginstate, (OffsetNumber) (i + 1),
							 index, values[i], isnull[i],
							 heap_tid, NULL /* backfilltime */);
		else
			ybginTupleDelete(ginstate, (OffsetNumber) (i + 1),
							 index, values[i], isnull[i],
							 heap_tid);
	}

	MemoryContextSwitchTo(oldCtx);
	MemoryContextDelete(writeCtx);
}

bool
ybgininsert(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid,
			Relation heap, IndexUniqueCheck checkUnique,
			struct IndexInfo *indexInfo, bool shared_insert)
{
	ybginWrite(index, values, isnull, heap_tid, heap, indexInfo, true /* isinsert */);

	/* index cannot be unique */
	return false;
}

void
ybgindelete(Relation index, Datum *values, bool *isnull, ItemPointer heap_tid,
			Relation heap, struct IndexInfo *indexInfo)
{
	ybginWrite(index, values, isnull, heap_tid, heap, indexInfo, false /* isinsert */);
}

IndexBuildResult *
ybginbackfill(Relation heap, Relation index, struct IndexInfo *indexInfo,
			  struct YbBackfillInfo *bfinfo, struct YbPgExecOutParam *bfresult)
{
	return ybginBuildCommon(heap, index, indexInfo, bfinfo, bfresult);
}
