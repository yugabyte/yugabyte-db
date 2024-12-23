/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_bitwise_operators.c
 *
 * Implementation of bitwise aggregation operators
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"

/* SelectBitOperationType indicates if doing $bitAnd / $bitNot / $bitOr / $bitXor operation to common code */
typedef enum SelectBitOperationType
{
	SelectBitOperationType_BitAnd, /* $bitAnd operation */
	SelectBitOperationType_BitNot, /* $bitNot operation */
	SelectBitOperationType_BitOr,  /* $bitOr operation */
	SelectBitOperationType_BitXor, /* $bitXor operation */
} SelectBitOperationType;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static bson_value_t BsonValueBitwiseOperation(const bson_value_t *left, const
											  bson_value_t *right, SelectBitOperationType
											  type);
static void ParseDollarBitNotOperation(const bson_value_t *argument,
									   AggregationExpressionData *data,
									   ParseAggregationExpressionContext *context);

static void ParseDollarBitOperation(const bson_value_t *argument,
									AggregationExpressionData *data,
									SelectBitOperationType type,
									ParseAggregationExpressionContext *context);
static void ProcessDollarBit(pgbson *doc, bson_value_t *result, void *expressionsArray,
							 SelectBitOperationType type,
							 ExpressionResult *expressionResult, bool
							 areArgumentsConstant);

/*
 * Evaluates the output of a $bitAnd expression. Via the new operator framework.
 */
void
HandlePreParsedDollarBitAnd(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	bson_value_t result;
	result.value_type = BSON_TYPE_INT32;
	result.value.v_int32 = -1;

	bool areArgumentsConstant = false;

	ProcessDollarBit(doc, &result, arguments, SelectBitOperationType_BitAnd,
					 expressionResult, areArgumentsConstant);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Evaluates the output of a $bitNot expression. Via the new operator framework.
 */
void
HandlePreParsedDollarBitNot(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	bson_value_t result;
	result.value_type = BSON_TYPE_INT32;
	result.value.v_int32 = 0;

	bool areArgumentsConstant = false;

	ProcessDollarBit(doc, &result, arguments, SelectBitOperationType_BitNot,
					 expressionResult, areArgumentsConstant);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Evaluates the output of a $bitOr expression. Via the new operator framework.
 */
void
HandlePreParsedDollarBitOr(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	bson_value_t result;
	result.value_type = BSON_TYPE_INT32;
	result.value.v_int32 = 0;

	bool areArgumentsConstant = false;

	ProcessDollarBit(doc, &result, arguments, SelectBitOperationType_BitOr,
					 expressionResult, areArgumentsConstant);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Evaluates the output of a $bitXor expression. Via the new operator framework.
 */
void
HandlePreParsedDollarBitXor(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	bson_value_t result;
	result.value_type = BSON_TYPE_INT32;
	result.value.v_int32 = 0;

	bool areArgumentsConstant = false;

	ProcessDollarBit(doc, &result, arguments, SelectBitOperationType_BitXor,
					 expressionResult, areArgumentsConstant);

	ExpressionResultSetValue(expressionResult, &result);
}


/* Parses the $bitAnd expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarBitAnd(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseDollarBitOperation(argument, data, SelectBitOperationType_BitAnd, context);
}


/* Parses the $bitNot expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarBitNot(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseDollarBitNotOperation(argument, data, context);
}


/* Parses the $bitOr expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarBitOr(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseDollarBitOperation(argument, data, SelectBitOperationType_BitOr, context);
}


/* Parses the $bitXor expression specified in the bson_value_t and stores it in the data argument.
 */
void
ParseDollarBitXor(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseDollarBitOperation(argument, data, SelectBitOperationType_BitXor, context);
}


/* Parses the $bitNot expression specified in the bson_value_t and stores it in the data argument.
 */
static void
ParseDollarBitNotOperation(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context)
{
	const char *operatorName = "$bitNot";

	AggregationExpressionData *arguments = ParseFixedArgumentsForExpression(argument, 1,
																			operatorName,
																			&data->
																			operator.
																			argumentsKind,
																			context);

	if (IsAggregationExpressionConstant(arguments))
	{
		/*Initialize data. If the input array is empty, return 0.*/
		data->value.value_type = BSON_TYPE_INT32;
		data->value.value.v_int32 = 0;

		pgbson *nullDoc = NULL;
		ExpressionResult *nullExpressionResult = NULL;
		ProcessDollarBit(nullDoc, &data->value, arguments,
						 SelectBitOperationType_BitNot, nullExpressionResult, true);
		data->kind = AggregationExpressionKind_Constant;
		pfree(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
	}
}


/* Parses the $bitAnd/$bitOr/$bitXor expression specified in the bson_value_t and stores it in the data argument.
 */
static void
ParseDollarBitOperation(const bson_value_t *argument, AggregationExpressionData *data,
						SelectBitOperationType type,
						ParseAggregationExpressionContext *context)
{
	bool areArgumentsConstant;
	List *arguments = ParseVariableArgumentsForExpression(argument,
														  &areArgumentsConstant,
														  context);

	if (areArgumentsConstant)
	{
		/*Initialize data. If the input array is empty, bitAnd return -1 and bitOr/bitXor return 0.*/
		data->value.value_type = BSON_TYPE_INT32;
		data->value.value.v_int32 = type == SelectBitOperationType_BitAnd ? -1 : 0;

		pgbson *nullDoc = NULL;
		ExpressionResult *nullExpressionResult = NULL;

		ProcessDollarBit(nullDoc, &data->value, arguments, type, nullExpressionResult,
						 areArgumentsConstant);
		data->kind = AggregationExpressionKind_Constant;

		list_free_deep(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*Function that processes the $bitAnd/$bitNot/$bitOr/$bitXor expression specified in the bson_value_t and stores the result in the result argument.
 */
static void
ProcessDollarBit(pgbson *doc, bson_value_t *result, void *arguments,
				 SelectBitOperationType type, ExpressionResult *expressionResult, bool
				 areArgumentsConstant)
{
	const char *operatorName = type == SelectBitOperationType_BitAnd ? "$bitAnd" :
							   type == SelectBitOperationType_BitNot ? "$bitNot" :
							   type == SelectBitOperationType_BitOr ? "$bitOr" :
							   "$bitXor";


	bool isNullOnEmpty = false;

	ExpressionResult childExpression;
	if (type == SelectBitOperationType_BitNot)
	{
		AggregationExpressionData *data = arguments;

		bson_value_t currentElem;
		if (areArgumentsConstant)
		{
			currentElem = data->value;
		}
		else
		{
			childExpression = ExpressionResultCreateChild(expressionResult);
			EvaluateAggregationExpressionData(data, doc, &childExpression,
											  isNullOnEmpty);
			currentElem = childExpression.value;
		}

		if (IsExpressionResultNullOrUndefined(&currentElem))
		{
			result->value_type = BSON_TYPE_NULL;
		}
		else if (BsonTypeIsNumberOrBool(currentElem.value_type))
		{
			if (currentElem.value_type == BSON_TYPE_INT32 ||
				currentElem.value_type == BSON_TYPE_INT64)
			{
				bson_value_t *nullElem = NULL;
				*result = BsonValueBitwiseOperation(&currentElem, nullElem, type);
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH)), errmsg(
							"%s only supports int and long, not: %s.", operatorName,
							BsonTypeName(currentElem.value_type)),
						errdetail_log(
							"%s only supports int and long, not: %s.", operatorName,
							BsonTypeName(currentElem.value_type)));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION28765)), errmsg(
						"%s only supports numeric types, not %s", operatorName,
						BsonTypeName(currentElem.value_type)),
					errdetail_log(
						"%s only supports numeric types, not %s", operatorName,
						BsonTypeName(currentElem.value_type)));
		}
	}
	else
	{
		List *expressionsArray = arguments;
		if (expressionsArray == NIL)
		{
			return;
		}

		bool isFirst = true;
		ListCell *cell;
		foreach(cell, expressionsArray)
		{
			AggregationExpressionData *data = lfirst(cell);
			bson_value_t currentElem;
			if (areArgumentsConstant)
			{
				currentElem = data->value;
			}
			else
			{
				childExpression = ExpressionResultCreateChild(expressionResult);
				EvaluateAggregationExpressionData(data, doc, &childExpression,
												  isNullOnEmpty);
				currentElem = childExpression.value;
			}


			if (IsExpressionResultNullOrUndefined(&currentElem))
			{
				result->value_type = BSON_TYPE_NULL;
				break;
			}

			/* If the current element is an int32/int64, process it according to the operation type. */
			if (currentElem.value_type == BSON_TYPE_INT32 ||
				currentElem.value_type == BSON_TYPE_INT64)
			{
				if (isFirst == true)
				{
					isFirst = false;
					*result = currentElem;
				}
				else
				{
					*result = BsonValueBitwiseOperation(result, &currentElem, type);
				}
			}
			else
			{
				ereport(ERROR, (errcode(ERRCODE_HELIO_TYPEMISMATCH)), errmsg(
							"%s only supports int and long operands.", operatorName),
						errdetail_log(
							"%s only supports int and long operands.", operatorName));
			}

			if (!areArgumentsConstant)
			{
				ExpressionResultReset(&childExpression);
			}
		}
	}
}


/* Function that performs bitwise operations on the bson_value_t type.
 *  Only supports int32 and int64 operands.
 */
static bson_value_t
BsonValueBitwiseOperation(const bson_value_t *left, const bson_value_t *right,
						  SelectBitOperationType type)
{
	bson_value_t resultValue = { 0 };

	/* Handle the bitwise NOT operation, which only requires the 'left' operand. */
	if (type == SelectBitOperationType_BitNot)
	{
		int64_t leftValue = BsonValueAsInt64(left);

		if (left->value_type == BSON_TYPE_INT32)
		{
			resultValue.value_type = BSON_TYPE_INT32;
			resultValue.value.v_int32 = ~leftValue;
		}
		else if (left->value_type == BSON_TYPE_INT64)
		{
			resultValue.value_type = BSON_TYPE_INT64;
			resultValue.value.v_int64 = ~leftValue;
		}
	}
	else /* Handle bitwise AND, OR, and XOR operations, which require both 'left' and 'right' operands. */
	{
		int64_t leftValue = BsonValueAsInt64(left);
		int64_t rightValue = BsonValueAsInt64(right);
		int64_t result = type == SelectBitOperationType_BitAnd ? leftValue & rightValue :
						 type == SelectBitOperationType_BitOr ? leftValue | rightValue :
						 leftValue ^ rightValue;

		/* Perform the bitwise operation and store the result based on the operand type. */
		if (left->value_type == BSON_TYPE_INT64 || right->value_type == BSON_TYPE_INT64)
		{
			resultValue.value_type = BSON_TYPE_INT64;
			resultValue.value.v_int64 = result;
		}
		else
		{
			resultValue.value_type = BSON_TYPE_INT32;
			resultValue.value.v_int32 = result;
		}
	}

	return resultValue;
}
