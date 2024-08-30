/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/operators/bson_expression_conditional_operators.c
 *
 * Conditional Operator expression implementations of BSON.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>

#include "io/helio_bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/mongo_errors.h"

/* --------------------------------------------------------- */
/* Type definitions */
/* --------------------------------------------------------- */

/* Represents an arg entry(branch and default) for $switch operator */
typedef struct SwitchEntry
{
	AggregationExpressionData *caseExpression;
	AggregationExpressionData *thenExpression;
} SwitchEntry;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static SwitchEntry * ParseBranchForSwitch(bson_iter_t *iter, bool *allArgumentsConstant,
										  ParseAggregationExpressionContext *context);


/*
 * Parses an $ifNull expression. expression and sets the parsed data in the data argument.
 * $ifNull is expressed as { "$ifNull": [ <expression1>, <expression2>, ..., <replacement-expression-if-null> ] }
 */
void
ParseDollarIfNull(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	int numArgs = argument->value_type == BSON_TYPE_ARRAY ?
				  BsonDocumentValueCountKeys(argument) : 1;

	if (numArgs < 2)
	{
		ereport(ERROR, (errcode(MongoDollarIfNullRequiresAtLeastTwoArgs), errmsg(
							"$ifNull needs at least two arguments, had: %d",
							numArgs)));
	}

	bool areArgumentsConstant;
	List *argumentsList = ParseVariableArgumentsForExpression(argument,
															  &areArgumentsConstant,
															  context);

	if (areArgumentsConstant)
	{
		data->value.value_type = BSON_TYPE_NULL;

		int idx = 0;
		while (argumentsList != NIL && idx < argumentsList->length)
		{
			AggregationExpressionData *currentData = list_nth(argumentsList, idx);

			if (IsExpressionResultNullOrUndefined(&data->value))
			{
				data->value = currentData->value;
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


/*
 * Evaluates the output of a $ifNull expression.
 * Since $ifNull is expressed as { "$ifNull": [ <expression1>, <expression2>, ..., <replacement-expression-if-null> ] }
 * We evaluate every expression and return the first that is not null or undefined, otherwise we return the last expression in the array.
 * $ifNull requires at least 2 arguments.
 */
void
HandlePreParsedDollarIfNull(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	List *argumentList = (List *) arguments;
	bson_value_t result =
	{
		.value_type = BSON_TYPE_NULL
	};

	int idx = 0;
	while (argumentList != NIL && idx < argumentList->length)
	{
		AggregationExpressionData *currentData = list_nth(argumentList, idx);

		bool isNullOnEmpty = false;
		ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);
		EvaluateAggregationExpressionData(currentData, doc, &childResult, isNullOnEmpty);

		if (IsExpressionResultNullOrUndefined(&result))
		{
			result = childResult.value;
		}

		idx++;
	}

	/* if the last argument resulted in EOD, native mongo doesn't return any result, i.e: missing field. */
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


/*
 * Parses an $cond expression. expression and sets the parsed data in the data argument.
 * $cond is expressed as { "$cond": [ <if-expression>, <then-expression>, <else-expression> ] } or
 * { "$cond": { "if": <expression>, "then": <expression>, "else": <expression> }}
 */
void
ParseDollarCond(const bson_value_t *argument, AggregationExpressionData *data,
				ParseAggregationExpressionContext *context)
{
	List *argumentsList = NIL;
	AggregationExpressionData *ifData = palloc0(sizeof(AggregationExpressionData));
	AggregationExpressionData *thenData = palloc0(sizeof(AggregationExpressionData));
	AggregationExpressionData *elseData = palloc0(sizeof(AggregationExpressionData));

	if (argument->value_type == BSON_TYPE_DOCUMENT)
	{
		bool isIfMissing = true;
		bool isThenMissing = true;
		bool isElseMissing = true;

		bson_iter_t documentIter;
		BsonValueInitIterator(argument, &documentIter);

		while (bson_iter_next(&documentIter))
		{
			const char *key = bson_iter_key(&documentIter);
			const bson_value_t *currentValue = bson_iter_value(&documentIter);

			if (strcmp(key, "if") == 0)
			{
				isIfMissing = false;
				ParseAggregationExpressionData(ifData, currentValue, context);
			}
			else if (strcmp(key, "then") == 0)
			{
				isThenMissing = false;
				ParseAggregationExpressionData(thenData, currentValue, context);
			}
			else if (strcmp(key, "else") == 0)
			{
				isElseMissing = false;
				ParseAggregationExpressionData(elseData, currentValue, context);
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

		argumentsList = list_make3(ifData, thenData, elseData);
	}
	else
	{
		int requiredArgs = 3;
		argumentsList = ParseFixedArgumentsForExpression(argument, requiredArgs, "$cond",
														 &data->operator.argumentsKind,
														 context);
		ifData = list_nth(argumentsList, 0);
		thenData = list_nth(argumentsList, 1);
		elseData = list_nth(argumentsList, 2);
	}

	if (IsAggregationExpressionConstant(ifData) && IsAggregationExpressionConstant(
			thenData) && IsAggregationExpressionConstant(elseData))
	{
		bson_value_t result = { 0 };
		result = BsonValueAsBool(&ifData->value) ? thenData->value :
				 elseData->value;

		/* If resulted is EOD because of field not found, should be a no-op. */
		if (result.value_type != BSON_TYPE_EOD)
		{
			data->value = result;
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


/*
 * Evaluates the output of a $cond expression.
 * Since $cond is expressed as { "$cond": [ <if-expression>, <then-expression>, <else-expression> ] } or
 * { "$cond": { "if": <expression>, "then": <expression>, "else": <expression> }}
 * We evaluate the <if-expression>, if its result is true we return the <then-expression>,
 * otherwise we return the <else-expression>.
 */
void
HandlePreParsedDollarCond(pgbson *doc, void *arguments,
						  ExpressionResult *expressionResult)
{
	List *argumentList = (List *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);

	AggregationExpressionData *ifData = list_nth(argumentList, 0);
	EvaluateAggregationExpressionData(ifData, doc, &childResult, isNullOnEmpty);
	bson_value_t ifValue = childResult.value;

	ExpressionResultReset(&childResult);

	AggregationExpressionData *thenData = list_nth(argumentList, 1);
	EvaluateAggregationExpressionData(thenData, doc, &childResult, isNullOnEmpty);
	bson_value_t thenValue = childResult.value;

	ExpressionResultReset(&childResult);

	AggregationExpressionData *elseData = list_nth(argumentList, 2);
	EvaluateAggregationExpressionData(elseData, doc, &childResult, isNullOnEmpty);
	bson_value_t elseValue = childResult.value;

	bson_value_t result = { 0 };
	result = BsonValueAsBool(&ifValue) ? thenValue :
			 elseValue;

	/* If resulted is EOD because of field not found, should be a no-op. */
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


/*
 * Parses an $switch expression. expression and sets the parsed data in the data argument.
 * $switch is expressed as { "$switch": { "branches": [ {"case": <expression>, "then": <expression>}, ...], "default": <expression>} }
 */
void
ParseDollarSwitch(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresObject), errmsg(
							"$switch requires an object as an argument, found: %s",
							BsonTypeName(argument->value_type))));
	}

	bool allArgumentsConstant = true;
	List *arguments = NIL;
	SwitchEntry *defaultExpression = NULL;

	bson_iter_t documentIter;
	BsonValueInitIterator(argument, &documentIter);
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
				SwitchEntry *branchDef = ParseBranchForSwitch(&childIter,
															  &allArgumentsConstant,
															  context);

				/* First we need to parse the branches without evaluating them, as in native mongo,
				 * parsing errors come first. */
				arguments = lappend(arguments, branchDef);
			}
		}
		else if (strcmp(key, "default") == 0)
		{
			/* It will be hit if no branch has previously been hit. */
			bson_value_t defaultCase = {
				.value_type = BSON_TYPE_BOOL, .value.v_bool = true
			};

			defaultExpression = palloc0(sizeof(SwitchEntry));
			defaultExpression->caseExpression = palloc0(
				sizeof(AggregationExpressionData)),
			defaultExpression->thenExpression = palloc0(
				sizeof(AggregationExpressionData));

			ParseAggregationExpressionData(defaultExpression->caseExpression,
										   &defaultCase, context);
			ParseAggregationExpressionData(defaultExpression->thenExpression,
										   bson_iter_value(&documentIter), context);

			allArgumentsConstant = allArgumentsConstant &&
								   IsAggregationExpressionConstant(
				defaultExpression->caseExpression) &&
								   IsAggregationExpressionConstant(
				defaultExpression->thenExpression);
		}
		else
		{
			ereport(ERROR, (errcode(MongoDollarSwitchBadArgument), errmsg(
								"$switch found an unknown argument: %s", key)));
		}
	}

	if (list_length(arguments) <= 0)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresAtLeastOneBranch), errmsg(
							"$switch requires at least one branch.")));
	}

	/* The default expression will be the last item in our arguments list. */
	if (defaultExpression != NULL)
	{
		arguments = lappend(arguments, defaultExpression);
	}

	if (allArgumentsConstant)
	{
		bson_value_t result = { 0 };
		bool foundTrueCase = false;
		ListCell *branchCell = NULL;
		foreach(branchCell, arguments)
		{
			CHECK_FOR_INTERRUPTS();

			SwitchEntry *branchDef = (SwitchEntry *) lfirst(branchCell);

			/* No default expression. */
			if (branchDef == NULL)
			{
				continue;
			}

			bson_value_t caseValue = branchDef->caseExpression->value;
			if (BsonValueAsBool(&caseValue))
			{
				result = branchDef->thenExpression->value;
				foundTrueCase = true;
				break;
			}
		}

		/* If no switch branch matches, the default expression will match as it has caseValue true*/
		if (!foundTrueCase)
		{
			ereport(ERROR, (errcode(MongoDollarSwitchNoMatchingBranchAndNoDefault),
							errmsg(
								"$switch could not find a matching branch for an input, and no default was specified.")));
		}

		if (result.value_type != BSON_TYPE_EOD)
		{
			data->value = result;
		}

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
	}
	else
	{
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
	}
}


/*
 * Evaluates the output of a $switch expression.
 * Since $switch is expressed as { "$switch": { "branches": [ {"case": <expression>, "then": <expression>}, ...], "default": <expression>} }
 * We evaluate the <case-expression> for each branch, if it is true we return the <then-expression>,
 * otherwise we return the <default-expression> if it exists.
 */
void
HandlePreParsedDollarSwitch(pgbson *doc, void *arguments,
							ExpressionResult *expressionResult)
{
	List *argumentList = (List *) arguments;

	bool isNullOnEmpty = false;
	ExpressionResult childResult = ExpressionResultCreateChild(expressionResult);

	bson_value_t result = { 0 };
	bool foundTrueCase = false;
	ListCell *branchCell = NULL;
	foreach(branchCell, argumentList)
	{
		CHECK_FOR_INTERRUPTS();

		SwitchEntry *branchDef = (SwitchEntry *) lfirst(branchCell);

		/* No default expression. */
		if (branchDef == NULL)
		{
			continue;
		}

		AggregationExpressionData *caseData = branchDef->caseExpression;
		EvaluateAggregationExpressionData(caseData, doc, &childResult, isNullOnEmpty);
		bson_value_t caseValue = childResult.value;

		ExpressionResultReset(&childResult);

		if (BsonValueAsBool(&caseValue))
		{
			AggregationExpressionData *thenData = branchDef->thenExpression;
			EvaluateAggregationExpressionData(thenData, doc, &childResult, isNullOnEmpty);
			result = childResult.value;
			foundTrueCase = true;
			break;
		}
	}

	/* If no match was found, then no switch branch matched and no default was provided.*/
	if (!foundTrueCase)
	{
		ereport(ERROR, (errcode(MongoDollarSwitchNoMatchingBranchAndNoDefault),
						errmsg(
							"$switch could not find a matching branch for an input, and no default was specified.")));
	}

	/* If resulted is EOD because of field not found, should be a no-op. */
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


static SwitchEntry *
ParseBranchForSwitch(bson_iter_t *iter, bool *allArgumentsConstant,
					 ParseAggregationExpressionContext *context)
{
	if (!BSON_ITER_HOLDS_DOCUMENT(iter))
	{
		ereport(ERROR, (errcode(MongoDollarSwitchRequiresObjectForEachBranch), errmsg(
							"$switch expected each branch to be an object, found: %s",
							BsonTypeName(bson_iter_type(iter)))));
	}

	bson_iter_t childIter;
	bson_iter_recurse(iter, &childIter);

	SwitchEntry *branchDef = palloc(sizeof(SwitchEntry));
	branchDef->caseExpression = palloc0(sizeof(AggregationExpressionData));
	branchDef->thenExpression = palloc0(sizeof(AggregationExpressionData));

	bool isCaseMissing = true;
	bool isThenMissing = true;
	while (bson_iter_next(&childIter))
	{
		const char *key = bson_iter_key(&childIter);
		const bson_value_t *currentValue = bson_iter_value(&childIter);
		if (strcmp(key, "case") == 0)
		{
			isCaseMissing = false;
			ParseAggregationExpressionData(branchDef->caseExpression, currentValue,
										   context);
			*allArgumentsConstant = *allArgumentsConstant &&
									IsAggregationExpressionConstant(
				branchDef->caseExpression);
		}
		else if (strcmp(key, "then") == 0)
		{
			isThenMissing = false;
			ParseAggregationExpressionData(branchDef->thenExpression, currentValue,
										   context);
			*allArgumentsConstant = *allArgumentsConstant &&
									IsAggregationExpressionConstant(
				branchDef->thenExpression);
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
