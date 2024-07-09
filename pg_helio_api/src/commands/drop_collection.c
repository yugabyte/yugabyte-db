/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/drop_collection.c
 *
 * Implementation of view and collection creation functions.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/resowner.h"
#include "utils/uuid.h"
#include "lib/stringinfo.h"
#include "access/xact.h"
#include "utils/syscache.h"
#include "nodes/makefuncs.h"
#include "catalog/namespace.h"

#include "utils/mongo_errors.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "metadata/index.h"
#include "utils/query_utils.h"
#include "utils/index_utils.h"
#include "utils/guc_utils.h"
#include "utils/version_utils.h"

#include "api_hooks.h"

static char * ConstructDropCommandCstr(char *databaseName, char *collectionName,
									   pgbson *writeConcern, char *uuid, bool
									   trackChanges);

PG_FUNCTION_INFO_V1(command_drop_collection);

/*
 * command_drop_collection implements the logic
 * of dropping a collection from a database with
 * an optional write concern
 */
Datum
command_drop_collection(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
	{
		PG_RETURN_BOOL(false);
	}

	Datum databaseNameDatum = PG_GETARG_DATUM(0);
	Datum collectionNameDatum = PG_GETARG_DATUM(1);

	MongoCollection *collection =
		GetMongoCollectionOrViewByNameDatum(databaseNameDatum,
											collectionNameDatum,
											NoLock);

	if (collection == NULL)
	{
		/* collection doesn't exist */
		PG_RETURN_BOOL(false);
	}

	char *databaseName = TextDatumGetCString(databaseNameDatum);
	char *collectionName = TextDatumGetCString(collectionNameDatum);
	if (strncmp(collectionName, "system.", 7) == 0)
	{
		/* system collection, cannot drop */
		PG_RETURN_BOOL(false);
	}

	bool trackChanges = PG_GETARG_BOOL(4);

	if (!IsMetadataCoordinator())
	{
		pgbson *writeConcern = PG_ARGISNULL(2) ? NULL : PG_GETARG_PGBSON(2);
		char *uuid = PG_ARGISNULL(3) ? NULL : DatumGetCString(
			DirectFunctionCall1(
				uuid_out, PG_GETARG_DATUM(3)));

		char *dropCommand = ConstructDropCommandCstr(databaseName, collectionName,
													 writeConcern, uuid, trackChanges);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(dropCommand);

		if (!result.success)
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg(
								"Internal error dropping collection in metadata coordinator"),
							errhint(
								"Internal error dropping collection in metadata coordinator %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_BOOL(strcasecmp(text_to_cstring(result.response), "t") == 0);
	}

	Datum collectionIdArgValue[1] = { UInt64GetDatum(collection->collectionId) };
	Oid collectionIdArgType[1] = { INT8OID };
	char collectionIdArgNull[1] = { ' ' };

	if (!PG_ARGISNULL(3))
	{
		Datum targetUuid = PG_GETARG_DATUM(3);

		StringInfo findUuidByRelIdQuery = makeStringInfo();
		appendStringInfo(findUuidByRelIdQuery,
						 "SELECT collection_uuid FROM %s.collections "
						 "WHERE collection_id = $1",
						 ApiCatalogSchemaName);

		bool readOnly = true;
		bool isNull = false;

		Datum collectionUuid = ExtensionExecuteQueryWithArgsViaSPI(
			findUuidByRelIdQuery->data,
			1, collectionIdArgType, collectionIdArgValue,
			collectionIdArgNull, readOnly,
			SPI_OK_SELECT,
			&isNull);

		if (isNull || memcmp(DatumGetPointer(collectionUuid), DatumGetPointer(targetUuid),
							 sizeof(pg_uuid_t)) != 0)
		{
			ereport(ERROR, (errcode(MongoCollectionUUIDMismatch),
							errmsg("drop collection %s.%s UUID mismatch", databaseName,
								   collectionName)));
		}
	}

	StringInfo deleteCommand = makeStringInfo();
	bool readOnly;
	bool isNull;

	appendStringInfo(deleteCommand,
					 "DROP TABLE IF EXISTS %s.documents_"
					 INT64_FORMAT,
					 ApiDataSchemaName,
					 collection->collectionId);
	readOnly = false;
	isNull = false;

	ExtensionExecuteQueryViaSPI(deleteCommand->data, readOnly, SPI_OK_UTILITY, &isNull);

	resetStringInfo(deleteCommand);
	appendStringInfo(deleteCommand,
					 "DROP TABLE IF EXISTS %s.retry_" INT64_FORMAT,
					 ApiDataSchemaName, collection->collectionId);
	readOnly = false;
	isNull = false;

	ExtensionExecuteQueryViaSPI(deleteCommand->data, readOnly, SPI_OK_UTILITY, &isNull);

	StringInfo deleteFromCollectionsCommand = makeStringInfo();
	appendStringInfo(deleteFromCollectionsCommand,
					 "DELETE FROM %s.collections WHERE collection_id = $1",
					 ApiCatalogSchemaName);
	isNull = false;

	RunQueryWithCommutativeWrites(deleteFromCollectionsCommand->data, 1,
								  collectionIdArgType, collectionIdArgValue,
								  collectionIdArgNull, SPI_OK_DELETE,
								  &isNull);

	DeleteAllCollectionIndexRecords(collection->collectionId);

	bool tableExists = false;
	if (IsClusterVersionAtleastThis(1, 12, 0))
	{
		tableExists = true;
	}

	if (tableExists)
	{
		StringInfo deleteFromIndexQueueCommand = makeStringInfo();
		appendStringInfo(deleteFromIndexQueueCommand,
						 "DELETE FROM %s WHERE collection_id = $1", GetIndexQueueName());
		isNull = false;

		RunQueryWithCommutativeWrites(deleteFromIndexQueueCommand->data, 1,
									  collectionIdArgType, collectionIdArgValue,
									  collectionIdArgNull, SPI_OK_DELETE,
									  &isNull);
	}

	DeleteAllCollectionIndexRecords(collection->collectionId);

	PG_RETURN_BOOL(true);
}


/*
 * Reconstructs the drop command from the parameter values
 */
static char *
ConstructDropCommandCstr(char *databaseName, char *collectionName, pgbson *writeConcern,
						 char *uuid, bool trackChanges)
{
	StringInfo dropCollectionQuery = makeStringInfo();
	appendStringInfo(dropCollectionQuery,
					 "SELECT %s.drop_collection(%s, %s",
					 ApiSchemaName,
					 quote_literal_cstr(databaseName),
					 quote_literal_cstr(collectionName));

	if (writeConcern != NULL)
	{
		appendStringInfo(dropCollectionQuery,
						 ", p_write_concern => %s::%s",
						 quote_literal_cstr(PgbsonToHexadecimalString(writeConcern)),
						 FullBsonTypeName
						 );
	}

	if (uuid != NULL)
	{
		appendStringInfo(dropCollectionQuery,
						 ", p_collection_uuid => %s",
						 quote_literal_cstr(uuid));
	}

	if (trackChanges == false)
	{
		appendStringInfo(dropCollectionQuery, ", p_track_changes => false");
	}

	appendStringInfoChar(dropCollectionQuery, ')');

	return dropCollectionQuery->data;
}
