/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_arithmetic_operators.c
 *
 * Arithmetic Operator expression implementations of BSON.
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
typedef void (*ProcessArithmeticSingleOperand)(const bson_value_t *currentValue,
											   bson_value_t *result);
typedef void (*ProcessArithmeticDualOperands)(void *state, bson_value_t *result);
typedef bool (*ProcessArithmeticVariableOperands)(const bson_value_t *currentValue,
												  void *state,
												  bson_value_t *result, bool
												  isFieldPathExpression);

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
static void ParseArithmeticSingleOperand(const bson_value_t *argument,
										 AggregationExpressionData *data, const
										 char *operatorName,
										 ProcessArithmeticSingleOperand
										 processOperatorFunc,
										 ParseAggregationExpressionContext *context);
static void ParseArithmeticDualOperands(const bson_value_t *argument,
										AggregationExpressionData *data, const
										char *operatorName,
										ProcessArithmeticDualOperands
										processArithmeticOperatorFunc,
										ParseAggregationExpressionContext *context);
static void ParseArithmeticRangeOperands(const bson_value_t *argument,
										 AggregationExpressionData *data, const
										 char *operatorName,
										 ProcessArithmeticDualOperands processOperatorFunc,
										 ParseAggregationExpressionContext *context);
static void ParseArithmeticVariableOperands(const bson_value_t *argument,
											void *state,
											AggregationExpressionData *data,
											ProcessArithmeticVariableOperands
											processOperatorFunc,
											ParseAggregationExpressionContext *context);
static void HandlePreParsedArithmeticSingleOperand(pgbson *doc, void *arguments,
												   ExpressionResult *
												   expressionResult,
												   ProcessArithmeticSingleOperand
												   processOperatorFunc);
static void HandlePreParsedArithmeticDualOperands(pgbson *doc, void *arguments,
												  ExpressionResult *
												  expressionResult,
												  ProcessArithmeticDualOperands
												  processOperatorFunc);
static void HandlePreParsedArithmeticVariableOperands(pgbson *doc, void *arguments,
													  void *state,
													  bson_value_t *result,
													  ExpressionResult *expressionResult,
													  ProcessArithmeticVariableOperands
													  processOperatorFunc);
static bool ProcessDollarAdd(const bson_value_t *currentElement, void *state,
							 bson_value_t *result,
							 bool isFieldPathExpression);
static bool ProcessDollarMultiply(const bson_value_t *currentElement, void *state,
								  bson_value_t *result, bool
								  isFieldPathExpression);
static void ProcessDollarSubtract(void *state, bson_value_t *result);
static void ProcessDollarLog(void *state, bson_value_t *result);
static void ProcessDollarPow(void *state, bson_value_t *result);
static void ProcessDollarMod(void *state, bson_value_t *result);
static void ProcessDollarDivide(void *state, bson_value_t *result);
static void ProcessDollarRound(void *state, bson_value_t *result);
static void ProcessDollarTrunc(void *state, bson_value_t *result);
static void ProcessDollarCeil(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarFloor(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarExp(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarSqrt(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarLog10(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarLn(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarAbs(const bson_value_t *currentValue, bson_value_t *result);
static void ProcessDollarAddAccumulatedResult(void *state, bson_value_t *result);
static void InitializeDualArgumentState(bson_value_t firstValue, bson_value_t secondValue,
										bool hasFieldExpression,
										DualArgumentExpressionState *state);
static bool CheckForDateOverflow(bson_value_t *value);
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
 * Parses an $add expression and sets the parsed data in the data argument.
 * $add is expressed as { "$add": [ <expression1>, <expression2>, ... ] }
 */
void
ParseDollarAdd(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	data->value.value_type = BSON_TYPE_INT32;
	data->value.value.v_int32 = 0;

	DollarAddState state =
	{
		.isDateTimeAdd = false,
		.foundUndefined = false,
	};

	ParseArithmeticVariableOperands(argument, &state, data, ProcessDollarAdd,
									context);

	if (data->kind == AggregationExpressionKind_Constant)
	{
		ProcessDollarAddAccumulatedResult(&state, &data->value);
	}
}


/*
 * Evaluates the output of an $add expression.
 * Since $add is expressed as { "$add": [ <expression1>, <expression2>, ... ] }
 * We evaluate the inner expressions and then return the addition of them.
 */
void
HandlePreParsedDollarAdd(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	bson_value_t result = {
		.value_type = BSON_TYPE_INT32,
		.value.v_int32 = 0
	};


	DollarAddState state =
	{
		.isDateTimeAdd = false,
		.foundUndefined = false,
	};

	HandlePreParsedArithmeticVariableOperands(doc, arguments, &state, &result,
											  expressionResult,
											  ProcessDollarAdd);


	if (result.value_type != BSON_TYPE_NULL)
	{
		ProcessDollarAddAccumulatedResult(&state, &result);
	}

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $multiply expression and sets the parsed data in the data argument.
 * $multiply is expressed as { "$multiply": [ <expression1>, <expression2>, ... ] }
 */
void
ParseDollarMultiply(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	data->value.value_type = BSON_TYPE_INT32;
	data->value.value.v_int32 = 1;

	ParseArithmeticVariableOperands(argument, NULL, data, ProcessDollarMultiply,
									context);
}


/*
 * Evaluates the output of an $multiply expression.
 * Since $multiply is expressed as { "$multiply": [ <expression1>, <expression2>, ... ] }
 * We evaluate the inner expressions and then return the product of them.
 */
void
HandlePreParsedDollarMultiply(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	bson_value_t result =
	{
		value_type: BSON_TYPE_INT32,
		value: { v_int32: 1 }
	};

	HandlePreParsedArithmeticVariableOperands(doc, arguments, NULL, &result,
											  expressionResult,
											  ProcessDollarMultiply);
	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Parses a $subtract expression and sets the parsed data in the data argument.
 * $subtract is expressed as { "$subtract": [ <expression1>, <expression2> ] }
 */
void
ParseDollarSubtract(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context)
{
	ParseArithmeticDualOperands(argument, data, "$subtract",
								ProcessDollarSubtract, context);
}


/*
 * Evaluates the output of a $subtract expression.
 * Since $subtract is expressed as { "$subtract": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and then return the difference of them.
 * $subtract accepts exactly 2 arguments.
 */
void
HandlePreParsedDollarSubtract(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarSubtract);
}


/*
 * Parses a $divide expression and sets the parsed data in the data argument.
 * $divide is expressed as { "$divide": [ <expression1>, <expression2> ] }
 */
void
ParseDollarDivide(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	ParseArithmeticDualOperands(argument, data, "$divide",
								ProcessDollarDivide, context);
}


/*
 * Evaluates the output of a $divide expression.
 * Since $divide is expressed as { "$divide": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and then return the division of them.
 * $divide accepts exactly 2 arguments.
 */
void
HandlePreParsedDollarDivide(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarDivide);
}


/*
 * Evaluates the output of a $mod expression.
 * Since $mod is expressed as { "$mod": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and then return the modulo of them.
 * $mod accepts exactly 2 arguments.
 */
void
ParseDollarMod(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseArithmeticDualOperands(argument, data, "$mod",
								ProcessDollarMod, context);
}


/*
 * Evaluates the output of a $mod expression.
 * $mod is expressed as { "$mod": [ <expression1>, <expression2> ] }
 * We evaluate the inner expressions and set their mod value to the result.
 */
void
HandlePreParsedDollarMod(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarMod);
}


/*
 * Parses a $pow expression and sets the parsed data in the data argument.
 * $pow is expressed as { "$pow": [ <expression1>, <expression2> ] }
 */
void
ParseDollarPow(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseArithmeticDualOperands(argument, data, "$pow",
								ProcessDollarPow, context);
}


/*
 * Evaluates the output of a $pow expression.
 * Since $pow is expressed as { "$pow": [ <number>, <exponent> ] }
 * We evaluate the inner expressions and elevate the number to the requested exponent.
 * $pow accepts exactly two arguments.
 */
void
HandlePreParsedDollarPow(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarPow);
}


/*
 * Parses a $log expression and sets the parsed data in the data argument.
 * $log is expressed as { "$log": [ <expression1>, <expression2> ] }
 */
void
ParseDollarLog(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseArithmeticDualOperands(argument, data, "$log",
								ProcessDollarLog, context);
}


/*
 * Evaluates the output of a $log expression.
 * Since $log is expressed as { "$log": [ <number>, <base> ] }
 * We evaluate the inner expressions and calculate the logarithm in the requested base.
 * $log accepts exactly two arguments.
 */
void
HandlePreParsedDollarLog(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarLog);
}


/*
 * Parses a $round expression and sets the parsed data in the data argument.
 * $round is expressed as { "$round": [<number>, <precision>] }
 * with <precision> being an optional argument.
 */
void
ParseDollarRound(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseArithmeticRangeOperands(argument, data, "$round",
								 ProcessDollarRound, context);
}


/*
 * Evaluates the output of a $round expression.
 * Since $round is expressed as { "$round": [<number>, <precision>] }
 * with <precision> being an optional argument.
 * We evaluate the number expression and precision and calculate the result.
 */
void
HandlePreParsedDollarRound(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarRound);
}


/*
 * Parses a $trunc expression and sets the parsed data in the data argument.
 * $trunc is expressed as { "$trunc": [<number>, <precision>] }
 * with <precision> being an optional argument.
 */
void
ParseDollarTrunc(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseArithmeticRangeOperands(argument, data, "$trunc",
								 ProcessDollarTrunc, context);
}


/*
 * Evaluates the output of a $trunc expression.
 * Since $trunc is expressed as { "$trunc": [<number>, <precision>] }
 * with <precision> being an optional argument.
 * We evaluate the number expression and precision and calculate the result.
 */
void
HandlePreParsedDollarTrunc(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticDualOperands(doc, arguments, expressionResult,
										  ProcessDollarTrunc);
}


/*
 * Parses a $ceil expression and sets the parsed data in the data argument.
 * $ceil is expressed as { "$ceil": <number> }
 * with <precision> being an optional argument.
 */
void
ParseDollarCeil(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$ceil",
								 ProcessDollarCeil, context);
}


/*
 * Evaluates the output of a $ceil expression.
 * Since $ceil is expressed as { "$ceil": <numeric-expression> }
 * We evaluate the inner expression and then return the smallest number
 * greater than or equal to the specified number.
 * $ceil accepts exactly one argument.
 */
void
HandlePreParsedDollarCeil(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarCeil);
}


/*
 * Parses a $floor expression and sets the parsed data in the data argument.
 * $floor is expressed as { "$floor": <numeric-expression> }
 * with <precision> being an optional argument.
 */
void
ParseDollarFloor(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$floor",
								 ProcessDollarFloor, context);
}


/*
 * Evaluates the output of a $floor expression.
 * Since $floor is expressed as { "$floor": <numeric-expression> }
 * We evaluate the inner expression and then return the largest number
 * less than or equal to the specified number.
 * $floor accepts exactly one argument.
 */
void
HandlePreParsedDollarFloor(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarFloor);
}


/*
 * Parses a $exp expression and sets the parsed data in the data argument.
 * $exp is expressed as { "$exp": <numeric-expression> }
 */
void
ParseDollarExp(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$exp",
								 ProcessDollarExp, context);
}


/*
 * Evaluates the output of a $exp expression.
 * Since $exp is expressed as { "$exp": <numeric-expression> }
 * We evaluate the inner expression and then return the result of
 * raising Euler's number to the specified exponent.
 * $exp accepts exactly one argument.
 */
void
HandlePreParsedDollarExp(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarExp);
}


/*
 * Parses a $sqrt expression and sets the parsed data in the data argument.
 * $sqrt is expressed as { "$sqrt": <numeric-expression> }
 * $sqrt accepts exactly one argument.
 */
void
ParseDollarSqrt(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$sqrt",
								 ProcessDollarSqrt, context);
}


/*
 * Evaluates the output of a $sqrt expression.
 * Since $sqrt is expressed as { "$sqrt": <numeric-expression> }
 * We evaluate the inner expression and calculate the sqrt of the evaluated
 * number and set it to the result.
 * $sqrt accepts exactly one argument.
 */
void
HandlePreParsedDollarSqrt(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarSqrt);
}


/*
 * Parses a $log10 expression and sets the parsed data in the data argument.
 * $log10 is expressed as { "$log10": <numeric-expression> }
 * $log10 accepts exactly one argument.
 */
void
ParseDollarLog10(const bson_value_t *argument, AggregationExpressionData *data,
				 ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$log10",
								 ProcessDollarLog10, context);
}


/*
 * Evaluates the output of a $log10 expression.
 * Since $log10 is expressed as { "$log10": <numeric-expression> }
 * We evaluate the inner expression and calculate the log10 of the evaluated
 * number and set it to the result.
 * $log10 accepts exactly one argument.
 */
void
HandlePreParsedDollarLog10(pgbson *doc, void *arguments,
						   ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarLog10);
}


/*
 * Evaluates the output of a $ln expression.
 * Since $ln is expressed as { "$ln": <numeric-expression> }
 * We evaluate the inner expression and calculate the natural logarithm of the evaluated
 * number and set it to the result.
 * $ln accepts exactly one argument.
 */
void
ParseDollarLn(const bson_value_t *argument, AggregationExpressionData *data,
			  ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$ln",
								 ProcessDollarLn, context);
}


/*
 * Evaluates the output of a $ln expression.
 * Since $ln is expressed as { "$ln": <numeric-expression> }
 * We evaluate the inner expression and calculate the natural logarithm of the evaluated
 * number and set it to the result.
 * $ln accepts exactly one argument.
 */
void
HandlePreParsedDollarLn(pgbson *doc, void *arguments,
						ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarLn);
}


/*
 * Parses a abs expression and sets the parsed data in the data argument.
 * abs is expressed as { "abs": <numeric-expression> }
 */
void
ParseDollarAbs(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseArithmeticSingleOperand(argument, data, "$abs",
								 ProcessDollarAbs, context);
}


/*
 * Evaluates the output of a $abs expression.
 * Since $abs is expressed as { "$abs": <numeric-expression> }
 * We evaluate the inner expression and set its absolute value to the result.
 * $abs accepts exactly one argument.
 */
void
HandlePreParsedDollarAbs(pgbson *doc, void *arguments,
						 ExpressionResult *expressionResult)
{
	HandlePreParsedArithmeticSingleOperand(doc, arguments, expressionResult,
										   ProcessDollarAbs);
}


/* Helper to parse arithmetic operators that take strictly single arguments. */
static void
ParseArithmeticSingleOperand(const bson_value_t *argument,
							 AggregationExpressionData *data, const
							 char *operatorName,
							 ProcessArithmeticSingleOperand processOperatorFunc,
							 ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 1;
	AggregationExpressionData *parsedData = ParseFixedArgumentsForExpression(argument,
																			 numOfRequiredArgs,
																			 operatorName,
																			 &data->
																			 operator.
																			 argumentsKind,
																			 context);

	/* If the arguments is constant: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(parsedData))
	{
		processOperatorFunc(&parsedData->value, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		pfree(parsedData);
	}
	else
	{
		data->operator.arguments = parsedData;
	}
}


/* Helper to evaluate pre-parsed expressions of operators that take strictly single operands. */
static void
HandlePreParsedArithmeticSingleOperand(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult,
									   ProcessArithmeticSingleOperand
									   processOperatorFunc)
{
	AggregationExpressionData *argument = (AggregationExpressionData *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(argument, doc, &childResult, isNullOnEmpty);

	bson_value_t currentValue = childResult.value;

	bson_value_t result = { 0 };
	processOperatorFunc(&currentValue, &result);
	ExpressionResultSetValue(expressionResult, &result);
}


/* Helper to parse arithmetic operators that take strictly two arguments. */
static void
ParseArithmeticDualOperands(const bson_value_t *argument,
							AggregationExpressionData *data, const
							char *operatorName,
							ProcessArithmeticDualOperands processOperatorFunc,
							ParseAggregationExpressionContext *context)
{
	int numOfRequiredArgs = 2;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   operatorName,
													   &data->operator.argumentsKind,
													   context);

	AggregationExpressionData *firstArg = list_nth(arguments, 0);
	AggregationExpressionData *secondArg = list_nth(arguments, 1);

	/* If both arguments are constants: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(firstArg) && IsAggregationExpressionConstant(
			secondArg))
	{
		DualArgumentExpressionState state;
		memset(&state, 0, sizeof(DualArgumentExpressionState));

		InitializeDualArgumentState(firstArg->value, secondArg->value, false, &state);
		processOperatorFunc(&state, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
	}
}


/* Helper to evaluate pre-parsed expressions of operators that take strictly two operands. */
static void
HandlePreParsedArithmeticDualOperands(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult,
									  ProcessArithmeticDualOperands
									  processOperatorFunc)
{
	List *argumentList = (List *) arguments;
	AggregationExpressionData *firstArg = list_nth(argumentList, 0);
	AggregationExpressionData *secondArg = list_nth(argumentList, 1);

	bool hasFieldExpression = false;
	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(firstArg, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;
	hasFieldExpression = childResult.isFieldPathExpression;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(secondArg, doc, &childResult, isNullOnEmpty);
	hasFieldExpression = hasFieldExpression || childResult.isFieldPathExpression;

	bson_value_t secondValue = childResult.value;

	bson_value_t result = { 0 };
	DualArgumentExpressionState state;
	memset(&state, 0, sizeof(DualArgumentExpressionState));

	InitializeDualArgumentState(firstValue, secondValue, hasFieldExpression, &state);
	processOperatorFunc(&state, &result);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Helper to parse arithmetic operators with a range ofarguments.
 * Currently used by only $round and $trunc which take one to two arguments.
 */
static void
ParseArithmeticRangeOperands(const bson_value_t *argument,
							 AggregationExpressionData *data, const
							 char *operatorName,
							 ProcessArithmeticDualOperands
							 processOperatorFunc,
							 ParseAggregationExpressionContext *context)
{
	int minNumOfArgs = 1;
	int maxNumOfArgs = 2;

	bson_value_t optionalDefault =
	{
		.value_type = BSON_TYPE_INT32,
		.value.v_int32 = 0
	};

	AggregationExpressionArgumentsKind kind = AggregationExpressionArgumentsKind_Palloc;

	AggregationExpressionData *firstArg;
	AggregationExpressionData *secondArg = ParseRangeArgumentsForExpression(
		&optionalDefault,
		minNumOfArgs,
		maxNumOfArgs,
		operatorName,
		&kind, context);

	List *argumentsList = NIL;
	if (argument->value_type == BSON_TYPE_ARRAY)
	{
		argumentsList = ParseRangeArgumentsForExpression(argument,
														 minNumOfArgs,
														 maxNumOfArgs,
														 operatorName,
														 &data->operator.argumentsKind,
														 context);

		if (argumentsList->length < 2)
		{
			argumentsList = lappend(argumentsList, secondArg);
		}

		firstArg = list_nth(argumentsList, 0);
		secondArg = list_nth(argumentsList, 1);
	}
	else
	{
		firstArg = ParseRangeArgumentsForExpression(argument,
													minNumOfArgs,
													maxNumOfArgs,
													operatorName,
													&data->operator.argumentsKind,
													context);
		argumentsList = list_make2(firstArg, secondArg);
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}

	/* If both arguments are constants: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(firstArg) && IsAggregationExpressionConstant(
			secondArg))
	{
		DualArgumentExpressionState state;
		memset(&state, 0, sizeof(DualArgumentExpressionState));

		InitializeDualArgumentState(firstArg->value, secondArg->value, false, &state);
		processOperatorFunc(&state, &data->value);

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(argumentsList);
	}
	else
	{
		data->operator.arguments = argumentsList;
	}
}


/* Helper to parse arithmetic operators that take variable number of operands. */
void
ParseArithmeticVariableOperands(const bson_value_t *argument,
								void *state,
								AggregationExpressionData *data,
								ProcessArithmeticVariableOperands processOperatorFunc,
								ParseAggregationExpressionContext *context)
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

			bool continueEnumerating = processOperatorFunc(&currentData->value,
														   state,
														   &data->value,
														   false);
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
static void
HandlePreParsedArithmeticVariableOperands(pgbson *doc, void *arguments, void *state,
										  bson_value_t *result,
										  ExpressionResult *expressionResult,
										  ProcessArithmeticVariableOperands
										  processOperatorFunc)
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

		bool continueEnumerating = processOperatorFunc(&currentValue, state, result,
													   childResult.
													   isFieldPathExpression);
		if (!continueEnumerating)
		{
			return;
		}

		idx++;
	}
}


/* Function that processes a single argument for $add and adds it to the current result. */
static bool
ProcessDollarAdd(const bson_value_t *currentElement, void *state, bson_value_t *result,
				 bool isFieldPathExpression)
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
ProcessDollarAddAccumulatedResult(void *state, bson_value_t *result)
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
ProcessDollarMultiply(const bson_value_t *currentElement, void *state,
					  bson_value_t *result,
					  bool isFieldPathExpression)
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
ProcessDollarSubtract(void *state, bson_value_t *result)
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
ProcessDollarDivide(void *state, bson_value_t *result)
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
ProcessDollarMod(void *state, bson_value_t *result)
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
static void
ProcessDollarCeil(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$ceil only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		CeilDecimal128Number(currentValue, result);
	}
	else if (currentValue->value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = ceil(BsonValueAsDouble(currentValue));
	}
	else
	{
		*result = *currentValue;
	}
}


/* Function that calculates the floor of the current element and sets it to the result. */
static void
ProcessDollarFloor(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$floor only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		FloorDecimal128Number(currentValue, result);
	}
	else if (currentValue->value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = floor(BsonValueAsDouble(currentValue));
	}
	else
	{
		*result = *currentValue;
	}
}


/* Function that raises Euler's number to the specified exponent and sets it to the result. */
static void
ProcessDollarExp(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$exp only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		EulerExpDecimal128(currentValue, result);
	}
	else
	{
		/* $exp returns double for non-decimal inputs. */
		double power = BsonValueAsDouble(currentValue);

		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = exp(power);
	}
}


/* Function that validates and calculates the square root of currentValue and sets it to the result. */
static void
ProcessDollarSqrt(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$sqrt only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	bson_value_t argDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(currentValue),
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

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		*result = sqrtResult;
	}
	else
	{
		/* If input is not decimal128 we should return a double. */
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&sqrtResult);
	}
}


/* Function that validates the $log10 argument and calculates its log10 and sets it to the result. */
static void
ProcessDollarLog10(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$log10 only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	bson_value_t argDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(currentValue),
	};

	bool isComparisonValid = false;
	int cmp = CompareBsonDecimal128ToZero(&argDecimal, &isComparisonValid);
	if (isComparisonValid && cmp != 1)
	{
		ereport(ERROR, (errcode(MongoDollarLog10MustBePositiveNumber), errmsg(
							"$log10's argument must be a positive number, but is %s",
							BsonValueToJsonForLogging(currentValue))));
	}

	bson_value_t log10Result;
	Log10Decimal128Number(&argDecimal, &log10Result);

	/* if the input type is decimal128 and not NaN we should return decimal128 */
	if (currentValue->value_type == BSON_TYPE_DECIMAL128 &&
		!IsDecimal128NaN(&log10Result))
	{
		*result = log10Result;
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&log10Result);
	}
}


/* Function that validates the $ln argument and calculates its natural logarithm and sets it to the result. */
static void
ProcessDollarLn(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$ln only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	bson_value_t argDecimal = {
		.value_type = BSON_TYPE_DECIMAL128,
		.value.v_decimal128 = GetBsonValueAsDecimal128(currentValue),
	};

	bool isComparisonValid = false;
	int cmp = CompareBsonDecimal128ToZero(&argDecimal, &isComparisonValid);
	if (isComparisonValid && cmp != 1)
	{
		ereport(ERROR, (errcode(MongoDollarLnMustBePositiveNumber), errmsg(
							"$ln's argument must be a positive number, but is %s",
							BsonValueToJsonForLogging(currentValue))));
	}

	bson_value_t lnResult;
	NaturalLogarithmDecimal128Number(&argDecimal, &lnResult);

	/* if the input type is decimal128 and not NaN we should return decimal128 */
	if (currentValue->value_type == BSON_TYPE_DECIMAL128 &&
		!IsDecimal128NaN(&lnResult))
	{
		*result = lnResult;
	}
	else
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = GetBsonDecimal128AsDouble(&lnResult);
	}
}


/* Function that calculates the result for $log based on the state and sets reset to the value. */
static void
ProcessDollarLog(void *state, bson_value_t *result)
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
ProcessDollarPow(void *state, bson_value_t *result)
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


/* Function that calculates the result for $round. based on the state and result. */
static void
ProcessDollarRound(void *state, bson_value_t *result)
{
	RoundOrTruncateValue(result, (DualArgumentExpressionState *) state,
						 RoundOperation_Round);
}


/* Function that calculates the result for $trunc. */
static void
ProcessDollarTrunc(void *state, bson_value_t *result)
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


/* Function that calculates the result for $abs based on the currentValue and result. */
static void
ProcessDollarAbs(const bson_value_t *currentValue, bson_value_t *result)
{
	if (IsExpressionResultNullOrUndefined(currentValue))
	{
		result->value_type = BSON_TYPE_NULL;
		return;
	}

	if (!BsonValueIsNumber(currentValue))
	{
		ereport(ERROR, (errcode(MongoLocation28765), errmsg(
							"$abs only supports numeric types, not %s",
							BsonTypeName(currentValue->value_type))));
	}

	if (currentValue->value_type == BSON_TYPE_DECIMAL128)
	{
		AbsDecimal128Number(currentValue, result);
	}
	else if (currentValue->value_type == BSON_TYPE_DOUBLE)
	{
		result->value_type = BSON_TYPE_DOUBLE;
		result->value.v_double = fabs(BsonValueAsDouble(currentValue));
	}
	else
	{
		if (currentValue->value_type == BSON_TYPE_INT64 &&
			currentValue->value.v_int64 == INT64_MIN)
		{
			ereport(ERROR, (errcode(MongoDollarAbsCantTakeLongMinValue), errmsg(
								"can't take $abs of long long min")));
		}

		int64_t absValue = llabs(BsonValueAsInt64(currentValue));

		if (currentValue->value_type == BSON_TYPE_INT32 && absValue <= INT32_MAX)
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
}


/* Initializes the state for dual argument expressions. */
static void
InitializeDualArgumentState(bson_value_t firstValue, bson_value_t secondValue, bool
							hasFieldExpression, DualArgumentExpressionState *state)
{
	state->firstArgument = firstValue;
	state->secondArgument = secondValue;
	state->hasFieldExpression = hasFieldExpression;
	state->hasNullOrUndefined = IsExpressionResultNullOrUndefined(&firstValue) ||
								IsExpressionResultNullOrUndefined(&secondValue);
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
