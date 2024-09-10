/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/bson_hashed_gin.c
 *
 * Gin operator implementations of BSON Hash indexing.
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <miscadmin.h>
#include <fmgr.h>
#include <string.h>
#include <access/reloptions.h>

#include "io/helio_bson_core.h"
#include "opclass/helio_gin_common.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "utils/helio_errors.h"
#include "io/bson_traversal.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

/*
 * Intermediate state for hash indexes while traversing the bson
 * document.
 */
typedef struct HashIndexTraverseState
{
	const char *indexPath;
	bool foundValue;
	bson_value_t bsonValue;
} HashIndexTraverseState;

static void ValidateHashedPathSpec(const char *prefix);
static void ThrowErrorArraysNotSupported(const char *path, int pathLength);

static Datum * GenerateTermsForDollarEqual(pgbsonelement *queryElement, int *nentries);
static Datum * GenerateTermsForDollarIn(pgbsonelement *queryElement, int *nentries);

static bool HashIndexVisitTopLevelField(pgbsonelement *element, const
										StringView *filterPath,
										void *state);
static bool HashIndexVisitArrayField(pgbsonelement *element, const StringView *filterPath,
									 int
									 arrayIndex, void *state);
static bool HashIndexContinueProcessIntermediateArray(void *state, const
													  bson_value_t *value);

/*
 * Extension functions for handling hash index execution when traversing bson documents.
 */
static const TraverseBsonExecutionFuncs HashIndexExecutionFuncs = {
	.ContinueProcessIntermediateArray = HashIndexContinueProcessIntermediateArray,
	.SetTraverseResult = NULL,
	.VisitArrayField = HashIndexVisitArrayField,
	.VisitTopLevelField = HashIndexVisitTopLevelField,
	.SetIntermediateArrayIndex = NULL,
};

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(gin_bson_hashed_extract_value);
PG_FUNCTION_INFO_V1(gin_bson_hashed_options);
PG_FUNCTION_INFO_V1(gin_bson_hashed_extract_query);
PG_FUNCTION_INFO_V1(gin_bson_hashed_consistent);

/*
 * gin_bson_hashed_extract_query is run on the query path when a
 * a dollar equal predicate can be pushed to the index.
 * The method then returns the query hash value as the term if the query can be pushed to
 * the index. If the query value is null, we generate two terms, the hash for null and a 0 as
 * a root term to match against documents where the path doesn't exist.
 * For more details see documentation on the 'extractQuery' method in the GIN extensibility.
 */
Datum
gin_bson_hashed_extract_query(PG_FUNCTION_ARGS)
{
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	StrategyNumber strategy = PG_GETARG_UINT16(2);

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	Datum query = PG_GETARG_DATUM(0);
	bytea *options = (bytea *) PG_GET_OPCLASS_OPTIONS();
	if (!ValidateIndexForQualifierValue(options, query, strategy))
	{
		*nentries = 0;

		/* Note: we don't use PG_RETURN_NULL here since fmgr complains
		 * even theough the contract here is that when nentries is 0, we return null
		 * See for instance gin_extract_jsonb_query in the Postgres codebase */
		PG_RETURN_POINTER(NULL);
	}

	pgbson *queryBson = DatumGetPgBson(query);
	pgbsonelement queryElement;
	PgbsonToSinglePgbsonElement(queryBson, &queryElement);

	if (strategy == BSON_INDEX_STRATEGY_DOLLAR_EQUAL)
	{
		PG_RETURN_POINTER(GenerateTermsForDollarEqual(&queryElement, nentries));
	}
	else if (strategy == BSON_INDEX_STRATEGY_DOLLAR_IN)
	{
		PG_RETURN_POINTER(GenerateTermsForDollarIn(&queryElement, nentries));
	}
	else
	{
		ereport(ERROR, (errmsg("Invalid strategy number %d", strategy)));
	}
}


/*
 * gin_bson_hashed_extract_value is run on the insert/update path and collects the terms
 * that will be indexed for hashed indexes. The method provides the bson document as an input, and
 * returns 1 term containing the hashed value of the path that was specified to be index in the
 * input bson. If the path to index is not found we return a 0 value as the hash representing a
 * root term in order to match null values as the query input against non existent paths in documents.
 * For more details see documentation on the 'extractValue' method in the GIN extensibility.
 */
Datum
gin_bson_hashed_extract_value(PG_FUNCTION_ARGS)
{
	pgbson *bson = PG_GETARG_PGBSON_PACKED(0);
	int32_t *nentries = (int32_t *) PG_GETARG_POINTER(1);

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	BsonGinHashOptions *options =
		(BsonGinHashOptions *) PG_GET_OPCLASS_OPTIONS();

	const char *indexPath;
	uint32_t indexPathLength;
	Get_Index_Path_Option(options, path, indexPath, indexPathLength);

	if (indexPathLength == 0)
	{
		*nentries = 0;
		PG_RETURN_POINTER(NULL);
	}

	bson_iter_t bsonIterator;
	PgbsonInitIterator(bson, &bsonIterator);

	HashIndexTraverseState traverseState = { 0 };
	traverseState.indexPath = indexPath;
	TraverseBson(&bsonIterator, indexPath, &traverseState, &HashIndexExecutionFuncs);

	Datum *entries = (Datum *) palloc(sizeof(Datum));
	*nentries = 1;

	if (!traverseState.foundValue || traverseState.bsonValue.value_type ==
		BSON_TYPE_UNDEFINED)
	{
		/* If we didn't find the path to index or the value at the path is undefined,
		 * we return 0 to be able to match this document on $eq: null queries. */
		entries[0] = Int64GetDatum(0);
		PG_RETURN_POINTER(entries);
	}

	/* we should get the hash and return it */
	entries[0] = Int64GetDatum(BsonValueHash(&traverseState.bsonValue, 0));

	PG_FREE_IF_COPY(bson, 0);
	PG_RETURN_POINTER(entries);
}


/*
 * gin_bson_hashed_options sets up the option specification for hashed indexes
 * This initializes the structure that is used by the Index AM to process user specified
 * options on how to handle documents with the index.
 * For hashed indexes we only need to track the path being indexed.
 * usage is as: using gin(document bson_gin_hashed_ops(path='a.b'))
 * For more details see documentation on the 'options' method in the GIN extensibility.
 */
Datum
gin_bson_hashed_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(BsonGinHashOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_Hashed, /* default value */
							IndexOptionsType_Hashed, /* min */
							IndexOptionsType_Hashed, /* max */
							offsetof(BsonGinHashOptions, base.type));
	add_local_int_reloption(relopts, "version",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V0,         /* max */
							offsetof(BsonGinHashOptions, base.version));
	add_local_string_reloption(relopts, "path",
							   "Prefix path for the index",
							   NULL, &ValidateHashedPathSpec, &FillSinglePathSpec,
							   offsetof(BsonGinHashOptions, path));

	add_local_string_reloption(relopts, "indexname",
							   "The mongo specific name for the index",
							   NULL, NULL, &FillDeprecatedStringSpec,
							   offsetof(BsonGinHashOptions, base.intOption_deprecated));
	add_local_int_reloption(relopts, "indextermsize",
							"[deprecated] The index term size limit for truncation",
							-1, /* default value */
							-1, /* min */
							-1, /* max: hashed index terms shouldn't be truncated. */
							offsetof(BsonGinHashOptions, base.intOption_deprecated));
	add_local_int_reloption(relopts, "ts",
							"[deprecated] The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinHashOptions, base.intOption_deprecated));
	PG_RETURN_VOID();
}


/*
 * gin_bson_hashed_consistent validates whether a given match on a key
 * can be used to satisfy a query. We can't fully trust the result if true in
 * the check array which is the result of the comparison of two hashes (index term and query term)
 * because there can be collisions when calculating hashes, so we always need to
 * recheck the query at runtime. For null values we force the recheck even if the result
 * it false, because we need to consider the mongo scenario of a missing path == null.
 * For more details see documentation on the 'consistent' method in the GIN extensibility.
 */
Datum
gin_bson_hashed_consistent(PG_FUNCTION_ARGS)
{
	StrategyNumber strategy = PG_GETARG_UINT16(1);

	bool *check = (bool *) PG_GETARG_POINTER(0);
	int32_t numKeys = (int32_t) PG_GETARG_INT32(3);
	bool *recheck = (bool *) PG_GETARG_POINTER(5);
	bool res;

	/* we set recheck to true always since having 2 identical hashes doesn't mean
	 * the elements are equal, there could be a hash collition.
	 */
	*recheck = true;

	switch (strategy)
	{
		case BSON_INDEX_STRATEGY_DOLLAR_EQUAL:
		case BSON_INDEX_STRATEGY_DOLLAR_IN:
		{
			res = false;
			for (int i = 0; i < numKeys && !res; i++)
			{
				res = res || check[i];
			}

			break;
		}

		default:
		{
			ereport(ERROR, errmsg("Invalid strategy number %d", strategy));
		}
	}

	PG_RETURN_BOOL(res);
}


/*
 * Generates the index hash terms for a given query predicate for a $eq.
 * This computes the hash, or also generates the "root" term if the $eq
 * is on null.
 * Updates nentries to the number of terms generated.
 */
static Datum *
GenerateTermsForDollarEqual(pgbsonelement *queryElement, int *nentries)
{
	if (queryElement->bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		/* hashed index doesn't support array types, so we just don't generate terms */
		*nentries = 0;
		return NULL;
	}

	/* get hash value for the query to match against indexed terms */
	int64 hash = BsonValueHash(&queryElement->bsonValue, 0);

	bool hasNull = queryElement->bsonValue.value_type == BSON_TYPE_NULL;
	Datum *entries;
	if (hasNull)
	{
		/* In the case of $eq: null we return a root term to match missing paths */
		entries = (Datum *) palloc(sizeof(Datum) * 2);
		*nentries = 2;
		entries[0] = Int64GetDatum(0);
		entries[1] = Int64GetDatum(hash);
	}
	else
	{
		entries = (Datum *) palloc(sizeof(Datum) * 1);
		*nentries = 1;
		entries[0] = Int64GetDatum(hash);
	}

	return entries;
}


/*
 * Generates the index hash terms for a given query predicate for a $in.
 * This generates N terms, each of which are the hash,
 * or also generates the "root" term if the $in contains null.
 *
 * Updates nentries to the number of terms generated (N or N+1).
 */
static Datum *
GenerateTermsForDollarIn(pgbsonelement *queryElement, int *nentries)
{
	if (queryElement->bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg("$in expects an array")));
	}

	bson_iter_t queryInIterator;
	if (!bson_iter_init_from_data(&queryInIterator,
								  queryElement->bsonValue.value.v_doc.data,
								  queryElement->bsonValue.value.v_doc.data_len))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_FAILEDTOPARSE),
						errmsg("Unable to read array for $in")));
	}

	bool arrayHasNull = false;
	while (bson_iter_next(&queryInIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&queryInIterator);
		if (arrayValue->value_type == BSON_TYPE_ARRAY)
		{
			/* hashed index doesn't support array types, so we just don't generate terms */
			*nentries = 0;
			return NULL;
		}

		arrayHasNull = arrayHasNull || arrayValue->value_type == BSON_TYPE_NULL;
		(*nentries)++;
	}

	if (arrayHasNull)
	{
		(*nentries)++;
	}

	/* now generate terms */
	Datum *entries = (Datum *) palloc(sizeof(Datum) * *nentries);
	bson_iter_init_from_data(&queryInIterator,
							 queryElement->bsonValue.value.v_doc.data,
							 queryElement->bsonValue.value.v_doc.data_len);
	int index = 0;
	while (bson_iter_next(&queryInIterator))
	{
		const bson_value_t *arrayValue = bson_iter_value(&queryInIterator);
		int64 hash = BsonValueHash(arrayValue, 0);
		entries[index] = Int64GetDatum(hash);
		index++;
	}

	if (arrayHasNull)
	{
		/* add the root term for documents that don't have the path */
		entries[index] = Int64GetDatum(0);
	}

	return entries;
}


/* Helper function to throw common error when array is found during the
 * extractValue document traversal. */
static void
pg_attribute_noreturn()
ThrowErrorArraysNotSupported(const char * path, int pathLength)
{
	char *errorPath;
	if (pathLength <= 0)
	{
		pathLength = strlen(path);
	}

	/* pathLength + 1 for \0 terminator */
	errorPath = (char *) palloc0(sizeof(char) * (pathLength + 1));
	memcpy(errorPath, path, pathLength);
	ereport(ERROR, errcode(ERRCODE_HELIO_HASHEDINDEXDONOTSUPPORTARRAYVALUES), errmsg(
				"hashed indexes do not currently support array values. Found array at path: %s",
				errorPath));
}


/* This function extracts the value at the given index path and stores it for hashing */
static bool
HashIndexVisitTopLevelField(pgbsonelement *element, const StringView *filterPath,
							void *state)
{
	HashIndexTraverseState *hashState = (HashIndexTraverseState *) state;
	if (element->bsonValue.value_type == BSON_TYPE_ARRAY)
	{
		ThrowErrorArraysNotSupported(hashState->indexPath, -1);
	}

	hashState->foundValue = true;
	hashState->bsonValue = element->bsonValue;

	/* no need to keep traversing */
	return false;
}


/* This function throws errors on encountering an array path during has indexing */
static bool
HashIndexVisitArrayField(pgbsonelement *element, const StringView *filterPath, int
						 arrayIndex,
						 void *state)
{
	HashIndexTraverseState *hashState = (HashIndexTraverseState *) state;
	ThrowErrorArraysNotSupported(hashState->indexPath, -1);
}


/* This function throws errors on encountering an array path during has indexing */
static bool
HashIndexContinueProcessIntermediateArray(void *state, const bson_value_t *value)
{
	HashIndexTraverseState *hashState = (HashIndexTraverseState *) state;
	ThrowErrorArraysNotSupported(hashState->indexPath, -1);
}


/* Validates that the path specified when creating
 * the index is valid. */
static void
ValidateHashedPathSpec(const char *prefix)
{
	if (prefix == NULL)
	{
		return;
	}

	int32_t stringLength = strlen(prefix);
	if (stringLength == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"Hashed index path must not be empty")));
	}

	if (prefix[stringLength - 1] == '.')
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"Index path must not have a trailing '.'")));
	}
}
