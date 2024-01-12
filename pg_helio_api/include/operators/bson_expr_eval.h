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

typedef struct FuncExpr FuncExpr;

typedef struct ExprEvalState ExprEvalState;

ExprEvalState * GetExpressionEvalState(const bson_value_t *expression,
									   MemoryContext memoryContext);

ExprEvalState * GetExpressionEvalStateFromFuncExpr(const FuncExpr *expression,
												   MemoryContext memoryContext);

bool EvalBooleanExpressionAgainstArray(ExprEvalState *evalState,
									   const bson_value_t *queryValue);

bool EvalBooleanExpressionAgainstValue(ExprEvalState *evalState,
									   const bson_value_t *queryValue,
									   bool shouldRecurseIfArray);

bson_value_t EvalExpressionAgainstArrayGetFirstMatch(ExprEvalState *evalState,
													 const bson_value_t *queryValue);
List * EvalExpressionAgainstArrayGetAllMatchingIndices(ExprEvalState *evalState,
													   const bson_value_t *arrayValue,
													   const bool shouldRecurseIfArray);

#endif
