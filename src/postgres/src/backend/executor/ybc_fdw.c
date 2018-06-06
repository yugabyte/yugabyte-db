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
 * http: *www.apache.org/licenses/LICENSE-2.0
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

#include "executor/ybc_fdw.h"

/*  TODO see which includes of this block are still needed. */
#include "access/htup_details.h"
#include "access/reloptions.h"
#include "access/sysattr.h"
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

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"

/* ----------------------------------------------------------------------------- */
/*  Planner/Optimizer functions */

/*
 * ybcGetForeignRelSize
 *		Obtain relation size estimates for a foreign table
 */
static void
ybcGetForeignRelSize(PlannerInfo *root,
					 RelOptInfo *baserel,
					 Oid foreigntableid)
{
	/* Save the output-rows estimate for the planner */
	baserel->rows = 1000;
	/* TODO implement this. */
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
	Cost		startup_cost;
	Cost		total_cost;
	Cost		cpu_per_tuple;

	/* Estimate costs */
	startup_cost = baserel->baserestrictcost.startup;
	cpu_per_tuple = cpu_tuple_cost * 10 + baserel->baserestrictcost.per_tuple;
	total_cost = startup_cost + seq_page_cost * baserel->pages + cpu_per_tuple * baserel->rows;

	/* Create a ForeignPath node and add it as only possible path. */
	add_path(baserel, (Path *)
			 create_foreignscan_path(root, baserel,
									 NULL,	/* default pathtarget */
									 baserel->rows,
									 startup_cost,
									 total_cost,
									 NIL,	/* no pathkeys */
									 NULL,	/* no outer rel either */
									 NULL,	/* no extra plan */
									 NULL /* no options yet */ ));

	/*
	 * TODO there are some things we could send to the planner here, in
	 * particular the primary key filtering and row ordering invariants.
	 */
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
	Index		scan_relid = baserel->relid;

	/*
	 * TODO We should tell postgres which restriction clauses we actually
	 * support here. For now just put all the scan_clauses into the plan
	 * node's qual list for the executor to check.  So all we have to do here
	 * is strip RestrictInfo nodes from the clauses and ignore pseudoconstants
	 * (which will be handled elsewhere).
	 */
	scan_clauses = extract_actual_clauses(scan_clauses, false);

	/* Create the ForeignScan node */
	return make_foreignscan(tlist,
							scan_clauses,
							scan_relid,
							NIL,	/* no expressions to evaluate */
							best_path->fdw_private,
							NIL,	/* no custom tlist */
							NIL,	/* no remote quals */
							outer_plan);
}

/* ----------------------------------------------------------------------------- */
/*  Scanning functions */

/*
 * FDW-specific information for ForeignScanState.fdw_state.
 */
typedef struct YbcFdwExecutionState
{
	YBCPgStatement handle;
	/* The handle for the internal YB Select statement. */

	/* TODO this stores the bind output space for each column. */
	/* Should be fixed when the API for bind is changed. */
	int32		values[10];
	int16		num_values;

}			YbcFdwExecutionState;

/*
 * ybcBeginForeignScan
 *		Initiate access to the Yugabyte by allocating a Select handle.
 */
static void
ybcBeginForeignScan(ForeignScanState *node, int eflags)
{
	ForeignScan *foreignScan = (ForeignScan *) node->ss.ps.plan;
	Relation	relation = node->ss.ss_currentRelation;
	char	   *dbname = get_database_name(MyDatabaseId);
	char	   *schemaname = get_namespace_name(relation->rd_rel->relnamespace);
	char	   *tablename = NameStr(relation->rd_rel->relname);
	YbcFdwExecutionState *ybc_state;
	ListCell   *lc;

	/* Do nothing in EXPLAIN (no ANALYZE) case.  node->fdw_state stays NULL. */
	if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
		return;

	PG_TRY();
	{
		/* Allocate and initialize YB scan state. */
		ybc_state = (YbcFdwExecutionState *) palloc(sizeof(YbcFdwExecutionState));
		node->fdw_state = (void *) ybc_state;
		HandleYBStatus(YBCPgAllocSelect(ybc_pg_session,
										dbname,
										schemaname,
										tablename,
										&ybc_state->handle));

		/* Set WHERE clause values (currently only partition key). */
		RelationGetIndexAttrBitmap(relation, INDEX_ATTR_BITMAP_PRIMARY_KEY);
		foreach(lc, foreignScan->scan.plan.qual)
		{
			Expr	   *expr = (Expr *) lfirst(lc);

			if (expr->type == T_OpExpr)
			{
				OpExpr	   *opExpr = (OpExpr *) expr;

				if (opExpr->opno == Int4EqualOperator)
				{
					Var		   *col_desc = linitial(opExpr->args);
					Const	   *col_val = lsecond(opExpr->args);

					/* TODO distinguish partition key only. */
					if (bms_is_member(col_desc->varattno - FirstLowInvalidHeapAttributeNumber,
									  relation->rd_pkattr))
					{
						HandleYBStatus(YBCPgSelectSetColumnInt4(ybc_state->handle,
																col_desc->varattno,
																DatumGetInt32(col_val->constvalue)));
					}
				}
			}
		}

		/* Set scan targets. */
		/* TODO Fix the allocation here when the YB API for this is changed. */
		int16		idx = 0;

		foreach(lc, foreignScan->scan.plan.targetlist)
		{
			TargetEntry *tle = (TargetEntry *) lfirst(lc);

			HandleYBStatus(YBCPgSelectBindExprInt4(ybc_state->handle, tle->resno, &ybc_state->values[idx]));
			idx++;
		}
		ybc_state->num_values = idx;

		/* Execute the select statement. */
		HandleYBStatus(YBCPgExecSelect(ybc_state->handle));

	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_state->handle));
		ybc_state->handle = NULL;
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * ybcIterateForeignScan
 *		Read next record from the data file and store it into the
 *		ScanTupleSlot as a virtual tuple
 */
static TupleTableSlot *
ybcIterateForeignScan(ForeignScanState *node)
{
	TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
	YbcFdwExecutionState *ybc_state = (YbcFdwExecutionState *) node->fdw_state;
	int64		num_rows_fetched;

	/* Clear tuple slot before starting */
	ExecClearTuple(slot);
	MemSet(slot->tts_values, 0, ybc_state->num_values * sizeof(Datum));
	MemSet(slot->tts_isnull, true, ybc_state->num_values * sizeof(bool));

	PG_TRY();
	{
		/* Fetch some rows -- currently always 1 (or 0) */
		HandleYBStatus(YBCPgSelectFetch(ybc_state->handle, &num_rows_fetched));

		/* If we have result(s) update the tuple slot. */
		if (num_rows_fetched >= 1)
		{
			/*
			 * TODO (neil) Can remove this check if pggate handles
			 * pre-fetching.
			 */
			if (num_rows_fetched > 1)
			{
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("Internal Error: We do not yet support fetching more "
								"than one row, found %d", num_rows_fetched)));
			}

			for (int i = 0; i < ybc_state->num_values; i++)
			{
				slot->tts_isnull[i] = false;
				slot->tts_values[i] = Int32GetDatum(ybc_state->values[i]);
			}

			ExecStoreVirtualTuple(slot);
		}
	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_state->handle));
		PG_RE_THROW();
	}
	PG_END_TRY();

	return slot;
}

/*
 * fileReScanForeignScan
 *		Rescan table, possibly with new parameters
 */
static void
ybcReScanForeignScan(ForeignScanState *node)
{

	YbcFdwExecutionState *ybc_state = (YbcFdwExecutionState *) node->fdw_state;

	HandleYBStatus(YBCPgDeleteStatement(ybc_state->handle));
	ybc_state->handle = NULL;

	/* Re-allocate and execute the select. */
	char	   *dbname = get_database_name(MyDatabaseId);
	char	   *schemaname = get_namespace_name(node->ss.ss_currentRelation->rd_rel->relnamespace);
	char	   *tablename = NameStr(node->ss.ss_currentRelation->rd_rel->relname);

	PG_TRY();
	{
		HandleYBStatus(YBCPgAllocSelect(ybc_pg_session,
										dbname,
										schemaname,
										tablename,
										&ybc_state->handle));

		/* TODO Set (primary/partition key) values and execute. */
		HandleYBStatus(YBCPgExecSelect(ybc_state->handle));
	}
	PG_CATCH();
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_state->handle));
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * ybcEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan
 */
static void
ybcEndForeignScan(ForeignScanState *node)
{
	YbcFdwExecutionState *ybc_state = (YbcFdwExecutionState *) node->fdw_state;

	/* If yb_state is NULL, we are in EXPLAIN; nothing to do */
	if (ybc_state)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_state->handle));
	}
}

/* ----------------------------------------------------------------------------- */
/*  FDW declaration */

/*
 * Foreign-data wrapper handler function: return a struct with pointers
 * to YugaByte callback routines.
 */
Datum
ybc_fdw_handler()
{
	FdwRoutine *fdwroutine = makeNode(FdwRoutine);

	fdwroutine->GetForeignRelSize = ybcGetForeignRelSize;
	fdwroutine->GetForeignPaths = ybcGetForeignPaths;
	fdwroutine->GetForeignPlan = ybcGetForeignPlan;
	fdwroutine->BeginForeignScan = ybcBeginForeignScan;
	fdwroutine->IterateForeignScan = ybcIterateForeignScan;
	fdwroutine->ReScanForeignScan = ybcReScanForeignScan;
	fdwroutine->EndForeignScan = ybcEndForeignScan;

	/* TODO: These are optional but we should support them eventually. */
	/* fdwroutine->ExplainForeignScan = fileExplainForeignScan; */
	/* fdwroutine->AnalyzeForeignTable = ybcAnalyzeForeignTable; */
	/* fdwroutine->IsForeignScanParallelSafe = ybcIsForeignScanParallelSafe; */

	PG_RETURN_POINTER(fdwroutine);
}
