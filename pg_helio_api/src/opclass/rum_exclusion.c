/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/opclass/rum_exclusion.c
 *
 * Rum implementations for indexing of the shard key based term for the purposes
 * of unique index via exclusion constraints.
 *
 * When building unique indexes, Citus requires (and we honor that) to have the distribution
 * column (shard_key_value) in the exclusion constraint. If we create an exclusion constraint
 * the defacto mode is to create it as EXCLUDE WITH (shard_key_value, [remaining columns]...)
 *
 * The problem with this is that GIN's composite index is actually just N indexes put together.
 * Consequently, the insert of a document in say, an unsharded collection, will scan the shard_key_value
 * for equality (which is all documents) and independently scan for the remaining columns (typically 1)
 * and intersect the results. With a large number of documents, the first scan can become very large making
 * insert performance degrade over time.
 *
 * Instead of this, we opt to create the index has a composite term ROW(shard_key_value, document), [remaining columns]
 * Where the first entry is inserted into the index as a custom term. The format of the term is a 16 byte value with:
 * byte[0..7]: shard_key_value as-is
 * byte[8..15]: hash of the unique term.
 *
 * This way, there is entropy of terms with determinism on the first term, that preserves the shard_key_value with full
 * fidelity (avoiding hash collisions on the shard key being hashed).
 * This also ensures that the following scenario:
 * { "a": 5, "shard_key": 1 } and { "a": [ 5, 6, 7 ], "shard_key": 1 }
 * wil violate the unique constraint as we would generate the terms as [ HASH{1}, HASH{5}] for the first document
 * and [ HASH{1}, HASH{5}], [ HASH{1}, HASH{6}], [ HASH{1}, HASH{7}] which would cause the first term to violate
 * the unique constraint. On an insert path, we look up those specific terms such as [ HASH{1}, HASH{5}] which have low
 * cardinality and making inserts significantly faster.
 *
 * See also: https://www.postgresql.org/docs/current/gin-extensibility.html
 * See also: https://github.com/postgrespro/rum
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <access/stratnum.h>
#include <access/reloptions.h>
#include <catalog/pg_type.h>
#include <utils/uuid.h>
#include <executor/executor.h>
#include <utils/typcache.h>

#include "utils/mongo_errors.h"
#include "opclass/helio_bson_gin_private.h"
#include "opclass/helio_gin_index_mgmt.h"
#include "metadata/metadata_cache.h"
#include "io/helio_bson_core.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static pgbson * GetShardKeyAndDocument(HeapTupleHeader input, int64_t *shardKey);
static IndexTraverseOption GetExclusionIndexTraverseOption(void *contextOptions,
														   const char *currentPath,
														   uint32_t currentPathLength,
														   bson_type_t bsonType);
static void GenerateTermsForExclusion(pgbson *document, int64_t shardKey,
									  GenerateTermsContext *context,
									  bool generateRootTerm);
static void ValidateExclusionPathSpec(const char *prefix);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(gin_bson_exclusion_extract_value);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_extract_query);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_pre_consistent);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_consistent);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_options);
PG_FUNCTION_INFO_V1(bson_unique_exclusion_index_equal);


/*
 * Runs the preconsistent function for the exclusion operator class
 * Since the index is full fidelity and we only support =
 * we just trust the index.
 */
Datum
gin_bson_exclusion_pre_consistent(PG_FUNCTION_ARGS)
{
	bool *recheck = (bool *) PG_GETARG_POINTER(5);
	*recheck = false;
	PG_RETURN_BOOL(true);
}


/*
 * Runs the consistent function for the exclusion operator class
 * Since the index is full fidelity and we only support =
 * we just trust the index.
 */
Datum
gin_bson_exclusion_consistent(PG_FUNCTION_ARGS)
{
	/* we always trust the index */
	bool *recheck = (bool *) PG_GETARG_POINTER(5);
	*recheck = false;
	PG_RETURN_BOOL(true);
}


/*
 * Extracts the value given an input record.
 * We extract terms as per the regular index, and then convert
 * the generated term into a 16 byte UUID. The UUID will have the form:
 * [byte 0..7]: shard_key_value
 * [byte 8..15]: hash of the term
 */
Datum
gin_bson_exclusion_extract_value(PG_FUNCTION_ARGS)
{
	HeapTupleHeader input = PG_GETARG_HEAPTUPLEHEADER(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	BsonGinSinglePathOptions *options =
		(BsonGinSinglePathOptions *) PG_GET_OPCLASS_OPTIONS();

	int64_t shardKey;
	pgbson *document = GetShardKeyAndDocument(input, &shardKey);
	GenerateTermsContext context = { 0 };
	context.options = options;
	context.indexTermSizeLimit = GetIndexTermSizeLimit(options);
	bool generateRootTerm = true;
	GenerateTermsForExclusion(document, shardKey, &context, generateRootTerm);
	*nentries = context.totalTermCount;
	PG_RETURN_POINTER(context.terms.entries);
}


/*
 * Given a query on an record extracts the term to be queried.
 * Since we only support equality, we just return the output of
 * extract_value as-is.
 */
Datum
gin_bson_exclusion_extract_query(PG_FUNCTION_ARGS)
{
	HeapTupleHeader input = PG_GETARG_HEAPTUPLEHEADER(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	StrategyNumber strategy = PG_GETARG_UINT16(2);

	if (strategy != BTEqualStrategyNumber)
	{
		ereport(ERROR, errmsg("Invalid strategy number %d", strategy));
	}


	if (!PG_HAS_OPCLASS_OPTIONS())
	{
		ereport(ERROR, (errmsg("Index does not have options")));
	}

	BsonGinSinglePathOptions *options =
		(BsonGinSinglePathOptions *) PG_GET_OPCLASS_OPTIONS();

	int64_t shardKey;
	pgbson *document = GetShardKeyAndDocument(input, &shardKey);

	GenerateTermsContext context = { 0 };
	context.options = options;
	bool generateRootTerm = false;
	context.indexTermSizeLimit = GetIndexTermSizeLimit(options);
	GenerateTermsForExclusion(document, shardKey, &context, generateRootTerm);
	*nentries = context.totalTermCount;
	PG_RETURN_POINTER(context.terms.entries);
}


/*
 * gin_bson_exclusion_options sets up the option specification for single field exclusion hash terms
 * This initializes the structure that is used by the Index AM to process user specified
 * options on how to handle documents with the index.
 */
Datum
gin_bson_exclusion_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(BsonGinExclusionHashOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_UniqueShardKey, /* default value */
							IndexOptionsType_UniqueShardKey, /* min */
							IndexOptionsType_UniqueShardKey, /* max */
							offsetof(BsonGinExclusionHashOptions, base.type));
	add_local_string_reloption(relopts, "path",
							   "Prefix path for the index",
							   NULL, &ValidateExclusionPathSpec, &FillSinglePathSpec,
							   offsetof(BsonGinExclusionHashOptions, path));
	add_local_string_reloption(relopts, "indexname",
							   "[deprecated] The mongo specific name for the index",
							   NULL, NULL, &FillDeprecatedStringSpec,
							   offsetof(BsonGinExclusionHashOptions,
										base.intOption_deprecated));
	add_local_int_reloption(relopts, "indextermsize",
							"[deprecated] The index term size limit for truncation",
							-1, /* default value */
							-1, /* min */
							-1, /* max: shard key index terms shouldn't be truncated. */
							offsetof(BsonGinExclusionHashOptions,
									 base.intOption_deprecated));
	add_local_int_reloption(relopts, "ts",
							"[deprecated] The index term size limit for truncation.",
							-1, /* default value */
							-1, /* min */
							INT32_MAX, /* max */
							offsetof(BsonGinExclusionHashOptions,
									 base.intOption_deprecated));

	PG_RETURN_VOID();
}


/*
 * Empty function that implements equality on the shard_key_value_and_document
 * We would never use this as we only use this in the push down of equality to the
 * exclusion constraint index.
 */
Datum
bson_unique_exclusion_index_equal(PG_FUNCTION_ARGS)
{
	ereport(ERROR, errmsg(
				"Unique equal should only be an operator pushed to the index."));
}


/* Validates that the path specified when creating
 * the index is valid. */
static void
ValidateExclusionPathSpec(const char *prefix)
{
	if (prefix == NULL)
	{
		return;
	}

	int32_t stringLength = strlen(prefix);
	if (stringLength == 0)
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Unique hash index path must not be empty")));
	}

	if (prefix[stringLength - 1] == '.')
	{
		ereport(ERROR, (errcode(MongoBadValue), errmsg(
							"Unique hash path must not have a trailing '.'")));
	}
}


/*
 * Given the composite type shard_key_value_and_document, extracts the
 * shard_key_value and document pieces out of the composite type.
 */
static pgbson *
GetShardKeyAndDocument(HeapTupleHeader input, int64_t *shardKey)
{
	/* Extract data about the row. */
	HeapTupleHeader tupleHeader = DatumGetHeapTupleHeader(input);

	bool isNull;
	Datum shardKeyDatum = GetAttributeByNum(tupleHeader, 1, &isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("Shard_key_value should not be null")));
	}

	Datum documentDatum = GetAttributeByNum(tupleHeader, 2, &isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("document should not be null")));
	}

	*shardKey = DatumGetInt64(shardKeyDatum);
	return DatumGetPgBson(documentDatum);
}


/*
 * Implements the logic that GenerateTerms needs to determine whether a path
 * should be indexed. Follows similar logic to the Single path index.
 */
static IndexTraverseOption
GetExclusionIndexTraverseOption(void *contextOptions,
								const char *currentPath,
								uint32_t currentPathLength,
								bson_type_t bsonType)
{
	BsonGinExclusionHashOptions *option = (BsonGinExclusionHashOptions *) contextOptions;
	const char *indexPath;
	uint32_t indexPathLength;
	Get_Index_Path_Option(option, path, indexPath, indexPathLength);
	bool isWildcard = false;
	return GetSinglePathIndexTraverseOptionCore(indexPath, indexPathLength,
												currentPath, currentPathLength,
												isWildcard);
}


/*
 * Helper method that implements the core logic for extracting terms from a given document
 * and shard key. Walks the document and builds the index terms for both extract_query and
 * extract_value. Generates the term as per a single key index, and then builds the
 * Hash(shard_key, term) as the term to be returned for each generated term.
 */
static void
GenerateTermsForExclusion(pgbson *document,
						  int64_t shardKey,
						  GenerateTermsContext *context,
						  bool generateRootTerm)
{
	context->traverseOptionsFunc = &GetExclusionIndexTraverseOption;
	context->generateNotFoundTerm = true;
	GenerateTerms(document, context, generateRootTerm);

	/* Now walk the generated terms and replace them with the hash */
	for (int i = 0; i < context->totalTermCount; i++)
	{
		Datum entry = context->terms.entries[i];
		bytea *termBson = DatumGetByteaP(entry);
		BsonIndexTerm indexTerm;
		InitializeBsonIndexTerm(termBson, &indexTerm);

		/* We store the term in a uuid as it is a cheap way to store
		 * 16 bytes without the length prefix overhead that bytea
		 * has. This will reduce the overall index term size for this term */
		pg_uuid_t *uuid = palloc(sizeof(pg_uuid_t));
		int64_t *firstBytes = (int64_t *) &uuid->data[0];
		*firstBytes = shardKey;
		int64_t *lastBytes = (int64_t *) &uuid->data[8];
		*lastBytes = BsonValueHash(&indexTerm.element.bsonValue, 0);
		context->terms.entries[i] = UUIDPGetDatum(uuid);
	}
}
