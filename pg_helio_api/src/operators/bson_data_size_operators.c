/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_data_size_operators.c
 *
 * Implementation of data size aggregation operators
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
static void SetResultValueForDollarBinarySize(bson_value_t *inputArgument,
											  bson_value_t *result);
static void SetResultValueForDollarBsonSize(bson_value_t *inputArgument,
											bson_value_t *result);


/*
 * This function handles the final result for $bsonSize operator which returns the size of the input bson document in bytes.
 */
void
HandlePreParsedDollarBsonSize(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	AggregationExpressionData *parsedData = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(parsedData, doc,
									  &childExpression,
									  isNullOnEmpty);

	bson_value_t result = { 0 };

	SetResultValueForDollarBsonSize(&childExpression.value, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function handles the parsing for the operator $bsonSize.
 * Input structure for $bsonSize is something like { $bsonSize: <object> }.
 * This object can be any expression as long as it can be resolved to document or null.
 */
void
ParseDollarBsonSize(const bson_value_t *argument, AggregationExpressionData *data, const
					ExpressionVariableContext *variableContext)
{
	int numOfReqArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfReqArgs,
																			 "$bsonSize",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 variableContext);

	if (IsAggregationExpressionConstant(parsedData))
	{
		SetResultValueForDollarBsonSize(&parsedData->value, &data->value);
		data->kind = AggregationExpressionKind_Constant;

		pfree(parsedData);

		return;
	}

	data->operator.arguments = parsedData;
}


/*
 * This function handles the final result for $binarySize operator which returns the size of the input expression in bytes.
 */
void
HandlePreParsedDollarBinarySize(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult)
{
	AggregationExpressionData *parsedData = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childExpression = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(parsedData, doc,
									  &childExpression,
									  isNullOnEmpty);

	bson_value_t result = { 0 };

	SetResultValueForDollarBinarySize(&childExpression.value, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * This function handles the parsing for the operator $binarySize.
 * Input structure for $bsonSize is something like { $binarySize: expression }.
 * This object can be any expression as long as it can be resolved to string or binary data.
 */
void
ParseDollarBinarySize(const bson_value_t *argument, AggregationExpressionData *data, const
					  ExpressionVariableContext *variableContext)
{
	int numOfReqArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfReqArgs,
																			 "$binarySize",
																			 &data->
																			 operator.
																			 argumentsKind,
																			 variableContext);

	if (IsAggregationExpressionConstant(parsedData))
	{
		SetResultValueForDollarBinarySize(&parsedData->value, &data->value);
		data->kind = AggregationExpressionKind_Constant;

		pfree(parsedData);

		return;
	}

	data->operator.arguments = parsedData;
}


/*
 * This function takes care of taking in the result and computing the final binary size in bytes.
 */
static void
SetResultValueForDollarBinarySize(bson_value_t *inputArgument, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(inputArgument))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (inputArgument->value_type == BSON_TYPE_BINARY)
	{
		result->value_type = BSON_TYPE_INT32;
		result->value.v_int32 = inputArgument->value.v_binary.data_len;
	}
	else if (inputArgument->value_type == BSON_TYPE_UTF8)
	{
		result->value_type = BSON_TYPE_INT32;
		result->value.v_int32 = inputArgument->value.v_utf8.len;
	}
	else
	{
		ereport(ERROR, (errcode(MongoLocation51276), errmsg(
							"$binarySize requires a string or BinData argument, found: %s",
							BsonTypeName(inputArgument->value_type)),
						errhint(
							"$binarySize requires a string or BinData argument, found: %s",
							BsonTypeName(inputArgument->value_type))));
	}
}


/*
 * This function takes care of taking in the result and computing the final bson size in bytes.
 */
static void
SetResultValueForDollarBsonSize(bson_value_t *inputArgument, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(inputArgument))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (inputArgument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoLocation31393), errmsg(
							"$bsonSize requires a document input, found: %s",
							BsonTypeName(inputArgument->value_type)),
						errhint("$bsonSize requires a document input, found: %s",
								BsonTypeName(inputArgument->value_type))));
	}

	/* Result type is int32 as the max document we can store is for 16MB and that size in bytes is less than INT32_MAX. */
	result->value.v_int32 = inputArgument->value.v_doc.data_len;
	result->value_type = BSON_TYPE_INT32;
}
