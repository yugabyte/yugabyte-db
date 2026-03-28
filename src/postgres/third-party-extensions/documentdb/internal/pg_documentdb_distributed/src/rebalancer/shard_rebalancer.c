/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/rebalancer/shard_rebalancer.c
 *
 * Implementation of a set of APIs for cluster rebalancing
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
#include "utils/typcache.h"
#include "parser/parse_type.h"
#include "nodes/makefuncs.h"

#include "io/bson_core.h"
#include "utils/documentdb_errors.h"
#include "utils/query_utils.h"
#include "commands/parse_error.h"
#include "metadata/metadata_cache.h"
#include "api_hooks.h"

extern bool EnableShardRebalancer;

PG_FUNCTION_INFO_V1(command_rebalancer_status);
PG_FUNCTION_INFO_V1(command_rebalancer_start);
PG_FUNCTION_INFO_V1(command_rebalancer_stop);


static void PopulateRebalancerRowsFromResponse(pgbson_writer *responseWriter,
											   pgbson *result);

static bool HasActiveRebalancing(void);
static char * GetRebalancerStrategy(pgbson *startArgs);

Datum
command_rebalancer_status(PG_FUNCTION_ARGS)
{
	if (!EnableShardRebalancer)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("rebalancer_status is not supported yet")));
	}

	bool readOnly = true;
	bool isNull = false;
	Datum result = ExtensionExecuteQueryViaSPI(
		FormatSqlQuery(
			"WITH r1 AS (SELECT jsonb_build_object("
			"  'state', state::text,"
			"  'startedAt', started_at::text,"
			"  'finishedAt', finished_at::text,"
			"  'task_state_counts', details->'task_state_counts',"
			"  'tasks', (SELECT jsonb_agg(task - 'LSN' - 'command' - 'hosts' - 'task_id') FROM jsonb_array_elements(details->'tasks') AS task)) AS obj"
			" FROM citus_rebalance_status()),"
			" r2 AS (SELECT jsonb_build_object('rows', jsonb_agg(r1.obj)) AS obj FROM r1)"
			" SELECT %s.bson_json_to_bson(r2.obj::text) FROM r2",
			CoreSchemaName), readOnly, SPI_OK_SELECT, &isNull);

	pgbson_writer responseWriter;
	PgbsonWriterInit(&responseWriter);
	if (isNull)
	{
		/* No results from rebalancer */
		PgbsonWriterAppendUtf8(&responseWriter, "mode", 4, "off");
	}
	else
	{
		PopulateRebalancerRowsFromResponse(&responseWriter, DatumGetPgBson(result));
	}

	result = ExtensionExecuteQueryViaSPI(
		FormatSqlQuery(
			"WITH r1 AS (SELECT jsonb_build_object('strategy_name', name, 'isDefault', default_strategy) AS obj FROM pg_dist_rebalance_strategy),"
			" r2 AS (SELECT jsonb_build_object('strategies', jsonb_agg(r1.obj)) AS obj FROM r1)"
			" SELECT %s.bson_json_to_bson(r2.obj::text) FROM r2",
			CoreSchemaName), readOnly, SPI_OK_SELECT, &isNull);

	if (!isNull)
	{
		PgbsonWriterConcat(&responseWriter, DatumGetPgBson(result));
	}

	PgbsonWriterAppendDouble(&responseWriter, "ok", 2, 1);

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&responseWriter));
}


Datum
command_rebalancer_start(PG_FUNCTION_ARGS)
{
	if (!EnableShardRebalancer)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("starting the shard rebalancer is not supported yet")));
	}

	if (IsChangeStreamFeatureAvailableAndCompatible())
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg(
							"starting the shard rebalancer is not supported when change streams is enabled")));
	}

	pgbson *startArgs = PG_GETARG_PGBSON(0);
	if (HasActiveRebalancing())
	{
		ereport(ERROR, (errcode(
							ERRCODE_DOCUMENTDB_BACKGROUNDOPERATIONINPROGRESSFORNAMESPACE),
						errmsg(
							"Cannot start rebalancing when another rebalancing is in progress")));
	}

	char *rebalancerStrategyName = GetRebalancerStrategy(startArgs);

	bool readOnly = false;
	bool isNull = false;
	if (rebalancerStrategyName != NULL)
	{
		Oid argTypes[1] = { TEXTOID };
		Datum argValues[1] = { CStringGetTextDatum(rebalancerStrategyName) };
		ExtensionExecuteQueryWithArgsViaSPI(
			"SELECT citus_set_default_rebalance_strategy($1)", 1,
			argTypes, argValues, NULL, readOnly,
			SPI_OK_SELECT, &isNull);
	}

	ExtensionExecuteQueryViaSPI("SELECT citus_rebalance_start()", readOnly, SPI_OK_SELECT,
								&isNull);

	pgbson_writer responseWriter;
	PgbsonWriterInit(&responseWriter);
	PgbsonWriterAppendDouble(&responseWriter, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&responseWriter));
}


Datum
command_rebalancer_stop(PG_FUNCTION_ARGS)
{
	if (!EnableShardRebalancer)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_COMMANDNOTSUPPORTED),
						errmsg("stopping the rebalancer is not supported yet")));
	}

	/* Check there's active jobs */
	bool hasActiveJobs = HasActiveRebalancing();
	if (hasActiveJobs)
	{
		bool readOnly = false;
		bool isNull = false;
		ExtensionExecuteQueryViaSPI("SELECT citus_rebalance_stop()", readOnly,
									SPI_OK_SELECT,
									&isNull);
	}

	pgbson_writer responseWriter;
	PgbsonWriterInit(&responseWriter);
	PgbsonWriterAppendBool(&responseWriter, "wasActive", 9, hasActiveJobs);
	PgbsonWriterAppendDouble(&responseWriter, "ok", 2, 1);
	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&responseWriter));
}


/*
 * Appends a DocumentDB compatible response for the rebalancer status:
 * {
 *    "mode": "off|full",
 *    "runningJobs": [ { job output from citus }],
 *    "otherJobs": [ { job output from citus }],
 * }
 */
static void
PopulateRebalancerRowsFromResponse(pgbson_writer *responseWriter, pgbson *result)
{
	pgbsonelement element;
	PgbsonToSinglePgbsonElement(result, &element);

	if (element.bsonValue.value_type == BSON_TYPE_NULL)
	{
		PgbsonWriterAppendUtf8(responseWriter, "mode", 4, "off");
		return;
	}

	if (element.bsonValue.value_type != BSON_TYPE_ARRAY)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("shard rebalancer response should be an array, not %s",
							   BsonTypeName(element.bsonValue.value_type)),
						errdetail_log(
							"shard rebalancer response should be an array, not %s",
							BsonTypeName(
								element.bsonValue.value_type))));
	}

	bson_iter_t rowsIter;
	BsonValueInitIterator(&element.bsonValue, &rowsIter);

	List *runningJobs = NIL;
	List *otherJobs = NIL;
	while (bson_iter_next(&rowsIter))
	{
		const bson_value_t *arrayValue = bson_iter_value(&rowsIter);
		if (arrayValue->value_type != BSON_TYPE_DOCUMENT)
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg(
								"shard rebalancer array element should be a document, not %s",
								BsonTypeName(
									arrayValue->value_type)),
							errdetail_log(
								"shard rebalancer array element should be a document, not %s",
								BsonTypeName(
									arrayValue->value_type))));
		}

		bson_value_t *arrayValueCopy = palloc(sizeof(bson_value_t));
		*arrayValueCopy = *arrayValue;

		bson_iter_t arrayValueIter;
		BsonValueInitIterator(arrayValue, &arrayValueIter);
		if (bson_iter_find(&arrayValueIter, "state"))
		{
			const char *stateValue = bson_iter_utf8(&arrayValueIter, NULL);
			if (strcmp(stateValue, "scheduled") == 0 ||
				strcmp(stateValue, "running") == 0)
			{
				runningJobs = lappend(runningJobs, arrayValueCopy);
			}
			else
			{
				otherJobs = lappend(otherJobs, arrayValueCopy);
			}
		}
		else
		{
			/* Shove it in other jobs */
			otherJobs = lappend(otherJobs, arrayValueCopy);
		}
	}

	char *status = list_length(runningJobs) > 0 ? "full" : "off";
	PgbsonWriterAppendUtf8(responseWriter, "mode", 4, status);

	pgbson_array_writer jobsWriter;
	PgbsonWriterStartArray(responseWriter, "runningJobs", 11, &jobsWriter);

	ListCell *jobCell;
	foreach(jobCell, runningJobs)
	{
		bson_value_t *value = lfirst(jobCell);
		PgbsonArrayWriterWriteValue(&jobsWriter, value);
	}

	PgbsonWriterEndArray(responseWriter, &jobsWriter);

	PgbsonWriterStartArray(responseWriter, "otherJobs", 9, &jobsWriter);

	foreach(jobCell, otherJobs)
	{
		bson_value_t *value = lfirst(jobCell);
		PgbsonArrayWriterWriteValue(&jobsWriter, value);
	}

	PgbsonWriterEndArray(responseWriter, &jobsWriter);
}


static bool
HasActiveRebalancing(void)
{
	bool readOnly = true;
	bool isNull = false;
	Datum result = ExtensionExecuteQueryViaSPI(
		"SELECT COUNT(*)::int4 FROM citus_rebalance_status() WHERE state::text IN ('scheduled', 'running', 'cancelling', 'failing')",
		readOnly, SPI_OK_SELECT, &isNull);

	return !isNull && DatumGetInt32(result) > 0;
}


static char *
GetRebalancerStrategy(pgbson *startArgs)
{
	bson_iter_t argsIter;
	PgbsonInitIterator(startArgs, &argsIter);
	while (bson_iter_next(&argsIter))
	{
		const char *key = bson_iter_key(&argsIter);
		if (strcmp(key, "strategy") == 0)
		{
			EnsureTopLevelFieldType("strategy", &argsIter, BSON_TYPE_UTF8);
			return bson_iter_dup_utf8(&argsIter, NULL);
		}
	}

	return NULL;
}
