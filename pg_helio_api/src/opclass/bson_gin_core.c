/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/bson_gin_core.c
 *
 * Gin operator implementations of BSON.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <access/reloptions.h>
#include <executor/executor.h>
#include <utils/builtins.h>
#include <utils/typcache.h>
#include <utils/lsyscache.h>
#include <utils/syscache.h>
#include <utils/timestamp.h>
#include <utils/array.h>
#include <parser/parse_coerce.h>
#include <catalog/pg_type.h>
#include <funcapi.h>
#include <lib/stringinfo.h>

#include "opclass/helio_gin_common.h"
#include "opclass/helio_bson_gin_private.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "opclass/helio_gin_index_term.h"
#include "io/helio_bson_core.h"
#include "aggregation/bson_query_common.h"
#include "query/helio_bson_compare.h"
#include "query/bson_dollar_operators.h"
#include "query/query_operator.h"
#include "utils/mongo_errors.h"
#include <math.h>

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */


/*
 * Wrapper around the Extra_data structure for $all.
 * Holds an 'either' of expression, or $regex
 */
typedef struct
{
	union
	{
		/*
		 * Shared state for the expression evaluation engine across terms
		 */
		BsonElemMatchIndexExprState exprEvalState;

		/* The regex data associated with this term (if applicable) */
		RegexData *regexData;
	};

	/*
	 * Whether or not this extra_data term is associated with the expression evaluation.
	 * If not, this is a regex term.
	 */
	bool isExprEvalState;
} BsonDollarAllIndexState;

typedef struct DollarRangeValues
{
	DollarRangeParams params;
	bool isArrayTerm;
	bytea *maxValueIndexTerm;
	bytea *minValueIndexTerm;
} DollarRangeValues;


typedef struct DollarArrayOpQueryData
{
	/* Whether the $in/$nin/etc array has nulls */
	bool arrayHasNull;

	/* Whether the $in/$nin/etc array has regexes */
	bool arrayHasRegex;

	/* Whether the array has an elemMatch */
	bool arrayHasElemMatch;

	/* Whether any of the terms are truncated */
	bool arrayHasTruncation;

	/* The count of terms $in/$nin/etc term */
	int inTermCount;

	/* The index of the queryKey of the root term */
	int rootTermIndex;

	/* The index of the queryKey of the root truncated term */
	int rootTruncatedTermIndex;
} DollarArrayOpQueryData;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */
static void GenerateTermsCore(bson_iter_t *bsonIter, const char *basePath,
							  uint32_t basePathLen, bool traverseArrays,
							  bool isArrayTerm, GenerateTermsContext *context,
							  bool isCheckForArrayTermsWithNestedDocument);
static Datum GeneratePathUndefinedTerm(void *options);
static Datum * GinBsonExtractQueryEqual(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryIn(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryNotEqual(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryNotIn(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryRegex(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryMod(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryExists(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQuerySize(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryDollarType(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryDollarAll(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryDollarBitWiseOperators(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryDollarRange(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryGreater(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryGreaterEqual(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryLess(BsonExtractQueryArgs *args);
static Datum * GinBsonExtractQueryLessEqual(BsonExtractQueryArgs *args);

static int32_t GinBsonComparePartialGreater(BsonIndexTerm *queryValue,
											BsonIndexTerm *compareValue);
static int32_t GinBsonComparePartialLess(BsonIndexTerm *queryValue,
										 BsonIndexTerm *compareValue,
										 bytea *maxValue);
static int32_t GinBsonComparePartialDollarRange(DollarRangeValues *rangeValues,
												BsonIndexTerm *queryValue,
												BsonIndexTerm *compareValue);
static int32_t GinBsonComparePartialExists(BsonIndexTerm *queryValue,
										   BsonIndexTerm *compareValue);
static int32_t GinBsonComparePartialSize(BsonIndexTerm *queryValue,
										 BsonIndexTerm *compareValue,
										 int64_t size);
static int32_t GinBsonComparePartialType(BsonIndexTerm *queryValue,
										 BsonIndexTerm *compareValue,
										 Datum *typeArray);
static int32_t GinBsonComparePartialRegex(BsonIndexTerm *queryValue,
										  BsonIndexTerm *compareValue,
										  RegexData *regexData);
static int32_t GinBsonComparePartialMod(BsonIndexTerm *queryValue,
										BsonIndexTerm *compareValue,
										pgbsonelement *modArray);
static int32_t GinBsonComparePartialBitsWiseOperator(BsonIndexTerm *queryValue,
													 BsonIndexTerm *compareValue,
													 pgbsonelement *filterSetPositionArray,
													 CompareArrayForBitwiseOp
													 bitsCompareFunc);
static void ProcessExtractQueryForRegex(pgbsonelement *element,
										bool *partialmatch, Pointer *extra_data);
static Datum GenerateEmptyArrayTerm(pgbsonelement *filterElement);
static inline Datum * GenerateNullEqualityIndexTerms(int32 *nentries, bool **partialmatch,
													 pgbsonelement *filterElement);

inline static bool
HandleConsistentEqualsNull(bool *check, bool *recheck)
{
	/* we're in the $eq: null / $gte : null/ $lte: null case. */
	/* result is true, if the value is null. */
	if (check[0] || check[1])
	{
		*recheck = false;
	}
	else
	{
		/* if the value isn't null */
		/* we can't be sure this path is a match. */
		/* set recheck to true to validate it in the runtime. */
		*recheck = true;
	}

	return true;
}


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/*
 * Core logic wrapper for generating terms for a single index path.
 * See gin_bson_single_path_extract_value for more details
 */
void
GenerateSinglePathTermsCore(pgbson *bson, GenerateTermsContext *context,
							BsonGinSinglePathOptions *singlePathOptions)
{
	context->options = (void *) singlePathOptions;
	context->traverseOptionsFunc = &GetSinglePathIndexTraverseOption;
	context->generateNotFoundTerm = singlePathOptions->generateNotFoundTerm;
	context->indexTermSizeLimit = GetIndexTermSizeLimit(singlePathOptions);

	bool generateRootTerm = true;
	GenerateTerms(bson, context, generateRootTerm);
}


/*
 * Core logic wrapper for generating terms for a single index path.
 * See gin_bson_wildcard_project_extract_value for more details
 */
void
GenerateWildcardPathTermsCore(pgbson *bson, GenerateTermsContext *context,
							  BsonGinWildcardProjectionPathOptions *wildcardOptions)
{
	context->options = (void *) wildcardOptions;
	context->traverseOptionsFunc = &GetWildcardProjectionPathIndexTraverseOption;

	/* Wildcard indexes always do not generate the not-found term */
	context->generateNotFoundTerm = false;
	context->indexTermSizeLimit = GetIndexTermSizeLimit(wildcardOptions);

	bool generateRootTerm = true;
	GenerateTerms(bson, context, generateRootTerm);
}


bool
GinBsonConsistentCore(BsonIndexStrategy strategy,
					  bool *check,
					  Pointer *extra_data,
					  int32_t numKeys,
					  bool *recheck,
					  Datum *queryKeys)
{
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
		{
			if (check[0])
			{
				/* $gt from the index can be fully trusted for hits */
				*recheck = false;
				return check[0];
			}

			bool isTermTruncated = IsSerializedIndexTermTruncated(DatumGetByteaP(
																	  queryKeys[0]));

			if (!isTermTruncated)
			{
				/* The index can be fully trusted for scenarios where there's no truncation */
				*recheck = false;
				return check[0];
			}

			if (numKeys != 2)
			{
				ereport(PANIC, (errmsg(
									"Unexpected - NumTerms for truncated queries should be 2")));
			}

			/* Otherwise, we need to validate the equality case. */
			if (check[1])
			{
				/* Equality can't be exact since we know it's truncated */
				*recheck = true;
				return check[1];
			}

			*recheck = false;
			return false;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS:
		{
			if (numKeys == 1)
			{
				/* No truncation we trust the index */
				*recheck = false;
			}
			else
			{
				/* Truncation happened, we can trust if not $eq based on truncation */
				*recheck = check[1];
			}

			return check[0];
		}

		case BSON_INDEX_STRATEGY_DOLLAR_MOD:
		{
			/* for $mod after the comparePartial call, */
			/* we will exactly know what matched, so we also don't need */
			/* recheck to be true. We can simply trust the checked value. */
			*recheck = false;
			return check[0];
		}

		case BSON_INDEX_STRATEGY_DOLLAR_RANGE:
		{
			*recheck = false;
			if (numKeys == 2)
			{
				/* no truncation */
				return check[0] || check[1];
			}

			if (check[1])
			{
				/* Array match - can trust the index since there's no array truncation */
				return true;
			}

			if (check[0])
			{
				/* In the raw match, it's an exact match only if it is not at the bounds */
				*recheck = check[2] || check[3];
				return true;
			}

			return false;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_REGEX:
		{
			/* $regex has 2 possible terms - a string or a string and a regex.
			 * Validate each scenario for the consistent check */
			*recheck = false;
			if (check[1])
			{
				/* Truncated string - thunk to runtime */
				*recheck = true;
				return true;
			}
			else if (numKeys == 2)
			{
				return check[0];
			}
			else
			{
				return check[0] || check[2];
			}
		}

		case BSON_INDEX_STRATEGY_DOLLAR_SIZE:
		case BSON_INDEX_STRATEGY_DOLLAR_TYPE:
		{
			/* For $type we can't be sure if the index matched the type
			 * exactly since we treat sort order types as equivalent.
			 * Revalidate this in the runtime
			 * For $size, size does not recurse into nested arrays, and so
			 * If we matched on a nested array term, we have to revalidate
			 * this on the runtime.
			 * For $size as well, truncation is not handled fully in the index.
			 * We need the runtime to revalidate.
			 */
			*recheck = true;
			return check[0];
		}

		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		{
			if (numKeys == 1)
			{
				/* for $eq since we go for an exact match, */
				/* we can trust the check - we don't need to look up the doc. */
				/* Unless the term is truncated */
				*recheck = IsSerializedIndexTermTruncated(DatumGetByteaP(queryKeys[0]));
				return check[0];
			}
			else
			{
				return HandleConsistentEqualsNull(check, recheck);
			}
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
		case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
		{
			BsonIndexTerm indexTerm;
			InitializeBsonIndexTerm(DatumGetByteaP(queryKeys[0]), &indexTerm);

			if (indexTerm.element.bsonValue.value_type == BSON_TYPE_NULL)
			{
				/* $gte: null path */
				return HandleConsistentEqualsNull(check, recheck);
			}

			/* $gte is true if it's $gt/$lt (the 0th term ) */

			/* Recheck is always false if the term is strictly greater than
			 * For equality, it's false unless it's truncated.
			 */
			BsonIndexTerm equalityTerm;
			InitializeBsonIndexTerm(DatumGetByteaP(queryKeys[1]), &equalityTerm);
			*recheck = !check[0] && equalityTerm.isIndexTermTruncated;

			/* A row matches if it's $gt/$lt OR $eq */
			return check[0] || check[1];
		}

		case BSON_INDEX_STRATEGY_DOLLAR_EXISTS:
		{
			*recheck = false;
			if (numKeys == 1)
			{
				/* positive exists case. */
				return check[0];
			}
			else
			{
				/* $exists: 0 case */
				Assert(numKeys == 2);
				return !check[0] && check[1];
			}
		}

		case BSON_INDEX_STRATEGY_UNIQUE_EQUAL:
		{
			bool res = false;
			int endIndex = numKeys;
			for (int i = 0; i < endIndex && !res; i++)
			{
				res = res || check[i];
			}

			*recheck = false;

			return res;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH:
		{
			*recheck = false;
			bool res = GinBsonElemMatchConsistent(check, extra_data, numKeys);
			if (res)
			{
				/* We need to recheck elemMatch since we can have
				 * false positives due to array of arrays, and secondly
				 * due to truncation of terms.
				 */
				*recheck = true;
			}

			return res;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		{
			/* The last slot (that is, if n is the number of entries, then
			 * (n+1)th slot) will be storing the pointer to the info whether
			 * the $in array has null or not */
			DollarArrayOpQueryData *queryData =
				(DollarArrayOpQueryData *) (extra_data[numKeys]);

			*recheck = false;
			bool result = false;
			for (int i = 0; i < queryData->inTermCount; i++)
			{
				if (check[i])
				{
					result = true;
					break;
				}
			}

			/* Now check scenarios: */
			if (queryData->arrayHasNull && !result)
			{
				/* if no item matched, the document could still match for */
				/* $eq/$in null. Handle this in the runtime. */
				result = true;
				*recheck = true;
			}

			if (queryData->arrayHasRegex &&
				queryData->rootTruncatedTermIndex > 0 &&
				check[queryData->rootTruncatedTermIndex])
			{
				/* Truncated terms just thunk to runtime */
				*recheck = true;
				result = true;
			}

			if (queryData->arrayHasTruncation &&
				queryData->rootTruncatedTermIndex > 0)
			{
				*recheck = check[queryData->rootTruncatedTermIndex];
			}

			return result;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL:
		{
			/* if it matched the root term, */
			/* and did not match the equality */
			/* it is a match for $ne */

			/* There's 2 terms - 0: RootTerm, 1: The $eq term */
			BsonIndexTerm indexTerm;
			InitializeBsonIndexTerm(DatumGetByteaP(queryKeys[1]), &indexTerm);

			if (indexTerm.element.bsonValue.value_type == BSON_TYPE_NULL)
			{
				/* this is the $ne null case. */
				if (check[1])
				{
					*recheck = false;
					return false;
				}
				else
				{
					/* if the field value is not null, we can't be sure */
					/* from the index whether it's a match or not. */
					/* pass it to the runtime to evaluate. */
					*recheck = true;
					return true;
				}
			}
			else
			{
				if (indexTerm.isIndexTermTruncated)
				{
					/* If the $ne is on a truncated term, we can't be totally
					 * sure.
					 */
					*recheck = true;
					return true;
				}

				*recheck = false;
				return check[0] && !check[1];
			}
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_IN:
		{
			/* The last slot (that is, if n is the number of entries, then
			 * (n+1)th slot) will be storing the pointer to the info whether
			 * the $nin array has null or not */
			DollarArrayOpQueryData *queryData =
				(DollarArrayOpQueryData *) extra_data[numKeys];
			bool res = check[queryData->rootTermIndex];

			bool mismatchOnTruncatedTerm = false;
			for (int i = 0; i < queryData->inTermCount; i++)
			{
				/* if any terms match, res is false. */
				if (check[i])
				{
					if (IsSerializedIndexTermTruncated(DatumGetByteaP(queryKeys[i])))
					{
						mismatchOnTruncatedTerm = true;
					}

					res = false;
					break;
				}
			}

			if (res && queryData->arrayHasNull)
			{
				/* none of the terms matched */
				res = true;
				*recheck = true;
			}
			else if (queryData->arrayHasRegex && queryData->rootTruncatedTermIndex > 0 &&
					 check[queryData->rootTruncatedTermIndex])
			{
				/* Regex on truncated - thunk to runtime */
				res = true;
				*recheck = true;
			}
			else if (res && queryData->arrayHasTruncation)
			{
				res = true;
				*recheck = true;
			}
			else if (!res && mismatchOnTruncatedTerm)
			{
				res = true;
				*recheck = true;
			}
			else
			{
				*recheck = false;
			}

			return res;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ALL:
		{
			/* The last slot (that is, if n is the number of entries, then
			 * (n+1)th slot) will be storing the pointer to the info whether
			 * the $all array has only null or not */
			DollarArrayOpQueryData *queryData =
				(DollarArrayOpQueryData *) extra_data[numKeys];
			bool hasOnlyNull = queryData->arrayHasNull;

			bool res = numKeys > 0;
			*recheck = false;

			/* if array has only null the last term is the root term. */
			for (int i = 0; i < queryData->inTermCount; i++)
			{
				res = res && check[i];
			}

			if (!res && hasOnlyNull)
			{
				/* if we get $all: [null] or [null,..<null>, null] as the query,
				 * we can match empty documents, so we need to recheck at runtime. */
				res = true;
				*recheck = true;
			}

			if (queryData->rootTruncatedTermIndex > 0)
			{
				bool isTruncated = check[queryData->rootTruncatedTermIndex];
				if (queryData->arrayHasRegex && isTruncated)
				{
					/* If a regex is requested */
					res = true;
					*recheck = true;
				}
				else if (queryData->arrayHasElemMatch && isTruncated)
				{
					/* If an $elemMatch is requested */
					res = true;
					*recheck = true;
				}
				else if (isTruncated)
				{
					*recheck = true;
				}
			}

			return res;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_CLEAR:
		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_CLEAR:
		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_SET:
		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_SET:
		{
			*recheck = check[2];
			return check[0] || check[1];
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_CHECK_VIOLATION), errmsg(
								"gin_bson_consistent: Unsupported strategy")));
		}
	}
}


/*
 * GinBsonExtractQueryUniqueIndexTerms runs the ExtractQuery phase of
 * The index lookup for a unique index equality operator.
 * This essentially looks up the index options and generates all the possible
 * terms given the index path specification.
 */
Datum *
GinBsonExtractQueryUniqueIndexTerms(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	GenerateTermsContext context = { 0 };
	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	BsonGinSinglePathOptions *options =
		(BsonGinSinglePathOptions *) PG_GET_OPCLASS_OPTIONS();
	context.options = (void *) options;
	context.traverseOptionsFunc = &GetSinglePathIndexTraverseOption;
	context.generateNotFoundTerm = options->generateNotFoundTerm;
	context.indexTermSizeLimit = GetIndexTermSizeLimit(options);

	bool addRootTerm = false;
	GenerateTerms(query, &context, addRootTerm);
	*nentries = context.totalTermCount;
	return context.terms.entries;
}


/*
 * Used as the term generation of the actual order by.
 * This matches all terms for that path. The root term
 * is also included to ensure we capture documents
 * that don't have the path in them.
 */
Datum *
GinBsonExtractQueryOrderBy(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	bool **partialMatch = (bool **) PG_GETARG_POINTER(3);

	Datum *entries;
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(query, &filterElement);

	filterElement.bsonValue.value_type = BSON_TYPE_MINKEY;

	*nentries = 2;

	int32_t sizeLimitUnused = 0;
	entries = (Datum *) palloc(sizeof(Datum) * 2);
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&filterElement,
														sizeLimitUnused).indexTermVal);
	entries[1] = PointerGetDatum(GenerateRootTerm());

	*partialMatch = palloc0(sizeof(bool) * 2);
	(*partialMatch)[0] = true;
	(*partialMatch)[1] = false;
	return entries;
}


int32_t
GinBsonComparePartialOrderBy(BsonIndexTerm *queryValue,
							 BsonIndexTerm *compareValue)
{
	int cmp = strcmp(queryValue->element.path, compareValue->element.path);

	/* Entry has the same path - it matches the ORDER BY */
	if (cmp == 0)
	{
		return 0;
	}

	/* A different path reached - bail */
	return 1;
}


/* --------------------------------------------------------- */
/* Private helper methods */
/* --------------------------------------------------------- */


/*
 * Validates that terms array has at least 'required' Datum entries
 * in its array of terms.
 */
inline static void
EnsureTermCapacity(GenerateTermsContext *context, int32_t required)
{
	int32_t requiredTotal = context->index + required;
	if (context->terms.entryCapacity < requiredTotal)
	{
		context->terms.entries = repalloc(context->terms.entries, sizeof(Datum) *
										  requiredTotal);
		context->terms.entryCapacity = requiredTotal;
	}
}


/*
 * Adds a term to the entry set and ensures there's
 * sufficient capacity to add the term in.
 */
inline static void
AddTerm(GenerateTermsContext *context, Datum term)
{
	EnsureTermCapacity(context, 1);
	context->terms.entries[context->index] = term;
	context->index++;
}


/*
 * This is the core logic that walks through a document and generates terms.
 * The generation happens in 2 phases: first to compute the total number of terms
 * and space to allocate, and the second to walk the structure and actually generate
 * the terms necessary.
 */
void
GenerateTerms(pgbson *bson, GenerateTermsContext *context, bool addRootTerm)
{
	bson_iter_t bsonIterator;

	/* now walk the entries and insert the terms */
	PgbsonInitIterator(bson, &bsonIterator);

	/* Initialize with minimum capacity */
	/* provision 1 root term + 1 value term/not-found term */
	context->terms.entries = palloc0(sizeof(Datum) * 2);
	context->terms.entryCapacity = 2;
	context->hasTruncatedTerms = false;

	if (addRootTerm)
	{
		/* all documents get the root term. */
		AddTerm(context, GenerateRootTerm());
	}

	int32_t initialIndex = context->index;

	bool inArrayContext = false;
	bool isArrayTerm = false;
	bool isCheckForArrayTermsWithNestedDocument = false;
	GenerateTermsCore(&bsonIterator, "", 0, inArrayContext,
					  isArrayTerm, context,
					  isCheckForArrayTermsWithNestedDocument);

	/* Add the path not found term if necessary */
	if (context->index == initialIndex && context->generateNotFoundTerm)
	{
		AddTerm(context, GeneratePathUndefinedTerm(context->options));
	}

	if (context->hasTruncatedTerms)
	{
		AddTerm(context, GenerateRootTruncatedTerm());
	}

	context->totalTermCount = context->index;
}


/*
 * GenerateTerms walks the bson document and for each path creates the term that will be
 * stored in the index for that term. Each term is of the form '{path}{typeCode}{value}'
 * i.e. a BsonElement.
 * Paths are represented as dot notation paths to facilitate faster query match behavior.
 * For objects, it recurses and generates nested terms as well.
 * For arrays, it recurses and generates both types of terms needed to resolve index lookup
 * and non indexed lookup.
 * if countTerms is true, then this method does not allocate any terms and instead updates
 * totalTermCount in the GenerateTermsContext.
 * isCheckForArrayTermsWithNestedDocument will be set to true to check that iterator holds document
 * if it holds document then only we will be generating path terms, It is set to true when array of array has document inside it then only we will be generating terms.
 * e.g, a: [[10,{"b":1}]] . we will be generating term a.0.b : 1 but not a.0 : 10 beacuse first value in array is an integer and second one is document.
 */
static void
GenerateTermsCore(bson_iter_t *bsonIter, const char *basePath,
				  uint32_t basePathLength, bool inArrayContext,
				  bool isArrayTerm, GenerateTermsContext *context,
				  bool isCheckForArrayTermsWithNestedDocument)
{
	char *pathBuilderBuffer = NULL;
	uint32_t pathBuilderBufferLength = 0;
	check_stack_depth();
	CHECK_FOR_INTERRUPTS();
	while (bson_iter_next(bsonIter))
	{
		const char *pathToInsert;
		uint32_t pathtoInsertLength;

		/* if array of array has not document inside it , we will not be generating parent path term */
		if (isCheckForArrayTermsWithNestedDocument && !BSON_ITER_HOLDS_DOCUMENT(bsonIter))
		{
			continue;
		}

		if (isArrayTerm)
		{
			/* if isArrayTerm is true (because we're inside an array context and we're generating */
			/* and we're building the non array-index based terms, then just use the base path) */
			/* this is because mongo can filter on array entries based on the array index (a.b.0 / a.b.1) */
			/* or simply on the array path itself (a.b) */
			pathToInsert = basePath;
			pathtoInsertLength = basePathLength;
		}
		else if (basePathLength == 0)
		{
			/* if we're at the root, simply use the field path. */
			pathToInsert = bson_iter_key(bsonIter);
			pathtoInsertLength = bson_iter_key_len(bsonIter);
		}
		else
		{
			/* otherwise build the path to insert. We use 'base.field' for the path */
			/* since dot paths are illegal in mongo for field names. */
			uint32_t fieldPathLength = bson_iter_key_len(bsonIter);
			uint32_t pathToInsertAllocLength;

			/* the length includes the two fields + the extra dot. */
			pathtoInsertLength = fieldPathLength + basePathLength + 1;

			/* need one more character for the \0 */
			pathToInsertAllocLength = pathtoInsertLength + 1;

			if (pathBuilderBufferLength == 0)
			{
				/* if there's no temp buffer create one */
				pathBuilderBuffer = (char *) palloc(pathToInsertAllocLength);
				pathBuilderBufferLength = pathToInsertAllocLength;
			}
			else if (pathBuilderBufferLength < pathToInsertAllocLength)
			{
				/* if there's not enough space in the temp buffer, realloc to ensure enough length. */
				pathBuilderBuffer = (char *) repalloc(pathBuilderBuffer,
													  pathToInsertAllocLength);
				pathBuilderBufferLength = pathToInsertAllocLength;
			}

			/* construct <basePath>.<current key> string */
			memcpy(pathBuilderBuffer, basePath, basePathLength);
			pathBuilderBuffer[basePathLength] = '.';
			memcpy(&pathBuilderBuffer[basePathLength + 1], bson_iter_key(bsonIter),
				   fieldPathLength);
			pathBuilderBuffer[pathtoInsertLength] = 0;

			pathToInsert = pathBuilderBuffer;
		}

		/* query whether or not to index the specific path given the options. */
		IndexTraverseOption option = context->traverseOptionsFunc(context->options,
																  pathToInsert,
																  pathtoInsertLength,
																  bson_iter_type(
																	  bsonIter));
		switch (option)
		{
			case IndexTraverse_Invalid:
			{
				/* path is invalid (e.g. index is for 'a.b' inclusive, and the current path is 'c') */
				continue;
			}

			case IndexTraverse_Recurse:
			{
				/* path is invalid, but may have valid descendants (e.g. index is for 'a.b.c' inclusive, */
				/* and the current path is 'a.b') */
				break;
			}

			case IndexTraverse_Match:
			{
				/*
				 * if array of array has document inside it, we will be iterating document and generating parent path term
				 * not the term directly with value of document.
				 * e.g, a:[[{b:1}]] -> we will be generating a.0.b : 1 but not generating a.0 : {b:1}
				 */
				if (isCheckForArrayTermsWithNestedDocument)
				{
					break;
				}

				/* path is valid and a match */
				bson_type_t type = bson_iter_type(bsonIter);

				/* Construct the { <path> : <typecode> <value> } BSON and add it to index entries */
				pgbsonelement element = { 0 };
				element.path = pathToInsert;
				element.pathLength = pathtoInsertLength;
				element.bsonValue = *bson_iter_value(bsonIter);
				BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(
					&element, context->indexTermSizeLimit);
				AddTerm(context, PointerGetDatum(
							serializedTerm.indexTermVal));
				if (serializedTerm.isIndexTermTruncated)
				{
					context->hasTruncatedTerms = true;
				}

				if (context->generateNotFoundTerm &&
					(type == BSON_TYPE_NULL || type == BSON_TYPE_UNDEFINED))
				{
					/* In this case, we also generate the undefined term */
					AddTerm(context, GeneratePathUndefinedTerm(
								context->options));
				}

				break;
			}

			default:
			{
				ereport(ERROR, (errmsg("Unknown prefix match on index %d", option)));
				break;
			}
		}

		if (BSON_ITER_HOLDS_DOCUMENT(bsonIter))
		{
			bson_iter_t containerIter;
			if (bson_iter_recurse(bsonIter, &containerIter))
			{
				bool inArrayContextInner = false;
				bool isArrayTermInner = false;
				bool isCheckForArrayTermsWithNestedDocument = false;
				GenerateTermsCore(&containerIter, pathToInsert, pathtoInsertLength,
								  inArrayContextInner, isArrayTermInner,
								  context, isCheckForArrayTermsWithNestedDocument);
			}
		}

		/* if we're already recursing because of an array's parent path term, */
		/* then we don't recurse into nested arrays. */
		/* if we're not in that case, we recurse down to produce inner terms of arrays. */
		if (!isArrayTerm)
		{
			if (BSON_ITER_HOLDS_ARRAY(bsonIter))
			{
				bson_iter_t containerIter;

				/* Count the array terms - to pre-allocate the term Datum pointers */
				int32_t arrayCount = BsonDocumentValueCountKeys(bson_iter_value(
																	bsonIter));
				EnsureTermCapacity(context, arrayCount);

				if (bson_iter_recurse(bsonIter, &containerIter))
				{
					bool inArrayContextInner = true;
					bool isArrayTermInner = false;
					bool isCheckForArrayTermsWithNestedDocument = false;
					GenerateTermsCore(&containerIter, pathToInsert, pathtoInsertLength,
									  inArrayContextInner, isArrayTermInner,
									  context, isCheckForArrayTermsWithNestedDocument);
				}

				/*
				 * for array of arrays, having document inside it. We will generate parent path terms *
				 * if below recursive call get triggerd with isCheckForArrayTermsWithNestedDocument set to true then we will check that
				 * array of array has document inside it , for document we will generate parent path term.
				 *
				 * e.g, a: [[{b: 10}]]  => is suppose to genrate a.0.b :10 as one of the path term because array of array has document inside it.
				 */
				if (bson_iter_recurse(bsonIter, &containerIter))
				{
					bool inArrayContextInner = true;
					bool isArrayTermInner = true;
					bool isCheckForArrayTermsWithNestedDocument = inArrayContext;
					GenerateTermsCore(&containerIter, pathToInsert, pathtoInsertLength,
									  inArrayContextInner, isArrayTermInner,
									  context, isCheckForArrayTermsWithNestedDocument);
				}
			}
		}
	}

	if (pathBuilderBuffer != NULL)
	{
		pfree(pathBuilderBuffer);
	}
}


/*
 * The core dispatch function for gin_bson_extract_query.
 * Based on the indexing strategy, calls the appropriate
 * extract query function for the index and returns the output.
 */
Datum *
GinBsonExtractQueryCore(BsonIndexStrategy strategy, BsonExtractQueryArgs *args)
{
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		{
			return GinBsonExtractQueryEqual(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
		{
			return GinBsonExtractQueryGreater(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
		{
			return GinBsonExtractQueryGreaterEqual(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS:
		{
			return GinBsonExtractQueryLess(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
		{
			return GinBsonExtractQueryLessEqual(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		{
			return GinBsonExtractQueryIn(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_EQUAL:
		{
			return GinBsonExtractQueryNotEqual(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_IN:
		{
			return GinBsonExtractQueryNotIn(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_REGEX:
		{
			return GinBsonExtractQueryRegex(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_MOD:
		{
			return GinBsonExtractQueryMod(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_EXISTS:
		{
			return GinBsonExtractQueryExists(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_SIZE:
		{
			return GinBsonExtractQuerySize(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_TYPE:
		{
			return GinBsonExtractQueryDollarType(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH:
		{
			return GinBsonExtractQueryElemMatch(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ALL:
		{
			return GinBsonExtractQueryDollarAll(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_CLEAR:
		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_CLEAR:
		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_SET:
		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_SET:
		{
			return GinBsonExtractQueryDollarBitWiseOperators(args);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_RANGE:
		{
			return GinBsonExtractQueryDollarRange(args);
		}

		case BSON_INDEX_STRATEGY_UNIQUE_EQUAL:
		default:
		{
			ereport(ERROR, (errcode(ERRCODE_CHECK_VIOLATION), errmsg(
								"gin_extract_query_bson: Unsupported strategy %d",
								strategy)));
		}
	}
}


/*
 * $eq/$gte/$lte on null means value that (equal/ greater or equal/ less or equal) null
 * or where the field doesn't have the value
 * or value is undefined
 *
 * Sets the respective partial matches to false
 *
 * Note: Make sure to at least palloc array to hold 3 elements
 */
static inline Datum *
GenerateNullEqualityIndexTerms(int32 *nentries, bool **partialmatch,
							   pgbsonelement *filterElement)
{
	*nentries = 3;
	*partialmatch = (bool *) palloc(sizeof(bool) * 3);
	Datum *entries = (Datum *) palloc(sizeof(Datum) * 3);

	/* size limit is not needed for NULL/UNDEFINED terms */
	int32_t sizeLimitUnused = 0;

	/* the first term generated is the value itself (null) */
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(filterElement,
														sizeLimitUnused).indexTermVal);
	(*partialmatch)[0] = false;

	/* The next term is undefined */
	filterElement->bsonValue.value_type = BSON_TYPE_UNDEFINED;
	entries[1] = PointerGetDatum(SerializeBsonIndexTerm(filterElement,
														sizeLimitUnused).indexTermVal);
	(*partialmatch)[1] = false;

	/* the next term generated is the root term */
	entries[2] = GenerateRootTerm();
	(*partialmatch)[2] = false;

	return entries;
}


/*
 * GinBsonExtractQueryEqual generates the index term needed to match
 * equality on a dotted path and value.
 * In this case, we can simply return the term as the value stored
 * in the index will be exactly the value being queried.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryEqual(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Datum *entries;

	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(query, &filterElement);

	if (filterElement.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* special case for $eq on null. */
		entries = GenerateNullEqualityIndexTerms(nentries, partialmatch, &filterElement);
	}
	else
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&filterElement,
															args->indexTermSizeLimit).
									 indexTermVal);
	}

	return entries;
}


/*
 * GinBsonExtractQueryDollarBitWiseOperators generates the index term needed to match
 * equality on a dotted path and value for all bitwise operators e.g., $bitsAllClear, $bitsAnyClear
 * We are generating two terms one for minimum value of numbers and other for minimum value of
 * binary because matching document can be type of number or binary.
 */
static Datum *
GinBsonExtractQueryDollarBitWiseOperators(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	pgbsonelement filterElement;
	Datum *entries;

	*partialmatch = (bool *) palloc(sizeof(bool) * 3);
	*extra_data = (Pointer *) palloc(sizeof(Pointer) * 3);

	/* add query address to extra_data for all entries so that we can use positional array from query during partial comparison*/
	(*extra_data)[0] = (Pointer) query;
	(*extra_data)[1] = (Pointer) query;

	/* Enable partial match for all entries*/
	(*partialmatch)[0] = true;
	(*partialmatch)[1] = true;
	(*partialmatch)[2] = false;


	PgbsonToSinglePgbsonElement(query, &filterElement);

	/* lowest possible values can be lowest of type double or binary */
	/* we are assuming (decimal128(nan) == double(nan)) < all possible numbers */
	pgbsonelement numberMinElement = filterElement;
	numberMinElement.bsonValue.value_type = BSON_TYPE_DOUBLE;
	numberMinElement.bsonValue.value.v_double = (double) NAN;

	*nentries = 3;
	entries = (Datum *) palloc(sizeof(Datum) * 3);

	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&numberMinElement,
														args->indexTermSizeLimit).
								 indexTermVal);

	pgbsonelement binaryMinElement = filterElement;
	binaryMinElement.bsonValue.value_type = BSON_TYPE_BINARY;
	binaryMinElement.bsonValue.value.v_binary.subtype = BSON_SUBTYPE_BINARY;
	binaryMinElement.bsonValue.value.v_binary.data_len = 0;
	entries[1] = PointerGetDatum(SerializeBsonIndexTerm(&binaryMinElement,
														args->indexTermSizeLimit).
								 indexTermVal);

	entries[2] = GenerateRootTruncatedTerm();

	return entries;
}


static Datum *
GinBsonExtractQueryDollarRange(BsonExtractQueryArgs *args)
{
	/* query: document @<> { "path": [ "min": MINVALUE, "max": MAXVALUE ] } */
	pgbson *filter = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;

	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(filter, &filterElement);
	DollarRangeValues *rangeValues = palloc0(sizeof(DollarRangeValues));

	DollarRangeParams *params = ParseQueryDollarRange(filter);
	rangeValues->params = *params;

	pgbsonelement maxElement =
	{
		.path = filterElement.path,
		.pathLength = filterElement.pathLength,
		.bsonValue = rangeValues->params.maxValue
	};
	BsonIndexTermSerialized maxSerialized = SerializeBsonIndexTerm(&maxElement,
																   args->
																   indexTermSizeLimit);
	rangeValues->maxValueIndexTerm = maxSerialized.indexTermVal;

	pgbsonelement minElement =
	{
		.path = filterElement.path,
		.pathLength = filterElement.pathLength,
		.bsonValue = rangeValues->params.minValue
	};
	BsonIndexTermSerialized minSerialized = SerializeBsonIndexTerm(&minElement,
																   args->
																   indexTermSizeLimit);
	rangeValues->minValueIndexTerm = minSerialized.indexTermVal;

	*nentries = 2;
	*partialmatch = (bool *) palloc(sizeof(bool) * 4);
	*extra_data = (Pointer *) palloc(sizeof(Pointer) * 4);
	Pointer *extraDataArray = *extra_data;

	Datum *entries = (Datum *) palloc(sizeof(Datum) * 4);

	/* The first index term is a range scan from "MINVALUE" to "MAXVALUE"
	 * We use the same ComparePartial for LessThan so we generate the terms identically.
	 */

	/* This handles > minValue && < maxValue */
	DollarRangeValues *rangeValuesCopy = palloc(sizeof(DollarRangeValues));
	*rangeValuesCopy = *rangeValues;
	rangeValuesCopy->isArrayTerm = false;
	entries[0] = PointerGetDatum(minSerialized.indexTermVal);
	extraDataArray[0] = (Pointer) rangeValuesCopy;
	(*partialmatch)[0] = true;

	/* Initialize an empty array for the second term*/
	/* This handles >= minValue && <= maxValue for arrays */
	rangeValues->isArrayTerm = true;

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendEmptyArray(&writer, filterElement.path, filterElement.pathLength);

	pgbson *emptyArrayBson = PgbsonWriterGetPgbson(&writer);

	PgbsonToSinglePgbsonElement(emptyArrayBson, &minElement);
	entries[1] = PointerGetDatum(SerializeBsonIndexTerm(&minElement, 0).indexTermVal);
	(*partialmatch)[1] = true;
	extraDataArray[1] = (Pointer) rangeValues;

	if (minSerialized.isIndexTermTruncated ||
		maxSerialized.isIndexTermTruncated)
	{
		*nentries = 4;

		/* This handles == minValue */
		entries[2] = PointerGetDatum(minSerialized.indexTermVal);
		(*partialmatch)[2] = false;
		extraDataArray[2] = (Pointer) rangeValues;

		/* This handles == maxValue */
		entries[3] = PointerGetDatum(maxSerialized.indexTermVal);
		(*partialmatch)[3] = false;
		extraDataArray[3] = (Pointer) rangeValues;
	}

	return entries;
}


/*
 * GinBsonExtractQueryNotEqual generates the index term needed to match
 * inequality on a dotted path and value.
 * In this case, we return two terms, the root term, and the term itself.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryNotEqual(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	Datum *entries = (Datum *) palloc(sizeof(Datum) * 2);

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(query, &element);
	*nentries = 2;

	BsonIndexTermSerialized serialized = SerializeBsonIndexTerm(&element,
																args->indexTermSizeLimit);
	entries[0] = GenerateRootTerm();
	entries[1] = PointerGetDatum(serialized.indexTermVal);
	return entries;
}


/*
 * GinBsonExtractQueryGreaterEqual generates the index term needed to match
 * $gte on a dotted path and value.
 * In this case, We return a lower bound value of the item being queried.
 * Since we set the PartialMatch value to be true, the value will be considered
 * a lower bound for the indexed search.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryGreaterEqual(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Datum *entries;

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(query, &element);
	if (element.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* special case for $gte: null */
		return GenerateNullEqualityIndexTerms(nentries, partialmatch, &element);
	}

	entries = (Datum *) palloc(sizeof(Datum) * 2);
	*partialmatch = (bool *) palloc(sizeof(bool) * 2);
	*nentries = 2;

	BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																	args->
																	indexTermSizeLimit);
	entries[0] = PointerGetDatum(serializedTerm.indexTermVal);
	(*partialmatch)[0] = true;

	/* Add an equals match for $gte */
	entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
	(*partialmatch)[1] = false;

	return entries;
}


/*
 * GinBsonExtractQueryGreater generates the index term needed to match
 * $gt on a dotted path and value.
 * In this case, We return a lower bound value of the item being queried.
 * Since we set the PartialMatch value to be true, the value will be considered
 * a lower bound for the indexed search.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryGreater(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Datum *entries;

	pgbsonelement element;
	PgbsonToSinglePgbsonElement(query, &element);
	entries = (Datum *) palloc(sizeof(Datum) * 2);
	*partialmatch = (bool *) palloc(sizeof(bool) * 2);
	*nentries = 1;

	BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																	args->
																	indexTermSizeLimit);
	entries[0] = PointerGetDatum(serializedTerm.indexTermVal);
	(*partialmatch)[0] = true;

	if (serializedTerm.isIndexTermTruncated)
	{
		/* Add the truncated term */
		*nentries = 2;
		entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
		(*partialmatch)[1] = false;
	}
	return entries;
}


static Datum *
GinBsonExtractQueryLessEqual(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	Datum *entries;
	pgbsonelement documentElement;
	PgbsonToSinglePgbsonElement(query, &documentElement);

	/* Clone it for now */
	pgbsonelement queryElement = documentElement;

	if (documentElement.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* special case for $lte: null */
		entries = GenerateNullEqualityIndexTerms(nentries, partialmatch,
												 &documentElement);
	}
	else
	{
		entries = (Datum *) palloc(sizeof(Datum) * 2);
		*partialmatch = (bool *) palloc(sizeof(bool) * 2);
		*extra_data = (Pointer *) palloc(sizeof(Pointer) * 2);
		*nentries = 2;
		(*partialmatch)[0] = true;

		/* Also serialize the actual index term */
		BsonIndexTermSerialized serializedTerm =
			SerializeBsonIndexTerm(&queryElement, args->indexTermSizeLimit);

		/* store the query value in extra data - this allows us to compute upper bound using */
		/* this extra value. */
		(*extra_data)[0] = (Pointer) serializedTerm.indexTermVal;

		/* now create a bson for that path which has the min value for the field */
		/* TODO: Today we set this as MinKey but this can be set as the min value for the equivalent */
		/* type (i.e. the lowest possible value for that type OR the highest possible value of the prior type) */
		/* since we don't have the table of these values, we safely start out with Minkey. */
		documentElement.bsonValue.value_type = BSON_TYPE_MINKEY;
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
															args->indexTermSizeLimit).
									 indexTermVal);

		/* In the case where we have truncation, also track equality on $lt */
		(*partialmatch)[1] = false;
		(*extra_data)[1] = NULL;
		entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
	}
	return entries;
}


/*
 * GinBsonExtractQueryLess generates the index term needed to match
 * $lg and $lte on a dotted path and value.
 * In this case, We return a lower bound value of the min value for that path.
 * Since we set the PartialMatch value to be true, the min value will be considered
 * a lower bound for the indexed search.
 * We also pass 'extraData' being the value being queried. This will later be treated
 * as the 'maxValue' -> when to stop enumerating on the index.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryLess(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	Datum *entries;
	pgbsonelement documentElement;
	PgbsonToSinglePgbsonElement(query, &documentElement);

	/* Clone it for now */
	pgbsonelement queryElement = documentElement;

	entries = (Datum *) palloc(sizeof(Datum) * 2);
	*partialmatch = (bool *) palloc(sizeof(bool) * 2);
	*nentries = 1;
	(*partialmatch)[0] = true;

	/* store the query value in extra data - this allows us to compute upper bound using */
	/* this extra value. */

	/* Also serialize the actual index term */
	BsonIndexTermSerialized serializedTerm =
		SerializeBsonIndexTerm(&queryElement, args->indexTermSizeLimit);


	*extra_data = (Pointer *) palloc(sizeof(Pointer) * 2);
	(*extra_data)[0] = (Pointer) serializedTerm.indexTermVal;

	/* now create a bson for that path which has the min value for the field */
	/* TODO: Today we set this as MinKey but this can be set as the min value for the equivalent */
	/* type (i.e. the lowest possible value for that type OR the highest possible value of the prior type) */
	/* since we don't have the table of these values, we safely start out with Minkey. */
	documentElement.bsonValue.value_type = BSON_TYPE_MINKEY;
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
														args->indexTermSizeLimit).
								 indexTermVal);

	/* In the case where we have truncation, also track equality on $lt */
	if (serializedTerm.isIndexTermTruncated)
	{
		*nentries = 2;
		(*partialmatch)[1] = false;
		(*extra_data)[1] = NULL;
		entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
	}

	return entries;
}


/*
 * GinBsonExtractQueryIn generates the index term needed to match
 * equality on an array of dotted path and values.
 * In this case, we can simply return the terms as the value stored
 * in the index will be exactly the value being queried.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryIn(BsonExtractQueryArgs *args)
{
	pgbson *inArray = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	int32_t inArraySize = 0, index = 0;
	Datum *entries;

	pgbsonelement queryElement;
	PgbsonToSinglePgbsonElement(inArray, &queryElement);
	if (queryElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$in should have an array of values")));
	}

	bson_iter_t arrayIter;
	bson_iter_init_from_data(&arrayIter, queryElement.bsonValue.value.v_doc.data,
							 queryElement.bsonValue.value.v_doc.data_len);

	bool arrayHasNull = false;
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIter);

		/* if it is bson document and valid one for $in/$nin array. It fails with exact same error for both $in/$nin. */
		if (!IsValidBsonDocumentForDollarInOrNinOp(arrayValue))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg("cannot nest $ under $in")));
		}

		inArraySize++;
		arrayHasNull = arrayHasNull || arrayValue->value_type == BSON_TYPE_NULL;
	}

	if (arrayHasNull)
	{
		inArraySize++;
	}

	/* allocate an additional one for truncation if necessary */
	inArraySize++;

	DollarArrayOpQueryData *inQueryData = palloc0(sizeof(DollarArrayOpQueryData));
	inQueryData->arrayHasNull = arrayHasNull;
	entries = (Datum *) palloc(sizeof(Datum) * inArraySize);
	bson_iter_init_from_data(&arrayIter, queryElement.bsonValue.value.v_doc.data,
							 queryElement.bsonValue.value.v_doc.data_len);

	/* The last slot is used to store the pointer to the info (bool) whether any
	 * of the array elements inside $in is null or not. Hence a '+ 1' here.
	 * The other slots are pointer to the regex patterns, if they exists, in
	 * in the $in array */
	*extra_data = (Pointer *) palloc0((sizeof(Pointer) * (inArraySize + 1)));
	*partialmatch = (bool *) palloc0(sizeof(bool) * inArraySize);

	bool *partialMatchArray = *partialmatch;
	Pointer *extraDataArray = *extra_data;

	while (bson_iter_next(&arrayIter))
	{
		if (index >= inArraySize)
		{
			ereport(ERROR, (errmsg(
								"Index is not expected to be greater than size - code defect")));
		}

		pgbsonelement element;
		element.path = queryElement.path;
		element.pathLength = queryElement.pathLength;
		element.bsonValue = *bson_iter_value(&arrayIter);

		if (element.bsonValue.value_type == BSON_TYPE_REGEX)
		{
			ProcessExtractQueryForRegex(&element, &partialMatchArray[index],
										&extraDataArray[index]);
			inQueryData->arrayHasRegex = true;
		}

		BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																		args->
																		indexTermSizeLimit);
		entries[index] = PointerGetDatum(serializedTerm.indexTermVal);

		inQueryData->arrayHasTruncation = inQueryData->arrayHasTruncation ||
										  serializedTerm.isIndexTermTruncated;
		index++;
	}

	inQueryData->inTermCount = index;
	if (arrayHasNull)
	{
		entries[index] = GenerateRootTerm();
		inQueryData->rootTermIndex = index;
		index++;
	}

	if (inQueryData->arrayHasRegex ||
		inQueryData->arrayHasTruncation)
	{
		entries[index] = GenerateRootTruncatedTerm();
		inQueryData->rootTruncatedTermIndex = index;
		index++;
	}

	*nentries = index;

	/* The last slot of extra_data (that is, if n is the number of entries,
	 * then (n+1)th slot) will be storing a pointer to the info whether
	 * the $in array has null or not */
	(*extra_data)[index] = (Pointer) palloc(sizeof(Pointer));
	(*extra_data)[index] = (Pointer) inQueryData;

	return entries;
}


/*
 * GinBsonExtractQueryNotIn generates the index term needed to match
 * inequality on an array of dotted path and values.
 * In this case, we can simply return the terms as the value stored
 * in the index will be exactly the value being queried.
 * Additionally, we return the root term as well. This is used in
 * consistent to produce the inverse set of the items in the array.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryNotIn(BsonExtractQueryArgs *args)
{
	pgbson *inArray = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	int32_t termCount, index;
	Datum *entries;

	pgbsonelement queryElement;
	PgbsonToSinglePgbsonElement(inArray, &queryElement);
	if (queryElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$in needs an array")));
	}

	bson_iter_t arrayIter;
	bson_iter_init_from_data(&arrayIter, queryElement.bsonValue.value.v_doc.data,
							 queryElement.bsonValue.value.v_doc.data_len);

	termCount = 0;
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIter);

		/* Make sure there is no $op (except $regex) in the $in/$nin array. It fails with exact same error for both $in/$nin. */
		if (!IsValidBsonDocumentForDollarInOrNinOp(arrayValue))
		{
			ereport(ERROR, (errcode(MongoBadValue), errmsg("cannot nest $ under $in")));
		}
		termCount++;
	}

	/* we generate two more possible terms for the root term. */
	termCount += 2;
	entries = (Datum *) palloc(sizeof(Datum) * termCount);
	index = 0;

	bson_iter_init_from_data(&arrayIter, queryElement.bsonValue.value.v_doc.data,
							 queryElement.bsonValue.value.v_doc.data_len);
	DollarArrayOpQueryData *queryData = palloc0(sizeof(DollarArrayOpQueryData));

	/* The last slot is used to store the pointer to the info (bool) whether any
	 * of the array elements inside $nin is null or not. Hence a '+ 1' here.
	 * The other slots are pointer to the regex patterns, if they exists, in
	 * in the $nin array */
	*extra_data = (Pointer *) palloc0((sizeof(Pointer) * (termCount + 1)));
	*partialmatch = (bool *) palloc0(sizeof(bool) * termCount);
	bool *partialMatchArray = *partialmatch;
	Pointer *extraDataArray = *extra_data;

	while (bson_iter_next(&arrayIter))
	{
		if (index >= termCount)
		{
			ereport(ERROR, (errcode(ERRCODE_CHECK_VIOLATION), errmsg(
								"Index greater than array dimensions - code defect")));
		}

		pgbsonelement element;
		element.path = queryElement.path;
		element.pathLength = queryElement.pathLength;
		element.bsonValue = *bson_iter_value(&arrayIter);

		if (element.bsonValue.value_type == BSON_TYPE_REGEX)
		{
			/* TODO(Truncation) */
			ProcessExtractQueryForRegex(&element, &partialMatchArray[index],
										&extraDataArray[index]);
			queryData->arrayHasRegex = true;
		}

		BsonIndexTermSerialized serialized = SerializeBsonIndexTerm(&element,
																	args->
																	indexTermSizeLimit);
		entries[index] = PointerGetDatum(serialized.indexTermVal);

		queryData->arrayHasNull = queryData->arrayHasNull ||
								  element.bsonValue.value_type == BSON_TYPE_NULL;
		queryData->arrayHasTruncation = serialized.isIndexTermTruncated;

		index++;
	}

	queryData->inTermCount = index;

	queryData->rootTermIndex = index;
	entries[index++] = GenerateRootTerm();

	if (queryData->arrayHasTruncation)
	{
		entries[index] = GenerateRootTruncatedTerm();
		queryData->rootTruncatedTermIndex = index;
		index++;
	}

	*nentries = index;

	/* The last slot of extra_data (that is, if n is the number of entries,
	 * then (n+1)th slot) will be storing a pointer to the info whether
	 * the $nin array has null or not */
	(*extra_data)[index] = (Pointer) palloc(sizeof(Pointer));
	(*extra_data)[index] = (Pointer) queryData;

	return entries;
}


/*
 * GinBsonExtractRegex generates the index term needed to match
 * $regex on a dotted path and value.
 * In this case, We return a lower bound value of the min value for that path.
 * Since we set the PartialMatch value to be true, the min value will be considered
 * a lower bound for the indexed search.
 * We also pass 'extraData' being the regex text being queried. This will later be treated
 * as the regex to be evaluated against the index.
 * The query is expected to be of the format {path}{utf8}{regex}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryRegex(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	pgbsonelement queryElement;

	PgbsonToSinglePgbsonElement(query, &queryElement);

	if ((queryElement.bsonValue.value_type != BSON_TYPE_UTF8) &&
		(queryElement.bsonValue.value_type != BSON_TYPE_REGEX))
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$regex has to be a string")));
	}

	int32_t numEntries = 2;
	bool isRegexValue = false;
	if (queryElement.bsonValue.value_type == BSON_TYPE_REGEX)
	{
		numEntries++;
		isRegexValue = true;
	}

	*nentries = numEntries;
	Datum *entries = (Datum *) palloc(sizeof(Datum) * numEntries);
	*partialmatch = (bool *) palloc0(sizeof(bool) * numEntries);
	(*partialmatch)[0] = true;
	(*partialmatch)[1] = false;

	if (isRegexValue)
	{
		(*partialmatch)[2] = false;
	}

	*extra_data = (Pointer *) palloc(sizeof(Pointer) * numEntries);

	/* now create a bson for that path which has the min value for the field */
	/* We set this as string.Empty but this can be set as the prefix expression */
	/* for the regex (i.e. the lowest possible value that can match a regex) */
	pgbsonelement emptyStringElement = queryElement;
	emptyStringElement.bsonValue.value_type = BSON_TYPE_UTF8;
	emptyStringElement.bsonValue.value.v_utf8.str = "";
	emptyStringElement.bsonValue.value.v_utf8.len = 0;

	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&emptyStringElement,
														args->indexTermSizeLimit).
								 indexTermVal);

	entries[1] = GenerateRootTruncatedTerm();

	if (isRegexValue)
	{
		/* Also match for the regex itself */
		entries[2] = PointerGetDatum(SerializeBsonIndexTerm(&queryElement,
															args->indexTermSizeLimit).
									 indexTermVal);
	}

	/* store the regex value in extra data - this allows us to compute regex match */
	/* this extra value. */
	RegexData *regexData = (RegexData *) palloc0(sizeof(RegexData));

	if (queryElement.bsonValue.value_type == BSON_TYPE_REGEX)
	{
		regexData->regex = queryElement.bsonValue.value.v_regex.regex;
		regexData->options = queryElement.bsonValue.value.v_regex.options;
	}
	else
	{
		regexData->regex = queryElement.bsonValue.value.v_utf8.str;
		regexData->options = NULL;
	}

	regexData->pcreData = RegexCompile(regexData->regex,
									   regexData->options);

	**extra_data = (Pointer) regexData;
	return entries;
}


/*
 * GinBsonExtractQueryMod generates the index term, needed to match
 * $mod on a dotted path, and value.
 * In this case, We return a lower bound value of the min value for that path.
 * Since we set the PartialMatch value to be true, the min value will be considered
 * a lower bound for the indexed search.
 * We also pass the $mod query value array of [Divisor, Remainder] as 'extraData'
 */
static Datum *
GinBsonExtractQueryMod(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;

	Datum *entries;
	pgbsonelement documentElement;
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(query, &filterElement);

	*extra_data = (Pointer *) palloc(sizeof(Pointer));
	**extra_data = (Pointer) query;

	/* find all values starting at mininum value. */
	documentElement.path = filterElement.path;
	documentElement.pathLength = filterElement.pathLength;
	documentElement.bsonValue.value_type = BSON_TYPE_DOUBLE;
	documentElement.bsonValue.value.v_double = (double) NAN;

	*nentries = 1;
	entries = (Datum *) palloc(sizeof(Datum));
	*partialmatch = (bool *) palloc(sizeof(bool));
	**partialmatch = true;
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
														args->indexTermSizeLimit).
								 indexTermVal);

	return entries;
}


/*
 * GinBsonExtractQueryExists generates the index term needed to match
 * $exists on dotted path and values.
 * In this case, we can simply return the term path with a minkey value
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQueryExists(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Datum *entries;
	pgbsonelement documentElement;
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(query, &filterElement);
	bool existsPositiveMatch = true;

	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	if (BsonValueIsNumberOrBool(&filterElement.bsonValue) &&
		BsonValueAsInt64(&filterElement.bsonValue) == 0)
	{
		existsPositiveMatch = false;
	}

	/* find all values starting at minkey. */
	documentElement.path = filterElement.path;
	documentElement.pathLength = filterElement.pathLength;
	documentElement.bsonValue.value_type = BSON_TYPE_MINKEY;

	if (existsPositiveMatch)
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		*partialmatch = (bool *) palloc(sizeof(bool));
		**partialmatch = true;
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
															args->indexTermSizeLimit).
									 indexTermVal);
	}
	else
	{
		*nentries = 2;
		entries = (Datum *) palloc(sizeof(Datum) * 2);
		*partialmatch = (bool *) palloc(sizeof(bool) * 2);

		/* first term is the exists term. */
		(*partialmatch)[0] = true;
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
															args->indexTermSizeLimit).
									 indexTermVal);

		/* second term is the root term. */
		(*partialmatch)[1] = false;
		entries[1] = GenerateRootTerm();
	}
	return entries;
}


/*
 * GinBsonExtractQuerySize generates the index term needed to match
 * $size on dotted path and values.
 * In this case, we can simply return the term path with a empty array value
 * The extra data is populated with the desired array size.
 * The query is expected to be of the format {path}{typeCode}{value}
 * i.e. a BsonElement.
 */
static Datum *
GinBsonExtractQuerySize(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	Datum *entries = (Datum *) palloc(sizeof(Datum));
	pgbsonelement documentElement;

	*partialmatch = (bool *) palloc(sizeof(bool));
	*nentries = 1;
	**partialmatch = true;
	PgbsonToSinglePgbsonElement(query, &documentElement);

	/* store the query value in extra data - this allows us to compute upper bound using */
	/* this extra value. */
	*extra_data = (Pointer *) palloc(sizeof(int64_t));
	*((int64_t *) *extra_data) = BsonValueAsInt64(&documentElement.bsonValue);

	/* now create a bson for that path which has the min value for the field */
	/* we map this to an empty array since that's the smallest value we'll encounter */
	entries[0] = GenerateEmptyArrayTerm(&documentElement);
	return entries;
}


/*
 * GinBsonExtractQueryDollarType generates the index term needed to match
 * $type on dotted path and values.
 * In this case, We write out the min value for each type. If no such value
 * is known, then we simply generate minKey as a fallback.
 */
static Datum *
GinBsonExtractQueryDollarType(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	Datum *entries = (Datum *) palloc(sizeof(Datum));
	pgbsonelement documentElement;
	PgbsonToSinglePgbsonElement(query, &documentElement);

	/* we only generate 1 term - minKey for the path. */
	/* note that we can't simply use the minValue of the type */
	/* since $type uses the bson type and the index is ordered by */
	/* Sort order type. For instance strings/symbols are ordered together */
	/* and int32, int64 etc are ordered together by value. */
	/* We can optimize this further, but for now we just use minValue. */
	*partialmatch = (bool *) palloc(sizeof(bool));
	*nentries = 1;
	**partialmatch = true;

	/* Compute the number of types we are requested to track */
	uint32_t numTerms = 0;

	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	if (documentElement.bsonValue.value_type == BSON_TYPE_UTF8 ||
		BsonValueIsNumberOrBool(&documentElement.bsonValue))
	{
		numTerms++;
	}
	else if (documentElement.bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		bson_iter_init_from_data(&arrayIter, documentElement.bsonValue.value.v_doc.data,
								 documentElement.bsonValue.value.v_doc.data_len);
		while (bson_iter_next(&arrayIter))
		{
			numTerms++;
		}
	}


	/* now allocate the structure containing the integer type codes */
	Datum *extraDataPtr = (Datum *) palloc(sizeof(Datum) * (numTerms + 1));

	*extra_data = (Pointer *) &extraDataPtr;

	/* The array is represented as a length N followed by N type values */
	/*  <int32_t length> <int32_t type> <int32_t type> ... */
	*extraDataPtr = Int32GetDatum(numTerms);

	/* add the type codes as extra data. */
	int index = 1;
	if (documentElement.bsonValue.value_type == BSON_TYPE_UTF8)
	{
		const char *typeNameStr = documentElement.bsonValue.value.v_utf8.str;
		bson_type_t type;
		if (strcmp(typeNameStr, "number") == 0)
		{
			/* Since $type on index only validates sort order, int32 is sufficient */
			type = BSON_TYPE_INT32;
		}
		else
		{
			type = BsonTypeFromName(typeNameStr);
		}

		extraDataPtr[index++] = Int32GetDatum((int32_t) type);
	}
	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	else if (BsonValueIsNumberOrBool(&documentElement.bsonValue))
	{
		int64_t typeCode = BsonValueAsInt64(&documentElement.bsonValue);

		/* TryGetTypeFromInt64 should be successful as this was already validated in the planner when walking the query. */
		bson_type_t resolvedType;
		TryGetTypeFromInt64(typeCode, &resolvedType);
		extraDataPtr[index++] = Int32GetDatum((int32_t) resolvedType);
	}
	else if (documentElement.bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		bson_iter_t arrayIter;
		bson_iter_init_from_data(&arrayIter, documentElement.bsonValue.value.v_doc.data,
								 documentElement.bsonValue.value.v_doc.data_len);
		while (bson_iter_next(&arrayIter))
		{
			const bson_value_t *currentValue = bson_iter_value(&arrayIter);

			/**
			 * TODO: FIXME - Verify whether strict number check is required here
			 * */
			if (BsonValueIsNumberOrBool(currentValue))
			{
				int64_t typeCode = BsonValueAsInt64(currentValue);

				/* TryGetTypeFromInt64 should be successful as this was already validated in the planner when walking the query. */
				bson_type_t resolvedType;
				TryGetTypeFromInt64(typeCode, &resolvedType);
				extraDataPtr[index++] = Int32GetDatum((int32_t) resolvedType);
			}
			else if (currentValue->value_type == BSON_TYPE_UTF8)
			{
				const char *typeNameStr = currentValue->value.v_utf8.str;
				bson_type_t type;
				if (strcmp(typeNameStr, "number") == 0)
				{
					/* Since $type on index only validates sort order, int32 is sufficient */
					type = BSON_TYPE_INT32;
				}
				else
				{
					type = BsonTypeFromName(typeNameStr);
				}

				extraDataPtr[index++] = Int32GetDatum((int32_t) type);
			}
			else
			{
				ereport(ERROR, (errmsg("Could not read data type in array for $type")));
			}
		}
	}
	else
	{
		ereport(ERROR, (errmsg("Could not read data type for $type value")));
	}

	/* now create a bson for that path which has the min value for the field */
	/* we map this to an empty array since that's the smallest value we'll encounter */
	pgbsonelement termElement = documentElement;
	termElement.bsonValue.value_type = BSON_TYPE_MINKEY;
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&termElement,
														args->indexTermSizeLimit).
								 indexTermVal);

	return entries;
}


/*
 * GinBsonExtractQueryDollarAll generates the index term needed to match
 * $all on dotted path and values.
 * In this case, we generate a term for each value in the $all array
 * and let the index match the values to those terms.
 */
static Datum *
GinBsonExtractQueryDollarAll(BsonExtractQueryArgs *args)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	pgbsonelement filterElement;
	PgbsonToSinglePgbsonElement(query, &filterElement);

	if (filterElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"$all needs an array")));
	}

	uint32_t termCount = 0;
	bson_iter_t arrayIterator;
	if (!bson_iter_init_from_data(&arrayIterator,
								  filterElement.bsonValue.value.v_doc.data,
								  filterElement.bsonValue.value.v_doc.data_len))
	{
		ereport(ERROR, (errmsg("Could not read array for $all")));
	}

	bool arrayHasOnlyNull = false;
	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);
		arrayHasOnlyNull = (termCount == 0 || arrayHasOnlyNull) &&
						   arrayValue->value_type == BSON_TYPE_NULL;
		termCount++;
	}

	/* if the $all array has only null values, we can match documents
	 * that don't define the path we are matching, so we create a root term
	 * to match them. */
	if (arrayHasOnlyNull)
	{
		termCount++;
	}

	/* Add one for the root truncated term */
	termCount++;

	/* We generate N terms for N elements in the $all query */
	DollarArrayOpQueryData *queryData = palloc0(sizeof(DollarArrayOpQueryData));
	queryData->arrayHasNull = arrayHasOnlyNull;
	Datum *entries = (Datum *) palloc(sizeof(Datum) * termCount);
	*partialmatch = (bool *) palloc0(sizeof(bool) * termCount);

	/* The last slot is used to store the pointer to the info (bool) whether all
	 * of the array elements inside $all are null or not. Hence a '+ 1' here.
	 * The other slots are pointer to the regex patterns, if they exists, in
	 * in the $all array */
	*extra_data = (Pointer *) palloc0((sizeof(Pointer) * (termCount + 1)));
	bool *partialMatchArray = *partialmatch;
	Pointer *extraDataArray = *extra_data;

	int index = 0;
	bson_iter_init_from_data(&arrayIterator, filterElement.bsonValue.value.v_doc.data,
							 filterElement.bsonValue.value.v_doc.data_len);

	while (bson_iter_next(&arrayIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&arrayIterator);

		pgbsonelement element;
		element.path = filterElement.path;
		element.pathLength = filterElement.pathLength;
		element.bsonValue = *arrayValue;

		pgbsonelement innerDocumentElement;
		if (element.bsonValue.value_type == BSON_TYPE_REGEX)
		{
			BsonDollarAllIndexState *dollarAllState = palloc(
				sizeof(BsonDollarAllIndexState));
			dollarAllState->isExprEvalState = false;
			extraDataArray[index] = (Pointer) dollarAllState;
			ProcessExtractQueryForRegex(&element, &partialMatchArray[index],
										(Pointer *) &dollarAllState->regexData);

			BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																			args->
																			indexTermSizeLimit);
			entries[index] = PointerGetDatum(serializedTerm.indexTermVal);
			queryData->arrayHasRegex = true;
		}
		else if (element.bsonValue.value_type == BSON_TYPE_DOCUMENT &&
				 TryGetBsonValueToPgbsonElement(&element.bsonValue,
												&innerDocumentElement) &&
				 strcmp(innerDocumentElement.path, "$elemMatch") == 0)
		{
			BsonDollarAllIndexState *dollarAllState = palloc(
				sizeof(BsonDollarAllIndexState));
			dollarAllState->isExprEvalState = true;
			extraDataArray[index] = (Pointer) dollarAllState;
			partialMatchArray[index] = true;
			dollarAllState->exprEvalState.expression = GetExpressionEvalState(
				&innerDocumentElement.bsonValue, CurrentMemoryContext);
			dollarAllState->exprEvalState.isEmptyExpression = IsBsonValueEmptyDocument(
				&innerDocumentElement.bsonValue);

			/* reset the element to point to an empty array */
			entries[index] = GenerateEmptyArrayTerm(&element);
			queryData->arrayHasElemMatch = true;
		}
		else
		{
			BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																			args->
																			indexTermSizeLimit);
			entries[index] = PointerGetDatum(serializedTerm.indexTermVal);
			queryData->arrayHasTruncation = queryData->arrayHasTruncation ||
											serializedTerm.isIndexTermTruncated;
		}

		index++;
	}

	queryData->inTermCount = index;

	if (arrayHasOnlyNull)
	{
		entries[index] = GenerateRootTerm();
		queryData->rootTermIndex = index;
		index++;
	}

	if (queryData->arrayHasTruncation ||
		queryData->arrayHasRegex ||
		queryData->arrayHasElemMatch)
	{
		entries[index] = GenerateRootTruncatedTerm();
		queryData->rootTruncatedTermIndex = index;
		index++;
	}

	*nentries = index;

	/* The last slot of extra_data (that is, if n is the number of entries,
	 * then (n+1)th slot) will be storing a pointer to the info whether
	 * the $in array has null or not */
	(*extra_data)[index] = (Pointer) queryData;

	return entries;
}


/*
 * This is the term that gets set when a document has no index.
 * terms defined. Used for non-sparse unique indexes
 * We pick a term that points to a path that is empty.
 * and give it to all terms. This is similar to the root term
 * except we pick a different value to distinguish the two.
 * We do not assign the term path to it since we don't want to
 * have false positives on $exists queries or equality to NULL queries.
 */
static Datum
GeneratePathUndefinedTerm(void *options)
{
	BsonGinIndexOptionsBase *optionsBase = (BsonGinIndexOptionsBase *) options;
	if (optionsBase->type != IndexOptionsType_SinglePath &&
		optionsBase->type != IndexOptionsType_UniqueShardKey)
	{
		ereport(ERROR, (errmsg(
							"Undefined term should only be set on single path indexes")));
	}

	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;
	element.bsonValue.value_type = BSON_TYPE_NULL;
	return PointerGetDatum(SerializeBsonIndexTerm(&element, 0).indexTermVal);
}


/*
 * The core dispatch function for gin_bson_compare_partial.
 * Based on the indexing strategy, calls the appropriate
 * extract query function for the index and returns the output.
 */
int32_t
GinBsonComparePartialCore(BsonIndexStrategy strategy, BsonIndexTerm *queryValue,
						  BsonIndexTerm *compareValue, Pointer extraData)
{
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
		case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
		{
			return GinBsonComparePartialGreater(queryValue, compareValue);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_RANGE:
		{
			DollarRangeValues *rangeValue = (DollarRangeValues *) extraData;
			return GinBsonComparePartialDollarRange(rangeValue, queryValue, compareValue);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
		case BSON_INDEX_STRATEGY_DOLLAR_LESS:
		{
			bytea *maxValue = (bytea *) extraData;

			return GinBsonComparePartialLess(queryValue, compareValue,
											 maxValue);
		}

		/* A partial compare for $in, $nin and $all is set to true only when
		 * there are regex inside the array. This is set in
		 * GinBsonExtractQueryIn(), GinBsonExtractQueryNotIn,
		 * GinBsonExtractQueryDollarAll() */
		case BSON_INDEX_STRATEGY_DOLLAR_REGEX:
		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		case BSON_INDEX_STRATEGY_DOLLAR_NOT_IN:
		{
			RegexData *regexData = (RegexData *) extraData;
			return GinBsonComparePartialRegex(queryValue, compareValue, regexData);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_MOD:
		{
			pgbson *modArrayData = (pgbson *) extraData;
			pgbsonelement modArray;
			PgbsonToSinglePgbsonElement(modArrayData, &modArray);
			return GinBsonComparePartialMod(queryValue, compareValue, &modArray);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ALL:
		{
			BsonDollarAllIndexState *indexState =
				(BsonDollarAllIndexState *) extraData;
			if (indexState->isExprEvalState)
			{
				BsonElemMatchIndexExprState *evalState = &indexState->exprEvalState;
				return GinBsonComparePartialElemMatchExpression(queryValue,
																compareValue,
																evalState);
			}
			else
			{
				return GinBsonComparePartialRegex(queryValue, compareValue,
												  indexState->regexData);
			}
		}

		case BSON_INDEX_STRATEGY_DOLLAR_ELEMMATCH:
		{
			return GinBsonComparePartialElemMatch(queryValue, compareValue,
												  extraData);
		}

		/* the compare happens to match the $exists. */
		case BSON_INDEX_STRATEGY_DOLLAR_EXISTS:
		{
			return GinBsonComparePartialExists(queryValue, compareValue);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_SIZE:
		{
			int64_t size = (int64_t) extraData;
			return GinBsonComparePartialSize(queryValue, compareValue, size);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_TYPE:
		{
			Datum *typeArray = (Datum *) extraData;
			return GinBsonComparePartialType(queryValue, compareValue, typeArray);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_CLEAR:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;
			PgbsonToSinglePgbsonElement(queryBson, &filterElement);
			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAllClear);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_CLEAR:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;
			PgbsonToSinglePgbsonElement(queryBson, &filterElement);
			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAnyClear);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_SET:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;
			PgbsonToSinglePgbsonElement(queryBson, &filterElement);
			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAllSet);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_SET:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;
			PgbsonToSinglePgbsonElement(queryBson, &filterElement);
			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAnySet);
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_CHECK_VIOLATION), errmsg(
								"gin_compare_partial_bson: Unsupported strategy")));
		}
	}
}


/*
 * GinBsonComparePartialGreater implements the core logic of comparing
 * an index term against a provided query for the operations of $gt, and $gte
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialGreater(BsonIndexTerm *queryValue,
							 BsonIndexTerm *compareValue)
{
	/* in $gt and $gte, the gin querying starts at the term being queried and iterates up. */
	/* therefore all paths should match the index. If we hit a path that's different, */
	/* it means we've exhausted scanning all the paths for the current query so we should */
	/* stop enumerating. */
	if (strcmp(queryValue->element.path, compareValue->element.path) != 0)
	{
		return 1;
	}
	else if (queryValue->element.bsonValue.value_type == BSON_TYPE_MINKEY)
	{
		return compareValue->element.bsonValue.value_type == BSON_TYPE_MINKEY ? -1 : 0;
	}
	else if (CompareBsonSortOrderType(&queryValue->element.bsonValue,
									  &compareValue->element.bsonValue) != 0)
	{
		/* if we hit a type mismatch, we can bail. */
		return 1;
	}
	else
	{
		bool isComparisonValid = true;
		int cmp = CompareBsonIndexTerm(compareValue, queryValue,
									   &isComparisonValid);
		if (!isComparisonValid)
		{
			return -1;
		}
		if (cmp > 0)
		{
			/* if compare value is > query value, it's a match and keep going */
			return 0;
		}
		else if (cmp == 0)
		{
			/* if it's equal, then it's only a match if it's $gte */
			/* for $gt it's not a match but keep enumerating since there may be */
			/* more values that are bigger after this. */
			return -1;
		}
		else
		{
			/* if somehow we end up in a scenario where we have a compareValue */
			/* that is less than the query value (maybe due to it being the only */
			/* value for that path?) stop enumerating. */
			return 1;
		}
	}
}


/*
 * GinBsonComparePartialBitsWiseOperator implements the core logic of comparing
 * an index term against a provided query for the operations of dollar bitwise operators.
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 * Partial Comparison for $bitsAllClear, $bitsAnyClear.
 */
static int32_t
GinBsonComparePartialBitsWiseOperator(BsonIndexTerm *queryValue,
									  BsonIndexTerm *compareValue,
									  pgbsonelement *filterSetPositionArray,
									  CompareArrayForBitwiseOp bitsCompareFunc)
{
	/* in dollar bitwise operators, the gin querying starts at min term of number and min term of binary and iterates up. */
	/* therefore all paths should match the index. If we hit a path that's different, */
	/* it means we've exhausted scanning all the paths for the current query so we should */
	/* stop enumerating. */

	if (strcmp(queryValue->element.path, compareValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else if (!BsonTypeIsNumber(compareValue->element.bsonValue.value_type) &&
			 compareValue->element.bsonValue.value_type != BSON_TYPE_BINARY)
	{
		/* if document type is neither a number nor binary type - we can bail */
		return 1;
	}
	else
	{
		if (compareValue->isIndexTermTruncated)
		{
			/* If it's truncated, mark it as a match and let the runtime deal with it */
			return 0;
		}

		if (CompareBitwiseOperator(&compareValue->element.bsonValue,
								   &(filterSetPositionArray->bsonValue), bitsCompareFunc))
		{
			return 0;
		}
		else
		{
			return -1;
		}
	}
}


/*
 * GinBsonComparePartialMod implements the core logic of comparing
 * an index term against a provided query for $mod operator.
 * Validates the output is appropriate, and then returns 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialMod(BsonIndexTerm *queryValue,
						 BsonIndexTerm *compareValue,
						 pgbsonelement *modArray)
{
	/* in $mod op, the gin querying starts at the minimum term of number and iterates up. */
	/* therefore all paths should match the index. If we hit a path that's different, */
	/* it means we've exhausted scanning all the paths for the current query so we should */
	/* stop enumerating. */
	if (strcmp(queryValue->element.path, compareValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else if (!BsonTypeIsNumber(compareValue->element.bsonValue.value_type))
	{
		/* if document type is not a number - bail out */
		return 1;
	}
	else
	{
		return CompareModOperator(&compareValue->element.bsonValue,
								  &(modArray->bsonValue)) ? 0 :
			   -1;
	}
}


/*
 * GinBsonComparePartialLess implements the core logic of comparing
 * an index term against a provided query for the operations of $lt, and $lte
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialLess(BsonIndexTerm *queryValue,
						  BsonIndexTerm *compareValue,
						  bytea *maxValue)
{
	/* for $lt and $lte, the query value is the minValue and the compare value is the value */
	int cmp;
	if (strcmp(compareValue->element.path, queryValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else
	{
		BsonIndexTerm maxValueIndexTerm;
		InitializeBsonIndexTerm(maxValue, &maxValueIndexTerm);
		if (maxValueIndexTerm.element.bsonValue.value_type == BSON_TYPE_MAXKEY)
		{
			return compareValue->element.bsonValue.value_type == BSON_TYPE_MAXKEY ? -1 :
				   0;
		}
		else if ((cmp = CompareBsonSortOrderType(&compareValue->element.bsonValue,
												 &maxValueIndexTerm.element.bsonValue)) !=
				 0)
		{
			/* if the type is less than the max that we want, continue. Otherwise break. */
			return cmp < 0 ? -1 : 1;
		}
		else
		{
			bool isComparisonValid = true;
			cmp = CompareBsonIndexTerm(compareValue,
									   &maxValueIndexTerm,
									   &isComparisonValid);
			if (!isComparisonValid)
			{
				return -1;
			}

			return cmp < 0 ? 0 : 1;
		}
	}
}


/*
 * GinBsonComparePartialDollarRange implements the core logic of comparing
 * an index term against a provided query for the operations of $range.
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialDollarRange(DollarRangeValues *rangeValues,
								 BsonIndexTerm *queryValue,
								 BsonIndexTerm *compareValue)
{
	if (strcmp(compareValue->element.path, queryValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can stop. */
		return 1;
	}

	if (!rangeValues->isArrayTerm)
	{
		BsonIndexTerm maxValueIndexTerm;
		InitializeBsonIndexTerm(rangeValues->maxValueIndexTerm, &maxValueIndexTerm);

		BsonIndexTerm minValueIndexTerm;
		InitializeBsonIndexTerm(rangeValues->minValueIndexTerm, &minValueIndexTerm);

		bool isMinConditionMet = false;
		bool isMaxConditionMet = false;
		if (minValueIndexTerm.element.bsonValue.value_type == BSON_TYPE_MINKEY)
		{
			isMinConditionMet = rangeValues->params.isMinInclusive ||
								compareValue->element.bsonValue.value_type !=
								BSON_TYPE_MINKEY;
		}
		else if (CompareBsonSortOrderType(&compareValue->element.bsonValue,
										  &rangeValues->params.minValue) == 0)
		{
			bool isComparisonValidMin = true;
			int minCmp = CompareBsonIndexTerm(compareValue, &minValueIndexTerm,
											  &isComparisonValidMin);

			/* Do the default comparison check for Min */
			isMinConditionMet = rangeValues->params.isMinInclusive ? minCmp >= 0 :
								minCmp > 0;
			isMinConditionMet = isMinConditionMet && isComparisonValidMin;

			/* The min may also be met if the min term is truncated for a > Min */
			if (!rangeValues->params.isMinInclusive && minCmp == 0 &&
				minValueIndexTerm.isIndexTermTruncated)
			{
				isMinConditionMet = true;
			}
		}

		int sortOrderCmpMax = CompareBsonSortOrderType(&compareValue->element.bsonValue,
													   &rangeValues->params.maxValue);
		int maxCmp = 0;
		if (maxValueIndexTerm.element.bsonValue.value_type == BSON_TYPE_MAXKEY)
		{
			isMaxConditionMet = rangeValues->params.isMaxInclusive ||
								compareValue->element.bsonValue.value_type !=
								BSON_TYPE_MAXKEY;
		}
		else if (sortOrderCmpMax == 0)
		{
			bool isComparisonValidMax = true;
			maxCmp = CompareBsonIndexTerm(compareValue, &maxValueIndexTerm,
										  &isComparisonValidMax);

			/* Do the default comparison check for Min */
			isMaxConditionMet = rangeValues->params.isMaxInclusive ? maxCmp <= 0 :
								maxCmp < 0;
			isMaxConditionMet = isMaxConditionMet && isComparisonValidMax;

			/* The max may also be met if the max term is truncated for a < Max */
			if (!rangeValues->params.isMaxInclusive && maxCmp == 0 &&
				maxValueIndexTerm.isIndexTermTruncated)
			{
				isMaxConditionMet = true;
			}
		}

		if (isMinConditionMet && isMaxConditionMet)
		{
			return 0;
		}

		if (sortOrderCmpMax > 0 || maxCmp > 0)
		{
			/* Reached a different type bracket or a value larger than the max - can bail */
			return 1;
		}

		return -1;
	}

	if (compareValue->element.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		return 1;
	}

	bson_iter_t arrayIter;
	BsonValueInitIterator(&compareValue->element.bsonValue, &arrayIter);

	bool isMinConditionSet = false;
	bool isMaxConditionSet = false;
	while (bson_iter_next(&arrayIter))
	{
		const bson_value_t *value = bson_iter_value(&arrayIter);

		bool isComparisonValid = true;
		int cmp;


		/*
		 *  Cond: <Value> $gt(e) <query_min_val>
		 *
		 *  query_min_val:Minkey()  => matches everything
		 *  query_min_val:MaxKey()  => matches only MaxKey() when isMinInClusive = true
		 *  query_min_val: int32    => Match only numbers
		 */

		if (CompareBsonSortOrderType(value, &rangeValues->params.minValue) == 0)
		{
			cmp = CompareBsonValueAndType(value,
										  &rangeValues->params.minValue,
										  &isComparisonValid);

			if (isComparisonValid && (
					cmp > 0 ||
					(cmp == 0 && rangeValues->params.isMinInclusive) ||
					(cmp == 0 && value->value_type == BSON_TYPE_MINKEY)))  /* [MinKey()] $gt MinKey() */
			{
				isMinConditionSet = true;
			}
		}
		else if (rangeValues->params.minValue.value_type == BSON_TYPE_MINKEY)
		{
			isMinConditionSet = true;
		}


		/*
		 *  Condition: <value> $lt(e) <query_max_val>
		 *
		 *      query_max_val:Maxkey()  => match everything
		 *      query_max_val:MinKey()  => match only MinKey() when isMaxInClusive = true
		 *      query_max_val:int32     => Matches only numbers
		 */

		if (CompareBsonSortOrderType(value, &rangeValues->params.maxValue) == 0)
		{
			cmp = CompareBsonValueAndType(value,
										  &rangeValues->params.maxValue,
										  &isComparisonValid);

			if (isComparisonValid && (
					cmp < 0 ||
					(cmp == 0 && rangeValues->params.isMaxInclusive) ||
					(cmp == 0 && value->value_type == BSON_TYPE_MAXKEY)))            /* [MaxKey()] $lt MaxKey() */
			{
				isMaxConditionSet = true;
			}
		}
		else if (rangeValues->params.minValue.value_type == BSON_TYPE_MAXKEY)
		{
			isMaxConditionSet = true;
		}

		if (isMinConditionSet && isMaxConditionSet)
		{
			return 0;
		}
	}

	return -1;
}


/*
 * GinBsonComparePartialRegex implements the core logic of comparing
 * an index term against a provided query for the operations of $regex
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialRegex(BsonIndexTerm *queryValue, BsonIndexTerm *compareValue,
						   RegexData *regexData)
{
	if (strcmp(compareValue->element.path, queryValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, bail. */
		return 1;
	}
	else if (CompareBsonSortOrderType(&compareValue->element.bsonValue,
									  &queryValue->element.bsonValue) > 0)
	{
		/* if the sort type is greater than string (so we've iterated through all possible strings) */
		/* we can stop iterating more. */
		return 1;
	}
	else if (compareValue->isIndexTermTruncated)
	{
		/* Don't compare truncated terms in the index */
		return -1;
	}
	else if (CompareRegexTextMatch(&compareValue->element.bsonValue, regexData))
	{
		return 0;
	}
	else
	{
		return -1;
	}
}


/*
 * Compares a query against an existing item in the index and a given
 * compiled expression. If the path doesn't match, exits the search.
 * If the type isn't an array, then exits the search. Otherwise
 * evaluates the expression against the array to return a match.
 * Note: This assumes that the ExtractQuery generated the minArray
 * term as a starting point.
 */
int32_t
GinBsonComparePartialElemMatchExpression(BsonIndexTerm *queryValue,
										 BsonIndexTerm *compareValue,
										 BsonElemMatchIndexExprState *exprState)
{
	/* for $elemMatch expressions, the query value is the min Array at the path */
	if (strcmp(compareValue->element.path, queryValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else if (compareValue->element.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		/* no more arrays left. we can bail */
		return 1;
	}
	else
	{
		bool compareResult;

		if (exprState->isEmptyExpression)
		{
			compareResult = false;
			bson_iter_t arrayIter;
			BsonValueInitIterator(&compareValue->element.bsonValue, &arrayIter);
			while (bson_iter_next(&arrayIter))
			{
				if (bson_iter_type(&arrayIter) == BSON_TYPE_DOCUMENT ||
					bson_iter_type(&arrayIter) == BSON_TYPE_ARRAY)
				{
					compareResult = true;
					break;
				}
			}
		}
		else
		{
			compareResult = EvalBooleanExpressionAgainstArray(exprState->expression,
															  &compareValue->element.
															  bsonValue);
		}

		/* if match, return 0 (match), otherwise continue evaluating */
		return compareResult ? 0 : -1;
	}
}


/*
 * GinBsonComparePartialExists implements the core logic of comparing
 * an index term against a provided query for the operations of $exists.
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialExists(BsonIndexTerm *queryValue, BsonIndexTerm *compareValue)
{
	/* for $exists and $lte, the query value is the minValue and the compare value is the value */
	if (strcmp(queryValue->element.path, compareValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else
	{
		/* path is a match - it's a match. */
		return 0;
	}
}


/*
 * GinBsonComparePartialSize implements the core logic of comparing
 * an index term against a provided query for the operations of $size.
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialSize(BsonIndexTerm *queryValue, BsonIndexTerm *compareValue, int64_t
						  arraySize)
{
	/* for $exists and $lte, the query value is the minValue and the compare value is the value */
	if (strcmp(compareValue->element.path, queryValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else if (compareValue->element.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		/* we've moved past arrays - we can bail. */
		return 1;
	}
	else
	{
		bson_iter_t arrayIter;
		bson_iter_init_from_data(&arrayIter,
								 compareValue->element.bsonValue.value.v_doc.data,
								 compareValue->element.bsonValue.value.v_doc.data_len);
		int64_t actualArraySize = 0;
		while (bson_iter_next(&arrayIter) && actualArraySize <= arraySize)
		{
			actualArraySize++;
		}

		/* If it's truncated: */
		if (compareValue->isIndexTermTruncated)
		{
			/* If we're already greater than the required size, we don't need to go
			 * to the runtime. e.g. if { $size: 1} and we counted 2 elements in the
			 * array, then this doesn't matter for the $size query.
			 */
			return actualArraySize > arraySize ? -1 : 0;
		}

		return actualArraySize == arraySize ? 0 : -1;
	}
}


/*
 * GinBsonComparePartialType implements the core logic of comparing
 * an index term against a provided query for the operations of $type.
 * Validates the output is appropriate, and then returns -1 if it's not a match
 * but needs to continue, 0 if it's a match, and 1 to stop iterating.
 */
static int32_t
GinBsonComparePartialType(BsonIndexTerm *queryValue, BsonIndexTerm *compareValue,
						  Datum *typeArray)
{
	/* for $exists and $lte, the query value is the minValue and the compare value is the value */
	if (strcmp(compareValue->element.path, queryValue->element.path) != 0)
	{
		/* if the path comparison is a mismatch, then we hit a different path - we can bail. */
		return 1;
	}
	else
	{
		/* The array is represented as a length N followed by N type values */
		/*  <int32_t length> <int32_t type> <int32_t type> ... */
		int32_t arrayLength = DatumGetInt32(*typeArray);
		typeArray++;
		for (int i = 0; i < arrayLength; i++)
		{
			/* Since the index treats SortOrderTypes as equivalent, we can only
			 * do partial checks of $type here - the rest has to be done in the
			 * runtime as a recheck
			 */
			bson_type_t typeArrayType = (bson_type_t) DatumGetInt32(typeArray[i]);
			int cmp = CompareSortOrderType(typeArrayType,
										   compareValue->element.bsonValue.value_type);
			if (cmp == 0)
			{
				/* the type matched, return match. */
				return 0;
			}
		}

		/* no types matched, continue searching */
		return -1;
	}
}


/*
 * This function gets called when the current bson term from the array Iterator
 * (during ExtractQuery) is of type REGEX and does the following:
 * 1. partial match for the term is set to true. (Memory is allocated for
 *    partial match array slots, during the first term processing)
 * 2. Regex pattern string is kept in extra_data slot
 */
static void
ProcessExtractQueryForRegex(pgbsonelement *element, bool *partialmatch,
							Pointer *extra_data)
{
	/* Partial match is set to true only when regex is present as an
	 * array element of $in, $nin, $all. This will enable the invocation of
	 * gin_bson_compare_partial with strategy as
	 * BSON_INDEX_STRATEGY_DOLLAR_REGEX. Thus we invoke
	 * GinBsonComparePartialRegex and subsequently
	 * CompareRegexTextMatch() to perform the actual regex match.
	 */
	*partialmatch = true;

	RegexData *regexData = (RegexData *) palloc0(sizeof(RegexData));
	regexData->regex = element->bsonValue.value.v_regex.regex;
	regexData->options = element->bsonValue.value.v_regex.options;

	regexData->pcreData = RegexCompile(regexData->regex,
									   regexData->options);

	/* This stores the regex pattern string */
	*extra_data = (Pointer) regexData;

	element->bsonValue.value_type = BSON_TYPE_UTF8;
	element->bsonValue.value.v_utf8.str = "";
	element->bsonValue.value.v_utf8.len = 0;
}


static Datum
GenerateEmptyArrayTerm(pgbsonelement *filterElement)
{
	pgbson_writer bsonWriter;

	/* now create a bson for that path which has the min value for the field */
	/* we map this to an empty array since that's the smallest value we'll encounter */
	PgbsonWriterInit(&bsonWriter);
	PgbsonWriterAppendEmptyArray(&bsonWriter, filterElement->path,
								 filterElement->pathLength);
	pgbson *doc = PgbsonWriterGetPgbson(&bsonWriter);

	pgbsonelement termElement;
	PgbsonToSinglePgbsonElement(doc, &termElement);
	return PointerGetDatum(SerializeBsonIndexTerm(&termElement, 0).indexTermVal);
}
