/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_expr_eval.h
 *
 * Common declarations of functions for handling expression evaluation on values for bson query expressions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_EXPR_EVAL_H
#define BSON_EXPR_EVAL_H

#include "io/bson_core.h"
#include <nodes/execnodes.h>

typedef struct FuncExpr FuncExpr;

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
}ExprEvalState;

ExprEvalState * GetExpressionEvalState(const bson_value_t *expression,
									   MemoryContext memoryContext);
ExprEvalState * GetExpressionEvalStateWithCollation(const bson_value_t *expression,
													MemoryContext memoryContext, const
													char *collationString);

ExprEvalState * GetExpressionEvalStateFromFuncExpr(const FuncExpr *expression,
												   MemoryContext memoryContext);

ExprEvalState * GetExpressionEvalStateForBsonInput(const bson_value_t *expression,
												   MemoryContext memoryContext, bool
												   hasOperatorRestrictions);

void FreeExprEvalState(ExprEvalState *exprEvalState, MemoryContext memoryContext);

bool EvalBooleanExpressionAgainstArray(ExprEvalState *evalState,
									   const bson_value_t *queryValue);

bool EvalBooleanExpressionAgainstValue(ExprEvalState *evalState,
									   const bson_value_t *queryValue,
									   bool shouldRecurseIfArray);
bool EvalBooleanExpressionAgainstBson(ExprEvalState *evalState,
									  const bson_value_t *queryValue);

bson_value_t EvalExpressionAgainstArrayGetFirstMatch(ExprEvalState *evalState,
													 const bson_value_t *queryValue);
List * EvalExpressionAgainstArrayGetAllMatchingIndices(ExprEvalState *evalState,
													   const bson_value_t *arrayValue,
													   const bool shouldRecurseIfArray);

#endif
