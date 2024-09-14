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

#include "utils/helio_errors.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "utils/error_utils.h"
#include "utils/query_utils.h"

#include "api_hooks.h"

extern bool EnableNativeColocation;
extern bool EnableNativeTableColocation;

static bool CanColocateAtDatabaseLevel(text *databaseDatum);
static const char * CreatePostgresDataTable(uint64_t collectionId,
											const char *colocateWith,
											const char *shardingColumn);
static uint64_t InsertIntoCollectionTable(text *databaseDatum, text *collectionDatum);

static uint64_t InsertMetadataIntoCollections(text *databaseDatum, text *collectionDatum,
											  bool *collectionExists);

static const char * GetOrCreateDatabaseConfigCollection(text *databaseDatum);

PG_FUNCTION_INFO_V1(command_create_collection_core);


/*
 * command_create_collection_core implements the
 * core logic to create a data collection holding
 * bson documents.
 */
Datum
command_create_collection_core(PG_FUNCTION_ARGS)
{
	text *databaseDatum = PG_GETARG_TEXT_PP(0);
	text *collectionDatum = PG_GETARG_TEXT_PP(1);
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
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Internal error creating collection in metadata coordinator %s",
								text_to_cstring(result.response)),
							errdetail_log(
								"Internal error creating collection in metadata coordinator %s",
								text_to_cstring(result.response))));
		}

		PG_RETURN_BOOL(strcasecmp(text_to_cstring(result.response), "t") == 0);
	}

	MongoCollection *collection = GetMongoCollectionByNameDatum(
		PointerGetDatum(databaseDatum), PointerGetDatum(collectionDatum),
		AccessShareLock);

	if (collection != NULL)
	{
		/* Collection already exists */
		PG_RETURN_BOOL(false);
	}

	const char *colocateWith = NULL;
	const char *shardingColumn = "shard_key_value";
	if (EnableNativeColocation)
	{
		/* For backwards compatibility, check if we can colocate with
		 * the database level colocation
		 */
		if (CanColocateAtDatabaseLevel(databaseDatum))
		{
			/* Not a legacy table, set the sharding column to null to create
			 * an unsharded collection.
			 */
			shardingColumn = NULL;

			/* To ensure colocation across tables, we get or create
			 * a special config table per database so that all tables
			 * can be colocated with this table.
			 */
			colocateWith = GetOrCreateDatabaseConfigCollection(databaseDatum);

			/* If table level colocation is desired - ignore the colocate with
			 * on the sentinel.
			 */
			if (EnableNativeTableColocation)
			{
				colocateWith = "none";
			}
		}
	}

	bool collectionExists = false;
	uint64_t collectionId = InsertMetadataIntoCollections(databaseDatum, collectionDatum,
														  &collectionExists);

	if (collectionExists)
	{
		PG_RETURN_BOOL(false);
	}

	ereport(NOTICE, (errmsg("creating collection")));
	CreatePostgresDataTable(collectionId, colocateWith, shardingColumn);
	PG_RETURN_BOOL(true);
}


static uint64_t
InsertMetadataIntoCollections(text *databaseDatum, text *collectionDatum,
							  bool *collectionExists)
{
	/* Insert row into the collections table */
	MemoryContext savedMemoryContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;
	BeginInternalSubTransaction(NULL);

	volatile uint64_t collectionId = 0;
	*collectionExists = false;
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
			*collectionExists = true;
		}
		else
		{
			ReThrowError(errorData);
		}
	}
	PG_END_TRY();

	return collectionId;
}


/*
 * Creates the core table schema for managing bson documents.
 * This is also responsible for creating any necessary indexes
 * and registering RBAC rules around the table.
 * Returns the name of the Postgres table
 */
static const char *
CreatePostgresDataTable(uint64_t collectionId, const char *colocateWith, const
						char *shardingColumn)
{
	StringInfo dataTableNameInfo = makeStringInfo();
	appendStringInfo(dataTableNameInfo, "%s.documents_%lu",
					 ApiDataSchemaName, collectionId);

	StringInfo retryTableNameInfo = makeStringInfo();
	appendStringInfo(retryTableNameInfo, "%s.retry_%lu", ApiDataSchemaName, collectionId);

	StringInfo createTableStringInfo = makeStringInfo();

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

	bool readOnly = false;
	bool isNull = false;
	bool isUnsharded = true;

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

	const char *distributionColumnUsed = DistributePostgresTable(dataTableNameInfo->data,
																 shardingColumn,
																 colocateWith,
																 isUnsharded);

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
	DistributePostgresTable(retryTableNameInfo->data, distributionColumnUsed,
							colocateWith,
							isUnsharded);
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
		ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
						errmsg(
							"CollectionId was null on inserted row. This is an unexpected bug"),
						errdetail_log(
							"CollectionId was null on inserted row. This is an unexpected bug")));
	}

	return DatumGetUInt64(resultDatum);
}


/*
 * Conditions for colocation:
 * 1) db.system.dbSentinel exists: We can safely colocate at the database level.
 * 2) There are no collections for that database.
 */
static bool
CanColocateAtDatabaseLevel(text *databaseDatum)
{
	const char *systemCollectionTable = "system.dbSentinel";
	text *systemCollectionsText = cstring_to_text(systemCollectionTable);
	MongoCollection *collection = GetMongoCollectionByNameDatum(
		PointerGetDatum(databaseDatum), PointerGetDatum(systemCollectionsText),
		AccessShareLock);

	if (collection != NULL)
	{
		/* db.system.dbSentinel already exists - we're good */
		ereport(LOG, (errmsg("Sentinel table exists for %s - can colocate",
							 collection->name.databaseName)));
		return true;
	}

	/*
	 * db.system.dbSentinel does not exist - we can
	 * safely colocate if the database is empty.
	 */
	Datum args[1] = { PointerGetDatum(databaseDatum) };
	Oid argTypes[1] = { TEXTOID };
	const char *checkDatabaseQuery = psprintf(
		"SELECT 1 FROM %s.collections WHERE database_name = $1 LIMIT 1",
		ApiCatalogSchemaName);
	bool isNull;
	ExtensionExecuteQueryWithArgsViaSPI(checkDatabaseQuery, 1, argTypes, args, NULL,
										false, SPI_OK_SELECT, &isNull);
	ereport(LOG, (errmsg("database %s has collections: %s", text_to_cstring(
							 databaseDatum),
						 isNull ? "false" : "true")));

	/* If the query returned no results then we can safely do database colocation */
	return isNull;
}


/*
 * Creates a database wide sentinel collection that is used for colocating other collections against.
 * This is a single collection per database.
 *
 * This is primarily needed if N writers are concurrently trying to create collections.
 * With native colocation they'll each get a separate colocation Id due to concurrent transactions
 * and produce N independently colocated tables.
 *
 * By forcing a unique collection per database, all N writers will queue waiting to create this one table
 * and then resume creating tables colocated with this.
 *
 * Across databases, concurrent creates will still work since they will lock on their own tables.
 */
static const char *
GetOrCreateDatabaseConfigCollection(text *databaseDatum)
{
	const char *systemCollectionTable = "system.dbSentinel";
	text *systemCollectionsText = cstring_to_text(systemCollectionTable);
	bool dbCollectionExists = false;
	MongoCollection *collection = GetMongoCollectionByNameDatum(
		PointerGetDatum(databaseDatum), PointerGetDatum(systemCollectionsText),
		AccessShareLock);

	if (collection != NULL)
	{
		/* This already exists - we're good */
		ereport(LOG, (errmsg("Returning existing %s for the sentinel table for %s.%s",
							 collection->tableName, collection->name.databaseName,
							 collection->name.collectionName)));
		return psprintf("%s.%s", ApiDataSchemaName, collection->tableName);
	}

	uint64_t databaseCollectionId =
		InsertMetadataIntoCollections(databaseDatum, systemCollectionsText,
									  &dbCollectionExists);

	if (dbCollectionExists)
	{
		collection = GetMongoCollectionByNameDatum(
			PointerGetDatum(databaseDatum), PointerGetDatum(systemCollectionsText),
			AccessShareLock);
		if (collection == NULL)
		{
			/* Weird case, insert failed, but cannot read it? */
			ereport(ERROR, (errcode(ERRCODE_HELIO_INTERNALERROR),
							errmsg(
								"Unable to create metadata database sentinel collection.")));
		}

		ereport(LOG, (errmsg("Returning %s for the sentinel table for %s.%s",
							 collection->tableName, collection->name.databaseName,
							 collection->name.collectionName)));
		return psprintf("%s.%s", ApiDataSchemaName, collection->tableName);
	}

	const char *colocateWith = "none";
	const char *shardingColumn = NULL;
	const char *tableName = CreatePostgresDataTable(databaseCollectionId, colocateWith,
													shardingColumn);
	ereport(LOG, (errmsg("Creating and returning %s for the sentinel database %s",
						 tableName, text_to_cstring(databaseDatum))));

	/* Add a policy to disallow writes on this table (TODO: Investigate why RLS didn't work here) */
	StringInfo createCheckStringInfo = makeStringInfo();
	appendStringInfo(createCheckStringInfo,
					 "ALTER TABLE %s ADD CONSTRAINT disallow_writes_check CHECK (false)",
					 tableName);
	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryViaSPI(createCheckStringInfo->data, readOnly, SPI_OK_UTILITY,
								&isNull);

	return tableName;
}
