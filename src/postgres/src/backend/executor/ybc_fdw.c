/*--------------------------------------------------------------------------------------------------
 *
 * ybc_fdw.c
 *		  Foreign-data wrapper for YugabyteDB.
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
 * IDENTIFICATION
 *		  src/backend/executor/ybc_fdw.c
 *
 *--------------------------------------------------------------------------------------------------
 */

#include "postgres.h"

#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

/*  TODO see which includes of this block are still needed. */
#include "access/htup_details.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
#include "access/xact.h"
#include "catalog/catalog.h"
#include "catalog/pg_foreign_table.h"
#include "commands/copy.h"
#include "commands/defrem.h"
#include "commands/explain.h"
#include "commands/vacuum.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "optimizer/cost.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/sampling.h"

/*  YB includes. */
#include "commands/dbcommands.h"
#include "catalog/pg_operator.h"
#include "catalog/yb_type.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "access/yb_scan.h"
#include "executor/ybcExpr.h"
#include "executor/ybc_fdw.h"
#include "optimizer/optimizer.h"
#include "utils/resowner_private.h"

/* -------------------------------------------------------------------------- */
/*  Planner/Optimizer functions */

typedef struct YbFdwPlanState
{
	/* Bitmap of attribute (column) numbers that we need to fetch from YB. */
	Bitmapset *target_attrs;

} YbFdwPlanState;

/*
 * ybcGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
static void
ybcGetForeignRelSize(PlannerInfo *root,
					 RelOptInfo *baserel,
					 Oid foreigntableid)
{
	YbFdwPlanState		*ybc_plan = NULL;

	ybc_plan = (YbFdwPlanState *) palloc0(sizeof(YbFdwPlanState));

	/* Set the estimate for the total number of rows (tuples) in this table. */
	if (yb_enable_optimizer_statistics)
	{
		set_baserel_size_estimates(root, baserel);
	}
	else
	{
		if (baserel->tuples < 0)
			baserel->tuples = YBC_DEFAULT_NUM_ROWS;

		/*
		* Initialize the estimate for the number of rows returned by this query.
		* This does not yet take into account the restriction clauses, but it will
		* be updated later by ybcIndexCostEstimate once it inspects the clauses.
		*/
		baserel->rows = baserel->tuples;
	}

	baserel->fdw_private = ybc_plan;


	/*
	 * Test any indexes of rel for applicability also.
	 */
	check_index_predicates(root, baserel);
}

/*
 * ybcGetForeignPaths
 *		Create possible access paths for a scan on the foreign table, which is
 *      the full table scan plus available index paths (including the  primary key
 *      scan path if any).
 */
static void
ybcGetForeignPaths(PlannerInfo *root,
				   RelOptInfo *baserel,
				   Oid foreigntableid)
{
	Cost startup_cost;
	Cost total_cost;

	/* Estimate costs */
	ybcCostEstimate(baserel, YBC_FULL_SCAN_SELECTIVITY,
					false /* is_backwards scan */,
					true /* is_seq_scan */,
					false /* is_uncovered_idx_scan */,
					&startup_cost,
					&total_cost,
					baserel->reltablespace /* index_tablespace_oid */);

	/* Create a ForeignPath node and it as the scan path */
	add_path(baserel,
			 (Path *) create_foreignscan_path(root,
											  baserel,
											  NULL, /* default pathtarget */
											  baserel->rows,
											  startup_cost,
											  total_cost,
											  NIL,  /* no pathkeys */
											  NULL, /* no outer rel either */
											  NULL, /* no extra plan */
											  NULL  /* no options yet */ ));

	/* Add primary key and secondary index paths also */
	create_index_paths(root, baserel);
}

/*
 * ybcGetForeignPlan
 *		Create a ForeignScan plan node for scanning the foreign table
 */
static ForeignScan *
ybcGetForeignPlan(PlannerInfo *root,
				  RelOptInfo *baserel,
				  Oid foreigntableid,
				  ForeignPath *best_path,
				  List *tlist,
				  List *scan_clauses,
				  Plan *outer_plan)
{
	YbFdwPlanState *yb_plan_state = (YbFdwPlanState *) baserel->fdw_private;
	Index			scan_relid = baserel->relid;
	List		   *local_quals = NIL;
	List		   *remote_quals = NIL;
	List		   *remote_colrefs = NIL;
	ListCell	   *lc;

	scan_clauses = extract_actual_clauses(scan_clauses, false);

	/*
	 * Split the expressions in the scan_clauses onto two lists:
	 * - remote_quals gets supported expressions to push down to DocDB, and
	 * - local_quals gets remaining to evaluate upon returned rows.
	 * The remote_colrefs list contains data type details of the columns
	 * referenced by the expressions in the remote_quals list. DocDB needs it
	 * to convert row values to Datum/isnull pairs consumable by Postgres
	 * functions.
	 * The remote_quals and remote_colrefs lists are sent with the protobuf
	 * read request.
	 */
	foreach(lc, scan_clauses)
	{
		List *colrefs = NIL;
		Expr *expr = (Expr *) lfirst(lc);
		if (YbCanPushdownExpr(expr, &colrefs))
		{
			remote_quals = lappend(remote_quals, expr);
			remote_colrefs = list_concat(remote_colrefs, colrefs);
		}
		else
		{
			local_quals = lappend(local_quals, expr);
			list_free_deep(colrefs);
		}
	}

	/* Get the target columns that need to be retrieved from DocDB */
	foreach(lc, baserel->reltarget->exprs)
	{
		Expr *expr = (Expr *) lfirst(lc);
		pull_varattnos_min_attr((Node *) expr,
								baserel->relid,
								&yb_plan_state->target_attrs,
								baserel->min_attr);
	}

	/* Get the target columns that are needed to evaluate local quals */
	foreach(lc, local_quals)
	{
		Expr *expr = (Expr *) lfirst(lc);
		pull_varattnos_min_attr((Node *) expr,
								baserel->relid,
								&yb_plan_state->target_attrs,
								baserel->min_attr);
	}

	/* Set scan targets. */
	List *target_attrs = NULL;
	bool wholerow = false;
	for (AttrNumber attnum = baserel->min_attr; attnum <= baserel->max_attr; attnum++)
	{
		int bms_idx = attnum - baserel->min_attr + 1;
		if (wholerow || bms_is_member(bms_idx, yb_plan_state->target_attrs))
		{
			switch (attnum)
			{
				case InvalidAttrNumber:
					/*
					 * Postgres repurposes InvalidAttrNumber to represent the "wholerow"
					 * junk attribute.
					 */
					wholerow = true;
					break;
				case SelfItemPointerAttributeNumber:
				case MinTransactionIdAttributeNumber:
				case MinCommandIdAttributeNumber:
				case MaxTransactionIdAttributeNumber:
				case MaxCommandIdAttributeNumber:
					ereport(ERROR,
					        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
							        "System column with id %d is not supported yet",
							        attnum)));
					break;
				case TableOidAttributeNumber:
					/* Nothing to do in YugaByte: Postgres will handle this. */
					break;
				case YBTupleIdAttributeNumber:
				default: /* Regular column: attnum > 0.
							NOTE: dropped columns may be included. */
				{
					TargetEntry *target = makeNode(TargetEntry);
					target->resno = attnum;
					target_attrs = lappend(target_attrs, target);
				}
			}
		}
	}

	/* Create the ForeignScan node */
	return make_foreignscan(tlist,           /* local target list */
							local_quals,
							scan_relid,
							target_attrs,    /* referenced attributes */
							remote_colrefs,  /* fdw_private data (attribute types) */
							NIL,             /* remote target list (none for now) */
							remote_quals,
							outer_plan,
							best_path->path.yb_path_info);
}

/* ------------------------------------------------------------------------- */
/*  Scanning functions */
/* YB_TODO(neil) Need to review all of these scan functions. */

/*
 * FDW-specific information for ForeignScanState.fdw_state.
 */
typedef struct YbFdwExecState
{
	/* The handle for the internal YB Select statement. */
	YBCPgStatement	handle;
	YBCPgExecParameters *exec_params; /* execution control parameters for YugaByte */
	bool is_exec_done; /* Each statement should be executed exactly one time */
} YbFdwExecState;

/*
 * ybcBeginForeignScan
 *		Initiate access to the Yugabyte by allocating a Select handle.
 */
static void
ybcBeginForeignScan(ForeignScanState *node, int eflags)
{
	EState      *estate      = node->ss.ps.state;
	Relation    relation     = node->ss.ss_currentRelation;

	YbFdwExecState *ybc_state = NULL;
	ForeignScan *foreignScan = castNode(ForeignScan, node->ss.ps.plan);

	/* Do nothing in EXPLAIN (no ANALYZE) case.  node->fdw_state stays NULL. */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;

	/* Allocate and initialize YB scan state. */
	ybc_state = (YbFdwExecState *) palloc0(sizeof(YbFdwExecState));

	node->fdw_state = (void *) ybc_state;
	HandleYBStatus(YBCPgNewSelect(YBCGetDatabaseOid(relation),
								  YbGetStorageRelid(relation),
								  NULL /* prepare_params */,
								  YBCIsRegionLocal(relation),
								  &ybc_state->handle));
	ybc_state->exec_params = &estate->yb_exec_params;

	ybc_state->exec_params->rowmark = -1;
	if (YBReadFromFollowersEnabled()) {
		ereport(DEBUG2, (errmsg("Doing read from followers")));
	}
	if (foreignScan->scan.yb_lock_mechanism == YB_RANGE_LOCK_ON_SCAN)
	{
		/*
		 * In case of SERIALIZABLE isolation level we have to take predicate locks to disallow
		 * INSERTion of new rows that satisfy the query predicate. So, we set the rowmark on all
		 * read requests sent to tserver instead of locking each tuple one by one in LockRows node.
		 */
		if (estate->es_rowmarks && estate->es_range_table_size > 0) {
			ExecRowMark *erm = estate->es_rowmarks[0];
			// Do not propagate non-row-locking row marks.
			if (erm->markType != ROW_MARK_REFERENCE && erm->markType != ROW_MARK_COPY)
			{
				ybc_state->exec_params->rowmark = erm->markType;
				ybc_state->exec_params->pg_wait_policy = erm->waitPolicy;
				YBSetRowLockPolicy(&ybc_state->exec_params->docdb_wait_policy,
								   erm->waitPolicy);
			}
		}
	}

	ybc_state->is_exec_done = false;

	YbSetCatalogCacheVersion(ybc_state->handle, YbGetCatalogCacheVersion());
}

/*
 * ybSetupScanTargets
 *		Add the target expressions to the DocDB statement.
 *		Currently target are either all column references or all aggregates.
 *		We do not push down target expressions yet.
 */
static void
ybcSetupScanTargets(ForeignScanState *node)
{
	ScanState *ss = &node->ss;
	EState *estate = ss->ps.state;
	ForeignScan *foreignScan = (ForeignScan *) ss->ps.plan;
	Relation relation = ss->ss_currentRelation;
	YBCPgStatement handle = ((YbFdwExecState *) node->fdw_state)->handle;
	TupleDesc tupdesc = RelationGetDescr(relation);
	ListCell *lc;

	/* Planning function above should ensure target list is set */
	List *target_attrs = foreignScan->fdw_exprs;

	MemoryContext oldcontext =
		MemoryContextSwitchTo(ss->ps.ps_ExprContext->ecxt_per_query_memory);

	/* Set scan targets. */
	if (node->yb_fdw_aggrefs != NIL)
	{
		YbDmlAppendTargetsAggregate(node->yb_fdw_aggrefs,
									RelationGetDescr(ss->ss_currentRelation),
									NULL /* index */,
									handle);

		/*
		 * For aggregate pushdown, we read just the aggregates from DocDB
		 * and pass that up to the aggregate node (agg pushdown wouldn't be
		 * enabled if we needed to read more than that).  Set up a dummy
		 * scan slot to hold that as many attributes as there are pushed
		 * aggregates.
		 */
		TupleDesc tupdesc =	CreateTemplateTupleDesc(list_length(node->yb_fdw_aggrefs));
		ExecInitScanTupleSlot(estate, ss, tupdesc, &TTSOpsVirtual);

		/*
		 * Consider the example "SELECT COUNT(oid) FROM pg_type", Postgres would have to do a
		 * sequential scan to fetch the system column oid. Here YSQL does pushdown so what's
		 * fetched from a tablet is the result of count(oid), which is not even a column, let
		 * alone a system column. Clear fsSystemCol because no system column is needed.
		 */
		foreignScan->fsSystemCol = false;
	}
	else
	{
		/* Set non-aggregate column targets. */
		bool target_added = false;
		foreach(lc, target_attrs)
		{
			TargetEntry *target = (TargetEntry *) lfirst(lc);
			AttrNumber	attnum = target->resno;

			if (attnum < 0)
				YbDmlAppendTargetSystem(attnum, handle);
			else
			{
				Assert(attnum > 0);
				if (!TupleDescAttr(tupdesc, attnum - 1)->attisdropped)
					YbDmlAppendTargetRegular(tupdesc, attnum, handle);
				else
					continue;
			}

			target_added = true;
		}

		/*
		 * We can have no target columns at this point for e.g. a count(*). We
		 * need to set a placeholder for the targets to properly make pg_dml
		 * fetcher recognize the correct number of rows though the targeted
		 * rows are not being effectively retrieved. Otherwise, the pg_dml
		 * fetcher will stop too early when seeing empty rows.
		 * TODO(#16717): Such placeholder target can be removed once the pg_dml
		 * fetcher can recognize empty rows in a response with no explict
		 * targets.
		 */
		if (!target_added)
			YbDmlAppendTargetSystem(YBTupleIdAttributeNumber, handle);
	}

	MemoryContextSwitchTo(oldcontext);
}

/*
 * ybcIterateForeignScan
 *		Read next record from the data file and store it into the
 *		ScanTupleSlot as a virtual tuple
 */
static TupleTableSlot *
ybcIterateForeignScan(ForeignScanState *node)
{
	YbFdwExecState *ybc_state = (YbFdwExecState *) node->fdw_state;
	EState	   *estate = node->ss.ps.state;
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;

	/*
	 * Unlike YbSeqScan, IndexScan, and IndexOnlyScan, YB ForeignScan does not
	 * call YbInstantiatePushdownParams before doing scan:
	 *
	 * - YbSeqNext
	 *   - YbInstantiatePushdownParams
	 *   - ybc_remote_beginscan
	 *     - YbDmlAppendQuals/YbDmlAppendColumnRefs
	 * - IndexScan/IndexNextWithReorder/ExecReScanIndexScan
	 *   - YbInstantiatePushdownParams
	 *   - index_rescan
	 *     - YbDmlAppendQuals/YbDmlAppendColumnRefs
	 * - IndexOnlyScan/ExecReScanIndexOnlyScan
	 *   - YbInstantiatePushdownParams
	 *   - index_rescan
	 *     - YbDmlAppendQuals/YbDmlAppendColumnRefs
	 * - ForeignNext
	 *   - ybcIterateForeignScan (impl of IterateForeignScan)
	 *     - YbInstantiatePushdownParams
	 *     - YbDmlAppendQuals/YbDmlAppendColumnRefs
	 *
	 * Reasoning:
	 *
	 * - FDW API does not provide an easy way to pass in a PushdownExprs
	 *   structure.  It allows passing in custom data using fdw_private, but it
	 *   expects a List type.  It technically can accept any type, but said
	 *   type should support node functions like copyObject() and
	 *   nodeToString(): PushdownExprs is not a node, so it doesn't support
	 *   them.  An alternative solution (though still not meeting the previous
	 *   criteria) is to rework PushdownExprs to be a list of structs rather
	 *   than a struct of lists, but this removes the ability to pass in the
	 *   entire quals list to YbExprInstantiateParams.  It can still be
	 *   iterated through and called on each qual, but that is more
	 *   inconvenient.
	 * - quals are already stored in fdw_recheck_quals, so putting them in
	 *   fdw_private in the form of PushdownExprs would be duplicate info.  Not
	 *   only that, but it would need to be updated after setup.
	 *
	 *   - exec_simple_query
	 *     - pg_plan_queries
	 *       - standard_planner
	 *         - create_plan
	 *           - ybcGetForeignPlan
	 *             - make_foreignscan
	 *               - (setup fdw_private, fdw_recheck_quals)
	 *         - set_plan_references
	 *           - set_foreignscan_references
	 *             - (modify fdw_recheck_quals)
	 *     - PortalRun
	 *       - ForeignNext
	 *         - ybcIterateForeignScan (this function)
	 *
	 *   If it were desired to solely rely on fdw_private holding
	 *   PushdownExprs, whatever modifications that happened to
	 *   fdw_recheck_quals would have to be updated onto fdw_private's copy of
	 *   quals before calling YbInstantiatePushdownParams.
	 * - Plan is to remove YB FDW code in favor of YbSeqScan, so it is not
	 *   worth the effort of making a good long-term solution here.
	 */
	PushdownExprs orig_pushdown = {
		.quals = foreignScan->fdw_recheck_quals,
		.colrefs = foreignScan->fdw_private,
	};
	PushdownExprs *pushdown = YbInstantiatePushdownParams(&orig_pushdown,
														  estate);

	/* Execute the select statement one time.
	 * TODO(neil) Check whether YugaByte PgGate should combine Exec() and Fetch() into one function.
	 * - The first fetch from YugaByte PgGate requires a number of operations including allocating
	 *   operators and protobufs. These operations are done by YBCPgExecSelect() function.
	 * - The subsequent fetches don't need to setup the query with these operations again.
	 */
	if (!ybc_state->is_exec_done) {
		ybcSetupScanTargets(node);
		if (pushdown != NULL)
		{
			YbDmlAppendQuals(pushdown->quals, true /* is_primary */,
							 ybc_state->handle);
			YbDmlAppendColumnRefs(pushdown->colrefs, true /* is_primary */,
								  ybc_state->handle);
		}
		HandleYBStatus(YBCPgExecSelect(ybc_state->handle, ybc_state->exec_params));
		ybc_state->is_exec_done = true;
	}

	/*
	 * If function forms a heap tuple, the ForeignNext function will set proper
	 * t_tableOid value there, so do not bother passing valid relid now.
	 */
	return ybFetchNext(ybc_state->handle,
					   node->ss.ss_ScanTupleSlot,
					   InvalidOid);
}

#ifdef NEIL_TODO
/* Keep this code till I look at ybFetchNext() */
{
	/* Clear tuple slot before starting */
	slot = node->ss.ss_ScanTupleSlot;
	ExecClearTuple(slot);

	TupleDesc       tupdesc = slot->tts_tupleDescriptor;
	Datum           *values = slot->tts_values;
	bool            *isnull = slot->tts_isnull;
	YBCPgSysColumns syscols;

	/* Fetch one row. */
	HandleYBStatus(YBCPgDmlFetch(ybc_state->handle,
	                             tupdesc->natts,
	                             (uint64_t *) values,
	                             isnull,
	                             &syscols,
	                             &has_data));

	/* If we have result(s) update the tuple slot. */
	if (has_data)
	{
		if (node->yb_fdw_aggs == NIL)
		{
			HeapTuple tuple = heap_form_tuple(tupdesc, values, isnull);
#ifdef NEIL_OID
	/* OID is now a regular column */
			if (syscols.oid != InvalidOid)
			{
				HeapTupleSetOid(tuple, syscols.oid);
			}
#endif
			slot = ExecStoreHeapTuple(tuple, slot, false);

			/* Setup special columns in the slot */
			TABLETUPLE_YBCTID(slot) = PointerGetDatum(syscols.ybctid);
		}
		else
		{
			/*
			 * Aggregate results stored in virtual slot (no tuple). Set the
			 * number of valid values and mark as non-empty.
			 */
			slot->tts_nvalid = tupdesc->natts;
			slot->tts_flags &= ~TTS_FLAG_EMPTY;
		}
	}

	return slot;
}
#endif

static void
ybcFreeStatementObject(YbFdwExecState* yb_fdw_exec_state)
{
	/* If yb_fdw_exec_state is NULL, we are in EXPLAIN; nothing to do */
	if (yb_fdw_exec_state != NULL && yb_fdw_exec_state->handle != NULL)
	{
		YBCPgDeleteStatement(yb_fdw_exec_state->handle);
		yb_fdw_exec_state->handle = NULL;
		yb_fdw_exec_state->exec_params = NULL;
		yb_fdw_exec_state->is_exec_done = false;
	}
}

/*
 * fileReScanForeignScan
 *		Rescan table, possibly with new parameters
 */
static void
ybcReScanForeignScan(ForeignScanState *node)
{
	YbFdwExecState *ybc_state = (YbFdwExecState *) node->fdw_state;

	/* Clear (delete) the previous select */
	ybcFreeStatementObject(ybc_state);

	/* Re-allocate and execute the select. */
	ybcBeginForeignScan(node, 0 /* eflags */);
}

/*
 * ybcEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan
 */
static void
ybcEndForeignScan(ForeignScanState *node)
{
	YbFdwExecState *ybc_state = (YbFdwExecState *) node->fdw_state;
	ybcFreeStatementObject(ybc_state);
}

/* ------------------------------------------------------------------------- */
/*  FDW declaration */

/*
 * Foreign-data wrapper handler function: return a struct with pointers
 * to YugaByte callback routines.
 */
Datum
ybc_fdw_handler()
{
	FdwRoutine *fdwroutine = makeNode(FdwRoutine);

	fdwroutine->GetForeignRelSize  = ybcGetForeignRelSize;
	fdwroutine->GetForeignPaths    = ybcGetForeignPaths;
	fdwroutine->GetForeignPlan     = ybcGetForeignPlan;
	fdwroutine->BeginForeignScan   = ybcBeginForeignScan;
	fdwroutine->IterateForeignScan = ybcIterateForeignScan;
	fdwroutine->ReScanForeignScan  = ybcReScanForeignScan;
	fdwroutine->EndForeignScan     = ybcEndForeignScan;

	/* TODO: These are optional but we should support them eventually. */
	/* fdwroutine->ExplainForeignScan = ybcExplainForeignScan; */
	/* fdwroutine->AnalyzeForeignTable = ybcAnalyzeForeignTable; */
	/* fdwroutine->IsForeignScanParallelSafe = ybcIsForeignScanParallelSafe; */

	PG_RETURN_POINTER(fdwroutine);
}
