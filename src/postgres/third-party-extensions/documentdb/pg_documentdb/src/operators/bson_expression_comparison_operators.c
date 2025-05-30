/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_comparison_operators.c
 *
 * Comparison Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/bson_core.h"
#include "query/bson_compare.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "collation/collation.h"

/* --------------------------------------------------------- */
/* Type declaration */
/* --------------------------------------------------------- */

/* Enum that defines the type of comparison to perform. */
typedef enum ComparisonType
{
	ComparisonType_Undefined = 0,
	ComparisonType_Cmp = 1,
	ComparisonType_Eq = 2,
	ComparisonType_Gt = 3,
	ComparisonType_Gte = 4,
	ComparisonType_Lt = 5,
	ComparisonType_Lte = 6,
	ComparisonType_Ne = 7,
} ComparisonType;

/* State for comparison operators. */
typedef struct ComparisonOperatorState
{
	AggregationExpressionData *firstArg;
	AggregationExpressionData *secondArg;
	char *collationString;
} ComparisonOperatorState;


/* *******************************************
 *  New aggregation operator's framework which uses pre parsed expression
 *  when building the projection tree.
 *  *******************************************
 */

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void HandlePreParsedComparisonOperator(pgbson *doc,
											  void *arguments,
											  ExpressionResult *expressionResult,
											  ComparisonType comparisonType);

static void ParseComparisonOperator(const bson_value_t *argument,
									AggregationExpressionData *data,
									const char *operatorName,
									ComparisonType comparisonType,
									ParseAggregationExpressionContext *parseContext);

static void CompareExpressionBsonValues(const bson_value_t *firstValue,
										const bson_value_t *secondValue,
										const char *collationString,
										bson_value_t *result,
										ComparisonType comparisonType);

/*
 * Generic function that evaluates the output of a comparison operator and sets
 * the result in the expression result argument.
 */
static void
HandlePreParsedComparisonOperator(pgbson *doc,
								  void *arguments,
								  ExpressionResult *expressionResult,
								  ComparisonType comparisonType)
{
	ComparisonOperatorState *state = arguments;

	AggregationExpressionData *first = state->firstArg;
	AggregationExpressionData *second = state->secondArg;
	char *collationString = state->collationString;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
	EvaluateAggregationExpressionData(first, doc, &childResult, isNullOnEmpty);

	bson_value_t firstValue = childResult.value;

	ExpressionResultReset(&childResult);
	EvaluateAggregationExpressionData(second, doc, &childResult, isNullOnEmpty);

	bson_value_t secondValue = childResult.value;

	bson_value_t result = { 0 };
	CompareExpressionBsonValues(&firstValue,
								&secondValue,
								collationString,
								&result,
								comparisonType);

	ExpressionResultSetValue(expressionResult, &result);
}


/*
 * Generic function that parses a given comparison operator and sets
 * the parsed data in the data argument.
 */
static void
ParseComparisonOperator(const bson_value_t *argument,
						AggregationExpressionData *data,
						const char *operatorName,
						ComparisonType comparisonType,
						ParseAggregationExpressionContext *parseContext)
{
	int numOfRequiredArgs = 2;
	List *arguments = ParseFixedArgumentsForExpression(argument,
													   numOfRequiredArgs,
													   operatorName,
													   &data->operator.argumentsKind,
													   parseContext);

	AggregationExpressionData *first = list_nth(arguments, 0);
	AggregationExpressionData *second = list_nth(arguments, 1);

	/* If both arguments are constants: compute comparison result, change
	 * expression type to constant, store the result in the expression value
	 * and free the arguments list as it won't be needed anymore. */
	if (IsAggregationExpressionConstant(first) && IsAggregationExpressionConstant(second))
	{
		CompareExpressionBsonValues(&first->value,
									&second->value,
									parseContext->collationString,
									&data->value,
									comparisonType);
		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
		ereport(DEBUG3, errmsg("Precomputed bson %s operator for constant comparison.",
							   operatorName));
	}
	else
	{
		ComparisonOperatorState *state = palloc0(sizeof(ComparisonOperatorState));
		state->firstArg = first;
		state->secondArg = second;

		const char *collationString = parseContext->collationString;
		if (IsCollationApplicable(collationString))
		{
			state->collationString = pstrdup(collationString);
		}

		data->operator.arguments = state;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_Palloc;
	}
}


/*
 * Function that compares 2 bson values. The boolean result
 * of the comparison is stored in the result argument.
 */
static void
CompareExpressionBsonValues(const bson_value_t *firstValue,
							const bson_value_t *secondValue,
							const char *collationString,
							bson_value_t *result,
							ComparisonType comparisonType)
{
	bool boolValue = false;
	bool isComparisonValid = false;
	int cmp = CompareBsonValueAndTypeWithCollation(firstValue, secondValue,
												   &isComparisonValid, collationString);

	switch (comparisonType)
	{
		case ComparisonType_Cmp:
		{
			result->value_type = BSON_TYPE_INT32;
			result->value.v_int32 = cmp == 0 ? 0 : cmp > 0 ? 1 : -1;
			return;
		}

		case ComparisonType_Eq:
		{
			boolValue = isComparisonValid && cmp == 0;
			break;
		}

		case ComparisonType_Gt:
		{
			boolValue = cmp > 0;
			break;
		}

		case ComparisonType_Gte:
		{
			boolValue = cmp >= 0;
			break;
		}

		case ComparisonType_Lt:
		{
			boolValue = cmp < 0;
			break;
		}

		case ComparisonType_Lte:
		{
			boolValue = cmp <= 0;
			break;
		}

		case ComparisonType_Ne:
		{
			boolValue = !isComparisonValid || cmp != 0;
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Invalid comparison aggregation operator %d",
								   comparisonType)));
		}
	}

	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = boolValue;
}


/*
 * Evaluates the output of an $eq expression.
 * $eq is expressed as { "$eq": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return a bool.
 * true if the values are equivalent, false otherwise.
 */
void
HandlePreParsedDollarEq(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Eq);
}


/*
 * Parses an $eq expression and sets the parsed data in the data argument.
 * $eq is expressed as { "$eq": [ <expression>, <expression> ] }
 */
void
ParseDollarEq(const bson_value_t *argument, AggregationExpressionData *data,
			  ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$eq", ComparisonType_Eq, context);
}


/*
 * Evaluates the output of an $cmp expression.
 * $cmp is expressed as { "$cmp": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return an int32.
 * 0 if the values are equivalent, 1 if the first expression is greater than the second
 * and -1 if the first expression is less than the second.
 */
void
HandlePreParsedDollarCmp(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Cmp);
}


/*
 * Parses an $cmp expression and sets the parsed data in the data argument.
 * $cmp is expressed as { "$cmp": [ <expression>, <expression> ] }
 */
void
ParseDollarCmp(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$cmp", ComparisonType_Cmp, context);
}


/*
 * Evaluates the output of an $gt expression.
 * $gt is expressed as { "$gt": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return a bool.
 * true if the first value is greater than the second value, false otherwise.
 */
void
HandlePreParsedDollarGt(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Gt);
}


/*
 * Parses an $gt expression and sets the parsed data in the data argument.
 * $gt is expressed as { "$gt": [ <expression>, <expression> ] }
 */
void
ParseDollarGt(const bson_value_t *argument, AggregationExpressionData *data,
			  ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$gt", ComparisonType_Gt, context);
}


/*
 * Evaluates the output of an $gte expression.
 * $gte is expressed as { "$gte": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return a bool.
 * true if the first value is greater or equal than the second value, false otherwise.
 */
void
HandlePreParsedDollarGte(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Gte);
}


/*
 * Parses an $gte expression and sets the parsed data in the data argument.
 * $gte is expressed as { "$gte": [ <expression>, <expression> ] }
 */
void
ParseDollarGte(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$gte", ComparisonType_Gte, context);
}


/*
 * Evaluates the output of an $lt expression.
 * $lt is expressed as { "$lt": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return a bool.
 * true if the first value is less than the second value, false otherwise.
 */
void
HandlePreParsedDollarLt(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Lt);
}


/*
 * Parses an $lt expression and sets the parsed data in the data argument.
 * $lt is expressed as { "$lt": [ <expression>, <expression> ] }
 */
void
ParseDollarLt(const bson_value_t *argument, AggregationExpressionData *data,
			  ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$lt", ComparisonType_Lt, context);
}


/*
 * Evaluates the output of an $lte expression.
 * $lte is expressed as { "$lte": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return a bool.
 * true if the first value is less or equal than the second value, false otherwise.
 */
void
HandlePreParsedDollarLte(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Lte);
}


/*
 * Parses an $lte expression and sets the parsed data in the data argument.
 * $lte is expressed as { "$lte": [ <expression>, <expression> ] }
 */
void
ParseDollarLte(const bson_value_t *argument, AggregationExpressionData *data,
			   ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$lte", ComparisonType_Lte, context);
}


/*
 * Evaluates the output of an $ne expression.
 * $ne is expressed as { "$ne": [ <expression>, <expression> ] }
 * We evaluate the inner expressions and then return a bool.
 * true if the values are not equivalent, false otherwise.
 */
void
HandlePreParsedDollarNe(pgbson *doc, void *arguments, ExpressionResult *expressionResult)
{
	HandlePreParsedComparisonOperator(doc, arguments, expressionResult,
									  ComparisonType_Ne);
}


/*
 * Parses an $ne expression and sets the parsed data in the data argument.
 * $ne is expressed as { "$ne": [ <expression>, <expression> ] }
 */
void
ParseDollarNe(const bson_value_t *argument, AggregationExpressionData *data,
			  ParseAggregationExpressionContext *context)
{
	ParseComparisonOperator(argument, data, "$ne", ComparisonType_Ne, context);
}
