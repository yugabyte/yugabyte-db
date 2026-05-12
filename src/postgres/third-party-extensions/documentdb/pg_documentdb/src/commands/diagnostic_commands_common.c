/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/diagnostic_commands_common.c
 *
 *-------------------------------------------------------------------------
 */
#include <math.h>
#include <postgres.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <funcapi.h>
#include <utils/builtins.h>
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <access/xact.h>
#include <utils/lsyscache.h>

#include "metadata/collection.h"
#include "metadata/index.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "planner/documentdb_planner.h"
#include "utils/hashset_utils.h"
#include "utils/version_utils.h"
#include "commands/parse_error.h"
#include "commands/commands_common.h"
#include "commands/diagnostic_commands_common.h"
#include "utils/error_utils.h"
#include "utils/documentdb_errors.h"


extern bool ForceRunDiagnosticCommandInline;

PG_FUNCTION_INFO_V1(command_node_worker);


Datum
command_node_worker(PG_FUNCTION_ARGS)
{
	Oid localFunctionOid = PG_GETARG_OID(0);
	pgbson *localFunctionArg = PG_GETARG_PGBSON(1);
	Oid currentTableOid = PG_GETARG_OID(2);
	ArrayType *chosenTablesArray = PG_GETARG_ARRAYTYPE_P(3);
	bool tablesQualified = PG_GETARG_BOOL(4);

	if (currentTableOid == InvalidOid)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Invalid current table passed to command_node_worker")));
	}

	/* Get the name of the current Table */
	char *tableNameString = get_rel_name(currentTableOid);

	if (tablesQualified)
	{
		Oid relNameSpace = get_rel_namespace(currentTableOid);
		char *tableNamespace = get_namespace_name(relNameSpace);

		tableNameString = psprintf("%s.%s", tableNamespace, tableNameString);
	}

	/* Need to build the result */
	const int slice_ndim = 0;
	ArrayMetaState *mState = NULL;
	ArrayIterator tableIterator = array_create_iterator(chosenTablesArray,
														slice_ndim, mState);

	Datum tableDatum;
	bool isNull;
	bool foundDesignatedTable = false;
	while (array_iterate(tableIterator, &tableDatum, &isNull))
	{
		if (isNull)
		{
			continue;
		}

		char *selectedTableName = TextDatumGetCString(tableDatum);
		if (strcmp(tableNameString, selectedTableName) == 0)
		{
			foundDesignatedTable = true;
			break;
		}
	}

	array_free_iterator(tableIterator);

	if (!foundDesignatedTable)
	{
		/* This is not the designated shard, return empty */
		ereport(DEBUG1, (errmsg(
							 "Skipping command_node_worker on table %s since not in chosen shards",
							 tableNameString)));
		PG_RETURN_NULL();
	}

	ereport(DEBUG1, (errmsg(
						 "Executing command_node_worker on table %s",
						 tableNameString)));

	/* On a designated table */
	Datum result = OidFunctionCall1(localFunctionOid,
									PointerGetDatum(localFunctionArg));
	PG_RETURN_DATUM(result);
}


/*
 * Issues a run_command_on_all_nodes to get the currentOp
 * worker data. used in diagnostic query scenarios, and handles
 * failures in retrieving errors from the workers. Callers are still
 * responsible for parsing errors from the bson directly.
 *
 * TODO: Make this a hook somehow.
 */
List *
RunQueryOnAllServerNodes(const char *commandName, Datum *values, Oid *types,
						 int numValues, PGFunction directFunc,
						 const char *nameSpaceName, const char *functionName)
{
	if (DefaultInlineWriteOperations || ForceRunDiagnosticCommandInline)
	{
		FunctionCallInfo fcinfo = palloc(SizeForFunctionCallInfo(numValues));
		Datum result;
		InitFunctionCallInfoData(*fcinfo, NULL, numValues, InvalidOid, NULL, NULL);

		for (int i = 0; i < numValues; i++)
		{
			fcinfo->args[i].value = values[i];
			fcinfo->args[i].isnull = false;
		}

		result = (*directFunc)(fcinfo);

		List *resultList = list_make1(DatumGetPgBson(result));
		pfree(fcinfo);
		return resultList;
	}

	StringInfo cmdStr = makeStringInfo();

	/* Add the query: FORMAT($$ SELECT schema.function(%%L, %%L) $$, $1, $2)" */
	appendStringInfo(cmdStr, "SELECT success, result FROM run_command_on_all_nodes("
							 "FORMAT($$ SELECT %s.%s(", nameSpaceName, functionName);

	/* Add formats for all the args*/
	char *separator = "";
	for (int i = 0; i < numValues; i++)
	{
		appendStringInfo(cmdStr, "%s%%L", separator);
		separator = ",";
	}

	appendStringInfo(cmdStr, ")$$");

	for (int i = 0; i < numValues; i++)
	{
		appendStringInfo(cmdStr, ",$%d", (i + 1));
	}

	appendStringInfo(cmdStr, "))");
	bool readOnly = true;
	List *workerBsons = NIL;
	MemoryContext priorMemoryContext = CurrentMemoryContext;
	SPI_connect();

	Portal workerQueryPortal = SPI_cursor_open_with_args("workerQueryPortal",
														 cmdStr->data,
														 numValues, types, values,
														 NULL, readOnly, 0);
	bool hasData = true;

	while (hasData)
	{
		SPI_cursor_fetch(workerQueryPortal, true, INT_MAX);

		hasData = SPI_processed >= 1;
		if (!hasData)
		{
			break;
		}

		if (SPI_tuptable)
		{
			for (int tupleNumber = 0; tupleNumber < (int) SPI_processed; tupleNumber++)
			{
				bool isNull;
				AttrNumber isSuccessAttr = 1;
				Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
												  SPI_tuptable->tupdesc, isSuccessAttr,
												  &isNull);
				if (isNull)
				{
					continue;
				}

				bool isSuccess = DatumGetBool(resultDatum);

				if (isSuccess)
				{
					AttrNumber resultAttribute = 2;
					resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
												SPI_tuptable->tupdesc, resultAttribute,
												&isNull);
					if (isNull)
					{
						ereport(ERROR, (errmsg(
											"%s worker was successful but returned a result null.",
											commandName)));
					}

					text *resultText = DatumGetTextP(resultDatum);
					char *resultString = text_to_cstring(resultText);

					MemoryContext spiContext = MemoryContextSwitchTo(priorMemoryContext);
					pgbson *bson;
					if (IsBsonHexadecimalString(resultString))
					{
						bson = PgbsonInitFromHexadecimalString(resultString);
					}
					else
					{
						/* It's a json string use json deserialization */
						bson = PgbsonInitFromJson(resultString);
					}

					workerBsons = lappend(workerBsons, bson);
					MemoryContextSwitchTo(spiContext);
				}
				else
				{
					AttrNumber resultAttribute = 2;
					resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
												SPI_tuptable->tupdesc, resultAttribute,
												&isNull);
					if (isNull)
					{
						elog(WARNING,
							 "%s worker was not successful but result returned null.",
							 commandName);
						continue;
					}

					text *resultText = DatumGetTextP(resultDatum);
					const char *workerError = text_to_cstring(resultText);

					StringView errorView = CreateStringViewFromString(workerError);
					StringView connectivityView = CreateStringViewFromString(
						"Unable to establish connection with");
					StringView recoveryErrorView = CreateStringViewFromString(
						"terminating connection due to conflict with recovery");
					StringView recoveryCancelErrorView = CreateStringViewFromString(
						"canceling statement due to conflict with recovery");
					StringView outOfMemoryView = CreateStringViewFromString(
						"out of memory");
					StringView errorStartView = CreateStringViewFromString(
						"ERROR: ");

					if (StringViewStartsWithStringView(&errorView, &errorStartView))
					{
						errorView = StringViewSubstring(&errorView,
														errorStartView.length);
					}

					if (StringViewStartsWithStringView(&errorView, &connectivityView))
					{
						ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
										errmsg(
											"%s on worker failed with connectivity errors",
											commandName),
										errdetail_log(
											"%s on worker failed with an unexpected error: %s",
											commandName, workerError)));
					}
					else if (StringViewStartsWithStringView(&errorView,
															&recoveryErrorView) ||
							 StringViewStartsWithStringView(&errorView,
															&recoveryCancelErrorView))
					{
						ereport(ERROR, (errcode(ERRCODE_T_R_SERIALIZATION_FAILURE),
										errmsg(
											"Worker %s operation failed due to recovery-related errors",
											commandName),
										errdetail_log(
											"Worker %s operation failed due to recovery-related errors: %s",
											commandName, workerError)));
					}
					else if (StringViewStartsWithStringView(&errorView, &outOfMemoryView))
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_EXCEEDEDMEMORYLIMIT),
										errmsg(
											"%s on worker failed with out of memory errors",
											commandName),
										errdetail_log(
											"%s on worker failed with an out of memory error: %s",
											commandName, workerError)));
					}
					else
					{
						ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
										errmsg(
											"%s on worker failed with an unexpected error",
											commandName),
										errdetail_log(
											"%s on worker failed with an unexpected error: %s",
											commandName, workerError)));
					}
				}
			}
		}
		else
		{
			ereport(ERROR, (errmsg("%s worker call tuple table was null.", commandName)));
		}
	}

	SPI_cursor_close(workerQueryPortal);
	SPI_finish();

	return workerBsons;
}


/* To ensure that run_command_in_workers generally has success
 * We run the worker function in a Try/Catch and write out the error so we get
 * a better error experience in the coordinator query.
 */
pgbson *
RunWorkerDiagnosticLogic(pgbson *(*workerFunc)(void *state), void *state)
{
	MemoryContext savedMemoryContext = CurrentMemoryContext;
	ResourceOwner oldOwner = CurrentResourceOwner;

	pgbson *response = NULL;
	BeginInternalSubTransaction(NULL);

	PG_TRY();
	{
		response = workerFunc(state);
		ReleaseCurrentSubTransaction();
		MemoryContextSwitchTo(savedMemoryContext);
		CurrentResourceOwner = oldOwner;
	}
	PG_CATCH();
	{
		MemoryContextSwitchTo(savedMemoryContext);
		ErrorData *errorData = CopyErrorDataAndFlush();

		/* Abort inner transaction */
		RollbackAndReleaseCurrentSubTransaction();

		/* Rollback changes MemoryContext */
		MemoryContextSwitchTo(savedMemoryContext);
		CurrentResourceOwner = oldOwner;

		pgbson_writer writer;
		PgbsonWriterInit(&writer);
		PgbsonWriterAppendInt32(&writer, ErrCodeKey, ErrCodeLength,
								errorData->sqlerrcode);
		PgbsonWriterAppendUtf8(&writer, ErrMsgKey, ErrMsgLength, errorData->message);
		response = PgbsonWriterGetPgbson(&writer);

		FreeErrorData(errorData);
	}
	PG_END_TRY();

	return response;
}
