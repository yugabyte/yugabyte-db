/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_timestamp_operators.c
 *
 * Implementation of timestamp aggregation operators
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void SetResultValueForDollarTsSecond(bson_value_t *inputArgument,
											bson_value_t *result);
static void SetResultValueForDollarTsIncrement(bson_value_t *inputArgument,
											   bson_value_t *result);

/*
 * This function handles the final result for $tsSecond operator which returns the seconds of input timestamp.
 */
void
HandlePreParsedDollarTsSecond(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	AggregationExpressionData *parsedData = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(parsedData, doc,
									  &childExpression,
									  isNullOnEmpty);

	bson_value_t result = { 0 };

	SetResultValueForDollarTsSecond(&childExpression.value, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function handles the parsing for the operator $tsSecond.
 * Input structure for $tsSecond is something like { $tsSecond: expression }.
 * This expression should be a timestamp.
 */
void
ParseDollarTsSecond(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	int numOfReqArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfReqArgs,
																			 "$tsSecond",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	if (IsAggregationExpressionConstant(parsedData))
	{
		SetResultValueForDollarTsSecond(&parsedData->value, &data->value);
		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
		return;
	}

	data->operator.arguments = parsedData;
}


/*
 * This function takes care of taking in the inputArgument and computing the final result for $tsSecond operator.
 */
static void
SetResultValueForDollarTsSecond(bson_value_t *inputArgument, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(inputArgument))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (inputArgument->value_type == BSON_TYPE_TIMESTAMP)
	{
		result->value_type = BSON_TYPE_INT64;
		result->value.v_int64 = inputArgument->value.v_timestamp.timestamp;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5687301), errmsg(
							"$tsSecond requires a timestamp argument, found: %s",
							BsonTypeName(inputArgument->value_type)),
						errdetail_log(
							"$tsSecond requires a timestamp argument, found: %s",
							BsonTypeName(inputArgument->value_type))));
	}
}


/*
 * handle $tsIncrement - return the incrementing ordinal from a timestamp as a long.
 */
void
HandlePreParsedDollarTsIncrement(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult)
{
	AggregationExpressionData *parsedData = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(parsedData, doc,
									  &childExpression,
									  isNullOnEmpty);

	bson_value_t result = { 0 };

	SetResultValueForDollarTsIncrement(&childExpression.value, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


void
ParseDollarTsIncrement(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context)
{
	int numOfReqArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfReqArgs,
																			 "$tsIncrement",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	if (IsAggregationExpressionConstant(parsedData))
	{
		SetResultValueForDollarTsIncrement(&parsedData->value, &data->value);
		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
		return;
	}

	data->operator.arguments = parsedData;
}


static void
SetResultValueForDollarTsIncrement(bson_value_t *inputArgument, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(inputArgument))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (inputArgument->value_type == BSON_TYPE_TIMESTAMP)
	{
		result->value_type = BSON_TYPE_INT64;
		result->value.v_int64 = inputArgument->value.v_timestamp.increment;
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION5687302), errmsg(
							"Argument to $tsIncrement must be a timestamp, but is %s",
							BsonTypeName(inputArgument->value_type)),
						errdetail_log(
							"Argument to $tsIncrement must be a timestamp, but is %s",
							BsonTypeName(inputArgument->value_type))));
	}
}
