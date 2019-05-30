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
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"
#include "access/ybcam.h"
#include "executor/ybcExpr.h"

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

	/* Save the output-rows estimate for the planner */
	baserel->rows = YBC_DEFAULT_NUM_ROWS;
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
	ybcCostEstimate(baserel, YBC_FULL_SCAN_SELECTIVITY, &startup_cost, &total_cost);

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
	Index          scan_relid     = baserel->relid;
	ListCell       *lc;

	scan_clauses = extract_actual_clauses(scan_clauses, false);

	/* Get the target columns that need to be retrieved from YugaByte */
	foreach(lc, baserel->reltarget->exprs)
	{
		Expr *expr = (Expr *) lfirst(lc);
		pull_varattnos_min_attr((Node *) expr,
		                        baserel->relid,
		                        &yb_plan_state->target_attrs,
		                        baserel->min_attr);
	}

	foreach(lc, scan_clauses)
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
	return make_foreignscan(tlist,  /* target list */
	                        scan_clauses,
	                        scan_relid,
	                        NIL,    /* expressions YB may evaluate (none) */
	                        target_attrs,  /* fdw_private data for YB */
	                        NIL,    /* custom YB target list (none for now) */
	                        NIL,    /* custom YB target list (none for now) */
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
	ResourceOwner	stmt_owner;
} YbFdwExecState;

/*
 * ybcBeginForeignScan
 *		Initiate access to the Yugabyte by allocating a Select handle.
 */
static void
ybcBeginForeignScan(ForeignScanState *node, int eflags)
{
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;
	EState      *estate      = node->ss.ps.state;
	Relation    relation     = node->ss.ss_currentRelation;
	TupleDesc   tupdesc      = RelationGetDescr(relation);

	/* Planning function above should ensure target list is set */
	List *target_attrs = foreignScan->fdw_private;

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
	                              InvalidOid /* index_oid */,
	                              &ybc_state->handle,
	                              estate ? &estate->es_yb_read_ht : 0));
	ResourceOwnerEnlargeYugaByteStmts(CurrentResourceOwner);
	ResourceOwnerRememberYugaByteStmt(CurrentResourceOwner, ybc_state->handle);
	ybc_state->stmt_owner = CurrentResourceOwner;

	/* Set scan targets. */
	bool has_targets = false;
	foreach(lc, target_attrs)
	{
		TargetEntry *target = (TargetEntry *) lfirst(lc);

		/* For regular (non-system) attribute check if they were deleted */
		Oid   attr_typid  = InvalidOid;
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
		}

		YBCPgTypeAttrs type_attrs = {attr_typmod};
		YBCPgExpr      expr       = YBCNewColumnRef(ybc_state->handle,
		                                            target->resno,
		                                            attr_typid,
		                                            &type_attrs);
		HandleYBStmtStatusWithOwner(YBCPgDmlAppendTarget(ybc_state->handle,
		                                                 expr),
		                            ybc_state->handle,
		                            ybc_state->stmt_owner);
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
			                                            &type_attrs);
			HandleYBStmtStatusWithOwner(YBCPgDmlAppendTarget(ybc_state->handle,
			                                                 expr),
			                            ybc_state->handle,
			                            ybc_state->stmt_owner);
			break;
		}
	}

	/* Set the current syscatalog version (will check that we are up to date) */
	HandleYBStmtStatusWithOwner(YBCPgSetCatalogCacheVersion(ybc_state->handle,
	                                                        yb_catalog_cache_version),
	                            ybc_state->handle,
	                            ybc_state->stmt_owner);

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

	TupleDesc       tupdesc = slot->tts_tupleDescriptor;
	Datum           *values = slot->tts_values;
	bool            *isnull = slot->tts_isnull;
	YBCPgSysColumns syscols;

	/* Fetch one row. */
	HandleYBStmtStatusWithOwner(YBCPgDmlFetch(ybc_state->handle,
	                                          tupdesc->natts,
	                                          (uint64_t *) values,
	                                          isnull,
	                                          &syscols,
	                                          &has_data),
	                            ybc_state->handle,
	                            ybc_state->stmt_owner);

	/* If we have result(s) update the tuple slot. */
	if (has_data)
	{
		HeapTuple tuple = heap_form_tuple(tupdesc, values, isnull);
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
	/* fdwroutine->ExplainForeignScan = ybcExplainForeignScan; */
	/* fdwroutine->AnalyzeForeignTable = ybcAnalyzeForeignTable; */
	/* fdwroutine->IsForeignScanParallelSafe = ybcIsForeignScanParallelSafe; */

	PG_RETURN_POINTER(fdwroutine);
}
