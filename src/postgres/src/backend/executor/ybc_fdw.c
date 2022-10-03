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
#include "optimizer/var.h"
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
		if (baserel->tuples == 0)
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
	List		   *local_clauses = NIL;
	List		   *remote_clauses = NIL;
	List		   *remote_params = NIL;
	ListCell	   *lc;

	scan_clauses = extract_actual_clauses(scan_clauses, false);

	/*
	 * Split the expressions in the scan_clauses onto two lists:
	 * - remote_clauses gets supported expressions to push down to DocDB, and
	 * - local_clauses gets remaining to evaluate upon returned rows.
	 * The remote_params list contains data type details of the columns
	 * referenced by the expressions in the remote_clauses list. DocDB needs it
	 * to convert row values to Datum/isnull pairs consumable by Postgres
	 * functions.
	 * The remote_clauses and remote_params lists are sent with the protobuf
	 * read request.
	 */
	foreach(lc, scan_clauses)
	{
		List *params = NIL;
		Expr *expr = (Expr *) lfirst(lc);
		if (YbCanPushdownExpr(expr, &params))
		{
			remote_clauses = lappend(remote_clauses, expr);
			remote_params = list_concat(remote_params, params);
		}
		else
		{
			local_clauses = lappend(local_clauses, expr);
			list_free_deep(params);
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

	/* Get the target columns that are needed to evaluate local clauses */
	foreach(lc, local_clauses)
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
				case ObjectIdAttributeNumber:
				case YBTupleIdAttributeNumber:
				default: /* Regular column: attrNum > 0*/
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
							local_clauses,   /* local qual */
							scan_relid,
							target_attrs,    /* referenced attributes */
							remote_params,   /* fdw_private data (attribute types) */
							NIL,             /* remote target list (none for now) */
							remote_clauses,  /* remote qual */
							outer_plan);
}

/* ------------------------------------------------------------------------- */
/*  Scanning functions */

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
	if (XactIsoLevel == XACT_SERIALIZABLE)
	{
		/*
		 * In case of SERIALIZABLE isolation level we have to take predicate locks to disallow
		 * INSERTion of new rows that satisfy the query predicate. So, we set the rowmark on all
		 * read requests sent to tserver instead of locking each tuple one by one in LockRows node.
		 */
		ListCell   *l;
		foreach(l, estate->es_rowMarks) {
			ExecRowMark *erm = (ExecRowMark *) lfirst(l);
			// Do not propagate non-row-locking row marks.
			if (erm->markType != ROW_MARK_REFERENCE && erm->markType != ROW_MARK_COPY)
			{
				ybc_state->exec_params->rowmark = erm->markType;
				/*
				 * TODO(Piyush): We don't honour SKIP LOCKED yet in serializable isolation level.
				 */
				ybc_state->exec_params->wait_policy = LockWaitError;
			}
			break;
		}
	}

	ybc_state->is_exec_done = false;

	/* Set the current syscatalog version (will check that we are up to date) */
	HandleYBStatus(YBCPgSetCatalogCacheVersion(ybc_state->handle, yb_catalog_cache_version));
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
	EState *estate = node->ss.ps.state;
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;
	Relation relation = node->ss.ss_currentRelation;
	YbFdwExecState *ybc_state = (YbFdwExecState *) node->fdw_state;
	TupleDesc tupdesc = RelationGetDescr(relation);
	ListCell *lc;

	/* Planning function above should ensure target list is set */
	List *target_attrs = foreignScan->fdw_exprs;

	MemoryContext oldcontext =
		MemoryContextSwitchTo(node->ss.ps.ps_ExprContext->ecxt_per_query_memory);

	/* Set scan targets. */
	if (node->yb_fdw_aggs == NIL)
	{
		/* Set non-aggregate column targets. */
		bool has_targets = false;
		foreach(lc, target_attrs)
		{
			TargetEntry *target = (TargetEntry *) lfirst(lc);

			/* For regular (non-system) attribute check if they were deleted */
			Oid   attr_typid  = InvalidOid;
			Oid   attr_collation = InvalidOid;
			int32 attr_typmod = 0;
			if (target->resno > 0)
			{
				Form_pg_attribute attr;
				attr = TupleDescAttr(tupdesc, target->resno - 1);
				/* Ignore dropped attributes */
				if (attr->attisdropped)
				{
					continue;
				}
				attr_typid  = attr->atttypid;
				attr_typmod = attr->atttypmod;
				attr_collation = attr->attcollation;
			}

			YBCPgTypeAttrs type_attrs = {attr_typmod};
			YBCPgExpr      expr       = YBCNewColumnRef(ybc_state->handle,
														target->resno,
														attr_typid,
														attr_collation,
														&type_attrs);
			HandleYBStatus(YBCPgDmlAppendTarget(ybc_state->handle, expr));
			has_targets = true;
		}

		/*
		 * We can have no target columns at this point for e.g. a count(*). For now
		 * we request the first non-dropped column in that case.
		 * TODO look into handling this on YugaByte side.
		 */
		if (!has_targets)
		{
			for (int16_t i = 0; i < tupdesc->natts; i++)
			{
				/* Ignore dropped attributes */
				if (TupleDescAttr(tupdesc, i)->attisdropped)
				{
					continue;
				}

				YBCPgTypeAttrs type_attrs = { TupleDescAttr(tupdesc, i)->atttypmod };
				YBCPgExpr      expr       = YBCNewColumnRef(ybc_state->handle,
															i + 1,
															TupleDescAttr(tupdesc, i)->atttypid,
															TupleDescAttr(tupdesc, i)->attcollation,
															&type_attrs);
				HandleYBStatus(YBCPgDmlAppendTarget(ybc_state->handle, expr));
				break;
			}
		}
	}
	else
	{
		/* Set aggregate scan targets. */
		foreach(lc, node->yb_fdw_aggs)
		{
			Aggref *aggref = lfirst_node(Aggref, lc);
			char *func_name = get_func_name(aggref->aggfnoid);
			ListCell *lc_arg;
			YBCPgExpr op_handle;
			const YBCPgTypeEntity *type_entity;

			/* Get type entity for the operator from the aggref. */
			type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, aggref->aggtranstype);

			/* Create operator. */
			HandleYBStatus(YBCPgNewOperator(ybc_state->handle, func_name, type_entity, aggref->aggcollid, &op_handle));

			/* Handle arguments. */
			if (aggref->aggstar) {
				/*
				 * Add dummy argument for COUNT(*) case, turning it into COUNT(0).
				 * We don't use a column reference as we want to count rows
				 * even if all column values are NULL.
				 */
				YBCPgExpr const_handle;
				HandleYBStatus(YBCPgNewConstant(ybc_state->handle,
								 type_entity,
								 false /* collate_is_valid_non_c */,
								 NULL /* collation_sortkey */,
								 0 /* datum */,
								 false /* is_null */,
								 &const_handle));
				HandleYBStatus(YBCPgOperatorAppendArg(op_handle, const_handle));
			} else {
				/* Add aggregate arguments to operator. */
				foreach(lc_arg, aggref->args)
				{
					TargetEntry *tle = lfirst_node(TargetEntry, lc_arg);
					if (IsA(tle->expr, Const))
					{
						Const* const_node = castNode(Const, tle->expr);
						/* Already checked by yb_agg_pushdown_supported */
						Assert(const_node->constisnull || const_node->constbyval);

						YBCPgExpr const_handle;
						HandleYBStatus(YBCPgNewConstant(ybc_state->handle,
										 type_entity,
										 false /* collate_is_valid_non_c */,
										 NULL /* collation_sortkey */,
										 const_node->constvalue,
										 const_node->constisnull,
										 &const_handle));
						HandleYBStatus(YBCPgOperatorAppendArg(op_handle, const_handle));
					}
					else if (IsA(tle->expr, Var))
					{
						/*
						 * Use original attribute number (varoattno) instead of projected one (varattno)
						 * as projection is disabled for tuples produced by pushed down operators.
						 */
						int attno = castNode(Var, tle->expr)->varoattno;
						Form_pg_attribute attr = TupleDescAttr(tupdesc, attno - 1);
						YBCPgTypeAttrs type_attrs = {attr->atttypmod};

						YBCPgExpr arg = YBCNewColumnRef(ybc_state->handle,
														attno,
														attr->atttypid,
														attr->attcollation,
														&type_attrs);
						HandleYBStatus(YBCPgOperatorAppendArg(op_handle, arg));
					}
					else
					{
						/* Should never happen. */
						ereport(ERROR,
								(errcode(ERRCODE_INTERNAL_ERROR),
								 errmsg("unsupported aggregate function argument type")));
					}
				}
			}

			/* Add aggregate operator as scan target. */
			HandleYBStatus(YBCPgDmlAppendTarget(ybc_state->handle, op_handle));
		}

		/*
		 * Setup the scan slot based on new tuple descriptor for the given targets. This is a dummy
		 * tupledesc that only includes the number of attributes.
		 */
		TupleDesc target_tupdesc = CreateTemplateTupleDesc(list_length(node->yb_fdw_aggs),
														   false /* hasoid */);
		ExecInitScanTupleSlot(estate, &node->ss, target_tupdesc);

		/*
		 * Consider the example "SELECT COUNT(oid) FROM pg_type", Postgres would have to do a
		 * sequential scan to fetch the system column oid. Here YSQL does pushdown so what's
		 * fetched from a tablet is the result of count(oid), which is not even a column, let
		 * alone a system column. Clear fsSystemCol because no system column is needed.
		 */
		foreignScan->fsSystemCol = false;
	}
	MemoryContextSwitchTo(oldcontext);
}

/*
 * ybSetupScanQual
 *		Add the pushable qual expressions to the DocDB statement.
 */
static void
ybSetupScanQual(ForeignScanState *node)
{
	EState	   *estate = node->ss.ps.state;
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;
	YbFdwExecState *yb_state = (YbFdwExecState *) node->fdw_state;
	List	   *qual = foreignScan->fdw_recheck_quals;
	ListCell   *lc;

	MemoryContext oldcontext =
		MemoryContextSwitchTo(node->ss.ps.ps_ExprContext->ecxt_per_query_memory);

	foreach(lc, qual)
	{
		Expr *expr = (Expr *) lfirst(lc);
		/*
		 * Some expressions may be parametrized, obviously remote end can not
		 * acccess the estate to get parameter values, so param references
		 * are replaced with constant expressions.
		 */
		expr = YbExprInstantiateParams(expr, estate);
		/* Create new PgExpr wrapper for the expression */
		YBCPgExpr yb_expr = YBCNewEvalExprCall(yb_state->handle, expr);
		/* Add the PgExpr to the statement */
		HandleYBStatus(YbPgDmlAppendQual(yb_state->handle, yb_expr, true));
	}

	MemoryContextSwitchTo(oldcontext);
}

/*
 * ybSetupScanColumnRefs
 *		Add the column references to the DocDB statement.
 */
static void
ybSetupScanColumnRefs(ForeignScanState *node)
{
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;
	YbFdwExecState *yb_state = (YbFdwExecState *) node->fdw_state;
	List	   *params = foreignScan->fdw_private;
	ListCell   *lc;

	MemoryContext oldcontext =
		MemoryContextSwitchTo(node->ss.ps.ps_ExprContext->ecxt_per_query_memory);

	foreach(lc, params)
	{
		YbExprParamDesc *param = (YbExprParamDesc *) lfirst(lc);
		YBCPgTypeAttrs type_attrs = { param->typmod };
		/* Create new PgExpr wrapper for the column reference */
		YBCPgExpr yb_expr = YBCNewColumnRef(yb_state->handle,
											param->attno,
											param->typid,
											param->collid,
											&type_attrs);
		/* Add the PgExpr to the statement */
		HandleYBStatus(YbPgDmlAppendColumnRef(yb_state->handle, yb_expr, true));
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

	/* Execute the select statement one time.
	 * TODO(neil) Check whether YugaByte PgGate should combine Exec() and Fetch() into one function.
	 * - The first fetch from YugaByte PgGate requires a number of operations including allocating
	 *   operators and protobufs. These operations are done by YBCPgExecSelect() function.
	 * - The subsequent fetches don't need to setup the query with these operations again.
	 */
	if (!ybc_state->is_exec_done) {
		ybcSetupScanTargets(node);
		ybSetupScanQual(node);
		ybSetupScanColumnRefs(node);
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

/*
 * ybcExplainForeignScan
 *		Produce extra output for EXPLAIN of a ForeignScan on a foreign table
 */
static void
ybcExplainForeignScan(ForeignScanState *node, ExplainState *es)
{
	if (node->yb_fdw_aggs != NIL)
		ExplainPropertyBool("Partial Aggregate", true, es);
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
	fdwroutine->ExplainForeignScan = ybcExplainForeignScan;

	/* TODO: These are optional but we should support them eventually. */
	/* fdwroutine->AnalyzeForeignTable = ybcAnalyzeForeignTable; */
	/* fdwroutine->IsForeignScanParallelSafe = ybcIsForeignScanParallelSafe; */

	PG_RETURN_POINTER(fdwroutine);
}
