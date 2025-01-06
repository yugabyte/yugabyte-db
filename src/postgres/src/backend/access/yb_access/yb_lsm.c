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
#include "commands/ybccmds.h"
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
				 errmsg("missing base table ybctid in index write request")));
	}

	bool has_null_attr = false;
	for (AttrNumber attnum = 1; attnum <= n_bound_atts; ++attnum)
	{
		Oid			type_id = GetTypeId(attnum, tupdesc);
		Oid			collation_id = YBEncodingCollation(stmt, attnum,
													   ybc_get_attcollation(tupdesc, attnum));
		Datum		value   = values[attnum - 1];
		bool		is_null = isnull[attnum - 1];

		YbBindDatumToColumn(stmt,
							attnum,
							type_id,
							collation_id,
							value,
							is_null,
							NULL /* null_type_entity */);


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
	 * - to ybbasectid if the index uses nulls-are-distinct mode and at least
	 * one index key column is null.
	 * - to NULL otherwise (setting is_null to true is enough).
	 */
	if (unique_index)
		YbBindDatumToColumn(stmt,
							YBUniqueIdxKeySuffixAttributeNumber,
							BYTEAOID,
							InvalidOid,
							ybbasectid,
							index->rd_index->indnullsnotdistinct ||!has_null_attr /* is_null */,
							NULL /* null_type_entity */);

	/*
	 * We may need to set the base ctid column:
	 * - for unique indexes only if we need it as a value (i.e. for inserts)
	 * - for non-unique indexes always (it is a key column).
	 */
	if (ybctid_as_value || !unique_index)
		YbBindDatumToColumn(stmt,
							YBIdxBaseTupleIdAttributeNumber,
							BYTEAOID,
							InvalidOid,
							ybbasectid,
							false /* is_null */,
							NULL /* null_type_entity */);

}

static void
doAssignForIdxUpdate(YBCPgStatement stmt,
					 Relation index,
					 Datum *values,
					 bool *isnull,
					 int n_atts,
					 Datum old_ybbasectid,
					 Datum new_ybbasectid)
{
	TupleDesc tupdesc		= RelationGetDescr(index);
	int		  indnkeyatts	= IndexRelationGetNumberOfKeyAttributes(index);

	if (old_ybbasectid == 0)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("missing base table ybctid in index write request")));
	}

	bool has_null_attr = false;
	for (AttrNumber attnum = 1; attnum <= n_atts; ++attnum)
	{
		Oid			type_id = GetTypeId(attnum, tupdesc);
		Oid			collation_id = YBEncodingCollation(stmt, attnum,
													   ybc_get_attcollation(tupdesc, attnum));
		Datum		value   = values[attnum - 1];
		bool		is_null = isnull[attnum - 1];

		YBCPgExpr ybc_expr = YBCNewConstant(stmt, type_id, collation_id, value, is_null);

		/*
		 * Attrs that are a part of the index key are 'bound' to their values.
		 * It is guaranteed by YbExecUpdateIndexTuples that the values of these
		 * attrs are unmodified. The non-key attrs are 'assigned' to their
		 * new (updated) values. These represent the values that are undergoing
		 * the update.
		 */
		if (attnum <= indnkeyatts)
		{
			HandleYBStatus(YBCPgDmlBindColumn(stmt, attnum, ybc_expr));

			/*
			 * In a unique index, if any of the key columns are NULL, we need
			 * to handle NULL != NULL semantics.
			 */
			has_null_attr = has_null_attr || is_null;
		}
		else
			HandleYBStatus(YBCPgDmlAssignColumn(stmt, attnum, ybc_expr));
	}

	bool unique_index = index->rd_index->indisunique;

	/*
	 * For a non-unique index, the base table CTID attribute is a part of the
	 * index key. Therefore, updates to the primary key require the index
	 * tuple to be deleted and re-inserted, and will not utilize this function.
	 * For a unique index, the base table CTID attribute is not a part of the
	 * index key and can be updated in place. Handle that here.
	 */
	if (new_ybbasectid != (Datum) NULL)
	{
		Assert(unique_index);

		YBCPgExpr ybc_expr = YBCNewConstant(stmt, BYTEAOID, InvalidOid, new_ybbasectid, false);
		HandleYBStatus(YBCPgDmlAssignColumn(stmt, YBIdxBaseTupleIdAttributeNumber, ybc_expr));
	}

	/*
	 * Bind to key columns that do not have an attnum in postgres:
	 * - For non-unique indexes, this is the base table CTID.
	 * - For unique indexes, this is the unique key suffix.
	 */
	if (!unique_index)
		YbBindDatumToColumn(stmt,
							YBIdxBaseTupleIdAttributeNumber,
							BYTEAOID,
							InvalidOid,
							old_ybbasectid,
							false,
							NULL /* null_type_entity */);

	else
		YbBindDatumToColumn(stmt,
							YBUniqueIdxKeySuffixAttributeNumber,
							BYTEAOID,
							InvalidOid,
							old_ybbasectid,
							index->rd_index->indnullsnotdistinct || !has_null_attr /* is_null */,
							NULL /* null_type_entity */);
}

static void
ybcinbuildCallback(Relation index, Datum ybctid, Datum *values,
				   bool *isnull, bool tupleIsAlive, void *state)
{
	YBCBuildState  *buildstate = (YBCBuildState *)state;

	if (!buildstate->isprimary)
		YBCExecuteInsertIndex(index,
							  values,
							  isnull,
							  ybctid,
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
	 * YB: Primary key index is an implicit part of the base table in Yugabyte.
	 * We don't need to scan the base table to build a primary key index. (#8024)
	 * We also don't need to build YB indexes during a major version upgrade,
	 * as we simply link the old DocDB table on master.
	 */
	if (!index->rd_index->indisprimary &&
		!(IsYugaByteEnabled() && IsBinaryUpgrade))
	{
		heap_tuples = yb_table_index_build_scan(heap, index, indexInfo, true,
												ybcinbuildCallback, &buildstate,
												NULL);
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
				Oid dboid = ((Form_pg_database) GETSTRUCT(pg_db_tuple))->oid;
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

static void
ybcinupdate(Relation index, Datum *values, bool *isnull, Datum oldYbctid,
			Datum newYbctid, Relation heap, struct IndexInfo *indexInfo)
{
	Assert(!index->rd_index->indisprimary);
	YBCExecuteUpdateIndex(index, values, isnull, oldYbctid, newYbctid,
						  doAssignForIdxUpdate);
}

static IndexBulkDeleteResult *
ybcinbulkdelete(IndexVacuumInfo *info, IndexBulkDeleteResult *stats,
				IndexBulkDeleteCallback callback, void *callback_state)
{
	YBC_LOG_WARNING("Unexpected bulk delete of index via vacuum");
	return NULL;
}

/*
 * Post-VACUUM cleanup.
 *
 * Result: a palloc'd struct containing statistical info for VACUUM displays.
 *
 * YB: this function is based on btvacuumcleanup implementation.
 * We only support ANALYZE and don't support VACUUM as of 04/05/2024.
 */
static IndexBulkDeleteResult *
ybcinvacuumcleanup(IndexVacuumInfo *info, IndexBulkDeleteResult *stats)
{
	/* No-op in ANALYZE ONLY mode */
	if (info->analyze_only)
		return stats;

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

static int64
ybcgetbitmap(IndexScanDesc scan, YbTIDBitmap *ybtbm)
{
	size_t		new_tuples = 0;
	SliceVector ybctids;
	YbScanDesc	ybscan = (YbScanDesc) scan->opaque;
	bool 		exceeded_work_mem = false;

	ybscan->exec_params = scan->yb_exec_params;
	/* exec_params can be NULL in case of systable_getnext, for example. */
	if (ybscan->exec_params)
		ybscan->exec_params->work_mem = work_mem;

	if (!ybscan->is_exec_done)
		pgstat_count_index_scan(scan->indexRelation);

	if (ybscan->quit_scan || ybtbm->work_mem_exceeded)
		return 0;

	HandleYBStatus(YBCPgRetrieveYbctids(ybscan->handle, ybscan->exec_params,
										ybscan->target_desc->natts, &ybctids, &new_tuples,
										&exceeded_work_mem));
	if (!exceeded_work_mem)
		yb_tbm_add_tuples(ybtbm, ybctids);
	else
		yb_tbm_set_work_mem_exceeded(ybtbm);

	YBCBitmapShallowDeleteVector(ybctids);

	return ybtbm->work_mem_exceeded ? 0 : new_tuples;
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
		YbScanDesc ybScan = (YbScanDesc) scan->opaque;
		/* For rescan, end the previous scan. */
		if (ybScan->pscan)
			yb_init_partition_key_data(ybScan->pscan);
		ybcinendscan(scan);
		scan->opaque = NULL;
	}

	YbScanDesc ybScan = ybcBeginScan(scan->heapRelation, scan->indexRelation,
									 scan->xs_want_itup, nscankeys, scankey,
									 scan->yb_scan_plan, scan->yb_rel_pushdown,
									 scan->yb_idx_pushdown, scan->yb_aggrefs,
									 scan->yb_distinct_prefixlen,
									 scan->yb_exec_params,
									 false /* is_internal_scan */,
									 scan->fetch_ybctids_only);
	scan->opaque = ybScan;
	if (scan->parallel_scan)
	{
		ParallelIndexScanDesc target = scan->parallel_scan;
		ScanDirection direction = ForwardScanDirection;
		ybScan->pscan = (YBParallelPartitionKeys)
			OffsetToPointer(target, target->ps_offset);
		Relation rel = scan->indexRelation;
		/* If scan is by the PK, use the main relation instead */
		if (scan->heapRelation &&
			scan->heapRelation->rd_pkindex == RelationGetRelid(rel))
		{
			elog(LOG, "Scan is by PK, get parallel ranges from the main table");
			rel = scan->heapRelation;
		}
		if (scan->yb_scan_plan)
		{
			if IsA(scan->yb_scan_plan, IndexScan)
				direction = ((IndexScan *) scan->yb_scan_plan)->indexorderdir;
			else if IsA(scan->yb_scan_plan, IndexOnlyScan)
				direction = ((IndexOnlyScan *) scan->yb_scan_plan)->indexorderdir;
		}
		ybParallelPrepare(ybScan->pscan, rel, scan->yb_exec_params,
						  !ScanDirectionIsBackward(direction));
	}
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
			return false;

		scan->xs_recheck = YbNeedsPgRecheck(ybscan);
		/*
		 * In the case of parallel scan we need to obtain boundaries from the
		 * pscan before the scan is executed. Also empty row from parallel range
		 * scan does not mean scan is done, it means the range is done and we
		 * need to pick up next. No rows from parallel range is possible, hence
		 * the loop.
		 */
		while (true)
		{
			/* Need to execute the request */
			if (!ybscan->is_exec_done)
			{
				/* Parallel mode: pick up parallel block first */
				if (ybscan->pscan != NULL)
				{
					YBParallelPartitionKeys parallel_scan = ybscan->pscan;
					const char *low_bound;
					size_t low_bound_size;
					const char *high_bound;
					size_t high_bound_size;
					/*
					 * If range is found, apply the boundaries, false means the
					 * scan * is done for that worker.
					 */
					if (ybParallelNextRange(parallel_scan,
											&low_bound, &low_bound_size,
											&high_bound, &high_bound_size))
					{
						HandleYBStatus(YBCPgDmlBindRange(ybscan->handle,
														 low_bound,
														 low_bound_size,
														 high_bound,
														 high_bound_size));
						if (low_bound)
							pfree((void *) low_bound);
						if (high_bound)
							pfree((void *) high_bound);
					}
					else
						return false;
					/*
					 * Use unlimited fetch.
					 * Parallel scan range is already of limited size, it is
					 * unlikely to exceed the
					 * message size, but may save some RPCs.
					 */
					ybscan->exec_params->limit_use_default = true;
					ybscan->exec_params->yb_fetch_row_limit = 0;
					ybscan->exec_params->yb_fetch_size_limit = 0;
				}
				/* Request with aggregates does not care of scan direction */
				HandleYBStatus(YBCPgExecSelect(ybscan->handle,
											   ybscan->exec_params));
				ybscan->is_exec_done = true;
			}

			/*
			 * Aggregate pushdown directly modifies the scan slot rather than
			 * passing it through xs_hitup or xs_itup.
			 */
			if (ybc_getnext_aggslot(scan, ybscan->handle,
									ybscan->prepare_params.index_only_scan))
				return true;
			/*
			 * Parallel scan needs to pick next range and reexecute, done
			 * otherwise.
			 */
			if (ybscan->pscan)
				ybscan->is_exec_done = false;
			else
				return false;
		}
	}

	/*
	 * IndexScan(SysTable, Index) --> HeapTuple.
	 */
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

static void
ybcinbindschema(YBCPgStatement handle,
				struct IndexInfo *indexInfo,
				TupleDesc indexTupleDesc,
				int16 *coloptions)
{
	YBCBindCreateIndexColumns(handle,
							  indexInfo,
							  indexTupleDesc,
							  coloptions,
							  indexInfo->ii_NumIndexKeyAttrs);
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
	amroutine->amcanparallel = true;
	amroutine->amcaninclude = true;
	amroutine->ybamcanupdatetupleinplace = true;
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
	amroutine->amgetbitmap = NULL; /* use yb_amgetbitmap below instead */
	amroutine->amendscan = ybcinendscan;
	amroutine->ammarkpos = NULL; /* TODO: support mark/restore pos with ordering */
	amroutine->amrestrpos = NULL;
	amroutine->amestimateparallelscan = yb_estimate_parallel_size;
	amroutine->aminitparallelscan = yb_init_partition_key_data;
	amroutine->amparallelrescan = NULL;
	amroutine->yb_amisforybrelation = true;
	amroutine->yb_aminsert = ybcininsert;
	amroutine->yb_amdelete = ybcindelete;
	amroutine->yb_amupdate = ybcinupdate;
	amroutine->yb_ambackfill = ybcinbackfill;
	amroutine->yb_ammightrecheck = ybcinmightrecheck;
	amroutine->yb_amgetbitmap = ybcgetbitmap;
	amroutine->yb_ambindschema = ybcinbindschema;

	PG_RETURN_POINTER(amroutine);
}
