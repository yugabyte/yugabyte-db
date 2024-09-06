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

#include "io/helio_bson_core.h"
#include "api_hooks.h"
#include "commands/create_indexes.h"
#include "utils/mongo_errors.h"
#include "sharding/sharding.h"
#include "metadata/metadata_cache.h"
#include "utils/query_utils.h"

extern bool EnableNativeColocation;

PG_FUNCTION_INFO_V1(command_get_shard_key_value);
PG_FUNCTION_INFO_V1(command_validate_shard_key);
PG_FUNCTION_INFO_V1(command_shard_collection);

static bson_value_t FindShardKeyFieldValue(bson_iter_t *docIter, const char *path);
static void InitShardKeyFieldValues(pgbson *shardKey,
									ShardKeyFieldValues *shardKeyValues);
static int ShardKeyFieldIndex(ShardKeyFieldValues *shardKey, const char *path);
static bool ComputeShardKeyFieldValuesHash(ShardKeyFieldValues *shardKeyValues,
										   int64 *shardKeyHash);
static void ValidateShardKey(const pgbson *shardKeyDoc);

Datum
command_shard_collection(PG_FUNCTION_ARGS)
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
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Internal error sharding collection in metadata coordinator"),
							errdetail_log(
								"Internal error sharding collection in metadata coordinator via distributed call %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_VOID();
	}

	MongoCollection *collection = GetMongoCollectionByNameDatum(
		databaseDatum, collectionDatum, AccessShareLock);

	if (collection == NULL)
	{
		if (isReshard)
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_NAMESPACENOTSHARDED),
							errmsg("Collection %s.%s is not sharded",
								   databaseName, collectionName),
							errdetail_log(
								"Can not reshard collection that doesn't exist: %s.%s",
								databaseName, collectionName)));
		}

		CreateCollection(databaseDatum, collectionDatum);
		collection = GetMongoCollectionByNameDatum(
			databaseDatum, collectionDatum, AccessShareLock);

		Assert(collection != NULL);
	}

	if (collection->shardKey == NULL && isReshard)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_NAMESPACENOTSHARDED),
						errmsg("Collection %s.%s is not sharded",
							   databaseName, collectionName),
						errdetail_log(
							"Can not reshard collection that is not sharded: %s.%s",
							databaseName, collectionName)));
	}

	if (collection->shardKey != NULL && PgbsonEquals(collection->shardKey, shardKey))
	{
		ereport(NOTICE, (errmsg(
							 "Skipping Sharding for collection %s.%s as the same options were passed in.",
							 databaseName, collectionName)));
		PG_RETURN_VOID();
	}

	if (collection->shardKey != NULL && !isReshard)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_ALREADYINITIALIZED),
						errmsg(
							"Sharding already enabled for collection %s.%s with options { \"_id\": \"%s.%s\", \"dropped\" : false, \"key\" : %s, \"unique\": false }.",
							databaseName, collectionName, databaseName, collectionName,
							PgbsonToJsonForLogging(shardKey))));
	}

	int nargs = 3;
	Oid argTypes[3] = { BsonTypeId(), TEXTOID, TEXTOID };
	char *argNulls = NULL;
	bool isNull = true;

	Datum values[3] = { 0 };
	values[0] = PointerGetDatum(shardKey);
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
	appendStringInfo(queryInfo,
					 "ALTER TABLE %s ADD CONSTRAINT shard_key_value_check"
					 " CHECK (shard_key_value = %s.get_shard_key_value(%s::%s, %lu, document))",
					 tmpDataTableName, ApiInternalSchemaName,
					 quote_literal_cstr(PgbsonToHexadecimalString(shardKey)),
					 FullBsonTypeName,
					 collection->collectionId);
	ExtensionExecuteQueryViaSPI(queryInfo->data, readOnly, SPI_OK_UTILITY, &isNull);

	const char *colocateWith = EnableNativeColocation ? "none" : NULL;
	bool isUnsharded = false;

	DistributePostgresTable(tmpDataTableName, "shard_key_value", colocateWith,
							isUnsharded);

	/* apply the new shard key by re-inserting all data */
	resetStringInfo(queryInfo);
	appendStringInfo(queryInfo,
					 "INSERT INTO %s (shard_key_value, object_id, document, creation_time)"
					 " SELECT %s.get_shard_key_value($1, $2, document), object_id, document, creation_time"
					 " FROM %s",
					 tmpDataTableName, ApiInternalSchemaName, qualifiedDataTableName);

	nargs = 2;
	Oid insertArgTypes[2] = { BsonTypeId(), INT8OID };
	Datum insertArgValues[2] = { 0 };
	insertArgValues[0] = PointerGetDatum(shardKey);
	insertArgValues[1] = UInt64GetDatum(collection->collectionId);

	ExtensionExecuteQueryWithArgsViaSPI(queryInfo->data, nargs, insertArgTypes,
										insertArgValues, argNulls, readOnly,
										SPI_OK_INSERT, &isNull);

	/*
	 * Replace the old table with the new table.
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
							   collectionName);

		pgbson_element_writer elementWriter;
		PgbsonInitObjectElementWriter(&createIndexesArgWriter, &elementWriter,
									  "indexes", 7);
		PgbsonElementWriterWriteSQLValue(&elementWriter, isNullIndexSpecArray,
										 indexSpecArray, RECORDARRAYOID);

		/* Re-create valid indexes. */
		pgbson *createIndexesMsg = PgbsonWriterGetPgbson(&createIndexesArgWriter);
		CreateIndexesArg createIndexesArg = ParseCreateIndexesArg(databaseDatum,
																  createIndexesMsg);
		bool skipCheckCollectionCreate = createIndexesArg.blocking;
		bool uniqueIndexOnly = false;

		/* We call it good if it doesn't throw. */
		create_indexes_non_concurrently(databaseDatum, createIndexesArg,
										skipCheckCollectionCreate, uniqueIndexOnly);
	}

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
								errmsg("Shard key cannot contain an array.")));
			}
			else if (BSON_ITER_HOLDS_REGEX(docIter))
			{
				ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								errmsg("Shard key cannot contain a regex.")));
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
								"Shard key cannot contain array values or array descendants.")));
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
								errmsg("invalid value for shard key: %s",
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


/*
 * InitShardKeyFieldValues initializes a ShardKeyFieldValues based on the shard
 * key.
 */
void
InitShardKeyFieldValues(pgbson *shardKeyBson, ShardKeyFieldValues *shardKeyValues)
{
	int shardKeyCount = PgbsonCountKeys(shardKeyBson);

	/* prepare data structure for storing shard key field values */
	shardKeyValues->fieldCount = shardKeyCount;
	shardKeyValues->fields = palloc(shardKeyCount * sizeof(const char *));
	shardKeyValues->values = palloc0(shardKeyCount * sizeof(bson_value_t));
	shardKeyValues->isSet = palloc0(shardKeyCount * sizeof(bool));

	/* build array of shard key field names */
	bson_iter_t shardKeyIter;
	PgbsonInitIterator(shardKeyBson, &shardKeyIter);

	for (int fieldIndex = 0; bson_iter_next(&shardKeyIter); fieldIndex++)
	{
		shardKeyValues->fields[fieldIndex] = bson_iter_key(&shardKeyIter);
	}
}


/*
 * ShardKeyFieldIndex returns the index of path in shardKey.fields
 * or -1 if it is not found.
 */
int
ShardKeyFieldIndex(ShardKeyFieldValues *shardKey, const char *path)
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
 * values to shardKeyHash.
 * NOTE: This method should only be used in the processing of query filters.
 */
bool
ComputeShardKeyFieldValuesHash(ShardKeyFieldValues *shardKeyValues,
							   int64 *shardKeyHash)
{
	*shardKeyHash = 0;

	for (int fieldIndex = 0; fieldIndex < shardKeyValues->fieldCount; fieldIndex++)
	{
		if (!shardKeyValues->isSet[fieldIndex])
		{
			/* not all fields in the shard key were specified */
			return false;
		}

		/* add the value into the hash */
		bson_value_t *value = &(shardKeyValues->values[fieldIndex]);

		if (value->value_type == BSON_TYPE_REGEX)
		{
			/* Cannot compute hash if the value is given as regex */
			return false;
		}

		*shardKeyHash = BsonValueHash(value, *shardKeyHash);
	}

	return true;
}


/*
 * ComputeShardKeyHashForQuery is the same as ComputeShardKeyHashForQueryValue but
 * with a pgbson instead.
 */
bool
ComputeShardKeyHashForQuery(pgbson *shardKey, uint64_t collectionId, pgbson *query,
							int64 *shardKeyHash)
{
	bson_value_t queryValue = ConvertPgbsonToBsonValue(query);
	return ComputeShardKeyHashForQueryValue(shardKey, collectionId, &queryValue,
											shardKeyHash);
}


/*
 * ComputeShardKeyHashForQueryValue returns whether the given query filters all
 * shard key fields by a specific value and computes the hash of the values.
 */
bool
ComputeShardKeyHashForQueryValue(pgbson *shardKey, uint64_t collectionId,
								 const bson_value_t *query, int64 *shardKeyHash)
{
	if (shardKey == NULL)
	{
		*shardKeyHash = collectionId;
		return true;
	}

	ShardKeyFieldValues shardKeyValues;
	InitShardKeyFieldValues(shardKey, &shardKeyValues);

	bson_iter_t queryDocIter;
	BsonValueInitIterator(query, &queryDocIter);

	/* determine the shard key field values from the query BSON */
	FindShardKeyFieldValuesForQuery(&queryDocIter, &shardKeyValues);

	/* compute the hash, returns false if not all shard key fields are set */
	return ComputeShardKeyFieldValuesHash(&shardKeyValues, shardKeyHash);
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
void
FindShardKeyFieldValuesForQuery(bson_iter_t *queryDocument,
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
				ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
								errmsg("Could not iterate through query document "
									   "$and.")));
			}

			while (bson_iter_next(&andIterator))
			{
				bson_iter_t andElementIterator;
				if (!BSON_ITER_HOLDS_DOCUMENT(&andIterator) ||
					!bson_iter_recurse(&andIterator, &andElementIterator))
				{
					ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
									errmsg("Could not iterate through elements within "
										   "$and query.")));
				}

				FindShardKeyFieldValuesForQuery(&andElementIterator, shardKeyValues);
			}
		}
		else if (key[0] == '$')
		{
			/* ignore other operators */
		}
		else
		{
			/* key is a path rather than an operator */
			int fieldIndex = ShardKeyFieldIndex(shardKeyValues, key);
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
							shardKeyValues->isSet[fieldIndex] = true;
						}
					} while (bson_iter_next(&shardKeyFieldValueIter));

					continue;
				}

				/* if the key is not an operator, fall through */
			}

			/* query has the form <field>:<value> */
			shardKeyValues->values[fieldIndex] = *bson_iter_value(queryDocument);
			shardKeyValues->isSet[fieldIndex] = true;
		}
	}
}
