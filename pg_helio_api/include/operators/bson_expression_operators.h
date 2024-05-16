/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_expression_operators.h
 *
 * Common declarations of the bson expression operators.
 *
 *-------------------------------------------------------------------------
 */

#include "utils/mongo_errors.h"
#include "operators/bson_expression.h"

#ifndef BSON_EXPRESSION_OPERATORS_H
#define BSON_EXPRESSION_OPERATORS_H

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */

/*
 * Wrapper that tracks the lifetime of inner temporary objects
 * created during expression evaluation that will be freed
 * when expression evaluation is complete.
 */
typedef struct ExpressionLifetimeTracker
{
	List *itemsToFree;
} ExpressionLifetimeTracker;

/* Private state tracked by the ExpressionResult. Not to be used
 * by operators or other implementations
 */
typedef struct ExpressionResultPrivate
{
	/* the writer if one is requested and is initialized with one. */
	pgbson_element_writer writer;

	/* Scratch space for the elementWriter if needed */
	pgbson_writer baseWriter;

	/* whether or not baseWriter is initialized */
	bool hasBaseWriter;

	/* whether or not the value has been set already */
	bool valueSet;

	struct ExpressionLifetimeTracker *tracker;

	/* The context containing variables available for the current expression being evaluated. */
	ExpressionVariableContext variableContext;
} ExpressionResultPrivate;

/*
 * structure to manage expression results in operator evaluations.
 * DO NOT access these fields directly. Use the ExpressionResult* functions.
 * Use ExpressionResultCreateChild() to initialize expression results.
 */
typedef struct ExpressionResult
{
	/* The final expression result. */
	bson_value_t value;

	/* whether the result is a writer or a value. */
	bool isExpressionWriter;

	/* whether the expression result value was set from a field path expression or not.
	 * i.e, "$<field>" -> "{$project: {a: "$b.c" }}" in this case "$b.c" is a field path expression. */
	bool isFieldPathExpression;

	ExpressionResultPrivate expressionResultPrivate;
} ExpressionResult;

/* Hook function that is called for every evaluated expression
 * in the argument array for an operator, with the current accumulated result.
 * Return true to continue enumeration and false to return the current result. */
typedef bool (*ProcessExpressionArgumentFunc)(bson_value_t *result,
											  const bson_value_t *currentValue,
											  bool isFieldPathExpression, void *state);

/* Hook function that is called after all arguments are processed in case some
 * special processing is needed for the result before setting the expression result */
typedef void (*ProcessExpressionResultFunc)(bson_value_t *result, void *state);

/* There are some operators that throw a custom error code and message when the wrong
 * number of args is provided by the user. So this hook function is called if provided
 * when that is the case, if not provided, we throw the common error using ThrowExpressionTakesExactlyNArgs.
 * Note that this callback must always throw, if not an Assert will fail. */
typedef void (*ThrowErrorInvalidNumberOfArgsFunc)(const char *operatorName, int
												  requiredArgs, int providedArgs);

/* The context that specifies the hook functions for a specific operator */
typedef struct ExpressionArgumentHandlingContext
{
	/* Function called for every argument that needs special processing. */
	ProcessExpressionArgumentFunc processElementFunc;

	/* Function called after all arguments are processed. */
	ProcessExpressionResultFunc processExpressionResultFunc;

	/* Function called if the wrong number of args is parsed for the given operator. */
	ThrowErrorInvalidNumberOfArgsFunc throwErrorInvalidNumberOfArgsFunc;

	/* Custom state passed to the hook functions. */
	void *state;
} ExpressionArgumentHandlingContext;

/* State for operators that have two arguments used to apply the
 * operation to the result. i.e: $divide, $substract. */
typedef struct DualArgumentExpressionState
{
	/* Whether the first operand has been processed or not. */
	bool isFirstProcessed;

	/* The first argument. */
	bson_value_t firstArgument;

	/* The second argument. */
	bson_value_t secondArgument;

	/* Whether any of the arguments was null or undefined. */
	bool hasNullOrUndefined;

	/* Whether any of the arguments was a field expression ("$a") or not. */
	bool hasFieldExpression;
} DualArgumentExpressionState;

/* State for $slice, $range which takes either 2 or 3 arguments */
typedef struct ThreeArgumentExpressionState
{
	/* The first argument */
	bson_value_t firstArgument;

	/* The Second argument */
	bson_value_t secondArgument;

	/* The Third argument */
	bson_value_t thirdArgument;

	/* Whether any of the arguments was null or undefined. */
	bool hasNullOrUndefined;

	/* number of args which are parsed */
	int totalProcessedArgs;
} ThreeArgumentExpressionState;

/* State for $indexOfBytes, $indexOfCP which takes either 2 or 4 arguments */
typedef struct FourArgumentExpressionState
{
	/* The first argument */
	bson_value_t firstArgument;

	/* The Second argument */
	bson_value_t secondArgument;

	/* The Third argument */
	bson_value_t thirdArgument;

	/* The Fourth argument */
	bson_value_t fourthArgument;

	/* Whether any of the arguments was null or undefined. */
	bool hasNullOrUndefined;

	/* number of args which are parsed */
	int totalProcessedArgs;
} FourArgumentExpressionState;

/* Type that holds information about a timezone.
 * If this is used to represent a timezone ID, it should be created
 * with the ParseTimezone method to make sure the ID is correct. */
typedef struct ExtensionTimezone
{
	/* Specifies if the timezone is a UTC offset in milliseconds or not.
	 * If not it is a timezone ID, i.e America/Los_Angeles. */
	bool isUtcOffset;
	union
	{
		/* The UTC offset in milliseconds if isUtcOffset == true. */
		int64_t offsetInMs;

		/* The timezone ID if isUtcOffset == false. */
		const char *id;
	};
} ExtensionTimezone;

/* Type to specify the case for date and timestamp types for
 * GetDateStringWithDefaultFormat and GetTimestampStringWithDefaultFormat methods */
typedef enum DateStringFormatCase
{
	/* For e.g.: Jan, Feb*/
	DateStringFormatCase_CamelCase,

	/* For e.g.: JAN, FEB*/
	DateStringFormatCase_UpperCase,

	/* For e.g.: jan, feb*/
	DateStringFormatCase_LowerCase,
} DateStringFormatCase;

/* --------------------------------------------------------- */
/* Shared functions */
/* --------------------------------------------------------- */

pgbson_element_writer * ExpressionResultGetElementWriter(ExpressionResult *context);
void ExpressionResultSetValue(ExpressionResult *expressionResult,
							  const bson_value_t *value);
void ExpressionResultSetValueFromWriter(ExpressionResult *expressionResult);
void EvaluateExpression(pgbson *document, const bson_value_t *expressionElement,
						ExpressionResult *expressionResult, bool isNullOnEmpty);
void EvaluateAggregationExpressionData(const AggregationExpressionData *expressionData,
									   pgbson *document,
									   ExpressionResult *expressionResult, bool
									   isNullOnEmpty);
bson_value_t EvaluateExpressionAndGetValue(pgbson *doc, const
										   bson_value_t *expression,
										   ExpressionResult *expressionResult,
										   bool isNullOnEmpty);
ExpressionResult ExpressionResultCreateChild(ExpressionResult *parent);
void ExpressionResultReset(ExpressionResult *expressionResult);
void ExpressionResultSetVariable(ExpressionResult *expressionResult, StringView
								 variableName, const bson_value_t *value);
void ExpressionResultOverrideSingleVariableValue(ExpressionResult *expressionResult, const
												 bson_value_t *value);
void ValidateVariableName(StringView name);

/* Operator handlers definition */
void HandleDollarAbs(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarAdd(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarAllElementsTrue(pgbson *doc, const bson_value_t *operatorValue,
								 ExpressionResult *expressionResult);
void HandleDollarAnd(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarAnyElementTrue(pgbson *doc, const bson_value_t *operatorValue,
								ExpressionResult *expressionResult);
void HandleDollarArrayElemAt(pgbson *doc, const bson_value_t *operatorValue,
							 ExpressionResult *expressionResult);
void HandleDollarArrayToObject(pgbson *doc, const bson_value_t *operatorValue,
							   ExpressionResult *expressionResult);
void HandleDollarCeil(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarConcat(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarCond(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarConcatArrays(pgbson *doc, const bson_value_t *operatorValue,
							  ExpressionResult *expressionResult);
void HandleDollarConvert(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult);
void HandleDollarDateToParts(pgbson *doc, const bson_value_t *operatorValue,
							 ExpressionResult *expressionResult);
void HandleDollarDateToString(pgbson *doc, const bson_value_t *operatorValue,
							  ExpressionResult *expressionResult);
void HandleDollarDayOfMonth(pgbson *doc, const bson_value_t *operatorValue,
							ExpressionResult *expressionResult);
void HandleDollarDayOfWeek(pgbson *doc, const bson_value_t *operatorValue,
						   ExpressionResult *expressionResult);
void HandleDollarDayOfYear(pgbson *doc, const bson_value_t *operatorValue,
						   ExpressionResult *expressionResult);
void HandleDollarDivide(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarExp(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarFilter(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarFirst(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarFloor(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarHour(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarIfNull(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarIn(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult);
void HandleDollarIndexOfBytes(pgbson *doc, const bson_value_t *operatorValue,
							  ExpressionResult *expressionResult);
void HandleDollarIndexOfCP(pgbson *doc, const bson_value_t *operatorValue,
						   ExpressionResult *expressionResult);
void HandleDollarIsArray(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult);
void HandleDollarIsNumber(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarIsoDayOfWeek(pgbson *doc, const bson_value_t *operatorValue,
							  ExpressionResult *expressionResult);
void HandleDollarIsoWeek(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult);
void HandleDollarIsoWeekYear(pgbson *doc, const bson_value_t *operatorValue,
							 ExpressionResult *expressionResult);
void HandleDollarLast(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarLiteral(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult);
void HandleDollarLn(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult);
void HandleDollarLog(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarLog10(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarLtrim(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarMergeObjects(pgbson *doc, const bson_value_t *operatorValue,
							  ExpressionResult *expressionResult);
void HandleDollarMeta(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarMillisecond(pgbson *doc, const bson_value_t *operatorValue,
							 ExpressionResult *expressionResult);
void HandleDollarMinute(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarMod(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarMonth(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarMultiply(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarNot(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarObjectToArray(pgbson *doc, const bson_value_t *operatorValue,
							   ExpressionResult *expressionResult);
void HandleDollarOr(pgbson *doc, const bson_value_t *operatorValue,
					ExpressionResult *expressionResult);
void HandleDollarPow(pgbson *doc, const bson_value_t *operatorValue,
					 ExpressionResult *expressionResult);
void HandleDollarRand(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarRound(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarRtrim(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarToBool(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarToDate(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarToDecimal(pgbson *doc, const bson_value_t *operatorValue,
						   ExpressionResult *expressionResult);
void HandleDollarToDouble(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarToInt(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarToLong(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarToObjectId(pgbson *doc, const bson_value_t *operatorValue,
							ExpressionResult *expressionResult);
void HandleDollarToString(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarTrunc(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarTrim(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarType(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarSecond(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarSetDifference(pgbson *doc, const bson_value_t *operatorValue,
							   ExpressionResult *expressionResult);
void HandleDollarSetEquals(pgbson *doc, const bson_value_t *operatorValue,
						   ExpressionResult *expressionResult);
void HandleDollarSetIntersection(pgbson *doc, const bson_value_t *operatorValue,
								 ExpressionResult *expressionResult);
void HandleDollarSetIsSubset(pgbson *doc, const bson_value_t *operatorValue,
							 ExpressionResult *expressionResult);
void HandleDollarSetUnion(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarSize(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarSlice(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarSplit(pgbson *doc, const bson_value_t *operatorValue,
					   ExpressionResult *expressionResult);
void HandleDollarSqrt(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarStrLenBytes(pgbson *doc, const bson_value_t *operatorValue,
							 ExpressionResult *expressionResult);
void HandleDollarStrLenCP(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarSubtract(pgbson *doc, const bson_value_t *operatorValue,
						  ExpressionResult *expressionResult);
void HandleDollarSwitch(pgbson *doc, const bson_value_t *operatorValue,
						ExpressionResult *expressionResult);
void HandleDollarWeek(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarYear(pgbson *doc, const bson_value_t *operatorValue,
					  ExpressionResult *expressionResult);
void HandleDollarToUpper(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult);
void HandleDollarToLower(pgbson *doc, const bson_value_t *operatorValue,
						 ExpressionResult *expressionResult);
void HandleDollarStrCaseCmp(pgbson *doc, const bson_value_t *operatorValue,
							ExpressionResult *expressionResult);

/*
 *************************************************************
 * New operator functions that use the pre parsed framework
 *************************************************************
 */
void HandlePreParsedDollarAvg(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarBsonSize(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarBinarySize(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarCmp(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarDateAdd(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarDateDiff(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarDateFromParts(pgbson *doc, void *arguments,
										ExpressionResult *expressionResult);
void HandlePreParsedDollarDateSubtract(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarDateTrunc(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarEq(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarGt(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarGte(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarLt(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarLte(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMap(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMax(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMin(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarNe(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarFilter(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarFirstN(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarIndexOfArray(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarLastN(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarMakeArray(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarMaxMinN(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarRange(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarReduce(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarRegexFind(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarRegexFindAll(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarRegexMatch(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarReplaceAll(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarReplaceOne(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarReverseArray(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarSetField(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSortArray(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarSubstrBytes(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarSubstrCP(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSum(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarTsIncrement(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarTsSecond(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarZip(pgbson *doc, void *arguments,
							  ExpressionResult *ExpressionResult);
void ParseDollarAvg(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarBsonSize(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarBinarySize(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarCmp(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarDateAdd(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarDateDiff(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarDateFromParts(const bson_value_t *argument,
							  AggregationExpressionData *data);
void ParseDollarDateSubtract(const bson_value_t *argument,
							 AggregationExpressionData *data);
void ParseDollarDateTrunc(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarEq(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarGt(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarGte(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarLt(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarLte(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarMap(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarMax(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarMin(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarNe(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarFilter(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarFirstN(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarIndexOfArray(const bson_value_t *argument,
							 AggregationExpressionData *data);
void ParseDollarLastN(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarLiteral(const bson_value_t *inputDocument,
						AggregationExpressionData *data);
void ParseDollarMakeArray(const bson_value_t *inputDocument,
						  AggregationExpressionData *data);
void ParseDollarMaxN(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarMinN(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarRange(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarReduce(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarRegexFind(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarRegexFindAll(const bson_value_t *argument,
							 AggregationExpressionData *data);
void ParseDollarRegexMatch(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarReplaceAll(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarReplaceOne(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarReverseArray(const bson_value_t *argument,
							 AggregationExpressionData *data);
void ParseDollarSetField(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarSortArray(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarSubstr(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarSubstrBytes(const bson_value_t *argument,
							AggregationExpressionData *data);
void ParseDollarSubstrCP(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarSum(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarTsSecond(const bson_value_t *argument, AggregationExpressionData *data);
void ParseDollarTsIncrement(const bson_value_t *argument,
							AggregationExpressionData *data);
void ParseDollarZip(const bson_value_t *argument, AggregationExpressionData *data);

/* Shared functions for operator handlers */
void * ParseFixedArgumentsForExpression(const bson_value_t *argumentValue,
										int numberOfExpectedArgs,
										const char *operatorName,
										AggregationExpressionArgumentsKind *argumentsKind);

void * ParseRangeArgumentsForExpression(const bson_value_t *argumentValue,
										int minRequiredArgs,
										int maxRequiredArgs,
										const char *operatorName,
										AggregationExpressionArgumentsKind *argumentsKind);
void HandleVariableArgumentExpression(pgbson *doc,
									  const bson_value_t *operatorValue,
									  ExpressionResult *expressionResult,
									  bson_value_t *startValue,
									  ExpressionArgumentHandlingContext *context);
void HandleRangedArgumentExpression(pgbson *doc,
									const bson_value_t *operatorValue,
									ExpressionResult *expressionResult,
									int minRequiredArgs,
									int maxRequiredArgs,
									const char *operatorName,
									ExpressionArgumentHandlingContext *context);
void HandleFixedArgumentExpression(pgbson *doc,
								   const bson_value_t *operatorValue,
								   ExpressionResult *expressionResult,
								   int numberOfExpectedArgs,
								   const char *operatorName,
								   ExpressionArgumentHandlingContext *context);
bool ProcessDualArgumentElement(bson_value_t *result,
								const bson_value_t *currentElement,
								bool isFieldPathExpression, void *state);
bool ProcessThreeArgumentElement(bson_value_t *result, const
								 bson_value_t *currentElement,
								 bool isFieldPathExpression, void *state);
bool ProcessFourArgumentElement(bson_value_t *result, const
								bson_value_t *currentElement,
								bool isFieldPathExpression, void *state);
StringView GetDateStringWithDefaultFormat(int64_t dateInMs, ExtensionTimezone timezone,
										  DateStringFormatCase formatCase);
StringView GetTimestampStringWithDefaultFormat(const bson_value_t *timeStampBsonElement,
											   ExtensionTimezone timezone,
											   DateStringFormatCase formatCase);

/* Helper inline method to throw error for expressions that take N number of args
 * but a different number was provided.
 */
inline static void
pg_attribute_noreturn()
ThrowExpressionTakesExactlyNArgs(const char * expression, int requiredArgs, int numArgs)
{
	ereport(ERROR, (errcode(MongoExpressionTakesExactlyNArgs), errmsg(
						"Expression %s takes exactly %d arguments. %d were passed in.",
						expression, requiredArgs, numArgs)));
}

/* Helper inline method to throw error for expressions that take minimum N number of args and Maximum M
 * but a different number was provided.
 */
inline static void
pg_attribute_noreturn()
ThrowExpressionNumOfArgsOutsideRange(const char * expression, int minRequiredArgs,
									 int maxRequiredArgs, int numArgs)
{
	ereport(ERROR, (errcode(MongoRangeArgumentExpressionArgsOutOfRange),
					errmsg(
						"Expression %s takes at least %d arguments, and at most %d, but %d were passed in.",
						expression, minRequiredArgs, maxRequiredArgs, numArgs)));
}

/* Whether or not the expression result value is undefined */
inline static bool
IsExpressionResultUndefined(const bson_value_t *value)
{
	return value->value_type == BSON_TYPE_UNDEFINED || value->value_type == BSON_TYPE_EOD;
}


/* Whether or not the expression result value is null or undefined */
inline static bool
IsExpressionResultNull(const bson_value_t *value)
{
	return value->value_type == BSON_TYPE_NULL || value->value_type ==
		   BSON_TYPE_UNDEFINED;
}


/* Whether or not the expression result value is null or undefined */
inline static bool
IsExpressionResultNullOrUndefined(const bson_value_t *value)
{
	return value->value_type == BSON_TYPE_NULL || IsExpressionResultUndefined(value);
}


/* Whether the AggregationExpressionData contains a constant */
static inline bool
IsAggregationExpressionConstant(const AggregationExpressionData *data)
{
	return data->kind == AggregationExpressionKind_Constant;
}


/* Given a list checks if each element in list is constant or not */
static inline bool
AreElementsInListConstant(List *args)
{
	Assert(args != NULL && IsA((args), List));
	int index = 0;
	int sizeOfList = args->length;
	while (index < sizeOfList)
	{
		if (!IsAggregationExpressionConstant(list_nth(args, index)))
		{
			return false;
		}
		index++;
	}
	return true;
}


#endif
