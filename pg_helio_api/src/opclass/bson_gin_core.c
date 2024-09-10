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
#include "utils/helio_errors.h"
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

	/* The index of the queryKey of the root exists term */
	int rootExistsTermIndex;

	/* The index of the queryKey of the non exists term */
	int nonExistsTermIndex;

	/* The index of the queryKey of the root truncated term */
	int rootTruncatedTermIndex;

	/* The index of the queryKey of the literal undefined term */
	int undefinedLiteralTermIndex;

	/* Index of the multi-key existence term */
	int multiKeyTermIndex;
} DollarArrayOpQueryData;


typedef struct DollarExistsQueryData
{
	/* whether the lookup was a partial match or not.
	 * Wildcard index terms don't give us enough information so we have to do
	 * a $gte: MinKey partial match. */
	bool isComparePartial;

	/* Whether is $exists: true or $exists: false. */
	bool isPositiveExists;
} DollarExistsQueryData;

extern bool EnableGenerateNonExistsTerm;
extern bool EnableCollation;

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
static Datum * GinBsonExtractQueryGreater(BsonExtractQueryArgs *args, bool isNegation);
static Datum * GinBsonExtractQueryGreaterEqual(BsonExtractQueryArgs *args, bool
											   isNegation);
static Datum * GinBsonExtractQueryLess(BsonExtractQueryArgs *args, bool isNegation);
static Datum * GinBsonExtractQueryLessEqual(BsonExtractQueryArgs *args, bool isNegation);

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
static Datum GenerateEmptyArrayTerm(pgbsonelement *filterElement, const
									IndexTermCreateMetadata *metadata);
static inline Datum * GenerateNullEqualityIndexTerms(int32 *nentries, bool **partialmatch,
													 pgbsonelement *filterElement,
													 const IndexTermCreateMetadata *
													 metadata);
static Datum * GenerateExistsEqualityTerms(int32 *nentries, bool **partialmatch,
										   Pointer **extraData,
										   pgbsonelement *filterElement, bool
										   isPositiveExists,
										   const void *indexOptions,
										   const IndexTermCreateMetadata *metadata);
static bson_value_t GetLowerBoundForLessThan(bson_type_t inputBsonType);


/*
 * returns true if the index is a wildcard index
 */
inline static bool
IsWildcardIndex(const void *indexOptions)
{
	BsonGinIndexOptionsBase *optionsBase = (BsonGinIndexOptionsBase *) indexOptions;
	switch (optionsBase->type)
	{
		case IndexOptionsType_SinglePath:
		{
			return ((BsonGinSinglePathOptions *) indexOptions)->isWildcard;
		}

		case IndexOptionsType_Wildcard:
		{
			return true;
		}

		case IndexOptionsType_Text:
		{
			return ((BsonGinTextPathOptions *) indexOptions)->isWildcard;
		}

		default:
		{
			return false;
		}
	}
}


inline static bool
HandleConsistentEqualsNull(bool *check, bool *recheck, bytea *indexOptions)
{
	/* we're in the $eq: null / $gte : null/ $lte: null case. */
	/* result is true, if the value is null, doesn't exist or literal undefined. */

	/* check[0] == literal null
	 * check[1] == Root non exists
	 * check[2] == literal undefined
	 * check[3] == root exists term
	 * check[4] == array ancestors
	 * check[5] == legacy root term
	 */
	if (check[0] || check[1] || check[2])
	{
		/* literal null, literal undefined, or non-exists */

		/* This definitely matches $eq: null except for array ancestors where we need
		 * to recheck in the runtime.
		 */

		/* For array multi-key terms - the index does not have fidelity - needs to push to runtime */
		*recheck = check[4];
		return true;
	}

	if (check[3])
	{
		/* It matches the root exists term - so some path exists and it's not
		 * null, or undefined. Could still be a false positive in the following cases:
		 * 1) The index is a wildcard index
		 * 2) The path has array ancestors
		 * In all other cases, this can fully be trusted as not matching null.
		 */
		if (check[4] || IsWildcardIndex(indexOptions))
		{
			*recheck = true;
			return true;
		}

		return false;
	}

	/* It matches the legacy root term follow existing path and push to runtime */
	*recheck = true;
	return check[5];
}


inline static bool
HandleConsistentGreaterLessEquals(Datum *queryKeys, bool *check, bool *recheck,
								  bytea *indexOptions)
{
	BsonIndexTerm indexTerm;
	InitializeBsonIndexTerm(DatumGetByteaPP(queryKeys[0]), &indexTerm);

	if (indexTerm.element.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* $gte: null path */
		return HandleConsistentEqualsNull(check, recheck, indexOptions);
	}

	/* $gte is true if it's $gt/$lt (the 0th term ) */

	/* Recheck is always false if the term is strictly greater than
	 * For equality, it's false unless it's truncated.
	 */
	BsonIndexTerm equalityTerm;
	InitializeBsonIndexTerm(DatumGetByteaPP(queryKeys[1]), &equalityTerm);
	*recheck = !check[0] && equalityTerm.isIndexTermTruncated;

	/* A row matches if it's $gt/$lt OR $eq */
	return check[0] || check[1];
}


inline static bool
HandleConsistentExists(bool *check, bool *recheck, const DollarExistsQueryData *queryData)
{
	*recheck = false;

	if (queryData->isComparePartial)
	{
		/* Wildcard index scenario */
		if (queryData->isPositiveExists)
		{
			return check[0];
		}

		/* There's 4 terms:
		 * check[0] -> $gte: MinKey()
		 */
		if (check[0])
		{
			/* It matched an exists: true - guaranteed false */
			return false;
		}

		/* It did not match exists: true, but matches Root/RootExists
		 * check[1] -> Legacy root term
		 * check[2] -> Root Non Exists
		 * check[3] -> Root Exists
		 * These match all docs - if we remove the ones in check[0]
		 * These all match exists: false
		 */
		return true;
	}

	/* Non compare-partial path (single path index) */
	if (check[0])
	{
		/* For positive - this is the exists term.
		 * For negative - this is the non-exists term
		 * return what it says
		 */
		return true;
	}

	/* check[0] didn't match use legacy root term and thunk to runtime */
	*recheck = true;
	return check[1];
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
	context->termMetadata = GetIndexTermMetadata(singlePathOptions);

	bool addRootTerm = true;
	GenerateTerms(bson, context, addRootTerm);
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
	context->termMetadata = GetIndexTermMetadata(wildcardOptions);

	bool addRootTerm = true;
	GenerateTerms(bson, context, addRootTerm);
}


/*
 * Common logic for handling the Consistent check for
 * $eq: null in array terms. This is used between $in/$all
 */
inline static bool
HandleConsistentArrayOpForEqualsNull(bool *check, DollarArrayOpQueryData *queryData,
									 bool *recheck, bytea *indexClassOptions)
{
	/* $eq/$in null. For new indexes we generate a non exists term,
	 * so if we match that and we don't match the root term, we don't need to recheck. */

	/* equals null checks on:
	 * check[0] == literal null
	 * check[1] == Root non exists
	 * check[2] == literal undefined
	 * check[3] == root exists term
	 * check[4] == array ancestors
	 * check[5] == legacy root term
	 */
	bool eqNullCheck[6] = { 0 };

	/* Exact equality on null is covered above */
	eqNullCheck[0] = false;
	eqNullCheck[1] = check[queryData->nonExistsTermIndex];
	eqNullCheck[2] = check[queryData->undefinedLiteralTermIndex];
	eqNullCheck[3] = check[queryData->rootExistsTermIndex];
	eqNullCheck[4] = check[queryData->multiKeyTermIndex];
	eqNullCheck[5] = check[queryData->rootTermIndex];

	return HandleConsistentEqualsNull(eqNullCheck, recheck, indexClassOptions);
}


static bool
HandleConsistentGreaterLess(bool *check, bool *recheck, int numKeys, Datum *queryKeys)
{
	if (check[0])
	{
		/* $gt/$lt from the index can be fully trusted for hits */
		*recheck = false;
		return check[0];
	}

	bool isTermTruncated = false;
	if (numKeys >= 2)
	{
		isTermTruncated = IsSerializedIndexTermTruncated(DatumGetByteaPP(
															 queryKeys[1]));
	}

	if (!isTermTruncated)
	{
		/* The index can be fully trusted for scenarios where there's no truncation */
		*recheck = false;
		return check[0];
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


static bool
HandleConsistentGreaterEquals(Pointer *extra_data, bool *check, bool *recheck,
							  Datum *queryKeys, bytea *indexClassOptions)
{
	/* we translate $exists: true to $gte: MinKey at the planner level
	 * We can't use queryKeys here and check if $gte: MinKey since we need to know if it is compare partial or not
	 * and that we can't tell from the query keys. */
	DollarExistsQueryData *existsQueryData = extra_data != NULL ?
											 (DollarExistsQueryData *) extra_data[
		0] : NULL;

	if (existsQueryData != NULL)
	{
		/* $exists: true case. */
		return HandleConsistentExists(check, recheck, existsQueryData);
	}

	return HandleConsistentGreaterLessEquals(queryKeys, check, recheck,
											 indexClassOptions);
}


bool
GinBsonConsistentCore(BsonIndexStrategy strategy,
					  bool *check,
					  Pointer *extra_data,
					  int32_t numKeys,
					  bool *recheck,
					  Datum *queryKeys,
					  bytea *indexClassOptions)
{
	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_GREATER:
		case BSON_INDEX_STRATEGY_DOLLAR_LESS:
		{
			return HandleConsistentGreaterLess(check, recheck, numKeys, queryKeys);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_GT:
		case BSON_INDEX_STRATEGY_DOLLAR_NOT_LT:
		{
			bool greaterLessResult = HandleConsistentGreaterLess(check, recheck, numKeys,
																 queryKeys);
			if (*recheck)
			{
				/* Thunk to the runtime to handle this */
				return true;
			}

			return !greaterLessResult;
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
				*recheck = IsSerializedIndexTermTruncated(DatumGetByteaPP(queryKeys[0]));
				return check[0];
			}
			else
			{
				return HandleConsistentEqualsNull(check, recheck, indexClassOptions);
			}
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
		{
			return HandleConsistentGreaterEquals(extra_data, check, recheck, queryKeys,
												 indexClassOptions);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
		{
			return HandleConsistentGreaterLessEquals(queryKeys, check, recheck,
													 indexClassOptions);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_GTE:
		{
			bool result = HandleConsistentGreaterEquals(extra_data, check, recheck,
														queryKeys, indexClassOptions);
			if (*recheck)
			{
				return true;
			}

			return !result;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_LTE:
		{
			bool result = HandleConsistentGreaterLessEquals(queryKeys, check, recheck,
															indexClassOptions);
			if (*recheck)
			{
				return true;
			}

			return !result;
		}

		case BSON_INDEX_STRATEGY_DOLLAR_EXISTS:
		{
			return HandleConsistentExists(check, recheck,
										  (DollarExistsQueryData *) extra_data[0]);
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

				/* $eq/$in null. For new indexes we generate a non exists term,
				 * so if we match that and we don't match the root term, we don't need to recheck. */
				result = HandleConsistentArrayOpForEqualsNull(check, queryData, recheck,
															  indexClassOptions);
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

			/* There's 3 terms - 0: RootTerm, 1: NonExistsTerm, 2: The $eq term */
			BsonIndexTerm indexTerm;
			InitializeBsonIndexTerm(DatumGetByteaPP(queryKeys[0]), &indexTerm);

			if (indexTerm.element.bsonValue.value_type == BSON_TYPE_NULL)
			{
				bool result = HandleConsistentEqualsNull(check, recheck,
														 indexClassOptions);

				if (*recheck)
				{
					/* If we're being asked to reevaluate in the runtime, then thunk to runtime */
					return true;
				}

				/* Otherwise invert the $eq: null result */
				return !result;
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

				/* All documents that don't match the equality */
				*recheck = false;
				return !check[0];
			}
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_IN:
		{
			/* The last slot (that is, if n is the number of entries, then
			 * (n+1)th slot) will be storing the pointer to the info whether
			 * the $nin array has null or not */
			DollarArrayOpQueryData *queryData =
				(DollarArrayOpQueryData *) extra_data[numKeys];

			/* for $eq: null if it matches the non exists term, then it matched a doc with this path undefined. */

			bool res = check[queryData->rootTermIndex] ||
					   check[queryData->nonExistsTermIndex] ||
					   check[queryData->rootExistsTermIndex];
			bool mismatchOnTruncatedTerm = false;
			for (int i = 0; i < queryData->inTermCount; i++)
			{
				/* if any terms match, res is false. */
				if (check[i])
				{
					if (IsSerializedIndexTermTruncated(DatumGetByteaPP(queryKeys[i])))
					{
						mismatchOnTruncatedTerm = true;
					}

					res = false;
					break;
				}
			}

			if (res && queryData->arrayHasNull)
			{
				bool equalsNull = HandleConsistentArrayOpForEqualsNull(check, queryData,
																	   recheck,
																	   indexClassOptions);

				if (*recheck)
				{
					/* Since recheck was set - we move to the runtime */
					res = true;
				}
				else
				{
					res = !equalsNull;
				}
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
				 * we can match empty documents, we can skip recheck if we didn't match the root term and matched the non-exists term. */
				res = HandleConsistentArrayOpForEqualsNull(check, queryData, recheck,
														   indexClassOptions);
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
					/*
					 * Document is truncated in one or more terms,
					 * and the equality term could be a false positive.
					 * recheck in the runtime.
					 */
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
	context.termMetadata = GetIndexTermMetadata(options);

	bool addRootTerm = false;
	GenerateTerms(query, &context, addRootTerm);
	*nentries = context.totalTermCount;

	return context.terms.entries;
}


/*
 * Used as the term generation of the actual order by.
 * This matches all terms for that path. The root term
 * and non exists term is also included to ensure we capture documents
 * that don't have the path in them. Once we reindex old indexes to contain
 * the root term we can just generate the non exists term.
 */
Datum *
GinBsonExtractQueryOrderBy(PG_FUNCTION_ARGS)
{
	pgbson *query = PG_GETARG_PGBSON(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	bool **partialMatch = (bool **) PG_GETARG_POINTER(3);


	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	bytea *options = (bytea *) PG_GET_OPCLASS_OPTIONS();
	IndexTermCreateMetadata termMetadata = GetIndexTermMetadata(options);

	Datum *entries;
	pgbsonelement filterElement;
	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &filterElement); /* TODO: collation index support */
	}

	filterElement.bsonValue.value_type = BSON_TYPE_MINKEY;

	*nentries = 3;
	entries = (Datum *) palloc(sizeof(Datum) * 3);
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&filterElement,
														&termMetadata).indexTermVal);
	entries[1] = GenerateRootTerm(&termMetadata);
	entries[2] = GenerateRootNonExistsTerm(&termMetadata);

	*partialMatch = palloc0(sizeof(bool) * 3);
	(*partialMatch)[0] = true;
	(*partialMatch)[1] = false;
	(*partialMatch)[2] = false;
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
	context->hasArrayAncestors = false;

	int32_t initialIndex = context->index;

	bool inArrayContext = false;
	bool isArrayTerm = false;
	bool isCheckForArrayTermsWithNestedDocument = false;
	GenerateTermsCore(&bsonIterator, "", 0, inArrayContext,
					  isArrayTerm, context,
					  isCheckForArrayTermsWithNestedDocument);

	bool hasNoTerms = context->index == initialIndex;

	/* Add the path not found term if necessary
	 * we do this for back compat only when the index used the legacy not found term
	 */
	if (hasNoTerms && context->generateNotFoundTerm)
	{
		AddTerm(context, GeneratePathUndefinedTerm(context->options));
	}

	if (addRootTerm)
	{
		/* GUC to preserve back-compat behavior. */
		if (!EnableGenerateNonExistsTerm)
		{
			AddTerm(context, GenerateRootTerm(&context->termMetadata));
		}
		else if (!hasNoTerms)
		{
			AddTerm(context, GenerateRootExistsTerm(&context->termMetadata));
		}
		else
		{
			AddTerm(context, GenerateRootNonExistsTerm(&context->termMetadata));
		}

		if (context->hasTruncatedTerms)
		{
			AddTerm(context, GenerateRootTruncatedTerm(&context->termMetadata));
		}

		if (context->hasArrayAncestors)
		{
			AddTerm(context, GenerateRootMultiKeyTerm(&context->termMetadata));
		}
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
				if (inArrayContext || BSON_ITER_HOLDS_ARRAY(bsonIter))
				{
					/* Mark the path as having array ancestors leading to the index path */
					context->hasArrayAncestors = true;
				}

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
					&element, &context->termMetadata);
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
			if (inArrayContext && option == IndexTraverse_Match)
			{
				/* Mark the path as having array ancestors leading to the index path */
				context->hasArrayAncestors = true;
			}

			bson_iter_t containerIter;
			if (bson_iter_recurse(bsonIter, &containerIter))
			{
				bool inArrayContextInner = false;
				bool isArrayTermInner = false;
				bool isCheckForArrayTermsWithNestedDocumentInner = false;
				GenerateTermsCore(&containerIter, pathToInsert, pathtoInsertLength,
								  inArrayContextInner, isArrayTermInner,
								  context, isCheckForArrayTermsWithNestedDocumentInner);
			}
		}

		/* if we're already recursing because of an array's parent path term, */
		/* then we don't recurse into nested arrays. */
		/* if we're not in that case, we recurse down to produce inner terms of arrays. */
		if (!isArrayTerm)
		{
			if (BSON_ITER_HOLDS_ARRAY(bsonIter))
			{
				if (inArrayContext && option == IndexTraverse_Match)
				{
					/* Mark the path as having array ancestors leading to the index path */
					context->hasArrayAncestors = true;
				}

				bson_iter_t containerIter;

				/* Count the array terms - to pre-allocate the term Datum pointers */
				int32_t arrayCount = BsonDocumentValueCountKeys(bson_iter_value(
																	bsonIter));
				EnsureTermCapacity(context, arrayCount);

				if (bson_iter_recurse(bsonIter, &containerIter))
				{
					bool inArrayContextInner = true;
					bool isArrayTermInner = false;
					bool isCheckForArrayTermsWithNestedDocumentInner = false;
					GenerateTermsCore(&containerIter, pathToInsert, pathtoInsertLength,
									  inArrayContextInner, isArrayTermInner,
									  context,
									  isCheckForArrayTermsWithNestedDocumentInner);
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
					bool isCheckForArrayTermsWithNestedDocumentInner = inArrayContext;
					GenerateTermsCore(&containerIter, pathToInsert, pathtoInsertLength,
									  inArrayContextInner, isArrayTermInner,
									  context,
									  isCheckForArrayTermsWithNestedDocumentInner);
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
			bool isNegation = false;
			return GinBsonExtractQueryGreater(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_GREATER_EQUAL:
		{
			bool isNegation = false;
			return GinBsonExtractQueryGreaterEqual(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS:
		{
			bool isNegation = false;
			return GinBsonExtractQueryLess(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_LESS_EQUAL:
		{
			bool isNegation = false;
			return GinBsonExtractQueryLessEqual(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_GT:
		{
			bool isNegation = true;
			return GinBsonExtractQueryGreater(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_GTE:
		{
			bool isNegation = true;
			return GinBsonExtractQueryGreaterEqual(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_LT:
		{
			bool isNegation = true;
			return GinBsonExtractQueryLess(args, isNegation);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_LTE:
		{
			bool isNegation = true;
			return GinBsonExtractQueryLessEqual(args, isNegation);
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
 * Note: Make sure to at least palloc array to hold 4 elements
 */
static inline Datum *
GenerateNullEqualityIndexTerms(int32 *nentries, bool **partialmatch,
							   pgbsonelement *filterElement,
							   const IndexTermCreateMetadata *metadata)
{
	*nentries = 6;
	*partialmatch = NULL;
	Datum *entries = (Datum *) palloc(sizeof(Datum) * 6);

	/* the first term generated is the value itself (null) */
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(filterElement,
														metadata).indexTermVal);

	/* non exists term. */
	entries[1] = GenerateRootNonExistsTerm(metadata);

	/* The next term is undefined, we generate this for back compat but new indexes should only have the non exists term. */
	filterElement->bsonValue.value_type = BSON_TYPE_UNDEFINED;
	entries[2] = PointerGetDatum(SerializeBsonIndexTerm(filterElement,
														metadata).indexTermVal);

	/* the next term generated is the root exists term */
	entries[3] = GenerateRootExistsTerm(metadata);

	/* the next term is the array ancestor term */
	entries[4] = GenerateRootMultiKeyTerm(metadata);

	/* the next term generated is the root term for back-compatibility */
	entries[5] = GenerateRootTerm(metadata);

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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &filterElement); /* TODO: collation index support */
	}

	if (filterElement.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* special case for $eq on null. */
		entries = GenerateNullEqualityIndexTerms(nentries, partialmatch,
												 &filterElement, &args->termMetadata);
	}
	else
	{
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum));
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&filterElement,
															&args->termMetadata).
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


	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &filterElement); /* TODO: collation index support */
	}

	/* lowest possible values can be lowest of type double or binary */
	/* we are assuming (decimal128(nan) == double(nan)) < all possible numbers */
	pgbsonelement numberMinElement = filterElement;
	numberMinElement.bsonValue.value_type = BSON_TYPE_DOUBLE;
	numberMinElement.bsonValue.value.v_double = (double) NAN;

	*nentries = 3;
	entries = (Datum *) palloc(sizeof(Datum) * 3);

	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&numberMinElement,
														&args->termMetadata).
								 indexTermVal);

	pgbsonelement binaryMinElement = filterElement;
	binaryMinElement.bsonValue.value_type = BSON_TYPE_BINARY;
	binaryMinElement.bsonValue.value.v_binary.subtype = BSON_SUBTYPE_BINARY;
	binaryMinElement.bsonValue.value.v_binary.data_len = 0;
	entries[1] = PointerGetDatum(SerializeBsonIndexTerm(&binaryMinElement,
														&args->termMetadata).
								 indexTermVal);

	entries[2] = GenerateRootTruncatedTerm(&args->termMetadata);

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
	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(filter, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(filter, &filterElement); /* TODO: collation index support */
	}
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
																   &args->termMetadata);
	rangeValues->maxValueIndexTerm = maxSerialized.indexTermVal;

	pgbsonelement minElement =
	{
		.path = filterElement.path,
		.pathLength = filterElement.pathLength,
		.bsonValue = rangeValues->params.minValue
	};
	BsonIndexTermSerialized minSerialized = SerializeBsonIndexTerm(&minElement,
																   &args->termMetadata);
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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(emptyArrayBson, &minElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(emptyArrayBson, &minElement); /* TODO: collation index support */
	}

	entries[1] = PointerGetDatum(SerializeBsonIndexTerm(&minElement,
														&args->termMetadata).indexTermVal);
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
	Datum *entries = (Datum *) palloc(sizeof(Datum) * 6);

	pgbsonelement element;
	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &element);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &element); /* TODO: collation index support */
	}

	if (element.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* $ne: null - we use $eq null and invert the set */
		return GenerateNullEqualityIndexTerms(nentries, args->partialmatch,
											  &element, &args->termMetadata);
	}

	*nentries = 4;
	BsonIndexTermSerialized serialized = SerializeBsonIndexTerm(&element,
																&args->termMetadata);
	entries[0] = PointerGetDatum(serialized.indexTermVal);
	entries[1] = GenerateRootExistsTerm(&args->termMetadata);
	entries[2] = GenerateRootNonExistsTerm(&args->termMetadata);
	entries[3] = GenerateRootTerm(&args->termMetadata);
	return entries;
}


/*
 * Adds the Root terms for negation scenarios into the entries.
 */
static void
GenerateNegationTerms(Datum *entries, int nextIndex,
					  bool *partialMatch,
					  IndexTermCreateMetadata *termMetadata)
{
	/* non exists term. */
	entries[nextIndex] = GenerateRootNonExistsTerm(termMetadata);
	partialMatch[nextIndex] = false;
	nextIndex++;

	/* the next term generated is the root exists term */
	entries[nextIndex] = GenerateRootExistsTerm(termMetadata);
	partialMatch[nextIndex] = false;
	nextIndex++;

	/* the next term generated is the root term for back-compatibility */
	entries[nextIndex] = GenerateRootTerm(termMetadata);
	partialMatch[nextIndex] = false;
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
GinBsonExtractQueryGreaterEqual(BsonExtractQueryArgs *args, bool isNegation)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Datum *entries;

	pgbsonelement element;
	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &element);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &element); /* TODO: collation index support */
	}
	if (element.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* special case for $gte: null */
		return GenerateNullEqualityIndexTerms(nentries, partialmatch, &element,
											  &args->termMetadata);
	}

	if (element.bsonValue.value_type == BSON_TYPE_MINKEY)
	{
		/* special case for $exists: true */
		bool isPositiveExists = true;
		return GenerateExistsEqualityTerms(nentries, partialmatch, args->extra_data,
										   &element, isPositiveExists, args->options,
										   &args->termMetadata);
	}

	int numTerms = isNegation ? 5 : 2;
	entries = (Datum *) palloc(sizeof(Datum) * numTerms);
	*partialmatch = (bool *) palloc(sizeof(bool) * numTerms);
	*nentries = 2;

	BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																	&args->
																	termMetadata);
	entries[0] = PointerGetDatum(serializedTerm.indexTermVal);
	(*partialmatch)[0] = true;

	/* Add an equals match for $gte */
	entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
	(*partialmatch)[1] = false;

	if (isNegation)
	{
		GenerateNegationTerms(entries, 2, *partialmatch, &args->termMetadata);
		*nentries = *nentries + 3;
	}

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
GinBsonExtractQueryGreater(BsonExtractQueryArgs *args, bool isNegation)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Datum *entries;

	pgbsonelement element;

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &element);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &element); /* TODO: collation index support */
	}

	int numTerms = isNegation ? 5 : 2;
	entries = (Datum *) palloc(sizeof(Datum) * numTerms);
	*partialmatch = (bool *) palloc(sizeof(bool) * numTerms);
	*nentries = 1;

	BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																	&args->
																	termMetadata);
	entries[0] = PointerGetDatum(serializedTerm.indexTermVal);
	(*partialmatch)[0] = true;

	if (serializedTerm.isIndexTermTruncated || isNegation)
	{
		/* Add the truncated term */
		*nentries = 2;
		entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
		(*partialmatch)[1] = false;
	}

	if (isNegation)
	{
		GenerateNegationTerms(entries, *nentries, *partialmatch, &args->termMetadata);
		*nentries += 3;
	}

	return entries;
}


static Datum *
GinBsonExtractQueryLessEqual(BsonExtractQueryArgs *args, bool isNegation)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	Datum *entries;
	pgbsonelement documentElement;
	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &documentElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &documentElement); /* TODO: collation index support */
	}

	/* Clone it for now */
	pgbsonelement queryElement = documentElement;

	if (documentElement.bsonValue.value_type == BSON_TYPE_NULL)
	{
		/* special case for $lte: null */
		entries = GenerateNullEqualityIndexTerms(nentries, partialmatch,
												 &documentElement, &args->termMetadata);
	}
	else
	{
		int numTerms = isNegation ? 5 : 2;
		entries = (Datum *) palloc(sizeof(Datum) * numTerms);
		*partialmatch = (bool *) palloc(sizeof(bool) * numTerms);
		*extra_data = (Pointer *) palloc(sizeof(Pointer) * numTerms);
		*nentries = 2;
		(*partialmatch)[0] = true;

		/* Also serialize the actual index term */
		BsonIndexTermSerialized serializedTerm =
			SerializeBsonIndexTerm(&queryElement, &args->termMetadata);

		/* store the query value in extra data - this allows us to compute upper bound using */
		/* this extra value. */
		(*extra_data)[0] = (Pointer) serializedTerm.indexTermVal;

		/* now create a bson for that path which has the min value for the field */
		/* Today we set this as the min value for the equivalent */
		/* type (i.e. the lowest possible value for that type OR the highest possible value of the prior type) */
		/* When we don't have the table of these values, we safely start out with Minkey. */
		documentElement.bsonValue = GetLowerBoundForLessThan(
			queryElement.bsonValue.value_type);
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
															&args->termMetadata).
									 indexTermVal);

		/* In the case where we have truncation, also track equality on $lt */
		(*partialmatch)[1] = false;
		(*extra_data)[1] = NULL;
		entries[1] = PointerGetDatum(serializedTerm.indexTermVal);

		if (isNegation)
		{
			GenerateNegationTerms(entries, 2, *partialmatch, &args->termMetadata);
			*nentries = 5;
		}
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
GinBsonExtractQueryLess(BsonExtractQueryArgs *args, bool isNegation)
{
	pgbson *query = args->query;
	int32 *nentries = args->nentries;
	bool **partialmatch = args->partialmatch;
	Pointer **extra_data = args->extra_data;
	Datum *entries;
	pgbsonelement documentElement;

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &documentElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &documentElement); /* TODO: collation index support */
	}

	/* Clone it for now */
	pgbsonelement queryElement = documentElement;

	int numEntries = isNegation ? 5 : 2;
	entries = (Datum *) palloc(sizeof(Datum) * numEntries);
	*partialmatch = (bool *) palloc(sizeof(bool) * numEntries);
	*nentries = 1;
	(*partialmatch)[0] = true;

	/* store the query value in extra data - this allows us to compute upper bound using */
	/* this extra value. */

	/* Also serialize the actual index term */
	BsonIndexTermSerialized serializedTerm =
		SerializeBsonIndexTerm(&queryElement, &args->termMetadata);

	*extra_data = (Pointer *) palloc0(sizeof(Pointer) * numEntries);
	(*extra_data)[0] = (Pointer) serializedTerm.indexTermVal;

	/* now create a bson for that path which has the min value for the field */
	/* Today we set this as the min value for the equivalent */
	/* type (i.e. the lowest possible value for that type OR the highest possible value of the prior type) */
	/* When we don't have the table of these values, we safely start out with Minkey. */
	documentElement.bsonValue = GetLowerBoundForLessThan(
		queryElement.bsonValue.value_type);
	entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
														&args->termMetadata).
								 indexTermVal);

	/* In the case where we have truncation, also track equality on $lt */
	if (serializedTerm.isIndexTermTruncated || isNegation)
	{
		*nentries = 2;
		(*partialmatch)[1] = false;
		entries[1] = PointerGetDatum(serializedTerm.indexTermVal);
	}

	if (isNegation)
	{
		GenerateNegationTerms(entries, *nentries, *partialmatch, &args->termMetadata);
		*nentries += 3;
	}

	return entries;
}


/*
 * Term generation for $eq: null type operators that take
 * arrays - this covers $in and $all scenarios
 */
inline static int
AddNullArrayOpTerms(Datum *entries, int index,
					DollarArrayOpQueryData *inQueryData,
					pgbsonelement *queryElement,
					IndexTermCreateMetadata *termMetadata)
{
	entries[index] = GenerateRootTerm(termMetadata);
	inQueryData->rootTermIndex = index;
	index++;

	entries[index] = GenerateRootExistsTerm(termMetadata);
	inQueryData->rootExistsTermIndex = index;
	index++;

	entries[index] = GenerateRootNonExistsTerm(termMetadata);
	inQueryData->nonExistsTermIndex = index;
	index++;

	entries[index] = GenerateRootMultiKeyTerm(termMetadata);
	inQueryData->multiKeyTermIndex = index;
	index++;

	pgbsonelement undefinedElement;
	undefinedElement.path = queryElement->path;
	undefinedElement.pathLength = queryElement->pathLength;
	undefinedElement.bsonValue.value_type = BSON_TYPE_UNDEFINED;
	BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&undefinedElement,
																	termMetadata);
	entries[index] = PointerGetDatum(serializedTerm.indexTermVal);
	inQueryData->undefinedLiteralTermIndex = index;
	index++;

	return index;
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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(inArray, &queryElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(inArray, &queryElement); /* TODO: collation index support */
	}

	if (queryElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
								"cannot nest $ under $in")));
		}

		inArraySize++;
		arrayHasNull = arrayHasNull || arrayValue->value_type == BSON_TYPE_NULL;
	}

	if (arrayHasNull)
	{
		/* one for undefined, root term, root Exists, the non exists term, and multi-key. */
		inArraySize += 5;
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
																		&args->
																		termMetadata);
		entries[index] = PointerGetDatum(serializedTerm.indexTermVal);

		inQueryData->arrayHasTruncation = inQueryData->arrayHasTruncation ||
										  serializedTerm.isIndexTermTruncated;
		index++;
	}

	inQueryData->inTermCount = index;
	if (arrayHasNull)
	{
		index = AddNullArrayOpTerms(entries, index, inQueryData, &queryElement,
									&args->termMetadata);
	}

	if (inQueryData->arrayHasRegex ||
		inQueryData->arrayHasTruncation)
	{
		entries[index] = GenerateRootTruncatedTerm(&args->termMetadata);
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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(inArray, &queryElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(inArray, &queryElement); /* TODO: collation index support */
	}

	if (queryElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
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
			ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
								"cannot nest $ under $in")));
		}
		termCount++;
	}

	/* we generate 5 more possible terms 1 for the root term, root exists term,
	 * the non exists term, multi-key term and the truncated term if needed.
	 * We also may add the literal undefined if there's $ne null.
	 */
	termCount += 6;
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
																	&args->
																	termMetadata);
		entries[index] = PointerGetDatum(serialized.indexTermVal);

		queryData->arrayHasNull = queryData->arrayHasNull ||
								  element.bsonValue.value_type == BSON_TYPE_NULL;
		queryData->arrayHasTruncation = serialized.isIndexTermTruncated;

		index++;
	}

	queryData->inTermCount = index;

	queryData->rootTermIndex = index;
	entries[index++] = GenerateRootTerm(&args->termMetadata);
	queryData->nonExistsTermIndex = index;
	entries[index++] = GenerateRootNonExistsTerm(&args->termMetadata);
	queryData->rootExistsTermIndex = index;
	entries[index++] = GenerateRootExistsTerm(&args->termMetadata);
	queryData->multiKeyTermIndex = index;
	entries[index++] = GenerateRootMultiKeyTerm(&args->termMetadata);
	if (queryData->arrayHasNull)
	{
		pgbsonelement undefinedElement;
		undefinedElement.path = queryElement.path;
		undefinedElement.pathLength = queryElement.pathLength;
		undefinedElement.bsonValue.value_type = BSON_TYPE_UNDEFINED;

		queryData->undefinedLiteralTermIndex = index;
		BsonIndexTermSerialized serialized = SerializeBsonIndexTerm(&undefinedElement,
																	&args->
																	termMetadata);
		entries[index++] = PointerGetDatum(serialized.indexTermVal);
	}

	if (queryData->arrayHasTruncation)
	{
		entries[index] = GenerateRootTruncatedTerm(&args->termMetadata);
		queryData->rootTruncatedTermIndex = index;
		index++;
	}

	*nentries = index;
	Assert(index <= termCount);

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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &queryElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &queryElement); /* TODO: collation index support */
	}

	if ((queryElement.bsonValue.value_type != BSON_TYPE_UTF8) &&
		(queryElement.bsonValue.value_type != BSON_TYPE_REGEX))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
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
														&args->termMetadata).
								 indexTermVal);

	entries[1] = GenerateRootTruncatedTerm(&args->termMetadata);

	if (isRegexValue)
	{
		/* Also match for the regex itself */
		entries[2] = PointerGetDatum(SerializeBsonIndexTerm(&queryElement,
															&args->termMetadata).
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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &filterElement); /* TODO: collation index support */
	}

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
														&args->termMetadata).
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
	pgbsonelement filterElement;

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &filterElement); /* TODO: collation index support */
	}

	bool existsPositiveMatch = true;

	/**
	 * TODO: FIXME - Verify whether strict number check is required here
	 * */
	if (BsonValueIsNumberOrBool(&filterElement.bsonValue) &&
		BsonValueAsInt64(&filterElement.bsonValue) == 0)
	{
		existsPositiveMatch = false;
	}

	return GenerateExistsEqualityTerms(args->nentries, args->partialmatch,
									   args->extra_data, &filterElement,
									   existsPositiveMatch, args->options,
									   &args->termMetadata);
}


static Datum *
GenerateExistsEqualityTerms(int32 *nentries, bool **partialmatch, Pointer **extraData,
							pgbsonelement *filterElement, bool isPositiveExists,
							const void *indexOptions,
							const IndexTermCreateMetadata *metadata)
{
	Datum *entries;

	/* Only single path non-wildcard indexes can know if the path exists or not, therefore we can trust the non-exists term.
	 * Otherwise we need to do a partial match with MinKey. */
	bool isComparePartial = IsWildcardIndex(indexOptions);

	DollarExistsQueryData *existsQueryData = (DollarExistsQueryData *) palloc0(
		sizeof(DollarExistsQueryData));
	existsQueryData->isComparePartial = isComparePartial;
	existsQueryData->isPositiveExists = isPositiveExists;

	if (!existsQueryData->isComparePartial)
	{
		/* Even though we're only setting one value in extra data, the contract in GIN/RUM is to allocate the same size of entries. */
		*extraData = (Pointer *) palloc(sizeof(Pointer) * 2);
		(*extraData)[0] = (Pointer) existsQueryData;

		*nentries = 2;
		entries = (Datum *) palloc(sizeof(Datum) * 2);
		*partialmatch = (bool *) palloc(sizeof(bool) * 2);

		entries[0] = isPositiveExists ? GenerateRootExistsTerm(metadata) :
					 GenerateRootNonExistsTerm(metadata);
		(*partialmatch)[0] = false;

		/* Left here for back compat */
		entries[1] = GenerateRootTerm(metadata);
		(*partialmatch)[1] = false;

		return entries;
	}

	pgbsonelement documentElement;
	documentElement.path = filterElement->path;
	documentElement.pathLength = filterElement->pathLength;
	documentElement.bsonValue.value_type = BSON_TYPE_MINKEY;

	/* for partial match (wildcard indexes) we treat $exists: true as gte MinKey and not undefined. */
	if (isPositiveExists)
	{
		*extraData = (Pointer *) palloc(sizeof(Pointer) * 1);
		*nentries = 1;
		entries = (Datum *) palloc(sizeof(Datum) * 1);
		*partialmatch = (bool *) palloc(sizeof(bool) * 1);
		**partialmatch = true;
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
															metadata).
									 indexTermVal);
	}
	else
	{
		/* Even though we're only setting one value in extra data, the contract in GIN/RUM is to allocate the same size of entries. */
		*extraData = (Pointer *) palloc(sizeof(Pointer) * 4);
		*nentries = 4;
		entries = (Datum *) palloc(sizeof(Datum) * 4);
		*partialmatch = (bool *) palloc(sizeof(bool) * 4);

		/* for wildcard indexes do partial match since we can't generate a non exists term there. */
		/* first term is the exists term. */
		(*partialmatch)[0] = true;
		entries[0] = PointerGetDatum(SerializeBsonIndexTerm(&documentElement,
															metadata).
									 indexTermVal);

		/* second term is the root term. */
		(*partialmatch)[1] = false;
		entries[1] = GenerateRootTerm(metadata);

		/* We match both exists/nonexists since for a wildcard index we need *all* documents
		 * to generate the inverse match on the Exists. Note that:
		 * NumDocs(RootTerm) + NumDocs(NonExistsTerm) + NumDocs(RootExists) == TotalDocs
		 * Since the Root term is only for legacy docs.
		 */

		/* third term is the non-exists term. */
		(*partialmatch)[2] = false;
		entries[2] = GenerateRootNonExistsTerm(metadata);

		/* third term is the exists term. */
		(*partialmatch)[3] = false;
		entries[3] = GenerateRootExistsTerm(metadata);
	}

	(*extraData)[0] = (Pointer) existsQueryData;

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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &documentElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &documentElement); /* TODO: collation index support */
	}

	/* store the query value in extra data - this allows us to compute upper bound using */
	/* this extra value. */
	*extra_data = (Pointer *) palloc(sizeof(int64_t));
	*((int64_t *) *extra_data) = BsonValueAsInt64(&documentElement.bsonValue);

	/* now create a bson for that path which has the min value for the field */
	/* we map this to an empty array since that's the smallest value we'll encounter */
	entries[0] = GenerateEmptyArrayTerm(&documentElement, &args->termMetadata);
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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &documentElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &documentElement); /* TODO: collation index support */
	}

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
														&args->termMetadata).
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

	if (EnableCollation)
	{
		PgbsonToSinglePgbsonElementWithCollation(query, &filterElement);
	}
	else
	{
		PgbsonToSinglePgbsonElement(query, &filterElement); /* TODO: collation index support */
	}

	if (filterElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
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
	 * and a non-exists term to match them. */
	if (arrayHasOnlyNull)
	{
		termCount += 5;
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
																			&args->
																			termMetadata);
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
			entries[index] = GenerateEmptyArrayTerm(&element, &args->termMetadata);
			queryData->arrayHasElemMatch = true;
		}
		else
		{
			BsonIndexTermSerialized serializedTerm = SerializeBsonIndexTerm(&element,
																			&args->
																			termMetadata);
			entries[index] = PointerGetDatum(serializedTerm.indexTermVal);
			queryData->arrayHasTruncation = queryData->arrayHasTruncation ||
											serializedTerm.isIndexTermTruncated;
		}

		index++;
	}

	queryData->inTermCount = index;

	if (arrayHasOnlyNull)
	{
		index = AddNullArrayOpTerms(entries, index, queryData, &filterElement,
									&args->termMetadata);
	}

	if (queryData->arrayHasTruncation ||
		queryData->arrayHasRegex ||
		queryData->arrayHasElemMatch)
	{
		entries[index] = GenerateRootTruncatedTerm(&args->termMetadata);
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

	IndexTermCreateMetadata termMetadata = GetIndexTermMetadata(options);

	pgbsonelement element = { 0 };
	element.path = "";
	element.pathLength = 0;
	element.bsonValue.value_type = BSON_TYPE_NULL;
	return PointerGetDatum(SerializeBsonIndexTerm(&element, &termMetadata).indexTermVal);
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
		case BSON_INDEX_STRATEGY_DOLLAR_NOT_GT:
		case BSON_INDEX_STRATEGY_DOLLAR_NOT_GTE:
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

		case BSON_INDEX_STRATEGY_DOLLAR_NOT_LT:
		case BSON_INDEX_STRATEGY_DOLLAR_NOT_LTE:
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

			if (EnableCollation)
			{
				PgbsonToSinglePgbsonElementWithCollation(modArrayData, &modArray);
			}
			else
			{
				PgbsonToSinglePgbsonElement(modArrayData, &modArray); /* TODO: collation index support */
			}
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
			if (EnableCollation)
			{
				PgbsonToSinglePgbsonElementWithCollation(queryBson, &filterElement);
			}
			else
			{
				PgbsonToSinglePgbsonElement(queryBson, &filterElement); /* TODO: collation index support */
			}
			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAllClear);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_CLEAR:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;

			if (EnableCollation)
			{
				PgbsonToSinglePgbsonElementWithCollation(queryBson, &filterElement);
			}
			else
			{
				PgbsonToSinglePgbsonElement(queryBson, &filterElement); /* TODO: collation index support */
			}

			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAnyClear);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ALL_SET:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;

			if (EnableCollation)
			{
				PgbsonToSinglePgbsonElementWithCollation(queryBson, &filterElement);
			}
			else
			{
				PgbsonToSinglePgbsonElement(queryBson, &filterElement); /* TODO: collation index support */
			}

			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAllSet);
		}

		case BSON_INDEX_STRATEGY_DOLLAR_BITS_ANY_SET:
		{
			pgbson *queryBson = (pgbson *) extraData;
			pgbsonelement filterElement;

			if (EnableCollation)
			{
				PgbsonToSinglePgbsonElementWithCollation(queryBson, &filterElement);
			}
			else
			{
				PgbsonToSinglePgbsonElement(queryBson, &filterElement); /* TODO: collation index support */
			}

			return GinBsonComparePartialBitsWiseOperator(queryValue, compareValue,
														 &filterElement,
														 CompareArrayForBitsAnySet);
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg("compare_partial_bson: Unsupported strategy %d",
								   strategy),
							errdetail_log("compare_partial_bson: Unsupported strategy %d",
										  strategy)));
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
GenerateEmptyArrayTerm(pgbsonelement *filterElement,
					   const IndexTermCreateMetadata *metadata)
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
	return PointerGetDatum(SerializeBsonIndexTerm(&termElement, metadata).indexTermVal);
}


/*
 * Given a bson type returns the lowest value for that type bracket
 * for the sort order type.
 */
static bson_value_t
GetLowerBoundForLessThan(bson_type_t inputBsonType)
{
	bson_value_t minValue = { 0 };
	minValue.value_type = BSON_TYPE_MINKEY;
	switch (inputBsonType)
	{
		case BSON_TYPE_EOD:
		case BSON_TYPE_MINKEY:
		{
			/* The minValue for MinKey is MinKey */
			return minValue;
		}

		case BSON_TYPE_UNDEFINED:
		case BSON_TYPE_NULL:
		{
			/* Use MinKey here since null has custom semantics */
			return minValue;
		}

		case BSON_TYPE_DOUBLE:
		case BSON_TYPE_INT32:
		case BSON_TYPE_INT64:
		case BSON_TYPE_DECIMAL128:
		{
			minValue.value_type = BSON_TYPE_DOUBLE;
			minValue.value.v_double = NAN;
			return minValue;
		}

		case BSON_TYPE_UTF8:
		case BSON_TYPE_SYMBOL:
		{
			minValue.value_type = inputBsonType;
			minValue.value.v_utf8.len = 0;
			minValue.value.v_utf8.str = "";
			return minValue;
		}

		case BSON_TYPE_DOCUMENT:
		{
			/* Return an empty document */
			pgbson *bson = PgbsonInitEmpty();
			return ConvertPgbsonToBsonValue(bson);
		}

		case BSON_TYPE_ARRAY:
		{
			pgbson_writer bsonWriter;

			/* now create a bson for that path which has the min value for the field */
			/* we map this to an empty array since that's the smallest value we'll encounter */
			PgbsonWriterInit(&bsonWriter);
			PgbsonWriterAppendEmptyArray(&bsonWriter, "", 0);
			pgbson *doc = PgbsonWriterGetPgbson(&bsonWriter);

			pgbsonelement termElement;
			PgbsonToSinglePgbsonElement(doc, &termElement);
			return termElement.bsonValue;
		}

		case BSON_TYPE_BOOL:
		{
			minValue.value_type = BSON_TYPE_BOOL;
			minValue.value.v_bool = false;
			return minValue;
		}

		case BSON_TYPE_DATE_TIME:
		{
			minValue.value_type = BSON_TYPE_DATE_TIME;
			minValue.value.v_datetime = INT64_MIN;
			return minValue;
		}

		case BSON_TYPE_MAXKEY:
		{
			/* MaxKey has custom semantics that does cross-type comparisons */
			return minValue;
		}

		case BSON_TYPE_BINARY:
		case BSON_TYPE_OID:
		case BSON_TYPE_TIMESTAMP:
		case BSON_TYPE_REGEX:
		case BSON_TYPE_DBPOINTER:
		case BSON_TYPE_CODE:
		case BSON_TYPE_CODEWSCOPE:
		{
			/* Thunk to minkey until we have something better */
			return minValue;
		}

		default:
		{
			/* not known, use minkey */
			return minValue;
		}
	}
}
