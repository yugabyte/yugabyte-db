/*-------------------------------------------------------------------------
 *
 * yb_target.c
 *	  Functions for configuring targets and column references
 *	  on a YbcPgStatement.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/backend/access/common/yb_target.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/yb_target.h"
#include "catalog/yb_type.h"
#include "executor/ybExpr.h"
#include "pg_yb_utils.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "ybgate/ybgate_api.h"

/*
 * Utility method to bind const to column.
 */
void
YbBindDatumToColumn(YbcPgStatement stmt,
					int attr_num,
					Oid type_id,
					Oid collation_id,
					Datum datum,
					bool is_null,
					const YbcPgTypeEntity *null_type_entity)
{
	YbcPgExpr	expr;
	const YbcPgTypeEntity *type_entity;

	if (is_null && null_type_entity)
		type_entity = null_type_entity;
	else
		type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);

	YbcPgCollationInfo collation_info;

	YBGetCollationInfo(collation_id, type_entity, datum, is_null,
					   &collation_info);

	HandleYBStatus(YBCPgNewConstant(stmt, type_entity,
									collation_info.collate_is_valid_non_c,
									collation_info.sortkey,
									datum, is_null, &expr));

	HandleYBStatus(YBCPgDmlBindColumn(stmt, attr_num, expr));
}

static void
YbDmlAppendTargetImpl(YbcPgStatement handle, AttrNumber attnum, Oid typid, Oid collation, Oid typmod)
{
	const YbcPgTypeAttrs type_attrs = {.typmod = typmod};

	HandleYBStatus(YBCPgDmlAppendTarget(handle,
										YBCNewColumnRef(handle, attnum, typid,
														collation, &type_attrs),
										false /* is_for_secondary_index */ ));
}

/*
 * Add a system column as target to the given statement handle.
 */
void
YbDmlAppendTargetSystem(AttrNumber attnum, YbcPgStatement handle)
{
	Assert(attnum < 0);
	YbDmlAppendTargetImpl(handle, attnum, InvalidOid /* typid */ ,
						  InvalidOid /* collation */ , -1 /* typmod */ );
}

void
YbDmlAppendTargetRegularAttr(const FormData_pg_attribute *attr,
							 YbcPgStatement handle)
{
	Assert(attr->attnum > 0);
	Assert(!attr->attisdropped);

	YbDmlAppendTargetImpl(handle, attr->attnum, attr->atttypid,
						  attr->attcollation, attr->atttypmod);

}

/*
 * Add a regular column as target to the given statement handle.
 * Assume tupdesc's relation is the same as handle's target relation.
 */
void
YbDmlAppendTargetRegular(TupleDesc tupdesc, AttrNumber attnum,
						 YbcPgStatement handle)
{
	Assert(attnum > 0);
	YbDmlAppendTargetRegularAttr(TupleDescAttr(tupdesc, attnum - 1), handle);
}

/*
 * Set aggregate targets into handle.  If index is not null, convert column
 * attribute numbers from table-based numbers to index-based ones.
 */
void
YbDmlAppendTargetsAggregate(List *aggrefs, Scan *outer_plan,
							TupleDesc tupdesc, Relation index,
							bool xs_want_itup, YbcPgStatement handle)
{
	ListCell   *lc;

	/* Set aggregate scan targets. */
	foreach(lc, aggrefs)
	{
		Aggref	   *aggref = lfirst_node(Aggref, lc);
		char	   *func_name = get_func_name(aggref->aggfnoid);
		ListCell   *lc_arg;
		YbcPgExpr	op_handle;
		const YbcPgTypeEntity *type_entity;

		/* Get type entity for the operator from the aggref. */
		type_entity = YbDataTypeFromOidMod(InvalidAttrNumber,
										   aggref->aggtranstype);

		/* Create operator. */
		HandleYBStatus(YBCPgNewOperator(handle, func_name, type_entity,
										aggref->aggcollid, &op_handle));

		/* Handle arguments. */
		if (aggref->aggstar)
		{
			/*
			 * Add dummy argument for COUNT(*) case, turning it into COUNT(0).
			 * We don't use a column reference as we want to count rows
			 * even if all column values are NULL.
			 */
			YbcPgExpr	const_handle;

			HandleYBStatus(YBCPgNewConstant(handle,
											type_entity,
											false /* collate_is_valid_non_c */ ,
											NULL /* collation_sortkey */ ,
											0 /* datum */ ,
											false /* is_null */ ,
											&const_handle));
			HandleYBStatus(YBCPgOperatorAppendArg(op_handle, const_handle));
		}
		else
		{
			/* Add aggregate arguments to operator. */
			foreach(lc_arg, aggref->args)
			{
				TargetEntry *tle = lfirst_node(TargetEntry, lc_arg);

				if (IsA(tle->expr, Const))
				{
					Const	   *const_node = castNode(Const, tle->expr);

					/* Already checked by yb_agg_pushdown_supported */
					Assert(const_node->constisnull || const_node->constbyval);

					YbcPgExpr	const_handle;

					HandleYBStatus(YBCPgNewConstant(handle,
													type_entity,
													false /* collate_is_valid_non_c */ ,
													NULL /* collation_sortkey */ ,
													const_node->constvalue,
													const_node->constisnull,
													&const_handle));
					HandleYBStatus(YBCPgOperatorAppendArg(op_handle,
														  const_handle));
				}
				else if (IsA(tle->expr, Var))
				{
					Var		   *var = castNode(Var, tle->expr);
					int			attno = var->varattno;

					/*
					 * Change column reference in an aggregate to attribute
					 * number. Given limited number of cases we support, we
					 * take a number of assumptions here: the outer plan is a
					 * plain Scan, and the scan's target list contains only
					 * simple Vars.
					 * Support for more generic plan shapes would require
					 * deep rework of Postgres/PgGate interactions.
					 */
					if (outer_plan)
					{
						List	   *tlist = outer_plan->plan.targetlist;

						Assert(var->varno == OUTER_VAR);
						Assert(attno > 0);
						Assert(attno <= list_length(tlist));
						TargetEntry *scan_tle = list_nth_node(TargetEntry, tlist, attno - 1);

						Assert(IsA(scan_tle->expr, Var));
						attno = castNode(Var, scan_tle->expr)->varattno;
					}
					Form_pg_attribute attr = TupleDescAttr(tupdesc, attno - 1);
					YbcPgTypeAttrs type_attrs = {attr->atttypmod};

					YbcPgExpr	arg = YBCNewColumnRef(handle,
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
		HandleYBStatus(YBCPgDmlAppendTarget(handle, op_handle,
											false /* is_for_secondary_index */ ));
	}
}

/*
 * YbDmlAppendTargets
 *
 * Add targets to the statement.  The colref list is expected to be made up of
 * YbExprColrefDesc nodes.  Unlike YbDmlAppendTargetRegular, it does not do any
 * dropped-columns checking.
 */
void
YbDmlAppendTargets(List *colrefs, YbcPgStatement handle)
{
	ListCell   *lc;
	YbcPgExpr	expr;
	YbcPgTypeAttrs type_attrs;
	YbExprColrefDesc *colref;

	foreach(lc, colrefs)
	{
		colref = lfirst_node(YbExprColrefDesc, lc);
		type_attrs.typmod = colref->typmod;
		expr = YBCNewColumnRef(handle,
							   colref->attno,
							   colref->typid,
							   colref->collid,
							   &type_attrs);
		HandleYBStatus(YBCPgDmlAppendTarget(handle, expr, false /* is_for_secondary_index */ ));
	}
}

void
YbAppendPrimaryColumnRef(YbcPgStatement dml, YbcPgExpr colref)
{
	HandleYBStatus(YbPgDmlAppendColumnRef(dml, colref,
										  false /* is_for_secondary_index */ ));
}

/*
 * YbDmlAppendColumnRefsImpl
 *
 * Add the list of column references used by pushed down expressions to the
 * statement.
 * The colref list is expected to be the list of YbExprColrefDesc nodes.
 */
static void
YbAppendColumnRefsImpl(YbcPgStatement dml, List *colrefs,
					   bool is_for_secondary_index)
{
	ListCell   *lc;

	foreach(lc, colrefs)
	{
		YbExprColrefDesc *param = lfirst_node(YbExprColrefDesc, lc);
		YbcPgTypeAttrs type_attrs = {param->typmod};

		HandleYBStatus(YbPgDmlAppendColumnRef(dml,
											  YBCNewColumnRef(dml,
															  param->attno,
															  param->typid,
															  param->collid,
															  &type_attrs),
											  is_for_secondary_index));
	}
}

void
YbAppendPrimaryColumnRefs(YbcPgStatement dml, List *colrefs)
{
	YbAppendColumnRefsImpl(dml, colrefs, false /* is_for_secondary_index */ );
}

static void
YbApplyPushdownImpl(YbcPgStatement dml, const YbPushdownExprs *pushdown,
					bool is_for_secondary_index)
{
	if (!pushdown)
		return;

	YbAppendColumnRefsImpl(dml, pushdown->colrefs, is_for_secondary_index);
	const uint32_t serialization_version = yb_major_version_upgrade_compatibility > 0
		? yb_major_version_upgrade_compatibility : YbgGetPgVersion();

	ListCell   *lc;

	foreach(lc, pushdown->quals)
	{
		Expr	   *expr = lfirst(lc);

		HandleYBStatus(YbPgDmlAppendQual(dml, YBCNewEvalExprCall(dml, expr), serialization_version,
										 is_for_secondary_index));
	}
}

void
YbApplyPrimaryPushdown(YbcPgStatement dml, const YbPushdownExprs *pushdown)
{
	YbApplyPushdownImpl(dml, pushdown, false /* is_for_secondary_index */ );
}

void
YbApplySecondaryIndexPushdown(YbcPgStatement dml, const YbPushdownExprs *pushdown)
{
	YbApplyPushdownImpl(dml, pushdown, true /* is_for_secondary_index */ );
}

Oid
ybc_get_attcollation(TupleDesc desc, AttrNumber attnum)
{
	return (attnum > 0 ?
			TupleDescAttr(desc, attnum - 1)->attcollation :
			InvalidOid);
}
