/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/distributed_index_operations.c
 *
 * Implementation of index operations for a distributed execution.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>

#include "io/bson_core.h"
#include "metadata/collection.h"
#include "utils/documentdb_errors.h"
#include "node_distributed_operations.h"
#include "distributed_index_operations.h"
#include "commands/coll_mod.h"
#include "parser/parse_func.h"
#include "metadata/metadata_cache.h"

static Oid UpdatePostgresIndexWorkerFunctionOid(void);

extern char *ApiDataSchemaName;
extern char *ApiDistributedSchemaNameV2;

PG_FUNCTION_INFO_V1(documentdb_update_postgres_index_worker);


Datum
documentdb_update_postgres_index_worker(PG_FUNCTION_ARGS)
{
	pgbson *argBson = PG_GETARG_PGBSON(0);

	IndexMetadataUpdateOperation operation = INDEX_METADATA_UPDATE_OPERATION_UNKNOWN;
	uint64_t collectionId = 0;
	int indexId = 0;
	bool value = false;
	bool hasValue = false;
	bson_iter_t argIter;
	PgbsonInitIterator(argBson, &argIter);
	while (bson_iter_next(&argIter))
	{
		const char *key = bson_iter_key(&argIter);
		if (strcmp(key, "collectionId") == 0)
		{
			collectionId = bson_iter_as_int64(&argIter);
		}
		else if (strcmp(key, "indexId") == 0)
		{
			indexId = bson_iter_int32(&argIter);
		}
		else if (strcmp(key, "operation") == 0)
		{
			operation = (IndexMetadataUpdateOperation) bson_iter_int32(&argIter);
		}
		else if (strcmp(key, "value") == 0)
		{
			value = bson_iter_as_bool(&argIter);
			hasValue = true;
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Unexpected argument to update_postgres_index_worker: %s",
								key)));
		}
	}

	if (collectionId == 0 || indexId == 0 || !hasValue ||
		operation == INDEX_METADATA_UPDATE_OPERATION_UNKNOWN)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Missing argument to update_postgres_index_worker")));
	}

	bool ignoreMissingShards = true;
	UpdatePostgresIndexCore(collectionId, indexId, operation, value, ignoreMissingShards);

	PG_RETURN_POINTER(PgbsonInitEmpty());
}


void
UpdateDistributedPostgresIndex(uint64_t collectionId, int indexId, int operation,
							   bool value)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "collectionId", 12, collectionId);
	PgbsonWriterAppendInt32(&writer, "indexId", 7, indexId);
	PgbsonWriterAppendInt32(&writer, "operation", 9, operation);
	PgbsonWriterAppendBool(&writer, "value", 5, value);

	MongoCollection *collection = GetMongoCollectionByColId(collectionId, NoLock);
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INVALIDNAMESPACE),
						errmsg("Failed to find collection for index update")));
	}

	char fullyQualifiedTableName[NAMEDATALEN * 2 + 2] = { 0 };
	pg_sprintf(fullyQualifiedTableName, "%s.%s", ApiDataSchemaName,
			   collection->tableName);

	bool backfillCoordinator = true;
	ExecutePerNodeCommand(UpdatePostgresIndexWorkerFunctionOid(), PgbsonWriterGetPgbson(
							  &writer),
						  false, fullyQualifiedTableName, backfillCoordinator);
}


/*
 * Returns the OID of the update_postgres_index_worker function.
 * it isn't really worth caching this since it's only used in the diagnostic path.
 * If that changes, this can be put into an OID cache of sorts.
 */
static Oid
UpdatePostgresIndexWorkerFunctionOid(void)
{
	List *functionNameList = list_make2(makeString(ApiDistributedSchemaNameV2),
										makeString("update_postgres_index_worker"));
	Oid paramOids[1] = { DocumentDBCoreBsonTypeId() };
	bool missingOK = false;

	return LookupFuncName(functionNameList, 1, paramOids, missingOK);
}
