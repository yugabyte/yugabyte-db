/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_trigonometry_operators.c
 *
 * Trigonometry Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <math.h>

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "types/decimal128.h"
#include "query/bson_dollar_operators.h"
#include "utils/mongo_errors.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */
typedef enum TrigOperatorType
{
	TrigOperatorType_DegreesToRadians = 0,
	TrigOperatorType_RadiansToDegrees = 1,
	TrigOperatorType_Sin = 2,
	TrigOperatorType_Cos = 3,
	TrigOperatorType_Tan = 4,
	TrigOperatorType_Sinh = 5,
	TrigOperatorType_Cosh = 6,
	TrigOperatorType_Tanh = 7,
	TrigOperatorType_Asin = 8,
	TrigOperatorType_Acos = 9,
	TrigOperatorType_Atan = 10,
	TrigOperatorType_Atan2 = 11,
	TrigOperatorType_Asinh = 12,
	TrigOperatorType_Acosh = 14,
	TrigOperatorType_Atanh = 15,
} TrigOperatorType;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void HandlePreParsedTrigOperatorSingleOperand(pgbson *doc,
													 void *arguments,
													 ExpressionResult *expressionResult,
													 const char *operatorName,
													 TrigOperatorType operator);
static void ParseTrigOperatorSingleOperand(const bson_value_t *argument,
										   AggregationExpressionData *data,
										   const char *operatorName,
										   TrigOperatorType operatorType);
static void ProcessDegreesToRadiansOperator(const bson_value_t *currentValue,
											bson_value_t *result);
static void ProcessRadiansToDegreesOperator(const bson_value_t *currentValue,
											bson_value_t *result);
static void ProcessTrigOperatorSingleOperand(bson_value_t *currentValue,
											 bson_value_t *result,
											 const char *operatorName, TrigOperatorType
											 operatorType);
static void ProcessSinOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessCosOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessTanOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessSinhOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessCoshOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessTanhOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessAsinOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessAcosOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessAtanOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessAtan2Operator(const bson_value_t *currentValue, const
								 bson_value_t *currentValue2, bson_value_t *result);
static void ProcessAsinhOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessAcoshOperator(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessAtanhOperator(const bson_value_t *currentValue, bson_value_t *result);


/*
 * Parses an $degreesToRadians expression and sets the parsed data in the data argument.
 * $degreesToRadians is expressed as { "$degreesToRadians": <expression> }
 */
void
ParseDollarDegreesToRadians(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$degreesToRadians",
								   TrigOperatorType_DegreesToRadians);
}


/*
 * Evaluates the output of an $degreesToRadians expression.
 * $degreesToRadians is expressed as { "$degreesToRadians": <expression> }
 * We evaluate the inner expression and set its degrees value to the result.
 */
void
HandlePreParsedDollarDegreesToRadians(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult,
											 "$degreesToRadians",
											 TrigOperatorType_DegreesToRadians);
}


/*
 * Parses an $radiansToDegrees expression and sets the parsed data in the data argument.
 * $radiansToDegrees is expressed as { "$radiansToDegrees": <expression> }
 */
void
ParseDollarRadiansToDegrees(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$radiansToDegrees",
								   TrigOperatorType_RadiansToDegrees);
}


/*
 * Evaluates the output of an $radiansToDegrees expression.
 * $radiansToDegrees is expressed as { "$radiansToDegrees": <expression> }
 * We evaluate the inner expression and set its degrees value to the result.
 */
void
HandlePreParsedDollarRadiansToDegrees(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult,
											 "$radiansToDegrees",
											 TrigOperatorType_RadiansToDegrees);
}


/*
 * Parses an $sin expression and sets the parsed data in the data argument.
 * $sin is expressed as { "$sin": <expression> }
 */
void
ParseDollarSin(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$sin", TrigOperatorType_Sin);
}


/*
 * Evaluates the output of an $sin expression.
 * $sin is expressed as { "$sin": <expression> }
 * We evaluate the inner expression and set its sine value to the result.
 */
void
HandlePreParsedDollarSin(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$sin",
											 TrigOperatorType_Sin);
}


/*
 * Parses an $cos expression and sets the parsed data in the data argument.
 * $cos is expressed as { "$cos": <expression> }
 */
void
ParseDollarCos(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$cos", TrigOperatorType_Cos);
}


/*
 * Evaluates the output of an $cos expression.
 * $cos is expressed as { "$cos": <expression> }
 * We evaluate the inner expression and set its cosine value to the result.
 */
void
HandlePreParsedDollarCos(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$cos",
											 TrigOperatorType_Cos);
}


/*
 * Parses an $tan expression and sets the parsed data in the data argument.
 * $tan is expressed as { "$tan": <expression> }
 */
void
ParseDollarTan(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$tan", TrigOperatorType_Tan);
}


/*
 * Evaluates the output of an $tan expression.
 * $tan is expressed as { "$tan": <expression> }
 * We evaluate the inner expression and set its tangent value to the result.
 */
void
HandlePreParsedDollarTan(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$tan",
											 TrigOperatorType_Tan);
}


/*
 * Parses an $sinh expression and sets the parsed data in the data argument.
 * $sinh is expressed as { "$sinh": <expression> }
 */
void
ParseDollarSinh(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$sinh", TrigOperatorType_Sinh);
}


/*
 * Evaluates the output of an $sinh expression.
 * $sinh is expressed as { "$sinh": <expression> }
 * We evaluate the inner expression and set its hyperbolic sine value to the result.
 */
void
HandlePreParsedDollarSinh(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$sinh",
											 TrigOperatorType_Sinh);
}


/*
 * Parses an $cosh expression and sets the parsed data in the data argument.
 * $cosh is expressed as { "$cosh": <expression> }
 */
void
ParseDollarCosh(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$cosh", TrigOperatorType_Cosh);
}


/*
 * Evaluates the output of an $cosh expression.
 * $cosh is expressed as { "$cosh": <expression> }
 * We evaluate the inner expression and set its hyperbolic cosine value to the result.
 */
void
HandlePreParsedDollarCosh(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$cosh",
											 TrigOperatorType_Cosh);
}


/*
 * Parses an $tanh expression and sets the parsed data in the data argument.
 * $tanh is expressed as { "$tanh": <expression> }
 */
void
ParseDollarTanh(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$tanh", TrigOperatorType_Tanh);
}


/*
 * Evaluates the output of an $tanh expression.
 * $tanh is expressed as { "$tanh": <expression> }
 * We evaluate the inner expression and set its hyperbolic tangent value to the result.
 */
void
HandlePreParsedDollarTanh(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$tanh",
											 TrigOperatorType_Tanh);
}


/*
 * Parses an $asin expression and sets the parsed data in the data argument.
 * $asin is expressed as { "$asin": <expression> }
 */
void
ParseDollarAsin(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$asin", TrigOperatorType_Asin);
}


/*
 * Evaluates the output of an $asin expression.
 * $asin is expressed as { "$asin": <expression> }
 * We evaluate the inner expression and set its arcsine value to the result.
 */
void
HandlePreParsedDollarAsin(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$asin",
											 TrigOperatorType_Asin);
}


/*
 * Parses an $acos expression and sets the parsed data in the data argument.
 * $acos is expressed as { "$acos": <expression> }
 */
void
ParseDollarAcos(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$acos", TrigOperatorType_Acos);
}


/*
 * Evaluates the output of an $acos expression.
 * $acos is expressed as { "$acos": <expression> }
 * We evaluate the inner expression and set its arccosine value to the result.
 */
void
HandlePreParsedDollarAcos(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$acos",
											 TrigOperatorType_Acos);
}


/*
 * Parses an $atan expression and sets the parsed data in the data argument.
 * $atan is expressed as { "$atan": <expression> }
 */
void
ParseDollarAtan(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$atan", TrigOperatorType_Atan);
}


/*
 * Evaluates the output of an $atan expression.
 * $atan is expressed as { "$atan": <expression> }
 * We evaluate the inner expression and set its arctangent value to the result.
 */
void
HandlePreParsedDollarAtan(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$atan",
											 TrigOperatorType_Atan);
}


/*
 * Parses an $atan2 expression and sets the parsed data in the data argument.
 * $atan2 is expressed as { "$atan2": [ <expression1>, <expression2> ] }
 */
void
ParseDollarAtan2(const bson_value_t *argument, AggregationExpressionData *data)
{
	int numOfRequiredArgs = 2;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   "$atan2",
													   &data->operator.argumentsKind);

	AggregationExpressionData *first = list_nth(arguments, 0);
	AggregationExpressionData *second = list_nth(arguments, 1);

	/* If both arguments are constants: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(first) && IsAggregationExpressionConstant(second))
	{
		ProcessAtan2Operator(&first->value, &second->value, &data->value);
		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
	}
}


/*
 * Evaluates the output of an $atan2 expression.
 * $atan2 is expressed as { "$atan2": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and set the arctangent value to the result.
 */
void
HandlePreParsedDollarAtan2(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	List *argumentList = (List *) arguments;
	AggregationExpressionData *first = list_nth(argumentList, 0);
	AggregationExpressionData *second = list_nth(argumentList, 1);

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(first, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(second, doc, &childResult, isNullOnEmpty);

	bson_value_t secondValue = childResult.value;

	bson_value_t result = { 0 };
	ProcessAtan2Operator(&firstValue, &secondValue, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses an $asinh expression and sets the parsed data in the data argument.
 * $asinh is expressed as { "$asinh": <expression> }
 */
void
ParseDollarAsinh(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$asinh", TrigOperatorType_Asinh);
}


/*
 * Evaluates the output of an $asinh expression.
 * $asinh is expressed as { "$asinh": <expression> }
 * We evaluate the inner expression and set its arctangent value to the result.
 */
void
HandlePreParsedDollarAsinh(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$asinh",
											 TrigOperatorType_Asinh);
}


/*
 * Parses an $acosh expression and sets the parsed data in the data argument.
 * $acosh is expressed as { "$acosh": <expression> }
 */
void
ParseDollarAcosh(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$acosh", TrigOperatorType_Acosh);
}


/*
 * Evaluates the output of an $acosh expression.
 * $acosh is expressed as { "$acosh": <expression> }
 * We evaluate the inner expression and set its arctangent value to the result.
 */
void
HandlePreParsedDollarAcosh(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$acosh",
											 TrigOperatorType_Acosh);
}


/*
 * Parses an $atanh expression and sets the parsed data in the data argument.
 * $atanh is expressed as { "$atanh": <expression> }
 */
void
ParseDollarAtanh(const bson_value_t *argument, AggregationExpressionData *data)
{
	ParseTrigOperatorSingleOperand(argument, data, "$atanh", TrigOperatorType_Atanh);
}


/*
 * Evaluates the output of an $atanh expression.
 * $atanh is expressed as { "$atanh": <expression> }
 * We evaluate the inner expression and set its arctangent value to the result.
 */
void
HandlePreParsedDollarAtanh(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedTrigOperatorSingleOperand(doc, arguments, expressionResult, "$atanh",
											 TrigOperatorType_Atanh);
}


/*
 * Evaluates the output of a trig expression with 1 operand.
 */
static void
HandlePreParsedTrigOperatorSingleOperand(pgbson *doc,
										 void *arguments,
										 ExpressionResult *expressionResult,
										 const char *operatorName,
										 TrigOperatorType operatorType)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t currentValue = childResult.value;

	bson_value_t result = { 0 };
	ProcessTrigOperatorSingleOperand(&currentValue, &result, operatorName, operatorType);

	ExpressionResultSetValue(expressionResult, &result);
}


static void
ParseTrigOperatorSingleOperand(const bson_value_t *argument,
							   AggregationExpressionData *data,
							   const char *operatorName,
							   TrigOperatorType operatorType)
{
	int numOfRequiredArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfRequiredArgs,
																			 operatorName,
																			 &data->
																			 operator.
																			 argumentsKind);

	/* If argument is a constant: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the argument as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(parsedData))
	{
		ProcessTrigOperatorSingleOperand(&parsedData->value, &data->value, operatorName,
										 operatorType);
		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
	}
	else
	{
		data->operator.arguments = parsedData;
	}
}


/* Function that validates the $sin argument and sets the sine value to the result. */
static void
ProcessTrigOperatorSingleOperand(bson_value_t *currentValue,
								 bson_value_t *result,
								 const char *operatorName,
								 TrigOperatorType operatorType)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"%s only supports numeric types, not %s",
							operatorName,
							BsonTypeName(currentValue->value_type))));
	}

	switch (operatorType)
	{
		case TrigOperatorType_DegreesToRadians:
		{
			ProcessDegreesToRadiansOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_RadiansToDegrees:
		{
			ProcessRadiansToDegreesOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Sin:
		{
			ProcessSinOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Cos:
		{
			ProcessCosOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Tan:
		{
			ProcessTanOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Sinh:
		{
			ProcessSinhOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Cosh:
		{
			ProcessCoshOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Tanh:
		{
			ProcessTanhOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Asin:
		{
			ProcessAsinOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Acos:
		{
			ProcessAcosOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Atan:
		{
			ProcessAtanOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Asinh:
		{
			ProcessAsinhOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Acosh:
		{
			ProcessAcoshOperator(currentValue, result);
			break;
		}

		case TrigOperatorType_Atanh:
		{
			ProcessAtanhOperator(currentValue, result);
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Invalid trigonometry aggregation operator %d",
								   operatorType)));
			break;
		}
	}
}


/* Function that validates the $degreesToRadians argument and sets the result. */
static void
ProcessDegreesToRadiansOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	bson_value_t pi = { .value_type = BSON_TYPE_DOUBLE, .value.v_double = M_PI };
	bson_value_t factor = { .value_type = BSON_TYPE_DOUBLE, .value.v_double = 180.0 };

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t piDecimal128, factorDecimal128;
		piDecimal128.value_type = BSON_TYPE_DECIMAL128;
		piDecimal128.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&pi);

		factorDecimal128.value_type = BSON_TYPE_DECIMAL128;
		factorDecimal128.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&factor);

		result->value_type = BSON_TYPE_DECIMAL128;

		DivideDecimal128Numbers(&piDecimal128, &factorDecimal128, result);
		MultiplyDecimal128Numbers(result, currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = pi.value.v_double / factor.value.v_double;
		MultiplyWithFactorAndUpdate(result, currentValue, true);
	}
}


/* Function that validates the $radiansToDegrees argument and sets the result. */
static void
ProcessRadiansToDegreesOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	bson_value_t pi = { .value_type = BSON_TYPE_DOUBLE, .value.v_double = M_PI };
	bson_value_t factor = { .value_type = BSON_TYPE_DOUBLE, .value.v_double = 180.0 };

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t piDecimal128, factorDecimal128;
		piDecimal128.value_type = BSON_TYPE_DECIMAL128;
		piDecimal128.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&pi);

		factorDecimal128.value_type = BSON_TYPE_DECIMAL128;
		factorDecimal128.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&factor);

		result->value_type = BSON_TYPE_DECIMAL128;

		DivideDecimal128Numbers(&factorDecimal128, &piDecimal128, result);
		MultiplyDecimal128Numbers(result, currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = factor.value.v_double / pi.value.v_double;
		MultiplyWithFactorAndUpdate(result, currentValue, true);
	}
}


/* Function that validates the $sin argument and sets the sine value to the result. */
static void
ProcessSinOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsBsonValueInfinity(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $sin to %s, value must be in (-inf,inf)",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		SinDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = sin((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $cos argument and sets the cosine value to the result. */
static void
ProcessCosOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsBsonValueInfinity(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $cos to %s, value must be in (-inf,inf)",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		CosDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = cos((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $tan argument and sets the tangent value to the result. */
static void
ProcessTanOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsBsonValueInfinity(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $tan to %s, value must be in (-inf,inf)",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		TanDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = tan((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $sinh argument and sets the hyperbolic sine value to the result. */
static void
ProcessSinhOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		SinhDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = sinh((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $cosh argument and sets the hyperbolic cosine value to the result. */
static void
ProcessCoshOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		CoshDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = cosh((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $tanh argument and sets the hyperbolic tangent value to the result. */
static void
ProcessTanhOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		TanhDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = tanh((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $asin argument and sets the arcsine value to the result. */
static void
ProcessAsinOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (BsonValueAsDouble(currentValue) < -1.0 || BsonValueAsDouble(currentValue) > 1.0)
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $asin to %s, value must be in [-1,1]",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AsinDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = asin((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $acos argument and sets the arccosine value to the result. */
static void
ProcessAcosOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (BsonValueAsDouble(currentValue) < -1.0 || BsonValueAsDouble(currentValue) > 1.0)
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $acos to %s, value must be in [-1,1]",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AcosDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = acos((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $atan argument and sets the arctangent value to the result. */
static void
ProcessAtanOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AtanDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = atan((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $atan2 argument and sets the hyperbolic arctan2 value to the result. */
static void
ProcessAtan2Operator(const bson_value_t *firstValue, const bson_value_t *secondValue,
					 bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(firstValue) ||
		IsExpressionResultNullOrUndefined(secondValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(firstValue) || !BsonValueIsNumber(secondValue))
	{
		if (firstValue->value_type == BSON_TYPE_DOUBLE)
		{
			ereport(ERROR, (errcode(MongoLocation51045), errmsg(
								"$atan2 only supports numeric types, not %s and %s",
								BsonTypeName(firstValue->value_type), BsonTypeName(
									secondValue->value_type))));
		}
		else
		{
			ereport(ERROR, (errcode(MongoLocation51044), errmsg(
								"$atan2 only supports numeric types, not %s and %s",
								BsonTypeName(firstValue->value_type), BsonTypeName(
									secondValue->value_type))));
		}
	}

	if (firstValue->value_type == BSON_TYPE_DECIMAL128 || secondValue->value_type ==
		BSON_TYPE_DECIMAL128)
	{
		/* Try type promotion for both. */
		bson_value_t fstDecimal128, secDecimal128;
		fstDecimal128.value_type = BSON_TYPE_DECIMAL128;
		fstDecimal128.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(firstValue);

		secDecimal128.value_type = BSON_TYPE_DECIMAL128;
		secDecimal128.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(secondValue);

		result->value_type = BSON_TYPE_DECIMAL128;
		Atan2Decimal128Numbers(&fstDecimal128, &secDecimal128, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = atan2((BsonValueAsDouble(firstValue)),
									   (BsonValueAsDouble(secondValue)));
	}
}


/* Function that validates the $asinh argument and sets the arctangent value to the result. */
static void
ProcessAsinhOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AsinhDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = asinh((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $acosh argument and sets the arctangent value to the result. */
static void
ProcessAcoshOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (BsonValueAsDouble(currentValue) < 1.0)
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $acosh to %s, value must be in [1,inf]",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AcoshDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = acosh((BsonValueAsDouble(currentValue)));
	}
}


/* Function that validates the $atanh argument and sets the arctangent value to the result. */
static void
ProcessAtanhOperator(const bson_value_t *currentValue, bson_value_t *result)
{
	if (BsonValueAsDouble(currentValue) < -1.0 || BsonValueAsDouble(currentValue) > 1.0)
	{
		ereport(ERROR, (errcode(MongoLocation50989), errmsg(
							"cannot apply $atanh to %s, value must be in [-1,1]",
							BsonValueToJsonForLogging(currentValue)
							)));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AtanhDecimal128Number(currentValue, result);
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = atanh((BsonValueAsDouble(currentValue)));
	}
}
