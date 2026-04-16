/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/operators/bson_expression_operators.h
 *
 * Common declarations of the bson expression operators.
 *
 *-------------------------------------------------------------------------
 */

#include "utils/documentdb_errors.h"
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

/* State for operators that have two arguments used to apply the
 * operation to the result. i.e: $divide, $substract. */
typedef struct DualArgumentExpressionState
{
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
void EvaluateAggregationExpressionData(const AggregationExpressionData *expressionData,
									   pgbson *document,
									   ExpressionResult *expressionResult, bool
									   isNullOnEmpty);
ExpressionResult ExpressionResultCreateChild(ExpressionResult *parent);
void ExpressionResultReset(ExpressionResult *expressionResult);
void ExpressionResultSetConstantVariable(ExpressionResult *expressionResult, const
										 StringView *variableName, const
										 bson_value_t *value);

/*
 *************************************************************
 * New operator functions that use the pre parsed framework
 *************************************************************
 */
void HandlePreParsedDollarAbs(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarAcos(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarAcosh(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarAdd(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarAllElementsTrue(pgbson *doc, void *arguments,
										  ExpressionResult *expressionResult);
void HandlePreParsedDollarAnd(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarAnyElementTrue(pgbson *doc, void *arguments,
										 ExpressionResult *expressionResult);
void HandlePreParsedDollarArrayElemAt(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarArrayToObject(pgbson *doc, void *arguments,
										ExpressionResult *expressionResult);
void HandlePreParsedDollarAsin(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarAsinh(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarAtan(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarAtan2(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarAtanh(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarAvg(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarBsonSize(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarBinarySize(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarBitAnd(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarBitNot(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarBitOr(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarBitXor(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarBucketInternal(pgbson *doc, void *arguments,
										 ExpressionResult *expressionResult);
void HandlePreParsedDollarCeil(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarCmp(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarConcat(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarConcatArrays(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarCond(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarConvert(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarCos(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarCosh(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarDateAdd(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarDateDiff(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarDateFromParts(pgbson *doc, void *arguments,
										ExpressionResult *expressionResult);
void HandlePreParsedDollarDateFromString(pgbson *doc, void *arguments,
										 ExpressionResult *expressionResult);
void HandlePreParsedDollarDateSubtract(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarDateToParts(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarDateToString(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarDateTrunc(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarDayOfMonth(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarDayOfWeek(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarDayOfYear(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarDegreesToRadians(pgbson *doc, void *arguments,
										   ExpressionResult *expressionResult);
void HandlePreParsedDollarDivide(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarEq(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarExp(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarFilter(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarFirst(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarFirstN(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarFloor(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarGetField(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarGt(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarGte(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarIndexOfBytes(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarIndexOfCP(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarHour(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarIfNull(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarIn(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarIndexOfArray(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarIsNumber(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarIsArray(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarLast(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarIsoDayOfWeek(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarIsoWeek(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarIsoWeekYear(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarLastN(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarLet(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarLog(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarLog10(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarLn(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarLt(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarLte(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarLtrim(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarMakeArray(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarMaxMinN(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarMap(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMax(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMergeObjects(pgbson *doc, void *arguments,
									   ExpressionResult *expressionResult);
void HandlePreParsedDollarMeta(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarMillisecond(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarMin(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMinute(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarMod(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarMonth(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarMultiply(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarNe(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarNot(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarObjectToArray(pgbson *doc, void *arguments,
										ExpressionResult *expressionResult);
void HandlePreParsedDollarOr(pgbson *doc, void *arguments,
							 ExpressionResult *expressionResult);
void HandlePreParsedDollarPow(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarRadiansToDegrees(pgbson *doc, void *arguments,
										   ExpressionResult *expressionResult);
void HandlePreParsedDollarRand(pgbson *doc, void *arguments,
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
void HandlePreParsedDollarRound(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarRtrim(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarSecond(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarSetDifference(pgbson *doc, void *arguments,
										ExpressionResult *expressionResult);
void HandlePreParsedDollarSetEquals(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarSetField(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSetIntersection(pgbson *doc, void *arguments,
										  ExpressionResult *expressionResult);
void HandlePreParsedDollarSetIsSubset(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarSetUnion(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSin(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarSinh(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarSize(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarSlice(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarSortArray(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarSplit(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarSqrt(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarStrCaseCmp(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarStrLenBytes(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarStrLenCP(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSubstrBytes(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarSubstrCP(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSubtract(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarSum(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarSwitch(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarTan(pgbson *doc, void *arguments,
							  ExpressionResult *expressionResult);
void HandlePreParsedDollarTanh(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarToBool(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarToDate(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarToDecimal(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarToDecimal(pgbson *doc, void *arguments,
									ExpressionResult *expressionResult);
void HandlePreParsedDollarToDouble(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarToInt(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarToLong(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarToObjectId(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarToString(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarToUUID(pgbson *doc, void *arguments,
								 ExpressionResult *expressionResult);
void HandlePreParsedDollarToLower(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarToUpper(pgbson *doc, void *arguments,
								  ExpressionResult *expressionResult);
void HandlePreParsedDollarTrim(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarTrunc(pgbson *doc, void *arguments,
								ExpressionResult *expressionResult);
void HandlePreParsedDollarToHashedIndexKey(pgbson *doc, void *arguments,
										   ExpressionResult *expressionResult);
void HandlePreParsedDollarTsIncrement(pgbson *doc, void *arguments,
									  ExpressionResult *expressionResult);
void HandlePreParsedDollarTsSecond(pgbson *doc, void *arguments,
								   ExpressionResult *expressionResult);
void HandlePreParsedDollarType(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarUnsetField(pgbson *doc, void *arguments,
									 ExpressionResult *expressionResult);
void HandlePreParsedDollarWeek(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarYear(pgbson *doc, void *arguments,
							   ExpressionResult *expressionResult);
void HandlePreParsedDollarZip(pgbson *doc, void *arguments,
							  ExpressionResult *ExpressionResult);

void ParseDollarAbs(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarAcos(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarAcosh(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarAdd(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarAllElementsTrue(const bson_value_t *argument,
								AggregationExpressionData *data,
								ParseAggregationExpressionContext *context);
void ParseDollarAnd(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarArray(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarAnyElemAt(const bson_value_t *argument,
						  AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarAnyElementTrue(const bson_value_t *argument,
							   AggregationExpressionData *data,
							   ParseAggregationExpressionContext *context);
void ParseDollarArrayElemAt(const bson_value_t *argument,
							AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarArrayToObject(const bson_value_t *argument,
							  AggregationExpressionData *data,
							  ParseAggregationExpressionContext *context);
void ParseDollarAsin(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarAsinh(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarAtan(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarAtan2(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarAtanh(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarAvg(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarBsonSize(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarBinarySize(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarBitAnd(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarBitNot(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarBitOr(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarBitXor(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarBucketInternal(const bson_value_t *argument,
							   AggregationExpressionData *data,
							   ParseAggregationExpressionContext *context);
void ParseDollarCeil(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarConcat(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarConcatArrays(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarCond(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarConvert(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarCos(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarCosh(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarCmp(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarDateAdd(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarDateDiff(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarDateFromParts(const bson_value_t *argument,
							  AggregationExpressionData *data,
							  ParseAggregationExpressionContext *context);
void ParseDollarDateFromString(const bson_value_t *argument,
							   AggregationExpressionData *data,
							   ParseAggregationExpressionContext *context);
void ParseDollarDateSubtract(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarDateToParts(const bson_value_t *argument, AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarDateToString(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarDateTrunc(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarDayOfMonth(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarDayOfWeek(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarDayOfYear(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarDegreesToRadians(const bson_value_t *argument,
								 AggregationExpressionData *data,
								 ParseAggregationExpressionContext *context);
void ParseDollarDivide(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarEq(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarExp(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarFirst(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarFirstN(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarFloor(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarFilter(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarGetField(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarGt(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarGte(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarHour(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarIfNull(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarIn(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarIndexOfArray(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarIsArray(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarIsNumber(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarIsoDayOfWeek(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarIsoWeek(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarIsoWeekYear(const bson_value_t *argument, AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarLast(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarLet(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarLiteral(const bson_value_t *inputDocument,
						AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarLn(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarLog(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarLog10(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarLt(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarLte(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarMap(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarMax(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarLastN(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarMakeArray(const bson_value_t *inputDocument,
						  AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarMaxN(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarMeta(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarMergeObjects(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarMillisecond(const bson_value_t *argument, AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarMin(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarMinN(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarMinute(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarMod(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarMonth(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarMultiply(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarNe(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarNot(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarObjectToArray(const bson_value_t *argument,
							  AggregationExpressionData *data,
							  ParseAggregationExpressionContext *context);
void ParseDollarOr(const bson_value_t *argument, AggregationExpressionData *data,
				   ParseAggregationExpressionContext *context);
void ParseDollarFilter(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarFirstN(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarIndexOfArray(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarIndexOfBytes(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarIndexOfCP(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarLastN(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarLiteral(const bson_value_t *inputDocument,
						AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarLtrim(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarMakeArray(const bson_value_t *inputDocument,
						  AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarMaxN(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarMinN(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarPow(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarRadiansToDegrees(const bson_value_t *argument,
								 AggregationExpressionData *data,
								 ParseAggregationExpressionContext *context);
void ParseDollarRand(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarRange(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarReduce(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarRegexFind(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarRegexFindAll(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarRegexMatch(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarReplaceAll(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarReplaceOne(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarReverseArray(const bson_value_t *argument,
							 AggregationExpressionData *data,
							 ParseAggregationExpressionContext *context);
void ParseDollarRound(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarRtrim(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarSecond(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarSetDifference(const bson_value_t *argument,
							  AggregationExpressionData *data,
							  ParseAggregationExpressionContext *context);
void ParseDollarSetEquals(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarSetField(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarSetIntersection(const bson_value_t *argument,
								AggregationExpressionData *data,
								ParseAggregationExpressionContext *context);
void ParseDollarSetIsSubset(const bson_value_t *argument, AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarSetUnion(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarSin(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarSize(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarSinh(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarSlice(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarSortArray(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarSplit(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarSqrt(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarStrCaseCmp(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarStrLenBytes(const bson_value_t *argument, AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarStrLenCP(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarSubstr(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarSubstrBytes(const bson_value_t *argument,
							AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarSubstrCP(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarSubtract(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarSum(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarSwitch(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarTan(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);
void ParseDollarTanh(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarToBool(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarToDate(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarToDecimal(const bson_value_t *argument, AggregationExpressionData *data,
						  ParseAggregationExpressionContext *context);
void ParseDollarToDouble(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarToHashedIndexKey(const bson_value_t *argument,
								 AggregationExpressionData *data,
								 ParseAggregationExpressionContext *context);
void ParseDollarToInt(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarToLong(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarToObjectId(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarToString(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarToUUID(const bson_value_t *argument, AggregationExpressionData *data,
					   ParseAggregationExpressionContext *context);
void ParseDollarToLower(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarToUpper(const bson_value_t *argument, AggregationExpressionData *data,
						ParseAggregationExpressionContext *context);
void ParseDollarToHashedIndexKey(const bson_value_t *argument,
								 AggregationExpressionData *data,
								 ParseAggregationExpressionContext *context);
void ParseDollarTrim(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarTrunc(const bson_value_t *argument, AggregationExpressionData *data,
					  ParseAggregationExpressionContext *context);
void ParseDollarTsSecond(const bson_value_t *argument, AggregationExpressionData *data,
						 ParseAggregationExpressionContext *context);
void ParseDollarTsIncrement(const bson_value_t *argument,
							AggregationExpressionData *data,
							ParseAggregationExpressionContext *context);
void ParseDollarType(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarUnsetField(const bson_value_t *argument, AggregationExpressionData *data,
						   ParseAggregationExpressionContext *context);
void ParseDollarWeek(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarYear(const bson_value_t *argument, AggregationExpressionData *data,
					 ParseAggregationExpressionContext *context);
void ParseDollarZip(const bson_value_t *argument, AggregationExpressionData *data,
					ParseAggregationExpressionContext *context);

/* Shared functions for operator handlers */
void * ParseFixedArgumentsForExpression(const bson_value_t *argumentValue,
										int numberOfExpectedArgs,
										const char *operatorName,
										AggregationExpressionArgumentsKind *argumentsKind,
										ParseAggregationExpressionContext *context);

void * ParseRangeArgumentsForExpression(const bson_value_t *argumentValue,
										int minRequiredArgs,
										int maxRequiredArgs,
										const char *operatorName,
										AggregationExpressionArgumentsKind *argumentsKind,
										ParseAggregationExpressionContext *context);
List * ParseVariableArgumentsForExpression(const bson_value_t *value, bool *isConstant,
										   ParseAggregationExpressionContext *context);
void ProcessThreeArgumentElement(const bson_value_t *currentElement, bool
								 isFieldPathExpression, void *state);
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
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION16020), errmsg(
						"The expression %s requires exactly %d arguments, but %d arguments were actually provided.",
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
	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_RANGEARGUMENTEXPRESSIONARGSOUTOFRANGE),
					errmsg(
						"The expression %s requires no fewer than %d arguments and no more than %d arguments, but %d arguments were actually provided.",
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


/* Initializes the state for dual argument expressions. */
static inline void
InitializeDualArgumentExpressionState(bson_value_t firstValue, bson_value_t secondValue,
									  bool
									  hasFieldExpression,
									  DualArgumentExpressionState *state)
{
	state->firstArgument = firstValue;
	state->secondArgument = secondValue;
	state->hasFieldExpression = hasFieldExpression;
	state->hasNullOrUndefined = IsExpressionResultNullOrUndefined(&firstValue) ||
								IsExpressionResultNullOrUndefined(&secondValue);
}


/*
 * Helper to free the space allocated when parsing operator expression.
 * This is used in place of list_free_deep when arguments may contain NULL entries.
 */
static inline void
FreeVariableLengthArgs(List *arguments)
{
	Assert(arguments != NIL);

	ListCell *cell;
	foreach(cell, arguments)
	{
		AggregationExpressionData *data = lfirst(cell);
		if (data != NULL)
		{
			pfree(data);
		}
	}

	pfree(arguments);
}


#endif
