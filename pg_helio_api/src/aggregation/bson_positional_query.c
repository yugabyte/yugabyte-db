/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_positional_query.c
 *
 * Implementation of the BSON Positional $ operator (shared between projection and update)
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <catalog/pg_collation.h>
#include <nodes/makefuncs.h>

#include "io/bson_core.h"
#include "aggregation/bson_positional_query.h"
#include "query/query_operator.h"
#include "operators/bson_expr_eval.h"
#include "metadata/metadata_cache.h"
#include "io/bson_traversal.h"


/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * An entry for every path in the top level query
 * and the associated execution context state that
 * will be used to evaluate the expression.
 */
typedef struct BsonPositionalQueryQual
{
	/* The path to the query filter (e.g. a.b.c) */
	const char *path;

	/* The expression to evaluate for this filter */
	ExprEvalState *evalState;

	/* Whether or not the filter only matches an array */
	bool isArrayMatch;

	/* Whether or not it's an empty elemMatch statement */
	bool isEmptyElemMatch;
} BsonPositionalQueryQual;

/*
 * A list of BsonPositionalQueryQual for each
 * qualifier found within a top level query.
 */
typedef struct BsonPositionalQueryData
{
	List *queryQuals;
} BsonPositionalQueryData;

/*
 * State passed to TraverseBson When traversing a source
 * document to extract a positional match
 * expression.
 */
typedef struct TraverseBsonPositionalQualState
{
	/* The expression to evaluate at the leaf */
	ExprEvalState *evalState;

	/* Output variable - the matched index (if any) */
	int32_t matchIndex;

	/* When traversing intermediate arrays, holds the currently
	 * matching array index */
	int32_t currentIntermediateIndex;

	/* Whether or not this qual is a match on an array */
	bool isArrayMatch;

	/* Whether or not it's an empty elemMatch query */
	bool isEmptyElemMatch;
} TraverseBsonPositionalQualState;


/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void UpdateQualsListFromQuery(List *queryQuals, List **finalList, bool
									 isArrayMatch, const char *pathOverride);
static void ProcessSingleFuncExpr(FuncExpr *expr, List **finalList, bool isArrayMatch,
								  const char *pathOverride);

static bool PositionalQueryVisitTopLevelField(pgbsonelement *element, const
											  StringView *filterPath,
											  void *state);
static bool PositionalQueryVisitArrayField(pgbsonelement *element, const
										   StringView *filterPath,
										   int arrayIndex, void *state);
static bool PositionalQueryContinueProcessIntermediateArray(void *state, const
															bson_value_t *value);
static void PositionalSetIntermediateArrayIndex(void *state, int32_t index);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

/*
 * Given an input document query, builds a positional query metadata
 * object that is used in evaluation of the positional $ operator.
 */
BsonPositionalQueryData *
GetPositionalQueryData(const pgbson *query)
{
	/* Step 1: Create Quals for the query based on BSON value inputs */
	List *queryQuals = CreateQualsForBsonValueTopLevelQuery(query);

	List *finalQuals = NIL;

	/* Step 2: Walk the query quals backwards and compile the expressions to be evaluated */
	bool isArrayMatch = false;
	char *pathOverride = NULL;
	UpdateQualsListFromQuery(queryQuals, &finalQuals, isArrayMatch, pathOverride);

	BsonPositionalQueryData *data = palloc(sizeof(BsonPositionalQueryData));
	data->queryQuals = finalQuals;
	return data;
}


/*
 * Walks the source document based on the query filters and finds the
 * first matching array index based on the query cached.
 * Returns -1 if no array index was matched.
 */
int32_t
MatchPositionalQueryAgainstDocument(const BsonPositionalQueryData *data, const
									pgbson *document)
{
	if (data == NULL)
	{
		return -1;
	}

	bson_iter_t documentIterator;
	ListCell *cell;
	const TraverseBsonExecutionFuncs execFuncs =
	{
		.ContinueProcessIntermediateArray =
			PositionalQueryContinueProcessIntermediateArray,
		.SetTraverseResult = NULL,
		.VisitArrayField = PositionalQueryVisitArrayField,
		.VisitTopLevelField = PositionalQueryVisitTopLevelField,
		.SetIntermediateArrayIndex = PositionalSetIntermediateArrayIndex,
	};

	/* Walk the quals and find a match */
	foreach(cell, data->queryQuals)
	{
		BsonPositionalQueryQual *qual = lfirst(cell);

		PgbsonInitIterator(document, &documentIterator);

		TraverseBsonPositionalQualState traverseState =
		{
			.evalState = qual->evalState,
			.matchIndex = -1,
			.currentIntermediateIndex = -1,
			.isArrayMatch = qual->isArrayMatch,
			.isEmptyElemMatch = qual->isEmptyElemMatch
		};

		/* Traverse the source document and try to find a match */
		TraverseBson(&documentIterator, qual->path, &traverseState, &execFuncs);
		if (traverseState.matchIndex >= 0)
		{
			return traverseState.matchIndex;
		}
	}

	/* No query matched - return -1 */
	return -1;
}


/*
 * Walks the query qualifiers and builds a list of qualifiers that contain expressions
 * used in the execution phase to do a positional query match.
 */
static void
UpdateQualsListFromQuery(List *queryQuals, List **finalList, bool isArrayMatch, const
						 char *pathOverride)
{
	check_stack_depth();
	for (int i = list_length(queryQuals) - 1; i >= 0; i--)
	{
		Expr *queryCell = list_nth(queryQuals, i);
		if (IsA(queryCell, FuncExpr))
		{
			ProcessSingleFuncExpr((FuncExpr *) queryCell, finalList, isArrayMatch,
								  pathOverride);
		}
		else if (IsA(queryCell, BoolExpr))
		{
			BoolExpr *boolExpr = (BoolExpr *) queryCell;
			if (boolExpr->boolop != AND_EXPR)
			{
				/* Skip OR, NOT etc. */
				continue;
			}

			/* Build the queryList as the last of the AND clauses */
			UpdateQualsListFromQuery(boolExpr->args, finalList, isArrayMatch,
									 pathOverride);
		}
	}
}


/*
 * Given a leaf FuncExpr, processes the expression and adds it to the list
 * of quals for the $ positional operator based on the type of function
 * qualifier.
 */
static void
ProcessSingleFuncExpr(FuncExpr *expr, List **finalList, bool isArrayMatch, const
					  char *pathOverride)
{
	if (list_length(expr->args) != 2)
	{
		return;
	}

	pgbsonelement singleElement;
	Expr *secondArg = lsecond(expr->args);
	Assert(IsA(secondArg, Const));
	Const *argConst = (Const *) secondArg;
	PgbsonToSinglePgbsonElement(DatumGetPgBson(argConst->constvalue),
								&singleElement);

	/* The path becomes the key to the list - the value converts to the FuncExpr */
	const char *path;
	if (pathOverride != NULL)
	{
		path = pathOverride;
	}
	else
	{
		path = pnstrdup(singleElement.path, singleElement.pathLength);
	}

	/* Special case, $all evaluates an array but we want the index of the first match of $all
	 * Therefore, we walk the $all to find any $elemMatches inside. If there are no $elemMatch
	 * elements, we convert the $all to a $in to get the first match. If there are $elemMatches
	 * this is treated as a $and and we get the last $elemMatch applicable
	 */
	if (expr->funcid == BsonValueAllMatchFunctionId())
	{
		/* Substitute the $all into the nested expr to get the first match */
		bson_iter_t allIter;
		BsonValueInitIterator(&singleElement.bsonValue, &allIter);

		/* First check if there's any $elemmatch's in the $all */
		bool isElemMatchExpression = false;
		while (bson_iter_next(&allIter))
		{
			pgbsonelement allElement;
			if (BSON_ITER_HOLDS_DOCUMENT(&allIter) &&
				TryGetBsonValueToPgbsonElement(bson_iter_value(&allIter), &allElement) &&
				strcmp(allElement.path, "$elemMatch") == 0)
			{
				isElemMatchExpression = true;
				break;
			}
		}

		if (isElemMatchExpression)
		{
			/* If there is an $elemMatch, treat multiple $elemMatches as a $and
			 * If there's only 1 elemMatch treat it as a regular $elemMatch
			 */
			Expr *nestedExpr = CreateQualForBsonValueArrayExpression(
				&singleElement.bsonValue);
			if (IsA(nestedExpr, BoolExpr))
			{
				BoolExpr *boolExpr = (BoolExpr *) nestedExpr;
				bool isArrayMatchInner = true;
				UpdateQualsListFromQuery(boolExpr->args, finalList, isArrayMatchInner,
										 path);
				return;
			}
			else if (IsA(nestedExpr, FuncExpr))
			{
				/* Evaluating with the nested expression with the current path */
				expr = (FuncExpr *) nestedExpr;
				bool isArrayMatchInner = true;
				ProcessSingleFuncExpr(expr, finalList, isArrayMatchInner, path);
				return;
			}

			/* $all had no valid matches */
			return;
		}
		else
		{
			/* Simple primitives for $all - treat as $in */
			isArrayMatch = true;
			expr->funcid = BsonValueInMatchFunctionId();
		}
	}


	BsonPositionalQueryQual *qual = palloc(sizeof(BsonPositionalQueryQual));
	qual->path = path;
	qual->isArrayMatch = isArrayMatch;
	qual->isEmptyElemMatch = false;
	if (expr->funcid == BsonValueElemMatchMatchFunctionId())
	{
		if (IsBsonValueEmptyDocument(&singleElement.bsonValue))
		{
			/* Empty elemMatch - matches only documents and arrays */
			qual->isEmptyElemMatch = true;
		}

		/* Refer to the actual quals of elemMatch since we evaluate inside arrays ourselves */
		qual->evalState = GetExpressionEvalState(&singleElement.bsonValue,
												 CurrentMemoryContext);
		qual->isArrayMatch = true;
	}
	else
	{
		singleElement.path = "";
		singleElement.pathLength = 0;
		argConst->constvalue = PointerGetDatum(PgbsonElementToPgbson(
												   &singleElement));
		qual->evalState = GetExpressionEvalStateFromFuncExpr(expr,
															 CurrentMemoryContext);
	}

	*finalList = lappend(*finalList, qual);
}


/*
 * Handle intermediate arrays during document traversal.
 * When we encounter an intermediate array, we reset the intermediate
 * array index and continue traversing if needed (no match has been found).
 */
static bool
PositionalQueryContinueProcessIntermediateArray(void *state, const
												bson_value_t *value)
{
	TraverseBsonPositionalQualState *queryState =
		(TraverseBsonPositionalQualState *) state;

	/* Not currently processing an index */
	queryState->currentIntermediateIndex = -1;

	/* Continue if there hasn't been a match */
	return queryState->matchIndex < 0;
}


/*
 * Handle a top level field at the path.
 */
static bool
PositionalQueryVisitTopLevelField(pgbsonelement *element,
								  const StringView *filterPath,
								  void *state)
{
	TraverseBsonPositionalQualState *queryState =
		(TraverseBsonPositionalQualState *) state;

	/* For top level fields, evaluate if we're inside an intermediate array
	 * to handle scenarios like a.b : 5 where a is an array. This is only
	 * done if it's not an array level match (like $elemmatch or $all)
	 */
	if (queryState->currentIntermediateIndex >= 0 && !queryState->isArrayMatch)
	{
		bool shouldRecurseIfArray = false;
		bool result = EvalBooleanExpressionAgainstValue(queryState->evalState,
														&element->bsonValue,
														shouldRecurseIfArray);
		if (result)
		{
			queryState->matchIndex = queryState->currentIntermediateIndex;

			/* Found a match can stop traversing */
			return false;
		}
	}
	return true;
}


/*
 * Handle an array field at the path. We evaluate conditions here for that path
 * and store the index that matched for the path.
 */
static bool
PositionalQueryVisitArrayField(pgbsonelement *element, const StringView *filterPath,
							   int arrayIndex, void *state)
{
	TraverseBsonPositionalQualState *queryState =
		(TraverseBsonPositionalQualState *) state;
	bool result;
	if (queryState->isEmptyElemMatch)
	{
		result = element->bsonValue.value_type == BSON_TYPE_DOCUMENT ||
				 element->bsonValue.value_type == BSON_TYPE_ARRAY;
	}
	else
	{
		bool shouldRecurseIfArray = false;
		result = EvalBooleanExpressionAgainstValue(queryState->evalState,
												   &element->bsonValue,
												   shouldRecurseIfArray);
	}

	if (result)
	{
		queryState->matchIndex = arrayIndex;

		/* Found a match can stop traversing */
		return false;
	}

	/* Continue traversing */
	return true;
}


/*
 * When traversing an intermediate array, we store the index
 * we are currently traversing here. That way, when there is a match
 * we can return that index on a leaf path.
 */
static void
PositionalSetIntermediateArrayIndex(void *state, int32_t index)
{
	TraverseBsonPositionalQualState *queryState =
		(TraverseBsonPositionalQualState *) state;
	queryState->currentIntermediateIndex = index;
}
