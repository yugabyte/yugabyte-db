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

static pgbson * GetShardKeyAndDocument(HeapTupleHeader input, int64_t *shardKey);
static IndexTraverseOption GetExclusionIndexTraverseOption(void *contextOptions,
														   const char *currentPath,
														   uint32_t currentPathLength,
														   bson_type_t bsonType);
static void GenerateTermsForExclusion(pgbson *document, int64_t shardKey,
									  GenerateTermsContext *context,
									  bool generateRootTerm);
static void ValidateExclusionPathSpec(const char *prefix);
static bool ProcessUniqueShardDocumentKeys(pgbson *uniqueShardDocument,
										   HTAB *termsHashSet, HASHACTION hashAction);
static HTAB * GetUniqueShardDocumentTermsHTAB(pgbson *document);

typedef struct IndexBounds
{
	int32_t minIndex;
	int32_t maxIndex;
} IndexBounds;

typedef struct IndexBoundsWithLength
{
	IndexBounds *indexBounds;
	int32_t length;
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
	context.termMetadata = GetIndexTermMetadata(options);
	bool generateRootTerm = true;
	GenerateTermsForExclusion(document, shardKey, &context, generateRootTerm);
	*nentries = context.totalTermCount;

	PG_FREE_IF_COPY(input, 0);
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
	context.termMetadata = GetIndexTermMetadata(options);
	GenerateTermsForExclusion(document, shardKey, &context, generateRootTerm);
	*nentries = context.totalTermCount;

	PG_FREE_IF_COPY(input, 0);
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
	if (IsClusterVersionAtleast(DocDB_V0, 24, 0))
	{
		PG_RETURN_BOOL(true);
	}

	ereport(ERROR, errmsg(
				"Unique index term equal operator class function is not supported."));
}


Datum
generate_unique_shard_document(PG_FUNCTION_ARGS)
{
	pgbson *document = PG_GETARG_PGBSON_PACKED(0);
	int64_t shardKeyValue = PG_GETARG_INT64(1);
	pgbson *projectionSpec = PG_GETARG_PGBSON_PACKED(2);

	bool sparse = PG_GETARG_BOOL(3);

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
		context.options = singlePathOptions;
		bool generateRootTerm = false;
		context.termMetadata = GetIndexTermMetadata(singlePathOptions);
		context.traverseOptionsFunc = &GetSinglePathIndexTraverseOption;
		context.generateNotFoundTerm = singlePathOptions->generateNotFoundTerm;
		GenerateTerms(document, &context, generateRootTerm);

		if (indexColumn >= INDEX_MAX_KEYS)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Cannot have more than 32 columns in the composite index extraction")));
		}

		if (context.totalTermCount == 0 && sparse)
		{
			continue;
		}

		numTermArray[indexColumn] = context.totalTermCount;
		termArray[indexColumn] = context.terms.entries;
		pathArray[indexColumn] = pathIter;
		indexColumn++;

		/* Calculate total terms */
		numTerms += context.totalTermCount;
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
			if (indexTerm.isIndexTermMetadata)
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


Datum
bson_unique_shard_path_equal(PG_FUNCTION_ARGS)
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


static Datum *
ExtractUniqueShardTermsFromInput(pgbson *input, int32_t *nentries, Pointer **extraData)
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
						errmsg("$numTerms must be the second field in the document")));
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

	Datum *indexEntries = palloc0(sizeof(Datum) * numTerms);

	IndexBounds *pathMap = NULL;
	if (extraData != NULL)
	{
		pathMap = palloc0(sizeof(IndexBounds) * numPaths);
		IndexBoundsWithLength *boundsWithLength = palloc0(sizeof(IndexBoundsWithLength));
		boundsWithLength->length = numPaths;
		boundsWithLength->indexBounds = pathMap;
		*extraData = (Pointer *) boundsWithLength;
	}

	int32_t index = 0;
	int32_t pathIndex = 0;
	while (bson_iter_next(&specIter))
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

		const char *key = bson_iter_key(&specIter);

		if (!BSON_ITER_HOLDS_ARRAY(&specIter))
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
		bson_iter_recurse(&specIter, &arrayIter);
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


Datum
gin_bson_unique_shard_extract_value(PG_FUNCTION_ARGS)
{
	pgbson *input = PG_GETARG_PGBSON_PACKED(0);
	int32 *nentries = (int32 *) PG_GETARG_POINTER(1);

	Pointer **extraData = NULL;
	Datum *indexEntries = ExtractUniqueShardTermsFromInput(input, nentries, extraData);
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

	Datum *indexEntries = ExtractUniqueShardTermsFromInput(input, nentries, extraData);

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
		ereport(ERROR, (errmsg("document should not be null")));
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
		context->terms.entries[i] = UUIDPGetDatum(uuid);
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


/*
 * Utility function that receives a unique shard document (i.e. document returned from the generate_unique_shard_document function),
 * inserts all terms in a hash table and returns it to the caller.
 */
static HTAB *
GetUniqueShardDocumentTermsHTAB(pgbson *uniqueShardDocument)
{
	HTAB *termsHashSet = CreateBsonValueHashSet();
	ProcessUniqueShardDocumentKeys(uniqueShardDocument, termsHashSet, HASH_ENTER);
	return termsHashSet;
}
