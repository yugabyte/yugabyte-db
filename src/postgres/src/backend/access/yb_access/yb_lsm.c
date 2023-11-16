/*--------------------------------------------------------------------------------------------------
 *
 * yb_lsm.c
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
 * src/backend/access/yb_access/yb_lsm.c
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/nbtree.h"
#include "access/reloptions.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/yb_scan.h"
#include "catalog/index.h"
#include "catalog/pg_type.h"
#include "executor/ybcModifyTable.h"
#include "miscadmin.h"
#include "pgstat.h"
#include "utils/rel.h"

static void ybcinendscan(IndexScanDesc scan);

/* --------------------------------------------------------------------------------------------- */

/* Working state for ybcinbuild and its callback */
typedef struct
{
	bool	isprimary;		/* are we building a primary index? */
	double	index_tuples;	/* # of tuples inserted into index */
	/*
	 * Write time for rows written to index as part of online index backfill.
	 * This field being non-null signifies that we are doing online index
	 * backfill.
	 */
	const uint64_t *backfill_write_time;
} YBCBuildState;

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
	YBCPgExpr expr = YBCNewConstant(stmt, type_id, collation_id, datum,
									is_null);
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
	TupleDesc tupdesc		= RelationGetDescr(index);
	int		  indnkeyatts	= IndexRelationGetNumberOfKeyAttributes(index);

	if (ybbasectid == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("Missing base table ybctid in index write request")));
	}

	bool has_null_attr = false;
	for (AttrNumber attnum = 1; attnum <= n_bound_atts; ++attnum)
	{
		Oid			type_id = GetTypeId(attnum, tupdesc);
		Oid			collation_id = YBEncodingCollation(stmt, attnum,
													   ybc_get_attcollation(tupdesc, attnum));
		Datum		value   = values[attnum - 1];
		bool		is_null = isnull[attnum - 1];

		bindColumn(stmt, attnum, type_id, collation_id, value, is_null);

		/*
		 * If any of the indexed columns is null, we need to take case of
		 * SQL null != null semantics.
		 * For details, see comment on kYBUniqueIdxKeySuffix.
		 */
		has_null_attr = has_null_attr || (is_null && attnum <= indnkeyatts);
	}

	const bool unique_index = index->rd_index->indisunique;

	/*
	 * For unique indexes we need to set the key suffix system column:
	 * - to ybbasectid if at least one index key column is null.
	 * - to NULL otherwise (setting is_null to true is enough).
	 */
	if (unique_index)
		bindColumn(stmt,
				   YBUniqueIdxKeySuffixAttributeNumber,
				   BYTEAOID,
				   InvalidOid,
				   ybbasectid,
				   !has_null_attr /* is_null */);

	/*
	 * We may need to set the base ctid column:
	 * - for unique indexes only if we need it as a value (i.e. for inserts)
	 * - for non-unique indexes always (it is a key column).
	 */
	if (ybctid_as_value || !unique_index)
		bindColumn(stmt,
				   YBIdxBaseTupleIdAttributeNumber,
				   BYTEAOID,
				   InvalidOid,
				   ybbasectid,
				   false /* is_null */);
}

static void
ybcinbuildCallback(Relation index, HeapTuple heapTuple, Datum *values, bool *isnull,
				   bool tupleIsAlive, void *state)
{
	YBCBuildState  *buildstate = (YBCBuildState *)state;

	if (!buildstate->isprimary)
		YBCExecuteInsertIndex(index,
							  values,
							  isnull,
							  heapTuple->t_ybctid,
							  buildstate->backfill_write_time,
							  doBindsForIdxWrite,
							  NULL /* indexstate */);

	buildstate->index_tuples += 1;
}

static IndexBuildResult *
ybcinbuild(Relation heap, Relation index, struct IndexInfo *indexInfo)
{
	YBCBuildState	buildstate;
	double			heap_tuples = 0;

	/* Do the heap scan */
	buildstate.isprimary = index->rd_index->indisprimary;
	buildstate.index_tuples = 0;
	buildstate.backfill_write_time = NULL;
	/*
	 * Primary key index is an implicit part of the base table in Yugabyte.
	 * We don't need to scan the base table to build a primary key index. (#8024)
	 */
	if (!index->rd_index->indisprimary)
	{
		heap_tuples = IndexBuildHeapScan(heap, index, indexInfo, true,
										 ybcinbuildCallback, &buildstate, NULL);
	}
	/*
	 * Return statistics
	 */
	IndexBuildResult *result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples  = heap_tuples;
	result->index_tuples = buildstate.index_tuples;
	return result;
}

static IndexBuildResult *
ybcinbackfill(Relation heap,
			  Relation index,
			  struct IndexInfo *indexInfo,
			  YbBackfillInfo *bfinfo,
			  YbPgExecOutParam *bfresult)
{
	YBCBuildState	buildstate;
	double			heap_tuples = 0;

	/* Do the heap scan */
	buildstate.isprimary = index->rd_index->indisprimary;
	buildstate.index_tuples = 0;
	/* Backfilled rows should be as if they happened at the time of backfill */
	buildstate.backfill_write_time = &bfinfo->read_time;
	heap_tuples = IndexBackfillHeapRangeScan(heap,
											 index,
											 indexInfo,
											 ybcinbuildCallback,
											 &buildstate,
											 bfinfo,
											 bfresult);

	/*
	 * Return statistics
	 */
	IndexBuildResult *result = (IndexBuildResult *) palloc(sizeof(IndexBuildResult));
	result->heap_tuples  = heap_tuples;
	result->index_tuples = buildstate.index_tuples;
	return result;
}

static void
ybcinbuildempty(Relation index)
{
	YBC_LOG_WARNING("Unexpected building of empty unlogged index");
}

static bool
ybcininsert(Relation index, Datum *values, bool *isnull, Datum ybctid, Relation heap,
			IndexUniqueCheck checkUnique, struct IndexInfo *indexInfo, bool sharedInsert)
{
	if (!index->rd_index->indisprimary)
	{
		if (sharedInsert)
		{
			if (!IsYsqlUpgrade)
				elog(ERROR, "shared insert cannot be done outside of YSQL upgrade");

			YB_FOR_EACH_DB(pg_db_tuple)
			{
				Oid dboid = HeapTupleGetOid(pg_db_tuple);
				/*
				 * Since this is a catalog index, we assume it exists in all databases.
				 * YB doesn't use PG locks so it's okay not to take them.
				 */
				YBCExecuteInsertIndexForDb(dboid,
										   index,
										   values,
										   isnull,
										   ybctid,
										   NULL /* backfill_write_time */,
										   doBindsForIdxWrite,
										   NULL /* indexstate */);
			}
			YB_FOR_EACH_DB_END;
		}
		else
			YBCExecuteInsertIndex(index,
								  values,
								  isnull,
								  ybctid,
								  NULL /* backfill_write_time */,
								  doBindsForIdxWrite,
								  NULL /* indexstate */);
	}

	return index->rd_index->indisunique ? true : false;
}

static void
ybcindelete(Relation index, Datum *values, bool *isnull, Datum ybctid, Relation heap,
			struct IndexInfo *indexInfo)
{
	if (!index->rd_index->indisprimary)
		YBCExecuteDeleteIndex(index, values, isnull, ybctid,
							  doBindsForIdxWrite, NULL /* indexstate */);
}

static IndexBulkDeleteResult *
ybcinbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of index via vacuum");
	return NULL;
}

static IndexBulkDeleteResult *
ybcinvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	YBC_LOG_WARNING("Unexpected index cleanup via vacuum");
	return NULL;
}

/* --------------------------------------------------------------------------------------------- */

static bool
ybcincanreturn(Relation index, int attno)
{
	/*
	 * If "canreturn" is true, Postgres will attempt to perform index-only scan on the indexed
	 * columns and expect us to return the column values as an IndexTuple. This will be the case
	 * for secondary index.
	 *
	 * For indexes which are primary keys, we will return the table row as a HeapTuple instead.
	 * For this reason, we set "canreturn" to false for primary keys.
	 */
	return !index->rd_index->indisprimary;
}

static bool
ybcinmightrecheck(Relation heap, Relation index, bool xs_want_itup,
				  ScanKey keys, int nkeys)
{
	return YbPredetermineNeedsRecheck(heap, index, xs_want_itup, keys, nkeys);
}

static void
ybcincostestimate(struct PlannerInfo *root, struct IndexPath *path, double loop_count,
				  Cost *indexStartupCost, Cost *indexTotalCost, Selectivity *indexSelectivity,
				  double *indexCorrelation, double *indexPages)
{
	/*
	 * Information is lacking for hypothetical index in order for estimation
	 * in YB to work.
	 * So we skip hypothetical index.
	 */
	if (path->indexinfo->hypothetical)
		return;
	ybcIndexCostEstimate(root,
						 path,
						 indexSelectivity,
						 indexStartupCost,
						 indexTotalCost);
}

static bytea *
ybcinoptions(Datum reloptions, bool validate)
{
	return default_reloptions(reloptions, validate, RELOPT_KIND_YB_LSM);
}

static bool
ybcinproperty(Oid index_oid, int attno, IndexAMProperty prop, const char *propname,
			  bool *res, bool *isnull)
{
	return false;
}

static bool
ybcinvalidate(Oid opclassoid)
{
	return true;
}

/* --------------------------------------------------------------------------------------------- */

static IndexScanDesc
ybcinbeginscan(Relation rel, int nkeys, int norderbys)
{
	IndexScanDesc scan;

	/* no order by operators allowed */
	Assert(norderbys == 0);

	/* get the scan */
	scan = RelationGetIndexScan(rel, nkeys, norderbys);
	scan->opaque = NULL;

	return scan;
}

static void
ybcinrescan(IndexScanDesc scan, ScanKey scankey, int nscankeys,	ScanKey orderbys, int norderbys)
{
	if (scan->opaque)
	{
		/* For rescan, end the previous scan. */
		ybcinendscan(scan);
		scan->opaque = NULL;
	}

	YbScanDesc ybScan = ybcBeginScan(scan->heapRelation, scan->indexRelation,
									 scan->xs_want_itup, nscankeys, scankey,
									 scan->yb_scan_plan, scan->yb_rel_pushdown,
									 scan->yb_idx_pushdown, scan->yb_aggrefs,
									 scan->yb_distinct_prefixlen,
									 scan->yb_exec_params);
	scan->opaque = ybScan;
}

/*
 * Processing the following SELECT.
 *   SELECT data FROM heapRelation WHERE rowid IN
 *     ( SELECT rowid FROM indexRelation WHERE key = given_value )
 *
 * TODO(neil) Postgres layer should make just one request for IndexScan.
 *   - Query ROWID from IndexTable using key.
 *   - Query data from Table (relation) using ROWID.
 */
static bool
ybcingettuple(IndexScanDesc scan, ScanDirection dir)
{
	YbScanDesc ybscan = (YbScanDesc) scan->opaque;
	ybscan->exec_params = scan->yb_exec_params;
	/* exec_params can be NULL in case of systable_getnext, for example. */
	if (ybscan->exec_params)
		ybscan->exec_params->work_mem = work_mem;

    if (!ybscan->is_exec_done)
        pgstat_count_index_scan(scan->indexRelation);
    
	/* Special case: aggregate pushdown. */
	if (scan->yb_aggrefs)
	{
		/*
		 * TODO(jason): deduplicate with ybc_getnext_heaptuple,
		 * ybc_getnext_indextuple.
		 */
		if (ybscan->quit_scan)
			return NULL;

		scan->xs_recheck = YbNeedsRecheck(ybscan);
		if (!ybscan->is_exec_done)
		{
			/* Request with aggregates does not care of scan direction */
			HandleYBStatus(YBCPgExecSelect(ybscan->handle,
										   ybscan->exec_params));
			ybscan->is_exec_done = true;
		}

		/*
		 * Aggregate pushdown directly modifies the scan slot rather than
		 * passing it through xs_hitup or xs_itup.
		 */
		return ybc_getnext_aggslot(scan, ybscan->handle,
								   ybscan->prepare_params.index_only_scan);
	}

	/*
	 * IndexScan(SysTable, Index) --> HeapTuple.
	 */
	scan->xs_ctup.t_ybctid = 0;
	bool has_tuple = false;
	if (ybscan->prepare_params.index_only_scan)
	{
		IndexTuple tuple = ybc_getnext_indextuple(ybscan, dir, &scan->xs_recheck);
		if (tuple)
		{
			scan->xs_itup = tuple;
			scan->xs_itupdesc = RelationGetDescr(scan->indexRelation);
			has_tuple = true;
		}
	}
	else
	{
		HeapTuple tuple = ybc_getnext_heaptuple(ybscan, dir, &scan->xs_recheck);
		if (tuple)
		{
			scan->xs_ctup.t_ybctid = tuple->t_ybctid;
			scan->xs_hitup = tuple;
			scan->xs_hitupdesc = RelationGetDescr(scan->heapRelation);
			has_tuple = true;
		}
	}

	return has_tuple;
}

static void
ybcinendscan(IndexScanDesc scan)
{
	ybc_free_ybscan((YbScanDesc)scan->opaque);
}

/*
 * LSM handler function: return IndexAmRoutine with access method parameters
 * and callbacks.
 */
Datum
ybcinhandler(PG_FUNCTION_ARGS)
{
	IndexAmRoutine *amroutine = makeNode(IndexAmRoutine);

	amroutine->amstrategies = BTMaxStrategyNumber;
	amroutine->amsupport = BTNProcs;
	amroutine->amcanorder = true;
	amroutine->amcanorderbyop = false;
	amroutine->amcanbackward = true;
	amroutine->amcanunique = true;
	amroutine->amcanmulticol = true;
	amroutine->amoptionalkey = true;
	amroutine->amsearcharray = true;
	amroutine->amsearchnulls = true;
	amroutine->amstorage = false;
	amroutine->amclusterable = true;
	amroutine->ampredlocks = true;
	amroutine->amcanparallel = false; /* TODO: support parallel scan */
	amroutine->amcaninclude = true;
	amroutine->amkeytype = InvalidOid;

	amroutine->ambuild = ybcinbuild;
	amroutine->ambuildempty = ybcinbuildempty;
	amroutine->aminsert = NULL; /* use yb_aminsert below instead */
	amroutine->ambulkdelete = ybcinbulkdelete;
	amroutine->amvacuumcleanup = ybcinvacuumcleanup;
	amroutine->amcanreturn = ybcincanreturn;
	amroutine->amcostestimate = ybcincostestimate;
	amroutine->amoptions = ybcinoptions;
	amroutine->amproperty = ybcinproperty;
	amroutine->amvalidate = ybcinvalidate;
	amroutine->ambeginscan = ybcinbeginscan;
	amroutine->amrescan = ybcinrescan;
	amroutine->amgettuple = ybcingettuple;
	amroutine->amgetbitmap = NULL; /* TODO: support bitmap scan */
	amroutine->amendscan = ybcinendscan;
	amroutine->ammarkpos = NULL; /* TODO: support mark/restore pos with ordering */
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = NULL; /* TODO: support parallel scan */
	amroutine->aminitparallelscan = NULL;
	amroutine->amparallelrescan = NULL;
	amroutine->yb_aminsert = ybcininsert;
	amroutine->yb_amdelete = ybcindelete;
	amroutine->yb_ambackfill = ybcinbackfill;
	amroutine->yb_ammightrecheck = ybcinmightrecheck;

	PG_RETURN_POINTER(amroutine);
}
