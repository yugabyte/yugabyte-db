/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/sharding.c
 *
 * Implementation of sharding functions.
 *
 *-------------------------------------------------------------------------
 */
#include <math.h>
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <executor/spi.h>
#include <lib/stringinfo.h>
#include <utils/builtins.h>
#include <nodes/makefuncs.h>

#include "io/bson_core.h"
#include "api_hooks.h"
#include "commands/create_indexes.h"
#include "utils/documentdb_errors.h"
#include "sharding/sharding.h"
#include "metadata/metadata_cache.h"
#include "utils/query_utils.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "metadata/collection.h"
#include "collation/collation.h"
#include "utils/guc_utils.h"
#include "utils/version_utils.h"
#include "utils/list_utils.h"

extern bool EnableNativeColocation;
extern int ShardingMaxChunks;
extern bool RecreateRetryTableOnSharding;
extern char *ApiGucPrefixV2;
extern bool EnablePrepareUnique;
extern bool ForceUpdateIndexInline;

/* Metadata about shard keys - this is unchanged through
 * iterating though the query for the shard key.
 */
typedef struct ShardKeyMetadata
{
	/* ordered array of shard key fields */
	const char **fields;
	int fieldCount;
} ShardKeyMetadata;

/*
 * ShardKeyFieldValues is used to keep track of shard key values in a query.
 * If we find a value for each shard key, then we will add a filter on
 * the shard_key_value column.
 *
 * We currently only extract a single shard key value based on top-level
 * filters and $and only.
 */
typedef struct ShardKeyFieldValues
{
	/* array of shard key values corresponding to shard key fields */
	bson_value_t *values;

	/* array specifying whether a field was set in the query */
	int *setCount;
} ShardKeyFieldValues;

/* When processing a $in, the set of values in the $in that are applied */
typedef struct ShardKeyInValueCount
{
	bson_value_t *values;

	int valueCount;
} ShardKeyInValueCount;

/*
 * Mode tracking sharding options
 */
typedef enum ShardCollectionMode
{
	/* It's a shard operation (unsharded -> sharded) */
	ShardCollectionMode_Shard = 1,

	/* It's a reshard operation (sharded -> sharded) */
	ShardCollectionMode_Reshard = 2,

	/* It's an unshard operation (sharded -> unsharded) */
	ShardCollectionMode_Unshard = 3,
} ShardCollectionMode;

/* Arguments to the shard collection and reshard collection functions
 * This is post-parsing and processing
 */
typedef struct ShardCollectionArgs
{
	/* The mongo database to act on*/
	char *databaseName;

	/* The mongo collection to act on*/
	char *collectionName;

	/* The shardkey if mode is != Unshard */
	pgbson *shardKeyDefinition;

	/* The number of chunks to create (0 means unset) */
	int numChunks;

	/* The command's sharding mode */
	ShardCollectionMode shardingMode;

	/* Whether or not to force redistribution on reshard */
	bool forceRedistribution;
} ShardCollectionArgs;

PG_FUNCTION_INFO_V1(command_get_shard_key_value);
PG_FUNCTION_INFO_V1(command_validate_shard_key);
PG_FUNCTION_INFO_V1(command_shard_collection);
PG_FUNCTION_INFO_V1(command_reshard_collection);
PG_FUNCTION_INFO_V1(command_unshard_collection);

static bson_value_t FindShardKeyFieldValue(bson_iter_t *docIter, const char *path);

static void InitShardKeyMetadata(pgbson *shardKeyBson,
								 ShardKeyMetadata *shardKeyMetadata);
static void InitShardKeyFieldValues(const ShardKeyMetadata *shardKeyMetadata,
									ShardKeyFieldValues *shardKeyValues);
static void CloneShardKeyFieldValues(const ShardKeyFieldValues *source,
									 const ShardKeyMetadata *shardKeyMetadata,
									 ShardKeyFieldValues *target);
static int ShardKeyFieldIndex(const ShardKeyMetadata *shardKey, const char *path);
static bool ComputeShardKeyFieldValuesHash(ShardKeyFieldValues *shardKeyValues,
										   const ShardKeyMetadata *shardKeyMetadata,
										   int64 *shardKeyHash,
										   bool *isShardKeyValueCollationAware);
static void ValidateShardKey(const pgbson *shardKeyDoc);
static void FindShardKeyFieldValuesForQuery(bson_iter_t *queryDocument,
											const ShardKeyMetadata *shardKeyMetadata,
											ShardKeyFieldValues *shardKeyValues);
static Expr * FindShardKeyValuesExprNew(bson_iter_t *queryDocIter, int
										collectionVarno,
										const ShardKeyMetadata *shardKeyMetadata,
										bool *isShardKeyValueCollationAware);

static void ShardCollectionCore(ShardCollectionArgs *args);
static void ShardCollectionLegacy(PG_FUNCTION_ARGS);
static void ParseShardCollectionRequest(pgbson *args, ShardCollectionArgs *shardArgs);
static void ParseReshardCollectionRequest(pgbson *args, ShardCollectionArgs *shardArgs);
static void ParseUnshardCollectionRequest(pgbson *args, ShardCollectionArgs *shardArgs);
static void RunPrepareUniqueForCollectionIndexes(const char *databaseName, const
												 char *collectionName, Datum
												 indexNamesArray);


/*
 * Top level command to shard a collection.
 */
Datum
command_shard_collection(PG_FUNCTION_ARGS)
{
	if (PG_NARGS() > 1)
	{
		ShardCollectionLegacy(fcinfo);
		PG_RETURN_VOID();
	}

	/* New function with 1 arg. */
	pgbson *shardArg = PG_GETARG_PGBSON_PACKED(0);

	if (!IsMetadataCoordinator())
	{
		StringInfo shardCollectionQuery = makeStringInfo();
		appendStringInfo(shardCollectionQuery,
						 "SELECT %s.shard_collection(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(PgbsonToHexadecimalString(shardArg)),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			shardCollectionQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Internal error sharding collection in metadata coordinator"),
							errdetail_log(
								"Internal error sharding collection in metadata coordinator via distributed call %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_VOID();
	}

	ShardCollectionArgs args = { 0 };
	ParseShardCollectionRequest(shardArg, &args);
	ShardCollectionCore(&args);
	PG_RETURN_VOID();
}


/*
 * Top level command to reshard a collection.
 */
Datum
command_reshard_collection(PG_FUNCTION_ARGS)
{
	pgbson *shardArg = PG_GETARG_PGBSON_PACKED(0);

	if (!IsMetadataCoordinator())
	{
		StringInfo shardCollectionQuery = makeStringInfo();
		appendStringInfo(shardCollectionQuery,
						 "SELECT %s.reshard_collection(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(PgbsonToHexadecimalString(shardArg)),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			shardCollectionQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Metadata coordinator encountered internal error while resharding the collection"),
							errdetail_log(
								"Metadata coordinator encountered internal error while resharding the collection via distributed call %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_VOID();
	}

	ShardCollectionArgs args = { 0 };
	ParseReshardCollectionRequest(shardArg, &args);
	ShardCollectionCore(&args);
	PG_RETURN_VOID();
}


/*
 * Top level command to unshard a collection.
 */
Datum
command_unshard_collection(PG_FUNCTION_ARGS)
{
	pgbson *shardArg = PG_GETARG_PGBSON_PACKED(0);

	if (!IsMetadataCoordinator())
	{
		StringInfo shardCollectionQuery = makeStringInfo();
		appendStringInfo(shardCollectionQuery,
						 "SELECT %s.unshard_collection(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(PgbsonToHexadecimalString(shardArg)),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			shardCollectionQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Internal error unsharding collection in metadata coordinator"),
							errdetail_log(
								"Internal error unsharding collection in metadata coordinator via distributed call %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_VOID();
	}

	ShardCollectionArgs args = { 0 };
	ParseUnshardCollectionRequest(shardArg, &args);
	ShardCollectionCore(&args);
	PG_RETURN_VOID();
}


/*
 * command_get_shard_key_value generates the shard key value for a given
 * shard key and document. Returns the collection_id if there's no shard_key.
 */
Datum
command_get_shard_key_value(PG_FUNCTION_ARGS)
{
	pgbson *shardKeyDoc = PG_ARGISNULL(0) ? NULL : PG_GETARG_PGBSON(0);
	int64_t collectionId = PG_GETARG_INT64(1);
	pgbson *document = PG_GETARG_PGBSON(2);

	int64 shardKeyValue = ComputeShardKeyHashForDocument(shardKeyDoc,
														 (uint64_t) collectionId,
														 document);

	PG_RETURN_INT64(shardKeyValue);
}


/*
 * ComputeShardKeyHashForDocument computes the shard key has for a given
 * shard key definition (we assume all fields are "hashed") and document.
 * Returns the collection_id if there's no shard_key.
 */
int64
ComputeShardKeyHashForDocument(pgbson *shardKeyDoc, uint64_t collectionId,
							   pgbson *document)
{
	if (shardKeyDoc == NULL)
	{
		return *(int64_t *) &collectionId;
	}

	int64 shardKeyValue = 0;

	bson_iter_t shardKeyIterator;
	PgbsonInitIterator(shardKeyDoc, &shardKeyIterator);
	while (bson_iter_next(&shardKeyIterator))
	{
		const char *shardKey = bson_iter_key(&shardKeyIterator);

		/* top-level iterator for the document */
		bson_iter_t documentIterator;
		PgbsonInitIterator(document, &documentIterator);

		/*
		 * Value at the shardKey path, or a "null" typed bson_value_t if the
		 * document doesn't have a field at shardKey path.
		 */
		bson_value_t value = FindShardKeyFieldValue(&documentIterator, shardKey);

		/* use the current value as seed */
		shardKeyValue = BsonValueHash(&value, shardKeyValue);
	}

	return shardKeyValue;
}


/*
 * FindShardKeyFieldValue recursively resolves an a.b.c path in the given
 * document iterator. If found, it returns the bson value at the path.
 * Otherwise, returns a "null" typed bson_value_t since not specifying
 * the sharding field is equivalent to setting it to a "null" value in
 * Mongo.
 *
 * If the path contains an unsupported type for a shard key, we throw
 * an error.
 */
static bson_value_t
FindShardKeyFieldValue(bson_iter_t *docIter, const char *path)
{
	char *dot = NULL;
	size_t fieldLength;

	if ((dot = strchr(path, '.')))
	{
		fieldLength = dot - path;
	}
	else
	{
		fieldLength = strlen(path);
	}

	if (bson_iter_find_w_len(docIter, path, fieldLength))
	{
		if (!dot)
		{
			if (BSON_ITER_HOLDS_ARRAY(docIter))
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg(
									"A shard key is not permitted to include any array elements.")));
			}
			else if (BSON_ITER_HOLDS_REGEX(docIter))
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg(
									"A shard key is not allowed to include any regular expression pattern.")));
			}
			else if (BSON_ITER_HOLDS_UNDEFINED(docIter))
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("Shard key cannot be undefined.")));
			}

			/* found a specific value */
			return *bson_iter_value(docIter);
		}

		if (BSON_ITER_HOLDS_ARRAY(docIter))
		{
			/* shard key path that contains array is invalid */
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Shard key is not allowed to include array values or any array-derived elements.")));
		}
		else if (BSON_ITER_HOLDS_DOCUMENT(docIter))
		{
			/* recurse into object */
			bson_iter_t childIter;
			if (bson_iter_recurse(docIter, &childIter))
			{
				return FindShardKeyFieldValue(&childIter, dot + 1);
			}
		}
	}

	const bson_value_t nullValue = {
		.value_type = BSON_TYPE_NULL
	};
	return nullValue;
}


/*
 * command_validate_shard_key throws an error if the given shard key
 * BSON is not valid for the current extension.
 */
Datum
command_validate_shard_key(PG_FUNCTION_ARGS)
{
	pgbson *shardKeyDoc = PG_GETARG_PGBSON(0);
	ValidateShardKey(shardKeyDoc);
	PG_RETURN_VOID();
}


static void
ValidateShardKey(const pgbson *shardKeyDoc)
{
	bson_iter_t shardKeyIterator;
	PgbsonInitIterator(shardKeyDoc, &shardKeyIterator);

	while (bson_iter_next(&shardKeyIterator))
	{
		const bson_value_t *value = bson_iter_value(&shardKeyIterator);

		if (value->value_type == BSON_TYPE_UTF8)
		{
			if (strcmp("hashed", value->value.v_utf8.str) != 0)
			{
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg("Shard key value provided is invalid: %s",
									   value->value.v_utf8.str)));
			}
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
							errmsg("only shard keys that use hashed are supported")));
		}
	}
}


static void
InitShardKeyMetadata(pgbson *shardKeyBson, ShardKeyMetadata *shardKeyMetadata)
{
	int shardKeyCount = PgbsonCountKeys(shardKeyBson);
	shardKeyMetadata->fieldCount = shardKeyCount;
	shardKeyMetadata->fields = palloc(shardKeyCount * sizeof(const char *));

	/* build array of shard key field names */
	bson_iter_t shardKeyIter;
	PgbsonInitIterator(shardKeyBson, &shardKeyIter);

	for (int fieldIndex = 0; bson_iter_next(&shardKeyIter); fieldIndex++)
	{
		shardKeyMetadata->fields[fieldIndex] = bson_iter_key(&shardKeyIter);
	}
}


/*
 * InitShardKeyFieldValues initializes a ShardKeyFieldValues based on the shard
 * key.
 */
static void
InitShardKeyFieldValues(const ShardKeyMetadata *metadata,
						ShardKeyFieldValues *shardKeyValues)
{
	int shardKeyCount = metadata->fieldCount;

	/* prepare data structure for storing shard key field values */
	shardKeyValues->values = palloc0(shardKeyCount * sizeof(bson_value_t));
	shardKeyValues->setCount = palloc0(shardKeyCount * sizeof(int));
}


static void
CloneShardKeyFieldValues(const ShardKeyFieldValues *source,
						 const ShardKeyMetadata *shardKeyMetadata,
						 ShardKeyFieldValues *target)
{
	InitShardKeyFieldValues(shardKeyMetadata, target);
	for (int fieldIndex = 0; fieldIndex < shardKeyMetadata->fieldCount; fieldIndex++)
	{
		target->values[fieldIndex] = source->values[fieldIndex];
		target->setCount[fieldIndex] = source->setCount[fieldIndex];
	}
}


/*
 * ShardKeyFieldIndex returns the index of path in shardKey.fields
 * or -1 if it is not found.
 */
int
ShardKeyFieldIndex(const ShardKeyMetadata *shardKey, const char *path)
{
	for (int fieldIndex = 0; fieldIndex < shardKey->fieldCount; fieldIndex++)
	{
		if (strcmp(path, shardKey->fields[fieldIndex]) == 0)
		{
			return fieldIndex;
		}
	}

	return -1;
}


/*
 * ComputeShardKeyFieldValuesHash returns whether all fields of the given
 * shardKeyValues are set and if so writes the hash of the shard key
 * values to shardKeyHash and sets isShardKeyFieldValueCollationAware.
 * NOTE: This method should only be used in the processing of query filters.
 */
bool
ComputeShardKeyFieldValuesHash(ShardKeyFieldValues *shardKeyValues,
							   const ShardKeyMetadata *shardKeyMetadata,
							   int64 *shardKeyHash,
							   bool *isShardKeyValueCollationAware)
{
	*shardKeyHash = 0;
	bool checkCollationAware = false;
	for (int fieldIndex = 0; fieldIndex < shardKeyMetadata->fieldCount; fieldIndex++)
	{
		if (shardKeyValues->setCount[fieldIndex] == 0)
		{
			/* not all fields in the shard key were specified */
			return false;
		}

		/* Insert the given value into the hash table */
		bson_value_t *value = &(shardKeyValues->values[fieldIndex]);

		if (value->value_type == BSON_TYPE_REGEX)
		{
			/* Cannot compute hash if the value is given as regex */
			return false;
		}

		checkCollationAware = checkCollationAware ||
							  IsBsonTypeCollationAware(value->value_type);
		*shardKeyHash = BsonValueHash(value, *shardKeyHash);
	}

	/* preserve collation-sensitivity identified in prior OR branch */
	*isShardKeyValueCollationAware = *isShardKeyValueCollationAware ||
									 checkCollationAware;
	return true;
}


/*
 * ComputeShardKeyHashForQuery is the same as ComputeShardKeyHashForQueryValue but
 * with a pgbson instead.
 */
bool
ComputeShardKeyHashForQuery(pgbson *shardKey, uint64_t collectionId, pgbson *query,
							int64 *shardKeyHash, bool *isShardKeyValueCollationAware)
{
	if (shardKey == NULL)
	{
		*shardKeyHash = collectionId;
		*isShardKeyValueCollationAware = false;
		return true;
	}

	bson_value_t queryValue = ConvertPgbsonToBsonValue(query);
	return ComputeShardKeyHashForQueryValue(shardKey, collectionId, &queryValue,
											shardKeyHash, isShardKeyValueCollationAware);
}


/*
 * Computes a new shard key expression given query values.
 */
Expr *
ComputeShardKeyExprForQueryValue(pgbson *shardKey, uint64_t collectionId, const
								 bson_value_t *queryDocument, int32_t collectionVarno,
								 bool *isShardKeyValueCollationAware)
{
	if (shardKey == NULL)
	{
		Datum shardKeyFieldValuesHashDatum = Int64GetDatum(collectionId);
		Const *shardKeyValueConst = makeConst(INT8OID, -1, InvalidOid, 8,
											  shardKeyFieldValuesHashDatum, false, true);

		/* construct document <operator> <value> expression */
		return CreateShardKeyValueFilter(collectionVarno, shardKeyValueConst);
	}

	bson_iter_t queryDocIter;
	BsonValueInitIterator(queryDocument, &queryDocIter);

	ShardKeyMetadata shardKeyMetadata;
	InitShardKeyMetadata(shardKey, &shardKeyMetadata);

	return FindShardKeyValuesExprNew(&queryDocIter, collectionVarno,
									 &shardKeyMetadata,
									 isShardKeyValueCollationAware);
}


/*
 * CreateZeroShardKeyValueFilter creates a filter of the form shard_key_value = <value>
 * for the given varno (read: rtable index).
 */
Expr *
CreateShardKeyValueFilter(int collectionVarno, Const *valueConst)
{
	/* shard_key_value is always the first column in our data tables */
	AttrNumber shardKeyAttNum = DOCUMENT_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER;
	Var *shardKeyValueVar = makeVar(collectionVarno, shardKeyAttNum, INT8OID, -1,
									InvalidOid, 0);

	/* construct document <operator> <value> expression */
	Expr *shardKeyValueFilter = make_opclause(BigintEqualOperatorId(), BOOLOID, false,
											  (Expr *) shardKeyValueVar,
											  (Expr *) valueConst, InvalidOid,
											  InvalidOid);

	return shardKeyValueFilter;
}


/*
 * ComputeShardKeyHashForQueryValue returns whether the given query filters all
 * shard key fields by a specific value and computes the hash of the values.
 */
bool
ComputeShardKeyHashForQueryValue(pgbson *shardKey, uint64_t collectionId,
								 const bson_value_t *query, int64 *shardKeyHash,
								 bool *isShardKeyValueCollationAware)
{
	if (shardKey == NULL)
	{
		*shardKeyHash = collectionId;
		*isShardKeyValueCollationAware = false;
		return true;
	}

	ShardKeyMetadata shardKeyMetadata;
	InitShardKeyMetadata(shardKey, &shardKeyMetadata);

	ShardKeyFieldValues shardKeyValues;
	InitShardKeyFieldValues(&shardKeyMetadata, &shardKeyValues);

	bson_iter_t queryDocIter;
	BsonValueInitIterator(query, &queryDocIter);

	/* determine the shard key field values from the query BSON */
	FindShardKeyFieldValuesForQuery(&queryDocIter, &shardKeyMetadata, &shardKeyValues);

	/* compute the hash, returns false if not all shard key fields are set */
	return ComputeShardKeyFieldValuesHash(&shardKeyValues, &shardKeyMetadata,
										  shardKeyHash,
										  isShardKeyValueCollationAware);
}


/*
 * FindShardKeyFieldValuesForQuery analyzes a query document to find the shard
 * key values.
 *
 * Currently, it considers only $and, <field>:{"$eq":<value>}, and <field>:<value>
 * clauses.
 *
 * Additionally, we look for exact matches with shard key fields, e.g. if the
 * shard key is a.b.c, we only look for equality filters on "a.b.c".
 *
 * In case a field has multiple equality filters, we use the latest one. In this scenario,
 * the query is either of the form {"$and":[{"a":1},{"a":1}], in which case using the
 * latest one does not affect the result. Or of the form {"$and":[{"a":1},{"a":2}]} in
 * which case the query effectively evaluates to false. In that case, the result is
 * irrelevant, since the query will be noop whereever we send it. We prefer still
 * computing a shard key value to make sure the query goes to at most 1 shard.
 *
 * In the future, we may want to expand this logic to compute multiple possible shard
 * key values in case of $or or $in.
 */
static void
FindShardKeyFieldValuesForQuery(bson_iter_t *queryDocument,
								const ShardKeyMetadata *shardKeyMetadata,
								ShardKeyFieldValues *shardKeyValues)
{
	while (bson_iter_next(queryDocument))
	{
		const char *key = bson_iter_key(queryDocument);

		if (strcmp(key, "$and") == 0)
		{
			bson_iter_t andIterator;
			if (!BSON_ITER_HOLDS_ARRAY(queryDocument) ||
				!bson_iter_recurse(queryDocument, &andIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Could not iterate through query document "
									   "$and.")));
			}

			while (bson_iter_next(&andIterator))
			{
				bson_iter_t andElementIterator;
				if (!BSON_ITER_HOLDS_DOCUMENT(&andIterator) ||
					!bson_iter_recurse(&andIterator, &andElementIterator))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg("Could not iterate through elements within "
										   "$and query.")));
				}

				FindShardKeyFieldValuesForQuery(&andElementIterator, shardKeyMetadata,
												shardKeyValues);
			}
		}
		else if (key[0] == '$')
		{
			/* ignore other operators */
		}
		else
		{
			/* key is a path rather than an operator */
			int fieldIndex = ShardKeyFieldIndex(shardKeyMetadata, key);
			if (fieldIndex < 0)
			{
				/* key is not one of the shard key paths */
				continue;
			}

			bson_iter_t shardKeyFieldValueIter;

			/* if the value under key is a non-empty document, it may be an operator  */
			if (BSON_ITER_HOLDS_DOCUMENT(queryDocument) &&
				bson_iter_recurse(queryDocument, &shardKeyFieldValueIter) &&
				bson_iter_next(&shardKeyFieldValueIter))
			{
				const char *firstKey = bson_iter_key(&shardKeyFieldValueIter);

				/* if the first key starts with $, it's an operator document */
				if (firstKey[0] == '$')
				{
					/* check for $eq operator */
					do {
						const char *operatorName = bson_iter_key(&shardKeyFieldValueIter);
						if (strcmp(operatorName, "$eq") == 0)
						{
							/* query has the form <field>:{"$eq":<value>} */
							shardKeyValues->values[fieldIndex] =
								*bson_iter_value(&shardKeyFieldValueIter);
							shardKeyValues->setCount[fieldIndex] = 1;
						}
					} while (bson_iter_next(&shardKeyFieldValueIter));

					continue;
				}

				/* if the key is not an operator, fall through */
			}

			/* query has the form <field>:<value> */
			shardKeyValues->values[fieldIndex] = *bson_iter_value(queryDocument);
			shardKeyValues->setCount[fieldIndex] = 1;
		}
	}
}


static Expr *
CreateShardKeyFilterCore(const ShardKeyMetadata *shardKeyMetadata,
						 ShardKeyFieldValues *fieldValues,
						 bool *isShardKeyValueCollationAware,
						 int collectionVarno)
{
	int64_t shardKeyHash;
	if (ComputeShardKeyFieldValuesHash(fieldValues, shardKeyMetadata, &shardKeyHash,
									   isShardKeyValueCollationAware))
	{
		/* Single shard key found via series of ANDs */
		Datum shardKeyFieldValuesHashDatum = Int64GetDatum(shardKeyHash);
		Const *shardKeyValueConst = makeConst(INT8OID, -1, InvalidOid, 8,
											  shardKeyFieldValuesHashDatum, false, true);

		/* construct document <operator> <value> expression */
		return CreateShardKeyValueFilter(collectionVarno, shardKeyValueConst);
	}

	return NULL;
}


static void
FindShardKeyValuesExprPhase1(bson_iter_t *queryDocIter, const ShardKeyMetadata *metadata,
							 ShardKeyFieldValues *fieldValues, List **orClauses,
							 ShardKeyFieldValues *inClauses)
{
	while (bson_iter_next(queryDocIter))
	{
		const char *key = bson_iter_key(queryDocIter);

		if (strcmp(key, "$and") == 0)
		{
			bson_iter_t andIterator;
			if (!BSON_ITER_HOLDS_ARRAY(queryDocIter) ||
				!bson_iter_recurse(queryDocIter, &andIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Could not iterate through query document "
									   "$and.")));
			}

			while (bson_iter_next(&andIterator))
			{
				bson_iter_t andElementIterator;
				if (!BSON_ITER_HOLDS_DOCUMENT(&andIterator) ||
					!bson_iter_recurse(&andIterator, &andElementIterator))
				{
					ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
									errmsg("Could not iterate through elements within "
										   "$and query.")));
				}

				FindShardKeyValuesExprPhase1(&andElementIterator, metadata, fieldValues,
											 orClauses, inClauses);
			}
		}
		else if (strcmp(key, "$or") == 0 && orClauses)
		{
			bson_value_t *orValue = palloc(sizeof(bson_value_t));
			*orValue = *bson_iter_value(queryDocIter);
			*orClauses = lappend(*orClauses, orValue);
		}
		else if (key[0] == '$')
		{
			/* ignore other top level operators */
		}
		else
		{
			/* key is a path rather than an operator */
			int fieldIndex = ShardKeyFieldIndex(metadata, key);
			if (fieldIndex < 0)
			{
				/* key is not one of the shard key paths */
				continue;
			}

			bson_iter_t shardKeyFieldValueIter;

			/* if the value under key is a non-empty document, it may be an operator  */
			if (BSON_ITER_HOLDS_DOCUMENT(queryDocIter) &&
				bson_iter_recurse(queryDocIter, &shardKeyFieldValueIter) &&
				bson_iter_next(&shardKeyFieldValueIter))
			{
				const char *firstKey = bson_iter_key(&shardKeyFieldValueIter);

				/* if the first key starts with $, it's an operator document */
				if (firstKey[0] == '$')
				{
					/* check for $eq operator */
					do {
						const char *operatorName = bson_iter_key(&shardKeyFieldValueIter);
						if (strcmp(operatorName, "$eq") == 0)
						{
							/* query has the form <field>:{"$eq":<value>} */
							fieldValues->values[fieldIndex] =
								*bson_iter_value(&shardKeyFieldValueIter);
							fieldValues->setCount[fieldIndex] = 1;
						}
						else if (strcmp(operatorName, "$in") == 0 && inClauses)
						{
							inClauses->values[fieldIndex] =
								*bson_iter_value(&shardKeyFieldValueIter);
							inClauses->setCount[fieldIndex]++;
						}
					} while (bson_iter_next(&shardKeyFieldValueIter));

					continue;
				}

				/* if the key is not an operator, fall through */
			}

			/* query has the form <field>:<value> */
			fieldValues->values[fieldIndex] = *bson_iter_value(queryDocIter);
			fieldValues->setCount[fieldIndex] = 1;
		}
	}
}


static Expr *
ConsiderShardKeyFiltersForInClauses(const ShardKeyMetadata *shardKeyMetadata,
									ShardKeyFieldValues *fieldValues,
									ShardKeyFieldValues *inFieldValues,
									bool *isShardKeyValueCollationAware,
									int collectionVarno)
{
	/* First pass, sanity checks */
	int totalShardKeys = 1;
	ShardKeyInValueCount *shardKeyValue = palloc0(shardKeyMetadata->fieldCount *
												  sizeof(ShardKeyInValueCount));
	for (int i = 0; i < shardKeyMetadata->fieldCount; i++)
	{
		if (fieldValues->setCount[i] > 0 && inFieldValues->setCount[i] > 0)
		{
			/* Both were set, ignore the in case */
			inFieldValues->setCount[i] = 0;
		}

		if (inFieldValues->setCount[i] > 1)
		{
			/* More than 1 $in specified on a path and it's not in the required path
			 * we can't safely determine shardKeyValue - bail.
			 */
			return NULL;
		}

		/* If the value is covered by at least required and in, then increment count */
		if (fieldValues->setCount[i] > 0)
		{
			shardKeyValue[i].valueCount = 1;
			shardKeyValue[i].values = palloc(sizeof(bson_value_t));
			shardKeyValue[i].values[0] = fieldValues->values[i];
		}
		else if (inFieldValues->setCount[i] > 0)
		{
			int numValues = BsonDocumentValueCountKeys(&inFieldValues->values[i]);
			bson_iter_t inIterator;
			BsonValueInitIterator(&inFieldValues->values[i], &inIterator);
			shardKeyValue[i].valueCount = numValues;
			shardKeyValue[i].values = palloc(numValues * sizeof(bson_value_t));
			totalShardKeys = totalShardKeys * numValues;

			int inIdx = 0;
			while (bson_iter_next(&inIterator))
			{
				shardKeyValue[i].values[inIdx++] = *bson_iter_value(&inIterator);
			}
		}
		else
		{
			/* Neither values are set - we can't get to a shard key */
			return NULL;
		}
	}

	/* The product of $ins produce totalShardKeys terms. If we got here, there's at least 1 value or in available */
	List *shardKeyValueExprs = NIL;
	ShardKeyFieldValues currentValues;
	InitShardKeyFieldValues(shardKeyMetadata, &currentValues);
	for (int i = 0; i < totalShardKeys; i++)
	{
		int indexToUse = i;
		for (int j = 0; j < shardKeyMetadata->fieldCount; j++)
		{
			int valueIndex = indexToUse % shardKeyValue[j].valueCount;
			currentValues.values[j] = shardKeyValue[j].values[valueIndex];
			currentValues.setCount[j] = 1;
			indexToUse = indexToUse / shardKeyValue[j].valueCount;
		}

		Expr *shardKeyFilter = CreateShardKeyFilterCore(shardKeyMetadata, &currentValues,
														isShardKeyValueCollationAware,
														collectionVarno);
		if (shardKeyFilter != NULL)
		{
			shardKeyValueExprs = lappend(shardKeyValueExprs, shardKeyFilter);
		}
	}


	for (int i = 0; i < shardKeyMetadata->fieldCount; i++)
	{
		pfree(shardKeyValue[i].values);
	}

	pfree(shardKeyValue);

	if (list_length(shardKeyValueExprs) == 0)
	{
		return NULL;
	}
	else if (list_length(shardKeyValueExprs) == 1)
	{
		/* If we have only one $or clause, return it */
		return (Expr *) linitial(shardKeyValueExprs);
	}
	else
	{
		BoolExpr *logicalExpr = makeNode(BoolExpr);
		logicalExpr->boolop = OR_EXPR;
		logicalExpr->args = shardKeyValueExprs;
		logicalExpr->location = -1;

		return (Expr *) logicalExpr;
	}
}


static Expr *
ConsiderShardKeyFiltersForOrClauses(const ShardKeyMetadata *shardKeyMetadata,
									ShardKeyFieldValues *fieldValues,
									List *orClauses,
									bool *isShardKeyValueCollationAware,
									int collectionVarno)
{
	List *shardKeyMultiClause = NIL;
	ListCell *orCell;
	foreach(orCell, orClauses)
	{
		bson_value_t *orValue = (bson_value_t *) lfirst(orCell);
		bson_iter_t orIterator;
		BsonValueInitIterator(orValue, &orIterator);

		/* If every arm of the $or has a shard key value filter then it's safe to pull up */
		List *orExprs = NIL;
		while (bson_iter_next(&orIterator))
		{
			bson_iter_t orElementIterator;
			if (!BSON_ITER_HOLDS_DOCUMENT(&orIterator) ||
				!bson_iter_recurse(&orIterator, &orElementIterator))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("Could not iterate through elements within "
									   "$or query.")));
			}

			ShardKeyFieldValues orValues;
			CloneShardKeyFieldValues(fieldValues, shardKeyMetadata, &orValues);
			FindShardKeyValuesExprPhase1(&orElementIterator, shardKeyMetadata,
										 &orValues, NULL, NULL);
			Expr *orExpr = CreateShardKeyFilterCore(shardKeyMetadata, &orValues,
													isShardKeyValueCollationAware,
													collectionVarno);
			if (orExpr != NULL)
			{
				orExprs = lappend(orExprs, orExpr);
			}
			else
			{
				list_free_deep(orExprs);
				orExprs = NIL;
				break;
			}
		}

		if (orExprs != NIL)
		{
			BoolExpr *logicalExpr = makeNode(BoolExpr);
			logicalExpr->boolop = OR_EXPR;
			logicalExpr->args = orExprs;
			logicalExpr->location = -1;

			shardKeyMultiClause = lappend(shardKeyMultiClause, logicalExpr);
		}
	}

	if (list_length(shardKeyMultiClause) == 0)
	{
		return NULL;
	}
	else if (list_length(shardKeyMultiClause) == 1)
	{
		/* If we have only one $or clause, return it */
		return (Expr *) linitial(shardKeyMultiClause);
	}
	else
	{
		/* If we have multiple $or clauses, combine them into a single expression */
		return make_ands_explicit(shardKeyMultiClause);
	}
}


static Expr *
FindShardKeyValuesExprNew(bson_iter_t *queryDocIter,
						  int collectionVarno,
						  const ShardKeyMetadata *shardKeyMetadata,
						  bool *isShardKeyValueCollationAware)
{
	/* Phase 1, collect the shard keys from all the $eq and $ands
	 * All top level $ors are collected separately as are $ins
	 */
	ShardKeyFieldValues fieldValues;
	InitShardKeyFieldValues(shardKeyMetadata, &fieldValues);

	List *orClauses = NIL;
	ShardKeyFieldValues inFieldValues;
	InitShardKeyFieldValues(shardKeyMetadata, &inFieldValues);
	FindShardKeyValuesExprPhase1(queryDocIter, shardKeyMetadata, &fieldValues, &orClauses,
								 &inFieldValues);

	/* First, see if all the required paths yield a shard key */
	Expr *singleKey = CreateShardKeyFilterCore(shardKeyMetadata, &fieldValues,
											   isShardKeyValueCollationAware,
											   collectionVarno);
	if (singleKey != NULL)
	{
		/* We have a single shard key value, return it */
		list_free_deep(orClauses);
		return singleKey;
	}

	/* Next try to see if $in clauses can satisfy the query */
	Expr *inBasedKey = ConsiderShardKeyFiltersForInClauses(shardKeyMetadata, &fieldValues,
														   &inFieldValues,
														   isShardKeyValueCollationAware,
														   collectionVarno);
	if (inBasedKey != NULL)
	{
		/* We have a single shard key value, return it */
		list_free_deep(orClauses);
		return inBasedKey;
	}

	/* Now, fieldValues is populated with all the entries that are *required*
	 * The orClauses & inClauses are populated with the $or and $in clauses
	 */
	return ConsiderShardKeyFiltersForOrClauses(shardKeyMetadata, &fieldValues,
											   orClauses, isShardKeyValueCollationAware,
											   collectionVarno);
}


static void
ShardCollectionLegacy(PG_FUNCTION_ARGS)
{
	pgbson *shardKey = PG_GETARG_PGBSON(2);

	ValidateShardKey(shardKey);

	Datum databaseDatum = PG_GETARG_DATUM(0);
	Datum collectionDatum = PG_GETARG_DATUM(1);

	char *databaseName = TextDatumGetCString(databaseDatum);
	char *collectionName = TextDatumGetCString(collectionDatum);
	bool isReshard = PG_GETARG_BOOL(3);

	if (!IsMetadataCoordinator())
	{
		StringInfo shardCollectionQuery = makeStringInfo();
		appendStringInfo(shardCollectionQuery,
						 "SELECT %s.shard_collection(%s,%s,%s::%s,%s)",
						 ApiSchemaName,
						 quote_literal_cstr(databaseName),
						 quote_literal_cstr(collectionName),
						 quote_literal_cstr(PgbsonToHexadecimalString(shardKey)),
						 FullBsonTypeName,
						 isReshard ? "true" : "false");
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			shardCollectionQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Internal error sharding collection in metadata coordinator"),
							errdetail_log(
								"Internal error sharding collection in metadata coordinator via distributed call %s",
								text_to_cstring(result.response))));
		}

		return;
	}

	ShardCollectionArgs args = { 0 };
	args.databaseName = databaseName;
	args.collectionName = collectionName;
	args.shardingMode = isReshard ? ShardCollectionMode_Reshard :
						ShardCollectionMode_Shard;
	args.shardKeyDefinition = shardKey;

	ShardCollectionCore(&args);
}


static void
ShardCollectionCore(ShardCollectionArgs *args)
{
	Datum databaseDatum = CStringGetTextDatum(args->databaseName);
	Datum collectionDatum = CStringGetTextDatum(args->collectionName);

	/* Allow for checking if reference metadata tables are correctly handled */
	EnsureMetadataTableReplicated("collections");

	MongoCollection *collection = GetMongoCollectionByNameDatum(
		databaseDatum, collectionDatum, AccessShareLock);

	if (collection == NULL)
	{
		if (args->shardingMode != ShardCollectionMode_Shard)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NAMESPACENOTSHARDED),
							errmsg("Collection %s.%s is not sharded",
								   args->databaseName, args->collectionName),
							errdetail_log(
								"Unable to perform re/unshard operation on a collection that is missing: %s.%s",
								args->databaseName, args->collectionName)));
		}

		CreateCollection(databaseDatum, collectionDatum);
		collection = GetMongoCollectionByNameDatum(
			databaseDatum, collectionDatum, AccessShareLock);

		Assert(collection != NULL);
	}

	if (collection->shardKey == NULL &&
		args->shardingMode != ShardCollectionMode_Shard)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NAMESPACENOTSHARDED),
						errmsg("Collection %s.%s is not sharded",
							   args->databaseName, args->collectionName),
						errdetail_log(
							"Can not re/unshard collection that is not sharded: %s.%s",
							args->databaseName, args->collectionName)));
	}

	if (!args->forceRedistribution &&
		collection->shardKey != NULL && PgbsonEquals(collection->shardKey,
													 args->shardKeyDefinition))
	{
		ereport(NOTICE, (errmsg(
							 "Skipping Sharding for collection %s.%s as the same options were passed in.",
							 args->databaseName, args->collectionName)));
		return;
	}

	if (collection->shardKey != NULL && args->shardingMode == ShardCollectionMode_Shard)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_ALREADYINITIALIZED),
						errmsg(
							"Sharding already enabled for collection %s.%s with options { \"_id\": \"%s.%s\", \"dropped\" : false, \"key\" : %s, \"unique\": false }.",
							args->databaseName, args->collectionName, args->databaseName,
							args->collectionName,
							PgbsonToJsonForLogging(args->shardKeyDefinition))));
	}

	int nargs = 3;
	Oid argTypes[3] = { BsonTypeId(), TEXTOID, TEXTOID };
	bool isNull = true;

	char argNulls[3] = { ' ', ' ', ' ' };
	Datum values[3] = { 0 };

	if (args->shardKeyDefinition == NULL)
	{
		argNulls[0] = 'n';
		values[0] = (Datum) 0;
	}
	else
	{
		values[0] = PointerGetDatum(args->shardKeyDefinition);
	}

	values[1] = databaseDatum;
	values[2] = collectionDatum;

	RunQueryWithCommutativeWrites(
		FormatSqlQuery("UPDATE %s.collections SET shard_key = $1"
					   " WHERE database_name = $2 AND collection_name = $3",
					   ApiCatalogSchemaName),
		nargs, argTypes, values, argNulls, SPI_OK_UPDATE, &isNull);

	/* Add 20 to the max table name length to account for the schema. */
	char qualifiedDataTableName[NAMEDATALEN + 20];
	sprintf(qualifiedDataTableName, "%s.%s", ApiDataSchemaName, collection->tableName);
	char tmpDataTableName[NAMEDATALEN + 20];
	sprintf(tmpDataTableName, "%s.%s_reshard", ApiDataSchemaName, collection->tableName);

	/* create a new table to reinsert the data into */
	StringInfo queryInfo = makeStringInfo();
	bool readOnly = false;
	appendStringInfo(queryInfo,
					 "CREATE TABLE %s (LIKE %s INCLUDING ALL EXCLUDING INDEXES)",
					 tmpDataTableName, qualifiedDataTableName);
	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	/* change the shard_key_value constraint */
	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 "ALTER TABLE %s DROP CONSTRAINT shard_key_value_check",
					 tmpDataTableName);
	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	resetStringInfo(queryInfo);
	if (args->shardingMode == ShardCollectionMode_Unshard)
	{
		appendStringInfo(queryInfo,
						 "ALTER TABLE %s ADD CONSTRAINT shard_key_value_check CHECK (shard_key_value = '%lu'::bigint)",
						 tmpDataTableName, collection->collectionId);
	}
	else
	{
		appendStringInfo(queryInfo,
						 "ALTER TABLE %s ADD CONSTRAINT shard_key_value_check"
						 " CHECK (shard_key_value = %s.get_shard_key_value(%s::%s, %lu, document))",
						 tmpDataTableName, ApiInternalSchemaName,
						 quote_literal_cstr(PgbsonToHexadecimalString(
												args->shardKeyDefinition)),
						 FullBsonTypeName,
						 collection->collectionId);
	}

	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	const char *colocateWith = EnableNativeColocation ? "none" : NULL;
	int shardCount = 0;
	if (args->numChunks > 0)
	{
		shardCount = args->numChunks;

		/* Custom numChunks requires breaking colocation */
		colocateWith = "none";
	}

	const char *distributionColumn = "shard_key_value";
	if (args->shardingMode == ShardCollectionMode_Unshard)
	{
		shardCount = 0;
		SetUnshardedColocationData(cstring_to_text(args->databaseName),
								   &distributionColumn, &colocateWith);
	}
	else if (shardCount == 1)
	{
		/*
		 * If we request 1 single shard, we can simply create a single shard distributed table
		 * which simplifies joins and pushdowns
		 */
		distributionColumn = NULL;
		shardCount = 0;
	}

	DistributePostgresTable(tmpDataTableName, distributionColumn, colocateWith,
							shardCount);

	/* apply the new shard key by re-inserting all data */
	resetStringInfo(queryInfo);

	nargs = 2;
	Oid insertArgTypes[2] = { BsonTypeId(), INT8OID };
	Datum insertArgValues[2] = { 0 };
	char insertArgNulls[2] = { ' ', ' ' };

	if (args->shardKeyDefinition == NULL)
	{
		insertArgValues[0] = (Datum) 0;
		insertArgNulls[0] = 'n';
	}
	else
	{
		insertArgValues[0] = PointerGetDatum(args->shardKeyDefinition);
	}

	insertArgValues[1] = UInt64GetDatum(collection->collectionId);

	if (collection->mongoDataCreationTimeVarAttrNumber != -1)
	{
		appendStringInfo(queryInfo,
						 "INSERT INTO %s (shard_key_value, object_id, document, creation_time)"
						 " SELECT %s.get_shard_key_value($1, $2, document), object_id, document, creation_time"
						 " FROM %s",
						 tmpDataTableName, ApiInternalSchemaName, qualifiedDataTableName);
	}
	else
	{
		appendStringInfo(queryInfo,
						 "INSERT INTO %s (shard_key_value, object_id, document)"
						 " SELECT %s.get_shard_key_value($1, $2, document), object_id, document"
						 " FROM %s",
						 tmpDataTableName, ApiInternalSchemaName, qualifiedDataTableName);
	}

	ExtensionExecuteQueryWithArgsViaSPI(queryInfo->data, nargs, insertArgTypes,
										insertArgValues, insertArgNulls, readOnly,
										SPI_OK_INSERT, &isNull);

	/*
	 * Failed to replace the old table with the newly provided table.
	 */
	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 "DROP TABLE %s", qualifiedDataTableName);

	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 "ALTER TABLE %s RENAME TO %s",
					 tmpDataTableName, collection->tableName);
	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	/* Update new table owner to admin role */
	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 "ALTER TABLE %s OWNER TO %s",
					 qualifiedDataTableName, ApiAdminRole);
	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	/* Make GUC default eventually: Recreate retry_table here with new shards */
	if (RecreateRetryTableOnSharding)
	{
		StringInfo retryTableNameInfo = makeStringInfo();
		appendStringInfo(retryTableNameInfo, "%s.retry_%lu", ApiDataSchemaName,
						 collection->collectionId);

		/* Recreate the retry table */
		resetStringInfo(queryInfo);
		appendStringInfo(queryInfo, "DROP TABLE %s", retryTableNameInfo->data);
		ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

		/* Since we're colocating with, shardCount should be 0 */
		int shardCountForRetry = 0;
		CreateRetryTable(retryTableNameInfo->data, qualifiedDataTableName,
						 distributionColumn, shardCountForRetry);
	}

	bool isPrepareUniqueArrayNull = true;
	Datum prepareUniqueNamesArray = (Datum) 0;

	bool isPrepareUniqueSupported = EnablePrepareUnique &&
									IsClusterVersionAtleast(DocDB_V0, 109, 0);
	if (isPrepareUniqueSupported)
	{
		/* Get prepareUnique index names that need to be converted after rebuilt. */
		resetStringInfo(queryInfo);
		appendStringInfo(queryInfo,
						 " SELECT array_agg((index_spec).index_name)::text[] FROM %s.collection_indexes WHERE "
						 "(index_is_valid OR %s.index_build_is_in_progress(index_id)) AND collection_id = %lu AND "
						 "(index_spec).index_options::%s OPERATOR(%s.@@) '{\"prepareUnique\": true}';",
						 ApiCatalogSchemaName, ApiInternalSchemaName,
						 collection->collectionId, FullBsonTypeName,
						 ApiCatalogSchemaName);

		prepareUniqueNamesArray = ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly,
															  SPI_OK_SELECT,
															  &isPrepareUniqueArrayNull);
	}

	/* Get all valid or in progress indexes and delete them from metadata entries related to the collection.
	 * TODO(MX): This really should not be CommutativeWrites for the entire query. Ideally only hte DELETE itself
	 * is commutative and is separate out from the other queries. This only really becomes a concern wiht MX
	 * and so for now this is left as-is.
	 */
	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 " WITH cte AS ("
					 " DELETE FROM %s.collection_indexes WHERE collection_id = %lu RETURNING *)"
					 " SELECT array_agg(%s.index_spec_as_bson(index_spec) ORDER BY index_id, '{}') FROM cte"
					 " WHERE index_is_valid OR %s.index_build_is_in_progress(index_id)",
					 ApiCatalogSchemaName, collection->collectionId,
					 ApiInternalSchemaName, ApiInternalSchemaName);

	bool isNullIndexSpecArray = true;
	Datum indexSpecArray = RunQueryWithCommutativeWrites(queryInfo->data, 0, NULL, NULL,
														 NULL, SPI_OK_SELECT,
														 &isNullIndexSpecArray);

	/* Create a vanilla RUM _id index but don't register it yet since we need to build it. */
	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 "SELECT %s.create_builtin_id_index(collection_id => %lu, register_id_index => false)",
					 ApiInternalSchemaName, collection->collectionId);
	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_SELECT, &isNull);

	if (!isNullIndexSpecArray)
	{
		pgbson_writer createIndexesArgWriter;
		PgbsonWriterInit(&createIndexesArgWriter);

		PgbsonWriterAppendUtf8(&createIndexesArgWriter, "createIndexes", 13,
							   args->collectionName);

		pgbson_element_writer elementWriter;
		PgbsonInitObjectElementWriter(&createIndexesArgWriter, &elementWriter,
									  "indexes", 7);
		PgbsonElementWriterWriteSQLValue(&elementWriter, isNullIndexSpecArray,
										 indexSpecArray, RECORDARRAYOID);

		/* Re-create valid indexes. */
		pgbson *createIndexesMsg = PgbsonWriterGetPgbson(&createIndexesArgWriter);


		/* Disable force GUCs and just follow the create index spec. */
		int savedGUCLevel = NewGUCNestLevel();
		SetGUCLocally(psprintf("%s.defaultUseCompositeOpClass", ApiGucPrefixV2), "false");

		bool buildAsUniqueForPrepareUnique = isPrepareUniqueSupported;
		CreateIndexesArg createIndexesArg = ParseCreateIndexesArg(databaseDatum,
																  createIndexesMsg,
																  buildAsUniqueForPrepareUnique);
		bool skipCheckCollectionCreate = createIndexesArg.blocking;
		bool uniqueIndexOnly = false;

		/* We call it good if it doesn't throw. */
		create_indexes_non_concurrently(databaseDatum, createIndexesArg,
										skipCheckCollectionCreate, uniqueIndexOnly);

		RollbackGUCChange(savedGUCLevel);

		/* if there were any prepareUnique indexes, run coll mod on the new collection to make them unique */
		if (!isPrepareUniqueArrayNull)
		{
			RunPrepareUniqueForCollectionIndexes(args->databaseName,
												 args->collectionName,
												 prepareUniqueNamesArray);
		}
	}
}


static void
RunPrepareUniqueForCollectionIndexes(const char *databaseName, const char *collectionName,
									 Datum indexNamesArray)
{
	Datum *elems = NULL;
	int nelems = 0;
	int nargs = 3;
	Oid argTypes[3] = { TEXTOID, TEXTOID, BsonTypeId() };
	Datum values[3];
	values[0] = CStringGetTextDatum(databaseName);
	values[1] = CStringGetTextDatum(collectionName);
	char *argNulls = NULL;
	bool readOnly = false;

	ArrayExtractDatums(DatumGetArrayTypeP(indexNamesArray), TEXTOID,
					   &elems, NULL, &nelems);

	StringInfo queryInfo = makeStringInfo();
	appendStringInfo(queryInfo,
					 "SELECT %s.coll_mod($1, $2, $3)",
					 ApiSchemaNameV2);

	for (int i = 0; i < nelems; i++)
	{
		const char *indexName = TextDatumGetCString(elems[i]);
		pgbson_writer collModSpecWriter;
		PgbsonWriterInit(&collModSpecWriter);
		PgbsonWriterAppendUtf8(&collModSpecWriter, "collMod", 7,
							   collectionName);

		pgbson_writer indexArgWriter;
		PgbsonWriterStartDocument(&collModSpecWriter, "index", 5, &indexArgWriter);
		PgbsonWriterAppendUtf8(&indexArgWriter, "name", 4,
							   indexName);
		PgbsonWriterAppendBool(&indexArgWriter, "prepareUnique", 13, true);
		PgbsonWriterEndDocument(&collModSpecWriter, &indexArgWriter);

		values[2] = PointerGetDatum(PgbsonWriterGetPgbson(&collModSpecWriter));

		bool isNull = true;
		ExtensionExecuteQueryWithArgsViaSPI(queryInfo->data, nargs, argTypes,
											values, argNulls, readOnly,
											SPI_OK_SELECT, &isNull);

		if (isNull)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Failed to convert prepareUnique index: \"%s\" when sharding the collection",
								indexName)));
		}
	}
}


void
ParseNamespaceName(const char *namespacePath, char **databaseName, char **collectionName)
{
	StringView strView = CreateStringViewFromString(namespacePath);
	StringView databaseView = StringViewFindPrefix(&strView, '.');
	if (databaseView.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("name needs to be fully qualified <db>.<collection>")));
	}

	if (strView.length < databaseView.length + 2)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg("name needs to be fully qualified <db>.<collection>")));
	}

	StringView collectionView = StringViewSubstring(&strView, databaseView.length + 1);

	ValidateDatabaseCollection(
		PointerGetDatum(cstring_to_text_with_len(databaseView.string,
												 databaseView.length)),
		PointerGetDatum(cstring_to_text_with_len(collectionView.string,
												 collectionView.length)));

	*databaseName = pnstrdup(databaseView.string, databaseView.length);
	*collectionName = pnstrdup(collectionView.string, collectionView.length);
}


static void
ParseShardCollectionRequest(pgbson *args, ShardCollectionArgs *shardArgs)
{
	bson_iter_t argIter;
	PgbsonInitIterator(args, &argIter);
	while (bson_iter_next(&argIter))
	{
		const char *key = bson_iter_key(&argIter);
		if (strcmp(key, "shardCollection") == 0)
		{
			EnsureTopLevelFieldType("shardCollection", &argIter, BSON_TYPE_UTF8);
			ParseNamespaceName(bson_iter_utf8(&argIter, NULL), &shardArgs->databaseName,
							   &shardArgs->collectionName);
		}
		else if (strcmp(key, "key") == 0)
		{
			EnsureTopLevelFieldType("key", &argIter, BSON_TYPE_DOCUMENT);
			shardArgs->shardKeyDefinition = PgbsonInitFromDocumentBsonValue(
				bson_iter_value(&argIter));
		}
		else if (strcmp(key, "unique") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("unique", &argIter);
			if (bson_iter_as_bool(&argIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("hashed shard keys cannot be declared unique.")));
			}
		}
		else if (strcmp(key, "numInitialChunks") == 0)
		{
			EnsureTopLevelFieldIsNumberLike("numInitialChunks", bson_iter_value(
												&argIter));
			shardArgs->numChunks = BsonValueAsInt32(bson_iter_value(&argIter));

			if (shardArgs->numChunks <= 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("numInitialChunks must be a positive number")));
			}

			if (shardArgs->numChunks > ShardingMaxChunks)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("numInitialChunks must be less than %d.",
									   ShardingMaxChunks)));
			}
		}
		else if (strcmp(key, "collation") == 0)
		{
			EnsureTopLevelFieldType("collation", &argIter, BSON_TYPE_DOCUMENT);
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Collation for sharded collections is currently unsupported")));
		}
		else if (strcmp(key, "timeseries") == 0)
		{
			EnsureTopLevelFieldType("timeseries", &argIter, BSON_TYPE_DOCUMENT);
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"timeseries on shard collection is not supported yet")));
		}
		else if (strcmp(key, "presplitHashedZones") == 0)
		{
			/* Ignored */
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			/* ignore */
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unrecognized key %s", key)));
		}
	}

	/* Validate required fields */
	if (shardArgs->collectionName == NULL ||
		shardArgs->databaseName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("shardCollection is a required field.")));
	}

	if (shardArgs->shardKeyDefinition == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("The key parameter is required.")));
	}

	ValidateShardKey(shardArgs->shardKeyDefinition);
	shardArgs->shardingMode = ShardCollectionMode_Shard;
}


static void
ParseReshardCollectionRequest(pgbson *args, ShardCollectionArgs *shardArgs)
{
	bson_iter_t argIter;
	PgbsonInitIterator(args, &argIter);
	while (bson_iter_next(&argIter))
	{
		const char *key = bson_iter_key(&argIter);
		if (strcmp(key, "reshardCollection") == 0)
		{
			EnsureTopLevelFieldType("reshardCollection", &argIter, BSON_TYPE_UTF8);
			ParseNamespaceName(bson_iter_utf8(&argIter, NULL), &shardArgs->databaseName,
							   &shardArgs->collectionName);
		}
		else if (strcmp(key, "key") == 0)
		{
			EnsureTopLevelFieldType("key", &argIter, BSON_TYPE_DOCUMENT);
			shardArgs->shardKeyDefinition = PgbsonInitFromDocumentBsonValue(
				bson_iter_value(&argIter));
		}
		else if (strcmp(key, "unique") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("unique", &argIter);
			if (bson_iter_as_bool(&argIter))
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("hashed shard keys cannot be declared unique.")));
			}
		}
		else if (strcmp(key, "numInitialChunks") == 0)
		{
			EnsureTopLevelFieldIsNumberLike("numInitialChunks", bson_iter_value(
												&argIter));
			shardArgs->numChunks = BsonValueAsInt32(bson_iter_value(&argIter));

			if (shardArgs->numChunks <= 0)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
								errmsg("numInitialChunks must be a positive number")));
			}

			if (shardArgs->numChunks > ShardingMaxChunks)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
								errmsg("numInitialChunks must be less than %d.",
									   ShardingMaxChunks)));
			}
		}
		else if (strcmp(key, "collation") == 0)
		{
			EnsureTopLevelFieldType("collation", &argIter, BSON_TYPE_DOCUMENT);
			ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							errmsg(
								"Collation for sharded collections is currently unsupported")));
		}
		else if (strcmp(key, "zones") == 0)
		{
			/* Ignored */
		}
		else if (strcmp(key, "forceRedistribution") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("forceRedistribution", &argIter);
			shardArgs->forceRedistribution = BsonValueAsBool(bson_iter_value(&argIter));
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			/* ignore */
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unrecognized key %s", key)));
		}
	}

	/* Validate required fields */
	if (shardArgs->collectionName == NULL ||
		shardArgs->databaseName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("reshardCollection is a required field.")));
	}

	if (shardArgs->shardKeyDefinition == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("The key parameter is required.")));
	}

	ValidateShardKey(shardArgs->shardKeyDefinition);
	shardArgs->shardingMode = ShardCollectionMode_Reshard;
}


static void
ParseUnshardCollectionRequest(pgbson *args, ShardCollectionArgs *shardArgs)
{
	bson_iter_t argIter;
	PgbsonInitIterator(args, &argIter);
	while (bson_iter_next(&argIter))
	{
		const char *key = bson_iter_key(&argIter);
		if (strcmp(key, "unshardCollection") == 0)
		{
			EnsureTopLevelFieldType("unshardCollection", &argIter, BSON_TYPE_UTF8);
			ParseNamespaceName(bson_iter_utf8(&argIter, NULL), &shardArgs->databaseName,
							   &shardArgs->collectionName);
		}
		else if (strcmp(key, "toShard") == 0)
		{
			EnsureTopLevelFieldType("toShard", &argIter, BSON_TYPE_UTF8);
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
							errmsg("unshardCollection with toShard not supported yet")));
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			/* ignore */
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
							errmsg("Unrecognized key %s", key)));
		}
	}

	/* Validate required fields */
	if (shardArgs->collectionName == NULL ||
		shardArgs->databaseName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_FAILEDTOPARSE),
						errmsg("unshardCollection is a required field.")));
	}

	shardArgs->shardingMode = ShardCollectionMode_Unshard;
}
