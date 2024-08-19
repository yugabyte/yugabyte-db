/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_boolean_operators.c
 *
 * Boolean Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */
typedef enum BooleanType
{
	BooleanType_Or = 1,
	BooleanType_And = 2,
	BooleanType_Not = 3,
} BooleanType;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void ParseBooleanVariableOperands(const bson_value_t *argument,
										 AggregationExpressionData *data,
										 ParseAggregationExpressionContext *context,
										 BooleanType
										 booleanType);
static void HandlePreParsedBooleanVariableOperands(pgbson *doc, void *arguments,
												   bson_value_t *result,
												   ExpressionResult *expressionResult,
												   BooleanType booleanType);
static bool ProcessBooleanOperator(const bson_value_t *currentElement,
								   bson_value_t *result,
								   bool isFieldPathExpression, BooleanType booleanType);

/*
 * Parses an $or expression and sets the parsed data in the data argument.
 * $or is expressed as { "$or":  [ <expression>, <expression>, .. ]  }
 */
void
ParseDollarOr(const bson_value_t *argument, AggregationExpressionData *data,
			  ParseAggregationExpressionContext *context)
{
	data->value.value_type = BSON_TYPE_BOOL;
	data->value.value.v_bool = false;
	ParseBooleanVariableOperands(argument, data, context, BooleanType_Or);
}


/*
 * Evaluates the output of an $or expression.
 * $or is expressed as { "$or": [ <expression>, <expression>, .. ] }
 * or { "$or": <expression> }.
 * We evaluate the inner expressions and then return a boolean
 * representing the table of truth for or.
 */
void
HandlePreParsedDollarOr(pgbson *doc, void *arguments,
						ExpressionResult *expressionResult)
{
	bson_value_t result =
	{
		.value_type = BSON_TYPE_BOOL,
		.value.v_bool = false
	};
	HandlePreParsedBooleanVariableOperands(doc, arguments, &result, expressionResult,
										   BooleanType_Or);
}


/*
 * Parses an $and expression and sets the parsed data in the data argument.
 * $and is expressed as { "$and":  [ <expression>, <expression>, .. ]  }
 */
void
ParseDollarAnd(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	data->value.value_type = BSON_TYPE_BOOL;
	data->value.value.v_bool = true;
	ParseBooleanVariableOperands(argument, data, context, BooleanType_And);
}


/*
 * Evaluates the output of an $and expression.
 * $and is expressed as { "$and": [ <expression>, <expression>, .. ] }
 * or { "$and": <expression> }.
 * We evaluate the inner expressions and then return a boolean
 * representing the table of truth for and.
 */
void
HandlePreParsedDollarAnd(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	bson_value_t result =
	{
		.value_type = BSON_TYPE_BOOL,
		.value.v_bool = true
	};

	HandlePreParsedBooleanVariableOperands(doc, arguments, &result, expressionResult,
										   BooleanType_And);
}


/*
 * Parses an $not expression and sets the parsed data in the data argument.
 * $not is expressed as { "$not": [ <expression> ] }
 * or { "$not": <expression> }.
 */
void
ParseDollarNot(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfRequiredArgs,
																			 "$not",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	/* If argument is a constant: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the argument as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(parsedData))
	{
		ProcessBooleanOperator(&parsedData->value, &data->value, false, BooleanType_Not);
		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
	}
	else
	{
		data->operator.arguments = parsedData;
	}
}


/*
 * Evaluates the output of a $not expression.
 * $not is expressed as { "$not": [ <expression> ] }
 * or { "$not": <expression> }.
 * We return the negated boolean of the evaluated expression.
 */
void
HandlePreParsedDollarNot(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t currentValue = childResult.value;

	bson_value_t result = { 0 };
	ProcessBooleanOperator(&currentValue, &result, childResult.isFieldPathExpression,
						   BooleanType_Not);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Helper to parse arithmetic operators that take variable number of operands.
 */
void
ParseBooleanVariableOperands(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context, BooleanType
							 booleanType)
{
	bool areArgumentsConstant;
	List *argumentsList = ParseVariableArgumentsForExpression(argument,
															  &areArgumentsConstant,
															  context);

	if (areArgumentsConstant)
	{
		int idx = 0;
		while (argumentsList != NIL && idx < argumentsList->length)
		{
			AggregationExpressionData *currentData = list_nth(argumentsList, idx);

			bool continueEnumerating = ProcessBooleanOperator(&currentData->value,
															  &data->value,
															  false, booleanType);
			if (!continueEnumerating)
			{
				break;
			}

			idx++;
		}

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(argumentsList);
	}
	else
	{
		data->operator.arguments = argumentsList;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/* Helper to evaluate pre-parsed expressions of operators that take variable number of operands. */
/* Currently used by $or and $and */
static void
HandlePreParsedBooleanVariableOperands(pgbson *doc, void *arguments,
									   bson_value_t *result,
									   ExpressionResult *expressionResult,
									   BooleanType booleanType)
{
	List *argumentList = (List *) arguments;

	int idx = 0;
	while (argumentList != NIL && idx < argumentList->length)
	{
		AggregationExpressionData *currentData = list_nth(argumentList, idx);

		bool isNullOnEmpty = false;
		ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(currentData, doc, &childResult, isNullOnEmpty);

		bson_value_t currentValue = childResult.value;
		bool continueEnumerating = ProcessBooleanOperator(&currentValue,
														  result,
														  childResult.
														  isFieldPathExpression,
														  booleanType);
		if (!continueEnumerating)
		{
			break;
		}

		idx++;
	}

	ExpressionResultSetValue(expressionResult, result);
}


/*
 * Helper to process a boolean operator
 * Returns true if the operator should continue evaluating the next element, false otherwise.
 */
static bool
ProcessBooleanOperator(const bson_value_t *currentElement, bson_value_t *result,
					   bool isFieldPathExpression, BooleanType booleanType)
{
	switch (booleanType)
	{
		case BooleanType_Or:
		{
			result->value.v_bool = result->value.v_bool || BsonValueAsBool(
				currentElement);
			return !result->value.v_bool;
		}

		case BooleanType_And:
		{
			result->value.v_bool = result->value.v_bool && BsonValueAsBool(
				currentElement);
			return result->value.v_bool;
		}

		case BooleanType_Not:
		{
			result->value_type = BSON_TYPE_BOOL;
			result->value.v_bool = !BsonValueAsBool(currentElement);
			return true;
		}

		default:
		{
			ereport(ERROR, (errmsg("Invalid boolean aggregation operator")));
			break;
		}
	}
}
