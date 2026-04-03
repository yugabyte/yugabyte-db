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

#include "io/bson_core.h"
#include "operators/bson_expression.h"
#include "operators/bson_expression_operators.h"
#include "utils/documentdb_errors.h"

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
static SwitchEntry * ParseBranchForSwitch(bson_iter_t *iter,
										  ParseAggregationExpressionContext *context);


/*
 * Parses an $ifNull expression and sets the parsed data in the data argument.
 * $ifNull is expressed as { "$ifNull": [ <>, <>, ..., <result if null> ] }
 */
void
ParseDollarIfNull(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	int numArgs = argument->value_type == BSON_TYPE_ARRAY ?
				  BsonDocumentValueCountKeys(argument) : 1;

	if (numArgs < 2)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARIFNULLREQUIRESATLEASTTWOARGS),
						errmsg(
							"Expression $ifNull requires at least two provided arguments, but received only %d.",
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
 * Since $ifNull is expressed as { "$ifNull": [ <>, <>, ..., <result if null> ] }
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

	/* if the last argument resulted in EOD, do not return any result (missing field). */
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


/*
 * Parses an $cond expression and sets the parsed data in the data argument.
 * $cond is expressed as { "$cond": [ if, then, else ] } or
 * { "$cond": { "if": <>, "then": <>, "else": <> }}
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
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARCONDBADPARAMETER),
								errmsg(
									"Unrecognized argument provided to operators $cond: %s",
									key)));
			}
		}

		if (isIfMissing)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARCONDMISSINGIFPARAMETER),
							errmsg("'if' parameter is missing in the $cond operator")));
		}
		else if (isThenMissing)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARCONDMISSINGTHENPARAMETER),
							errmsg("'then' parameter is missing in the $cond operator")));
		}
		else if (isElseMissing)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARCONDMISSINGELSEPARAMETER),
							errmsg("'else' parameter is missing in the $cond operator")));
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

	if (IsAggregationExpressionConstant(ifData))
	{
		bool ifResult = BsonValueAsBool(&ifData->value);
		if (ifResult && IsAggregationExpressionConstant(thenData))
		{
			/* safety check but unlikely */
			if (thenData->value.value_type != BSON_TYPE_EOD)
			{
				data->value = thenData->value;
			}

			data->kind = AggregationExpressionKind_Constant;
			list_free_deep(argumentsList);
			return;
		}
		else if (!ifResult && IsAggregationExpressionConstant(elseData))
		{
			/* safety check but unlikely */
			if (elseData->value.value_type != BSON_TYPE_EOD)
			{
				data->value = elseData->value;
			}

			data->kind = AggregationExpressionKind_Constant;
			list_free_deep(argumentsList);
			return;
		}
	}

	data->operator.arguments = argumentsList;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
}


/*
 * Evaluates the output of a $cond expression.
 * Since $cond is expressed as { "$cond": [ if, then, else ] } or
 * { "$cond": { "if": <>, "then": <>, "else": <> }}
 * We evaluate the if argument, if its result is true we return the then evaluated expression,
 * otherwise we return the else evaluated expression.
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

	if (BsonValueAsBool(&ifValue))
	{
		/* short-circuit */
		AggregationExpressionData *thenData = list_nth(argumentList, 1);
		EvaluateAggregationExpressionData(thenData, doc, &childResult, isNullOnEmpty);
		bson_value_t thenValue = childResult.value;

		/* If value is EOD because of field not found, should be a no-op. */
		if (thenValue.value_type != BSON_TYPE_EOD)
		{
			ExpressionResultSetValue(expressionResult, &thenValue);
		}

		return;
	}

	AggregationExpressionData *elseData = list_nth(argumentList, 2);
	EvaluateAggregationExpressionData(elseData, doc, &childResult, isNullOnEmpty);
	bson_value_t elseValue = childResult.value;

	/* If value is EOD because of field not found, should be a no-op. */
	if (elseValue.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &elseValue);
	}
}


/*
 * Parses an $switch expression. expression and sets the parsed data in the data argument.
 * $switch is expressed as { "$switch": { "branches": [ {"case": <>, "then": <>}, ...], "default": <>} }
 */
void
ParseDollarSwitch(const bson_value_t *argument, AggregationExpressionData *data,
				  ParseAggregationExpressionContext *context)
{
	if (argument->value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARSWITCHREQUIRESOBJECT), errmsg(
							"The $switch expression requires an object as its argument, but instead received: %s",
							BsonTypeName(argument->value_type))));
	}

	bool allBranchesConstant = true;
	bool allCasesFalseConstants = true;
	bool allPriorCasesFalseConstants = true;
	int firstConstantTrueBranchIndex = -1;
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
				ereport(ERROR, (errcode(
									ERRCODE_DOCUMENTDB_DOLLARSWITCHREQUIRESARRAYFORBRANCHES),
								errmsg(
									"$switch requires an array for 'branches', but received: %s",
									BsonTypeName(bson_iter_type(&documentIter)))));
			}

			bson_iter_t childIter;
			bson_iter_recurse(&documentIter, &childIter);

			int branchIndex = 0;
			while (bson_iter_next(&childIter))
			{
				/* Parse the branch - errors are caught during parsing */
				SwitchEntry *branchDef = ParseBranchForSwitch(&childIter, context);

				/* Check if this branch is fully constant */
				bool isBranchConstant =
					IsAggregationExpressionConstant(branchDef->caseExpression) &&
					IsAggregationExpressionConstant(branchDef->thenExpression);

				/* Check if the case is constant and evaluates to a boolean */
				bool caseIsConstantTrue = isBranchConstant &&
										  BsonValueAsBool(
					&branchDef->caseExpression->value);

				bool caseIsConstantFalse = isBranchConstant &&
										   !BsonValueAsBool(
					&branchDef->caseExpression->value);

				/*
				 * Track if we can optimize to a constant true branch.
				 * We can only optimize if ALL prior branches are constant false
				 * AND the current branch is fully constant (both case and then).
				 */
				if (caseIsConstantTrue &&
					allPriorCasesFalseConstants &&
					firstConstantTrueBranchIndex == -1)
				{
					firstConstantTrueBranchIndex = branchIndex;
				}

				/*
				 * Update tracking flags:
				 * - allBranchesConstant: true only if all branches are constant
				 * - allCasesFalseConstants: true only if all case expressions are constant false
				 * - allPriorCasesFalseConstants: true only if all prior cases are constant false
				 */
				allBranchesConstant = allBranchesConstant && isBranchConstant;
				allCasesFalseConstants = allCasesFalseConstants && caseIsConstantFalse;

				/*
				 * If this branch is non-constant or constant true,
				 * then we can't optimize any later constant true branches.
				 */
				if (!caseIsConstantFalse)
				{
					allPriorCasesFalseConstants = false;
				}

				arguments = lappend(arguments, branchDef);
				branchIndex++;
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
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARSWITCHBADARGUMENT), errmsg(
								"Unknown argument: %s", key)));
		}
	}

	if (list_length(arguments) <= 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_DOLLARSWITCHREQUIRESATLEASTONEBRANCH),
						errmsg(
							"$switch must contain at least one branch.")));
	}

	/*
	 * Short-circuit optimization: If we found a constant true branch where
	 * all prior branches are constant false, we can optimize to that branch.
	 */
	if (firstConstantTrueBranchIndex >= 0)
	{
		SwitchEntry *branchDef = (SwitchEntry *) list_nth(arguments,
														  firstConstantTrueBranchIndex);

		/* no-op if EOD */
		if (branchDef->thenExpression->value.value_type != BSON_TYPE_EOD)
		{
			data->value = branchDef->thenExpression->value;
		}

		data->kind = AggregationExpressionKind_Constant;
		list_free_deep(arguments);
		return;
	}

	/* If all branches are constant but none matched so far */
	/* OR: if all cases were constant but none was true, we can use the default */
	if (allBranchesConstant || allCasesFalseConstants)
	{
		/* No default expression is available. */
		if (defaultExpression == NULL)
		{
			ereport(ERROR, (errcode(
								ERRCODE_DOCUMENTDB_DOLLARSWITCHNOMATCHINGBRANCHANDNODEFAULT),
							errmsg(
								"The $switch operator failed to locate a matching branch for "
								"the provided input, and no default branch was defined.")));
		}

		/* If default expression is constant, we can return it as the result. */
		if (IsAggregationExpressionConstant(defaultExpression->thenExpression))
		{
			/* no-op if EOD */
			if (defaultExpression->thenExpression->value.value_type != BSON_TYPE_EOD)
			{
				data->value = defaultExpression->thenExpression->value;
			}

			data->kind = AggregationExpressionKind_Constant;
			list_free_deep(arguments);
			return;
		}

		/* Otherwise, we pass down only the default expression as a switch branch */
		/* to be processed in runtime */
		list_free_deep(arguments);
		arguments = NIL;
		arguments = lappend(arguments, defaultExpression);
		data->operator.arguments = arguments;
		data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
		return;
	}


	/* The default expression will be the last item in our arguments list. */
	if (defaultExpression != NULL)
	{
		arguments = lappend(arguments, defaultExpression);
	}

	data->operator.arguments = arguments;
	data->operator.argumentsKind = AggregationExpressionArgumentsKind_List;
}


/*
 * Evaluates the output of a $switch expression.
 * Since $switch is expressed as { "$switch": { "branches": [ {"case": <>, "then": <>}, ...], "default": <>} }
 * We evaluate the case argument for each branch, if it is true we return the evaluated then for that branch,
 * otherwise we return the evaluated default if it exists.
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

		/* No default value expression. */
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
		ereport(ERROR, (errcode(
							ERRCODE_DOCUMENTDB_DOLLARSWITCHNOMATCHINGBRANCHANDNODEFAULT),
						errmsg(
							"The $switch operator failed to locate a matching branch for the provided input, and no default branch was defined.")));
	}

	/* If resulted is EOD because of field not found, should be a no-op. */
	if (result.value_type != BSON_TYPE_EOD)
	{
		ExpressionResultSetValue(expressionResult, &result);
	}
}


static SwitchEntry *
ParseBranchForSwitch(bson_iter_t *iter,
					 ParseAggregationExpressionContext *context)
{
	if (!BSON_ITER_HOLDS_DOCUMENT(iter))
	{
		ereport(ERROR, (errcode(
							ERRCODE_DOCUMENTDB_DOLLARSWITCHREQUIRESOBJECTFOREACHBRANCH),
						errmsg(
							"$switch requires each branch to be an object, but received: %s",
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
		}
		else if (strcmp(key, "then") == 0)
		{
			isThenMissing = false;
			ParseAggregationExpressionData(branchDef->thenExpression, currentValue,
										   context);
		}
		else
		{
			ereport(ERROR, (errcode(
								ERRCODE_DOCUMENTDB_DOLLARSWITCHUNKNOWNARGUMENTFORBRANCH),
							errmsg(
								"$switch encountered an unrecognized argument for a branch: %s",
								key)));
		}
	}

	if (isCaseMissing)
	{
		ereport(ERROR, (errcode(
							ERRCODE_DOCUMENTDB_DOLLARSWITCHREQUIRESCASEEXPRESSIONFORBRANCH),
						errmsg(
							"The $switch requires that every branch must contain a valid 'case' expression.")));
	}
	else if (isThenMissing)
	{
		ereport(ERROR, (errcode(
							ERRCODE_DOCUMENTDB_DOLLARSWITCHREQUIRESTHENEXPRESSIONFORBRANCH),
						errmsg(
							"The $switch requires that every branch must contain a valid 'then' expression.")));
	}

	return branchDef;
}
