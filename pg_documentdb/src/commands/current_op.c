
/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/current_op.c
 *
 * Implementation of the current_op command.
 *-------------------------------------------------------------------------
 */
#include <math.h>
#include <postgres.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <funcapi.h>
#include <utils/builtins.h>
#include <catalog/namespace.h>
#include <catalog/pg_am_d.h>

#include "io/bson_core.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"
#include "utils/feature_counter.h"
#include "utils/hashset_utils.h"
#include "utils/version_utils.h"
#include "utils/query_utils.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "commands/diagnostic_commands_common.h"
#include "io/bson_set_returning_functions.h"
#include "metadata/collection.h"
#include "metadata/index.h"
#include "utils/guc_utils.h"


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
	char *rawMongoTable;

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

	/* Index spec for running create Index */
	IndexSpec *indexSpec;
} SingleWorkerActivity;

PG_FUNCTION_INFO_V1(command_current_op);
PG_FUNCTION_INFO_V1(command_current_op_command);
PG_FUNCTION_INFO_V1(command_current_op_worker);
PG_FUNCTION_INFO_V1(command_current_op_aggregation);

static pgbson * BuildAggregateSpecFromCommandSpec(pgbson *commandSpec,
												  pgbson **filterSpec);
static void CurrentOpAggregateCore(pgbson *spec, TupleDesc descriptor,
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
static IndexSpec * GetIndexSpecForShardedCreateIndexQuery(SingleWorkerActivity *activity);
static void AddIndexBuilds(TupleDesc descriptor, Tuplestorestate *tupleStore);
static const char * WriteIndexBuildProgressAndGetMessage(SingleWorkerActivity *activity,
														 pgbson_writer *writer);
static void WriteGlobalPidOfLockingProcess(SingleWorkerActivity *activity,
										   pgbson_writer *writer);
static void WriteIndexSpec(SingleWorkerActivity *activity, pgbson_writer *commandWriter);

extern char *CurrentOpApplicationName;


/* Single node scenario - the global_pid can be assumed to be just the one for the coordinator */
char *DistributedOperationsQuery =
	"SELECT *, (10000000000 + pid)::int8 as global_pid FROM pg_stat_activity";

/* Similar in logic to the distributed node-id calculation */
const char *FirstLockingPidQuery =
	"SELECT array_agg( (($2 / 10000000000) * 10000000000) + pid::integer) FROM unnest(pg_blocking_pids($1::integer)) pid LIMIT 1";

/*
 * Command wrapper for CurrentOp. Tracks feature counter usage
 * and simply calls the common logic.
 * TODO: Deprecate this once the service doesn't depend on it.
 */
Datum
command_current_op_command(PG_FUNCTION_ARGS)
{
	ReportFeatureUsage(FEATURE_COMMAND_CURRENTOP);

	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	CurrentOpAggregateCore(PG_GETARG_PGBSON(0), descriptor, tupleStore);

	PG_RETURN_VOID();
}


Datum
command_current_op(PG_FUNCTION_ARGS)
{
	ReportFeatureUsage(FEATURE_COMMAND_CURRENTOP);

	pgbson *commandSpec = PG_GETARG_PGBSON(0);
	pgbson *filterSpec = NULL;
	pgbson *aggregateSpec = BuildAggregateSpecFromCommandSpec(commandSpec, &filterSpec);

	/* Now write the response */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	StringInfo str = makeStringInfo();
	appendStringInfo(str,
					 "WITH currentOpQuery AS (SELECT %s.current_op_aggregation($1) AS document), "
					 "currentOpResponse AS (SELECT COALESCE(array_agg(document), '{}') AS \"inprog\", "
					 " 1::float AS \"ok\" FROM currentOpQuery WHERE $2 IS NULL OR document OPERATOR(%s.@@) $2) "
					 " SELECT %s.row_get_bson(currentOpResponse) FROM currentOpResponse",
					 ApiInternalSchemaNameV2, ApiCatalogSchemaName, CoreSchemaName);

	Oid argTypes[2] = { BsonTypeId(), BsonTypeId() };
	Datum argValues[2] = { PointerGetDatum(aggregateSpec), PointerGetDatum(filterSpec) };
	char argNulls[2] = { ' ', ' ' };
	if (filterSpec == NULL)
	{
		argNulls[1] = 'n';
	}

	bool readOnly = true;
	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(str->data, 2, argTypes, argValues,
													   argNulls, readOnly, SPI_OK_SELECT,
													   &isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg(
							"Unexpected - currentOp internal query returned a null response")));
	}

	PG_RETURN_DATUM(result);
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

	CurrentOpAggregateCore(PG_GETARG_PGBSON(0), descriptor, tupleStore);
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
CurrentOpAggregateCore(pgbson *spec, TupleDesc descriptor,
					   Tuplestorestate *tupleStore)
{
	CurrentOpOptions options = { 0 };
	PopulateCurrentOpOptions(spec, &options);

	List *workerBsons = NIL;
	if (options.localOps)
	{
		workerBsons = list_make1(CurrentOpWorkerCore(spec));
	}
	else
	{
		int numValues = 1;
		Datum values[1] = { PointerGetDatum(spec) };
		Oid types[1] = { BsonTypeId() };
		workerBsons = RunQueryOnAllServerNodes("CurrentOp", values, types, numValues,
											   command_current_op_worker,
											   ApiToApiInternalSchemaName,
											   "current_op_worker");
	}

	MergeWorkerBsons(workerBsons, descriptor, tupleStore);

	/* The index queue and build status only needs to run on the coordinator
	 * run this only here.
	 */
	AddIndexBuilds(descriptor, tupleStore);
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
			errorCode = errorCode == 0 ? ERRCODE_DOCUMENTDB_INTERNALERROR : errorCode;
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
						   " EXTRACT(epoch FROM now() - pa.state_change)::bigint AS state_change_since FROM (");

	appendStringInfoString(queryInfo, DistributedOperationsQuery);

	/* To get the collections associated with the command join with locks */
	appendStringInfoString(queryInfo, ") pa LEFT JOIN lateral "
									  " ( "
									  "	SELECT c.relname::text AS collectionRawName "
									  "	FROM pg_locks pl "
									  "	JOIN pg_class c ON (pl.relation = c.oid) "
									  "	JOIN pg_namespace nsp ON (c.relnamespace = nsp.oid) ");

	appendStringInfo(queryInfo,
					 "	WHERE nsp.nspname = '%s' AND c.relkind = 'r' AND pl.pid = pa.pid LIMIT 1",
					 ApiDataSchemaName);

	appendStringInfo(queryInfo,
					 " ) e2 ON true WHERE (NOT query LIKE '%%%s.current_op%%') ",
					 ApiToApiInternalSchemaName);

	if (CurrentOpApplicationName != NULL &&
		strlen(CurrentOpApplicationName) > 0)
	{
		appendStringInfo(queryInfo,
						 " AND (application_name = '%s' OR application_name LIKE '%%%s')",
						 CurrentOpApplicationName, GetExtensionApplicationName());
	}

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
 * Gets the command name and details for a command executing in the
 * API schema
 */
static const char *
DetectApiSchemaCommand(const char *topLevelQuery, const char *schemaName,
					   SingleWorkerActivity *activity,
					   pgbson_writer *commandWriter)
{
	const char *namespaceQuery = strstr(topLevelQuery, schemaName);
	if (namespaceQuery == NULL)
	{
		return NULL;
	}

	const char *query = namespaceQuery + strlen(schemaName);
	if (strstr(query, ".update(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "update", 6,
							   activity->processedMongoCollection);
		return "update";
	}
	else if (strstr(query, ".insert(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "insert", 6,
							   activity->processedMongoCollection);
		return "insert";
	}
	else if (strstr(query, ".delete(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "delete", 6,
							   activity->processedMongoCollection);
		return "remove";
	}
	else if (strstr(query, ".cursor_get_more(") == query)
	{
		PgbsonWriterAppendInt64(commandWriter, "getMore", 6, 0);
		return "getmore";
	}
	else if (strstr(query, ".find_cursor_first_page(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "find", 4,
							   activity->processedMongoCollection);
		return "query";
	}
	else if (strstr(query, ".find_and_modify(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "findAndModify", 13,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".aggregate_cursor_first_page(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "aggregate", 9,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, "_catalog.bson_aggregation_pipeline(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "aggregate", 9,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".count_query(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "count", 5,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".distinct_query(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "distinct", 8,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".list_collections_cursor_first_page(") == query)
	{
		PgbsonWriterAppendInt64(commandWriter, "listCollections", 15, 1);
		return "command";
	}
	else if (strstr(query, ".list_indexes_cursor_first_page(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "listIndexes", 11,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".create_indexes(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".drop_indexes(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "dropIndexes", 11,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".coll_stats(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "collStats", 9,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".create_collection_view(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "create", 6,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".coll_mod(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "collMod", 7,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".shard_collection(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "shardCollection", 15,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".drop_collection(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "drop", 4,
							   activity->processedMongoCollection);
		return "command";
	}
	else if (strstr(query, ".drop_database(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "dropDatabase", 12,
							   activity->processedMongoDatabase);
		return "command";
	}

	return NULL;
}


/*
 * Gets the command name and details for a command executing in the
 * API internal schema
 */
static const char *
DetectApiInternalSchemaCommand(const char *topLevelQuery, const char *schemaName,
							   SingleWorkerActivity *activity,
							   pgbson_writer *commandWriter)
{
	const char *namespaceQuery = strstr(topLevelQuery, schemaName);
	if (namespaceQuery == NULL)
	{
		return NULL;
	}

	const char *query = namespaceQuery + strlen(schemaName);
	if (strstr(query, ".update_one(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "update", 6,
							   activity->processedMongoCollection);
		return "workerCommand";
	}
	else if (strstr(query, ".insert_one(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "insert", 6,
							   activity->processedMongoCollection);
		return "workerCommand";
	}
	else if (strstr(query, ".delete_one(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "delete", 6,
							   activity->processedMongoCollection);
		return "workerCommand";
	}
	else if (strstr(query, ".create_indexes_non_concurrently(") == query)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		return "command";
	}

	return NULL;
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
	/* Check for a command on the top level API namespace */
	const char *commandName = DetectApiSchemaCommand(query, ApiSchemaName, activity,
													 commandWriter);
	if (commandName != NULL)
	{
		return commandName;
	}

	/* Also check for ApiSchemaNameV2 explicitly */
	if (strcmp(ApiSchemaName, ApiSchemaNameV2) != 0)
	{
		commandName = DetectApiSchemaCommand(query, ApiSchemaNameV2, activity,
											 commandWriter);
		if (commandName != NULL)
		{
			return commandName;
		}
	}

	commandName = DetectApiInternalSchemaCommand(query, ApiInternalSchemaName, activity,
												 commandWriter);
	if (commandName != NULL)
	{
		return commandName;
	}

	/* Also check for ApiSchemaNameV2 explicitly */
	if (strcmp(ApiInternalSchemaName, DocumentDBApiInternalSchemaName) != 0)
	{
		commandName = DetectApiSchemaCommand(query, DocumentDBApiInternalSchemaName,
											 activity,
											 commandWriter);
		if (commandName != NULL)
		{
			return commandName;
		}
	}

	if (strstr(query, "CREATE INDEX") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		WriteIndexSpec(activity, commandWriter);
		activity->processedBuildIndexStatProgress = true;
		return "workerCommand";
	}
	else if (strstr(query, "ALTER TABLE") != NULL &&
			 strstr(query, "ADD CONSTRAINT") != NULL)
	{
		PgbsonWriterAppendUtf8(commandWriter, "createIndexes", 13,
							   activity->processedMongoCollection);
		PgbsonWriterAppendBool(commandWriter, "unique", 6, true);
		WriteIndexSpec(activity, commandWriter);
		activity->processedBuildIndexStatProgress = true;
		return "workerCommand";
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
	if (activity->rawMongoTable == NULL &&
		(strstr(activity->query, "CREATE INDEX") != NULL))
	{
		/* Get Index Spec for sharded create index query, for this activity->rawMongoTable could be null */
		IndexSpec *spec = GetIndexSpecForShardedCreateIndexQuery(activity);
		if (spec != NULL)
		{
			activity->indexSpec = spec;
		}
	}

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
 * GetIndexSpecForShardedCreateIndexQuery gets index_spec and collection_name for sharded CREATE INDEX query
 *  TODO: Remove citus dependency here.
 */
static IndexSpec *
GetIndexSpecForShardedCreateIndexQuery(SingleWorkerActivity *activity)
{
	ArrayType *indexAmIdsArray = NULL;
	int arraySize = 4;
	Datum indexAmIdsDatum[4] = { 0 };
	indexAmIdsDatum[0] = RumIndexAmId();
	indexAmIdsDatum[1] = PgVectorHNSWIndexAmId();
	indexAmIdsDatum[2] = PgVectorIvfFlatIndexAmId();
	indexAmIdsDatum[3] = GIST_AM_OID;

	/* TODO - Add diskann index am id */

	indexAmIdsArray = construct_array(indexAmIdsDatum, arraySize, OIDOID,
									  sizeof(Oid), true,
									  TYPALIGN_INT);

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT pc.relname::text AS relation_name FROM pg_stat_activity psa LEFT JOIN pg_locks pl ON psa.pid = pl.pid LEFT JOIN pg_class pc ON pl.relation = pc.oid WHERE psa.application_name = 'citus_internal gpid=%ld' AND pc.relam = ANY($1) LIMIT 1",
					 activity->global_pid);

	int argCount = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(indexAmIdsArray) };
	char argNulls[1] = { ' ' };

	bool readOnly = true;
	bool isNull;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(cmdStr->data, argCount, argTypes,
													   argValues,
													   argNulls,
													   readOnly, SPI_OK_SELECT, &isNull);
	if (isNull)
	{
		return NULL;
	}
	char *relationName = text_to_cstring(DatumGetTextP(result));

	int indexId = 0;
	int prefixLength = strlen(DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX);

	if (relationName == NULL ||
		strncmp(relationName, DOCUMENT_DATA_TABLE_INDEX_NAME_FORMAT_PREFIX,
				prefixLength) != 0)
	{
		return NULL;
	}

	/* relationName = documents_rum_index_497_102044 or documents_rum_index_497 */
	relationName += prefixLength;
	const char *underscore = strchr(relationName, '_');

	char *numEndPointer = NULL;
	int parsedIndexId = (int) strtol(relationName, &numEndPointer, 10);

	if (numEndPointer == underscore || numEndPointer == NULL || *numEndPointer == '\0')
	{
		indexId = parsedIndexId;
	}
	else
	{
		return NULL;
	}

	IndexDetails *detail = IndexIdGetIndexDetails(indexId);

	/* assign right value to rawMongoTable */
	activity->rawMongoTable = (char *) palloc(NAMEDATALEN);
	snprintf(activity->rawMongoTable, NAMEDATALEN, DOCUMENT_DATA_TABLE_NAME_FORMAT,
			 detail->collectionId);

	return &(detail->indexSpec);
}


/*
 * Port from the current_op 1.8 SQL file - tracks the index build failures from background index
 * build status and updates the currentOp document for it.
 */
static void
AddIndexBuilds(TupleDesc descriptor, Tuplestorestate *tupleStore)
{
	/* This can be cleaned up to be better - but not in the current iteration */
	StringInfo queryInfo = makeStringInfo();
	appendStringInfo(queryInfo, " SELECT "
								" coll.database_name AS database_name, "
								" coll.collection_name AS collection_name, "
								" ci.index_spec AS index_spec, "
								" iq.index_cmd_status AS status, "
								" COALESCE(iq.comment::text, '') AS comment, "
								" coll.database_name || '.' || coll.collection_name AS ns "
								" FROM %s AS iq "
								" JOIN %s.collection_indexes AS ci ON (iq.index_id = ci.index_id) "
								" JOIN %s.collections AS coll ON (ci.collection_id = coll.collection_id) "
								" WHERE iq.cmd_type = 'C' AND ( "
								" iq.index_cmd_status IN (1, 3) "
								" )", GetIndexQueueName(), ApiCatalogSchemaName,
					 ApiCatalogSchemaName);

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
		AttrNumber databaseAttr = 1;
		Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
										  SPI_tuptable->tupdesc, databaseAttr,
										  &isNull);
		if (isNull)
		{
			continue;
		}

		char *databaseName = TextDatumGetCString(SPI_datumTransfer(resultDatum, false,
																   -1));

		AttrNumber collectionAttr = 2;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, collectionAttr,
									&isNull);
		if (isNull)
		{
			continue;
		}

		char *collectionName = TextDatumGetCString(SPI_datumTransfer(resultDatum, false,
																	 -1));

		AttrNumber indexSpecAttr = 3;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, indexSpecAttr,
									&isNull);
		if (isNull)
		{
			continue;
		}

		IndexSpec *indexSpec = DatumGetIndexSpec(SPI_datumTransfer(resultDatum, false,
																   -1));

		AttrNumber statusAttr = 4;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, statusAttr,
									&isNull);
		int32 status = DatumGetInt32(resultDatum);

		AttrNumber commentAttr = 5;
		resultDatum = SPI_getbinval(SPI_tuptable->vals[0],
									SPI_tuptable->tupdesc, commentAttr,
									&isNull);
		char *comment = TextDatumGetCString(SPI_datumTransfer(resultDatum, false, -1));

		AttrNumber nsAttr = 6;
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
		else if (status == IndexCmdStatus_Queued)
		{
			msg = "Index build is queued";
		}

		pgbson_writer finalWriter;
		PgbsonWriterInit(&finalWriter);
		PgbsonWriterAppendUtf8(&finalWriter, "shard", 5, "defaultShard");
		PgbsonWriterAppendUtf8(&finalWriter, "op", 2, "command");

		pgbson_writer commandWriter;
		PgbsonWriterStartDocument(&finalWriter, "command", 7, &commandWriter);
		WriteIndexSpecAsCurrentOpCommand(&commandWriter, databaseName, collectionName,
										 indexSpec);
		PgbsonWriterEndDocument(&finalWriter, &commandWriter);
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


static const char *
PhaseToUserMessage(const char *phaseString)
{
	if (strcmp(phaseString, "initializing") == 0)
	{
		return "Initializing index.";
	}
	else if (strcmp(phaseString, "waiting for old snapshots") == 0)
	{
		return "Index is waiting for commands to reach snapshot threshold.";
	}
	else if (strstr(phaseString, "waiting for") != NULL)
	{
		return "Index is waiting for other concurrent commands.";
	}
	else if (strcmp(phaseString, "building index") == 0)
	{
		return "Building index.";
	}
	else if (strstr(phaseString, "index validation") != NULL)
	{
		return "Validating index.";
	}
	else
	{
		ereport(WARNING, (errmsg("Index build is in an unknown phase %s",
								 phaseString)));
		return "Index is in an unknown phase";
	}
}


/*
 * Gets the index progress data from pg_stat_progress_create_index and writes it out to the
 * "progress" document as well as builds a message for the top level currentOp.
 * TODO: Remove citus dependency here.
 */
static const char *
WriteIndexBuildProgressAndGetMessage(SingleWorkerActivity *activity,
									 pgbson_writer *writer)
{
	/* For multi-shard operations like create index that uses multiple connections per node, we could see multiple entries in citus_stat_activity with same global_pid but different pids on same node. We should check for global_pid here */
	StringInfo str = makeStringInfo();

	/* NULLIF(blocks_total) ensures that "Progress" is NULL if blocks_total is 0 so we don't get div by zero errors and row_get_bson skips the field. */
	appendStringInfo(str,
					 "WITH c1 AS (SELECT phase, blocks_done, blocks_total, (blocks_done * 100.0 / NULLIF(blocks_total, 0)) AS \"Progress\", "
					 " tuples_done AS \"documents_done\", tuples_total AS \"documents_total\", ");

	if (DefaultInlineWriteOperations)
	{
		/* Match the distributed set up to say a single node has a global pid of node 1 + PID (Similar to citus logic) */
		appendStringInfo(str,
						 " (10000000000 + current_locker_pid)::int8 AS AS \"Waiting on op_prefix\""
						 " FROM pg_stat_progress_create_index WHERE (10000000000 + current_locker_pid)::int8 = $1), ");
	}
	else
	{
		appendStringInfo(str,
						 " pg_catalog.citus_calculate_gpid(pg_catalog.citus_nodeid_for_gpid($1), current_locker_pid::integer) AS \"Waiting on op_prefix\""
						 " FROM pg_stat_progress_create_index WHERE pid IN (SELECT process_id FROM pg_catalog.get_all_active_transactions() WHERE global_pid = $1)), ");
	}

	appendStringInfo(str,
					 " c2 AS (SELECT %s.row_get_bson(c1) AS document FROM c1) "
					 " SELECT %s.bson_array_agg(c2.document, '') FROM c2", CoreSchemaName,
					 ApiCatalogSchemaName);

	Oid argTypes[1] = { INT8OID };
	Datum argValues[1] = {
		Int64GetDatum(activity->global_pid)
	};
	char argNulls[1] = { ' ' };
	bool readOnly = true;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(str->data, 1, argTypes, argValues,
													   argNulls,
													   readOnly, SPI_OK_SELECT, &isNull);
	if (isNull)
	{
		return "";
	}

	pgbson *resultBson = DatumGetPgBson(result);
	pgbsonelement resultElement;
	bson_iter_t arrayIter;
	PgbsonToSinglePgbsonElement(resultBson, &resultElement);

	if (resultElement.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		/* somehow we didn't get an array */
		return "";
	}

	BsonValueInitIterator(&resultElement.bsonValue, &arrayIter);

	/* Phase */
	StringInfo messageInfo = makeStringInfo();

	pgbson_array_writer progress_elem_writer;
	PgbsonWriterStartArray(writer, "builds", 6, &progress_elem_writer);
	while (bson_iter_next(&arrayIter))
	{
		/* Should be documents */
		bson_iter_t subDocument;
		if (BSON_ITER_HOLDS_DOCUMENT(&arrayIter) &&
			bson_iter_recurse(&arrayIter, &subDocument))
		{
			pgbson_writer singleWriter;
			PgbsonArrayWriterStartDocument(&progress_elem_writer, &singleWriter);
			while (bson_iter_next(&subDocument))
			{
				const char *key = bson_iter_key(&subDocument);
				if (strcmp(key, "phase") == 0)
				{
					uint32_t length;
					const char *phaseString = bson_iter_utf8(&subDocument, &length);
					const char *userString = PhaseToUserMessage(phaseString);
					appendStringInfo(messageInfo, "%s,", userString);
					PgbsonWriterAppendUtf8(&singleWriter, "phase", 5, userString);
				}
				else
				{
					PgbsonWriterAppendValue(&singleWriter, key, strlen(key),
											bson_iter_value(&subDocument));
				}
			}

			PgbsonArrayWriterEndDocument(&progress_elem_writer, &singleWriter);
		}
	}
	PgbsonWriterEndArray(writer, &progress_elem_writer);

	return messageInfo->data;
}


/*
 * Given an activity that's waiting on a lock, gets the process that currently holds that lock.
 */
static void
WriteGlobalPidOfLockingProcess(SingleWorkerActivity *activity, pgbson_writer *writer)
{
	Oid argTypes[2] = { INT8OID, INT8OID };
	Datum argValues[2] = {
		Int64GetDatum(activity->stat_pid), Int64GetDatum(activity->global_pid)
	};
	char argNulls[2] = { ' ', ' ' };
	bool readOnly = true;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(FirstLockingPidQuery, 2, argTypes,
													   argValues,
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
					  ARR_ELEMTYPE(val_array), sizeof(int64), true, TYPALIGN_INT,
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


/*
 * Given an indexSpec, write the index detail
 */
static void
WriteIndexSpec(SingleWorkerActivity *activity,
			   pgbson_writer *commandWriter)
{
	if (activity->indexSpec == NULL)
	{
		return;
	}
	WriteIndexSpecAsCurrentOpCommand(commandWriter, activity->processedMongoDatabase,
									 activity->processedMongoCollection,
									 activity->indexSpec);
}


static pgbson *
BuildAggregateSpecFromCommandSpec(pgbson *commandSpec, pgbson **filterSpec)
{
	*filterSpec = NULL;
	bool allOps = false;
	bool hasFilter = false;
	bson_iter_t commandIter;
	PgbsonInitIterator(commandSpec, &commandIter);
	pgbson_writer filterWriter;
	PgbsonWriterInit(&filterWriter);
	while (bson_iter_next(&commandIter))
	{
		const char *key = bson_iter_key(&commandIter);
		if (strcmp(key, "currentOp") == 0)
		{
			continue;
		}
		else if (strcmp(key, "$all") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("$all", &commandIter);
			allOps = BsonValueAsBool(bson_iter_value(&commandIter));
		}
		else if (strcmp(key, "$ownOps") == 0)
		{
			EnsureTopLevelFieldIsBooleanLike("$ownOps", &commandIter);
		}
		else if (IsCommonSpecIgnoredField(key))
		{
			/* Ignore these */
		}
		else
		{
			/* Treat other field sas a filter */
			hasFilter = true;
			PgbsonWriterAppendValue(&filterWriter, key, strlen(key), bson_iter_value(
										&commandIter));
		}
	}

	if (hasFilter)
	{
		*filterSpec = PgbsonWriterGetPgbson(&filterWriter);
	}

	/* TODO: Handle ownOps */
	pgbson_writer specWriter;
	PgbsonWriterInit(&specWriter);
	PgbsonWriterAppendBool(&specWriter, "allUsers", 8, allOps);
	PgbsonWriterAppendBool(&specWriter, "idleConnections", 15, true);
	PgbsonWriterAppendBool(&specWriter, "idleSessions", 12, true);

	return PgbsonWriterGetPgbson(&specWriter);
}
