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

#include "utils/mongo_errors.h"
#include "metadata/collection.h"
#include "metadata/index.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "planner/pgmongo_planner.h"
#include "utils/hashset_utils.h"
#include "utils/version_utils.h"
#include "commands/parse_error.h"
#include "commands/diagnostic_commands_common.h"


/*
 * Issues a run_command_on_all_nodes to get the currentOp
 * worker data. used in diagnostic query scenarios, and handles
 * failures in retrieving errors from the workers. Callers are still
 * responsible for parsing errors from the bson directly.
 */
List *
GetWorkerBsonsFromAllWorkers(const char *query, Datum *paramValues,
							 Oid *types, int numValues,
							 const char *commandName)
{
	bool readOnly = true;
	List *workerBsons = NIL;
	MemoryContext priorMemoryContext = CurrentMemoryContext;
	SPI_connect();

	Portal workerQueryPortal = SPI_cursor_open_with_args("workerQueryPortal", query,
														 numValues, types, paramValues,
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
					elog(WARNING, "%s call to worker failed %s", commandName,
						 text_to_cstring(resultText));
					ereport(ERROR, (errcode(MongoInternalError),
									errmsg("%s on worker failed with an unexpected error",
										   commandName),
									errhint(
										"%s on worker failed with an unexpected error",
										commandName)));
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

		/* Abort the inner transaction */
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
