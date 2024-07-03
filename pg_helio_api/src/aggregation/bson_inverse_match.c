/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_inverse_match.c
 *
 * Implementation of the $inverseMatch operator against every document in the aggregation.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/builtins.h>

#include "operators/bson_expr_eval.h"
#include "io/helio_bson_core.h"
#include "io/bson_traversal.h"
#include "operators/bson_expression.h"
#include "utils/mongo_errors.h"
#include "utils/fmgr_utils.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

/*
 * Intermediate state to track if the path was found in the document
 * to extract the query for an inverse match
 */
typedef struct InverseMatchTraverseState
{
	/* whether the path was found or not. */
	bool foundValue;

	/* the value in the specified path. */
	bson_value_t bsonValue;
} InverseMatchTraverseState;

/*
 * Struct that holds the args passed down to inverse match in order to cache it per query execution
 * so that we only parse it once per query.
 */
typedef struct InverseMatchArgs
{
	/* The path to look in the document for the query to execute. */
	StringView path;

	/* The argument to the executed query.
	 * it can be a constant or a path expression. */
	AggregationExpressionData queryInputExpression;

	/* This is the specified value for the default result in case a document
	 * doesn't define a value for the query path specified in the spec. */
	bson_value_t defaultResult;
} InverseMatchArgs;

static void PopulateInverseMatchArgs(InverseMatchArgs *args, bson_iter_t *specIter);
static void ValidateQueryInput(const bson_value_t *value);
static bool EvaluateInverseMatch(pgbson *document, const InverseMatchArgs *args);
static bool InverseMatchVisitTopLevelField(pgbsonelement *element, const
										   StringView *filterPath,
										   void *state);
static bool InverseMatchContinueProcessIntermediateArray(void *state, const
														 bson_value_t *value);

static const TraverseBsonExecutionFuncs InverseMatchExecutionFuncs = {
	.ContinueProcessIntermediateArray = InverseMatchContinueProcessIntermediateArray,
	.SetTraverseResult = NULL,
	.VisitArrayField = NULL,
	.VisitTopLevelField = InverseMatchVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL,
};

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_dollar_inverse_match);

/* Function that evaluates an inverse match against the given document with the given spec.
 * The spec is in the form of {"path": <path>, "input": <document or array of documents> }
 */
Datum
bson_dollar_inverse_match(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	pgbson *spec = PG_GETARG_PGBSON_PACKED(1);

	const InverseMatchArgs *args;
	int argPosition = 1;

	bson_iter_t specIter;
	PgbsonInitIterator(spec, &specIter);
	SetCachedFunctionState(
		args,
		InverseMatchArgs,
		argPosition,
		PopulateInverseMatchArgs,
		&specIter);

	bool result;
	if (args == NULL)
	{
		InverseMatchArgs inverseMatchArgs;
		PopulateInverseMatchArgs(&inverseMatchArgs, &specIter);
		result = EvaluateInverseMatch(document, &inverseMatchArgs);
	}
	else
	{
		result = EvaluateInverseMatch(document, args);
	}

	PG_FREE_IF_COPY(document, 0);
	PG_FREE_IF_COPY(spec, 1);
	PG_RETURN_BOOL(result);
}


/* Given the document and args it extracts the query at the specified path in the given document,
 * compiles the expression and evaluates it against the input value in the args and returns the result of the evaluation. */
static bool
EvaluateInverseMatch(pgbson *document, const InverseMatchArgs *args)
{
	bson_iter_t documentIter;
	PgbsonInitIterator(document, &documentIter);

	const char *queryPath = args->path.string;
	InverseMatchTraverseState traverseState = { 0 };
	TraverseBson(&documentIter, queryPath, &traverseState, &InverseMatchExecutionFuncs);

	if (!traverseState.foundValue)
	{
		if (args->defaultResult.value_type == BSON_TYPE_EOD)
		{
			ereport(ERROR, (errcode(MongoBadValue),
							errmsg(
								"$inverseMatch failed to find a value for path: '%s' in one of the documents, if this is expected, please provide a default result value in the stage spec.'",
								queryPath),
							errhint(
								"$inverseMatch failed to find a value for the specified path in one of the documents.")));
		}

		return args->defaultResult.value.v_bool;
	}

	bson_value_t queryValue = traverseState.bsonValue;
	if (queryValue.value_type != BSON_TYPE_DOCUMENT)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$inverseMatch expects the value in the path specified to be a document, got: '%s', for path: '%s'",
							BsonTypeName(queryValue.value_type),
							queryPath),
						errhint(
							"$inverseMatch expects the value in the path specified to be a document, got: '%s'",
							BsonTypeName(queryValue.value_type))));
	}

	MemoryContext memoryContext = CurrentMemoryContext;
	ExprEvalState *exprEvalState = GetExpressionEvalState(&queryValue, memoryContext);

	bson_value_t queryInput;
	if (args->queryInputExpression.kind == AggregationExpressionKind_Constant)
	{
		queryInput = args->queryInputExpression.value;
	}
	else
	{
		pgbson_writer valueWriter;
		pgbson_element_writer elementWriter;
		PgbsonWriterInit(&valueWriter);
		bool isNullOnEmpty = false;
		ExpressionVariableContext *variableContext = NULL;
		StringView path = { .string = "", .length = 0 };
		PgbsonInitObjectElementWriter(&valueWriter, &elementWriter, "", 0);
		EvaluateAggregationExpressionDataToWriter(&args->queryInputExpression, document,
												  path,
												  &valueWriter, variableContext,
												  isNullOnEmpty);

		queryInput = PgbsonElementWriterGetValue(&elementWriter);
		if (queryInput.value_type == BSON_TYPE_EOD ||
			queryInput.value_type == BSON_TYPE_UNDEFINED)
		{
			/* Couldn't resolve the input query value, return an empty document. */
			pgbson *emptyBson = PgbsonInitEmpty();
			queryInput = ConvertPgbsonToBsonValue(emptyBson);
		}

		ValidateQueryInput(&queryInput);
	}

	bson_type_t inputType = queryInput.value_type;
	bool result = false;

	if (inputType == BSON_TYPE_ARRAY)
	{
		result = EvalBooleanExpressionAgainstArray(exprEvalState, &queryInput);
	}
	else if (inputType == BSON_TYPE_DOCUMENT)
	{
		bool recurseIntoArray = false;
		result = EvalBooleanExpressionAgainstValue(exprEvalState, &queryInput,
												   recurseIntoArray);
	}
	else
	{
		FreeExprEvalState(exprEvalState, memoryContext);
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"inverse match expects query input element to be document or an array of documents got: %s",
							BsonTypeName(inputType))));
	}

	FreeExprEvalState(exprEvalState, memoryContext);
	return result;
}


/*
 * Parses the {"path": <document>, "input": <document or array of documents>, "defaultResult": <bool> }
 * and stores it into the args parameter. It just validates the input as the rest of validation is done at the top level query and transformed to the
 * expected spec at this stage of the query.
 */
static void
PopulateInverseMatchArgs(InverseMatchArgs *args, bson_iter_t *specIter)
{
	bson_value_t pathValue = { 0 };
	bson_value_t queryInput = { 0 };
	bson_value_t defaultResult = {
		.value_type = BSON_TYPE_BOOL,
		.value.v_bool = false,
	};

	while (bson_iter_next(specIter))
	{
		const char *key = bson_iter_key(specIter);
		if (strcmp(key, "path") == 0)
		{
			pathValue = *bson_iter_value(specIter);
		}
		else if (strcmp(key, "input") == 0)
		{
			queryInput = *bson_iter_value(specIter);
		}
		else if (strcmp(key, "defaultResult") == 0)
		{
			defaultResult = *bson_iter_value(specIter);
		}
	}

	args->path.length = pathValue.value.v_utf8.len;
	args->path.string = pathValue.value.v_utf8.str;

	/*
	 * We support parsing as an expression to support cases where the input might come from a previous stage,
	 * i.e: $project in order to build the input in a specific shape and reference it via a path expression.
	 */
	ParseAggregationExpressionData(&args->queryInputExpression, &queryInput);

	AggregationExpressionKind expressionKind = args->queryInputExpression.kind;
	if (expressionKind != AggregationExpressionKind_Constant &&
		expressionKind != AggregationExpressionKind_Path)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$inverseMatch expects 'input' to be a constant value or a string path expression.")));
	}

	if (expressionKind == AggregationExpressionKind_Constant)
	{
		ValidateQueryInput(&args->queryInputExpression.value);
	}

	if (defaultResult.value_type != BSON_TYPE_EOD)
	{
		args->defaultResult.value_type = BSON_TYPE_BOOL;
		args->defaultResult.value.v_bool = defaultResult.value.v_bool;
	}
}


static void
ValidateQueryInput(const bson_value_t *value)
{
	bson_type_t queryInputType = value->value_type;
	if (queryInputType != BSON_TYPE_DOCUMENT &&
		queryInputType != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg(
							"$inverseMatch requires that 'input' be a document or an array of documents, found: %s",
							BsonTypeName(queryInputType)),
						errhint(
							"$inverseMatch requires that 'input' be a document or an array of documents, found: %s",
							BsonTypeName(queryInputType))));
	}

	if (queryInputType == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		BsonValueInitIterator(value, &arrayIter);
		while (bson_iter_next(&arrayIter))
		{
			if (!BSON_ITER_HOLDS_DOCUMENT(&arrayIter))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"$inverseMatch requires that if 'input' is an array its contents be documents, found: %s",
									BsonIterTypeName(&arrayIter)),
								errhint(
									"$inverseMatch requires that if 'input' is an array its contents be documents, found: %s",
									BsonIterTypeName(&arrayIter))));
			}
		}
	}
}


/* This function extracts and stores the value to do the inverse match */
static bool
InverseMatchVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
							   void *state)
{
	InverseMatchTraverseState *inverseMatchState = (InverseMatchTraverseState *) state;

	inverseMatchState->foundValue = true;
	inverseMatchState->bsonValue = element->bsonValue;

	/* no need to keep traversing */
	return false;
}


/* This function stops the traversing of the bson in intermediate array fields. */
static bool
InverseMatchContinueProcessIntermediateArray(void *state, const
											 bson_value_t *value)
{
	ereport(ERROR, (errcode(MongoBadValue),
					errmsg(
						"$inverseMatch requires that the query value in path points to a document field but instead an array field was found.")));
}
