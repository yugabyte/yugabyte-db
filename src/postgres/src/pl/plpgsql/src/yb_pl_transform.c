/*--------------------------------------------------------------------------------------------------
 *
 * yb_pl_transform.c
 *    Static analysis of an online schema change transform.
 *
 *    An online schema change checks its transform once, when the job begins,
 *    instead of checking each row the transform returns.  That needs the
 *    transform's body as analyzed expressions, and only PL/pgSQL can produce
 *    them: a reference "s.col" to a field of the argument resolves, through
 *    PL/pgSQL's parser hooks, to a Param on one of the function's datums, and
 *    only the compiled function can map that datum back to the argument's
 *    column.  The backend reaches yb_plpgsql_single_return_expr through
 *    load_external_function.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * IDENTIFICATION
 *    src/pl/plpgsql/src/yb_pl_transform.c
 *--------------------------------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/tupdesc.h"
#include "fmgr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/analyze.h"
#include "parser/parser.h"
#include "plpgsql.h"
#include "utils/typcache.h"

typedef struct YbArgRefContext
{
	PLpgSQL_execstate *estate;
	int			argDno;			/* the argument's datum */
	Oid			argType;
	TupleDesc	argDesc;
	const char *why;			/* set when a reference cannot be mapped */
} YbArgRefContext;

/*
 * Replace each Param on the argument with a Var of varno 1 over the
 * argument's row type: a field reference by the column's Var, the whole
 * argument by a whole-row Var.  A Param on any other datum sets ctx->why.
 */
static Node *
YbArgRefsToVarsMutator(Node *node, YbArgRefContext *ctx)
{
	if (node == NULL)
		return NULL;

	/*
	 * A sublink's query is left as it is: the caller refuses subqueries, and
	 * expression_tree_mutator cannot descend into a Query.
	 */
	if (IsA(node, Query))
		return node;

	if (IsA(node, Param) && ((Param *) node)->paramkind == PARAM_EXTERN)
	{
		Param	   *param = (Param *) node;
		int			dno = param->paramid - 1;
		PLpgSQL_datum *datum;

		if (dno < 0 || dno >= ctx->estate->ndatums)
		{
			ctx->why = "It refers to a parameter other than its argument.";
			return node;
		}

		if (dno == ctx->argDno)
			return (Node *) makeVar(1, InvalidAttrNumber, ctx->argType, -1,
									InvalidOid, 0);

		datum = ctx->estate->datums[dno];
		if (datum->dtype == PLPGSQL_DTYPE_RECFIELD &&
			((PLpgSQL_recfield *) datum)->recparentno == ctx->argDno)
		{
			const char *fieldname = ((PLpgSQL_recfield *) datum)->fieldname;

			for (int i = 0; i < ctx->argDesc->natts; i++)
			{
				Form_pg_attribute att = TupleDescAttr(ctx->argDesc, i);

				if (!att->attisdropped &&
					strcmp(NameStr(att->attname), fieldname) == 0)
					return (Node *) makeVar(1, att->attnum, att->atttypid,
											att->atttypmod, att->attcollation,
											0);
			}
		}

		ctx->why = "It refers to a variable other than its argument.";
		return node;
	}

	return expression_tree_mutator(node, YbArgRefsToVarsMutator, (void *) ctx);
}

/*
 * The parsed expression must be a bare expression: PL/pgSQL accepts the tail
 * of a SELECT after it (RETURN x FROM t WHERE ...).
 */
static bool
YbIsBareExpression(Query *query)
{
	return query->commandType == CMD_SELECT &&
		query->utilityStmt == NULL &&
		query->rtable == NIL &&
		query->jointree->fromlist == NIL &&
		query->jointree->quals == NULL &&
		query->cteList == NIL &&
		query->groupClause == NIL &&
		query->groupingSets == NIL &&
		query->havingQual == NULL &&
		query->windowClause == NIL &&
		query->distinctClause == NIL &&
		query->sortClause == NIL &&
		query->limitOffset == NULL &&
		query->limitCount == NULL &&
		query->rowMarks == NIL &&
		query->setOperations == NULL &&
		!query->hasAggs &&
		!query->hasWindowFuncs &&
		!query->hasTargetSRFs &&
		list_length(query->targetList) == 1;
}

static Node *
YbAnalyzeSingleReturn(PLpgSQL_function *func, const char **why)
{
	PLpgSQL_stmt_block *block = func->action;
	PLpgSQL_stmt_return *ret;
	PLpgSQL_execstate estate;
	List	   *raw;
	Query	   *query;
	YbArgRefContext ctx;
	Node	   *result;

	if (func->fn_nargs != 1 ||
		func->datums[func->fn_argvarnos[0]]->dtype != PLPGSQL_DTYPE_REC)
	{
		*why = "It must take a single row-typed argument.";
		return NULL;
	}

	/*
	 * A DECLARE section runs its initializers before the body, and an
	 * EXCEPTION clause runs the body in a subtransaction.
	 */
	if (block->exceptions != NULL || block->n_initvars != 0 ||
		list_length(block->body) != 1 ||
		((PLpgSQL_stmt *) linitial(block->body))->cmd_type != PLPGSQL_STMT_RETURN)
	{
		*why = "Its body must be a single RETURN statement, with no DECLARE section or EXCEPTION clause.";
		return NULL;
	}

	ret = (PLpgSQL_stmt_return *) linitial(block->body);
	if (ret->expr == NULL)
	{
		*why = "Its RETURN statement must return an expression.";
		return NULL;
	}

	/* As exec_prepare_plan does before the parser hooks run. */
	ret->expr->func = func;
	yb_plpgsql_estate_setup_for_analysis(&estate, func);

	raw = raw_parser(ret->expr->query, ret->expr->parseMode);
	if (list_length(raw) != 1)
	{
		*why = "Its RETURN statement must return a single expression.";
		return NULL;
	}
	query = parse_analyze_withcb(linitial_node(RawStmt, raw),
								 ret->expr->query,
								 (ParserSetupHook) plpgsql_parser_setup,
								 (void *) ret->expr, NULL);
	if (!YbIsBareExpression(query))
	{
		*why = "Its RETURN statement must return a single expression, with no FROM, WHERE or other SELECT clause.";
		return NULL;
	}

	ctx.estate = &estate;
	ctx.argDno = func->fn_argvarnos[0];
	ctx.argType = ((PLpgSQL_rec *) func->datums[ctx.argDno])->rectypeid;
	ctx.argDesc = lookup_rowtype_tupdesc_copy(ctx.argType, -1);
	ctx.why = NULL;

	result = YbArgRefsToVarsMutator((Node *) linitial_node(TargetEntry,
														   query->targetList)->expr,
									&ctx);
	if (ctx.why != NULL)
	{
		*why = ctx.why;
		return NULL;
	}
	return result;
}

/*
 * Compile PL/pgSQL function fnOid and, if its body is exactly
 * "BEGIN RETURN <expression>; END" and it takes one row-typed argument,
 * return the expression, parse-analyzed, with every reference to the argument
 * turned into a Var of varno 1 over the argument's row type: s.col into the
 * column's Var, and s itself into a whole-row Var.  Names resolve under the
 * caller's settings, so the caller applies the function's SET clauses first.
 *
 * Otherwise return NULL with *why set to a sentence saying why.  A body that does not compile
 * raises the compiler's error.
 */
Node *
yb_plpgsql_single_return_expr(Oid fnOid, const char **why)
{
	LOCAL_FCINFO(fake_fcinfo, 0);
	FmgrInfo	flinfo;
	PLpgSQL_function *func;
	PLpgSQL_execstate *save_cur_estate;
	Node	   *result = NULL;

	/* As plpgsql_validator compiles a function without calling it. */
	MemSet(fake_fcinfo, 0, SizeForFunctionCallInfo(0));
	MemSet(&flinfo, 0, sizeof(flinfo));
	fake_fcinfo->flinfo = &flinfo;
	flinfo.fn_oid = fnOid;
	flinfo.fn_mcxt = CurrentMemoryContext;

	func = plpgsql_compile(fake_fcinfo, false);

	/* As plpgsql_call_handler does around a call. */
	save_cur_estate = func->cur_estate;
	func->use_count++;

	PG_TRY();
	{
		result = YbAnalyzeSingleReturn(func, why);
	}
	PG_FINALLY();
	{
		func->use_count--;
		func->cur_estate = save_cur_estate;
	}
	PG_END_TRY();

	return result;
}
