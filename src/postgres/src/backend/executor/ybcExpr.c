/*--------------------------------------------------------------------------------------------------
 * ybcExpr.c
 *        Routines to construct YBC expression tree.
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
 *        src/backend/executor/ybcExpr.c
 *--------------------------------------------------------------------------------------------------
 */

#include <inttypes.h>

#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "catalog/pg_proc.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "utils/datum.h"
#include "utils/relcache.h"
#include "utils/rel.h"
#include "parser/parse_type.h"
#include "utils/lsyscache.h"
#include "commands/dbcommands.h"
#include "executor/executor.h"
#include "executor/nodeSubplan.h"
#include "executor/tuptable.h"
#include "miscadmin.h"
#include "utils/syscache.h"
#include "utils/builtins.h"

#include "pg_yb_utils.h"
#include "executor/ybcExpr.h"
#include "catalog/yb_type.h"

Node *yb_expr_instantiate_params_mutator(Node *node, EState *estate);
bool yb_pushdown_walker(Node *node, List **colrefs);
bool yb_can_pushdown_func(Oid funcid);

YBCPgExpr YBCNewColumnRef(YBCPgStatement ybc_stmt, int16_t attr_num,
						  int attr_typid, int attr_collation,
						  const YBCPgTypeAttrs *type_attrs) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *type_entity = YbDataTypeFromOidMod(attr_num, attr_typid);
	YBCPgCollationInfo collation_info;
	YBGetCollationInfo(attr_collation, type_entity, 0 /* datum */, true /* is_null */,
					   &collation_info);
	HandleYBStatus(YBCPgNewColumnRef(ybc_stmt, attr_num, type_entity,
									 collation_info.collate_is_valid_non_c,
									 type_attrs, &expr));
	return expr;
}

YBCPgExpr YBCNewConstant(YBCPgStatement ybc_stmt, Oid type_id, Oid collation_id,
						 Datum datum, bool is_null) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);
	YBCPgCollationInfo collation_info;
	YBGetCollationInfo(collation_id, type_entity, datum, is_null, &collation_info);
	HandleYBStatus(YBCPgNewConstant(ybc_stmt, type_entity,
									collation_info.collate_is_valid_non_c,
									collation_info.sortkey,
									datum, is_null, &expr));
	return expr;
}

YBCPgExpr YBCNewConstantVirtual(YBCPgStatement ybc_stmt, Oid type_id, YBCPgDatumKind kind) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *type_entity = YbDataTypeFromOidMod(InvalidAttrNumber, type_id);
	HandleYBStatus(YBCPgNewConstantVirtual(ybc_stmt, type_entity, kind, &expr));
	return expr;
}

YBCPgExpr YBCNewTupleExpr(YBCPgStatement ybc_stmt,
						  const YBCPgTypeAttrs *type_attrs, int num_elems, YBCPgExpr *elems) {
	YBCPgExpr expr = NULL;
	const YBCPgTypeEntity *tuple_type_entity = YBCPgFindTypeEntity(RECORDOID);
	HandleYBStatus(YBCPgNewTupleExpr(ybc_stmt, tuple_type_entity, type_attrs,
									 num_elems, elems, &expr));
	return expr;
}

/*
 * yb_expr_instantiate_params_mutator
 *
 *	  Expression mutator used internally by YbExprInstantiateParams
 */
Node *yb_expr_instantiate_params_mutator(Node *node, EState *estate)
{
	if (node == NULL)
		return NULL;

	if (IsA(node, Param))
	{
		Param	   *param = castNode(Param, node);
		Datum		pval = 0;
		bool		pnull = true;
		int16		typLen = 0;
		bool		typByVal = false;
		if (param->paramkind == PARAM_EXEC)
		{
			ParamExecData *prm = &(estate->es_param_exec_vals[param->paramid]);
			if (prm->execPlan != NULL)
			{
				/* Parameter not evaluated yet, so go do it */
				ExecSetParamPlan(prm->execPlan, GetPerTupleExprContext(estate));
				/* ExecSetParamPlan should have processed this param... */
				Assert(prm->execPlan == NULL);
			}
			pval = prm->value;
			pnull = prm->isnull;
		}
		else
		{
			ParamExternData *prm = NULL;
			ParamExternData prmdata;
			if (estate->es_param_list_info->paramFetch != NULL)
				prm = estate->es_param_list_info->paramFetch(estate->es_param_list_info,
															 param->paramid,
															 true, &prmdata);
			else
				prm = &estate->es_param_list_info->params[param->paramid - 1];

			Assert(OidIsValid(prm->ptype) && prm->ptype == param->paramtype);
			pval = prm->value;
			pnull = prm->isnull;
		}
		get_typlenbyval(param->paramtype, &typLen, &typByVal);
		/*
		 * If parameter is by reference, make a copy in the current memory
		 * context
		 */
		if (!pnull && !typByVal)
			pval = datumCopy(pval, typByVal, typLen);

		return (Node *) makeConst(param->paramtype,
								  param->paramtypmod,
								  param->paramcollid,
								  (int) typLen,
								  pval,
								  pnull,
								  typByVal);
	}
	return expression_tree_mutator(node,
								   yb_expr_instantiate_params_mutator,
								   (void *) estate);
}

/*
 * YbExprInstantiateParams
 *
 *	  Replace the Param nodes of the expression tree with Const nodes carrying
 *	  current parameter values before pushing the expression down to DocDB
 */
Expr *YbExprInstantiateParams(Expr* expr, EState *estate)
{
	/* Fast-path if there are no params. */
	if (estate->es_param_list_info == NULL &&
		estate->es_param_exec_vals == NULL)
			return expr;

	/*
	 * This does not follow common pattern of mutator invocation due to the
	 * corner case when expr is a bare T_Param node. The expression_tree_mutator
	 * just makes a copy of primitive nodes without running the mutator function
	 * on them. So here we run the mutator to make sure the bare T_Param is
	 * getting replaced, and if the expr is anything else, it will be properly
	 * forwarded to the expression_tree_mutator.
	 */
	return (Expr *) yb_expr_instantiate_params_mutator((Node *) expr, estate);
}

/*
 * YbInstantiatePushdownParams
 *	  Replace the Param nodes of the expression trees with Const nodes carrying
 *	  current parameter values before pushing the expression down to DocDB.
 */
PushdownExprs *
YbInstantiatePushdownParams(PushdownExprs *pushdown, EState *estate)
{
	PushdownExprs *result;
	if (pushdown->quals == NIL)
		return NULL;
	/* Make new instance for the scan state. */
	result = (PushdownExprs *) palloc(sizeof(PushdownExprs));
	/* Store mutated list of expressions. */
	result->quals = (List *)
		YbExprInstantiateParams((Expr *) pushdown->quals, estate);
	/*
	 * Column references are not modified by the executor, so it is OK to copy
	 * the reference.
	 */
	result->colrefs = pushdown->colrefs;
	return result;
}


/*
 * yb_can_pushdown_func
 *
 *	  Determine if the function can be pushed down to DocDB
 *	  Since catalog access is not currently available in DocDB, only built in
 *	  functions are pushable. The lack of catalog access imposes also other
 *	  limitations:
 *	   - Only immutable functions are pushable. Stable and volatile functions
 *	     are permitted to access the catalog;
 *	   - DocDB must support conversion of parameter and result values between
 *	     DocDB and Postgres formats, so there should be conversion functions;
 *	   - Typically functions with polymorfic parameters or result need catalog
 *	     access to determine runtime data types, so they are not pushed down.
 */
bool yb_can_pushdown_func(Oid funcid)
{
	HeapTuple		tuple;
	Form_pg_proc	pg_proc;
	bool			result;

	/* Quick check if the function is builtin */
	if (!is_builtin_func(funcid))
	{
		return false;
	}

	/*
	 * Check whether this function is on a list of hand-picked functions
	 * safe for pushdown.
	 */
	for (int i = 0; i < yb_funcs_safe_for_pushdown_count; ++i)
	{
		if (funcid == yb_funcs_safe_for_pushdown[i])
			return true;
	}

	/*
	 * Check whether this function is on a list of hand-picked functions
	 * that cannot be pushed down.
	 */
	for (int i = 0; i < yb_funcs_unsafe_for_pushdown_count; ++i)
	{
		if (funcid == yb_funcs_unsafe_for_pushdown[i])
			return false;
	}

	/* Examine misc function attributes that may affect pushability */
	tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for function %u", funcid);
	pg_proc = (Form_pg_proc) GETSTRUCT(tuple);
	result = true;
	if (pg_proc->provolatile != PROVOLATILE_IMMUTABLE)
	{
		result = false;
	}
	if (result &&
		(!YBCPgFindTypeEntity(pg_proc->prorettype) ||
		 IsPolymorphicType(pg_proc->prorettype)))
	{
		result = false;
	}
	if (result)
	{
		for (int i = 0; i < pg_proc->pronargs; i++)
		{
			Oid typid = pg_proc->proargtypes.values[i];
			if (!YBCPgFindTypeEntity(typid) || IsPolymorphicType(typid))
			{
				result = false;
				break;
			}
		}
	}
	ReleaseSysCache(tuple);
	return result;
}

/*
 * yb_pushdown_walker
 *
 *	  Expression walker used internally by YbCanPushdownExpr
 */
bool yb_pushdown_walker(Node *node, List **colrefs)
{
	if (node == NULL)
		return false;
	switch (node->type)
	{
		case T_Var:
		{
			Var		   *var_expr = castNode(Var, node);
			AttrNumber	attno = var_expr->varattno;
			/* DocDB is not aware of Postgres virtual attributes */
			if (!AttrNumberIsForUserDefinedAttr(attno))
				return true;
			/* Need to convert values between DocDB and Postgres formats */
			if (!YBCPgFindTypeEntity(var_expr->vartype))
				return true;
			/* Collect column reference */
			if (colrefs)
			{
				ListCell   *lc;
				bool		found = false;

				/* Check if the column reference has already been collected */
				foreach(lc, *colrefs)
				{
					YbExprColrefDesc *param = (YbExprColrefDesc *) lfirst(lc);
					if (param->attno == attno)
					{
						found = true;
						break;
					}
				}

				if (!found)
				{
					/* Add new column reference to the list */
					YbExprColrefDesc *new_param = makeNode(YbExprColrefDesc);
					new_param->attno = attno;
					new_param->typid = var_expr->vartype;
					new_param->typmod = var_expr->vartypmod;
					new_param->collid = var_expr->varcollid;
					*colrefs = lappend(*colrefs, new_param);
				}
			}
			break;
		}
		case T_FuncExpr:
		{
			FuncExpr *func_expr = castNode(FuncExpr, node);
			/* DocDB executor does not expand variadic argument */
			if (func_expr->funcvariadic)
				return true;
			/*
			 * Unsafe to pushdown function if collation is not C, there may be
			 * needed metadata lookup for collation details.
			 */
			if (YBIsCollationValidNonC(func_expr->inputcollid))
				return true;
			/* Check if the function is pushable */
			if (!yb_can_pushdown_func(func_expr->funcid))
				return true;
			break;
		}
		case T_OpExpr:
		{
			OpExpr *op_expr = castNode(OpExpr, node);
			/*
			 * Unsafe to pushdown function if collation is not C, there may be
			 * needed metadata lookup for collation details.
			 */
			if (YBIsCollationValidNonC(op_expr->inputcollid))
				return true;
			if (!yb_can_pushdown_func(op_expr->opfuncid))
				return true;
			break;
		}
		case T_ScalarArrayOpExpr:
		{
			ScalarArrayOpExpr *saop_expr = castNode(ScalarArrayOpExpr, node);
			if (!yb_enable_saop_pushdown)
				return true;
			/*
			 * Unsafe to pushdown function if collation is not C, there may be
			 * needed metadata lookup for collation details.
			 */
			if (YBIsCollationValidNonC(saop_expr->inputcollid))
				return true;
			if (!yb_can_pushdown_func(saop_expr->opfuncid))
				return true;
			if (list_length(saop_expr->args) == 2)
			{
				/* Check if DocDB can deconstruct the array */
				Oid elmtype = get_element_type(exprType(lsecond(saop_expr->args)));
				int16_t elmlen;
				bool elmbyval;
				char elmalign;
				if (!YbTypeDetails(elmtype, &elmlen, &elmbyval, &elmalign))
					return true;
			}
			else
				return true;
			break;
		}
		case T_CaseExpr:
		{
			CaseExpr *case_expr = castNode(CaseExpr, node);
			/*
			 * Support for implicit equality comparison would require catalog
			 * lookup to find equality operation for the argument data type.
			 */
			if (case_expr->arg)
				return true;
			break;
		}
		case T_Param:
		{
			Param *p = castNode(Param, node);
			if (!YBCPgFindTypeEntity(p->paramtype))
				return true;
			break;
		}
		case T_Const:
		{
			Const *c = castNode(Const, node);
			/*
			* Constant value may need to be converted to DocDB format, but
			* DocDB does not support arbitrary types.
			*/
			if (!YBCPgFindTypeEntity(c->consttype))
				return true;
			break;
		}
		case T_RelabelType:
		case T_NullTest:
		case T_BoolExpr:
		case T_CaseWhen:
			break;
		default:
			return true;
	}
	return expression_tree_walker(node, yb_pushdown_walker, (void *) colrefs);
}

/*
 * YbCanPushdownExpr
 *
 *	  Determine if the expression is pushable.
 *	  In general, expression tree is pushable if DocDB knows how to execute
 *	  all its nodes, in other words, it should be handeled by the evalExpr()
 *	  function defined in the ybgate_api.c. In addition, external paremeter
 *	  references of supported data types are also pushable, since these
 *	  references are replaced with constants by YbExprInstantiateParams before
 *	  the DocDB request is sent.
 *
 *	  If the colrefs parameter is provided, function also collects column
 *	  references represented by Var nodes in the expression tree. The colrefs
 *	  list may be initially empty (NIL) or already contain some YbExprColrefDesc
 *	  entries. That allows to collect column references from multiple
 *	  expressions into single list. The function avoids adding duplicate
 *	  references, however it does not remove duplecates if they are already
 *	  present in the colrefs list.
 *
 *	  To add support for another expression node type it should be added to the
 *	  yb_pushdown_walker where it should check node attributes that may affect
 *	  pushability, and implement evaluation of that node type instance in the
 *	  evalExpr() function.
 */
bool YbCanPushdownExpr(Expr *pg_expr, List **colrefs)
{
	/* respond with false if pushdown disabled in GUC */
	if (!yb_enable_expression_pushdown)
		return false;

	return !yb_pushdown_walker((Node *) pg_expr, colrefs);
}

/*
 * yb_transactional_walker
 *
 *	  Expression tree walker for the YbIsTransactionalExpr function.
 *	  As of initial version, it may be too optimistic, needs revisit.
 */
bool yb_transactional_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;
	switch (node->type)
	{
		case T_FuncExpr:
		{
			/*
			 * Built-in functions should be safe. If we learn of functions
			 * that are unsafe we may need a blacklist here.
			 * User defined function may be everything, unless it is immutable
			 * By definition, immutable functions can not access database.
			 * Otherwise safely assume that not immutable function would needs
			 * a distributed transaction.
			 */
			FuncExpr	   *func_expr = castNode(FuncExpr, node);
			Oid 			funcid = func_expr->funcid;
			HeapTuple		tuple;
			Form_pg_proc	pg_proc;
			if (is_builtin_func(funcid))
			{
				break;
			}
			tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(funcid));
			if (!HeapTupleIsValid(tuple))
				elog(ERROR, "cache lookup failed for function %u", funcid);
			pg_proc = (Form_pg_proc) GETSTRUCT(tuple);
			if (pg_proc->provolatile != PROVOLATILE_IMMUTABLE)
			{
				ReleaseSysCache(tuple);
				return true;
			}
			ReleaseSysCache(tuple);
			break;
		}
		/*
		 * The list of the expression types below was built by scrolling over
		 * expression_tree_walker function and selecting those looking like they
		 * do suspiciously transactional thing like running a subquery.
		 */
		case T_RangeTblRef:
		case T_SubLink:
		case T_SubPlan:
		case T_AlternativeSubPlan:
		case T_Query:
		case T_CommonTableExpr:
		case T_FromExpr:
		case T_JoinExpr:
		case T_AppendRelInfo:
		case T_RangeTblFunction:
		case T_TableSampleClause:
		case T_TableFunc:
			return true;
		/*
		 * Optimistically assume all other expression types do not
		 * require a distributed transaction.
		 */
		default:
			break;
	}
	return expression_tree_walker(node, yb_transactional_walker, context);
}

/*
 * YbIsTransactionalExpr
 *
 *	  Determine if the expression may need distributed transaction.
 *	  One shard modify table queries (INSERT, UPDATE, DELETE) running in
 *	  autocommit mode may skip starting distributed transactions. Without
 *	  distributed transaction overhead those statements perform much better.
 *	  However, certain expression may need to run a subquery or otherwise
 *	  access multiple nodes transactionally. This function checks if that
 *	  might be the case for given expression, and therefore distributed
 *	  transaction should be used for parent statement.
 *	  Historically the same function was used to determine if an expression
 *	  is pushable or if expression is (not) transactional, out of consideration
 *	  that if expression is simple enough to be pushable, it is not
 *	  transactional. That is generally true, pushable expression are not
 *	  transactional, however there are many not pushable expressions, which are
 *	  not transactional at the same time, so we can still benefit from higher
 *	  performing one shard queries even if they use not pushable expressions.
 *	  Besides, expression pushdown may be turned off with a GUC parameter.
 *	  If this function misdetermine transactional expression as not
 *	  transactional distributed transaction may be forced by surrounding the
 *	  statement with BEGIN; ... COMMIT;
 *	  Opposite misdetermination causes performance overhead only.
 */
bool YbIsTransactionalExpr(Node *pg_expr)
{
	return yb_transactional_walker(pg_expr, NULL);
}

/*
 * YBCNewEvalExprCall
 *
 *	  Serialize the Postgres expression tree and associate it with the
 *	  DocDB statement. Caller is supposed to ensure that expression is pushable
 *	  so DocDB can handle it.
 */
YBCPgExpr YBCNewEvalExprCall(YBCPgStatement ybc_stmt, Expr *pg_expr)
{
	YBCPgExpr ybc_expr;
	YBCPgCollationInfo collation_info;
	const YBCPgTypeEntity *type_ent;
	type_ent = YbDataTypeFromOidMod(InvalidAttrNumber,
									exprType((Node *) pg_expr));
	YBGetCollationInfo(exprCollation((Node *) pg_expr),
					   type_ent,
					   0 /* Datum */,
					   true /* is_null */,
					   &collation_info);
	HandleYBStatus(YBCPgNewOperator(ybc_stmt,
									"eval_expr_call",
									type_ent,
									collation_info.collate_is_valid_non_c,
									&ybc_expr));

	Datum expr_datum = CStringGetDatum(nodeToString(pg_expr));
	YBCPgExpr expr = YBCNewConstant(ybc_stmt, CSTRINGOID, C_COLLATION_OID,
									expr_datum , /* IsNull */ false);
	HandleYBStatus(YBCPgOperatorAppendArg(ybc_expr, expr));
	return ybc_expr;
}

/* ------------------------------------------------------------------------- */
/*  Execution output parameter from Yugabyte */
YbPgExecOutParam *YbCreateExecOutParam()
{
	YbPgExecOutParam *param = makeNode(YbPgExecOutParam);
	param->bfoutput = makeStringInfo();

	/* Not yet used */
	param->status = makeStringInfo();
	param->status_code = 0;

	return param;
}

void YbWriteExecOutParam(YbPgExecOutParam *param, const YbcPgExecOutParamValue *value) {
	appendStringInfoString(param->bfoutput, value->bfoutput);

	/* Not yet used */
	if (value->status)
	{
		appendStringInfoString(param->status, value->status);
		param->status_code = value->status_code;
	}
}
