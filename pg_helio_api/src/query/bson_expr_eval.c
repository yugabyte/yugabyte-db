/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expr_eval.c
 *
 * Module defining reusable evaluation of query expressions.
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <nodes/execnodes.h>
#include <executor/executor.h>

#include "operators/bson_expr_eval.h"
#include "query/query_operator.h"
#include "utils/mongo_errors.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * State tracking necessary objects to evaluate a function
 * against multiple datums. Reused across multiple expression
 * executions.
 */
typedef struct ExprEvalState
{
	/*
	 * Executor state that is retained for execution for the functions (PG Internal state).
	 * This tracks parameters, query JIT state, memory contexts per row etc.
	 */
	EState *estate;

	/*
	 * State that tracks the compiled and JITed expression. Holds the IL steps and function
	 * calls to be evaluated for the expression.
	 */
	ExprState *exprState;

	/*
	 * Holds the slots that will have the data for each row (Evaluation of expression requires
	 * a tuple table slot holding the Datums that are inputs to the expression).
	 * This will be updated per execution with the current tuple (datum) being evaluated.
	 */
	TupleTableSlot *tupleSlot;

	/*
	 * The Expression context used by PG (Internal state) for expression evaluation.
	 * This holds per evaluation execution state including the incoming tuple table slot
	 * and is mutated during the execution of the expression. This will be reset per
	 * execution of the expression.
	 */
	ExprContext *exprContext;

	/*
	 * Pre-allocated memory for the array of datums placed in the tuple table slot.
	 * This array can be reused across executions of the expression.
	 */
	Datum *datums;
} ExprEvalState;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static Datum ExpressionEval(ExprEvalState *exprEvalState,
							const pgbsonelement *element);
static ExprEvalState * CreateEvalStateFromExpr(Expr *expression);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(command_evaluate_query_expression);
PG_FUNCTION_INFO_V1(command_evaluate_expression_get_first_match);


/*
 * Top level export of an internal method that takes
 * an expression and a value as pgbson and evaluates the expression
 * against the value to return whether the value matches the
 * query expression.
 */
Datum
command_evaluate_query_expression(PG_FUNCTION_ARGS)
{
	pgbson *expression = PG_GETARG_PGBSON(0);
	pgbson *value = PG_GETARG_PGBSON(1);

	/* get the value */
	pgbsonelement valueElement;
	PgbsonToSinglePgbsonElement(value, &valueElement);

	/* get the expression value */
	bson_value_t expressionValue = ConvertPgbsonToBsonValue(expression);

	/* Compile the expression */
	ExprEvalState *evalState = GetExpressionEvalState(&expressionValue,
													  fcinfo->flinfo->fn_mcxt);

	/* Evaluate the expression */
	Datum result = ExpressionEval(evalState, &valueElement);
	PG_RETURN_BOOL(DatumGetBool(result));
}


/*
 * Top level export of an internal method that takes
 * an expression and a value as pgbson and evaluates the expression
 * against the value to returns the first matching value as a pgbson.
 * If no values match, returns NULL.
 */
Datum
command_evaluate_expression_get_first_match(PG_FUNCTION_ARGS)
{
	pgbson *expression = PG_GETARG_PGBSON(0);
	pgbson *value = PG_GETARG_PGBSON(1);

	/* get the value */
	pgbsonelement valueElement;
	PgbsonToSinglePgbsonElement(value, &valueElement);

	/* get the expression value */
	bson_value_t expressionValue = ConvertPgbsonToBsonValue(expression);

	/* Compile the expression */
	ExprEvalState *evalState = GetExpressionEvalState(&expressionValue,
													  fcinfo->flinfo->fn_mcxt);

	pgbsonelement finalValue = { 0 };
	finalValue.path = "";
	finalValue.pathLength = 0;
	finalValue.bsonValue = EvalExpressionAgainstArrayGetFirstMatch(evalState,
																   &valueElement.bsonValue);
	if (finalValue.bsonValue.value_type == BSON_TYPE_EOD)
	{
		PG_RETURN_NULL();
	}
	else
	{
		PG_RETURN_POINTER(PgbsonElementToPgbson(&finalValue));
	}
}


/*
 * Evaluate a query expression against every element of the array
 * and for each element check whether the query returns a match.
 * If any element returns a match, return true. Else return false.
 *
 * Note: The input value *must* be of type array.
 */
bool
EvalBooleanExpressionAgainstArray(ExprEvalState *evalState, const
								  bson_value_t *queryValue)
{
	bson_iter_t arrayIterator;
	if (queryValue->value_type != BSON_TYPE_ARRAY ||
		!bson_iter_init_from_data(&arrayIterator, queryValue->value.v_doc.data,
								  queryValue->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Input value should be an array. found type %s",
							BsonTypeName(
								queryValue->value_type))));
	}

	pgbsonelement element;
	while (bson_iter_next(&arrayIterator))
	{
		BsonIterToPgbsonElement(&arrayIterator, &element);
		Datum result = ExpressionEval(evalState, &element);
		if (DatumGetBool(result))
		{
			return true;
		}
	}

	return false;
}


/*
 * Evaluate a query expression against the provided value.
 * If the value returns a match, return true. Else return false.
 *
 * Note: For Nested Array there are cases where 1 level nested arrays are recursed
 * to find a match, so this method first runs the check for the complete array value
 * and if its not a match then individual nested array values are checked if
 * @shouldRecurseIfArray flag is true
 */
bool
EvalBooleanExpressionAgainstValue(ExprEvalState *evalState,
								  const bson_value_t *queryValue,
								  bool shouldRecurseIfArray)
{
	pgbsonelement element = { 0 };
	element.bsonValue = *queryValue;

	/* First complete array as value is checked against the expression */
	bool matched = DatumGetBool(ExpressionEval(evalState, &element));
	if (!matched && shouldRecurseIfArray && queryValue->value_type == BSON_TYPE_ARRAY)
	{
		/* Second if shouldRecurseIfArray is true, then all elements are checked against the expr */
		matched = EvalBooleanExpressionAgainstArray(evalState, &(element.bsonValue));
	}
	return matched;
}


/*
 * Evaluate a query expression against every element of the array
 * and for each element check whether the query returns a match.
 * If any element returns a match, return the value of the first
 * element that matches the query. Otherwise, returns a
 * bson_value_t with BSON_TYPE_EOD
 *
 * Note: The input value *must* be of type array.
 */
bson_value_t
EvalExpressionAgainstArrayGetFirstMatch(ExprEvalState *evalState,
										const bson_value_t *queryValue)
{
	bson_iter_t arrayIterator;
	if (queryValue->value_type != BSON_TYPE_ARRAY ||
		!bson_iter_init_from_data(&arrayIterator, queryValue->value.v_doc.data,
								  queryValue->value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Input value should be an array. found type %s",
							BsonTypeName(
								queryValue->value_type))));
	}

	pgbsonelement element;
	while (bson_iter_next(&arrayIterator))
	{
		BsonIterToPgbsonElement(&arrayIterator, &element);
		Datum result = ExpressionEval(evalState, &element);
		if (DatumGetBool(result))
		{
			return element.bsonValue;
		}
	}

	element.bsonValue.value_type = BSON_TYPE_EOD;
	return element.bsonValue;
}


/*
 * Evaluate a query expression against every element of the array
 * and for each element check whether the query returns a match.
 * Return the list of all the matching indices where expression has matched the value in array
 *
 * Note: The input value *must* be of type array.
 */
List *
EvalExpressionAgainstArrayGetAllMatchingIndices(ExprEvalState *evalState,
												const bson_value_t *arrayValue,
												const bool shouldRecurseIfArray)
{
	if (arrayValue->value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Input value should be an array. found type %s",
							BsonTypeName(
								arrayValue->value_type))));
	}

	bson_iter_t arrayIterator;
	BsonValueInitIterator(arrayValue, &arrayIterator);

	pgbsonelement element;
	List *matchingIndices = NIL;
	int index = 0;
	while (bson_iter_next(&arrayIterator))
	{
		BsonIterToPgbsonElement(&arrayIterator, &element);

		/*
		 * If the value itself is array then check for any match consider the complete array a match
		 */
		Datum result;
		if (shouldRecurseIfArray && element.bsonValue.value_type == BSON_TYPE_ARRAY)
		{
			result = EvalBooleanExpressionAgainstArray(evalState, &(element.bsonValue));
		}
		else
		{
			result = ExpressionEval(evalState, &element);
		}
		if (DatumGetBool(result))
		{
			matchingIndices = lappend_int(matchingIndices, index);
		}
		index++;
	}

	return matchingIndices;
}


/*
 * Compiles the expression pointed to by the FuncExpr and creates an expression
 * object that can be reused to evaluate expressions for input values. The state object
 * is created in the specified MemoryContext.
 */
ExprEvalState *
GetExpressionEvalStateFromFuncExpr(const FuncExpr *expression,
								   MemoryContext memoryContext)
{
	MemoryContext originalMemoryContext = MemoryContextSwitchTo(memoryContext);
	ExprEvalState *evalState = CreateEvalStateFromExpr((Expr *) expression);
	MemoryContextSwitchTo(originalMemoryContext);
	return evalState;
}


/*
 * Compiles the expression pointed to by the bson value and creates an expression
 * object that can be reused to evaluate expressions for input values. The state object
 * is created in the specified MemoryContext.
 */
ExprEvalState *
GetExpressionEvalState(const bson_value_t *expression, MemoryContext memoryContext)
{
	const char *collationString = NULL;
	return GetExpressionEvalStateWithCollation(expression, memoryContext,
											   collationString);
}


/*
 * Compiles the expression pointed to by the bson value and creates an expression
 * object that can be reused to evaluate expressions for input values. The state object
 * is created in the specified MemoryContext.
 */
ExprEvalState *
GetExpressionEvalStateWithCollation(const bson_value_t *expression, MemoryContext
									memoryContext, const char *collationString)
{
	MemoryContext originalMemoryContext = MemoryContextSwitchTo(memoryContext);
	Expr *expr = CreateQualForBsonValueExpression(expression, collationString);
	ExprEvalState *evalState = CreateEvalStateFromExpr(expr);
	MemoryContextSwitchTo(originalMemoryContext);
	return evalState;
}


/* Frees the resources of a ExprEvalState heap object.
 * This should only be used on an ExprEvalState created on the same memory context using the
 * GetExpressionEvalState* functions. */
void
FreeExprEvalState(ExprEvalState *exprEvalState, MemoryContext memoryContext)
{
	if (exprEvalState != NULL)
	{
		MemoryContext originalMemoryContext = MemoryContextSwitchTo(memoryContext);

		if (exprEvalState->estate != NULL)
		{
			/* this will destroy the executor state memory context which holds exprState and exprContext. */
			FreeExecutorState(exprEvalState->estate);
			exprEvalState->estate = NULL;
		}

		if (exprEvalState->datums != NULL)
		{
			pfree(exprEvalState->datums);
			exprEvalState->datums = NULL;
		}

		if (exprEvalState->tupleSlot != NULL)
		{
			ExecDropSingleTupleTableSlot(exprEvalState->tupleSlot);
			exprEvalState->tupleSlot = NULL;
		}

		pfree(exprEvalState);

		MemoryContextSwitchTo(originalMemoryContext);
	}
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */

/*
 * Given a query Expression, compiles, and plans an execution engine
 * that can use the query expression to evaluate conditions.
 */
static ExprEvalState *
CreateEvalStateFromExpr(Expr *expression)
{
	ExprEvalState *evalState = palloc(sizeof(ExprEvalState));
	evalState->estate = CreateExecutorState();
	evalState->exprState = ExecPrepareExpr(expression, evalState->estate);
	evalState->exprContext = GetPerTupleExprContext(evalState->estate);
	evalState->datums = palloc(sizeof(Datum));

	TupleDesc tupleDescriptor = CreateTemplateTupleDesc(1);

	/* We only have 1 attribute (the input) */
	AttrNumber attributeNumber = (AttrNumber) 1;
	char *attributeName = NULL;
	Oid attributeOid = INTERNALOID;
	int attributeTypeModifier = -1;

	/* Attribute is not an array */
	int numDimensions = 0;
	TupleDescInitEntry(tupleDescriptor, attributeNumber, attributeName,
					   attributeOid, attributeTypeModifier, numDimensions);

	TupleTableSlot *slot = MakeSingleTupleTableSlot(tupleDescriptor, &TTSOpsMinimalTuple);
	evalState->exprContext->ecxt_scantuple = slot;
	slot->tts_nvalid = 1;
	slot->tts_isnull = palloc0(sizeof(bool));
	evalState->tupleSlot = slot;
	return evalState;
}


/*
 * Evaluates an expression given the expression evaluation state against a target
 * value in the pgbsonelement and returns the Datum that is returned by the expression.
 */
static Datum
ExpressionEval(ExprEvalState *exprEvalState, const pgbsonelement *element)
{
	bool isNull = false;

	*(exprEvalState->datums) = PointerGetDatum(element);

	ResetExprContext(exprEvalState->exprContext);
	exprEvalState->tupleSlot->tts_values = exprEvalState->datums;
	Datum result = ExecEvalExprSwitchContext(exprEvalState->exprState,
											 exprEvalState->exprContext, &isNull);

	if (isNull)
	{
		return (Datum) NULL;
	}
	else
	{
		return result;
	}
}
