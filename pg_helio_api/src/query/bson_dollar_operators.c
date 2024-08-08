/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/bson_dollar_operators.c
 *
 * Implementation of the BSON Comparison dollar operators.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <math.h>

#include "io/helio_bson_core.h"
#include "aggregation/bson_query_common.h"
#include "io/bson_traversal.h"
#include "query/helio_bson_compare.h"
#include "operators/bson_expression.h"
#include "query/bson_dollar_operators.h"
#include "utils/mongo_errors.h"
#include "operators/bson_expr_eval.h"
#include "utils/fmgr_utils.h"
#include "utils/hashset_utils.h"
#include "opclass/helio_bson_text_gin.h"
#include "types/decimal128.h"
#include <utils/version_utils.h>

/*
 * Custom bson_orderBy options to allow specific types when sorting.
 */
typedef enum CustomOrderByOptions
{
	/* Default options where all types are allowed */
	CustomOrderByOptions_Default = 0x0,

	/* Allow only date types, required for ORDER BY in $setWindowFields */
	CustomOrderByOptions_AllowOnlyDates = 0x1,

	/* Allow only numbers, required for ORDER BY in $setWindowFields */
	CustomOrderByOptions_AllowOnlyNumbers = 0x2,
} CustomOrderByOptions;

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * Per query evaluation State for $all.
 * This tracks information calculated once per query.
 */
typedef struct BsonDollarAllQueryState
{
	/* Whether or not the $all array has exclusively nulls */
	bool arrayHasOnlyNulls;

	/* The number of elements in the $all array */
	int numElements;

	/* An array of exprEvalState for expression evaluation.
	 * If that entry has an expression, then it's set.
	 * Otherwise it's null. There are numElements elements
	 * in this array.
	 * The value is null if there's no eval states at all
	 */
	ExprEvalState **evalStateArray;

	/* An array of cached Regex. Will be NULL when there is no regex inside
	 * $all array. There are numElements elements in this array irrespective
	 * of the number of regex. crePcreData[<array index>] will be non-NUll when
	 * the array element for $all is a regex  */
	PcreData **crePcreData;

	/* The filter as a pgbsonelement */
	pgbsonelement filterElement;
} BsonDollarAllQueryState;

/*
 * Per query evaluation State for $in.
 * This tracks information calculated once per query.
 */
typedef struct BsonDollarInQueryState
{
	/* The filter as a pgbsonelement */
	pgbsonelement filterElement;

	/* List of elements in the "$in" input that are of type BSON_DOLLAR_REGEX. */
	List *regexList;

	/* Hash all entries in `$in` excluding those of regex type, as regex-type entries are being handled separately. */
	HTAB *bsonValueHashSet;

	/* true if array has any null value */
	bool hasNull;
} BsonDollarInQueryState;

/* State for comparison operations order by traversal */
typedef struct TraverseOrderByValidateState
{
	TraverseValidateState traverseState; /* must be the first field */
	bool isOrderByMin;
	bson_value_t orderByValue;
	CustomOrderByOptions options;
} TraverseOrderByValidateState;

/* State for comparison operations of simple dollar operators
 * where the query only needs the filter to process the comparison */
typedef struct TraverseElementValidateState
{
	TraverseValidateState traverseState; /* must be the first field */
	pgbsonelement *filter;
} TraverseElementValidateState;

/* State for the comparison of $in operator */
typedef struct TraverseInValidateState
{
	/* must be the first field */
	TraverseValidateState traverseState;

	/* The filter as a pgbsonelement */
	pgbsonelement *filter;

	/* List of elements in the "$in" input that are of type BSON_DOLLAR_REGEX. */
	List *regexList;

	/* Hash all entries in `$in` excluding those of regex type, as regex-type entries are being handled separately. */
	HTAB *bsonValueHashSet;

	/* true if array has any null value */
	bool hasNull;
} TraverseInValidateState;


/* State for comparison of the $all operator. This needs an intermediate
 * array of bools to validate that all the elements being requested
 * are available for comparison
 */
typedef struct TraverseAllValidateState
{
	TraverseElementValidateState elementState; /* must be the first field */

	/*
	 * The per item matchState - array of bools that tracks the per
	 * $all element match.
	 * Null if the $all array is empty.
	 */
	bool *matchState;

	/* See BsonDollarAllQueryState */
	ExprEvalState **evalStateArray;

	/* To hold the list of cached regex */
	PcreData **crePcreData;

	/* See BsonDollarAllQueryState */
	bool arrayHasOnlyNulls;
} TraverseAllValidateState;


/*
 *  State for comparison of the $range operator at runtime.
 *  DollarRangeParams and the filter in traverseState does not change throughout the execution.
 */
typedef struct TraverseRangeValidateState
{
	TraverseElementValidateState elementState;
	DollarRangeParams params;
	bool isMinConditionSet;
	bool isMaxConditionSet;
} TraverseRangeValidateState;


/*
 * Shared query state for $expr
 */
typedef struct BsonDollarExprQueryState
{
	/*
	 * The cached expression context for the query
	 */
	AggregationExpressionData *expression;

	/*
	 * Any variable context for let.
	 */
	ExpressionVariableContext *variableContext;
} BsonDollarExprQueryState;

/*
 * Holds intermediate state needed to handle regex matches
 * during bson document traversal for comparisons
 */
typedef struct TraverseRegexValidateState
{
	TraverseValidateState traverseState; /* must be the first field */
	RegexData regexData;
} TraverseRegexValidateState;

/*
 * Wrapper type that holds state that can be cached
 * across executions for elemMatch.
 */
typedef struct BsonElemMatchQueryState
{
	/* The expression evaluation state that can be reused
	 * when evaluating the filter against values inside
	 * document tuples */
	ExprEvalState *expressionEvaluationState;

	/* The filter element including the path for
	 * the elemMatch filter
	 */
	pgbsonelement filterElement;

	/* Whether or not the elemMatch is on an empty query */
	bool isEmptyElemMatch;
} BsonElemMatchQueryState;


/*
 * Holds intermediate state needed to handle $elemMatch queries
 * during bson document traversal for comparisons.
 */
typedef struct TraverseElemMatchValidateState
{
	TraverseValidateState traverseState; /* must be the first field */

	/* the evaluation state for the elemMatch */
	ExprEvalState *expressionEvaluationState;

	/* Whether or not the elemMatch object is empty */
	bool isEmptyElemMatch;
} TraverseElemMatchValidateState;

typedef bool (*IsQueryFilterNullFunc)(const TraverseValidateState *state);

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static int CompareLogicalOperator(const pgbsonelement *documentElement, const
								  pgbsonelement *filterElement, bool *isPathValid);
static bool CompareEqualMatch(const pgbsonelement *documentIterator,
							  TraverseValidateState *validationState, bool
							  isFirstArrayTerm);
static bool CompareInMatch(const pgbsonelement *documentIterator,
						   TraverseValidateState *validationState, bool
						   isFirstArrayTerm);
static bool CompareGreaterMatch(const pgbsonelement *documentIterator,
								TraverseValidateState *validationState, bool
								isFirstArrayTerm);
static bool CompareGreaterEqualMatch(const pgbsonelement *documentIterator,
									 TraverseValidateState *validationState, bool
									 isFirstArrayTerm);
static bool CompareLessMatch(const pgbsonelement *documentIterator,
							 TraverseValidateState *validationState, bool
							 isFirstArrayTerm);
static bool CompareLessEqualMatch(const pgbsonelement *documentIterator,
								  TraverseValidateState *validationState, bool
								  isFirstArrayTerm);
static bool CompareExistsMatch(const pgbsonelement *documentIterator,
							   TraverseValidateState *validationState, bool
							   isFirstArrayTerm);
static bool CompareArraySizeMatch(const pgbsonelement *documentIterator,
								  TraverseValidateState *validationState, bool
								  isFirstArrayTerm);
static bool CompareArrayTypeMatch(const pgbsonelement *documentIterator,
								  TraverseValidateState *validationState, bool
								  isFirstArrayTerm);
static bool CompareAllMatch(const pgbsonelement *documentIterator,
							TraverseValidateState *validationState, bool
							isFirstArrayTerm);
static void CompareForOrderBy(const pgbsonelement *documentIterator,
							  TraverseOrderByValidateState *validationState);
static bool CompareBitsAllClearMatch(const pgbsonelement *documentIterator,
									 TraverseValidateState *traverseState, bool
									 isFirstArrayTerm);
static bool CompareBitsAnyClearMatch(const pgbsonelement *documentIterator,
									 TraverseValidateState *traverseState, bool
									 isFirstArrayTerm);
static bool CompareBitsAllSetMatch(const pgbsonelement *documentIterator,
								   TraverseValidateState *traverseState, bool
								   isFirstArrayTerm);
static bool CompareBitsAnySetMatch(const pgbsonelement *documentIterator,
								   TraverseValidateState *traverseState, bool
								   isFirstArrayTerm);
static bool CompareRegexMatch(const pgbsonelement *documentElement,
							  TraverseValidateState *validationState, bool ignore);
static bool CompareModMatch(const pgbsonelement *documentIterator,
							TraverseValidateState *traverseState, bool ignore);
static bool CompareElemMatchMatch(const pgbsonelement *documentIterator,
								  TraverseValidateState *validationState,
								  bool isFirstArrayTerm);
static bool CompareBsonValueAgainstQueryCore(const pgbsonelement *element,
											 pgbsonelement *filterElement,
											 TraverseValidateState *state,
											 const TraverseBsonExecutionFuncs *
											 executionFuncs,
											 IsQueryFilterNullFunc isQueryFilterNull);
static bool CompareBsonAgainstQuery(const pgbson *element,
									const pgbson *filter,
									CompareMatchValueFunc compareFunc,
									IsQueryFilterNullFunc isQueryFilterNull);
static bool IsExistPositiveMatch(pgbson *filter);
static pgbsonelement PopulateRegexState(PG_FUNCTION_ARGS,
										TraverseRegexValidateState *state);
static void PopulateRegexFromQuery(RegexData *regexState, pgbson *filter);
static void PopulateDollarInValidationState(PG_FUNCTION_ARGS,
											TraverseInValidateState *state,
											pgbsonelement *filterElement);
static void PopulateDollarInStateFromQuery(BsonDollarInQueryState *dollarInState,
										   const pgbson *filter);
static pgbsonelement PopulateElemMatchValidationState(PG_FUNCTION_ARGS,
													  TraverseElemMatchValidateState *
													  state);
static void PopulateElemMatchStateFromQuery(BsonElemMatchQueryState *state, const
											pgbson *filter);
static void PopulateRangeStateFromQuery(DollarRangeParams *state, const
										pgbson *filter);
static void PopulateDollarAllValidationState(PG_FUNCTION_ARGS,
											 TraverseAllValidateState *state,
											 pgbsonelement *filterElement);
static void PopulateDollarAllStateFromQuery(BsonDollarAllQueryState *state, const
											pgbson *filter);
static void PopulateExprStateFromQuery(BsonDollarExprQueryState *state,
									   const pgbson *filter, const pgbson *variableSpec);

static bool IsQueryFilterNullForValue(const TraverseValidateState *filterElement);
static bool IsQueryFilterNullForArray(const TraverseValidateState *filterElement);
static bool IsQueryFilterNullForDollarAll(const TraverseValidateState *filterElement);

static bool CompareVisitTopLevelField(pgbsonelement *element, const
									  StringView *filterPath,
									  void *state);
static bool CompareVisitArrayField(pgbsonelement *element, const StringView *filterPath,
								   int arrayIndex, void *state);
static void CompareSetTraverseResult(void *state, TraverseBsonResult compareResult);
static bool CompareContinueProcessIntermediateArray(void *state, const
													bson_value_t *value);
static bool OrderByVisitTopLevelField(pgbsonelement *element, const
									  StringView *filterPath,
									  void *state);
static bool OrderByVisitArrayField(pgbsonelement *element, const StringView *filterPath,
								   int arrayIndex, void *state);
static bool DollarRangeVisitTopLevelField(pgbsonelement *element, const
										  StringView *filterPath,
										  void *state);
static bool DollarRangeVisitArrayField(pgbsonelement *element, const
									   StringView *filterPath,
									   int arrayIndex, void *state);
static Datum BsonOrderbyCore(pgbson *leftBson, pgbson *rightBson, bool validateSort,
							 const CustomOrderByOptions options);

/*
 * Standard execution functions for traversing bson and evaluating queries for $ops.
 */
static const TraverseBsonExecutionFuncs CompareExecutionFuncs = {
	.ContinueProcessIntermediateArray = CompareContinueProcessIntermediateArray,
	.SetTraverseResult = CompareSetTraverseResult,
	.VisitArrayField = CompareVisitArrayField,
	.VisitTopLevelField = CompareVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL
};

/*
 * Execution functions for traversing bson and evaluating queries for order by.
 */
static const TraverseBsonExecutionFuncs OrderByExecutionFuncs = {
	.ContinueProcessIntermediateArray = CompareContinueProcessIntermediateArray,
	.SetTraverseResult = CompareSetTraverseResult,
	.VisitArrayField = OrderByVisitArrayField,
	.VisitTopLevelField = OrderByVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL
};


/*
 * Execution functions for traversing bson and evaluating queries for $elemMatch
 * or $size or $all with $elemMatch- mainly avoids visiting leaf array fields.
 */
static const TraverseBsonExecutionFuncs CompareTopLevelFieldExecutionFuncs = {
	.ContinueProcessIntermediateArray = CompareContinueProcessIntermediateArray,
	.SetTraverseResult = CompareSetTraverseResult,
	.VisitArrayField = NULL,
	.VisitTopLevelField = CompareVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL,
};


/*
 * Execution functions for traversing bson and evaluating queries for $range
 * or $size or $all with $elemMatch- mainly avoids visiting leaf array fields.
 */
static const TraverseBsonExecutionFuncs CompareDollarRangeExecutionFuncs = {
	.ContinueProcessIntermediateArray = CompareContinueProcessIntermediateArray,
	.SetTraverseResult = CompareSetTraverseResult,
	.VisitArrayField = DollarRangeVisitArrayField,
	.VisitTopLevelField = DollarRangeVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL
};


/*
 * Inline convenience function to reduce boiler plate code for most
 * comparison operators when applying to bson_values.
 */
static inline bool
CompareBsonValueAgainstQuery(const pgbsonelement *element,
							 const pgbson *filter,
							 CompareMatchValueFunc compareFunc,
							 IsQueryFilterNullFunc isQueryFilterNull)
{
	TraverseElementValidateState elementValidationState = { { 0 }, NULL };

	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	elementValidationState.traverseState.matchFunc = compareFunc;
	elementValidationState.filter = &filterElement;
	return CompareBsonValueAgainstQueryCore(element, &filterElement,
											&elementValidationState.traverseState,
											&CompareExecutionFuncs,
											isQueryFilterNull);
}


/*
 * Inline convenience function to reduce boiler plate around
 * processing the query traverse result when there's nulls in the filter.
 */
inline static bool
ProcessQueryResultAndGetMatch(IsQueryFilterNullFunc isQueryFilterNull,
							  const TraverseValidateState *state)
{
	/* special case for null. For null, equality returns true if the values match */
	/* or if the path doesn't exist. */
	if (isQueryFilterNull != NULL && isQueryFilterNull(state))
	{
		return state->compareResult != CompareResult_Mismatch;
	}
	else
	{
		return state->compareResult == CompareResult_Match;
	}
}


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_dollar_size);
PG_FUNCTION_INFO_V1(bson_dollar_type);
PG_FUNCTION_INFO_V1(bson_dollar_all);
PG_FUNCTION_INFO_V1(bson_dollar_elemmatch);
PG_FUNCTION_INFO_V1(bson_dollar_eq);
PG_FUNCTION_INFO_V1(bson_dollar_gt);
PG_FUNCTION_INFO_V1(bson_dollar_gte);
PG_FUNCTION_INFO_V1(bson_dollar_lt);
PG_FUNCTION_INFO_V1(bson_dollar_lte);
PG_FUNCTION_INFO_V1(bson_dollar_in);
PG_FUNCTION_INFO_V1(bson_dollar_ne);
PG_FUNCTION_INFO_V1(bson_dollar_nin);
PG_FUNCTION_INFO_V1(bson_dollar_exists);
PG_FUNCTION_INFO_V1(command_bson_orderby);
PG_FUNCTION_INFO_V1(bson_orderby_partition);
PG_FUNCTION_INFO_V1(bson_vector_orderby);
PG_FUNCTION_INFO_V1(bson_dollar_bits_all_clear);
PG_FUNCTION_INFO_V1(bson_dollar_bits_any_clear);
PG_FUNCTION_INFO_V1(bson_dollar_bits_all_set);
PG_FUNCTION_INFO_V1(bson_dollar_bits_any_set);
PG_FUNCTION_INFO_V1(bson_dollar_regex);
PG_FUNCTION_INFO_V1(bson_dollar_mod);
PG_FUNCTION_INFO_V1(bson_dollar_expr);
PG_FUNCTION_INFO_V1(bson_dollar_text);
PG_FUNCTION_INFO_V1(bson_dollar_range);
PG_FUNCTION_INFO_V1(bson_dollar_lookup_join_filter);
PG_FUNCTION_INFO_V1(bson_dollar_merge_join_filter);
PG_FUNCTION_INFO_V1(bson_dollar_not_gt);
PG_FUNCTION_INFO_V1(bson_dollar_not_gte);
PG_FUNCTION_INFO_V1(bson_dollar_not_lt);
PG_FUNCTION_INFO_V1(bson_dollar_not_lte);

PG_FUNCTION_INFO_V1(bson_value_dollar_eq);
PG_FUNCTION_INFO_V1(bson_value_dollar_gt);
PG_FUNCTION_INFO_V1(bson_value_dollar_gte);
PG_FUNCTION_INFO_V1(bson_value_dollar_lt);
PG_FUNCTION_INFO_V1(bson_value_dollar_lte);
PG_FUNCTION_INFO_V1(bson_value_dollar_size);
PG_FUNCTION_INFO_V1(bson_value_dollar_type);
PG_FUNCTION_INFO_V1(bson_value_dollar_in);
PG_FUNCTION_INFO_V1(bson_value_dollar_nin);
PG_FUNCTION_INFO_V1(bson_value_dollar_ne);
PG_FUNCTION_INFO_V1(bson_value_dollar_exists);
PG_FUNCTION_INFO_V1(bson_value_dollar_elemmatch);
PG_FUNCTION_INFO_V1(bson_value_dollar_all);
PG_FUNCTION_INFO_V1(bson_value_dollar_regex);
PG_FUNCTION_INFO_V1(bson_value_dollar_mod);
PG_FUNCTION_INFO_V1(bson_value_dollar_bits_all_clear);
PG_FUNCTION_INFO_V1(bson_value_dollar_bits_any_clear);
PG_FUNCTION_INFO_V1(bson_value_dollar_bits_all_set);
PG_FUNCTION_INFO_V1(bson_value_dollar_bits_any_set);

/*
 * Traverses the document for a given dot-path notation
 * When a matching path is found, then returns true if the
 * element is an array with the specified size. Returns false
 * otherwise.
 */
Datum
bson_dollar_size(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	bson_iter_t documentIterator;
	pgbsonelement filterElement;
	TraverseElementValidateState state = { { 0 }, NULL };
	PgbsonInitIterator(document, &documentIterator);
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	filterElement.pathLength = 0;
	state.filter = &filterElement;
	state.traverseState.matchFunc = CompareArraySizeMatch;
	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &CompareTopLevelFieldExecutionFuncs);
	PG_RETURN_BOOL(state.traverseState.compareResult == CompareResult_Match);
}


/*
 * Given a pgbsonelement value, returns true if the element
 * is an array with the specified size. Returns false otherwise.
 */
Datum
bson_value_dollar_size(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	pgbsonelement filterElement;
	TraverseElementValidateState state = { { 0 }, NULL };
	PgbsonToSinglePgbsonElement(query, &filterElement);
	filterElement.pathLength = 0;
	state.filter = &filterElement;
	state.traverseState.matchFunc = CompareArraySizeMatch;
	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQueryCore(element, &filterElement,
													&state.traverseState,
													&CompareTopLevelFieldExecutionFuncs,
													isNullFilterEquality));
}


/*
 * Traverses the document for a given dot-path notation
 * When a matching path is found, then returns true if the
 * element is the type specified. Returns false
 * otherwise.
 */
Datum
bson_dollar_type(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareArrayTypeMatch,
										   isNullFilterEquality));
}


/*
 * Given a pgbsonelement value, returns true if the element
 * is the type specified. Returns false otherwise.
 */
Datum
bson_value_dollar_type(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query, CompareArrayTypeMatch,
												isNullFilterEquality));
}


/*
 * Traverses the document for a given dot-path notation
 * When a matching path is found, then returns true if the
 * element meets the $all operator criteria. Returns false
 * otherwise.
 */
Datum
bson_dollar_all(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	TraverseAllValidateState validationState = {
		.elementState =
		{
			.filter = NULL,
			.traverseState = { 0 }
		},
		.matchState = NULL,
		.arrayHasOnlyNulls = false,
		.evalStateArray = NULL,
		.crePcreData = NULL
	};

	pgbsonelement filterElement = { 0 };
	PopulateDollarAllValidationState(fcinfo, &validationState, &filterElement);

	/* an empty array for $all doesn't match any elements */
	if (validationState.matchState == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	const TraverseBsonExecutionFuncs *execFuncs = &CompareExecutionFuncs;
	if (validationState.evalStateArray != NULL)
	{
		execFuncs = &CompareTopLevelFieldExecutionFuncs;
	}

	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);
	TraverseBson(&documentIterator, filterElement.path,
				 &validationState.elementState.traverseState,
				 execFuncs);
	pfree(validationState.matchState);

	/* if path was not found and the $all array only contains null,
	 * ([null], or [null,null,null]) we have a match. */
	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForDollarAll;
	PG_RETURN_BOOL(ProcessQueryResultAndGetMatch(isNullFilterEquality,
												 &validationState.elementState.
												 traverseState));
}


/*
 * Implements the semantics for the $all function for bson_value inputs.
 * Checks if the value is meets the criteria for all the elements in the
 * $all array.
 */
Datum
bson_value_dollar_all(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	TraverseAllValidateState state =
	{
		.elementState =
		{
			.filter = NULL,
			.traverseState = { 0 }
		},
		.matchState = NULL,
		.arrayHasOnlyNulls = false,
		.evalStateArray = NULL,
		.crePcreData = NULL
	};

	pgbsonelement filterElement = { 0 };
	PopulateDollarAllValidationState(fcinfo, &state, &filterElement);

	/* an empty array for $all doesn't match any elements */
	if (state.matchState == NULL)
	{
		PG_RETURN_BOOL(false);
	}

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForDollarAll;
	bool compareResult = CompareBsonValueAgainstQueryCore(element,
														  state.elementState.filter,
														  &state.elementState.
														  traverseState,
														  &CompareExecutionFuncs,
														  isNullFilterEquality);
	pfree(state.matchState);
	PG_RETURN_BOOL(compareResult);
}


/*
 * implements the Mongo's $elemMatch functionality
 * in the runtime. Checks that the value in the element is
 * an array and at least one element matches all the conditions
 * provided in the nested filter.
 */
Datum
bson_dollar_elemmatch(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	bson_iter_t documentIterator;
	TraverseElemMatchValidateState state = {
		.traverseState = { 0 },
		.expressionEvaluationState = NULL,
	};

	pgbsonelement filterElement = PopulateElemMatchValidationState(fcinfo, &state);
	PgbsonInitIterator(document, &documentIterator);
	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &CompareTopLevelFieldExecutionFuncs);
	PG_RETURN_BOOL(state.traverseState.compareResult == CompareResult_Match);
}


/*
 * implements the Mongo's $elemMatch functionality
 * in the runtime for bson_values. Checks that the value in the element is
 * an array and at least one element matches all the conditions
 * provided in the nested filter.
 */
Datum
bson_value_dollar_elemmatch(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	TraverseElemMatchValidateState state = {
		.traverseState = { 0 },
		.expressionEvaluationState = NULL,
	};

	pgbsonelement filterElement = PopulateElemMatchValidationState(fcinfo, &state);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQueryCore(element, &filterElement,
													&state.traverseState,
													&CompareTopLevelFieldExecutionFuncs,
													isNullFilterEquality));
}


/*
 * bson_dollar_bits_all_clear implements the Mongo's $bitsAllClear functionality
 * in the runtime. This compares that all set bits of filter are unset in document or not.
 */
Datum
bson_dollar_bits_all_clear(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareBitsAllClearMatch,
										   isNullFilterEquality));
}


/*
 * bson_dollar_bits_any_clear implements the Mongo's $bitsAnyClear functionality
 * in the runtime. This compares that any set bit of filter is unset in document or not.
 */
Datum
bson_dollar_bits_any_clear(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareBitsAnyClearMatch,
										   isNullFilterEquality));
}


/*
 * bson_dollar_bits_all_set implements the Mongo's $bitsAllSet functionality
 * in the runtime. This compares that all set bits of filter are set in document or not.
 */
Datum
bson_dollar_bits_all_set(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareBitsAllSetMatch,
										   isNullFilterEquality));
}


/*
 * bson_dollar_bits_any_set implements the Mongo's $bitsAnySet functionality
 * in the runtime. This compares that any set bit of filter is set in document or not.
 */
Datum
bson_dollar_bits_any_set(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareBitsAnySetMatch,
										   isNullFilterEquality));
}


/*
 * implements the Mongo's $bitsAllClear functionality
 * in the runtime. This compares that all set bits of filter are unset in element value or not.
 */
Datum
bson_value_dollar_bits_all_clear(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query,
												CompareBitsAllClearMatch,
												isNullFilterEquality));
}


/*
 * implements the Mongo's $bitsAnyClear functionality
 * in the runtime. This compares that any set bits of filter is unset in element value or not.
 */
Datum
bson_value_dollar_bits_any_clear(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query,
												CompareBitsAnyClearMatch,
												isNullFilterEquality));
}


/*
 * implements the Mongo's $bitsAllSet functionality
 * in the runtime. This compares that all set bits of filter are set in element value or not.
 */
Datum
bson_value_dollar_bits_all_set(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query,
												CompareBitsAllSetMatch,
												isNullFilterEquality));
}


/*
 * implements the Mongo's $bitsAnySet functionality
 * in the runtime. This compares that any set bits of filter is set in element value or not.
 */
Datum
bson_value_dollar_bits_any_set(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query,
												CompareBitsAnySetMatch,
												isNullFilterEquality));
}


/*
 * bson_dollar_regex implements the Mongo's $regex functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the regex on the value provided.
 */
Datum
bson_dollar_regex(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	bson_iter_t documentIterator;
	TraverseRegexValidateState state = {
		{ 0 }, { 0 }
	};

	pgbsonelement filterElement = PopulateRegexState(fcinfo, &state);
	PgbsonInitIterator(document, &documentIterator);
	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &CompareExecutionFuncs);
	PG_RETURN_BOOL(state.traverseState.compareResult == CompareResult_Match);
}


/*
 * implements the Mongo's $regex functionality
 * in the runtime. Checks that the value in the element provided
 * that at least one matches the regex on the value provided.
 */
Datum
bson_value_dollar_regex(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	TraverseRegexValidateState state = {
		{ 0 }, { 0 }
	};

	pgbsonelement filterElement = PopulateRegexState(fcinfo, &state);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQueryCore(element, &filterElement,
													&state.traverseState,
													&CompareExecutionFuncs,
													isNullFilterEquality));
}


/*
 * Implements the Mongo's $mod functionality in the runtime for bson_values.
 * Checks if the value of given field divided by a divisor has the specified remainder
 */
Datum
bson_dollar_mod(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareModMatch,
										   isNullFilterEquality));
}


/*
 * Implements the Mongo's $mod functionality in the runtime.
 * Checks if the value of given field divided by a divisor has the specified remainder
 */
Datum
bson_value_dollar_mod(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query, CompareModMatch,
												isNullFilterEquality));
}


/*
 * bson_dollar_eq implements the Mongo's $eq functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the equality semantics on the value
 * provided.
 */
Datum
bson_dollar_eq(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);
	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareEqualMatch,
										   isNullFilterEquality));
}


/*
 * implements the Mongo's $eq functionality
 * in the runtime. Checks that the value in the element provided
 * is equal to the value in the filter.
 */
Datum
bson_value_dollar_eq(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query, CompareEqualMatch,
												isNullFilterEquality));
}


/*
 * bson_dollar_gt implements the Mongo's $gt functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the greater than semantics on the value
 * provided.
 */
Datum
bson_dollar_gt(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareGreaterMatch,
										   isNullFilterEquality));
}


/*
 * implements the Mongo's $gt functionality
 * in the runtime. Checks that the value in the element provided
 * is greater than the value in the filter.
 */
Datum
bson_value_dollar_gt(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query, CompareGreaterMatch,
												isNullFilterEquality));
}


/*
 * implements the Mongo's $not: { $gt: {} } functionality
 * in the runtime. Checks that the value in the element provided
 * is greater than the value in the filter.
 */
Datum
bson_dollar_not_gt(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	bool result = CompareBsonAgainstQuery(document, query, CompareGreaterMatch,
										  isNullFilterEquality);
	PG_RETURN_BOOL(!result);
}


/*
 * bson_dollar_gte implements the Mongo's $gte functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the greater than or equal
 *  semantics on the value provided.
 */
Datum
bson_dollar_gte(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);
	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareGreaterEqualMatch,
										   isNullFilterEquality));
}


/*
 * bson_dollar_gte implements the Mongo's $not: { $gte: {} } functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the greater than or equal
 *  semantics on the value provided.
 */
Datum
bson_dollar_not_gte(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);
	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	bool result = CompareBsonAgainstQuery(document, filter, CompareGreaterEqualMatch,
										  isNullFilterEquality);
	PG_RETURN_BOOL(!result);
}


/*
 * bson_dollar_range implements the $range functionality
 * in the runtime. This traverses the document based on the
 * rewritten range query of the following format checks that at least
 * one matches range semantics on the value provided.
 *
 *  { "path": { "min": VALUE, "max": VALUE, "minInclusive": BOOL, "maxInclusive": BOOL } }
 */
Datum
bson_dollar_range(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);
	const DollarRangeParams *cachedRangeParamsState;
	SetCachedFunctionState(
		cachedRangeParamsState,
		DollarRangeParams,
		1,
		PopulateRangeStateFromQuery,
		filter);

	DollarRangeParams localState = {
		{ 0 }, { 0 }, false, false
	};
	if (cachedRangeParamsState == NULL)
	{
		PopulateRangeStateFromQuery(&localState, filter);
		cachedRangeParamsState = &localState;
	}

	TraverseRangeValidateState rangeState = {
		{
			{ 0 }, NULL
		},
		{
			{ 0 }, { 0 }, false, false
		},
		false, false
	};

	/*
	 *  params (i.e., DollarRangeParams) remains unchanged throughout multiple invocations of bson_dollar_range()
	 *  during the execution of a query. The rest can be modified as the query execution progresses.
	 *
	 *  We only use cached values for the unchanged parts.
	 */

	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	rangeState.elementState.filter = &filterElement;
	rangeState.params = *cachedRangeParamsState;
	rangeState.isMinConditionSet = false;
	rangeState.isMaxConditionSet = false;

	bson_iter_t documentIterator;
	PgbsonInitIterator(document, &documentIterator);
	TraverseBson(&documentIterator, rangeState.elementState.filter->path,
				 (void *) &rangeState,
				 &CompareDollarRangeExecutionFuncs);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	if (rangeState.params.isMaxInclusive ||
		rangeState.params.isMinInclusive)
	{
		isNullFilterEquality = IsQueryFilterNullForValue;
	}

	PG_RETURN_BOOL(ProcessQueryResultAndGetMatch(isNullFilterEquality,
												 &rangeState.elementState.
												 traverseState));
}


/*
 * implements the Mongo's $gte functionality
 * in the runtime. Checks that the value in the element provided
 * is greater than or equal the value in the filter.
 */
Datum
bson_value_dollar_gte(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query,
												CompareGreaterEqualMatch,
												isNullFilterEquality));
}


/*
 * bson_dollar_not_lt implements the Mongo's $not: { $lt: {}} functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the less than
 *  semantics on the value provided.
 */
Datum
bson_dollar_not_lt(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	bool result = CompareBsonAgainstQuery(document, filter, CompareLessMatch,
										  isNullFilterEquality);
	PG_RETURN_BOOL(!result);
}


/*
 * bson_dollar_lt implements the Mongo's $lt functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the less than
 *  semantics on the value provided.
 */
Datum
bson_dollar_lt(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareLessMatch,
										   isNullFilterEquality));
}


/*
 * implements the Mongo's $lt functionality
 * in the runtime. Checks that the value in the element provided
 * is less than the value in the filter.
 */
Datum
bson_value_dollar_lt(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query, CompareLessMatch,
												isNullFilterEquality));
}


/*
 * bson_dollar_lte implements the Mongo's $lte functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the less than or equal
 *  semantics on the value provided.
 */
Datum
bson_dollar_lte(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	PG_RETURN_BOOL(CompareBsonAgainstQuery(document, filter, CompareLessEqualMatch,
										   isNullFilterEquality));
}


/*
 * bson_dollar_not_lte implements the Mongo's $not: { $lte: {}} functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches the less than or equal
 *  semantics on the value provided.
 */
Datum
bson_dollar_not_lte(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	bool result = CompareBsonAgainstQuery(document, filter, CompareLessEqualMatch,
										  isNullFilterEquality);
	PG_RETURN_BOOL(!result);
}


/*
 * implements the Mongo's $lte functionality
 * in the runtime. Checks that the value in the element provided
 * is less than or equal to the value in the filter.
 */
Datum
bson_value_dollar_lte(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	PG_RETURN_BOOL(CompareBsonValueAgainstQuery(element, query, CompareLessEqualMatch,
												isNullFilterEquality));
}


/*
 * bson_dollar_in implements the Mongo's $in functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one matches at least one of the input array values.
 */
Datum
bson_dollar_in(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	bson_iter_t documentIterator;
	TraverseInValidateState state = { { 0 }, NULL, NIL, NULL, false };

	pgbsonelement filterElement = { 0 };
	PopulateDollarInValidationState(fcinfo, &state, &filterElement);

	PgbsonInitIterator(document, &documentIterator);

	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &CompareExecutionFuncs);
	if (state.hasNull)
	{
		/* If any element in the input is null and the target path cannot be found in the document, we'll choose that document. */
		PG_RETURN_BOOL(state.traverseState.compareResult != CompareResult_Mismatch);
	}
	else
	{
		PG_RETURN_BOOL(state.traverseState.compareResult == CompareResult_Match);
	}
}


/*
 * The runtime implementation of this is identical to $in. so we just make a direct function call
 * to the function - We just have a tail end argument of the index path so that we can do index
 * pushdown for $lookup scenarios.
 */
Datum
bson_dollar_lookup_join_filter(PG_FUNCTION_ARGS)
{
	return bson_dollar_in(fcinfo);
}


/*
 * The runtime implementation of this is almost identical $eq. Just validating source and extracting filter from it.
 * We just have a tail end argument of the index path so that we can do index pushdown for $merge scenarios.
 */
Datum
bson_dollar_merge_join_filter(PG_FUNCTION_ARGS)
{
	if (IsClusterVersionAtleastThis(1, 20, 0))
	{
		return bson_dollar_eq(fcinfo);
	}

	/*TODO : Remove below code once we deploy 1.20 in production */
	pgbson *targetDocument = PG_GETARG_PGBSON(0);
	pgbson *sourceDocument = PG_GETARG_PGBSON(1);

	char *joinField = text_to_cstring(PG_GETARG_TEXT_P(2));

	bson_iter_t sourceIter;
	if (!PgbsonInitIteratorAtPath(sourceDocument, joinField, &sourceIter))
	{
		/* If the source lacks an object ID, we return false and generate a new one during the document's insertion into the target. */
		if (strcmp(joinField, "_id") == 0)
		{
			PG_RETURN_BOOL(false);
		}

		ereport(ERROR, (errcode(MongoLocation51132),
						errmsg(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array"),
						errhint(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array")));
	}

	pgbsonelement filterElement;
	filterElement.path = joinField;
	filterElement.pathLength = strlen(joinField);
	filterElement.bsonValue = *bson_iter_value(&sourceIter);

	if (filterElement.bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoLocation51185),
						errmsg(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array"),
						errhint(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array")));
	}
	else if (filterElement.bsonValue.value_type == BSON_TYPE_NULL ||
			 filterElement.bsonValue.value_type == BSON_TYPE_UNDEFINED)
	{
		ereport(ERROR, (errcode(MongoLocation51132),
						errmsg(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array"),
						errhint(
							"$merge write error: 'on' field cannot be missing, null, undefined or an array")));
	}

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	bson_iter_t targetIter;

	TraverseElementValidateState state = { { 0 }, NULL };
	PgbsonInitIterator(targetDocument, &targetIter);
	state.filter = &filterElement;
	state.traverseState.matchFunc = CompareEqualMatch;
	TraverseBson(&targetIter, filterElement.path, &state.traverseState,
				 &CompareExecutionFuncs);
	PG_RETURN_BOOL(ProcessQueryResultAndGetMatch(isNullFilterEquality,
												 &state.traverseState));
}


/*
 * implements the Mongo's $in functionality
 * in the runtime. Checks that the value in the element provided
 * is equal to at least one of the values in the filter.
 */
Datum
bson_value_dollar_in(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	TraverseInValidateState state = { { 0 }, NULL, NIL, NULL, false };

	pgbsonelement filterElement = { 0 };
	PopulateDollarInValidationState(fcinfo, &state, &filterElement);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForArray;
	PG_RETURN_BOOL(CompareBsonValueAgainstQueryCore(element, &filterElement,
													&state.traverseState,
													&CompareExecutionFuncs,
													isNullFilterEquality));
}


/*
 * bson_dollar_nin implements the Mongo's $nin functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one does not match the query value presented.
 * Note: per Mongo's $nin requirements, documents that don't have the field
 * are also considered to be $nin.
 */
Datum
bson_dollar_nin(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	bson_iter_t documentIterator;
	TraverseInValidateState state = { { 0 }, NULL, NIL, NULL, false };

	pgbsonelement filterElement = { 0 };
	PopulateDollarInValidationState(fcinfo, &state, &filterElement);

	PgbsonInitIterator(document, &documentIterator);

	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &CompareExecutionFuncs);

	if (state.hasNull)
	{
		/* If any element in the input is null and the target path cannot be found in the document, we'll not choose that document. */
		PG_RETURN_BOOL(state.traverseState.compareResult == CompareResult_Mismatch);
	}
	else
	{
		PG_RETURN_BOOL(state.traverseState.compareResult != CompareResult_Match);
	}
}


/*
 * implements the Mongo's $nin functionality
 * in the runtime. Checks that the value in the element provided
 * is not equal to any of the values in the filter.
 */
Datum
bson_value_dollar_nin(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	TraverseInValidateState state = { { 0 }, NULL, NIL, NULL, false };

	pgbsonelement filterElement = { 0 };
	PopulateDollarInValidationState(fcinfo, &state, &filterElement);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForArray;
	PG_RETURN_BOOL(!CompareBsonValueAgainstQueryCore(element, &filterElement,
													 &state.traverseState,
													 &CompareExecutionFuncs,
													 isNullFilterEquality));
}


/*
 * bson_dollar_ne implements the Mongo's $ne functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one does not match the query value presented.
 * Note: per Mongo's $ne requirements, documents that don't have the field
 * are also considered to be $ne.
 */
Datum
bson_dollar_ne(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	PG_RETURN_BOOL(!CompareBsonAgainstQuery(document, filter, CompareEqualMatch,
											isNullFilterEquality));
}


/*
 * implements the Mongo's $ne functionality
 * in the runtime. Checks that the value in the element provided
 * is not equal to the value in the filter.
 */
Datum
bson_value_dollar_ne(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	IsQueryFilterNullFunc isNullFilterEquality = IsQueryFilterNullForValue;
	PG_RETURN_BOOL(!CompareBsonValueAgainstQuery(element, query, CompareEqualMatch,
												 isNullFilterEquality));
}


/*
 * bson_dollar_exists implements the Mongo's $exits functionality
 * in the runtime. This traverses the document based on Mongo's
 * filter dot-notation syntax and for all possible values, checks
 * that at least one path matches the $exists operator.
 */
Datum
bson_dollar_exists(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);

	bool existsPositiveMatch = IsExistPositiveMatch(filter);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	bool match = CompareBsonAgainstQuery(document, filter, CompareExistsMatch,
										 isNullFilterEquality);
	PG_RETURN_BOOL(existsPositiveMatch ? match : !match);
}


/*
 * implements the Mongo's $gt functionality
 * in the runtime. Checks that the value in the element provided
 * exists.
 */
Datum
bson_value_dollar_exists(PG_FUNCTION_ARGS)
{
	pgbsonelement *element = (pgbsonelement *) PG_GETARG_POINTER(0);
	pgbson *query = (pgbson *) PG_GETARG_PGBSON(1);

	bool existsPositiveMatch = IsExistPositiveMatch(query);

	IsQueryFilterNullFunc isNullFilterEquality = NULL;
	bool match = CompareBsonValueAgainstQuery(element, query, CompareExistsMatch,
											  isNullFilterEquality);
	PG_RETURN_BOOL(existsPositiveMatch ? match : !match);
}


/*
 * implements Mongo's $expr functionality
 * in the runtime. Evaluates the expression pointed to by the filter
 * against the document. Returns true in the following cases:
 * 1) The expression evaluated to a boolean result and is true.
 * 2) The expression evaluated to a numeric and is not 0.
 * 3) The expression evaluated to a result that is defined.
 */
Datum
bson_dollar_expr(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON(0);
	pgbson *filter = PG_GETARG_PGBSON(1);
	pgbson *variablesContext = NULL;

	int argPositions[2] = { 1, 2 };
	int numArgs = 1;

	if (PG_NARGS() > 2)
	{
		variablesContext = PG_GETARG_MAYBE_NULL_PGBSON(2);
		numArgs = 2;
	}

	const BsonDollarExprQueryState *cachedExprQueryState = NULL;
	BsonDollarExprQueryState localState = { 0 };
	SetCachedFunctionStateMultiArgs(
		cachedExprQueryState,
		BsonDollarExprQueryState,
		argPositions,
		numArgs,
		PopulateExprStateFromQuery,
		filter,
		variablesContext);

	if (cachedExprQueryState == NULL)
	{
		PopulateExprStateFromQuery(&localState, filter, variablesContext);
		cachedExprQueryState = &localState;
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bool isNullOnEmpty = false;
	StringView emptyPathView = { .length = 0, .string = "" };
	EvaluateAggregationExpressionDataToWriter(
		cachedExprQueryState->expression,
		document, emptyPathView, &writer,
		cachedExprQueryState->variableContext,
		isNullOnEmpty);

	bson_iter_t resultIterator;
	PgbsonWriterGetIterator(&writer, &resultIterator);

	if (!bson_iter_next(&resultIterator))
	{
		PG_RETURN_BOOL(false);
	}

	pgbsonelement resultElement;
	BsonIterToPgbsonElement(&resultIterator, &resultElement);
	if (BsonValueIsNumberOrBool(&resultElement.bsonValue))
	{
		PG_RETURN_BOOL(BsonValueAsInt32(&resultElement.bsonValue) != 0);
	}

	bool result = resultElement.bsonValue.value_type != BSON_TYPE_NULL &&
				  resultElement.bsonValue.value_type != BSON_TYPE_UNDEFINED;
	PG_RETURN_BOOL(result);
}


/*
 * implements Mongo's $text functionality
 * in the runtime. Simply fails as $text is not supported on the runtime.
 */
Datum
bson_dollar_text(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoIndexNotFound),
					errmsg("text index required for $text query")));
	PG_RETURN_BOOL(false);
}


/*
 * command_bson_orderby traverses a document for a path specified by a filter
 * and returns the minimum value found at that path. For values that are not arrays it
 * returns the value at that path. For an array, it returns the smallest/largest value of the
 * array at the given path based on the sort state.
 */
Datum
command_bson_orderby(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	pgbson *filter = PG_GETARG_PGBSON_PACKED(1);
	bool validateSort = true;
	CustomOrderByOptions options = CustomOrderByOptions_Default;
	Datum returnedBson = BsonOrderbyCore(document, filter, validateSort, options);

	PG_FREE_IF_COPY(document, 0);
	PG_FREE_IF_COPY(filter, 1);
	PG_RETURN_DATUM(returnedBson);
}


/*
 * bson_orderby_partition traverses a document for a range based window
 * and ensures every value is either number (regular range) or date time value
 * (time based range) for the the window.
 */
Datum
bson_orderby_partition(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	pgbson *filter = PG_GETARG_PGBSON_PACKED(1);
	bool isTimeRangeWindow = PG_GETARG_BOOL(2);
	bool validateSort = true;

	CustomOrderByOptions options = isTimeRangeWindow ?
								   CustomOrderByOptions_AllowOnlyDates :
								   CustomOrderByOptions_AllowOnlyNumbers;
	Datum returnedBson = BsonOrderbyCore(document, filter, validateSort,
										 options);

	PG_FREE_IF_COPY(document, 0);
	PG_FREE_IF_COPY(filter, 1);
	PG_RETURN_DATUM(returnedBson);
}


/*
 * bson_vector_orderby (aka, |=<>|) is used in defining ORDER BY of the following form:
 * ORDER BY document |=<>| '{ "path" : "myname", "vector": "[]", "k": 10 }'::ApiCatalogSchemaName.bson
 *
 * We always rewrite that to the following to perform vector based comparisons:
 * ORDER BY ApiCatalogSchemaName.bson_extract_vector(document, 'elem') <=>
 * ApiCatalogSchemaName.bson_extract_vector('{ "path" : "myname", "vector": [], "k": 10 }', 'vector')
 *
 * If we would not reach here, since the rewrite phase will throw error if a matching similarity
 * index was not found.
 */
Datum
bson_vector_orderby(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoBadValue),
					errmsg(
						"Similarity index was not found for a vector similarity search query.")));
}


/*
 * Applies the $sort runtime projection.
 * ValidateSort is to handle a bug with $first where it passes the expression
 * as one of the sort specs
 */
Datum
BsonOrderby(pgbson *document, pgbson *filter, bool validateSort)
{
	CustomOrderByOptions options = CustomOrderByOptions_Default;
	return BsonOrderbyCore(document, filter, validateSort, options);
}


/*
 * Takes 8 bits at a time from input data and reads it from LSB to MSB and finds the bits which are set,
 * creates set bits positional array and repeat this until the data is exhausted.
 *
 * e.g., src = "ab" in Binary (01100001 01100010)
 * first 8 bit : 01100001  -> set bits: [0,5,6]
 * next 8 bit : 01100010 -> set bits [9,13,14]
 * Final set bits array : [0,5,6,9,13,14]
 * @src : source data for which writing a set position array
 * @srcLength : total Size of source data in bytes
 * @writer :  pgbson_writer where need to write a position array
 */
void
WriteSetBitPositionArray(uint8_t *src, int srcLength, pgbson_writer *writer)
{
	int lengthInBits = srcLength * 8;
	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, "", 0, &arrayWriter);

	for (int i = 0; i < lengthInBits; i++)
	{
		/*  (i & 7) is equivalent to (i % 8) */
		int bitmask = (1 << (i & 7));

		/*  (i >> 3) is equivalent to (i / 8) */
		if ((src[i >> 3] & bitmask) == bitmask)
		{
			bson_value_t elementValue;
			elementValue.value_type = BSON_TYPE_INT32;
			elementValue.value.v_int32 = i;
			PgbsonArrayWriterWriteValue(&arrayWriter, &elementValue);
		}
	}
	PgbsonWriterEndArray(writer, &arrayWriter);
}


/*
 * Compare set bit position array for $bitsAllClear operator.
 * any element of filter array is not present in source array then it is a match for $bitsAllClear
 * @sourceArrayIter : Iterator of source document array
 * @filterArrayIter : Iterator of filter array
 * @isSignExtended : on true in filter array all value > 31 will be consider as correct match
 */
bool
CompareArrayForBitsAllClear(bson_iter_t *sourceArrayIter, bson_iter_t *filterArrayIter,
							bool isSignExtended)
{
	/* filter or source array is empty */
	if (!bson_iter_next(filterArrayIter) || !bson_iter_next(sourceArrayIter))
	{
		return true;
	}

	while (true)
	{
		const bson_value_t *sourceValue = bson_iter_value(sourceArrayIter);
		const bson_value_t *filterValue = bson_iter_value(filterArrayIter);
		int sourceArrayElement = BsonValueAsInt32(sourceValue);
		int filterArrayElement = BsonValueAsInt32(filterValue);

		if (sourceArrayElement == filterArrayElement)
		{
			/* any element of filter array should not present in source array for $bitsAllClear */
			return false;
		}
		else if (sourceArrayElement < filterArrayElement)
		{
			/* No more elements remains in source array */
			if (!bson_iter_next(sourceArrayIter))
			{
				if (isSignExtended)
				{
					/* No more elements remains in source array and but source is sign extended */
					/* e.g., source = [0,1,2,3,4,......,31]   filter = [0,1,2,.....,33,34] . */
					/* Now let's assume source iter is pointing to 31 and filter iter is pointing to 33 but if  */
					/* source isSignExtended so any position greater than 31 will also be considered as set bit */
					return false;
				}

				return true;
			}
		}
		else
		{
			if (!bson_iter_next(filterArrayIter))
			{
				/* No more elements remains in filter */
				return true;
			}
		}
	}
}


/*
 * Compare set bit position array for $bitsAnyClear operator.
 * any element of filter array is not present in source array then it is a match for $bitsAnyClear
 * @sourceArrayIter : Iterator of source document array
 * @filterArrayIter : Iterator of filter array
 * @isSignExtended : on true in filter array all value > 31 will be consider as correct match
 */
bool
CompareArrayForBitsAnyClear(bson_iter_t *sourceArrayIter, bson_iter_t *filterArrayIter,
							bool isSignExtended)
{
	/* filter array is empty */
	if (!bson_iter_next(filterArrayIter))
	{
		return false;
	}

	/* source array is empty */
	if (!bson_iter_next(sourceArrayIter))
	{
		return true;
	}

	while (true)
	{
		const bson_value_t *sourceValue = bson_iter_value(sourceArrayIter);
		const bson_value_t *filterValue = bson_iter_value(filterArrayIter);
		int sourceArrayElement = BsonValueAsInt32(sourceValue);
		int filterArrayElement = BsonValueAsInt32(filterValue);

		if (sourceArrayElement == filterArrayElement)
		{
			/* if source element and filter element values are same move to next element in both array.*/

			/* if filter array has no more elements */
			if (!bson_iter_next(filterArrayIter))
			{
				return false;
			}

			/* if source array has no more elements and filter has elements */
			if (!bson_iter_next(sourceArrayIter))
			{
				if (isSignExtended)
				{
					/* No more elements remains in source array and but source is sign extended */
					/* e.g., source = [0,1,2,3,4,......,31]   filter = [0,1,2,.....,33,34] . */
					/* Now let's assume source iter is pointing to 31 and filter iter is pointing to 33 but if  */
					/* source isSignExtended so any position greater than 31 will also be considered as set bit */
					return false;
				}

				return true;
			}
		}
		else if (sourceArrayElement < filterArrayElement)
		{
			/* No more elements remains in source array */
			if (!bson_iter_next(sourceArrayIter))
			{
				if (isSignExtended)
				{
					/* No more elements remains in source array and but source is sign extended */
					/* e.g., source = [0,1,2,3,4,......,31]   filter = [0,1,2,.....,33,34] . */
					/* Now let's assume source iter is pointing to 31 and filter iter is pointing to 33 but if  */
					/* source isSignExtended so any position greater than 31 will also be considered as set bit */
					return false;
				}

				return true;
			}
		}
		else
		{
			/* if current source element is greater than current filter element, that means current filter element is not present in source array */
			return true;
		}
	}
}


/*
 * Compare set bit position array for $bitsAllSet operator.
 * all element of filter array should present in source array then it is a match for $bitsAllSet
 * @sourceArrayIter : Iterator of source document array
 * @filterArrayIter : Iterator of filter array
 * @isSignExtended : on true in filter array all value > 31 will be consider as correct match
 */
bool
CompareArrayForBitsAllSet(bson_iter_t *sourceArrayIter, bson_iter_t *filterArrayIter,
						  bool isSignExtended)
{
	/* filter array is empty */
	if (!bson_iter_next(filterArrayIter))
	{
		return true;
	}

	/* source array is empty */
	if (!bson_iter_next(sourceArrayIter))
	{
		return false;
	}

	while (true)
	{
		const bson_value_t *sourceValue = bson_iter_value(sourceArrayIter);
		const bson_value_t *filterValue = bson_iter_value(filterArrayIter);
		int sourceArrayElement = BsonValueAsInt32(sourceValue);
		int filterArrayElement = BsonValueAsInt32(filterValue);

		if (sourceArrayElement == filterArrayElement)
		{
			/* if source element and filter element values are same move to next element in both array.*/

			/* if filter array has no more elements */
			if (!bson_iter_next(filterArrayIter))
			{
				return true;
			}

			/* if source array has no more elements and filter has elements */
			if (!bson_iter_next(sourceArrayIter))
			{
				if (isSignExtended)
				{
					/* No more elements remains in source array and but source is sign extended */
					/* e.g., source = [0,1,2,3,4,......,31]   filter = [0,1,2,.....,33,34] . */
					/* Now let's assume source iter is pointing to 31 and filter iter is pointing to 33 but if  */
					/* source isSignExtended so any position greater than 31 will also be considered as set bit */
					return true;
				}

				return false;
			}
		}
		else if (sourceArrayElement < filterArrayElement)
		{
			/* No more elements remains in source array */
			if (!bson_iter_next(sourceArrayIter))
			{
				if (isSignExtended)
				{
					/* No more elements remains in source array and but source is sign extended */
					/* e.g., source = [0,1,2,3,4,......,31]   filter = [0,1,2,.....,33,34] . */
					/* Now let's assume source iter is pointing to 31 and filter iter is pointing to 33 but if  */
					/* source isSignExtended so any position greater than 31 will also be considered as set bit */
					return true;
				}

				return false;
			}
		}
		else
		{
			/* element of filter is less than source which means that element is not present in source */
			return false;
		}
	}
}


/*
 * Compare set bit position array for $bitsAnySet operator.
 * any element of filter array is  present in source array then it is a match for $bitsAnySet
 * @sourceArrayIter : Iterator of source document array
 * @filterArrayIter : Iterator of filter array
 * @isSignExtended : on true in filter array all value > 31 will be consider as correct match
 */
bool
CompareArrayForBitsAnySet(bson_iter_t *sourceArrayIter, bson_iter_t *filterArrayIter,
						  bool isSignExtended)
{
	/* filter array is empty */
	if (!bson_iter_next(filterArrayIter))
	{
		return false;
	}

	/* source array is empty */
	if (!bson_iter_next(sourceArrayIter))
	{
		return false;
	}

	while (true)
	{
		const bson_value_t *sourceValue = bson_iter_value(sourceArrayIter);
		const bson_value_t *filterValue = bson_iter_value(filterArrayIter);
		int sourceArrayElement = BsonValueAsInt32(sourceValue);
		int filterArrayElement = BsonValueAsInt32(filterValue);

		if (sourceArrayElement == filterArrayElement)
		{
			return true;
		}
		else if (sourceArrayElement < filterArrayElement)
		{
			/* No more elements remains in source array */
			if (!bson_iter_next(sourceArrayIter))
			{
				if (isSignExtended)
				{
					/* No more elements remains in source array and but source is sign extended */
					/* e.g., source = [0,1,2,3,4,......,31]   filter = [0,1,2,.....,33,34] . */
					/* Now let's assume source iter is pointing to 31 and filter iter is pointing to 33 but if  */
					/* source isSignExtended so any position greater than 31 will also be considered as set bit */
					return true;
				}

				return false;
			}
		}
		else
		{
			if (!bson_iter_next(filterArrayIter))
			{
				/* No more elements remains in filter */
				return false;
			}
		}
	}
}


/*
 * Determine type of source document and convert it to positional array.
 * compare source positional array with filter positional array for all bitwise operator and returns true on match
 * @documentValue : source document bson value.
 * @filterArray : positional array of input filter
 * @bitsCompareFunc : Compare function for comprison between source and filter . it will be different for all bitwise operator.
 */
bool
CompareBitwiseOperator(const bson_value_t *documentValue, const
					   bson_value_t *filterArray, CompareArrayForBitwiseOp
					   bitsCompareFunc)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_iter_t sourceArrayIter;
	bson_iter_t filterArrayIter;
	pgbson *setPositionArrayBson;
	bool isMatch = false;
	bool isSignExtended = false;
	bson_type_t documentType = documentValue->value_type;

	switch (documentType)
	{
		case BSON_TYPE_INT64:
		case BSON_TYPE_INT32:
		{
			int64 sourceDocIntvalue = BsonValueAsInt64(documentValue);
			int sourceDocIntSize = sizeof(sourceDocIntvalue);

			/* Negative value are sign extended */
			if (sourceDocIntvalue < 0)
			{
				isSignExtended = true;
			}

			/* create set bit position array of source value */
			WriteSetBitPositionArray((uint8_t *) &sourceDocIntvalue, sourceDocIntSize,
									 &writer);
			break;
		}

		case BSON_TYPE_DECIMAL128:
		{
			int64_t intVal;
			if (IsDecimal128Finite(documentValue))
			{
				if (!IsDecimal128InInt64Range(documentValue))
				{
					return false;
				}
				else
				{
					intVal = BsonValueAsInt64(documentValue);
				}
			}
			else
			{
				/* for NaN and Infinity all bits are unset */
				intVal = 0;
			}

			/* Negative value are sign extended */
			if (intVal < 0)
			{
				isSignExtended = true;
			}

			int sourceDocIntSize = sizeof(intVal);
			WriteSetBitPositionArray((uint8_t *) &intVal, sourceDocIntSize, &writer);
			break;
		}

		case BSON_TYPE_DOUBLE:
		{
			double doubleVal = BsonValueAsDouble(documentValue);

			if (floor(doubleVal) != doubleVal)
			{
				return false;
			}

			/* Negative value are sign extended */
			if (doubleVal < 0)
			{
				isSignExtended = true;
			}

			int64 intVal = BsonValueAsInt64(documentValue);
			int sourceDocIntSize = sizeof(intVal);
			WriteSetBitPositionArray((uint8_t *) &intVal, sourceDocIntSize, &writer);
			break;
		}

		case BSON_TYPE_BINARY:
		{
			unsigned char *decodedData = documentValue->value.v_binary.data;
			int decodedDataLength = documentValue->value.v_binary.data_len;

			/* create set bit position array of source value */
			WriteSetBitPositionArray(decodedData, decodedDataLength, &writer);
			break;
		}

		default:
		{
			return isMatch;
		}
	}

	setPositionArrayBson = PgbsonWriterGetPgbson(&writer);
	PgbsonInitIteratorAtPath(setPositionArrayBson, "", &sourceArrayIter);
	const bson_value_t *sourceArray = bson_iter_value(&sourceArrayIter);
	BsonValueInitIterator(filterArray, &filterArrayIter);
	BsonValueInitIterator(sourceArray, &sourceArrayIter);

	/* compare source bit set position array with filter bit set position array */
	isMatch = bitsCompareFunc(&sourceArrayIter,
							  &filterArrayIter,
							  isSignExtended);
	return isMatch;
}


/*
 * Perform Modulus on a given source value using a given divisor value,
 * and set the remainder arg value.
 * "validateInputs" argument decides whether to validate the input dividend
 * and divisor values. If the caller sets it to false, it the caller's responsibility
 * to provide correct dividend and divisor values. (specially non-zero divisor)
 * Such an arrangement is given so that the caller can provide its own error message,
 * for invalid values.
 */
void
GetRemainderFromModBsonValues(const bson_value_t *dividendValue,
							  const bson_value_t *divisorValue,
							  bool validateInputs,
							  bson_value_t *remainder)
{
	if (validateInputs)
	{
		if (!BsonValueIsNumber(dividendValue) || !BsonValueIsNumber(divisorValue))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"dividend and divisor must be numeric types")));
		}

		if (IsBsonValueInfinity(divisorValue))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"divisor cannot be infinite")));
		}

		if (IsBsonValueNaN(divisorValue))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"divisor cannot be NaN")));
		}

		if ((divisorValue->value_type == BSON_TYPE_DECIMAL128 && IsDecimal128Zero(
				 divisorValue)) ||
			(BsonValueAsDouble(divisorValue) == 0.0))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg(
								"divisor cannot be Zero")));
		}
	}

	if (dividendValue->value_type == BSON_TYPE_DECIMAL128 ||
		divisorValue->value_type == BSON_TYPE_DECIMAL128)
	{
		bson_value_t dividend = *dividendValue;
		bson_value_t divisor = *divisorValue;
		if (divisorValue->value_type != BSON_TYPE_DECIMAL128)
		{
			divisor.value_type = BSON_TYPE_DECIMAL128;
			divisor.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(divisorValue);
		}
		else if (dividendValue->value_type != BSON_TYPE_DECIMAL128)
		{
			dividend.value_type = BSON_TYPE_DECIMAL128;
			dividend.value.v_decimal128 = GetBsonValueAsDecimal128Quantized(
				dividendValue);
		}

		remainder->value_type = BSON_TYPE_DECIMAL128;
		ModDecimal128Numbers(&dividend, &divisor, remainder);
	}
	else
	{
		double dividend = BsonValueAsDouble(dividendValue);
		double divisor = BsonValueAsDouble(divisorValue);

		remainder->value.v_double = fmod(dividend, divisor);
		remainder->value_type = BSON_TYPE_DOUBLE;

		/*
		 * We should return a double unless any of the following is true:
		 *    both operands are long or one is int, we should return long
		 *    one of the operands is double (not Inf) and one long and the remainder fits in an int32, we should return long.
		 *    the dividend is an int32, the other is not Inf and the remainder can be represented as an int32, we should return int.
		 * (ref: 4.4.13\jstests\core\mod_overflow.js, 4.4.13\jstests\aggregation\expressions\expression_mod.js).
		 */
		bool checkFixedInteger = true;
		if ((dividendValue->value_type == BSON_TYPE_INT64 ||
			 divisorValue->value_type == BSON_TYPE_INT64))
		{
			if (dividendValue->value_type != divisorValue->value_type &&
				(dividendValue->value_type == BSON_TYPE_DOUBLE ||
				 divisorValue->value_type == BSON_TYPE_DOUBLE))
			{
				if (isinf(dividend) || isinf(divisor) ||
					!IsBsonValue32BitInteger(remainder, checkFixedInteger))
				{
					/* should return double. */
					return;
				}
			}


			remainder->value_type = BSON_TYPE_INT64;
			remainder->value.v_int64 = (int64_t) remainder->value.v_double;
			return;
		}
		else if (dividendValue->value_type == BSON_TYPE_INT32 &&
				 !isinf(divisor) && !isinf(dividend) &&
				 IsBsonValue32BitInteger(remainder, checkFixedInteger))
		{
			remainder->value_type = BSON_TYPE_INT32;
			remainder->value.v_int32 = (int32_t) remainder->value.v_double;
		}
	}
}


/*
 * Perform Modulus on a given source value using a divisor given in query,
 * and returns true if remainder is equal to the remainder given in query, else returns false.
 * @srcVal : bson value of matched field of source document.
 * @modArrVal : bson value (array) provided in the $mod query [divisor, remainder]
 */
bool
CompareModOperator(const bson_value_t *srcVal, const bson_value_t *modArrVal)
{
	bson_iter_t modArrayIter;
	BsonValueInitIterator(modArrVal, &modArrayIter);

	/* modArrVal went through all the necessary checks in query planning phase */
	/* So no need to recheck its validity here. Use the array values right away */
	bson_iter_next(&modArrayIter);
	const bson_value_t *divisorBsonValue = bson_iter_value(&modArrayIter);
	int64_t divisor = BsonValueAsInt64(divisorBsonValue);

	bson_iter_next(&modArrayIter);
	const bson_value_t *remainderBsonValue = bson_iter_value(&modArrayIter);
	int64_t rem = BsonValueAsInt64(remainderBsonValue);


	bool checkFixedInteger = false;
	if (!IsBsonValue64BitInteger(srcVal, checkFixedInteger))
	{
		return false;
	}

	int64_t dividend = BsonValueAsInt64(srcVal);

	/* For a corner case where dividend is INT64_MIN and divisor is -1 */
	/* the division (INT64_MIN / -1) overflows 64 bits. So handle it separately */
	if (dividend == INT64_MIN && divisor == -1)
	{
		return (rem == 0);
	}

	const int64_t rem_calc = (dividend % divisor);
	return (rem_calc == rem);
}


/* --------------------------------------------------------- */
/* Helpers */
/* --------------------------------------------------------- */


/*
 * Helper for BsonOrderBy that accepts a options to strictly check the types of documents involved
 * in the ordering.
 * Few cases require the type to be same e.g. $sort in `$setWindowFields` stage
 */
static Datum
BsonOrderbyCore(pgbson *document, pgbson *filter, bool validateSort,
				CustomOrderByOptions options)
{
	bson_iter_t documentIterator;
	pgbsonelement filterElement;
	TraverseOrderByValidateState state = {
		{ 0 }, NULL, { 0 }, options
	};

	pgbson_writer writer;
	PgbsonInitIterator(document, &documentIterator);
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	uint32_t filterPathLength = filterElement.pathLength;
	filterElement.pathLength = 0;

	if (TryCheckMetaScoreOrderBy(&filterElement.bsonValue))
	{
		PgbsonWriterInit(&writer);
		double ranking = EvaluateMetaTextScore(document);
		PgbsonWriterAppendDouble(&writer, "rank", 4, ranking);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
	}

	if (!BsonValueIsNumber(&filterElement.bsonValue) && validateSort)
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Invalid sort direction %s",
							   BsonValueToJsonForLogging(
								   &filterElement.bsonValue))));
	}

	int32_t orderBy = BsonValueAsInt32(&filterElement.bsonValue);
	state.isOrderByMin = orderBy == 1;

	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &OrderByExecutionFuncs);

	/* Match order by outputs similar to what the index produces for terms */
	PgbsonWriterInit(&writer);
	if (state.orderByValue.value_type == BSON_TYPE_EOD)
	{
		/* here we write an empty path and minKey so it's sorted first. */
		state.orderByValue.value_type = BSON_TYPE_UNDEFINED;
		PgbsonWriterAppendValue(&writer, filterElement.path, filterPathLength,
								&state.orderByValue);
	}
	else
	{
		PgbsonWriterAppendValue(&writer, filterElement.path, filterPathLength,
								&state.orderByValue);
	}

	return PointerGetDatum(PgbsonWriterGetPgbson(&writer));
}


/*
 * Helper function that abstracts setting up common state
 * and evaluating a comparison function against a value
 * and returning whether or not it matched the query provided.
 */
static bool
CompareBsonValueAgainstQueryCore(const pgbsonelement *element,
								 pgbsonelement *filterElement,
								 TraverseValidateState *state,
								 const TraverseBsonExecutionFuncs *executionFuncs,
								 IsQueryFilterNullFunc isQueryFilterNull)
{
	if (filterElement->pathLength == 0)
	{
		bool isFirstArrayElement = false;
		return state->matchFunc(element, state, isFirstArrayElement);
	}
	else
	{
		/* filter has a nested path - match documents and arrays */
		if (element->bsonValue.value_type != BSON_TYPE_DOCUMENT &&
			element->bsonValue.value_type != BSON_TYPE_ARRAY)
		{
			return false;
		}

		bson_iter_t documentIterator;
		if (!bson_iter_init_from_data(
				&documentIterator,
				element->bsonValue.value.v_doc.data,
				element->bsonValue.value.v_doc.data_len))
		{
			return false;
		}

		filterElement->pathLength = 0;
		TraverseBson(&documentIterator, filterElement->path, state,
					 executionFuncs);
		return ProcessQueryResultAndGetMatch(isQueryFilterNull, state);
	}
}


/*
 * Helper function that abstracts setting up common state
 * and traversing a bson document to the appropriate value,
 * evaluating a comparison function against a value
 * and returning whether or not it matched the query provided.
 */
static bool
CompareBsonAgainstQuery(const pgbson *element,
						const pgbson *filter,
						CompareMatchValueFunc compareFunc,
						IsQueryFilterNullFunc isQueryFilterNull)
{
	bson_iter_t documentIterator;
	pgbsonelement filterElement;
	TraverseElementValidateState state = { { 0 }, NULL };
	PgbsonInitIterator(element, &documentIterator);
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	filterElement.pathLength = 0;
	state.filter = &filterElement;
	state.traverseState.matchFunc = compareFunc;
	TraverseBson(&documentIterator, filterElement.path, &state.traverseState,
				 &CompareExecutionFuncs);
	return ProcessQueryResultAndGetMatch(isQueryFilterNull, &state.traverseState);
}


/*
 * Implements the core logic of <value> $eq <value>
 */
static bool
CompareEqualMatch(const pgbsonelement *documentIterator,
				  TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	bool isPathValid;
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	if (validationState->filter->bsonValue.value_type == BSON_TYPE_NULL &&
		documentIterator->bsonValue.value_type == BSON_TYPE_UNDEFINED)
	{
		return true;
	}

	int cmp = CompareLogicalOperator(documentIterator, validationState->filter,
									 &isPathValid);
	return isPathValid && cmp == 0;
}


/*
 * Implements the core logic of <value> $lt <value>
 */
static bool
CompareLessMatch(const pgbsonelement *documentIterator,
				 TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	bool isPathValid;
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	if (validationState->filter->bsonValue.value_type == BSON_TYPE_MAXKEY)
	{
		return documentIterator->bsonValue.value_type != BSON_TYPE_MAXKEY;
	}

	int cmp = CompareLogicalOperator(documentIterator, validationState->filter,
									 &isPathValid);
	return isPathValid && cmp < 0;
}


/*
 * Implements the core logic of <value> $lte <value>
 */
static bool
CompareLessEqualMatch(const pgbsonelement *documentIterator,
					  TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	bool isPathValid;
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	if (validationState->filter->bsonValue.value_type == BSON_TYPE_MAXKEY)
	{
		return true;
	}

	int cmp = CompareLogicalOperator(documentIterator, validationState->filter,
									 &isPathValid);
	return isPathValid && cmp <= 0;
}


/*
 * Implements the core logic of <value> $gt <value>
 */
static bool
CompareGreaterMatch(const pgbsonelement *documentIterator,
					TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	bool isPathValid;
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	if (validationState->filter->bsonValue.value_type == BSON_TYPE_MINKEY)
	{
		return documentIterator->bsonValue.value_type != BSON_TYPE_MINKEY;
	}

	int cmp = CompareLogicalOperator(documentIterator, validationState->filter,
									 &isPathValid);
	return isPathValid && cmp > 0;
}


/*
 * Implements the core logic of <value> $gte <value>
 */
static bool
CompareGreaterEqualMatch(const pgbsonelement *documentIterator,
						 TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	bool isPathValid;
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	if (validationState->filter->bsonValue.value_type == BSON_TYPE_MINKEY)
	{
		return true;
	}
	int cmp = CompareLogicalOperator(documentIterator, validationState->filter,
									 &isPathValid);
	return isPathValid && cmp >= 0;
}


/*
 * Implements the core logic of <value> $exists <value>
 */
static bool
CompareExistsMatch(const pgbsonelement *documentIterator,
				   TraverseValidateState *validationState, bool isFirstArrayTerm)
{
	/* exists only asserts that there's a value there. */
	/* if we get to the compare - we can always return true. */
	return true;
}


/*
 * Implements the core logic of <value> $in <value>
 */
static bool
CompareInMatch(const pgbsonelement *documentIterator,
			   TraverseValidateState *validationState, bool
			   isFirstArrayTerm)
{
	TraverseInValidateState *inValidationState =
		(TraverseInValidateState *) validationState;

	bson_iter_t arrayIter;
	BsonValueInitIterator(&inValidationState->filter->bsonValue, &arrayIter);
	TraverseRegexValidateState nestedRegexState = {
		{ 0 }, { 0 }
	};

	bool match = false;

	/* 1: if $in has any NULL entry see if matches with document */
	if (inValidationState->hasNull)
	{
		match = documentIterator->bsonValue.value_type == BSON_TYPE_NULL ||
				documentIterator->bsonValue.value_type == BSON_TYPE_UNDEFINED;

		if (match)
		{
			return true;
		}
	}

	/* 2: verify if document matches with any entry of $in which are hashed */
	hash_search(inValidationState->bsonValueHashSet, &documentIterator->bsonValue,
				HASH_FIND, &match);

	if (match)
	{
		return true;
	}

	/* 3: iterate over all regex entry present in $in and see matches with document */
	List *regexList = inValidationState->regexList;
	ListCell *regexData = NULL;
	foreach(regexData, regexList)
	{
		RegexData *data = (RegexData *) lfirst(regexData);
		nestedRegexState.regexData.regex = data->regex;
		nestedRegexState.regexData.options = data->options;
		nestedRegexState.regexData.pcreData = data->pcreData;

		if (CompareRegexMatch(documentIterator, &nestedRegexState.traverseState,
							  isFirstArrayTerm))
		{
			return true;
		}
	}

	return false;
}


/*
 * Implements the core logic of <value> $size <value>
 */
static bool
CompareArraySizeMatch(const pgbsonelement *documentIterator,
					  TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	if (documentIterator->bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		return false;
	}

	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	const pgbsonelement *filterElement = validationState->filter;
	int64_t arraySize = BsonValueAsInt64(&filterElement->bsonValue);
	bson_iter_t arrayIterator;
	int64_t actualArraySize = 0;
	bson_iter_init_from_data(&arrayIterator, documentIterator->bsonValue.value.v_doc.data,
							 documentIterator->bsonValue.value.v_doc.data_len);

	/* count array size or bail if the array gets too big. */
	/* We iterate one more than the desired length to ensure we increment */
	/* to higher than the array size if the array is actually bigger. */
	while (bson_iter_next(&arrayIterator) && actualArraySize <= arraySize)
	{
		actualArraySize++;
	}

	return arraySize == actualArraySize;
}


/*
 * Given a particular <path, type, value> validates that the value
 * matches the type requested by the filter. If it doesn't match returns false.
 */
static bool
CompareArrayTypeMatch(const pgbsonelement *documentIterator,
					  TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	if (validationState->filter->bsonValue.value_type == BSON_TYPE_UTF8)
	{
		/* validate the type matches if only a single value is specified. */
		if (strcmp(validationState->filter->bsonValue.value.v_utf8.str, "number") == 0)
		{
			return BsonTypeIsNumber(documentIterator->bsonValue.value_type);
		}

		const char *typeName = BsonTypeName(documentIterator->bsonValue.value_type);
		return strcmp(typeName, validationState->filter->bsonValue.value.v_utf8.str) == 0;
	}
	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	else if (BsonValueIsNumberOrBool(&validationState->filter->bsonValue))
	{
		int64_t numericValue = BsonValueAsInt64(&validationState->filter->bsonValue);

		/* TryGetTypeFromInt64 should always return true as we already validated this in the query planner. */
		bson_type_t typeToMatch;
		TryGetTypeFromInt64(numericValue, &typeToMatch);
		return typeToMatch == documentIterator->bsonValue.value_type;
	}
	else if (validationState->filter->bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		const char *typeName = BsonTypeName(documentIterator->bsonValue.value_type);
		bson_iter_t arrayIterator;
		bson_iter_init_from_data(
			&arrayIterator,
			validationState->filter->bsonValue.value.v_doc.data,
			validationState->filter->bsonValue.value.v_doc.data_len);
		while (bson_iter_next(&arrayIterator))
		{
			/* if it's an array, validate any type matches. */
			const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);

			/* the types were validated before this point already, but double
			 * check them just in case
			 *
			 * TODO: FIXME - Verify whether strict number check is required here
			 * */
			if (BsonValueIsNumberOrBool(arrayValue))
			{
				int64_t numericValue = BsonValueAsInt64(arrayValue);

				/* TryGetTypeFromInt64 should always return true as we already validated this in the query planner. */
				bson_type_t typeToMatch;
				TryGetTypeFromInt64(numericValue, &typeToMatch);
				if (typeToMatch == documentIterator->bsonValue.value_type)
				{
					return true;
				}
			}
			else if (arrayValue->value_type == BSON_TYPE_UTF8)
			{
				const char *arrayElement = arrayValue->value.v_utf8.str;
				if (strcmp(arrayElement, "number") == 0)
				{
					if (BsonTypeIsNumber(documentIterator->bsonValue.value_type))
					{
						return true;
					}
				}
				else if (strcmp(typeName, arrayElement) == 0)
				{
					return true;
				}
			}
		}

		/* no values in the array matched - fail. */
		return false;
	}

	return false;
}


/*
 * Given a particular <path, value, filter> validates that the element
 * meets the criteria for the $all operator filter.
 * If it doesn't match returns false.
 */
static bool
CompareAllMatch(const pgbsonelement *documentIterator,
				TraverseValidateState *traverseState, bool isFirstArrayTerm)
{
	TraverseAllValidateState *validationState =
		(TraverseAllValidateState *) traverseState;
	bson_iter_t arrayIterator;
	bson_iter_init_from_data(
		&arrayIterator,
		validationState->elementState.filter->bsonValue.value.v_doc.data,
		validationState->elementState.filter->bsonValue.value.v_doc.data_len);

	int index = 0;
	bool res = true;
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);

		/* we haven't matched the current element in $all,
		 * try to match it */
		if (!validationState->matchState[index])
		{
			bool match;
			if (validationState->evalStateArray != NULL &&
				validationState->evalStateArray[index] != NULL)
			{
				/* For $elemMatch cases, $all only matches arrays for the $elemmatch
				 * condition. Skip any non-array paths as not matched.
				 */
				if (documentIterator->bsonValue.value_type != BSON_TYPE_ARRAY)
				{
					match = false;
				}
				else
				{
					match = EvalBooleanExpressionAgainstArray(
						validationState->evalStateArray[index],
						&documentIterator->bsonValue);
				}
			}
			else if (arrayValue->value_type == BSON_TYPE_REGEX)
			{
				RegexData regexData;

				regexData.regex = arrayValue->value.v_regex.regex;
				regexData.options = arrayValue->value.v_regex.options;
				regexData.pcreData = validationState->crePcreData[index];
				match = CompareRegexTextMatch(&documentIterator->bsonValue, &regexData);
			}
			else
			{
				bool isComparisonValid = false;
				int cmp = CompareBsonValueAndType(arrayValue,
												  &documentIterator->bsonValue,
												  &isComparisonValid);
				match = isComparisonValid && cmp == 0;
			}

			validationState->matchState[index] = match;
			res = res && match;
		}

		index++;
	}

	return res;
}


/*
 * CompareForOrderBy takes a current element that is in the document,
 * a query-wide State, and updates the state with the new element if
 * it is a better match for the ORDER BY expression (based on min/max)
 * than the one selected so far.
 */
static void
CompareForOrderBy(const pgbsonelement *element,
				  TraverseOrderByValidateState *validationState)
{
	if (validationState->orderByValue.value_type == BSON_TYPE_EOD)
	{
		validationState->orderByValue = element->bsonValue;
	}
	else
	{
		bool isComparisonValidIgnore;
		int sortOrderCmp = CompareBsonValueAndType(&element->bsonValue,
												   &validationState->orderByValue,
												   &isComparisonValidIgnore);

		if (sortOrderCmp > 0 && !validationState->isOrderByMin)
		{
			validationState->orderByValue = element->bsonValue;
		}
		else if (sortOrderCmp < 0 && validationState->isOrderByMin)
		{
			validationState->orderByValue = element->bsonValue;
		}
	}
}


/*
 * Implements the core logic of comparing 2 values and returns
 * -1 if left < right, 0 if they are equal, and 1 if left > right
 * Also returns a boolean that indicates whether the comparison is
 * valid (e.g. comparing NaN is an invalid comparison).
 */
static int
CompareLogicalOperator(const pgbsonelement *documentElement, const
					   pgbsonelement *filterElement,
					   bool *isPathValid)
{
	/* Comparisons only work on the same sort order type. */
	int cmp = CompareBsonSortOrderType(&documentElement->bsonValue,
									   &filterElement->bsonValue);
	if (cmp == 0)
	{
		return CompareBsonValueAndType(&documentElement->bsonValue,
									   &filterElement->bsonValue, isPathValid);
	}
	else
	{
		*isPathValid = false;
		return 0;
	}
}


/*
 * Implements the core logic of <value> $exists <value>
 */
static bool
IsExistPositiveMatch(pgbson *filter)
{
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	bool existsPositiveMatch = true;

	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	if (BsonValueIsNumberOrBool(&filterElement.bsonValue) &&
		BsonValueAsInt64(&filterElement.bsonValue) == 0)
	{
		existsPositiveMatch = false;
	}

	return existsPositiveMatch;
}


/*
 * Given a top level function arguments for regex and a traverse state for
 * regex, populates the data inside the validation state.
 */
static pgbsonelement
PopulateRegexState(PG_FUNCTION_ARGS, TraverseRegexValidateState *state)
{
	pgbson *filter = PG_GETARG_PGBSON(1);
	const RegexData *regexState;
	pgbsonelement filterElement;

	PgbsonToSinglePgbsonElement(filter, &filterElement);

	/* State populated if and only if cached state is unusable */
	RegexData localState = { 0 };

	SetCachedFunctionState(regexState, RegexData, 1, PopulateRegexFromQuery, filter);
	if (regexState == NULL)
	{
		/* Cache is not available */
		PopulateRegexFromQuery(&localState, filter);
		regexState = &localState;
	}

	state->regexData = *regexState;
	state->traverseState.matchFunc = CompareRegexMatch;
	return filterElement;
}


/*
 * Helper method that extracts the regex from a query filter and sets the necessary
 * state in the TraverseRegexValidationState for use in traversing and evaluating
 * $regex matches.
 */
static void
PopulateRegexFromQuery(RegexData *regexState, pgbson *filter)
{
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);

	if (filterElement.bsonValue.value_type != BSON_TYPE_UTF8 &&
		filterElement.bsonValue.value_type != BSON_TYPE_REGEX)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$regex has to be a string")));
	}


	if (filterElement.bsonValue.value_type == BSON_TYPE_REGEX)
	{
		regexState->regex = filterElement.bsonValue.value.v_regex.regex;
		regexState->options = filterElement.bsonValue.value.v_regex.options;
	}
	else
	{
		regexState->regex = filterElement.bsonValue.value.v_utf8.str;
		regexState->options = NULL;
	}

	regexState->pcreData = RegexCompile(regexState->regex,
										regexState->options);
}


/*
 * Given a top level function arguments for elemMatch and a traverse state
 * for elemMatch, populates the data inside the validation state.
 */
static pgbsonelement
PopulateElemMatchValidationState(PG_FUNCTION_ARGS, TraverseElemMatchValidateState *state)
{
	pgbson *filter = PG_GETARG_PGBSON(1);
	const BsonElemMatchQueryState *elemMatchState;

	/* State populated iff cached state is unusable */
	BsonElemMatchQueryState localState = { 0 };

	SetCachedFunctionState(
		elemMatchState,
		BsonElemMatchQueryState,
		1,
		PopulateElemMatchStateFromQuery,
		filter);

	if (elemMatchState == NULL)
	{
		/* need to repopulate it for the query: Using existing state is unsafe */
		PopulateElemMatchStateFromQuery(&localState, filter);
		elemMatchState = &localState;
	}

	state->expressionEvaluationState = elemMatchState->expressionEvaluationState;
	state->traverseState.matchFunc = CompareElemMatchMatch;
	state->isEmptyElemMatch = elemMatchState->isEmptyElemMatch;
	return elemMatchState->filterElement;
}


/*
 * Helper function for $elemMatch that processes a given query filter, compiles the filter into a
 * ExprEvalState and caches the result into the function memory context.
 * If it's already cached, returns the cached value.
 */
static void
PopulateElemMatchStateFromQuery(BsonElemMatchQueryState *state, const pgbson *filter)
{
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);

	state->isEmptyElemMatch = IsBsonValueEmptyDocument(&filterElement.bsonValue);
	state->expressionEvaluationState = GetExpressionEvalState(&filterElement.bsonValue,
															  CurrentMemoryContext);
	state->filterElement = filterElement;
}


/*
 * Helper function for $range that processes a given query filter, compiles the filter into a
 * TraverseRangeValidateState and caches the result into the function memory context.
 * If it's already cached, returns the cached value.
 */
static void
PopulateRangeStateFromQuery(DollarRangeParams *params, const
							pgbson *filter)
{
	*params = *ParseQueryDollarRange((pgbson *) filter);
	Assert(params != NULL);
}


/*
 * Based on the PG_FUNCTION_ARGS, builds or retrieves the cached
 * per-query state for $all. Using the cached state, the function
 * then populates the per query instance state (TraverseAllValidateState)
 * used during execution and evaluation of $all.
 */
static void
PopulateDollarAllValidationState(PG_FUNCTION_ARGS,
								 TraverseAllValidateState *state,
								 pgbsonelement *filterElement)
{
	pgbson *filter = PG_GETARG_PGBSON(1);
	const BsonDollarAllQueryState *dollarAllState;

	/* State populated iff cached state is unavailable */
	BsonDollarAllQueryState localState = { 0 };

	SetCachedFunctionState(
		dollarAllState,
		BsonDollarAllQueryState,
		1,
		PopulateDollarAllStateFromQuery,
		filter);

	if (dollarAllState == NULL)
	{
		/* need to repopulate it for the query: Existing state is unsafe */
		PopulateDollarAllStateFromQuery(&localState, filter);
		dollarAllState = &localState;
	}

	*filterElement = dollarAllState->filterElement;
	state->elementState.filter = filterElement;
	state->elementState.traverseState.matchFunc = CompareAllMatch;
	state->arrayHasOnlyNulls = dollarAllState->arrayHasOnlyNulls;
	state->evalStateArray = dollarAllState->evalStateArray;
	state->crePcreData = dollarAllState->crePcreData;
	state->matchState = NULL;
	if (dollarAllState->numElements > 0)
	{
		state->matchState = palloc0(sizeof(bool) * dollarAllState->numElements);
	}
}


/*
 * Helper function for $all that processes a given query filter, and extracts
 * commonly used filter values for the query. This includes the number of elements
 * in the $all array, any $elemMatch states, and whether the $all array is only nulls.
 */
static void
PopulateDollarAllStateFromQuery(BsonDollarAllQueryState *dollarAllState,
								const pgbson *filter)
{
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	if (filterElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$all needs an array")));
	}

	uint32_t numElements = 0;
	uint32_t numElemMatches = 0;
	bson_iter_t arrayIterator;
	if (!bson_iter_init_from_data(
			&arrayIterator,
			filterElement.bsonValue.value.v_doc.data,
			filterElement.bsonValue.value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(MongoBadValue),
						errmsg("Could not read array for $all")));
	}

	/* First pass - compute the set of elements */
	bool arrayHasOnlyNull = true;
	bool regexFound = false;
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);
		arrayHasOnlyNull = arrayHasOnlyNull && arrayValue->value_type == BSON_TYPE_NULL;
		regexFound = regexFound || (arrayValue->value_type == BSON_TYPE_REGEX);

		pgbsonelement elementValue;
		if (arrayValue->value_type == BSON_TYPE_DOCUMENT &&
			TryGetBsonValueToPgbsonElement(arrayValue, &elementValue))
		{
			if (strcmp(elementValue.path, "$elemMatch") == 0)
			{
				numElemMatches++;
			}
		}

		numElements++;
	}

	/* second pass - build up the set of elements */

	dollarAllState->filterElement = filterElement;
	dollarAllState->arrayHasOnlyNulls = arrayHasOnlyNull;
	dollarAllState->numElements = numElements;
	dollarAllState->evalStateArray = NULL;
	dollarAllState->crePcreData = NULL;

	if (numElemMatches > 0)
	{
		dollarAllState->evalStateArray = palloc(sizeof(ExprEvalState *) *
												numElements);
		bson_iter_init_from_data(
			&arrayIterator,
			filterElement.bsonValue.value.v_doc.data,
			filterElement.bsonValue.value.v_doc.data_len);
		numElements = 0;
		while (bson_iter_next(&arrayIterator))
		{
			const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);

			dollarAllState->evalStateArray[numElements] = NULL;
			pgbsonelement elementValue;
			if (arrayValue->value_type == BSON_TYPE_DOCUMENT &&
				TryGetBsonValueToPgbsonElement(arrayValue, &elementValue))
			{
				if (strcmp(elementValue.path, "$elemMatch") == 0)
				{
					dollarAllState->evalStateArray[numElements] =
						GetExpressionEvalState(&elementValue.bsonValue,
											   CurrentMemoryContext);
				}
			}

			numElements++;
		}
	}
	else if (regexFound)
	{
		dollarAllState->crePcreData = palloc(sizeof(PcreData *) * numElements);

		bson_iter_init_from_data(
			&arrayIterator,
			filterElement.bsonValue.value.v_doc.data,
			filterElement.bsonValue.value.v_doc.data_len);

		for (numElements = 0; bson_iter_next(&arrayIterator); numElements++)
		{
			const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);

			dollarAllState->crePcreData[numElements] = NULL;

			if (arrayValue->value_type == BSON_TYPE_REGEX)
			{
				dollarAllState->crePcreData[numElements] =
					RegexCompile(arrayValue->value.v_regex.regex,
								 arrayValue->value.v_regex.options);
			}
		}
	}
}


/*
 * Parses the expr document from $filter and constructs an aggregation expression data
 * Also if the variableSpec is not null, parses and stores it in state.
 */
static void
PopulateExprStateFromQuery(BsonDollarExprQueryState *state,
						   const pgbson *filter, const pgbson *variableSpec)
{
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(filter, &element);

	AggregationExpressionData *exprData = (AggregationExpressionData *) palloc0(
		sizeof(AggregationExpressionData));
	ParseAggregationExpressionData(exprData, &element.bsonValue);
	state->expression = exprData;

	if (variableSpec != NULL)
	{
		bson_value_t varsValue = ConvertPgbsonToBsonValue(variableSpec);
		ExpressionVariableContext *variableContext =
			palloc0(sizeof(ExpressionVariableContext));

		ParseVariableSpec(&varsValue, variableContext);
		state->variableContext = variableContext;
	}
}


/*
 * Based on the PG_FUNCTION_ARGS, builds or retrieves the cached
 * per-query state for $in. Using the cached state, the function
 * then populates the per query instance state (TraverseElementValidateState)
 * used during execution of $in.
 */
static void
PopulateDollarInValidationState(PG_FUNCTION_ARGS,
								TraverseInValidateState *state,
								pgbsonelement *filterElement)
{
	pgbson *filter = PG_GETARG_PGBSON(1);

	BsonDollarInQueryState *dollarInState;

	/* State populated iff cached state is unavailable */
	BsonDollarInQueryState localState = { { 0 }, NULL, NULL, false };

	SetCachedFunctionState(
		dollarInState,
		BsonDollarInQueryState,
		1,
		PopulateDollarInStateFromQuery,
		filter);
	if (dollarInState == NULL)
	{
		/* Need to repopulate it for the query: Existing state is unsafe */
		PopulateDollarInStateFromQuery(&localState, filter);
		dollarInState = &localState;
	}

	*filterElement = dollarInState->filterElement;

	state->filter = filterElement;
	state->traverseState.matchFunc = CompareInMatch;
	state->hasNull = dollarInState->hasNull;
	state->regexList = dollarInState->regexList;
	state->bsonValueHashSet = dollarInState->bsonValueHashSet;
}


/*
 * Helper function for $in that processes a given query filter and compiles any regex
 * contained in the query filter
 */
static void
PopulateDollarInStateFromQuery(BsonDollarInQueryState *dollarInState,
							   const pgbson *filter)
{
	pgbsonelement filterElement;
	bson_iter_t arrayIterator;

	PgbsonToSinglePgbsonElement(filter, &filterElement);
	BsonValueInitIterator(&filterElement.bsonValue, &arrayIterator);

	dollarInState->filterElement = filterElement;
	dollarInState->regexList = NIL;

	/* Generate a hash table for the $in input array, which is created per query and automatically destroyed after query execution. */
	dollarInState->bsonValueHashSet = CreateBsonValueHashSet();

	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);

		if (arrayValue->value_type == BSON_TYPE_NULL)
		{
			dollarInState->hasNull = true;
		}
		else if (arrayValue->value_type == BSON_TYPE_REGEX)
		{
			RegexData *regexData = palloc(sizeof(RegexData));
			regexData->regex = arrayValue->value.v_regex.regex;
			regexData->options = arrayValue->value.v_regex.options;
			regexData->pcreData = RegexCompile(arrayValue->value.v_regex.regex,
											   arrayValue->value.v_regex.options);
			dollarInState->regexList = lappend(dollarInState->regexList, regexData);
		}
		else
		{
			bool found = false;
			hash_search(dollarInState->bsonValueHashSet, arrayValue, HASH_ENTER, &found);
		}
	}
}


/*
 * Checks if a single value in a pgbsonelement is null.
 * Used in $eq/$ne/$gte/$lte scenarios to validate that the filter is null to apply
 * special case logic for missing fields.
 */
static bool
IsQueryFilterNullForValue(const TraverseValidateState *state)
{
	TraverseElementValidateState *elementState = (TraverseElementValidateState *) state;
	return elementState->filter->bsonValue.value_type == BSON_TYPE_NULL;
}


/*
 * Checks if a value in an array inside the pgbsonelement is null.
 * Used in $in/$nin scenarios to validate that the filter is null to apply
 * special case logic for missing fields.
 */
static bool
IsQueryFilterNullForArray(const TraverseValidateState *state)
{
	TraverseInValidateState *elementState = (TraverseInValidateState *) state;
	if (elementState->filter->bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Expecting an array input for filter but found type %s",
							BsonTypeName(
								elementState->filter->
								bsonValue.value_type))));
	}

	bson_iter_t filterArrayIterator;
	bson_iter_init_from_data(&filterArrayIterator,
							 elementState->filter->bsonValue.value.v_doc.data,
							 elementState->filter->bsonValue.value.v_doc.data_len);
	while (bson_iter_next(&filterArrayIterator))
	{
		if (bson_iter_type(&filterArrayIterator) == BSON_TYPE_NULL)
		{
			return true;
		}
	}

	return false;
}


/*
 * Returns true if the array of elements in $all are all null.
 */
static bool
IsQueryFilterNullForDollarAll(const TraverseValidateState *state)
{
	TraverseAllValidateState *elementState = (TraverseAllValidateState *) state;
	return elementState->arrayHasOnlyNulls;
}


/*
 * The last function parameter 'bool ignore' is not applicable here.
 * This is used in functions like CompareOrderByMinMatch,
 * CompareOrderByMaxMatch. The Match functions are hooked to the
 * last argument of the function TraverseBsonAndValidateMatch()
 * and hence the function signature is fixed.
 */
static bool
CompareRegexMatch(const pgbsonelement *documentElement,
				  TraverseValidateState *validationState, bool ignore)
{
	TraverseRegexValidateState *regexState =
		(TraverseRegexValidateState *) validationState;
	return CompareRegexTextMatch(&documentElement->bsonValue, &(regexState->regexData));
}


/*
 * Implements the core logic of <value> $elemMatch <value>
 * For the given value, checks if it's an array, and if it is,
 * evaluates the query expression against every element in that array.
 * If at least one returns true, returns a match.
 */
static bool
CompareElemMatchMatch(const pgbsonelement *documentIterator,
					  TraverseValidateState *validationState, bool isFirstArrayTerm)
{
	TraverseElemMatchValidateState *elemMatchState =
		(TraverseElemMatchValidateState *) validationState;

	if (documentIterator->bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		return false;
	}

	if (elemMatchState->isEmptyElemMatch)
	{
		/* An empty elemMatch only matches documents and arrays */
		bson_iter_t arrayIter;
		BsonValueInitIterator(&documentIterator->bsonValue, &arrayIter);
		while (bson_iter_next(&arrayIter))
		{
			if (bson_iter_type(&arrayIter) == BSON_TYPE_DOCUMENT ||
				bson_iter_type(&arrayIter) == BSON_TYPE_ARRAY)
			{
				return true;
			}
		}

		return false;
	}
	else
	{
		return EvalBooleanExpressionAgainstArray(
			elemMatchState->expressionEvaluationState,
			&documentIterator->bsonValue);
	}
}


/*
 * $bitsAllClear comparison between source document and filter.
 */
static bool
CompareBitsAllClearMatch(const pgbsonelement *documentIterator,
						 TraverseValidateState *traverseState,
						 bool isFirstArrayTerm)
{
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	const bson_value_t *filterArray = &validationState->filter->bsonValue;

	return CompareBitwiseOperator(&documentIterator->bsonValue, filterArray,
								  CompareArrayForBitsAllClear);
}


/*
 * $bitsAnyClear comparison between source document and filter.
 */
static bool
CompareBitsAnyClearMatch(const pgbsonelement *documentIterator,
						 TraverseValidateState *traverseState,
						 bool isFirstArrayTerm)
{
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	const bson_value_t *filterArray = &validationState->filter->bsonValue;

	return CompareBitwiseOperator(&documentIterator->bsonValue, filterArray,
								  CompareArrayForBitsAnyClear);
}


/*
 * $bitsAllSet comparison between source document and filter.
 */
static bool
CompareBitsAllSetMatch(const pgbsonelement *documentIterator,
					   TraverseValidateState *traverseState,
					   bool isFirstArrayTerm)
{
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	const bson_value_t *filterArray = &validationState->filter->bsonValue;

	return CompareBitwiseOperator(&documentIterator->bsonValue, filterArray,
								  CompareArrayForBitsAllSet);
}


/*
 * $bitsAnySet comparison between source document and filter.
 */
static bool
CompareBitsAnySetMatch(const pgbsonelement *documentIterator,
					   TraverseValidateState *traverseState,
					   bool isFirstArrayTerm)
{
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	const bson_value_t *filterArray = &validationState->filter->bsonValue;

	return CompareBitwiseOperator(&documentIterator->bsonValue, filterArray,
								  CompareArrayForBitsAnySet);
}


/*
 * Visits the top level field of a given path (e.g. the value at a.b.c given a filterPath of a.b.c)
 * And runs the comparison logic against the value found at that path.
 * Returns true if comparison logic should continue processing.
 */
static bool
CompareVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
						  void *state)
{
	TraverseValidateState *validateState = (TraverseValidateState *) state;

	/*
	 *  TODO: Remove resetting the path length.
	 *
	 *  The path length was set to 0 to make sure that bson_compare() does not compare based on the path when the comparison was done using bson_iter.
	 *  The code was later refactored in the following PR to use CompareBsonValueAndType() but resetting of the path length was not removed.
	 *
	 *  Please search the code for more "pathLength = 0" while addressing the TODO.
	 *
	 */
	element->pathLength = 0;


	bool isFirstArrayTerm = false;
	bool isMatched = validateState->matchFunc(element, validateState, isFirstArrayTerm);
	if (isMatched)
	{
		validateState->compareResult = CompareResult_Match;
		return false;
	}

	/* continue parsing if not matched */
	validateState->compareResult = CompareResult_Mismatch;
	return !isMatched;
}


/*
 * Visits the top level field of a given path (e.g. the value at a.b.c given a filterPath of a.b.c)
 * And runs the comparison logic against the value found at that path.
 * Returns true if comparison logic should continue processing.
 */
static bool
CompareVisitArrayField(pgbsonelement *element, const StringView *filterPath, int
					   arrayIndex,
					   void *state)
{
	TraverseValidateState *validateState = (TraverseValidateState *) state;
	element->pathLength = 0;
	bool isFirstArrayTerm = arrayIndex == 0;
	bool isInnerMatched = validateState->matchFunc(element, validateState,
												   isFirstArrayTerm);
	if (isInnerMatched)
	{
		validateState->compareResult = CompareResult_Match;
		return false;
	}

	/* continue parsing if not matched */
	return true;
}


/*
 * Updates the comparison logic with the traverse result for path not found/mismatches.
 */
static void
CompareSetTraverseResult(void *state, TraverseBsonResult traverseResult)
{
	TraverseValidateState *validateState = (TraverseValidateState *) state;
	switch (traverseResult)
	{
		case TraverseBsonResult_PathNotFound:
		{
			validateState->compareResult = CompareResult_PathNotFound;
			break;
		}

		case TraverseBsonResult_TypeMismatch:
		{
			validateState->compareResult = CompareResult_Mismatch;
			break;
		}

		default:
		{
			ereport(ERROR, (errmsg("Unexpected traverse result %d", traverseResult)));
		}
	}
}


/*
 * Returns whether or not comparison searches should continue.
 */
static bool
CompareContinueProcessIntermediateArray(void *state, const bson_value_t *value)
{
	TraverseValidateState *validateState = (TraverseValidateState *) state;
	return validateState->compareResult != CompareResult_Match;
}


/*
 * Visits a top level field for an ORDER BY. This skips array types
 * since inner array elements are then visited next.
 */
static bool
OrderByVisitTopLevelField(pgbsonelement *element, const
						  StringView *filterPath,
						  void *state)
{
	TraverseOrderByValidateState *validateState = (TraverseOrderByValidateState *) state;

	/* Check if there is not strict type requirement */
	if ((validateState->options & CustomOrderByOptions_AllowOnlyDates) ==
		CustomOrderByOptions_AllowOnlyDates)
	{
		if (element->bsonValue.value_type != BSON_TYPE_DATE_TIME)
		{
			ereport(ERROR, (errcode(MongoLocation5429513),
							errmsg(
								"PlanExecutor error during aggregation :: caused by :: "
								"Invalid range: Expected the sortBy field to be a Date, "
								"but it was %s", BsonTypeName(
									element->bsonValue.value_type)),
							errhint(
								"PlanExecutor error during aggregation :: caused by :: "
								"Invalid range: Expected the sortBy field to be a Date, "
								"but it was %s", BsonTypeName(
									element->bsonValue.value_type))));
		}
	}

	if ((validateState->options & CustomOrderByOptions_AllowOnlyNumbers) ==
		CustomOrderByOptions_AllowOnlyNumbers)
	{
		if (!BsonTypeIsNumber(element->bsonValue.value_type))
		{
			ereport(ERROR, (errcode(MongoLocation5429414),
							errmsg(
								"PlanExecutor error during aggregation :: caused by :: "
								"Invalid range: Expected the sortBy field to be a number, "
								"but it was %s", BsonTypeName(
									element->bsonValue.value_type)),
							errhint(
								"PlanExecutor error during aggregation :: caused by :: "
								"Invalid range: Expected the sortBy field to be a number, "
								"but it was %s", BsonTypeName(
									element->bsonValue.value_type))));
		}
	}

	if (element->bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		/* These are processed as array fields - do nothing */
		return true;
	}

	CompareForOrderBy(element, validateState);
	return true;
}


/*
 * Visits recursed array fields in an ORDER BY.
 */
static bool
OrderByVisitArrayField(pgbsonelement *element, const StringView *filterPath,
					   int arrayIndex, void *state)
{
	TraverseOrderByValidateState *validateState = (TraverseOrderByValidateState *) state;
	CompareForOrderBy(element, validateState);
	return true;
}


/*
 * $mod comparison between source document and filter.
 */
static bool
CompareModMatch(const pgbsonelement *document,
				TraverseValidateState *traverseState,
				bool isFirstArrayTerm)
{
	TraverseElementValidateState *validationState =
		(TraverseElementValidateState *) traverseState;
	const bson_value_t *modArrVal = &validationState->filter->bsonValue;

	return CompareModOperator(&document->bsonValue, modArrVal);
}


/*
 * Visits a top level field in a $range operator.
 */
static bool
DollarRangeVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
							  void *state)
{
	TraverseRangeValidateState *rangeState = (TraverseRangeValidateState *) state;

	pgbsonelement filterElement = { 0 };
	filterElement.path = filterPath->string;
	filterElement.pathLength = filterPath->length;
	filterElement.bsonValue = rangeState->params.minValue;

	TraverseElementValidateState elementState = { { 0 }, NULL };
	elementState.filter = &filterElement;

	bool minCondition = rangeState->params.isMinInclusive ?
						CompareGreaterEqualMatch(element, &elementState.traverseState,
												 false)
						: CompareGreaterMatch(element, &elementState.traverseState,
											  false);

	filterElement.bsonValue = rangeState->params.maxValue;

	bool maxCondition = rangeState->params.isMaxInclusive ?
						CompareLessEqualMatch(element, &elementState.traverseState, false)
						: CompareLessMatch(element, &elementState.traverseState, false);

	if (minCondition && maxCondition)
	{
		rangeState->elementState.traverseState.compareResult =
			CompareResult_Match;
		return false;
	}
	else
	{
		rangeState->elementState.traverseState.compareResult =
			CompareResult_Mismatch;
		return true;
	}
}


/*
 * Visits array fields in an $range operator.
 */
static bool
DollarRangeVisitArrayField(pgbsonelement *element, const StringView *filterPath,
						   int arrayIndex, void *state)
{
	TraverseRangeValidateState *rangeState = (TraverseRangeValidateState *) state;

	if (arrayIndex == 0)
	{
		rangeState->isMinConditionSet = false;
		rangeState->isMaxConditionSet = false;
	}

	pgbsonelement filterElement = { 0 };
	filterElement.path = filterPath->string;
	filterElement.pathLength = filterPath->length;
	filterElement.bsonValue = rangeState->params.minValue;

	TraverseElementValidateState elementState = { { 0 }, NULL };
	elementState.filter = &filterElement;

	bool minCondition = rangeState->params.isMinInclusive ?
						CompareGreaterEqualMatch(element, &elementState.traverseState,
												 false)
						: CompareGreaterMatch(element, &elementState.traverseState,
											  false);

	filterElement.bsonValue = rangeState->params.maxValue;

	bool maxCondition = rangeState->params.isMaxInclusive ?
						CompareLessEqualMatch(element, &elementState.traverseState, false)
						: CompareLessMatch(element, &elementState.traverseState, false);

	rangeState->isMinConditionSet = rangeState->isMinConditionSet || minCondition;
	rangeState->isMaxConditionSet = rangeState->isMaxConditionSet || maxCondition;

	if (rangeState->isMinConditionSet && rangeState->isMaxConditionSet)
	{
		rangeState->elementState.traverseState.compareResult =
			CompareResult_Match;
		return false;
	}
	else
	{
		rangeState->elementState.traverseState.compareResult =
			CompareResult_Mismatch;
		return true;
	}
}
