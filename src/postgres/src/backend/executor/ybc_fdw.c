/*--------------------------------------------------------------------------------------------------
 *
 * ybc_fdw.c
 *		  Foreign-data wrapper for YugaByte DB.
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

#include "executor/ybc_fdw.h"

/*  TODO see which includes of this block are still needed. */
#include "access/htup_details.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
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
#include "optimizer/planmain.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/var.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/sampling.h"

/*  YB includes. */
#include "commands/dbcommands.h"
#include "catalog/pg_operator.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"

#include "utils/resowner_private.h"

/* Number of rows assumed for a YB table if no size estimates exist */
static const int DEFAULT_YB_NUM_ROWS = 1000;

/* -------------------------------------------------------------------------- */
/*  Planner/Optimizer functions */

typedef struct YbFdwPlanState
{
	/* YugaByte metadata about the referenced table/relation. */
	Bitmapset *primary_key;
	Bitmapset *hash_key;

	/* Bitmap of attribute (column) numbers that we need to fetch from YB. */
	Bitmapset *target_attrs;

	/* (Equality) Conditions on hash key -- filtered by YugaByte */
	List *yb_hconds;

	/* (Equality) Conditions on range key -- filtered by YugaByte */
	List *yb_rconds;

	/* Rest of baserestrictinfo conditions -- filtered by Postgres */
	List *pg_conds;

	/*
	 * The set of columns set by YugaByte conds (i.e. in yb_hconds or yb_rconds
	 * above). Used to check if hash or primary key is fully set.
	 */
	Bitmapset *yb_cols;

} YbFdwPlanState;

/*
 * Returns whether an expression can be pushed down to be evaluated by YugaByte.
 * Otherwise, it will need to be evaluated by Postgres as it filters the rows
 * returned by YugaByte.
 */
static void ybcClassifyWhereExpr(RelOptInfo *baserel,
		                        YbFdwPlanState *yb_state,
		                        Expr *expr)
{
	HeapTuple        tuple;
	Form_pg_operator form;
	/* YugaByte only supports base relations (e.g. no joins or child rels) */
	if (baserel->reloptkind == RELOPT_BASEREL)
	{

		/* YugaByte only supports operator expressions (e.g. no functions) */
		if (IsA(expr, OpExpr))
		{

			/* Get operator info */
			OpExpr *opExpr = (OpExpr *) expr;
			tuple = SearchSysCache1(OPEROID, ObjectIdGetDatum(opExpr->opno));
			if (!HeapTupleIsValid(tuple))
				ereport(ERROR,
				        (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
						        "cache lookup failed for operator %u",
						        opExpr->opno)));
			form = (Form_pg_operator) GETSTRUCT(tuple);
			char *opname = NameStr(form->oprname);
			bool is_eq   = strcmp(opname, "=") == 0;
			/* Note: the != operator is converted to <> in the parser stage */
			bool is_ineq = strcmp(opname, ">") == 0 ||
			               strcmp(opname, ">=") == 0 ||
			               strcmp(opname, "<") == 0 ||
			               strcmp(opname, "<=") == 0 ||
			               strcmp(opname, "<>") == 0;

			ReleaseSysCache(tuple);

			/* Currently, YugaByte only supports comparison operators. */
			if (is_eq || is_ineq)
			{
				/* Supported operators ensure there are exactly two arguments */
				Expr *left  = linitial(opExpr->args);
				Expr *right = lsecond(opExpr->args);

				/*
				 * Currently, YugaByte only supports conds of the form '<col>
				 * <op> <value>' or '<value> <op> <col>' at this point.
				 * Note: Postgres should have already evaluated expressions
				 * with no column refs before this point.
				 */
				if ((IsA(left, Var) && IsA(right, Const)) ||
				    (IsA(left, Const) && IsA(right, Var)))
				{
					AttrNumber attrNum;
					attrNum = IsA(left, Var) ? ((Var *) left)->varattno
					                         : ((Var *) right)->varattno;

					int bms_idx = attrNum - baserel->min_attr + 1;
					bool is_primary = bms_is_member(bms_idx,
					                                yb_state->primary_key);
					bool is_hash    = bms_is_member(bms_idx,
					                                yb_state->hash_key);

					/*
					 * TODO Once we support WHERE clause in pggate, these
					 * conditions need to be updated accordingly.
					 */
					if (is_hash && is_eq)
					{
						yb_state->yb_cols   = bms_add_member(yb_state->yb_cols,
						                                     bms_idx);
						yb_state->yb_hconds = lappend(yb_state->yb_hconds,
						                              expr);
						return;
					}
					else if (is_primary && is_eq)
					{
						yb_state->yb_cols   = bms_add_member(yb_state->yb_cols,
						                                     bms_idx);
						yb_state->yb_rconds = lappend(yb_state->yb_rconds,
						                              expr);
						return;
					}
				}
			}
		}
	}

	/* Otherwise let postgres handle the condition (default) */
	yb_state->pg_conds = lappend(yb_state->pg_conds, expr);
}

/*
 * Add a Postgres expression as a where condition to a YugaByte select
 * statement. Assumes the expression can be evaluated by YugaByte
 * (i.e. ybcIsYbExpression returns true).
 */
static void ybcAddWhereCond(Expr* expr, YBCPgStatement yb_stmt)
{
	OpExpr *opExpr = (OpExpr *) expr;

	/*
	 * ybcClassifyWhereExpr should only pass conditions to YugaByte if the
	 * assertion below holds.
	 */
	Assert(opExpr->args->length == 2);
	Expr *left  = linitial(opExpr->args);
	Expr *right = lsecond(opExpr->args);
	Assert((IsA(left, Var) && IsA(right, Const)) ||
	       (IsA(left, Const) && IsA(right, Var)));

	Var       *col_desc = IsA(left, Var) ? (Var *) left : (Var *) right;
	Const     *col_val  = IsA(left, Var) ? (Const *) right : (Const *) left;
	YBCPgExpr ybc_expr  = YBCNewConstant(yb_stmt,
	                                     col_desc->vartype,
	                                     col_val->constvalue,
	                                     col_val->constisnull);
	HandleYBStatus(YBCPgDmlBindColumn(yb_stmt, col_desc->varattno, ybc_expr));
}

/*
 * ybcGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
static void
ybcGetForeignRelSize(PlannerInfo *root,
					 RelOptInfo *baserel,
					 Oid foreigntableid)
{
	Oid					relid;
	Relation			rel = NULL;
	ListCell 			*cell = NULL;
	YbFdwPlanState		*ybc_plan = NULL;

	ybc_plan = (YbFdwPlanState *) palloc0(sizeof(YbFdwPlanState));

	relid = root->simple_rte_array[baserel->relid]->relid;

	/*
	 * Get table info (from both Postgres and YugaByte).
	 * YugaByte info is currently mainly primary and partition (hash) keys.
	 */
	rel = RelationIdGetRelation(relid);
	YBCPgTableDesc ybc_table_desc = NULL;
	HandleYBStatus(YBCPgGetTableDesc(ybc_pg_session,
									 YBCGetDatabaseOid(rel),
									 relid,
	                                 &ybc_table_desc));

	for (AttrNumber col = baserel->min_attr; col <= baserel->max_attr; col++)
	{
		bool is_primary = false;
		bool is_hash    = false;
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
		                                           col,
		                                           &is_primary,
		                                           &is_hash), ybc_table_desc);
		int bms_idx = col - baserel->min_attr + 1;
		if (is_hash)
		{
			ybc_plan->hash_key = bms_add_member(ybc_plan->hash_key, bms_idx);
		}
		if (is_primary)
		{
			ybc_plan->primary_key = bms_add_member(ybc_plan->primary_key,
			                                       bms_idx);
		}
	}
	HandleYBStatus(YBCPgDeleteTableDesc(ybc_table_desc));
	ybc_table_desc = NULL;
	RelationClose(rel);
	rel = NULL;

	/*
	 * Split scan_clauses between those handled by YugaByte and the rest (which
	 * should be checked by Postgres).
	 * Ignore pseudoconstants (which will be handled elsewhere).
	 */
	foreach(cell, baserel->baserestrictinfo)
	{
		RestrictInfo *ri = (RestrictInfo *) lfirst(cell);
		ybcClassifyWhereExpr(baserel, ybc_plan, ri->clause);
	}

	/* Save the output-rows estimate for the planner */
	baserel->rows = DEFAULT_YB_NUM_ROWS;
	baserel->fdw_private = ybc_plan;
}

/*
 * ybcGetForeignPaths
 *		Create possible access paths for a scan on the foreign table
 *
 *		Currently we don't support any push-down feature, so there is only one
 *		possible access path, which simply returns all records in the order in
 *		the data file.
 */
static void
ybcGetForeignPaths(PlannerInfo *root,
				   RelOptInfo *baserel,
				   Oid foreigntableid)
{
	Cost startup_cost;
	Cost total_cost;
	Cost cpu_per_tuple;

	/* Estimate costs */
	startup_cost  = baserel->baserestrictcost.startup;
	cpu_per_tuple = cpu_tuple_cost * 10 + baserel->baserestrictcost.per_tuple;
	total_cost    = startup_cost + seq_page_cost * baserel->pages +
	                cpu_per_tuple * baserel->rows;

	/* Create a ForeignPath node and add it as only possible path. */
	/* TODO Can add YB order guarantees to pathkeys (if hash key is fixed). */
	add_path(baserel,
	         (Path *) create_foreignscan_path(root,
	                                          baserel,
	                                          NULL,    /* default pathtarget */
	                                          baserel->rows,
	                                          startup_cost,
	                                          total_cost,
	                                          NIL,    /* no pathkeys */
	                                          NULL,    /* no outer rel either */
	                                          NULL,    /* no extra plan */
	                                          NULL /* no options yet */ ));
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
	Index          scan_relid     = baserel->relid;
	List           *fdw_private;
	ListCell       *lc;

	/*
	 * Split any unprocessed scan_clauses (i.e. joins restrictions if any)
	 * between those handled by YugaByte and the rest (which should be
	 * checked by Postgres).
	 * Ignore pseudoconstants (which will be handled elsewhere).
	 */
	scan_clauses = extract_actual_clauses(scan_clauses, false);

	foreach(lc, scan_clauses)
	{
		Expr *expr = (Expr *) lfirst(lc);
		if (!list_member_ptr(yb_plan_state->yb_hconds, expr) &&
		    !list_member_ptr(yb_plan_state->yb_rconds, expr) &&
		    !list_member_ptr(yb_plan_state->pg_conds, expr))
		{
			ybcClassifyWhereExpr(baserel, yb_plan_state, expr);
		}
	}

	/*
	 * If hash key is not fully set, we must do a full-table scan in YugaByte
	 * and defer all filtering to Postgres.
	 * Else, if primary key is not fully set we need to remove all range
	 * key conds and defer filtering for range column conds to Postgres.
	 */
	if (!bms_is_subset(yb_plan_state->hash_key, yb_plan_state->yb_cols))
	{
		yb_plan_state->pg_conds  = scan_clauses;
		yb_plan_state->yb_hconds = NIL;
		yb_plan_state->yb_rconds = NIL;
	}
	else if (!bms_is_subset(yb_plan_state->primary_key, yb_plan_state->yb_cols))
	{
		yb_plan_state->pg_conds  = list_concat(yb_plan_state->pg_conds,
		                                       yb_plan_state->yb_rconds);
		yb_plan_state->yb_rconds = NIL;
	}

	/*
	 * Get the target columns that need to be retrieved from YugaByte.
	 * Specifically, any columns that are either:
	 * 1. Referenced in the select targets (i.e. selected columns or exprs).
	 * 2. Referenced in the WHERE clause exprs that Postgres must evaluate.
	 */
	foreach(lc, baserel->reltarget->exprs)
	{
		Expr *expr = (Expr *) lfirst(lc);
		pull_varattnos_min_attr((Node *) expr,
		                        baserel->relid,
		                        &yb_plan_state->target_attrs,
		                        baserel->min_attr);
	}

	foreach(lc, yb_plan_state->pg_conds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		pull_varattnos_min_attr((Node *) expr,
		                        baserel->relid,
		                        &yb_plan_state->target_attrs,
		                        baserel->min_attr);
	}

	/* Set scan targets. */
	List *target_attrs = NULL;
	for (AttrNumber i = baserel->min_attr; i <= baserel->max_attr; i++)
	{
		int bms_idx = i - baserel->min_attr + 1;
		if (bms_is_member(bms_idx, yb_plan_state->target_attrs))
		{
			switch (i)
			{
				case InvalidAttrNumber:
					ereport(ERROR,
					        (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
							        "Unexpected invalid attribute number")));
					break;
				case SelfItemPointerAttributeNumber:
				case MinTransactionIdAttributeNumber:
				case MinCommandIdAttributeNumber:
				case MaxTransactionIdAttributeNumber:
				case MaxCommandIdAttributeNumber:
					ereport(ERROR,
					        (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg(
							        "System column with id %d is not supported yet",
							        i)));
					break;
				case TableOidAttributeNumber:
					/* Nothing to do in YugaByte: Postgres will handle this. */
					break;
				case ObjectIdAttributeNumber:
				case YBTupleIdAttributeNumber:
				default: /* Regular column: attrNum > 0*/
				{
					TargetEntry *target = makeNode(TargetEntry);
					target->resno = i;
					target_attrs = lappend(target_attrs, target);
				}
			}
		}
	}

	/*
	 * We can have no target columns for e.g. a count(*). For now we request
	 * the first primary key column in that case.
	 * TODO look into handling this on YugaByte side.
	 */
	if (!target_attrs)
	{
		TargetEntry *target = makeNode(TargetEntry);
		int bms_idx = bms_next_member(yb_plan_state->primary_key, -1);
		target->resno = bms_idx + baserel->min_attr - 1;
		target_attrs = lappend(target_attrs, target);
	}

	List *yb_conds = list_concat(yb_plan_state->yb_hconds,
	                             yb_plan_state->yb_rconds);

	/* Create the ForeignScan node */
	fdw_private = list_make2(target_attrs, yb_conds);
	return make_foreignscan(tlist,  /* target list */
	                        yb_plan_state->pg_conds,  /* checked by Postgres */
	                        scan_relid,
	                        NIL,    /* expressions YB may evaluate (none) */
	                        fdw_private,  /* private data for YB */
	                        NIL,    /* custom YB target list (none for now */
	                        yb_conds,    /* checked by YB */
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
	YBCPgStatement handle;
	ResourceOwner stmt_owner;
} YbFdwExecState;

/*
 * ybcBeginForeignScan
 *		Initiate access to the Yugabyte by allocating a Select handle.
 */
static void
ybcBeginForeignScan(ForeignScanState *node, int eflags)
{
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;
	Relation    relation     = node->ss.ss_currentRelation;

	/* Planning function above should ensure both target and conds are set */
	Assert(foreignScan->fdw_private->length == 2);
	List *target_attrs = linitial(foreignScan->fdw_private);
	List *yb_conds     = lsecond(foreignScan->fdw_private);

	YbFdwExecState *ybc_state = NULL;
	ListCell       *lc;

	/* Do nothing in EXPLAIN (no ANALYZE) case.  node->fdw_state stays NULL. */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;

	/* Allocate and initialize YB scan state. */
	ybc_state = (YbFdwExecState *) palloc0(sizeof(YbFdwExecState));

	node->fdw_state = (void *) ybc_state;
	HandleYBStatus(YBCPgNewSelect(ybc_pg_session,
								  YBCGetDatabaseOid(relation),
								  RelationGetRelid(relation),
	                              &ybc_state->handle));
	ResourceOwnerEnlargeYugaByteStmts(CurrentResourceOwner);
	ResourceOwnerRememberYugaByteStmt(CurrentResourceOwner, ybc_state->handle);
	ybc_state->stmt_owner = CurrentResourceOwner;

	/* Set WHERE clause values (currently only primary key). */
	foreach(lc, yb_conds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybcAddWhereCond(expr, ybc_state->handle);
	}

	/* Set scan targets. */
	foreach(lc, target_attrs)
	{
		TargetEntry *target = (TargetEntry *) lfirst(lc);

		/* For regular (non-system) attribute check if they were deleted */
		if (target->resno > 0)
		{
			Form_pg_attribute attr;
			attr = relation->rd_att->attrs[target->resno - 1];
			/* Ignore dropped attributes */
			if (attr->attisdropped)
			{
				continue;
			}
		}
		YBCPgExpr   expr    = YBCNewColumnRef(ybc_state->handle, target->resno);
		HandleYBStmtStatusWithOwner(YBCPgDmlAppendTarget(ybc_state->handle,
		                                                 expr),
		                            ybc_state->handle,
		                            ybc_state->stmt_owner);
	}

	/* Execute the select statement. */
	HandleYBStmtStatusWithOwner(YBCPgExecSelect(ybc_state->handle),
	                            ybc_state->handle,
	                            ybc_state->stmt_owner);
}

/*
 * ybcIterateForeignScan
 *		Read next record from the data file and store it into the
 *		ScanTupleSlot as a virtual tuple
 */
static TupleTableSlot *
ybcIterateForeignScan(ForeignScanState *node)
{
	TupleTableSlot *slot      = node->ss.ss_ScanTupleSlot;
	YbFdwExecState *ybc_state = (YbFdwExecState *) node->fdw_state;
	bool           has_data   = false;

	/* Clear tuple slot before starting */
	ExecClearTuple(slot);

	TupleDesc tupdesc = slot->tts_tupleDescriptor;

	Datum           *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool            *nulls  = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Fetch one row. */
	HandleYBStmtStatusWithOwner(YBCPgDmlFetch(ybc_state->handle,
	                                          tupdesc->natts,
	                                          (uint64_t *) values,
	                                          nulls,
	                                          &syscols,
	                                          &has_data),
	                            ybc_state->handle,
	                            ybc_state->stmt_owner);

	/* If we have result(s) update the tuple slot. */
	if (has_data)
	{
		HeapTuple tuple = heap_form_tuple(tupdesc, values, nulls);
		if (syscols.oid != InvalidOid)
		{
			HeapTupleSetOid(tuple, syscols.oid);
		}

		slot = ExecStoreTuple(tuple, slot, InvalidBuffer, false);

		/* Setup special columns in the slot */
		slot->tts_ybctid = PointerGetDatum(syscols.ybctid);
	}

	return slot;
}

static void
ybcFreeStatementObject(YbFdwExecState* yb_fdw_exec_state)
{
	/* If yb_fdw_exec_state is NULL, we are in EXPLAIN; nothing to do */
	if (yb_fdw_exec_state != NULL && yb_fdw_exec_state->handle != NULL)
	{
		HandleYBStatus(YBCPgDeleteStatement(yb_fdw_exec_state->handle));
		ResourceOwnerForgetYugaByteStmt(yb_fdw_exec_state->stmt_owner,
										yb_fdw_exec_state->handle);
		yb_fdw_exec_state->handle = NULL;
		yb_fdw_exec_state->stmt_owner = NULL;
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
	/* fdwroutine->ExplainForeignScan = fileExplainForeignScan; */
	/* fdwroutine->AnalyzeForeignTable = ybcAnalyzeForeignTable; */
	/* fdwroutine->IsForeignScanParallelSafe = ybcIsForeignScanParallelSafe; */

	PG_RETURN_POINTER(fdwroutine);
}
