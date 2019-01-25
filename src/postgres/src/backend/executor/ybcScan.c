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
			scan_plan->primary_key = bms_add_member(scan_plan->primary_key, bms_idx);
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
	*is_ineq = (strcmp(opname, ">") == 0  ||
				strcmp(opname, ">=") == 0 ||
				strcmp(opname, "<") == 0  ||
				strcmp(opname, "<=") == 0 ||
				strcmp(opname, "<>") == 0);

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
					bool is_primary = bms_is_member(bms_idx, yb_plan->primary_key);
					bool is_hash    = bms_is_member(bms_idx, yb_plan->hash_key);

					/*
					 * TODO Once we support WHERE clause in pggate, these
					 * conditions need to be updated accordingly.
					 */
					if (is_hash && is_eq)
					{
						yb_plan->yb_cols   = bms_add_member(yb_plan->yb_cols, bms_idx);
						yb_plan->yb_hconds = lappend(yb_plan->yb_hconds, expr);
						return;
					}
					else if (is_primary && is_eq)
					{
						yb_plan->yb_cols   = bms_add_member(yb_plan->yb_cols, bms_idx);
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
static void ybcAddWhereCond(Expr *expr, YBCPgStatement yb_stmt, bool useIndex)
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
	if (useIndex)
	{
		HandleYBStatus(YBCPgDmlBindIndexColumn(yb_stmt, col_desc->varattno, ybc_expr));
	}
	else
	{
		HandleYBStatus(YBCPgDmlBindColumn(yb_stmt, col_desc->varattno, ybc_expr));
	}
}

YbScanState ybcBeginScan(Relation rel, Relation index, List *target_attrs, List *yb_conds)
{
	Oid         dboid     = YBCGetDatabaseOid(rel);
	Oid         relid     = RelationGetRelid(rel);
	Oid         index_id  = index ? RelationGetRelid(index) : InvalidOid;
	YbScanState ybc_state = NULL;
	ListCell    *lc;

	/* Allocate and initialize YB scan state. */
	ybc_state = (YbScanState) palloc0(sizeof(YbScanStateData));

	HandleYBStatus(YBCPgNewSelect(ybc_pg_session, dboid, relid, index_id, &ybc_state->handle,
								  NULL /* read_time */));
	ResourceOwnerEnlargeYugaByteStmts(CurrentResourceOwner);
	ResourceOwnerRememberYugaByteStmt(CurrentResourceOwner, ybc_state->handle);
	ybc_state->stmt_owner = CurrentResourceOwner;
	ybc_state->tupleDesc  = RelationGetDescr(rel);

	YbScanPlan ybc_plan = (YbScanPlan) palloc0(sizeof(YbScanPlanData));
	ybcLoadTableInfo(index ? index : rel, ybc_plan);

	foreach(lc, yb_conds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybClassifyWhereExpr(expr, IsYBRelation(rel), ybc_plan);
	}

	/*
	 * If hash key is not fully set, we must do a full-table scan in YugaByte
	 * and defer all filtering to Postgres.
	 * Else, if primary key is set only partially, we need to defer all range
	 * key conds after the first missing cond to Postgres.
	 */
	if (!bms_is_subset(ybc_plan->hash_key, ybc_plan->yb_cols))
	{
		ybc_plan->pg_conds  = list_concat(ybc_plan->pg_conds, ybc_plan->yb_hconds);
		ybc_plan->pg_conds  = list_concat(ybc_plan->pg_conds, ybc_plan->yb_rconds);
		ybc_plan->yb_hconds = NIL;
		ybc_plan->yb_rconds = NIL;
	}
	else
	{
		AttrNumber attnum;
		TupleDesc  tupleDesc = index ? RelationGetDescr(index) : RelationGetDescr(rel);

		/*
		 * TODO: We scan the range columns by increasing attribute number to look for the first
		 * missing condition. Currently, this works because there is a bug where the range columns
		 * of the primary key in YugaByte follows the same order as the attribute number. When the
		 * bug is fixed, this scan needs to be updated.
		 */
		for (attnum = 1; attnum <= tupleDesc->natts; attnum++)
		{
			int  bms_idx = attnum - FirstLowInvalidHeapAttributeNumber;
			if ( bms_is_member(bms_idx, ybc_plan->primary_key) &&
				!bms_is_member(bms_idx, ybc_plan->yb_cols))
			{
				/* Move the rest of the range key conds to Postgres */
				for (attnum = attnum + 1; attnum <= tupleDesc->natts; attnum++)
				{
					bms_idx = attnum - FirstLowInvalidHeapAttributeNumber;
					if (!bms_is_member(bms_idx, ybc_plan->primary_key))
						continue;

					ListCell   *prev = NULL;
					ListCell   *next = NULL;
					for (lc = list_head(ybc_plan->yb_rconds); lc; lc = next)
					{
						next = lnext(lc);

						OpExpr *opExpr = (OpExpr *) lfirst(lc);
						Expr   *left  = linitial(opExpr->args);
						Expr   *right = lsecond(opExpr->args);
						Var    *col_desc = IsA(left, Var) ? (Var *) left : (Var *) right;

						if (col_desc->varattno == attnum)
						{
							ybc_plan->yb_rconds = list_delete_cell(ybc_plan->yb_rconds, lc, prev);
							lappend(ybc_plan->pg_conds, opExpr);
						}
						else
							prev = lc;
					}
				}
				break;
			}
		}
	}

	/* Set WHERE clause values (currently only primary key). */
	bool useIndex = (index != NULL);
	foreach(lc, ybc_plan->yb_hconds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybcAddWhereCond(expr, ybc_state->handle, useIndex);
	}
	foreach(lc, ybc_plan->yb_rconds)
	{
		Expr *expr = (Expr *) lfirst(lc);
		ybcAddWhereCond(expr, ybc_state->handle, useIndex);
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

	/*
	 * Set the current syscatalog version (will check that we are up to date).
	 * Avoid it for syscatalog tables so that we can still use this for
	 * refreshing the caches when we are behind.
	 * Note: This works because we do not allow modifying schemas (alter/drop)
	 * for system catalog tables.
	 */
	if (!IsSystemRelation(rel))
	{
		HandleYBStmtStatusWithOwner(YBCPgSetCatalogCacheVersion(ybc_state->handle,
		                                                        ybc_catalog_cache_version),
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
		if (syscols.ybctid != NULL)
		{
			tuple->t_ybctid = PointerGetDatum(syscols.ybctid);
		}
		if (syscols.ybbasectid != NULL)
		{
			tuple->t_ybctid = PointerGetDatum(syscols.ybbasectid);
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
