
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/current_op.c
 *
 * Implementation of the current_op command.
 * https://www.mongodb.com/docs/manual/reference/command/currentOp/
 *-------------------------------------------------------------------------
 */
#include <math.h>
#include <postgres.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <funcapi.h>
#include <utils/builtins.h>
#include <catalog/namespace.h>

#include "io/bson_core.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "utils/mongo_errors.h"
#include "utils/feature_counter.h"
#include "utils/hashset_utils.h"
#include "utils/version_utils.h"
#include "utils/query_utils.h"
#include "commands/parse_error.h"
#include "commands/diagnostic_commands_common.h"
#include "io/bson_set_returning_functions.h"
#include "metadata/collection.h"


/*
 * Options to the CurrentOp command. Mirrors the superset
 * of the aggregation stage and command.
 */
typedef struct
{
	/* Show ops from all users */
	bool allUsers;

	/* Show ops from idle connections */
	bool idleConnections;

	/* Show ops from idle cursors */
	bool idleCursors;

	/* Show ops from idle sessions */
	bool idleSessions;

	/* Show ops from only current node */
	bool localOps;
} CurrentOpOptions;


/*
 * Wrapper holding a single activity in a worker.
 */
typedef struct
{
	/* the state field from pg_stat_activity */
	const char *state;

	/* The query string from pg_stat_activity */
	const char *query;

	/* the seconds since Epoch */
	int64 query_start;

	/* Seconds that the command is running */
	int secs_running;

	/* The mongo specific operationId for the operation */
	const char *operationId;

	/* The raw mongo table (if it could be determined) */
	const char *rawMongoTable;

	/* The wait_event_type in the pg_stat_activity */
	const char *wait_event_type;

	/* The postgres PID for the operation */
	int64 stat_pid;

	/* The global pid for the operation */
	int64 global_pid;

	/* The seconds since the backend last state change */
	int64 state_change_since;

	/* Mongo collection name determined from the table name */
	const char *processedMongoCollection;

	/* Mongo collection name determined from the table name */
	const char *processedMongoDatabase;

	/* During processing, whether or not to add index build stats */
	bool processedBuildIndexStatProgress;
} SingleWorkerActivity;

PG_FUNCTION_INFO_V1(command_current_op_command);
PG_FUNCTION_INFO_V1(command_current_op_worker);
PG_FUNCTION_INFO_V1(command_current_op_aggregation);


static void CurrentOpAggregateCore(PG_FUNCTION_ARGS, TupleDesc descriptor,
								   Tuplestorestate *tupleStore);
static void PopulateCurrentOpOptions(pgbson *bson, CurrentOpOptions *options);
static void MergeWorkerBsons(List *workerBsons, TupleDesc descriptor,
							 Tuplestorestate *tupleStore);
static pgbson * CurrentOpWorkerCore(void *spec);
static List * WorkerGetBaseActivities(void);
static void WriteOneActivityToDocument(SingleWorkerActivity *activity,
									   pgbson_writer *singleActivityWriter);
static const char * WriteCommandAndGetQueryType(const char *query,
												SingleWorkerActivity *activity,
												pgbson_writer *commandWriter);
static void DetectMongoCollection(SingleWorkerActivity *activity);
static void AddFailedIndexBuilds(TupleDesc descriptor, Tuplestorestate *tupleStore);
static const char * WriteIndexBuildProgressAndGetMessage(SingleWorkerActivity *activity,
														 pgbson_writer *writer);
static void WriteGlobalPidOfLockingProcess(SingleWorkerActivity *activity,
										   pgbson_writer *writer);


/*
 * Command wrapper for CurrentOp. Tracks feature counter usage
 * and simply calls the common logic
 */
Datum
command_current_op_command(PG_FUNCTION_ARGS)
{
	ReportFeatureUsage(FEATURE_COMMAND_CURRENTOP);

	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	CurrentOpAggregateCore(fcinfo, descriptor, tupleStore);
	PG_RETURN_VOID();
}


/*
 * Aggregation top level function for CurrentOp.
 * Returns the set of commands as an SRF.
 */
Datum
command_current_op_aggregation(PG_FUNCTION_ARGS)
{
	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	CurrentOpAggregateCore(fcinfo, descriptor, tupleStore);
	PG_RETURN_VOID();
}


/*
 * EntryPoint for currentOp inside the worker nodes.
 * This collects the raw data on a per-command basis and posts
 * it to the coordinator. Note that we need to do this in the worker
 * since pg_class, pg_locks etc are not replicated tables and so they
 * need to be joined between pg_stat_activity on the node.
 * The processed rows are sent back to the coordinator as a single BSON.
 */
Datum
command_current_op_worker(PG_FUNCTION_ARGS)
{
	pgbson *currentOpSpec = PG_GETARG_PGBSON(0);

	pgbson *response = RunWorkerDiagnosticLogic(&CurrentOpWorkerCore, currentOpSpec);

	PG_RETURN_POINTER(response);
}


/*
 * Core implementation logic for currentOp on the query Coordinator.
 */
static void
CurrentOpAggregateCore(PG_FUNCTION_ARGS, TupleDesc descriptor,
					   Tuplestorestate *tupleStore)
{
	pgbson *spec = PG_GETARG_PGBSON(0);

	CurrentOpOptions options = { 0 };
	PopulateCurrentOpOptions(spec, &options);

	List *workerBsons = NIL;
	if (options.localOps)
	{
		workerBsons = list_make1(CurrentOpWorkerCore(spec));
	}
	else
	{
		const char *query = "SELECT success, result FROM run_command_on_all_nodes("
							"FORMAT($$ SELECT mongo_api_v1.current_op_worker(%L) $$, $1))";

		int numValues = 1;
		Datum values[1] = { PointerGetDatum(spec) };
		Oid types[1] = { BsonTypeId() };
		workerBsons = GetWorkerBsonsFromAllWorkers(query, values, types, numValues,
												   "CurrentOp");
	}

	MergeWorkerBsons(workerBsons, descriptor, tupleStore);

	/* The index queue and build status is only valid on the coordinator - run this only here.
	 * TODO: For MX support, this needs to be run_command_on_coordinator.
	 */
	AddFailedIndexBuilds(descriptor, tupleStore);
}


/*
 * Populates the CurrentOp options based on the command spec.
 */
static void
PopulateCurrentOpOptions(pgbson *spec, CurrentOpOptions *options)
{
	bson_iter_t currentOpIter;
	PgbsonInitIterator(spec, &currentOpIter);
	while (bson_iter_next(&currentOpIter))
	{
		const char *path = bson_iter_key(&currentOpIter);
		const bson_value_t *value = bson_iter_value(&currentOpIter);
		EnsureTopLevelFieldIsBooleanLike(path, &currentOpIter);

		bool valueBool = BsonValueAsBool(value);
		if (strcmp(path, "allUsers") == 0)
		{
			options->allUsers = valueBool;
		}
		else if (strcmp(path, "idleConnections") == 0)
		{
			options->idleConnections = valueBool;
		}
		else if (strcmp(path, "idleCursors") == 0)
		{
			options->idleCursors = valueBool;
		}
		else if (strcmp(path, "idleSessions") == 0)
		{
			options->idleSessions = valueBool;
		}
		else if (strcmp(path, "localOps") == 0)
		{
			options->localOps = valueBool;
		}
		else if (strcmp(path, "backtrace") == 0)
		{
			/* ignore*/
		}
	}
}


/*
 * Logic that builds the currentOp responses (only runs on query coordinator)
 */
static void
MergeWorkerBsons(List *workerBsons, TupleDesc descriptor, Tuplestorestate *tupleStore)
{
	ListCell *workerCell;
	foreach(workerCell, workerBsons)
	{
		pgbson *workerBson = lfirst(workerCell);
		bson_iter_t workerIter;
		PgbsonInitIterator(workerBson, &workerIter);

		int errorCode = 0;
		const char *errorMessage = NULL;
		while (bson_iter_next(&workerIter))
		{
			const char *key = bson_iter_key(&workerIter);
			if (strcmp(key, ErrCodeKey) == 0)
			{
				errorCode = BsonValueAsInt32(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, ErrMsgKey) == 0)
			{
				const char *string = bson_iter_utf8(&workerIter, NULL);
				errorMessage = pstrdup(string);
			}
			else if (strcmp(key, "activities") == 0)
			{
				bson_iter_t activityIter;
				if (bson_iter_recurse(&workerIter, &activityIter))
				{
					while (bson_iter_next(&activityIter))
					{
						pgbson *docBson = PgbsonInitFromDocumentBsonValue(bson_iter_value(
																			  &
																			  activityIter));

						Datum tupleValue[1] = { PointerGetDatum(docBson) };
						bool nulls[1] = { false };
						tuplestore_putvalues(tupleStore, descriptor, tupleValue, nulls);
					}
				}
			}
			else
			{
				ereport(ERROR, (errmsg("unknown field received from currentOp worker %s",
									   key)));
			}
		}

		if (errorMessage != NULL)
		{
			errorCode = errorCode == 0 ? MongoInternalError : errorCode;
			ereport(ERROR, (errcode(errorCode), errmsg("Error running currentOp: %s",
													   errorMessage)));
		}
	}
}


/*
 * Core data gathering logic in the worker nodes (executes on all workers + coordinator)
 */
static pgbson *
CurrentOpWorkerCore(void *specPointer)
{
	pgbson *spec = (pgbson *) specPointer;
	CurrentOpOptions options = { 0 };
	PopulateCurrentOpOptions(spec, &options);

	List *activities = WorkerGetBaseActivities();

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	pgbson_array_writer childWriter;
	PgbsonWriterStartArray(&writer, "activities", 10, &childWriter);

	ListCell *cell;
	foreach(cell, activities)
	{
		SingleWorkerActivity *activity = lfirst(cell);

		if (!options.idleConnections && !options.idleSessions &&
			strcmp(activity->state, "active") != 0)
		{
			/* Skip inactive sessions if not requested */
			continue;
		}

		/* by default we would want to just send the activity up - but we really
		 * need to post-process the activity here. This is because things like
		 * index progress need to be handled fully on the worker (the tables aren't
		 * distributed).
		 */
		pgbson_writer singleActivityWriter;
		PgbsonArrayWriterStartDocument(&childWriter, &singleActivityWriter);
		WriteOneActivityToDocument(activity, &singleActivityWriter);
		PgbsonArrayWriterEndDocument(&childWriter, &singleActivityWriter);
	}

	list_free_deep(activities);

	PgbsonWriterEndArray(&writer, &childWriter);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Extracts necessary information in the worker from pg_stat_activity.
 * pg_stat_activity has a PID, we also get the opid, global_pid, lock info,
 * running information, and join it with pg_locks to get any tables being
 * accessed by this query.
 * Each activity is parsed into a SingleWorkerActivity struct to be processed
 * later in the WriteOneActivityToDocument function
 */
static List *
WorkerGetBaseActivities()
{
	/* Now that we're on the worker node, first get the query of operations */

	/* This is similar to the original currentOp query in SQL (Pre 1.9 )
	 * Add new fields to the end of the selectors only */
	StringInfo queryInfo = makeStringInfo();
	appendStringInfoString(queryInfo,
						   " SELECT "
						   " pa.query AS query, "
						   " pa.pid::bigint AS stat_pid, "
						   " pa.state AS state, "
						   " pa.global_pid::bigint AS global_pid, "
						   " collectionRawName AS mongo_collection_raw, "
						   " EXTRACT(epoch FROM pa.query_start)::bigint AS query_start, "
						   " EXTRACT(epoch FROM now() - pa.query_start)::bigint AS secs_running, "
						   " pa.wait_event_type AS wait_event_type, "
						   " pa.global_pid || ':' || (EXTRACT(epoch FROM pa.query_start) * 1000000)::numeric(20,0) AS op_id, "
						   " EXTRACT(epoch FROM now() - pa.state_change)::bigint AS state_change_since "
						   " FROM (SELECT * FROM pg_stat_activity LEFT JOIN pg_catalog.get_all_active_transactions() ON process_id = pid) pa LEFT JOIN lateral "
						   " ( "
						   "	SELECT c.relname::text AS collectionRawName "
						   "	FROM pg_locks pl "
						   "	JOIN pg_class c ON (pl.relation = c.oid) "
						   "	JOIN pg_namespace nsp ON (c.relnamespace = nsp.oid) ");

	appendStringInfo(queryInfo,
					 "	WHERE nsp.nspname = '%s' AND c.relkind = 'r' AND c.oid != '%s.changes'::regclass AND pl.pid = pa.pid LIMIT 1",
					 ApiDataSchemaName, ApiDataSchemaName);

	appendStringInfoString(queryInfo,
						   " ) e2 ON true WHERE (NOT query LIKE '%mongo_api_v1.current_op%') AND (application_name = 'MongoGateway-Data' OR application_name = 'PgmongoBackend')");

	List *workerActivities = NIL;
	SPIParseOpenOptions parseOptions =
	{
		.read_only = true,
		.cursorOptions = 0,
		.params = NULL
	};
	MemoryContext priorMemoryContext = CurrentMemoryContext;
	SPI_connect();

	Portal currentOpPortal = SPI_cursor_parse_open("pgstatsportal", queryInfo->data,
												   &parseOptions);
	bool hasData = true;

	while (hasData)
	{
		SPI_cursor_fetch(currentOpPortal, true, 1);

		hasData = SPI_processed >= 1;
		if (!hasData)
		{
			break;
		}

		bool isNull;
		MemoryContext spiContext = NULL;
		SingleWorkerActivity *activity = MemoryContextAllocZero(priorMemoryContext,
																sizeof(
																	SingleWorkerActivity));

		/* Get Query (Attr 1)*/
		Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
										  SPI_tuptable->tupdesc, 1,
										  &isNull);
		if (!isNull)
		{
			spiContext = MemoryContextSwitchTo(priorMemoryContext);
			activity->query = TextDatumGetCString(resultDatum);
			MemoryContextSwitchTo(spiContext);
		}
		else
		{
			activity->query = "";
		}

		/* stat_pid (Attr 2) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 2,
									&isNull);
		if (!isNull)
		{
			activity->stat_pid = DatumGetInt64(resultDatum);
		}

		/* state (Attr 3) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 3,
									&isNull);
		if (!isNull)
		{
			spiContext = MemoryContextSwitchTo(priorMemoryContext);
			activity->state = TextDatumGetCString(resultDatum);
			MemoryContextSwitchTo(spiContext);
		}
		else
		{
			activity->state = "unknown";
		}

		/* GlobalPid (Attr 4 ) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 4,
									&isNull);
		if (!isNull)
		{
			activity->global_pid = DatumGetInt64(resultDatum);
		}

		/* CollectionNameRaw (attr 5) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 5,
									&isNull);
		if (!isNull)
		{
			spiContext = MemoryContextSwitchTo(priorMemoryContext);
			activity->rawMongoTable = TextDatumGetCString(resultDatum);
			MemoryContextSwitchTo(spiContext);
		}
		else
		{
			activity->rawMongoTable = NULL;
		}

		/* Query_Start (Attr 6) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 6,
									&isNull);
		if (!isNull)
		{
			activity->query_start = DatumGetInt64(resultDatum);
		}

		/* secs_running (Attr 7) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 7,
									&isNull);
		if (!isNull)
		{
			activity->secs_running = DatumGetInt64(resultDatum);
		}

		/* wait_event_type (Attr 8) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 8,
									&isNull);
		if (!isNull)
		{
			spiContext = MemoryContextSwitchTo(priorMemoryContext);
			activity->wait_event_type = TextDatumGetCString(resultDatum);
			MemoryContextSwitchTo(spiContext);
		}
		else
		{
			activity->wait_event_type = "";
		}

		/* op_id (Attr 9) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 9,
									&isNull);
		if (!isNull)
		{
			spiContext = MemoryContextSwitchTo(priorMemoryContext);
			activity->operationId = TextDatumGetCString(resultDatum);
			MemoryContextSwitchTo(spiContext);
		}
		else
		{
			activity->operationId = "";
		}

		/* state_change_since (Attr 10) */
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, 10,
									&isNull);
		if (!isNull)
		{
			activity->state_change_since = DatumGetInt64(resultDatum);
		}

		spiContext = MemoryContextSwitchTo(priorMemoryContext);
		workerActivities = lappend(workerActivities, activity);
		MemoryContextSwitchTo(spiContext);
	}

	SPI_cursor_close(currentOpPortal);
	SPI_finish();

	return workerActivities;
}


/*
 * Given an activity on a worker node via SingleWorkerActivity,
 * writes the activity to the target pgbson_writer. The activity
 * is written in the Mongo Compatible currentOp format.
 */
static void
WriteOneActivityToDocument(SingleWorkerActivity *workerActivity,
						   pgbson_writer *singleActivityWriter)
{
	/* First step - detect the mongo collection */
	DetectMongoCollection(workerActivity);

	PgbsonWriterAppendUtf8(singleActivityWriter, "shard", 5, "defaultShard");
	bool isActive = false;
	const char *type = "unknown";
	if (strcmp(workerActivity->state, "active") == 0)
	{
		isActive = true;
		type = "op";
	}
	else if (strcmp(workerActivity->state, "idle") == 0)
	{
		type = "idleSession";
	}
	else if (strcmp(workerActivity->state, "idle in transaction") == 0)
	{
		/* We mark these differently to provide info to the user */
		type = "idleSessionInTransaction";
	}
	else if (strcmp(workerActivity->state, "idle in transaction (aborted)") == 0)
	{
		/* We mark these differently to provide info to the user */
		type = "idleSessionInTransaction";
	}

	PgbsonWriterAppendBool(singleActivityWriter, "active", 6, isActive);
	PgbsonWriterAppendUtf8(singleActivityWriter, "type", 4, type);

	PgbsonWriterAppendUtf8(singleActivityWriter, "opid", 4, workerActivity->operationId);

	/* report the globalPid so that we can track locks back to this process */
	PgbsonWriterAppendInt64(singleActivityWriter, "op_prefix", 9,
							workerActivity->global_pid);


	if (isActive)
	{
		/* ns is only valid for an active query. */
		int databaseNameLength = strlen(workerActivity->processedMongoDatabase);
		if (databaseNameLength > 0)
		{
			char mongoNamespace[MAX_NAMESPACE_NAME_LENGTH] = { 0 };
			memcpy(mongoNamespace, workerActivity->processedMongoDatabase,
				   databaseNameLength);

			int collectionLength = strlen(workerActivity->processedMongoCollection);
			if (collectionLength > 0)
			{
				mongoNamespace[databaseNameLength] = '.';
			}

			memcpy(&mongoNamespace[databaseNameLength + 1],
				   workerActivity->processedMongoCollection, collectionLength);
			PgbsonWriterAppendUtf8(singleActivityWriter, "ns", 2, mongoNamespace);
		}

		bson_value_t stagingValue = { 0 };
		if (workerActivity->query_start > 0)
		{
			stagingValue.value_type = BSON_TYPE_DATE_TIME;
			stagingValue.value.v_datetime = workerActivity->query_start * 1000;
			PgbsonWriterAppendValue(singleActivityWriter, "currentOpTime", 13,
									&stagingValue);
		}

		PgbsonWriterAppendInt64(singleActivityWriter, "secs_running", 12,
								workerActivity->secs_running);

		pgbson_writer commandDocumentWriter;
		PgbsonWriterStartDocument(singleActivityWriter, "command", 7,
								  &commandDocumentWriter);

		const char *queryType = WriteCommandAndGetQueryType(workerActivity->query,
															workerActivity,
															&commandDocumentWriter);
		PgbsonWriterEndDocument(singleActivityWriter, &commandDocumentWriter);
		PgbsonWriterAppendUtf8(singleActivityWriter, "op", 2, queryType);
	}
	else if (workerActivity->state_change_since > 0)
	{
		PgbsonWriterAppendInt64(singleActivityWriter, "secs_idle", 9,
								workerActivity->state_change_since);
	}

	bool waitingForLock = false;
	if (strcmp(workerActivity->wait_event_type, "Lock") == 0 ||
		strcmp(workerActivity->wait_event_type, "Extension") == 0 ||
		strcmp(workerActivity->wait_event_type, "LWLock") == 0)
	{
		waitingForLock = true;
	}

	PgbsonWriterAppendBool(singleActivityWriter, "waitingForLock", 14, waitingForLock);

	/* If we can extract it get the locking process (to help recover if needed) */
	if (waitingForLock)
	{
		WriteGlobalPidOfLockingProcess(workerActivity, singleActivityWriter);
	}

	/* If the command requested logging index build progress, query and log it */
	if (workerActivity->processedBuildIndexStatProgress && workerActivity->stat_pid > 0)
	{
		pgbson_writer progressWriter;
		PgbsonWriterStartDocument(singleActivityWriter, "progress", 8, &progressWriter);
		const char *message = WriteIndexBuildProgressAndGetMessage(workerActivity,
																   &progressWriter);
		PgbsonWriterEndDocument(singleActivityWriter, &progressWriter);

		if (message != NULL)
		{
			PgbsonWriterAppendUtf8(singleActivityWriter, "msg", 3, message);
		}
	}
}


/*
 * Parses the "query" of the pg_stat_activity and returns the top level "op"
 * for that query. Also updates the "command" document for the operation in the
 * writer.
 * Note that beyond Mongo's commands, we add a "workerCommand" type for internal
 * queries e.g. the actual CREATE INDEX or ALTER TABLE, or the update_one calls.
 * TODO: Can we figure out a way to do a binary search for this code?
 */
static const char *
WriteCommandAndGetQueryType(const char *query, SingleWorkerActivity *activity,
							pgbson_writer *commandWriter)
{
	if (strstr(query, "mongo_api_v1.update(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "update", 6,
							   activity->processedMongoCollection);
		return "update";
	}
	else if (strstr(query, "mongo_api_internal.update_one(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "update", 6,
							   activity->processedMongoCollection);
		return "workerCommand";
	}
	else if (strstr(query, "mongo_api_v1.insert(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "insert", 6,
							   activity->processedMongoCollection);
		return "insert";
	}
	else if (strstr(query, "mongo_api_internal.insert_one(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "insert", 6,
							   activity->processedMongoCollection);
		return "workerCommand";
	}
	else if (strstr(query, "mongo_api_v1.delete(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "delete", 6,
							   activity->processedMongoCollection);
		return "remove";
	}
	else if (strstr(query, "mongo_api_internal.delete_one(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "delete", 6,
							   activity->processedMongoCollection);
		return "workerCommand";
	}
	else if (strstr(query, "mongo_api_v1.cursor_get_more(") != NULL)
	{
		PgbsonWriterAppendInt64(commandWriter, "getMore", 6, 0);
		return "getmore";
	}
	else if (strstr(query, "mongo_api_v1.find_cursor_first_page(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "find", 4,
							   activity->processedMongoCollection);
		return "query";
	}
	else if (strstr(query, "mongo_api_v1.find_and_modify(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "findAndModify", 13,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.aggregate_cursor_first_page(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "aggregate", 9,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.count_query(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "count", 5,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.distinct_query(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "distinct", 8,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.list_collections_cursor_first_page(") != NULL)
	{
		PgbsonWriterAppendInt64(commandWriter, "listCollections", 15, 1);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.list_indexes_cursor_first_page(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "listIndexes", 11,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_internal.create_indexes_non_concurrently(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.create_indexes(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "CREATE INDEX") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		activity->processedBuildIndexStatProgress = true;
		return "workerCommand";
	}
	else if (strstr(query, "ALTER TABLE") != NULL && strstr(query, "ADD CONSTRAINT") !=
			 NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		PgbsonWriterAppendBool(commandWriter, "unique", 6, true);
		activity->processedBuildIndexStatProgress = true;
		return "workerCommand";
	}
	else if (strstr(query, "mongo_api_v1.drop_indexes(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "dropIndexes", 11,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.coll_stats(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "collStats", 9,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.create_collection_view(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "create", 6,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.coll_mod(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "collMod", 7,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.shard_collection(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "shardCollection", 15,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.drop_collection(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "drop", 4,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "mongo_api_v1.drop_database(") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "dropDatabase", 13,
							   activity->processedMongoDatabase);
		return "command";
	}
	else
	{
		return "command";
	}
}


/*
 * Looks at the "raw" Mongo collection and extracts the
 * actual Mongo database/collection.
 */
static void
DetectMongoCollection(SingleWorkerActivity *activity)
{
	activity->processedMongoDatabase = "";
	activity->processedMongoCollection = "";

	/* No raw collection found */
	if (activity->rawMongoTable == NULL ||
		strlen(activity->rawMongoTable) < 11 ||
		strncmp(activity->rawMongoTable, "documents_", 10) != 0)
	{
		return;
	}

	uint64 collectionId = (uint64) atoll(&activity->rawMongoTable[10]);
	if (collectionId == 0)
	{
		return;
	}

	const MongoCollection *collection = GetMongoCollectionByColId(collectionId, NoLock);
	if (collection != NULL)
	{
		activity->processedMongoDatabase = collection->name.databaseName;
		activity->processedMongoCollection = collection->name.collectionName;
	}
}


/*
 * Port from the current_op 1.8 SQL file - tracks the index build failures from background index
 * build status and updates the currentOp document for it.
 */
static void
AddFailedIndexBuilds(TupleDesc descriptor, Tuplestorestate *tupleStore)
{
	/* This can be cleaned up to be better - but not in the current iteration */
	StringInfo queryInfo = makeStringInfo();
	appendStringInfo(queryInfo, " SELECT "
								" %s.index_spec_as_current_op_command(coll.database_name, coll.collection_name, ci.index_spec) AS command, "
								" iq.index_cmd_status AS status, "
								" COALESCE(iq.comment::text, '') AS comment, "
								" coll.database_name || '.' || coll.collection_name AS ns "
								" FROM %s.%s_index_queue AS iq "
								" JOIN %s.collection_indexes AS ci ON (iq.index_id = ci.index_id) "
								" JOIN %s.collections AS coll ON (ci.collection_id = coll.collection_id) "
								" WHERE iq.cmd_type = 'C' AND ( "
								" iq.index_cmd_status = 3 "
								" OR ( "
								"   iq.index_cmd_status = 2 "
								"   AND iq.global_pid IS NOT NULL "
								"   AND iq.global_pid NOT IN ( "
								"       SELECT distinct global_pid FROM citus_stat_activity WHERE global_pid IS NOT NULL "
								"       ) "
								"   ) "
								" )", ApiInternalSchemaName, ApiCatalogSchemaName,
					 ExtensionObjectPrefix,
					 ApiCatalogSchemaName, ApiCatalogSchemaName);

	SPIParseOpenOptions parseOptions =
	{
		.read_only = true,
		.cursorOptions = 0,
		.params = NULL
	};
	MemoryContext priorMemoryContext = CurrentMemoryContext;
	SPI_connect();

	Portal currentOpPortal = SPI_cursor_parse_open("failedIndexesPortal", queryInfo->data,
												   &parseOptions);
	bool hasData = true;
	while (hasData)
	{
		SPI_cursor_fetch(currentOpPortal, true, 1);

		hasData = SPI_processed >= 1;
		if (!hasData)
		{
			break;
		}

		bool isNull;
		AttrNumber commandAttr = 1;
		Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
										  SPI_tuptable->tupdesc, commandAttr,
										  &isNull);
		if (isNull)
		{
			continue;
		}

		pgbson *commandDoc = DatumGetPgBson(SPI_datumTransfer(resultDatum, false, -1));

		AttrNumber statusAttr = 2;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, statusAttr,
									&isNull);
		int32 status = DatumGetInt32(resultDatum);

		AttrNumber commentAttr = 3;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, commentAttr,
									&isNull);
		char *comment = TextDatumGetCString(SPI_datumTransfer(resultDatum, false, -1));

		AttrNumber nsAttr = 4;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, nsAttr,
									&isNull);
		char *ns = TextDatumGetCString(SPI_datumTransfer(resultDatum, false, -1));

		char *msg = "";
		if (status == IndexCmdStatus_Failed)
		{
			pgbson *bsonDoc;
			if (IsBsonHexadecimalString(comment))
			{
				bsonDoc = PgbsonInitFromHexadecimalString(comment);
			}
			else
			{
				bsonDoc = PgbsonInitFromJson(comment);
			}

			const char *path = "err_msg";
			bson_iter_t pathIterator;
			if (PgbsonInitIteratorAtPath(bsonDoc, path, &pathIterator))
			{
				const bson_value_t *value = bson_iter_value(&pathIterator);
				msg = psprintf(
					"Index build failed with error '%s', Index build will be retried",
					value->value.v_utf8.str);
			}
		}
		else if (status == IndexCmdStatus_Inprogress)
		{
			msg = "Index build failed, Index build will be retried";
		}

		pgbson_writer finalWriter;
		PgbsonWriterInit(&finalWriter);
		PgbsonWriterAppendUtf8(&finalWriter, "shard", 5, "defaultShard");
		PgbsonWriterAppendUtf8(&finalWriter, "op", 2, "command");
		PgbsonWriterAppendDocument(&finalWriter, "command", 7, commandDoc);
		PgbsonWriterAppendUtf8(&finalWriter, "type", 4, "op");
		PgbsonWriterAppendUtf8(&finalWriter, "msg", 3, msg);
		PgbsonWriterAppendBool(&finalWriter, "active", 6, false);
		PgbsonWriterAppendUtf8(&finalWriter, "ns", 2, ns);
		pgbson *activity = PgbsonWriterGetPgbson(&finalWriter);

		MemoryContext spiContext = MemoryContextSwitchTo(priorMemoryContext);
		Datum values[1] = { PointerGetDatum(activity) };
		bool nulls[1] = { false };
		tuplestore_putvalues(tupleStore, descriptor, values, nulls);
		MemoryContextSwitchTo(spiContext);
	}

	SPI_cursor_close(currentOpPortal);
	SPI_finish();
}


/*
 * Gets the index progress data from pg_stat_progress_create_index and writes it out to the
 * "progress" document as well as builds a message for the top level currentOp.
 */
static const char *
WriteIndexBuildProgressAndGetMessage(SingleWorkerActivity *activity,
									 pgbson_writer *writer)
{
	const char *query =
		"SELECT phase, blocks_done, blocks_total, tuples_done, tuples_total, pg_catalog.citus_calculate_gpid(pg_catalog.citus_nodeid_for_gpid($2), current_locker_pid::integer)"
		" FROM pg_stat_progress_create_index WHERE pid = $1 LIMIT 1";

	Oid argTypes[2] = { INT8OID, INT8OID };
	Datum argValues[2] = {
		Int64GetDatum(activity->stat_pid), Int64GetDatum(activity->global_pid)
	};
	char argNulls[2] = { ' ', ' ' };
	bool readOnly = true;

	Datum outputValues[6] = { 0 };
	bool outputNulls[6] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(query, 2, argTypes, argValues, argNulls,
												  readOnly, SPI_OK_SELECT, outputValues,
												  outputNulls, 6);

	/* Phase */
	StringInfo messageInfo = makeStringInfo();
	if (!outputNulls[0])
	{
		const char *phaseString = TextDatumGetCString(outputValues[0]);
		if (strcmp(phaseString, "initializing") == 0)
		{
			appendStringInfo(messageInfo, "Initializing index.");
		}
		else if (strcmp(phaseString, "waiting for old snapshots") == 0)
		{
			appendStringInfo(messageInfo,
							 "Index is waiting for commands to reach snapshot threshold.");
		}
		else if (strstr(phaseString, "waiting for") != NULL)
		{
			appendStringInfo(messageInfo,
							 "Index is waiting for other concurrent commands.");
		}
		else if (strcmp(phaseString, "building index") == 0)
		{
			appendStringInfo(messageInfo, "Building index.");
		}
		else if (strstr(phaseString, "index validation") != NULL)
		{
			appendStringInfo(messageInfo, "Validating index.");
		}
		else
		{
			appendStringInfo(messageInfo, "Index is in an unknown phase");
			ereport(WARNING, (errmsg("Index build is in an unknown phase %s",
									 phaseString)));
		}
	}

	if (!outputNulls[1] && !outputNulls[2])
	{
		/* Blocks are available */
		int64 blocksDone = DatumGetInt64(outputValues[1]);
		int64 blocksTotal = DatumGetInt64(outputValues[2]);

		double progress = blocksDone * 1.0 / blocksTotal;

		appendStringInfo(messageInfo, "Progress %.10f.", progress);

		PgbsonWriterAppendInt64(writer, "blocks_done", 11, blocksDone);
		PgbsonWriterAppendInt64(writer, "blocks_total", 12, blocksTotal);
	}

	if (!outputNulls[3] && !outputNulls[4])
	{
		PgbsonWriterAppendInt64(writer, "documents_done", 11, DatumGetInt64(
									outputValues[3]));
		PgbsonWriterAppendInt64(writer, "documents_total", 12, DatumGetInt64(
									outputValues[4]));
	}

	if (!outputNulls[5])
	{
		appendStringInfo(messageInfo, "Waiting on op_prefix: %ld.", DatumGetInt64(
							 outputValues[5]));
	}

	return messageInfo->data;
}


/*
 * Given an activity that's waiting on a lock, gets the process that currently holds that lock.
 */
static void
WriteGlobalPidOfLockingProcess(SingleWorkerActivity *activity, pgbson_writer *writer)
{
	const char *query =
		"SELECT array_agg(pg_catalog.citus_calculate_gpid(pg_catalog.citus_nodeid_for_gpid($2), pid::integer)) "
		" FROM unnest(pg_blocking_pids($1::integer)) pid LIMIT 1";

	Oid argTypes[2] = { INT8OID, INT8OID };
	Datum argValues[2] = {
		PointerGetDatum(activity->stat_pid), Int64GetDatum(activity->global_pid)
	};
	char argNulls[2] = { ' ', ' ' };
	bool readOnly = true;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, 2, argTypes, argValues,
													   argNulls,
													   readOnly, SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		return;
	}

	ArrayType *val_array = DatumGetArrayTypeP(result);

	Datum *val_datums;
	bool *val_is_null_marker;
	int val_count;

	deconstruct_array(val_array,
					  ARR_ELEMTYPE(val_array), -1, false, TYPALIGN_INT,
					  &val_datums, &val_is_null_marker, &val_count);

	PgbsonWriterAppendInt32(writer, "writeConflicts", 14, val_count);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, "locking_op_prefixes", 19, &arrayWriter);

	for (int i = 0; i < val_count; i++)
	{
		if (!val_is_null_marker[i])
		{
			bson_value_t arrayValue = { 0 };
			arrayValue.value_type = BSON_TYPE_INT64;
			arrayValue.value.v_int64 = DatumGetInt64(val_datums[i]);
			PgbsonArrayWriterWriteValue(&arrayWriter, &arrayValue);
		}
	}

	PgbsonWriterEndArray(writer, &arrayWriter);

	pfree(val_array);
	pfree(val_datums);
	pfree(val_is_null_marker);
}
