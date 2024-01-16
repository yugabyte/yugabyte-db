/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_arithmetic_operators.c
 *
 * Arithmetic Operator expression implementations of BSON.
 * See also: https://www.mongodb.com/docs/manual/reference/operator/aggregation/#arithmetic-expression-operators
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

/* State for $add operator */
typedef struct DollarAddState
{
	/* Whether we've found a date time expression argument. */
	bool isDateTimeAdd;

	/* Whether an expression resulted in undefined. */
	bool foundUndefined;
} DollarAddState;


typedef enum RoundOperation
{
	RoundOperation_Round = 0,
	RoundOperation_Trunc = 1,
} RoundOperation;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static bool CheckForDateOverflow(bson_value_t *value);
static bool ProcessDollarAddElement(bson_value_t *result,
									const bson_value_t *currentElement,
									bool isFieldPathExpression, void *state);
static void ProcessDollarAddAccumulatedResult(bson_value_t *result, void *state);
static bool ProcessDollarMultiplyElement(bson_value_t *result,
										 const bson_value_t *currentElement,
										 bool isFieldPathExpression, void *state);
static void ProcessDollarSubstract(bson_value_t *result, void *state);
static void ProcessDollarDivide(bson_value_t *result, void *state);
static void ProcessDollarMod(bson_value_t *result, void *state);
static void ProcessDollarLog(bson_value_t *result, void *state);
static void ProcessDollarPow(bson_value_t *result, void *state);
static void ProcessDollarRound(bson_value_t *result, void *state);
static void ProcessDollarTrunc(bson_value_t *result, void *state);
static bool ProcessDollarCeilElement(bson_value_t *result,
									 const bson_value_t *currentElement,
									 bool isFieldPathExpression, void *state);
static bool ProcessDollarFloorElement(bson_value_t *result,
									  const bson_value_t *currentElement,
									  bool isFieldPathExpression, void *state);
static bool ProcessDollarExpElement(bson_value_t *result,
									const bson_value_t *currentElement,
									bool isFieldPathExpression, void *state);
static bool ProcessDollarSqrtElement(bson_value_t *result, const
									 bson_value_t *currentElement,
									 bool isFieldPathExpression, void *state);
static bool ProcessDollarLog10Element(bson_value_t *result, const
									  bson_value_t *currentElement,
									  bool isFieldPathExpression, void *state);
static bool ProcessDollarLnElement(bson_value_t *result, const
								   bson_value_t *currentElement,
								   bool isFieldPathExpression, void *state);
static bool ProcessDollarAbsElement(bson_value_t *result, const
									bson_value_t *currentElement,
									bool isFieldPathExpression, void *state);
static void ThrowIfNotNumeric(const bson_value_t *value, const char *operatorName,
							  bool isFieldPathExpression);
static void ThrowIfNotNumericOrDate(const bson_value_t *value, const char *operatorName,
									bool isFieldPathExpression);
static void ThrowInvalidTypesForDollarSubtract(bson_value_t minuend,
											   bson_value_t subtrahend);
static int CompareBsonDecimal128ToZero(const bson_value_t *value,
									   bool *isComparisonValid);
static void RoundOrTruncateValue(bson_value_t *result,
								 DualArgumentExpressionState *dualState,
								 RoundOperation operationType);

/*
 * Evaluates the output of an $add expression.
 * Since $add is expressed as { "$add": [ <expression1>, <expression2>, ... ] }
 * We evaluate the inner expressions and then return the addition of them.
 */
void
HandleDollarAdd(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	DollarAddState state =
	{
		.isDateTimeAdd = false,
		.foundUndefined = false,
	};

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarAddElement,
		.processExpressionResultFunc = ProcessDollarAddAccumulatedResult,
		.state = &state,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_INT32;
	startValue.value.v_int32 = 0;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult, &startValue,
									 &context);
}


void
HandleDollarMultiply(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarMultiplyElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_INT32;
	startValue.value.v_int32 = 1;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult, &startValue,
									 &context);
}


/*
 * Evaluates the output of a $subtract expression.
 * Since $subtract is expressed as { "$subtract": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and then return the difference of them.
 * $subtract accepts exactly 2 arguments.
 */
void
HandleDollarSubtract(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarSubstract,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$subtract", &context);
}


/*
 * Evaluates the output of a $divide expression.
 * Since $divide is expressed as { "$divide": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and then return the division of them.
 * $divide accepts exactly 2 arguments.
 */
void
HandleDollarDivide(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarDivide,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$divide", &context);
}


/*
 * Evaluates the output of a $mod expression.
 * Since $mod is expressed as { "$mod": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and then return the modulo of them.
 * $mod accepts exactly 2 arguments.
 */
void
HandleDollarMod(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarMod,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$mod", &context);
}


/*
 * Evaluates the output of a $ceil expression.
 * Since $ceil is expressed as { "$ceil": <numeric-expression> }
 * We evaluate the inner expression and then return the smallest number
 * greater than or equal to the specified number.
 * $ceil accepts exactly one argument.
 */
void
HandleDollarCeil(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarCeilElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$ceil", &context);
}


/*
 * Evaluates the output of a $floor expression.
 * Since $floor is expressed as { "$floor": <numeric-expression> }
 * We evaluate the inner expression and then return the largest number
 * less than or equal to the specified number.
 * $floor accepts exactly one argument.
 */
void
HandleDollarFloor(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarFloorElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$floor", &context);
}


/*
 * Evaluates the output of a $exp expression.
 * Since $exp is expressed as { "$exp": <numeric-expression> }
 * We evaluate the inner expression and then return the result of
 * raising Euler's number to the specified exponent.
 * $exp accepts exactly one argument.
 */
void
HandleDollarExp(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarExpElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$exp", &context);
}


/*
 * Evaluates the output of a $sqrt expression.
 * Since $sqrt is expressed as { "$sqrt": <numeric-expression> }
 * We evaluate the inner expression and calculate the sqrt of the evaluated
 * number and set it to the result.
 * $sqrt accepts exactly one argument.
 */
void
HandleDollarSqrt(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarSqrtElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$sqrt", &context);
}


/*
 * Evaluates the output of a $log10 expression.
 * Since $log10 is expressed as { "$log10": <numeric-expression> }
 * We evaluate the inner expression and calculate the log10 of the evaluated
 * number and set it to the result.
 * $log10 accepts exactly one argument.
 */
void
HandleDollarLog10(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarLog10Element,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$log10", &context);
}


/*
 * Evaluates the output of a $ln expression.
 * Since $ln is expressed as { "$ln": <numeric-expression> }
 * We evaluate the inner expression and calculate the natural logarithm of the evaluated
 * number and set it to the result.
 * $ln accepts exactly one argument.
 */
void
HandleDollarLn(pgbson *doc, const bson_value_t *operatorValue,
			   ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarLnElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$ln", &context);
}


/*
 * Evaluates the output of a $log expression.
 * Since $log is expressed as { "$log": [ <number>, <base> ] }
 * We evaluate the inner expressions and calculate the logarithm in the requested base.
 * $log accepts exactly two arguments.
 */
void
HandleDollarLog(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarLog,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$log", &context);
}


/*
 * Evaluates the output of a $pow expression.
 * Since $pow is expressed as { "$pow": [ <number>, <exponent> ] }
 * We evaluate the inner expressions and elevate the number to the requested exponent.
 * $pow accepts exactly two arguments.
 */
void
HandleDollarPow(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarPow,
		.state = &state,
	};

	int numberOfRequiredArgs = 2;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$pow", &context);
}


/*
 * Evaluates the output of a $abs expression.
 * Since $abs is expressed as { "$abs": <numeric-expression> }
 * We evaluate the inner expression and set its absolute value to the result.
 * $abs accepts exactly one argument.
 */
void
HandleDollarAbs(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarAbsElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$abs", &context);
}


/*
 * Evaluates the output of a $round expression.
 * Since $round is expressed as { "$round": [<number>, <precision>] }
 * with <precision> being an optional argument.
 * We evaluate the number expression and precision and calculate the result.
 */
void
HandleDollarRound(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	bson_value_t precisionDefault;
	precisionDefault.value_type = BSON_TYPE_INT32;
	precisionDefault.value.v_int32 = 0;
	state.secondArgument = precisionDefault;

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarRound,
		.state = &state,
	};

	int minRequiredArgs = 1;
	int maxRequiredArgs = 2;
	HandleRangedArgumentExpression(doc, operatorValue, expressionResult,
								   minRequiredArgs, maxRequiredArgs, "$round", &context);
}


/*
 * Evaluates the output of a $trunc expression.
 * Since $trunc is expressed as { "$trunc": [<number>, <precision>] }
 * with <precision> being an optional argument.
 * We evaluate the number expression and precision and calculate the result.
 */
void
HandleDollarTrunc(pgbson *doc, const bson_value_t *operatorValue,
				  ExpressionResult *expressionResult)
{
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	bson_value_t precisionDefault;
	precisionDefault.value_type = BSON_TYPE_INT32;
	precisionDefault.value.v_int32 = 0;
	state.secondArgument = precisionDefault;

	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDualArgumentElement,
		.processExpressionResultFunc = ProcessDollarTrunc,
		.state = &state,
	};

	int minRequiredArgs = 1;
	int maxRequiredArgs = 2;
	HandleRangedArgumentExpression(doc, operatorValue, expressionResult,
								   minRequiredArgs, maxRequiredArgs, "$trunc", &context);
}


/* Function that processes a single argument for $add and adds it to the current result. */
static bool
ProcessDollarAddElement(bson_value_t *result, const bson_value_t *currentElement,
						bool isFieldPathExpression, void *state)
{
	DollarAddState *addState = (DollarAddState *) state;

	if (currentElement->value_type == BSON_TYPE_NULL)
	{
		/* break argument processing and return null */
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	/* if undefined is found, we should return null when there is not
	 * a date overflow */
	if (IsExpressionResultUndefined(currentElement))
	{
		addState->foundUndefined = true;
		return true;
	}

	ThrowIfNotNumericOrDate(currentElement, "$add", isFieldPathExpression);

	bson_value_t evaluatedValue = *currentElement;
	if (evaluatedValue.value_type == BSON_TYPE_DATE_TIME)
	{
		if (addState->isDateTimeAdd)
		{
			ereport(ERROR, (errcode(MongoDollarAddOnlyOneDate), errmsg(
								"only one date allowed in an $add expression")));
		}

		addState->isDateTimeAdd = true;
		int64_t dateTime = evaluatedValue.value.v_datetime;
		evaluatedValue.value_type = BSON_TYPE_INT64;
		evaluatedValue.value.v_int64 = dateTime;
	}

	/* If it is a date time addition and any decimal128 values are added and
	 * result in an overflow, we need to return NaN. */
	if (addState->isDateTimeAdd &&
		(result->value_type == BSON_TYPE_DECIMAL128 ||
		 evaluatedValue.value_type == BSON_TYPE_DECIMAL128))
	{
		if (result->value_type != BSON_TYPE_DECIMAL128)
		{
			bson_decimal128_t decimal128 = GetBsonValueAsDecimal128Quantized(result);
			result->value_type = BSON_TYPE_DECIMAL128;
			result->value.v_decimal128 = decimal128;
		}
		else if (evaluatedValue.value_type != BSON_TYPE_DECIMAL128)
		{
			bson_decimal128_t decimal128 =
				GetBsonValueAsDecimal128Quantized(&evaluatedValue);
			evaluatedValue.value_type = BSON_TYPE_DECIMAL128;
			evaluatedValue.value.v_decimal128 = decimal128;
		}

		Decimal128Result addDecimal128Result =
			AddDecimal128Numbers(result, &evaluatedValue, result);

		if (addDecimal128Result != Decimal128Result_Success ||
			!IsDecimal128Finite(result) || !IsDecimal128InInt64Range(result))
		{
			/* If we get an error or a decimal outside of the Int64 range we should return an invalid date. */
			result->value_type = BSON_TYPE_DATE_TIME;
			result->value.v_datetime = INT64_MAX;
			return false;
		}

		return true;
	}


	bool overflowedFromInt64Ignore = false;
	AddNumberToBsonValue(result, &evaluatedValue,
						 &overflowedFromInt64Ignore);

	return true;
}


/* Function that validates the final state before returning the result for $add. */
static void
ProcessDollarAddAccumulatedResult(bson_value_t *result, void *state)
{
	DollarAddState *addState = (DollarAddState *) state;
	if (addState->isDateTimeAdd)
	{
		if (CheckForDateOverflow(result))
		{
			ereport(ERROR, (errcode(MongoOverflow), errmsg(
								"date overflow in $add")));
		}
		else if (!addState->foundUndefined &&
				 result->value_type != BSON_TYPE_DATE_TIME)
		{
			bool throwIfFailed = false;
			int64_t dateTime =
				BsonValueAsInt64WithRoundingMode(result,
												 ConversionRoundingMode_NearestEven,
												 throwIfFailed);
			result->value_type = BSON_TYPE_DATE_TIME;
			result->value.v_datetime = dateTime;
		}
	}

	if (addState->foundUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
	}
}


/* Function that processes a single element for the $multiply operator. */
static bool
ProcessDollarMultiplyElement(bson_value_t *result, const bson_value_t *currentElement,
							 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		/* Break argument enumeration and set result to null */
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	ThrowIfNotNumeric(currentElement, "$multiply", isFieldPathExpression);

	bool convertInt64OverflowToDouble = true;
	MultiplyWithFactorAndUpdate(result, currentElement, convertInt64OverflowToDouble);

	return true;
}


/* Function that calculates the result for $substract based on the result and state. */
static void
ProcessDollarSubstract(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	if (dualState->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t minuend = dualState->firstArgument;
	bson_value_t subtrahend = dualState->secondArgument;

	bool isDateOperation = minuend.value_type == BSON_TYPE_DATE_TIME;
	if (!BsonValueIsNumber(&minuend) && !isDateOperation)
	{
		ThrowInvalidTypesForDollarSubtract(minuend, subtrahend);
	}

	/* The subtrahend can only be of date type if the minuend is. */
	if (!BsonValueIsNumber(&subtrahend) &&
		(!isDateOperation || subtrahend.value_type != BSON_TYPE_DATE_TIME))
	{
		ThrowInvalidTypesForDollarSubtract(minuend, subtrahend);
	}

	*result = minuend;

	bool overflowedFromInt64Ignore = false;
	if (isDateOperation)
	{
		result->value_type = BSON_TYPE_INT64;
		result->value.v_int64 = minuend.value.v_datetime;

		bool isDateTimeResult = true;
		if (subtrahend.value_type == BSON_TYPE_DATE_TIME)
		{
			int64_t dateTime = subtrahend.value.v_datetime;
			subtrahend.value_type = BSON_TYPE_INT64;
			subtrahend.value.v_int64 = dateTime;

			/* When both operands are date time it returns the difference in ms. */
			isDateTimeResult = false;
		}

		SubtractNumberFromBsonValue(result, &subtrahend, &overflowedFromInt64Ignore);

		/* Native mongo doesn't check for date overflow in $subtract. */
		if (isDateTimeResult)
		{
			int64_t dateTime = BsonValueAsInt64(result);
			result->value_type = BSON_TYPE_DATE_TIME;
			result->value.v_datetime = dateTime;
		}
		else if (result->value_type != BSON_TYPE_INT64)
		{
			int64_t value = BsonValueAsInt64(result);
			result->value_type = BSON_TYPE_INT64;
			result->value.v_int64 = value;
		}
	}
	else
	{
		SubtractNumberFromBsonValue(result, &subtrahend, &overflowedFromInt64Ignore);
	}
}


/* Function that calculates the result for $divide based on the result and state. */
static void
ProcessDollarDivide(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	if (dualState->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t divisorValue = dualState->secondArgument;
	*result = dualState->firstArgument;

	if (!BsonValueIsNumber(result) || !BsonValueIsNumber(&divisorValue))
	{
		if (dualState->hasFieldExpression)
		{
			ereport(ERROR, (errcode(MongoTypeMismatch), errmsg(
								"$divide only supports numeric types")));
		}
		else
		{
			ereport(ERROR, (errcode(MongoTypeMismatch), errmsg(
								"$divide only supports numeric types, not %s and %s",
								BsonTypeName(result->value_type),
								BsonTypeName(divisorValue.value_type))));
		}
	}

	/* Native mongo returns double for $divide unless one operand is decimal128 */
	if (result->value_type == BSON_TYPE_DECIMAL128 ||
		divisorValue.value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t dividend;
		bson_value_t divisor;
		if (divisorValue.value_type == BSON_TYPE_DECIMAL128)
		{
			dividend.value_type = BSON_TYPE_DECIMAL128;
			dividend.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(result);

			divisor = divisorValue;
		}
		else
		{
			divisor.value_type = BSON_TYPE_DECIMAL128;
			divisor.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&divisorValue);

			dividend = *result;
		}

		if (IsDecimal128Zero(&divisor))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"can't $divide by zero")));
		}

		result->value_type = BSON_TYPE_DECIMAL128;

		DivideDecimal128Numbers(&dividend, &divisor, result);
	}
	else
	{
		double dividend = BsonValueAsDouble(result);
		double divisor = BsonValueAsDouble(&divisorValue);

		if (divisor == 0.0)
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"can't $divide by zero")));
		}

		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = dividend / divisor;
	}
}


/* Function that calculates the result for $mod based on the result and state. */
static void
ProcessDollarMod(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	if (dualState->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t divisorValue = dualState->secondArgument;
	bson_value_t dividendValue = dualState->firstArgument;

	if (!BsonValueIsNumber(&dividendValue) || !BsonValueIsNumber(&divisorValue))
	{
		if (dualState->hasFieldExpression)
		{
			ereport(ERROR, (errcode(MongoDollarModOnlyNumeric), errmsg(
								"$mod only supports numeric types")));
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarModOnlyNumeric), errmsg(
								"$mod only supports numeric types, not %s and %s",
								BsonTypeName(dividendValue.value_type),
								BsonTypeName(divisorValue.value_type))));
		}
	}

	if ((divisorValue.value_type == BSON_TYPE_DECIMAL128 &&
		 IsDecimal128Zero(&divisorValue)) ||
		(BsonValueAsDouble(&divisorValue) == 0.0))
	{
		ereport(ERROR, (errcode(MongoDollarModByZeroProhibited), errmsg(
							"can't $mod by zero")));
	}

	bson_value_t remainder;
	bool validateInputs = false;
	GetRemainderFromModBsonValues(&dividendValue, &divisorValue, validateInputs,
								  &remainder);

	*result = remainder;
}


/* Function that calculates the ceil of the current element and sets it to the result. */
static bool
ProcessDollarCeilElement(bson_value_t *result, const bson_value_t *currentElement,
						 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$ceil only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	if (currentElement->value_type == BSON_TYPE_DECIMAL128)
	{
		CeilDecimal128Number(currentElement, result);
	}
	else if (currentElement->value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = ceil(BsonValueAsDouble(currentElement));
	}
	else
	{
		*result = *currentElement;
	}

	return true;
}


/* Function that calculates the floor of the current element and sets it to the result. */
static bool
ProcessDollarFloorElement(bson_value_t *result, const bson_value_t *currentElement,
						  bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$floor only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	if (currentElement->value_type == BSON_TYPE_DECIMAL128)
	{
		FloorDecimal128Number(currentElement, result);
	}
	else if (currentElement->value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = floor(BsonValueAsDouble(currentElement));
	}
	else
	{
		*result = *currentElement;
	}

	return true;
}


/* Function that raises Euler's number to the specified exponent and sets it to the result. */
static bool
ProcessDollarExpElement(bson_value_t *result, const bson_value_t *currentElement,
						bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$exp only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	if (currentElement->value_type == BSON_TYPE_DECIMAL128)
	{
		EulerExpDecimal128(currentElement, result);
	}
	else
	{
		/* $exp returns double for non-decimal inputs. */
		double power = BsonValueAsDouble(currentElement);

		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = exp(power);
	}

	return true;
}


/* Function that validates and calculates the square root of currentElement and sets it to the result. */
static bool
ProcessDollarSqrtElement(bson_value_t *result, const bson_value_t *currentElement,
						 bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$sqrt only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	bson_value_t argDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(currentElement),
	};

	bool isComparisonValid = false;
	int cmp = CompareBsonDecimal128ToZero(&argDecimal, &isComparisonValid);
	if (isComparisonValid && cmp == -1)
	{
		ereport(ERROR, (errcode(MongoDollarSqrtGreaterOrEqualToZero), errmsg(
							"$sqrt's argument must be greater than or equal to 0")));
	}

	bson_value_t sqrtResult;
	SqrtDecimal128Number(&argDecimal, &sqrtResult);

	if (currentElement->value_type == BSON_TYPE_DECIMAL128)
	{
		*result = sqrtResult;
	}
	else
	{
		/* If input is not decimal128 we should return a double. */
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&sqrtResult);
	}

	return true;
}


/* Function that validates the $log10 argument and calculates its log10 and sets it to the result. */
static bool
ProcessDollarLog10Element(bson_value_t *result, const bson_value_t *currentElement,
						  bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$log10 only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	bson_value_t argDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(currentElement),
	};

	bool isComparisonValid = false;
	int cmp = CompareBsonDecimal128ToZero(&argDecimal, &isComparisonValid);
	if (isComparisonValid && cmp != 1)
	{
		ereport(ERROR, (errcode(MongoDollarLog10MustBePositiveNumber), errmsg(
							"$log10's argument must be a positive number, but is %s",
							BsonValueToJsonForLogging(currentElement))));
	}

	bson_value_t log10Result;
	Log10Decimal128Number(&argDecimal, &log10Result);

	/* if the input type is decimal128 and not NaN we should return decimal128 */
	if (currentElement->value_type == BSON_TYPE_DECIMAL128 &&
		!IsDecimal128NaN(&log10Result))
	{
		*result = log10Result;
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&log10Result);
	}

	return true;
}


/* Function that validates the $ln argument and calculates its natural logarithm and sets it to the result. */
static bool
ProcessDollarLnElement(bson_value_t *result, const bson_value_t *currentElement,
					   bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$ln only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	bson_value_t argDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(currentElement),
	};

	bool isComparisonValid = false;
	int cmp = CompareBsonDecimal128ToZero(&argDecimal, &isComparisonValid);
	if (isComparisonValid && cmp != 1)
	{
		ereport(ERROR, (errcode(MongoDollarLnMustBePositiveNumber), errmsg(
							"$ln's argument must be a positive number, but is %s",
							BsonValueToJsonForLogging(currentElement))));
	}

	bson_value_t lnResult;
	NaturalLogarithmDecimal128Number(&argDecimal, &lnResult);

	/* if the input type is decimal128 and not NaN we should return decimal128 */
	if (currentElement->value_type == BSON_TYPE_DECIMAL128 &&
		!IsDecimal128NaN(&lnResult))
	{
		*result = lnResult;
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&lnResult);
	}

	return true;
}


/* Function that calculates the result for $log. */
static void
ProcessDollarLog(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	if (dualState->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t numberValue = dualState->firstArgument;
	bson_value_t baseValue = dualState->secondArgument;

	if (!BsonValueIsNumber(&numberValue))
	{
		ereport(ERROR, (errcode(MongoDollarLogArgumentMustBeNumeric), errmsg(
							"$log's argument must be numeric, not %s",
							BsonTypeName(numberValue.value_type))));
	}

	if (!BsonValueIsNumber(&baseValue))
	{
		ereport(ERROR, (errcode(MongoDollarLogBaseMustBeNumeric), errmsg(
							"$log's base must be numeric, not %s",
							BsonTypeName(baseValue.value_type))));
	}

	if (numberValue.value_type == BSON_TYPE_DECIMAL128 ||
		baseValue.value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t numberDecimal = {
			.value_type = BSON_TYPE_DECIMAL128,
			.value.v_decimal128 = GetBsonValueAsDecimal128(&numberValue),
		};

		bson_value_t baseDecimal = {
			.value_type = BSON_TYPE_DECIMAL128,
			.value.v_decimal128 = GetBsonValueAsDecimal128(&baseValue),
		};

		/* If any of the values are NaN we should return a double NaN, rather
		 * than a decimal128 NaN. */
		if (IsDecimal128NaN(&numberDecimal) || IsDecimal128NaN(&baseDecimal))
		{
			result->value_type = BSON_TYPE_DOUBLE;
			result->value.v_double = NAN;
			return;
		}

		bool isComparisonValid = false;
		int cmp = CompareBsonDecimal128ToZero(&numberDecimal, &isComparisonValid);
		if (isComparisonValid && cmp != 1)
		{
			ereport(ERROR, (errcode(MongoDollarLogNumberMustBePositive), errmsg(
								"$log's argument must be a positive number, but is %s",
								BsonValueToJsonForLogging(&numberValue))));
		}

		bson_value_t oneValue = {
			.value_type = BSON_TYPE_INT32,
			.value.v_int32 = 1,
		};

		oneValue.value.v_decimal128 = GetBsonValueAsDecimal128(&oneValue);
		oneValue.value_type = BSON_TYPE_DECIMAL128;

		cmp = CompareBsonDecimal128(&baseDecimal, &oneValue, &isComparisonValid);
		if (isComparisonValid && cmp != 1)
		{
			ereport(ERROR, (errcode(MongoDollarLogBaseMustBeGreaterThanOne), errmsg(
								"$log's base must be a positive number not equal to 1, but is %s",
								BsonValueToJsonForLogging(&baseValue))));
		}

		LogDecimal128Number(&numberDecimal, &baseDecimal, result);
	}
	else
	{
		double number = BsonValueAsDouble(&numberValue);
		double base = BsonValueAsDouble(&baseValue);
		result->value_type = BSON_TYPE_DOUBLE;

		if (isnan(number) || isnan(number))
		{
			result->value.v_double = NAN;
			return;
		}

		if (number <= 0.0)
		{
			ereport(ERROR, (errcode(MongoDollarLogNumberMustBePositive), errmsg(
								"$log's argument must be a positive number, but is %s",
								BsonValueToJsonForLogging(&numberValue))));
		}

		if (base <= 1.0)
		{
			ereport(ERROR, (errcode(MongoDollarLogBaseMustBeGreaterThanOne), errmsg(
								"$log's base must be a positive number not equal to 1, but is %s",
								BsonValueToJsonForLogging(&baseValue))));
		}

		/* log of a specific base need to go through base conversion which can be calculated
		 * with log10 or natural logarithm.
		 * 1. logB(number) = log10(number) / log10(base)
		 * 2. logB(number) = logn(number) / logn(base)
		 * However using log10 can be more precise in the decimal digits in some cases
		 * but in this case we use natural logarithm to match native mongo as JS Test depend
		 * on that behavior.
		 */
		result->value.v_double = log(number) / log(base);
	}
}


/* Function that calculates the result for $pow. */
static void
ProcessDollarPow(bson_value_t *result, void *state)
{
	DualArgumentExpressionState *dualState = (DualArgumentExpressionState *) state;

	if (dualState->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t baseValue = dualState->firstArgument;
	bson_value_t exponentValue = dualState->secondArgument;

	if (!BsonValueIsNumber(&baseValue))
	{
		ereport(ERROR, (errcode(MongoDollarPowBaseMustBeNumeric), errmsg(
							"$pow's base must be numeric, not %s",
							BsonTypeName(baseValue.value_type))));
	}

	if (!BsonValueIsNumber(&exponentValue))
	{
		ereport(ERROR, (errcode(MongoDollarPowExponentMustBeNumeric), errmsg(
							"$pow's exponent must be numeric, not %s",
							BsonTypeName(exponentValue.value_type))));
	}

	/* We use decimal128 to calculate pow to not loose precision and return the
	 * exact result always. */
	bson_value_t baseDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(&baseValue),
	};

	bson_value_t exponentDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(&exponentValue),
	};

	bool isComparisonValid = false;
	int cmp = CompareBsonDecimal128ToZero(&exponentDecimal, &isComparisonValid);

	if (IsDecimal128Zero(&baseDecimal) && isComparisonValid && cmp == -1)
	{
		ereport(ERROR, (errcode(MongoDollarPowExponentInvalidForZeroBase), errmsg(
							"$pow cannot take a base of 0 and a negative exponent")));
	}

	bson_value_t decimalPowResult;
	PowDecimal128Number(&baseDecimal, &exponentDecimal, &decimalPowResult);

	/* input had decimal128, so we return a decimal128 result. */
	if (baseValue.value_type == BSON_TYPE_DECIMAL128 ||
		exponentValue.value_type == BSON_TYPE_DECIMAL128)
	{
		*result = decimalPowResult;
	}
	else
	{
		double base = BsonValueAsDouble(&baseValue);
		double exponent = BsonValueAsDouble(&exponentValue);

		/* If exponent is negative and base is not -1, 0 or 1, we should return a double. */
		bool forceDoubleResult = exponent < 0 &&
								 (base < -1 || base > 1);

		/* if any of the values are doubles or the result can't fit on an int64, return double. */
		if (forceDoubleResult ||
			baseValue.value_type == BSON_TYPE_DOUBLE ||
			exponentValue.value_type == BSON_TYPE_DOUBLE ||
			!IsDecimal128InInt64Range(&decimalPowResult))
		{
			result->value_type = BSON_TYPE_DOUBLE;
			result->value.v_double = GetBsonDecimal128AsDouble(&decimalPowResult);
			return;
		}

		if (baseValue.value_type == BSON_TYPE_INT32 &&
			exponentValue.value_type == BSON_TYPE_INT32 &&
			IsDecimal128InInt32Range(&decimalPowResult))
		{
			result->value_type = BSON_TYPE_INT32;
			result->value.v_int32 = GetBsonDecimal128AsInt32(&decimalPowResult,
															 ConversionRoundingMode_NearestEven);
			return;
		}

		/* We should return long. */
		result->value_type = BSON_TYPE_INT64;
		result->value.v_int64 = GetBsonDecimal128AsInt64(&decimalPowResult,
														 ConversionRoundingMode_NearestEven);
	}
}


/* Function that calculates the result for $round. */
static void
ProcessDollarRound(bson_value_t *result, void *state)
{
	RoundOrTruncateValue(result, (DualArgumentExpressionState *) state,
						 RoundOperation_Round);
}


/* Function that calculates the result for $trunc. */
static void
ProcessDollarTrunc(bson_value_t *result, void *state)
{
	RoundOrTruncateValue(result, (DualArgumentExpressionState *) state,
						 RoundOperation_Trunc);
}


/* Performs the $round or $trunc operator with the arguments in the state value and sets it to the result. */
static void
RoundOrTruncateValue(bson_value_t *result, DualArgumentExpressionState *dualState,
					 RoundOperation operationType)
{
	const char *operatorName = operationType == RoundOperation_Round ? "$round" :
							   "$trunc";

	if (dualState->hasNullOrUndefined)
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	bson_value_t number = dualState->firstArgument;
	bson_value_t precision = dualState->secondArgument;

	if (!BsonValueIsNumber(&number))
	{
		ereport(ERROR, (errcode(MongoDollarRoundFirstArgMustBeNumeric), errmsg(
							"%s only supports numeric types, not %s",
							operatorName, BsonTypeName(number.value_type))));
	}

	bool throwIfFailed = true;
	long precisionAsLong = BsonValueAsInt64WithRoundingMode(&precision,
															ConversionRoundingMode_Floor,
															throwIfFailed);

	/* In native mongo, it validates first if the precision value can be converted to long. */
	if (!IsBsonValueFixedInteger(&precision))
	{
		ereport(ERROR, (errcode(MongoDollarRoundPrecisionMustBeIntegral), errmsg(
							"precision argument to  %s must be a integral value",
							operatorName)));
	}

	if (precisionAsLong < -20 || precisionAsLong > 100)
	{
		ereport(ERROR, (errcode(MongoDollarRoundPrecisionOutOfRange), errmsg(
							"cannot apply %s with precision value %ld value must be in [-20, 100]",
							operatorName, precisionAsLong)));
	}

	if (precisionAsLong >= 0 &&
		(number.value_type == BSON_TYPE_INT64 ||
		 number.value_type == BSON_TYPE_INT32))
	{
		/* if number to round is int32 or int64 and precision is positive,
		 * we should just return the number. */
		*result = number;
		return;
	}

	bson_value_t decimal128Number = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(&number),
	};

	bson_value_t operationResult;
	if (operationType == RoundOperation_Round)
	{
		RoundDecimal128Number(&decimal128Number, precisionAsLong, &operationResult);
	}
	else
	{
		TruncDecimal128Number(&decimal128Number, precisionAsLong, &operationResult);
	}

	if (number.value_type == BSON_TYPE_DECIMAL128)
	{
		*result = operationResult;
	}
	else if (number.value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&operationResult);
	}
	else if (number.value_type == BSON_TYPE_INT32 &&
			 IsDecimal128InInt32Range(&operationResult))
	{
		result->value_type = BSON_TYPE_INT32;
		result->value.v_int32 = GetBsonDecimal128AsInt32(&operationResult,
														 ConversionRoundingMode_Floor);
	}
	else
	{
		if (!IsDecimal128InInt64Range(&operationResult))
		{
			ereport(ERROR, (errcode(MongoDollarRoundOverflowInt64),
							errmsg(
								"invalid conversion from Decimal128 result in %s resulting from arguments: [%s, %s]",
								operatorName, BsonValueToJsonForLogging(&number),
								BsonValueToJsonForLogging(&precision)),
							errhint(
								"invalid conversion from Decimal128 result in %s resulting from argument type: [%s, %s]",
								operatorName, BsonTypeName(number.value_type),
								BsonTypeName(precision.value_type))));
		}

		result->value_type = BSON_TYPE_INT64;
		result->value.v_int64 = GetBsonDecimal128AsInt64(&operationResult,
														 ConversionRoundingMode_Floor);
	}
}


/* Function that validates the $abs argument and sets the absolute value to the result. */
static bool
ProcessDollarAbsElement(bson_value_t *result, const bson_value_t *currentElement,
						bool isFieldPathExpression, void *state)
{
	if (IsExpressionResultNullOrUndefined(currentElement))
	{
		result->value_type = BSON_TYPE_NULL;
		return false;
	}

	if (!BsonValueIsNumber(currentElement))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$abs only supports numeric types, not %s",
							BsonTypeName(currentElement->value_type))));
	}

	if (currentElement->value_type == BSON_TYPE_DECIMAL128)
	{
		AbsDecimal128Number(currentElement, result);
	}
	else if (currentElement->value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = fabs(BsonValueAsDouble(currentElement));
	}
	else
	{
		if (currentElement->value_type == BSON_TYPE_INT64 &&
			currentElement->value.v_int64 == INT64_MIN)
		{
			ereport(ERROR, (errcode(MongoDollarAbsCantTakeLongMinValue), errmsg(
								"can't take $abs of long long min")));
		}

		int64_t absValue = llabs(BsonValueAsInt64(currentElement));

		if (currentElement->value_type == BSON_TYPE_INT32 && absValue <= INT32_MAX)
		{
			result->value_type = BSON_TYPE_INT32;
			result->value.v_int32 = (int32_t) absValue;
		}
		else
		{
			result->value_type = BSON_TYPE_INT64;
			result->value.v_int64 = absValue;
		}
	}

	return true;
}


/* Checks if we overflowed int64 or the value is NaN, which for a date, is an overflow in mongo. */
static bool
CheckForDateOverflow(bson_value_t *value)
{
	/* If we got int64 operands on a date that caused
	 * overflow, AddNumberToBsonValue will coerce to double.
	 * So the only way to have an overflow is if the result is a double. */
	if (value->value_type == BSON_TYPE_DOUBLE)
	{
		double doubleValue = value->value.v_double;
		return isnan(doubleValue) ||
			   doubleValue <= (double) INT64_MIN ||
			   doubleValue >= (double) INT64_MAX;
	}

	return false;
}


/* Throws error for $subtract when value types are incorrect. */
static void
ThrowInvalidTypesForDollarSubtract(bson_value_t minuend, bson_value_t subtrahend)
{
	ereport(ERROR, (errcode(MongoTypeMismatch), errmsg(
						"can't $subtract %s from %s",
						BsonTypeName(subtrahend.value_type),
						BsonTypeName(minuend.value_type))));
}


/* Throws if the value is not numeric or a date value. */
static void
ThrowIfNotNumericOrDate(const bson_value_t *value, const char *operatorName,
						bool isFieldPathExpression)
{
	if (!BsonValueIsNumber(value) && value->value_type != BSON_TYPE_DATE_TIME)
	{
		/* Mongo emits a different error message if the value is a field/operator expression
		 * or just a constant value. */
		if (!isFieldPathExpression)
		{
			/* TODO: when we move to 6.1 the error code is TypeMismatch */
			ereport(ERROR, (errcode(MongoDollarAddNumericOrDateTypes), errmsg(
								"%s only supports numeric or date types, not %s",
								operatorName,
								BsonTypeName(value->value_type))));
		}
		else
		{
			/* TODO: when we move to 6.1 the error code is TypeMismatch */
			ereport(ERROR, (errcode(MongoDollarAddNumericOrDateTypes), errmsg(
								"only numbers and dates are allowed in %s expression",
								operatorName)));
		}
	}
}


/* Throws if the value is not numeric value. */
static void
ThrowIfNotNumeric(const bson_value_t *value, const char *operatorName,
				  bool isFieldPathExpression)
{
	if (!BsonValueIsNumber(value))
	{
		/* Mongo emits a different error message if the value is a field/operator expression
		 * or just a constant value. */
		if (!isFieldPathExpression)
		{
			ereport(ERROR, (errcode(MongoTypeMismatch), errmsg(
								"%s only supports numeric types, not %s",
								operatorName,
								BsonTypeName(value->value_type))));
		}
		else
		{
			ereport(ERROR, (errcode(MongoTypeMismatch), errmsg(
								"only numbers are allowed in an %s expression",
								operatorName)));
		}
	}
}


/* Compares the current decimal 128 value to decimal128 zero and returns an int:
 * 1 => Greater than zero
 * 0 => Equal to zero
 * -1 => Smaller than zero
 */
static int
CompareBsonDecimal128ToZero(const bson_value_t *value, bool *isComparisonValid)
{
	bson_decimal128_t decimal128Zero = {
		.high = 0,
		.low = 0,
	};

	bson_value_t decimalZeroValue;
	decimalZeroValue.value_type = BSON_TYPE_DECIMAL128;
	decimalZeroValue.value.v_decimal128 = decimal128Zero;

	return CompareBsonDecimal128(value, &decimalZeroValue, isComparisonValid);
}
