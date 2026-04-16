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

#include "io/bson_core.h"
#include "io/bson_hash.h"
#include "utils/documentdb_errors.h"
#include "utils/version_utils.h"
#include "utils/hashset_utils.h"
#include "opclass/bson_gin_private.h"
#include "opclass/bson_gin_index_mgmt.h"
#include "metadata/metadata_cache.h"

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

extern int DefaultUniqueIndexKeyhashOverride;
extern bool UseNewUniqueHashEqualityFunction;

static pgbson * GetShardKeyAndDocument(HeapTupleHeader input, int64_t *shardKey);
static IndexTraverseOption GetExclusionIndexTraverseOption(void *contextOptions,
														   const char *currentPath,
														   uint32_t currentPathLength,
														   bson_type_t bsonType,
														   int32_t *pathIndex);
static void GenerateTermsForExclusion(pgbson *document, int64_t shardKey,
									  GenerateTermsContext *context,
									  GinEntryPathData *pathData,
									  bool generateRootTerm);
static void ValidateExclusionPathSpec(const char *prefix);
static bool ProcessUniqueShardDocumentKeysNew(pgbson *uniqueShardDocument,
											  int64_t *shardKeyComparison,
											  bool *hasShardKey,
											  HTAB *termsHashSet, HASHACTION hashAction);
static bool ProcessUniqueShardDocumentKeys(pgbson *uniqueShardDocument,
										   HTAB *termsHashSet, HASHACTION hashAction);
static HTAB * GetUniqueShardDocumentTermsHTABNew(pgbson *uniqueShardDocument,
												 int64_t *shardKeyValue,
												 bool *hasShardKeyValue);
static HTAB * GetUniqueShardDocumentTermsHTAB(pgbson *document);

typedef struct IndexBounds
{
	int32_t minIndex;
	int32_t maxIndex;
	int32_t numTerms;
} IndexBounds;

typedef struct IndexBoundsWithLength
{
	IndexBounds *indexBounds;
	int32_t length;
	bool isCompositeHash;
} IndexBoundsWithLength;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */
PG_FUNCTION_INFO_V1(gin_bson_exclusion_extract_value);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_extract_query);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_pre_consistent);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_consistent);
PG_FUNCTION_INFO_V1(gin_bson_exclusion_options);
PG_FUNCTION_INFO_V1(bson_unique_exclusion_index_equal);
PG_FUNCTION_INFO_V1(generate_unique_shard_document);
PG_FUNCTION_INFO_V1(bson_unique_shard_path_equal);
PG_FUNCTION_INFO_V1(gin_bson_unique_shard_extract_value);
PG_FUNCTION_INFO_V1(gin_bson_unique_shard_extract_query);
PG_FUNCTION_INFO_V1(gin_bson_unique_shard_pre_consistent);
PG_FUNCTION_INFO_V1(gin_bson_unique_shard_consistent);
PG_FUNCTION_INFO_V1(bson_unique_shard_path_index_equal);
PG_FUNCTION_INFO_V1(bson_unique_index_term_equal);
PG_FUNCTION_INFO_V1(bson_unique_shard_path_options);

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
	GinEntryPathData pathData = { 0 };
	context.options = options;
	pathData.termMetadata = GetIndexTermMetadata(options);
	bool generateRootTerm = true;
	GenerateTermsForExclusion(document, shardKey, &context, &pathData, generateRootTerm);
	*nentries = pathData.terms.index;

	PG_FREE_IF_COPY(input, 0);
	PG_RETURN_POINTER(pathData.terms.entries);
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
	GinEntryPathData pathData = { 0 };
	context.options = options;
	bool generateRootTerm = false;
	pathData.termMetadata = GetIndexTermMetadata(options);
	GenerateTermsForExclusion(document, shardKey, &context, &pathData, generateRootTerm);
	*nentries = pathData.terms.index;

	PG_FREE_IF_COPY(input, 0);
	PG_RETURN_POINTER(pathData.terms.entries);
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
	add_local_int_reloption(relopts, "version",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V0,         /* max */
							offsetof(BsonGinExclusionHashOptions, base.version));
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
				"Unique exclusion index equal should only be an operator pushed to the index."));
}


Datum
bson_unique_shard_path_index_equal(PG_FUNCTION_ARGS)
{
	/* We trust the index for the recheck here */
	PG_RETURN_BOOL(true);
}


/*
 * bson_unique_index_equal is a dummy function that is used by the runtime to represent a unique index comparison.
 * The operator is unused as it is always pushed into the RUM index for index evaluation.
 * Note we can't use bson_equal (which does a field by field semantic equality), nor dollar_equal since it expects
 * document @= filter (which is not the behavior seen for unique indexes). We need a custom commutative operator
 * that allows for index pushdown for unique.
 */
Datum
bson_unique_index_term_equal(PG_FUNCTION_ARGS)
{
	/* In this case, we presume that the index is correct (for recheck purposes) */
	PG_RETURN_BOOL(true);
}


Datum
generate_unique_shard_document(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	int64_t shardKeyValue = PG_GETARG_INT64(1);
	pgbson *projectionSpec = PG_GETARG_PGBSON_PACKED(2);

	bool sparse = PG_GETARG_BOOL(3);
	bool generateCompositeTerms = PG_NARGS() > 4 ? PG_GETARG_BOOL(4) : false;

	Datum *termArray[INDEX_MAX_KEYS] = { 0 };
	int32_t numTermArray[INDEX_MAX_KEYS] = { 0 };
	StringView pathArray[INDEX_MAX_KEYS] = { 0 };

	/* Next, write out the terms generated per path these will be used by the op-class to
	 * generate the collision resistant hash.
	 */
	int32_t numTerms = 0;
	int32_t indexColumn = 0;
	bson_iter_t specIter;
	PgbsonInitIterator(projectionSpec, &specIter);
	while (bson_iter_next(&specIter))
	{
		StringView pathIter = bson_iter_key_string_view(&specIter);

		char *buffer = palloc0(sizeof(BsonGinSinglePathOptions) + 5 + pathIter.length);
		BsonGinSinglePathOptions *singlePathOptions = (BsonGinSinglePathOptions *) buffer;
		singlePathOptions->isWildcard = false;
		singlePathOptions->generateNotFoundTerm = !sparse;
		singlePathOptions->base.indexTermTruncateLimit = INT32_MAX;
		singlePathOptions->base.type = IndexOptionsType_SinglePath;
		singlePathOptions->base.version = IndexOptionsVersion_V0;
		singlePathOptions->path = sizeof(BsonGinSinglePathOptions);
		char *pathPrefix = buffer + sizeof(BsonGinSinglePathOptions);
		*(uint32_t *) pathPrefix = pathIter.length;
		pathPrefix += 4;
		memcpy(pathPrefix, pathIter.string, pathIter.length);

		GenerateTermsContext context = { 0 };
		GinEntryPathData pathData = { 0 };
		context.options = singlePathOptions;
		bool generateRootTerm = false;
		pathData.termMetadata = GetIndexTermMetadata(singlePathOptions);
		context.traverseOptionsFunc = &GetSinglePathIndexTraverseOption;

		if (generateCompositeTerms)
		{
			pathData.generatePathBasedUndefinedTerms =
				singlePathOptions->generateNotFoundTerm;
			context.generateNotFoundTerm = false;
		}
		else
		{
			context.generateNotFoundTerm = singlePathOptions->generateNotFoundTerm;
		}

		GenerateTerms(document, &context, &pathData, generateRootTerm);

		if (indexColumn >= INDEX_MAX_KEYS)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Cannot have more than 32 columns in the composite index extraction")));
		}

		if (pathData.terms.index == 0 && sparse)
		{
			continue;
		}

		numTermArray[indexColumn] = pathData.terms.index;
		termArray[indexColumn] = pathData.terms.entries;
		pathArray[indexColumn] = pathIter;
		indexColumn++;

		/* Calculate total terms */
		numTerms += pathData.terms.index;
	}

	/* This is similar to a projection - except we also add in the shard key value */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "$shard_key_value", 16, shardKeyValue);
	PgbsonWriterAppendInt32(&writer, "$numTerms", 9, numTerms);
	PgbsonWriterAppendInt32(&writer, "$numPaths", 9, indexColumn);


	for (int32_t i = 0; i < indexColumn; i++)
	{
		if (termArray[i] == 0)
		{
			continue;
		}

		pgbson_array_writer singleTerm;
		PgbsonWriterStartArray(&writer, pathArray[i].string, pathArray[i].length,
							   &singleTerm);

		/* Now write it out in asc order */
		for (int32_t j = 0; j < numTermArray[i]; j++)
		{
			Datum entry = termArray[i][j];
			bytea *termBson = DatumGetByteaPP(entry);
			BsonIndexTerm indexTerm;
			InitializeBsonIndexTerm(termBson, &indexTerm);
			if (IsIndexTermMetadata(&indexTerm))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"Unexpected - found metadata term in index build for unique_shard_document"),
								errdetail_log(
									"Unexpected - found metadata term in index build for unique_shard_document")));
			}

			PgbsonArrayWriterWriteValue(&singleTerm, &indexTerm.element.bsonValue);
		}

		PgbsonWriterEndArray(&writer, &singleTerm);
	}

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


static Datum
BsonUniqueShardEqualLegacy(PG_FUNCTION_ARGS)
{
	/*
	 * Logging for testing purposes. We need to assert that we recheck the index when there's a hash collision and the
	 * terms are truncated.
	 */
	ereport(DEBUG1, (errmsg("Executing unique index runtime recheck.")));

	pgbson *left = PG_GETARG_PGBSON_PACKED(0);
	pgbson *right = PG_GETARG_PGBSON_PACKED(1);


	/* Build HTAB with every pair of { <path> : <term> } */
	HTAB *leftHashTable = GetUniqueShardDocumentTermsHTAB(left);

	/*
	 * Iterate through pgbson on the right to check if every path (key) has
	 * a term match on the left.
	 */
	bool uniquenessConflict = ProcessUniqueShardDocumentKeys(right, leftHashTable,
															 HASH_FIND);

	hash_destroy(leftHashTable);
	PG_FREE_IF_COPY(left, 0);
	PG_FREE_IF_COPY(right, 1);

	PG_RETURN_BOOL(uniquenessConflict);
}


static Datum
BsonUniqueShardEqualNew(PG_FUNCTION_ARGS)
{
	/*
	 * Logging for testing purposes. We need to assert that we recheck the index when there's a hash collision and the
	 * terms are truncated.
	 */
	ereport(DEBUG1, (errmsg("Executing unique index runtime recheck.")));

	pgbson *left = PG_GETARG_PGBSON_PACKED(0);
	pgbson *right = PG_GETARG_PGBSON_PACKED(1);


	/* Build HTAB with every pair of { <path> : <term> } */
	int64_t leftShardKey = 0;
	bool hasLeftShardKey = false;
	HTAB *leftHashTable = GetUniqueShardDocumentTermsHTABNew(left, &leftShardKey,
															 &hasLeftShardKey);

	/*
	 * Iterate through pgbson on the right to check if every path (key) has
	 * a term match on the left.
	 */
	int64_t rightShardKey = 0;
	bool hasRightShardKey = false;
	bool uniquenessConflict = ProcessUniqueShardDocumentKeysNew(right, &rightShardKey,
																&hasRightShardKey,
																leftHashTable, HASH_FIND);

	hash_destroy(leftHashTable);
	PG_FREE_IF_COPY(left, 0);
	PG_FREE_IF_COPY(right, 1);

	if (!hasLeftShardKey || !hasRightShardKey)
	{
		ereport(ERROR, (errmsg("Required field $shard_key_value is missing")));
	}

	if (leftShardKey != rightShardKey)
	{
		uniquenessConflict = false;
	}

	PG_RETURN_BOOL(uniquenessConflict);
}


Datum
bson_unique_shard_path_equal(PG_FUNCTION_ARGS)
{
	if (UseNewUniqueHashEqualityFunction)
	{
		return BsonUniqueShardEqualNew(fcinfo);
	}
	else
	{
		return BsonUniqueShardEqualLegacy(fcinfo);
	}
}


static Datum *
GenerateNonCompositeHashTerms(bson_iter_t *specIter, uint32_t numTerms,
							  uint32_t numPaths, int64_t shardKeyValue,
							  int32_t *nentries, Pointer **extraData)
{
	Datum *indexEntries = palloc0(sizeof(Datum) * numTerms);
	IndexBounds *pathMap = NULL;
	if (extraData != NULL)
	{
		pathMap = palloc0(sizeof(IndexBounds) * numPaths);
		IndexBoundsWithLength *boundsWithLength = palloc0(sizeof(IndexBoundsWithLength) *
														  numTerms);

		IndexBoundsWithLength singleBounds = { 0 };
		singleBounds.length = numPaths;
		singleBounds.indexBounds = pathMap;
		singleBounds.isCompositeHash = false;
		for (uint32_t i = 0; i < numTerms; i++)
		{
			boundsWithLength[i] = singleBounds;
		}

		*extraData = (Pointer *) boundsWithLength;
	}

	uint32_t index = 0;
	uint32_t pathIndex = 0;
	while (bson_iter_next(specIter))
	{
		if (pathIndex >= numPaths)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg("numPaths specified was >= indexPaths encountered"),
							errdetail_log(
								"numPaths specified %d was >= indexPaths %d encountered",
								numPaths,
								pathIndex)));
		}

		const char *key = bson_iter_key(specIter);

		if (!BSON_ITER_HOLDS_ARRAY(specIter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"term values to generate for a given key should be an array")));
		}

		bson_value_t pathValue = { 0 };
		pathValue.value_type = BSON_TYPE_UTF8;
		pathValue.value.v_utf8.len = strlen(key);
		pathValue.value.v_utf8.str = (char *) key;
		int64_t keyhash = BsonValueHash(&pathValue, 0);

		if (pathMap != NULL)
		{
			pathMap[pathIndex].minIndex = index;
		}

		bson_iter_t arrayIter;
		bson_iter_recurse(specIter, &arrayIter);
		while (bson_iter_next(&arrayIter))
		{
			const bson_value_t *indexTerm = bson_iter_value(&arrayIter);

			/* We store the term in a uuid as it is a cheap way to store
			 * 16 bytes without the length prefix overhead that bytea
			 * has. This will reduce the overall index term size for this term */
			pg_uuid_t *uuid = palloc(sizeof(pg_uuid_t));
			int64_t *firstBytes = (int64_t *) &uuid->data[0];
			*firstBytes = shardKeyValue;
			int64_t *lastBytes = (int64_t *) &uuid->data[8];

			/*
			 * Hash the value.
			 * We check the GUC in order to force a hash collision.
			 * This is only used for testing and should not be set in production.
			 */
			*lastBytes = DefaultUniqueIndexKeyhashOverride > 0 ?
						 (uint64_t) DefaultUniqueIndexKeyhashOverride :
						 HashBsonValueComparableExtended(indexTerm, keyhash);

			if (index > numTerms)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"Invalid number of terms specified. Specified %d terms but found at least %d terms",
									numTerms, index),
								errdetail_log(
									"Invalid number of terms specified. Specified %d terms but found at least %d terms",
									numTerms, index)));
			}

			indexEntries[index] = PointerGetDatum(uuid);
			index++;
		}

		if (pathMap != NULL)
		{
			pathMap[pathIndex].maxIndex = index;
		}

		pathIndex++;
	}

	*nentries = numTerms;
	return indexEntries;
}


static Datum *
GenerateCompositeHashTerms(bson_iter_t *specIter, uint32_t numTerms,
						   uint32_t numPaths, int64_t shardKeyValue,
						   int32_t *nentries, Pointer **extraData)
{
	int64_t *indexHashes = palloc0(sizeof(int64_t) * numTerms);
	IndexBounds *pathMap = palloc0(sizeof(IndexBounds) * numPaths);

	uint32_t index = 0;
	uint32_t pathIndex = 0;
	while (bson_iter_next(specIter))
	{
		if (pathIndex >= numPaths)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg("numPaths specified was >= indexPaths encountered"),
							errdetail_log(
								"numPaths specified %d was >= indexPaths %d encountered",
								numPaths,
								pathIndex)));
		}

		const char *key = bson_iter_key(specIter);

		if (!BSON_ITER_HOLDS_ARRAY(specIter))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"term values to generate for a given key should be an array")));
		}

		bson_value_t pathValue = { 0 };
		pathValue.value_type = BSON_TYPE_UTF8;
		pathValue.value.v_utf8.len = strlen(key);
		pathValue.value.v_utf8.str = (char *) key;
		int64_t keyhash = BsonValueHash(&pathValue, 0);

		pathMap[pathIndex].minIndex = index;
		bson_iter_t arrayIter;
		bson_iter_recurse(specIter, &arrayIter);
		while (bson_iter_next(&arrayIter))
		{
			const bson_value_t *indexTerm = bson_iter_value(&arrayIter);

			/*
			 * Hash the value.
			 * We check the GUC in order to force a hash collision.
			 * This is only used for testing and should not be set in production.
			 */
			uint64_t hash = DefaultUniqueIndexKeyhashOverride > 0 ?
							(uint64_t) DefaultUniqueIndexKeyhashOverride :
							HashBsonValueComparableExtended(indexTerm, keyhash);

			if (index > numTerms)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"Invalid number of terms specified. Specified %d terms but found at least %d terms",
									numTerms, index),
								errdetail_log(
									"Invalid number of terms specified. Specified %d terms but found at least %d terms",
									numTerms, index)));
			}

			indexHashes[index] = hash;
			index++;
		}

		pathMap[pathIndex].maxIndex = index;
		pathIndex++;
	}

	if (numPaths == 0)
	{
		*nentries = 0;
		return NULL;
	}

	/* Now we have indexhashes populated with all the hashes per term
	 * We also have the path map.
	 */
	int totalCompositeTermCount = 1;
	for (uint32 i = 0; i < numPaths; i++)
	{
		pathMap[i].numTerms = pathMap[i].maxIndex - pathMap[i].minIndex;
		if (pathMap[i].numTerms == 0)
		{
			ereport(ERROR, (errmsg("Unexpected - should not have 0 terms for path %u",
								   i)));
		}

		totalCompositeTermCount = totalCompositeTermCount * pathMap[i].numTerms;
	}

	Datum *indexTerms = palloc(sizeof(Datum) * totalCompositeTermCount);
	for (int i = 0; i < totalCompositeTermCount; i++)
	{
		int termIndex = i;
		int64_t termHash = 0;
		for (uint32_t j = 0; j < numPaths; j++)
		{
			int32_t currentIndex = termIndex % pathMap[j].numTerms;
			termIndex = termIndex / pathMap[j].numTerms;

			/* access the hash */
			int64_t current = indexHashes[currentIndex + pathMap[j].minIndex];

			/* Combine hashes */
			termHash = (int64_t) hash_combine64((uint64) termHash, (uint64) current);
		}

		pg_uuid_t *uuid = palloc(sizeof(pg_uuid_t));
		int64_t *firstBytes = (int64_t *) &uuid->data[0];
		*firstBytes = shardKeyValue;
		int64_t *lastBytes = (int64_t *) &uuid->data[8];
		*lastBytes = termHash;
		indexTerms[i] = UUIDPGetDatum(uuid);
	}

	if (extraData != NULL)
	{
		IndexBoundsWithLength *boundsWithLength = palloc0(sizeof(IndexBoundsWithLength) *
														  totalCompositeTermCount);

		IndexBoundsWithLength singleBounds = { 0 };
		singleBounds.length = numPaths;
		singleBounds.indexBounds = pathMap;
		singleBounds.isCompositeHash = true;
		for (int i = 0; i < totalCompositeTermCount; i++)
		{
			boundsWithLength[i] = singleBounds;
		}

		*extraData = (Pointer *) boundsWithLength;
	}

	pfree(indexHashes);
	*nentries = totalCompositeTermCount;
	return indexTerms;
}


static Datum *
ExtractUniqueShardTermsFromInput(pgbson *input, int32_t *nentries, Pointer **extraData,
								 BsonShardPathExclusionOptions *options)
{
	bson_iter_t specIter;
	int64_t shardKeyValue = 0;
	int32_t numTerms = 0;
	int32_t numPaths = 0;
	PgbsonInitIterator(input, &specIter);

	/* First field is the shard key value */
	if (!bson_iter_next(&specIter))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"$shard_key_value is a required field for unique shard key path")));
	}

	if (strcmp(bson_iter_key(&specIter), "$shard_key_value") == 0)
	{
		shardKeyValue = bson_iter_int64(&specIter);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"$shard_key_value must be the first field in the document")));
	}

	/* next field is numTerms */
	if (!bson_iter_next(&specIter))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"$numTerms is a required field for unique shard key path")));
	}

	if (strcmp(bson_iter_key(&specIter), "$numTerms") == 0)
	{
		numTerms = bson_iter_int32(&specIter);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"$numTerms should always appear as the second field within the document.")));
	}

	/* next field is numTerms */
	if (!bson_iter_next(&specIter))
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"$numPaths is a required field for unique shard key path")));
	}

	if (strcmp(bson_iter_key(&specIter), "$numPaths") == 0)
	{
		numPaths = bson_iter_int32(&specIter);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("$numPaths must be the third field in the document")));
	}

	if (options->enableCompositeHashGeneration)
	{
		return GenerateCompositeHashTerms(&specIter, numTerms, numPaths, shardKeyValue,
										  nentries, extraData);
	}
	else
	{
		return GenerateNonCompositeHashTerms(&specIter, numTerms, numPaths, shardKeyValue,
											 nentries, extraData);
	}
}


Datum
gin_bson_unique_shard_extract_value(PG_FUNCTION_ARGS)
{
	pgbson *input = PG_GETARG_PGBSON_PACKED(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	Pointer **extraData = NULL;

	BsonShardPathExclusionOptions baseOptions = { 0 };
	BsonShardPathExclusionOptions *optionsPtr = &baseOptions;
	if (PG_HAS_OPCLASS_OPTIONS())
	{
		optionsPtr = (BsonShardPathExclusionOptions *) PG_GET_OPCLASS_OPTIONS();
	}

	Datum *indexEntries = ExtractUniqueShardTermsFromInput(input, nentries, extraData,
														   optionsPtr);
	PG_FREE_IF_COPY(input, 0);
	PG_RETURN_POINTER(indexEntries);
}


Datum
gin_bson_unique_shard_extract_query(PG_FUNCTION_ARGS)
{
	pgbson *input = PG_GETARG_PGBSON_PACKED(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);
	StrategyNumber strategy = PG_GETARG_UINT16(2);
	Pointer **extraData = (Pointer **) PG_GETARG_POINTER(4);

	if (strategy != 1)
	{
		ereport(ERROR, errmsg("Invalid strategy number %d", strategy));
	}

	BsonShardPathExclusionOptions baseOptions = { 0 };
	BsonShardPathExclusionOptions *optionsPtr = &baseOptions;
	if (PG_HAS_OPCLASS_OPTIONS())
	{
		optionsPtr = (BsonShardPathExclusionOptions *) PG_GET_OPCLASS_OPTIONS();
	}

	Datum *indexEntries = ExtractUniqueShardTermsFromInput(input, nentries, extraData,
														   optionsPtr);

	PG_FREE_IF_COPY(input, 0);
	PG_RETURN_POINTER(indexEntries);
}


Datum
gin_bson_unique_shard_pre_consistent(PG_FUNCTION_ARGS)
{
	bool *recheck = (bool *) PG_GETARG_POINTER(5);

	/* If we found a match it's a hash match so we need to recheck the runtime */
	*recheck = true;
	PG_RETURN_BOOL(true);
}


Datum
gin_bson_unique_shard_consistent(PG_FUNCTION_ARGS)
{
	bool *check = (bool *) PG_GETARG_POINTER(0);
	bool *recheck = (bool *) PG_GETARG_POINTER(5);
	Pointer *extra_data = (Pointer *) PG_GETARG_POINTER(4);

	IndexBoundsWithLength *boundsWithLength = (IndexBoundsWithLength *) extra_data;

	if (boundsWithLength->isCompositeHash)
	{
		/* The hash is now an exact match - recheck on the runtime */
		*recheck = true;
		PG_RETURN_BOOL(true);
	}

	for (int i = 0; i < boundsWithLength->length; i++)
	{
		IndexBounds bounds = boundsWithLength->indexBounds[i];
		bool columnMatched = false;
		for (int j = bounds.minIndex; j < bounds.maxIndex; j++)
		{
			if (check[j])
			{
				columnMatched = true;
				break;
			}
		}

		if (!columnMatched)
		{
			PG_RETURN_BOOL(false);
		}
	}

	/* If we found a match it's a hash match so we need to recheck the runtime */
	*recheck = true;
	PG_RETURN_BOOL(true);
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
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
							"Unique hash index path must not be empty")));
	}

	if (prefix[stringLength - 1] == '.')
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE), errmsg(
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
	HeapTupleHeader tupleHeader = input;

	bool isNull;
	Datum shardKeyDatum = GetAttributeByNum(tupleHeader, 1, &isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("Shard_key_value should not be null")));
	}

	Datum documentDatum = GetAttributeByNum(tupleHeader, 2, &isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("The document value must not be null")));
	}

	*shardKey = DatumGetInt64(shardKeyDatum);
	return DatumGetPgBsonPacked(documentDatum);
}


/*
 * Implements the logic that GenerateTerms needs to determine whether a path
 * should be indexed. Follows similar logic to the Single path index.
 */
static IndexTraverseOption
GetExclusionIndexTraverseOption(void *contextOptions,
								const char *currentPath,
								uint32_t currentPathLength,
								bson_type_t bsonType, int32_t *pathIndex)
{
	BsonGinExclusionHashOptions *option = (BsonGinExclusionHashOptions *) contextOptions;
	const char *indexPath;
	uint32_t indexPathLength;
	Get_Index_Path_Option(option, path, indexPath, indexPathLength);
	bool isWildcard = false;
	*pathIndex = 0;
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
						  GinEntryPathData *pathData,
						  bool generateRootTerm)
{
	context->traverseOptionsFunc = &GetExclusionIndexTraverseOption;
	context->generateNotFoundTerm = true;
	GenerateTerms(document, context, pathData, generateRootTerm);

	/* Now walk the generated terms and replace them with the hash */
	for (int i = 0; i < pathData->terms.index; i++)
	{
		Datum entry = pathData->terms.entries[i];
		bytea *termBson = DatumGetByteaPP(entry);
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
		pathData->terms.entries[i] = UUIDPGetDatum(uuid);
	}
}


/*
 * Utility function that iterates on all keys of a unique shard document and takes action
 * against a terms hash set. In case of a HASH_FIND action, this function also returns a
 * boolean indicating an uniqueness conflict.
 */
static bool
ProcessUniqueShardDocumentKeys(pgbson *uniqueShardDocument, HTAB *termsHashSet, HASHACTION
							   hashAction)
{
	bson_iter_t specIter;
	PgbsonInitIterator(uniqueShardDocument, &specIter);

	while (bson_iter_next(&specIter))
	{
		/*
		 * This skips the loop until we reach the keys that contain arrays. These are the ones
		 * that store the terms we need to process.
		 */
		if (!BSON_ITER_HOLDS_ARRAY(&specIter))
		{
			continue;
		}

		const char *key = bson_iter_key(&specIter);
		bson_iter_t arrayIter;
		bson_iter_recurse(&specIter, &arrayIter);

		bool keyTermMatch = false;
		while (bson_iter_next(&arrayIter))
		{
			pgbson_writer writer;
			PgbsonWriterInit(&writer);

			const bson_value_t *indexTerm = bson_iter_value(&arrayIter);
			PgbsonWriterAppendValue(&writer, key, strlen(key), indexTerm);

			/* Get bson containing both key and indexTerm. */
			pgbson *pgbson = PgbsonWriterGetPgbson(&writer);
			const bson_value_t keyValueTerm = ConvertPgbsonToBsonValue(pgbson);

			/* Query hash table with given action. */
			bool found;
			hash_search(termsHashSet, &keyValueTerm, hashAction, &found);

			if (found)
			{
				/* keyTerm pair on the document was found on the hash table. */
				keyTermMatch = true;
				break;
			}
		}

		if (!keyTermMatch && hashAction == HASH_FIND)
		{
			/*
			 * No term for this key was found on the hash table, meaning the unique shard
			 * documents don't have a uniqueness conflict. We return early if action is HASH_FIND.
			 */
			return false;
		}
	}

	/*
	 * Each path (key) on the document has a term match on the hash table, meaning
	 * there's a uniqueness conflict.
	 */
	return true;
}


static bool
ProcessUniqueShardDocumentKeysNew(pgbson *uniqueShardDocument,
								  int64_t *shardKeyComparison, bool *hasShardKey,
								  HTAB *termsHashSet, HASHACTION hashAction)
{
	bson_iter_t specIter;
	PgbsonInitIterator(uniqueShardDocument, &specIter);

	while (bson_iter_next(&specIter))
	{
		const char *key = bson_iter_key(&specIter);
		if (strcmp(key, "$shard_key_value") == 0)
		{
			int64_t shardKeyValue = BsonValueAsInt64(bson_iter_value(&specIter));
			*shardKeyComparison = shardKeyValue;
			*hasShardKey = true;
			continue;
		}

		/*
		 * This skips the loop until we reach the keys that contain arrays. These are the ones
		 * that store the terms we need to process.
		 */
		if (!BSON_ITER_HOLDS_ARRAY(&specIter))
		{
			continue;
		}

		uint32_t keyPathLength = strlen(key);
		bson_iter_t arrayIter;
		bson_iter_recurse(&specIter, &arrayIter);

		bool keyTermMatch = false;
		while (bson_iter_next(&arrayIter))
		{
			pgbsonelement element = { 0 };
			element.path = key;
			element.pathLength = keyPathLength;
			element.bsonValue = *bson_iter_value(&arrayIter);

			/* Query hash table with given action. */
			bool found;
			hash_search(termsHashSet, &element, hashAction, &found);

			if (found)
			{
				/* keyTerm pair on the document was found on the hash table. */
				keyTermMatch = true;
				break;
			}
		}

		if (!keyTermMatch && hashAction == HASH_FIND)
		{
			/*
			 * No term for this key was found on the hash table, meaning the unique shard
			 * documents don't have a uniqueness conflict. We return early if action is HASH_FIND.
			 */
			return false;
		}
	}

	/*
	 * Each path (key) on the document has a term match on the hash table, meaning
	 * there's a uniqueness conflict.
	 */
	return true;
}


static HTAB *
GetUniqueShardDocumentTermsHTAB(pgbson *uniqueShardDocument)
{
	HTAB *termsHashSet = CreateBsonValueHashSet();
	ProcessUniqueShardDocumentKeys(uniqueShardDocument, termsHashSet, HASH_ENTER);
	return termsHashSet;
}


/*
 * Utility function that receives a unique shard document (i.e. document returned from the generate_unique_shard_document function),
 * inserts all terms in a hash table and returns it to the caller.
 */
static HTAB *
GetUniqueShardDocumentTermsHTABNew(pgbson *uniqueShardDocument, int64_t *shardKeyValue,
								   bool *hasShardKeyValue)
{
	HTAB *termsHashSet = CreatePgbsonElementPathAndValueHashSet();
	ProcessUniqueShardDocumentKeysNew(uniqueShardDocument, shardKeyValue,
									  hasShardKeyValue,
									  termsHashSet, HASH_ENTER);
	return termsHashSet;
}


Datum
bson_unique_shard_path_options(PG_FUNCTION_ARGS)
{
	local_relopts *relopts = (local_relopts *) PG_GETARG_POINTER(0);

	init_local_reloptions(relopts, sizeof(BsonShardPathExclusionOptions));

	/* add an option that has a default value of single path and accepts *one* value
	 *  This is used later to key off whether it's a single path or multi-key wildcard index options */
	add_local_int_reloption(relopts, "optionsType",
							"The type of the options struct.",
							IndexOptionsType_UniqueShardPath, /* default value */
							IndexOptionsType_UniqueShardPath, /* min */
							IndexOptionsType_UniqueShardPath, /* max */
							offsetof(BsonShardPathExclusionOptions, base.type));
	add_local_int_reloption(relopts, "version",
							"The version of the options struct.",
							IndexOptionsVersion_V0,         /* default value */
							IndexOptionsVersion_V0,         /* min */
							IndexOptionsVersion_V0,         /* max */
							offsetof(BsonShardPathExclusionOptions, base.version));

	add_local_bool_reloption(relopts, "cmp",
							 "Whether to generate composite based hash terms",
							 false,
							 offsetof(BsonShardPathExclusionOptions,
									  enableCompositeHashGeneration));

	PG_RETURN_VOID();
}
