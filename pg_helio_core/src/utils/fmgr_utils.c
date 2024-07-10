/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/fmgr_utils.c
 *
 * Utilities for FMGR (Function manager)
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <nodes/primnodes.h>

#include "utils/fmgr_utils.h"


/*
 * Takes an fcinfo (arguments to a top level function),
 * and a specified set of argument indices to that function,
 * and validates whether it's safe to use the common fn_extra
 * field to persist state information about that argument across
 * function calls.
 *
 * This validates that the underlying function call uses arguments
 * that are Const or PARAM_EXTERN (The parameter value that is supplied
 * from outside the plan for a bind variable in sql statement).
 * Function calls/CoerceViaIO calls etc are deemed to be unsafe in this
 * context.
 */
bool
IsSafeToReuseFmgrFunctionExtraMultiArgs(PG_FUNCTION_ARGS, int *argLocations, int numArgs)
{
	if (fcinfo->flinfo->fn_expr == NULL)
	{
		/* We don't know anything about the caller function */
		return false;
	}

	Node *expr = fcinfo->flinfo->fn_expr;
	List *args;

	if (IsA(expr, FuncExpr))
	{
		args = ((FuncExpr *) expr)->args;
	}
	else if (IsA(expr, OpExpr))
	{
		args = ((OpExpr *) expr)->args;
	}
	else if (IsA(expr, Const))
	{
		/* If the expression itself is const, we can safely reuse the cache */
		return true;
	}
	else
	{
		return false;
	}

	for (int i = 0; i < numArgs; i++)
	{
		if (list_length(args) <= argLocations[i])
		{
			return false;
		}

		Node *node = (Node *) lfirst(list_nth_cell(args, argLocations[i]));

		if (IsA(node, RelabelType))
		{
			RelabelType *relabeled = (RelabelType *) node;
			if (relabeled->relabelformat == COERCE_IMPLICIT_CAST)
			{
				node = (Node *) relabeled->arg;
			}
		}

		/* Only allow reusing if the parameter requested is a const or a Param of type extern */
		if (!IsA(node, Const) &&
			(!IsA(node, Param) || (((Param *) node)->paramkind != PARAM_EXTERN)))
		{
			return false;
		}
	}

	return true;
}
