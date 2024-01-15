/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/create_collection_view.c
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
#include "lib/stringinfo.h"
#include "access/xact.h"

#include "utils/mongo_errors.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "utils/query_utils.h"

#include "api_hooks.h"

static const char * CreatePostgresDataTable(uint64_t collectionId);
static uint64_t InsertIntoCollectionTable(text *databaseDatum, text *collectionDatum);


PG_FUNCTION_INFO_V1(command_create_collection_core);


/*
 * command_create_collection_core implements the
 * core logic to create a data collection holding
 * bson documents.
 */
Datum
command_create_collection_core(PG_FUNCTION_ARGS)
{
	text *databaseDatum = PG_GETARG_TEXT_P(0);
	text *collectionDatum = PG_GETARG_TEXT_P(1);
	if (!IsMetadataCoordinator())
	{
		StringInfo createCollectionQuery = makeStringInfo();
		appendStringInfo(createCollectionQuery,
						 "SELECT %s.create_collection(%s,%s)",
						 ApiSchemaName,
						 quote_literal_cstr(text_to_cstring(databaseDatum)),
						 quote_literal_cstr(text_to_cstring(collectionDatum)));
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			createCollectionQuery->data);

		if (!result.success)
		{
			ereport(ERROR, (errcode(MongoInternalError),
							errmsg(
								"Internal error creating collection in metadata coordinator %s",
								text_to_cstring(result.response)),
							errhint(
								"Internal error creating collection in metadata coordinator %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_BOOL(strcasecmp(text_to_cstring(result.response), "true") == 0);
	}

	MongoCollection *collection = GetMongoCollectionByNameDatum(
		PointerGetDatum(databaseDatum), PointerGetDatum(collectionDatum),
		AccessShareLock);

	if (collection != NULL)
	{
		/* Collection already exists */
		PG_RETURN_BOOL(false);
	}

	/* Insert row into the collections table */
	MemoryContext savedMemoryContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;
	BeginInternalSubTransaction(NULL);

	uint64_t collectionId = 0;
	bool collectionExists = false;
	PG_TRY();
	{
		collectionId = InsertIntoCollectionTable(databaseDatum, collectionDatum);
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(savedMemoryContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(savedMemoryContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort the inner transaction */
		RollbackAndReleaseCurrentSubTransaction();

		/* Rollback changes MemoryContext */
		MemoryContextSwitchTo(savedMemoryContext);
		CurrentResourceOwner = oldOwner;

		if (errorData->sqlerrcode == ERRCODE_UNIQUE_VIOLATION)
		{
			ereport(LOG, (errmsg(
							  "Skipping creating collection because there was a unique key violation.")));
			collectionExists = true;
		}
		else
		{
			ReThrowError(errorData);
		}
	}
	PG_END_TRY();

	if (collectionExists)
	{
		PG_RETURN_BOOL(false);
	}

	const char *tableName = CreatePostgresDataTable(collectionId);

	PostProcessCreateTable(tableName, collectionId, databaseDatum, collectionDatum);
	PG_RETURN_BOOL(true);
}


/*
 * Creates the core table schema for managing bson documents.
 * This is also responsible for creating any necessary indexes
 * and registering RBAC rules around the table.
 * Returns the name of the Postgres table
 */
static const char *
CreatePostgresDataTable(uint64_t collectionId)
{
	StringInfo dataTableNameInfo = makeStringInfo();
	appendStringInfo(dataTableNameInfo, "%s.documents_%lu",
					 ApiDataSchemaName, collectionId);

	StringInfo retryTableNameInfo = makeStringInfo();
	appendStringInfo(retryTableNameInfo, "%s.retry_%lu", ApiDataSchemaName, collectionId);

	StringInfo createTableStringInfo = makeStringInfo();
	ereport(NOTICE, (errmsg("creating collection")));

	/* Create the actual table */
	appendStringInfo(createTableStringInfo,
					 "CREATE TABLE %s ("

	                 /* derived shard key field generated from the real shard key */
					 "shard_key_value bigint not null,"

	                 /* unique ID of the object */
					 "object_id %s.bson not null,"

	                 /*
	                  * the document
	                  *
	                  * NB: Ensure to match MONGO_DATA_TABLE_DOCUMENT_VAR_ contants
	                  *     defined in collection.h if you decide changing definiton
	                  *     or position of document column.
	                  */
					 "document %s.bson not null,"

	                 /* creation time (for TTL) */
					 "creation_time timestamptz not null default now()"
					 ")", dataTableNameInfo->data,
					 CoreSchemaName, CoreSchemaName);

	/* Allow modifying the core schema */
	ModifyCreateTableSchema(createTableStringInfo, dataTableNameInfo->data);

	bool readOnly = false;
	bool isNull = false;

	/* Assuming it didn't throw, it's a success */
	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* Change the owner to the extension admin */
	resetStringInfo(createTableStringInfo);
	appendStringInfo(createTableStringInfo,
					 "ALTER TABLE %s OWNER TO %s",
					 dataTableNameInfo->data, ApiAdminRole);

	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* Create the _id_ index (corresponds to the primary key) */
	resetStringInfo(createTableStringInfo);
	appendStringInfo(createTableStringInfo,
					 "SELECT %s.create_builtin_id_index(%lu)",
					 ApiInternalSchemaName, collectionId);
	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_SELECT,
								&isNull);

	const char *colocateWith = NULL;
	DistributePostgresTable(dataTableNameInfo->data, "shard_key_value", colocateWith);

	resetStringInfo(createTableStringInfo);
	appendStringInfo(createTableStringInfo,
					 "ALTER TABLE %s ADD CONSTRAINT shard_key_value_check CHECK (shard_key_value = '%lu'::bigint)",
					 dataTableNameInfo->data, collectionId);
	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* Create the retry record table */
	/* table that records committed writes and their logical transaction IDs */
	resetStringInfo(createTableStringInfo);
	appendStringInfo(createTableStringInfo,
					 "CREATE TABLE %s ("

	                 /* derived shard key field generated from the real shard key */
					 "shard_key_value bigint not null,"

	                 /* logical transaction ID chosen by the client */
					 "transaction_id text not null,"

	                 /* object ID of the document that was written (or NULL for noop) */
					 "object_id %s.bson,"

	                 /* whether rows were affected by the write */
					 "rows_affected bool not null,"

	                 /* time at which the record was written (for TTL) */
					 "write_time timestamptz default now(),"

	                 /*
	                  * (Maybe projected) old or new document that next trial should
	                  * report (or NULL if not applicable to the command or the command
	                  * couldn't match any documents).
	                  */
					 "result_document %s.bson null,"
					 "PRIMARY KEY (shard_key_value, transaction_id)"
					 ")", retryTableNameInfo->data, CoreSchemaName, CoreSchemaName);
	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	/* Change the owner to the extension admin */
	resetStringInfo(createTableStringInfo);
	appendStringInfo(createTableStringInfo,
					 "ALTER TABLE %s OWNER TO %s",
					 retryTableNameInfo->data, ApiAdminRole);
	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	resetStringInfo(createTableStringInfo);
	appendStringInfo(createTableStringInfo,
					 "CREATE INDEX ON %s (object_id)", retryTableNameInfo->data);
	ExtensionExecuteQueryViaSPI(createTableStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	colocateWith = dataTableNameInfo->data;
	DistributePostgresTable(retryTableNameInfo->data, "shard_key_value", colocateWith);
	return dataTableNameInfo->data;
}


/*
 * Registers the table into the schema catalog so that queries
 * against the table's friendly name know to track the actual underlying postgres
 * table. As part of registering the table into the catalog, the insert returns
 * a unique identifier which is then used to create the underlying postgres table.
 */
static uint64_t
InsertIntoCollectionTable(text *databaseDatum, text *collectionDatum)
{
	const char *query = FormatSqlQuery("INSERT INTO %s.collections ("
									   " database_name, collection_name, collection_uuid )"
									   " VALUES ($1, $2, gen_random_uuid()) RETURNING collection_id",
									   ApiCatalogSchemaName);

	int nargs = 2;
	Oid argTypes[2] = { TEXTOID, TEXTOID };
	Datum argValues[2] = {
		PointerGetDatum(databaseDatum), PointerGetDatum(collectionDatum)
	};
	char argNulls[2] = { ' ', ' ' };
	bool readOnly = false;
	bool isNull = false;
	Datum resultDatum = ExtensionExecuteQueryWithArgsViaSPI(
		query, nargs, argTypes, argValues, argNulls,
		readOnly, SPI_OK_INSERT_RETURNING, &isNull);

	if (isNull)
	{
		ereport(ERROR, (errcode(MongoInternalError),
						errmsg(
							"CollectionId was null on inserted row. This is an unexpected bug"),
						errhint(
							"CollectionId was null on inserted row. This is an unexpected bug")));
	}

	return DatumGetUInt64(resultDatum);
}
