/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_expression_conditional_operators.c
 *
 * Conditional Operator expression implementations of BSON.
 * See also: https://www.mongodb.com/docs/manual/reference/operator/aggregation/#conditional-expression-operators
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>

#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/mongo_errors.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */

/* Represents a branch for $switch operator */
typedef struct SwitchBranch
{
	bson_value_t caseExpression;
	bson_value_t thenExpression;
} SwitchBranch;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static SwitchBranch * ParseBranchForSwitch(bson_iter_t *iter);

/*
 * Evaluates the output of a $ifNull expression.
 * Since $ifNull is expressed as { "$ifNull": [ <expression1>, <expression2>, ..., <replacement-expression-if-null> ] }
 * We evaluate every expression and return the first that is not null or undefined, otherwise we return the last expression in the array.
 * $ifNull requires at least 2 arguments.
 */
void
HandleDollarIfNull(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	int numArgs = operatorValue->value_type == BSON_TYPE_ARRAY ?
				  BsonDocumentValueCountKeys(operatorValue) : 1;

	if (numArgs < 2)
	{
		ereport(ERROR, (errcode(MongoDollarIfNullRequiresAtLeastTwoArgs), errmsg(
							"$ifNull needs at least two arguments, had: %d",
							numArgs)));
	}

	bson_iter_t arrayIter;
	BsonValueInitIterator(operatorValue, &arrayIter);

	bson_value_t result;
	result.value_type = BSON_TYPE_NULL;

	bool isNullOnEmpty = false;
	while (bson_iter_next(&arrayIter))
	{
		bson_value_t evaluatedValue =
			EvaluateExpressionAndGetValue(doc, bson_iter_value(&arrayIter),
										  expressionResult, isNullOnEmpty);

		if (IsExpressionResultNullOrUndefined(&result))
		{
			/* don't break iteration in case a non-null is found
			 * in order to emit errors for any expression that could've
			 * resulted in one as part of the arguments. */
			result = evaluatedValue;
		}
	}

	/* if the last argument resulted in EOD, native mongo doesn't return any result, i.e: missing field. */
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


/*
 * Evaluates the output of a $cond expression.
 * Since $cond is expressed as { "$cond": [ <if-expression>, <then-expression>, <else-expression> ] } or
 * { "$cond": { "if": <expression>, "then": <expression>, "else": <expression> }}
 * We evaluate the <if-expression>, if its result is true we return the <then-expression>,
 * otherwise we return the <else-expression>.
 */
void
HandleDollarCond(pgbson *doc, const bson_value_t *operatorValue,
				 ExpressionResult *expressionResult)
{
	bson_value_t ifValue;
	bson_value_t thenValue;
	bson_value_t elseValue;
	bool isNullOnEmpty = false;
	if (operatorValue->value_type == BSON_TYPE_DOCUMENT)
	{
		bool isIfMissing = true;
		bool isThenMissing = true;
		bool isElseMissing = true;

		bson_iter_t documentIter;
		BsonValueInitIterator(operatorValue, &documentIter);
		while (bson_iter_next(&documentIter))
		{
			const char *key = bson_iter_key(&documentIter);
			const bson_value_t *currentValue = bson_iter_value(&documentIter);
			if (strcmp(key, "if") == 0)
			{
				isIfMissing = false;
				ifValue = EvaluateExpressionAndGetValue(doc, currentValue,
														expressionResult, isNullOnEmpty);
			}
			else if (strcmp(key, "then") == 0)
			{
				isThenMissing = false;
				thenValue = EvaluateExpressionAndGetValue(doc, currentValue,
														  expressionResult,
														  isNullOnEmpty);
			}
			else if (strcmp(key, "else") == 0)
			{
				isElseMissing = false;
				elseValue = EvaluateExpressionAndGetValue(doc, currentValue,
														  expressionResult,
														  isNullOnEmpty);
			}
			else
			{
				ereport(ERROR, (errcode(MongoDollarCondBadParameter), errmsg(
									"Unrecognized parameter to $cond: %s", key)));
			}
		}

		if (isIfMissing)
		{
			ereport(ERROR, (errcode(MongoDollarCondMissingIfParameter),
							errmsg("Missing 'if' parameter to $cond")));
		}
		else if (isThenMissing)
		{
			ereport(ERROR, (errcode(MongoDollarCondMissingThenParameter),
							errmsg("Missing 'then' parameter to $cond")));
		}
		else if (isElseMissing)
		{
			ereport(ERROR, (errcode(MongoDollarCondMissingElseParameter),
							errmsg("Missing 'else' parameter to $cond")));
		}
	}
	else
	{
		int numArgs = operatorValue->value_type == BSON_TYPE_ARRAY ?
					  BsonDocumentValueCountKeys(operatorValue) : 1;

		int requiredArgs = 3;
		if (numArgs != requiredArgs)
		{
			ThrowExpressionTakesExactlyNArgs("$cond", 3, numArgs);
		}

		bson_iter_t arrayIter;
		BsonValueInitIterator(operatorValue, &arrayIter);

		bson_iter_next(&arrayIter);
		ifValue = EvaluateExpressionAndGetValue(
			doc, bson_iter_value(&arrayIter), expressionResult, isNullOnEmpty);

		/* We need to evaluate both expressions, even if one of them is not going to be used in the result
		 * as we need to make sure we throw any errors that the then/else expressions could throw when
		 * evaluating them to match native mongo behavior. */
		bson_iter_next(&arrayIter);
		thenValue = EvaluateExpressionAndGetValue(
			doc, bson_iter_value(&arrayIter), expressionResult, isNullOnEmpty);

		bson_iter_next(&arrayIter);
		elseValue = EvaluateExpressionAndGetValue(
			doc, bson_iter_value(&arrayIter), expressionResult, isNullOnEmpty);
	}

	bson_value_t result = BsonValueAsBool(&ifValue) ? thenValue : elseValue;

	/* If resulted in EOD because of field not found, should be a no-op. */
	if (result.value_type != BSON_TYPE_EOD)
	{
		if (IsExpressionResultUndefined(&result))
		{
			result.value_type = BSON_TYPE_NULL;
		}

		ExpressionResultSetValue(expressionResult, &result);
	}
}


/*
 * Evaluates the output of a $switch expression.
 * $switch is expressed as { "$switch": { "branches": [ {"case": <expression>, "then": <expression>}, ...], "default": <expression>} }
 * We evaluate every case expression, if any returns true, we return it's corresponding then expression, else, we return the default expression.
 */
void
HandleDollarSwitch(pgbson *doc, const bson_value_t *operatorValue,
				   ExpressionResult *expressionResult)
{
	if (operatorValue->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresObject), errmsg(
							"$switch requires an object as an argument, found: %s",
							BsonTypeName(operatorValue->value_type))));
	}

	List *branches = NIL;
	const bson_value_t *defaultExpression = NULL;

	bson_iter_t documentIter;
	BsonValueInitIterator(operatorValue, &documentIter);
	while (bson_iter_next(&documentIter))
	{
		const char *key = bson_iter_key(&documentIter);
		if (strcmp(key, "branches") == 0)
		{
			if (!BSON_ITER_HOLDS_ARRAY(&documentIter))
			{
				ereport(ERROR, (errcode(MongoDollarSwitchRequiresArrayForBranches),
								errmsg(
									"$switch expected an array for 'branches', found: %s",
									BsonTypeName(bson_iter_type(&documentIter)))));
			}

			bson_iter_t childIter;
			bson_iter_recurse(&documentIter, &childIter);

			while (bson_iter_next(&childIter))
			{
				SwitchBranch *branchDef = ParseBranchForSwitch(&childIter);

				/* First we need to parse the branches without evaluating them, as in native mongo,
				 * parsing errors come first. */
				branches = lappend(branches, branchDef);
			}
		}
		else if (strcmp(key, "default") == 0)
		{
			defaultExpression = bson_iter_value(&documentIter);
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarSwitchBadArgument), errmsg(
								"$switch found an unknown argument: %s", key)));
		}
	}

	if (list_length(branches) <= 0)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresAtLeastOneBranch), errmsg(
							"$switch requires at least one branch.")));
	}

	bson_value_t result;
	bool foundTrueCase = false;
	bool isNullOnEmpty = false;
	ListCell *branchCell = NULL;
	foreach(branchCell, branches)
	{
		CHECK_FOR_INTERRUPTS();

		SwitchBranch *branchDef = (SwitchBranch *) lfirst(branchCell);
		bson_value_t caseValue = EvaluateExpressionAndGetValue(doc,
															   &(branchDef->caseExpression),
															   expressionResult,
															   isNullOnEmpty);

		if (BsonValueAsBool(&caseValue))
		{
			result = EvaluateExpressionAndGetValue(doc,
												   &(branchDef->thenExpression),
												   expressionResult, isNullOnEmpty);
			foundTrueCase = true;
			break;
		}
	}

	if (!foundTrueCase && defaultExpression)
	{
		result = EvaluateExpressionAndGetValue(doc, defaultExpression,
											   expressionResult, isNullOnEmpty);
	}
	else if (!foundTrueCase)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchNoMatchingBranchAndNoDefault), errmsg(
							"$switch could not find a matching branch for an input, and no default was specified.")));
	}

	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}

	list_free_deep(branches);
}


static SwitchBranch *
ParseBranchForSwitch(bson_iter_t *iter)
{
	if (!BSON_ITER_HOLDS_DOCUMENT(iter))
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresObjectForEachBranch), errmsg(
							"$switch expected each branch to be an object, found: %s",
							BsonTypeName(bson_iter_type(iter)))));
	}

	bson_iter_t childIter;
	bson_iter_recurse(iter, &childIter);

	SwitchBranch *branchDef = palloc(sizeof(SwitchBranch));
	bool isCaseMissing = true;
	bool isThenMissing = true;
	while (bson_iter_next(&childIter))
	{
		const char *key = bson_iter_key(&childIter);
		const bson_value_t *currentValue = bson_iter_value(&childIter);
		if (strcmp(key, "case") == 0)
		{
			isCaseMissing = false;
			branchDef->caseExpression = *currentValue;
		}
		else if (strcmp(key, "then") == 0)
		{
			isThenMissing = false;
			branchDef->thenExpression = *currentValue;
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarSwitchUnknownArgumentForBranch), errmsg(
								"$switch found an unknown argument to a branch: %s",
								key)));
		}
	}

	if (isCaseMissing)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresCaseExpressionForBranch),
						errmsg(
							"$switch requires each branch have a 'case' expression")));
	}
	else if (isThenMissing)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresThenExpressionForBranch),
						errmsg(
							"$switch requires each branch have a 'then' expression")));
	}

	return branchDef;
}
