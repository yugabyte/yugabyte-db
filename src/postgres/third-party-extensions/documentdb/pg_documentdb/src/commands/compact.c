/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/compact.c
 *
 * Implementation of the blocking compact command.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <commands/vacuum.h>
#include <nodes/parsenodes.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <storage/lmgr.h>
#include <utils/syscache.h>

#include "api_hooks.h"
#include "commands/commands_common.h"
#include "commands/parse_error.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"
#include "utils/feature_counter.h"
#include "utils/query_utils.h"
#include "utils/storage_utils.h"
#include "utils/version_utils.h"

typedef struct CompactArgs
{
	/* The name of the database */
	char *databaseName;

	/* The name of the collection */
	char *collectionName;

	/*
	 * Estimate the amount of space that would be freed by a compact operation
	 * without actually performing it.
	 */
	bool dryRun;

	/*
	 * Only run the compact operation if the amount of space freed is greater
	 * than this value (in MB). The default is no value which means always run compact
	 */
	double freeSpaceTargetMB;

	/*
	 * This is not used today, with this false compact should be run on secondary nodes.
	 * TODO: support force: false once we have writable secondaries. Today only force: true
	 * is supported with blocking primary
	 */
	bool force;
} CompactArgs;

static void ParseCompactCommandSpec(pgbson *compactSpec, CompactArgs *args);
static void PerformVacuum(MongoCollection *collection);
static void ValidateLocksAndCheckAccess(MongoCollection *collection);


PG_FUNCTION_INFO_V1(command_compact);

/*
 * command_compact implements the functionality of compact Database command
 * dbcommand/compact.
 */
Datum
command_compact(PG_FUNCTION_ARGS)
{
	pgbson *compactSpec = PG_GETARG_PGBSON(0);
	if (!IsMetadataCoordinator())
	{
		StringInfo compactQueryCoordinator = makeStringInfo();
		appendStringInfo(compactQueryCoordinator,
						 "SELECT %s.compact(%s::%s.bson)",
						 ApiSchemaNameV2,
						 quote_literal_cstr(
							 PgbsonToHexadecimalString(PG_GETARG_PGBSON(0))),
						 CoreSchemaNameV2);
		DistributedRunCommandResult result = RunCommandOnMetadataCoordinator(
			compactQueryCoordinator->data);
		if (!result.success)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"Internal error while running compact in metadata coordinator %s",
								text_to_cstring(result.response)),
							errdetail_log(
								"Internal error while running compact in metadata coordinator %s",
								text_to_cstring(result.response))));
		}
		pgbson *response = PgbsonInitFromHexadecimalString(text_to_cstring(
															   result.response));
		PG_RETURN_POINTER(response);
	}

	ReportFeatureUsage(FEATURE_COMMAND_COMPACT);

	CompactArgs args;
	memset(&args, 0, sizeof(CompactArgs));
	ParseCompactCommandSpec(compactSpec, &args);

	if (args.databaseName == NULL || args.collectionName == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_BADVALUE),
						errmsg(
							"Invalid command compact specification, missing database or collection name")));
	}

	/*
	 * VACUUM FULL is a blocking operation and it takes AccessExclusiveLock on the table.
	 * Also we can only execute it on the top level (not within a function and procedure tranasction), so we can't
	 * take any lock on the collection here to avoid deadlock situation.
	 */
	MongoCollection *collection = GetMongoCollectionByNameDatum(CStringGetTextDatum(
																	args.databaseName),
																CStringGetTextDatum(
																	args.collectionName),
																NoLock);

	if (collection == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_NAMESPACENOTFOUND),
						errmsg("ns does not exist: %s.%s", args.databaseName,
							   args.collectionName)));
	}

	ValidateLocksAndCheckAccess(collection);

	/* Start building the response */
	pgbson_writer response;
	PgbsonWriterInit(&response);
	PgbsonWriterAppendDouble(&response, "ok", 2, 1);

	/* Get the bloat stats before vacuuming */
	CollectionBloatStats beforeVacuumStats;
	memset(&beforeVacuumStats, 0, sizeof(CollectionBloatStats));

	/* Get the bloat stats before vacuuming */
	beforeVacuumStats = GetCollectionBloatEstimate(collection->collectionId);
	if (args.dryRun)
	{
		PgbsonWriterAppendInt64(&response, "estimatedBytesFreed", 19,
								beforeVacuumStats.estimatedBloatStorage);
		PG_RETURN_POINTER(PgbsonWriterGetPgbson(&response));
	}

	if (!beforeVacuumStats.nullStats && (beforeVacuumStats.estimatedBloatStorage /
										 BYTES_PER_MB) >= args.freeSpaceTargetMB)
	{
		/* Only perform full vacuum if there are stats available and freeSpace target is met */
		elog(LOG, "Performing compact vacuum full on collection %s.%s",
			 args.databaseName, args.collectionName);
		PerformVacuum(collection);
	}

	/* This is very rough, currently it doesn't considers the space freed by vacuuming index
	 * TODO: Optimize
	 */
	uint64 freedSpace = beforeVacuumStats.nullStats ? 0 :
						beforeVacuumStats.estimatedBloatStorage;
	PgbsonWriterAppendInt64(&response, "bytesFreed", 10, freedSpace);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&response));
}


/*-------------------*/
/* Private functions */
/*-------------------*/

/*
 * Performs the necessary checks to ensure that the current user has priveleges to
 * perform the compact operation on the collection, also checks if the realtion to be vacuumed
 * is available for exclusive access locking
 */
static void
ValidateLocksAndCheckAccess(MongoCollection *collection)
{
	/*
	 * Check if the current user is permitted to perform VACUUM FULL on the collection.
	 */
	HeapTuple tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(collection->relationId));
	if (!HeapTupleIsValid(tuple))
	{
		ereport(ERROR, (
					errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
					errmsg(
						"Cannot find relation in cache while performing compact on collection %s.%s",
						collection->name.databaseName,
						collection->name.collectionName),
					errdetail_log(
						"Cannot find relation in cache while performing compact on collection %s.%s",
						collection->name.databaseName, collection->name.collectionName)));
	}
	Form_pg_class classForm = (Form_pg_class) GETSTRUCT(tuple);

	bits32 options = VACOPT_VACUUM | VACOPT_FULL;
	bool userCanVacuum = false;
#if PG_VERSION_NUM >= 170000
	userCanVacuum = vacuum_is_permitted_for_relation(collection->relationId,
													 classForm, options);
#else
	userCanVacuum = vacuum_is_relation_owner(collection->relationId,
											 classForm, options);
#endif
	ReleaseSysCache(tuple);

	if (!userCanVacuum)
	{
		ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
						errmsg(
							"permisson denied for performing compact on collection %s.%s",
							collection->name.databaseName,
							collection->name.collectionName),
						errdetail_log(
							"permisson denied for performing compact on collection %s.%s",
							collection->name.databaseName,
							collection->name.collectionName)));
	}

	/* Now checking if the collection is available for exclusive locking :
	 * - To validate early if only 1 vacuum is running on the collection.
	 *
	 * Immediately unlock the table to avoid deadlock situation with VACUUM FULL.
	 */
	if (ConditionalLockRelationOid(collection->relationId, AccessExclusiveLock))
	{
		UnlockRelationOid(collection->relationId, AccessExclusiveLock);
	}
	else
	{
		/* Throw conflicting operations in progress */
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_LOCATION17308),
						errmsg(
							"compact is not allowed on collection %s.%s because another operation is in progress",
							collection->name.databaseName,
							collection->name.collectionName),
						errdetail_log(
							"compact is not allowed on collection %s.%s because another operation is in progress",
							collection->name.databaseName,
							collection->name.collectionName)));
	}
}


/*
 * This sends a VACUUM FULL command to the local server via libpq, as VACUUM FULL can't
 * be executed in a transaction block.
 */
static void
PerformVacuum(MongoCollection *collection)
{
	Assert(collection != NULL && collection->relationId != InvalidOid);

	const char *vacuumFullQuery = FormatSqlQuery("VACUUM FULL %s.documents_%ld",
												 ApiDataSchemaName,
												 collection->collectionId);

	/* VACUUM needs to be performed at the top level */
	bool useSerialExecution = false;
	Oid userOid = GetUserId();
	ExtensionExecuteQueryAsUserOnLocalhostViaLibPQ((char *) vacuumFullQuery, userOid,
												   useSerialExecution);
}


static void
ParseCompactCommandSpec(pgbson *compactSpec, CompactArgs *args)
{
	if (compactSpec == NULL)
	{
		return;
	}

	bson_iter_t specIter;
	PgbsonInitIterator(compactSpec, &specIter);
	while (bson_iter_next(&specIter))
	{
		pgbsonelement element;
		BsonIterToPgbsonElement(&specIter, &element);

		if (strcmp(element.path, "compact") == 0)
		{
			EnsureTopLevelFieldType("compact", &specIter, BSON_TYPE_UTF8);
			args->collectionName = pstrdup(element.bsonValue.value.v_utf8.str);
		}
		else if (strcmp(element.path, "$db") == 0)
		{
			EnsureTopLevelFieldType("$db", &specIter, BSON_TYPE_UTF8);
			args->databaseName = pstrdup(element.bsonValue.value.v_utf8.str);
		}
		else if (strcmp(element.path, "force") == 0)
		{
			EnsureTopLevelFieldType("force", &specIter, BSON_TYPE_BOOL);
			args->force = element.bsonValue.value.v_bool;
			if (!args->force)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
								errmsg(
									"command compact option force:false is not supported")));
			}
		}
		else if (strcmp(element.path, "dryRun") == 0)
		{
			EnsureTopLevelFieldType("dryRun", &specIter, BSON_TYPE_BOOL);
			args->dryRun = element.bsonValue.value.v_bool;
		}
		else if (strcmp(element.path, "freeSpaceTargetMB") == 0)
		{
			EnsureTopLevelFieldIsNumberLike("freeSpaceTargetMB", &element.bsonValue);
			args->freeSpaceTargetMB = BsonValueAsDouble(&element.bsonValue);
		}
		else if (!IsCommonSpecIgnoredField(element.path))
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_UNKNOWNBSONFIELD),
							errmsg(
								"The BSON field compact.%s is not recognized as a known field",
								element.path),
							errdetail_log(
								"The BSON field compact.%s is not recognized as a known field",
								element.path)));
		}
	}
}
