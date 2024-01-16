/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_boolean_operators.c
 *
 * Boolean Operator expression implementations of BSON.
 * See also: https://www.mongodb.com/docs/manual/reference/operator/aggregation/#boolean-expression-operators
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
static bool ProcessDollarOrElement(bson_value_t *result, const
								   bson_value_t *currentElement,
								   bool isFieldPathExpression, void *state);
static bool ProcessDollarAndElement(bson_value_t *result, const
									bson_value_t *currentElement,
									bool isFieldPathExpression, void *state);
static bool ProcessDollarNotElement(bson_value_t *result, const
									bson_value_t *currentElement,
									bool isFieldPathExpression, void *state);

/*
 * Evaluates the output of an $or expression.
 * $or is expressed as { "$or": [ <expression>, <expression>, .. ] }
 * or { "$or": <expression> }.
 * We evaluate the inner expressions and then return a boolean
 * representing the table of truth for or.
 */
void
HandleDollarOr(pgbson *doc, const bson_value_t *operatorValue,
			   ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarOrElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_BOOL;
	startValue.value.v_bool = false;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult,
									 &startValue, &context);
}


/*
 * Evaluates the output of an $and expression.
 * $and is expressed as { "$and": [ <expression>, <expression>, .. ] }
 * or { "$and": <expression> }.
 * We evaluate the inner expressions and then return a boolean
 * representing the table of truth for and.
 */
void
HandleDollarAnd(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarAndElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	bson_value_t startValue;
	startValue.value_type = BSON_TYPE_BOOL;
	startValue.value.v_bool = true;
	HandleVariableArgumentExpression(doc, operatorValue, expressionResult,
									 &startValue, &context);
}


/*
 * Evaluates the output of a $not expression.
 * $not is expressed as { "$not": [ <expression> ] }
 * or { "$not": <expression> }.
 * We return the negated boolean of the evaluated expression.
 */
void
HandleDollarNot(pgbson *doc, const bson_value_t *operatorValue,
				ExpressionResult *expressionResult)
{
	ExpressionArgumentHandlingContext context =
	{
		.processElementFunc = ProcessDollarNotElement,
		.processExpressionResultFunc = NULL,
		.state = NULL,
	};

	int numberOfRequiredArgs = 1;
	HandleFixedArgumentExpression(doc, operatorValue, expressionResult,
								  numberOfRequiredArgs, "$not", &context);
}


/* Function that processes a single element for the $or operator. */
static bool
ProcessDollarOrElement(bson_value_t *result, const bson_value_t *currentElement,
					   bool isFieldPathExpression, void *state)
{
	result->value.v_bool = result->value.v_bool || BsonValueAsBool(currentElement);

	/* Native Mongo validates all the expressions even if an expression would evaluate to true before
	 * hitting the faulted expression (this is the case for expressions with a constant argument and other scenarios), however
	 * we don't have a way to do that, so we should bail if we find a true expression as an $or can't be false if an expression
	 * evaluates to true. This enables more scenarios like an $or on a $filter condition or on a $switch statement, etc, which
	 * might reference variables or paths conditionally and evaluating all expressions regardless could break valid scenarios. */
	return !result->value.v_bool;
}


/* Function that processes a single element for the $and operator. */
static bool
ProcessDollarAndElement(bson_value_t *result, const bson_value_t *currentElement,
						bool isFieldPathExpression, void *state)
{
	result->value.v_bool = result->value.v_bool && BsonValueAsBool(currentElement);

	/* Native Mongo validates all the expressions even if an expression would evaluate to false before
	 * hitting the faulted expression (this is the case for expressions with a constant argument and other scenarios), however
	 * we don't have a way to do that, so we should bail if we find a false expression as an $and can't be true if an expression
	 * evaluates to false. This enables more scenarios like an $and on a $filter condition or on a $switch statement, etc, which
	 * might reference variables or paths conditionally and evaluating all expressions regardless could break valid scenarios. */
	return result->value.v_bool;
}


/* Function that processes a single element for the $not operator. */
static bool
ProcessDollarNotElement(bson_value_t *result, const bson_value_t *currentElement,
						bool isFieldPathExpression, void *state)
{
	result->value_type = BSON_TYPE_BOOL;
	result->value.v_bool = !BsonValueAsBool(currentElement);
	return true;
}
