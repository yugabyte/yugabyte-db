/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/rename_collection.c
 *
 * Implementation of the rename_collection UDF.
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
#include "utils/query_utils.h"

#include "api_hooks.h"

static void DropMongoCollection(char *, char *);
static void UpdateMongoCollectionName(char *, char *, char *);

PG_FUNCTION_INFO_V1(command_rename_collection);

/*
 * command_rename_collection implements the functionality
 * of the renameCollection database command.
 */
Datum
command_rename_collection(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("db name cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("collection name cannot be NULL")));
	}

	if (PG_ARGISNULL(2))
	{
		ereport(ERROR, (errmsg("collection target name cannot be NULL")));
	}

	Datum database_datum = PG_GETARG_DATUM(0);
	Datum collection_datum = PG_GETARG_DATUM(1);
	Datum new_collection_datum = PG_GETARG_DATUM(2);
	bool drop_target = PG_ARGISNULL(3) ? false : PG_GETARG_BOOL(3);

	/*
	 * Check if the collection to be updated exists. if not, throw an error.
	 */
	MongoCollection *collection =
		GetMongoCollectionByNameDatum(database_datum,
									  collection_datum,
									  NoLock);

	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_NAMESPACENOTFOUND),
						errmsg("collection %s.%s does not exist", TextDatumGetCString(
								   database_datum), TextDatumGetCString(
								   collection_datum))));
	}

	/*
	 * Checking whether the new collection name already exists in the database. If yes and drop_target is false,
	 * then throw an error. Drop it otherwise.
	 */
	MongoCollection *target_collection =
		GetMongoCollectionOrViewByNameDatum(database_datum,
											new_collection_datum,
											NoLock);

	if (target_collection != NULL)
	{
		if (drop_target)
		{
			DropMongoCollection(TextDatumGetCString(database_datum), TextDatumGetCString(
									new_collection_datum));
		}
		else
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_NAMESPACEEXISTS),
							errmsg("collection %s.%s already exists",
								   TextDatumGetCString(database_datum),
								   TextDatumGetCString(
									   new_collection_datum))));
		}
	}

	/*
	 * Update the collection name.
	 */
	UpdateMongoCollectionName(
		TextDatumGetCString(database_datum),
		TextDatumGetCString(collection_datum),
		TextDatumGetCString(new_collection_datum));

	PG_RETURN_VOID();
}


/*
 * Drops a collection from a database.
 */
static void
DropMongoCollection(char *database_name, char *target_collection_name)
{
	Datum argValues[2] = {
		CStringGetTextDatum(database_name), CStringGetTextDatum(target_collection_name)
	};
	Oid argTypes[2] = { TEXTOID, TEXTOID };
	char *argNulls = NULL;
	StringInfo cmdStr = makeStringInfo();

	appendStringInfo(cmdStr,
					 "SELECT %s.drop_collection($1, $2);",
					 ApiSchemaName);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr->data,
										2,
										argTypes, argValues, argNulls,
										readOnly, SPI_OK_SELECT,
										&isNull);
}


/*
 * Updates the name of a collection in a database.
 */
static void
UpdateMongoCollectionName(char *database_name, char *collection_name, char *new_name)
{
	Datum argValues[3] = {
		CStringGetTextDatum(new_name), CStringGetTextDatum(database_name),
		CStringGetTextDatum(collection_name)
	};
	Oid argTypes[3] = { TEXTOID, TEXTOID, TEXTOID };
	char *argNulls = NULL;

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "UPDATE %s.collections SET collection_name = $1 WHERE database_name = $2 AND collection_name = $3",
					 ApiCatalogSchemaName);

	bool isNull = false;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr->data,
										3,
										argTypes, argValues, argNulls,
										readOnly, SPI_OK_UPDATE,
										&isNull);
}
