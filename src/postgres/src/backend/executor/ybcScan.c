/*--------------------------------------------------------------------------------------------------
 *
 * ybcScan.c
 *	  Utilities for YugaByte scan.
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
 * src/backend/executor/ybcScan.c
 *
 *--------------------------------------------------------------------------------------------------
 *
 * This is meant to a be a common api betwen regular scan path (ybc_fdw.c)
 * and syscatalog scan path (ybcam.c).
 * TODO currently this is only used by syscatalog scan path, ybc_fdw needs to
 * be refactored.
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "commands/dbcommands.h"
#include "catalog/catalog.h"
#include "catalog/pg_database.h"
#include "catalog/pg_operator.h"
#include "executor/ybcScan.h"
#include "miscadmin.h"
#include "nodes/relation.h"
#include "utils/rel.h"
#include "utils/lsyscache.h"
#include "utils/resowner_private.h"
#include "utils/syscache.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "pg_yb_utils.h"


void ybcFreeScanState(YbScanState ybc_state)
{
	/* If yb_fdw_exec_state is NULL, we are in EXPLAIN; nothing to do */
	if (ybc_state != NULL && ybc_state->handle != NULL)
	{
		HandleYBStatus(YBCPgDeleteStatement(ybc_state->handle));
		ResourceOwnerForgetYugaByteStmt(ybc_state->stmt_owner,
		                                ybc_state->handle);
		ybc_state->handle = NULL;
		ybc_state->stmt_owner = NULL;
		ybc_state->tupleDesc = NULL;
	}
}

/*
 * Get YugaByte-specific table metadata and load it into the scan_plan.
 * Currently only the hash and primary key info.
 */
static void ybcLoadTableInfo(Relation rel, YbScanPlan scan_plan)
{
	Oid            dboid          = YBCGetDatabaseOid(rel);
	Oid            relid          = RelationGetRelid(rel);
	YBCPgTableDesc ybc_table_desc = NULL;

	HandleYBStatus(YBCPgGetTableDesc(ybc_pg_session, dboid, relid, &ybc_table_desc));

	for (AttrNumber attrNum = 1; attrNum <= rel->rd_att->natts; attrNum++)
	{
		bool is_primary = false;
		bool is_hash    = false;
		HandleYBTableDescStatus(YBCPgGetColumnInfo(ybc_table_desc,
		                                           attrNum,
		                                           &is_primary,
		                                           &is_hash), ybc_table_desc);
		int bms_idx = attrNum - FirstLowInvalidHeapAttributeNumber;
		if (is_hash)
		{
			scan_plan->hash_key = bms_add_member(scan_plan->hash_key, bms_idx);
		}
		if (is_primary)
		{
			scan_plan->primary_key = bms_add_member(scan_plan->primary_key,
			                                       bms_idx);
		}
	}
	HandleYBStatus(YBCPgDeleteTableDesc(ybc_table_desc));
	ybc_table_desc = NULL;
}

static void analyzeOperator(OpExpr* opExpr, bool *is_eq, bool *is_ineq)
{
	Form_pg_operator form;
	*is_eq = false;
	*is_ineq = false;

	if (opExpr->opno == InvalidOid)
	{
		return;
	}
	else if (opExpr->opno == Int4EqualOperator)
	{
		*is_eq = true;
		return;
	}

	HeapTuple tuple = SearchSysCache1(OPEROID, ObjectIdGetDatum(opExpr->opno));
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
		        (errcode(ERRCODE_INTERNAL_ERROR), errmsg(
				        "cache lookup failed for operator %u",
				        opExpr->opno)));
	form = (Form_pg_operator) GETSTRUCT(tuple);
	char *opname = NameStr(form->oprname);
	*is_eq   = strcmp(opname, "=") == 0;
	/* Note: the != operator is converted to <> in the parser stage */
	*is_ineq = strcmp(opname, ">") == 0 ||
	               strcmp(opname, ">=") == 0 ||
	               strcmp(opname, "<") == 0 ||
	               strcmp(opname, "<=") == 0 ||
	               strcmp(opname, "<>") == 0;

	ReleaseSysCache(tuple);
}

/*
 * Analyze a where expression to classify it as either supported by YugaByte
 * (i.e. will get pushed down) or not (i.e. will get evaluated/filtered by
 * Postgres).
 */
static void ybClassifyWhereExpr(Expr *expr, bool is_supported_rel, YbScanPlan yb_plan)
{
	/* YugaByte only supports base relations (e.g. no joins or child rels) */
	if (is_supported_rel)
	{

		/* YugaByte only supports operator expressions (e.g. no functions) */
		if (IsA(expr, OpExpr))
		{
			OpExpr *opExpr = (OpExpr *) expr;

			/* Get operator info */
			bool is_eq = false;
			bool is_ineq = false;
			analyzeOperator(opExpr, &is_eq, &is_ineq);

			/* Currently, YugaByte only supports comparison operators. */
			if (is_eq || is_ineq)
			{
				/* Supported operators ensure there are exactly two arguments */
				Expr *left  = linitial(opExpr->args);
				Expr *right = lsecond(opExpr->args);

				/*
				 * Currently, YugaByte only supports conditions of the form
				 * '<col> <op> <value>' or '<value> <op> <col>' at this point.
				 * Note: Postgres should have already evaluated expressions
				 * with no column refs before this point.
				 */
				if ((IsA(left, Var) && IsA(right, Const)) ||
				    (IsA(left, Const) && IsA(right, Var)))
				{
					AttrNumber attnum = IsA(left, Var) ? ((Var *) left)->varattno
					                                  : ((Var *) right)->varattno;

					int  bms_idx = attnum - FirstLowInvalidHeapAttributeNumber;
					bool is_primary = bms_is_member(bms_idx,
					                                yb_plan->primary_key);
					bool is_hash = bms_is_member(bms_idx, yb_plan->hash_key);

					/*
					 * TODO Once we support WHERE clause in pggate, these
					 * conditions need to be updated accordingly.
					 */
					if (is_hash && is_eq)
					{
						yb_plan->yb_cols   = bms_add_member(yb_plan->yb_cols,
						                                    bms_idx);
						yb_plan->yb_hconds = lappend(yb_plan->yb_hconds, expr);
						return;
					}
					else if (is_primary && is_eq)
					{
						yb_plan->yb_cols   = bms_add_member(yb_plan->yb_cols,
						                                    bms_idx);
						yb_plan->yb_rconds = lappend(yb_plan->yb_rconds, expr);
						return;
					}
				}
			}
		}
	}

	/* Otherwise let postgres handle the condition (default) */
	yb_plan->pg_conds = lappend(yb_plan->pg_conds, expr);
}

/*
 * Add a Postgres expression as a where condition to a YugaByte select
 * statement. Assumes the expression can be evaluated by YugaByte
 * (i.e. ybcIsYbExpression returns true).
 */
static void ybcAddWhereCond(Expr *expr, YBCPgStatement yb_stmt)
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

YbScanState ybcBeginScan(Relation rel, List *target_attrs, List *yb_conds)
{
	Oid         dboid     = YBCGetDatabaseOid(rel);
	Oid         relid     = RelationGetRelid(rel);
	YbScanState ybc_state = NULL;
	ListCell    *lc;

	/* Allocate and initialize YB scan state. */
	ybc_state = (YbScanState) palloc0(sizeof(YbScanStateData));

	HandleYBStatus(YBCPgNewSelect(
	    ybc_pg_session, dboid, relid, &ybc_state->handle, NULL /* read_time */));
	ResourceOwnerEnlargeYugaByteStmts(CurrentResourceOwner);
	ResourceOwnerRememberYugaByteStmt(CurrentResourceOwner, ybc_state->handle);
	ybc_state->stmt_owner = CurrentResourceOwner;
	ybc_state->tupleDesc  = RelationGetDescr(rel);

	YbScanPlan ybc_plan = (YbScanPlan) palloc0(sizeof(YbScanPlanData));
	ybcLoadTableInfo(rel, ybc_plan);

	foreach(lc, yb_conds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybClassifyWhereExpr(expr, IsYBRelation(rel), ybc_plan);
	}

	/*
	 * If hash key is not fully set, we must do a full-table scan in YugaByte
	 * and defer all filtering to Postgres.
	 * Else, if primary key is not fully set we need to remove all range
	 * key conds and defer filtering for range column conds to Postgres.
	 */
	if (!bms_is_subset(ybc_plan->hash_key, ybc_plan->yb_cols))
	{
		ybc_plan->pg_conds  = list_concat(ybc_plan->pg_conds,
		                                  ybc_plan->yb_hconds);
		ybc_plan->pg_conds  = list_concat(ybc_plan->pg_conds,
		                                  ybc_plan->yb_rconds);
		ybc_plan->yb_hconds = NIL;
		ybc_plan->yb_rconds = NIL;
	}
	else if (!bms_is_subset(ybc_plan->primary_key, ybc_plan->yb_cols))
	{
		ybc_plan->pg_conds  = list_concat(ybc_plan->pg_conds,
		                                  ybc_plan->yb_rconds);
		ybc_plan->yb_rconds = NIL;
	}

	/* Set WHERE clause values (currently only primary key). */
	foreach(lc, ybc_plan->yb_hconds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybcAddWhereCond(expr, ybc_state->handle);
	}
	foreach(lc, ybc_plan->yb_rconds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybcAddWhereCond(expr, ybc_state->handle);
	}

	/* Set scan targets. */
	foreach(lc, target_attrs)
	{
		TargetEntry *target = (TargetEntry *) lfirst(lc);

		/* Regular (non-system) attribute. */
		Oid attr_typid = InvalidOid;
		int32 attr_typmod = 0;
		if (target->resno > 0)
		{
			Form_pg_attribute attr;
			attr = ybc_state->tupleDesc->attrs[target->resno - 1];
			/* Ignore dropped attributes */
			if (attr->attisdropped)
			{
				continue;
			}
			attr_typid = attr->atttypid;
			attr_typmod = attr->atttypmod;
		}

		YBCPgTypeAttrs type_attrs = { attr_typmod };
		YBCPgExpr expr = YBCNewColumnRef(ybc_state->handle, target->resno, attr_typid, &type_attrs);
		HandleYBStmtStatusWithOwner(YBCPgDmlAppendTarget(ybc_state->handle, expr),
		                            ybc_state->handle,
		                            ybc_state->stmt_owner);
	}

	/* Execute the select statement. */
	HandleYBStmtStatusWithOwner(YBCPgExecSelect(ybc_state->handle),
	                            ybc_state->handle,
	                            ybc_state->stmt_owner);
	return ybc_state;
}

HeapTuple ybcFetchNext(YbScanState ybc_state)
{
	HeapTuple tuple    = NULL;
	bool      has_data = false;
	TupleDesc tupdesc  = ybc_state->tupleDesc;

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

	if (has_data)
	{
		tuple = heap_form_tuple(tupdesc, values, nulls);

		if (syscols.oid != InvalidOid)
		{
			HeapTupleSetOid(tuple, syscols.oid);
		}
	}
	pfree(values);
	pfree(nulls);

	return tuple;
}

extern void ybcEndScan(YbScanState ybc_state)
{
	ybcFreeScanState(ybc_state);
}
