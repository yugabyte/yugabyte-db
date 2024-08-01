/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/cursors.c
 *
 * Implementation of the cursor based operations.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <funcapi.h>
#include <utils/portal.h>
#include <utils/varlena.h>
#include <executor/spi.h>
#include <tcop/dest.h>
#include <tcop/pquery.h>
#include <tcop/tcopprot.h>
#include <metadata/metadata_cache.h>
#include <io/helio_bson_core.h>
#include <utils/snapmgr.h>
#include <common/hashfn.h>
#include <commands/portalcmds.h>
#include <utils/mongo_errors.h>
#include <commands/cursor_common.h>
#include <commands/cursor_private.h>
#include <utils/hashset_utils.h>
#include "aggregation/bson_aggregation_pipeline.h"
#include "io/bson_set_returning_functions.h"
#include "commands/commands_common.h"


/*
 * Configuration controling what we send as the worker
 * page size (each worker will return around this size in bytes)
 * Used in testing.
 */
extern int32_t MaxWorkerCursorSize;

/*
 * Reason why the current page execution yielded.
 */
typedef enum TerminationReason
{
	/* The cursor execution completed */
	TerminationReason_CursorCompletion = 1,

	/* The cursor batchsize limit was exceeded */
	TerminationReason_BatchSizeLimit = 2,

	/* The cursor batch Item limit was exceeded */
	TerminationReason_BatchItemLimit = 3
} TerminationReason;


/*
 * The entry of a continuation for a given
 * shard.
 */
typedef struct CursorContinuationEntry
{
	/* The shard name string */
	const char *tableName;

	/* The length of the shard name */
	uint32_t tableNameLength;

	/* The TID inside the shard that is the continuation. */
	ItemPointerData continuation;
} CursorContinuationEntry;

/*
 * The entry of a tailable cursor continuation for a given node.
 */
typedef struct TailableCursorContinuationEntry
{
	/* The node Id of the worker */
	int nodeId;

	/* contiunation token for the tailable cursor. */
	const char *continuationToken;
} TailableCursorContinuationEntry;

static void HoldPortal(Portal portal);
static uint32 CursorHashEntryHashFunc(const void *obj, size_t objsize);
static int CursorHashEntryCompareFunc(const void *obj1, const void *obj2,
									  Size objsize);
static pgbson * BuildCursorDocument(HTAB *cursorMap, int numIterations);

static pgbson * SerializeContinuationForWorker(HTAB *cursorMap, int32_t batchSize, bool
											   isTailable);

static void UpdateCursorInContinuationMap(pgbson *continuationValue, HTAB *cursorMap, bool
										  isTailable);

static void UpdateCursorInContinuationMapCore(bson_iter_t *iter, HTAB *cursorMap);
static void UpdateTailableCursorInContinuationMapCore(bson_iter_t *iter,
													  HTAB *cursorMap);

static Portal PlanStreamingQuery(Query *query, Datum parameter, HTAB *cursorMap);

static TerminationReason FetchCursorAndWriteUntilPageOrSize(Portal portal, int32_t
															batchSize,
															pgbson_array_writer *writer,
															uint32_t *accumulatedSize,
															HTAB *cursorMap,
															int32_t *numRowsFetched,
															uint64_t *
															currentAccumulatedSize,
															MemoryContext writerContext,
															bool isTailableCursor);

PG_FUNCTION_INFO_V1(command_enumerate_cursors);
PG_FUNCTION_INFO_V1(command_get_cursor_page);
PG_FUNCTION_INFO_V1(command_get_cursor_page_query);

const char NodeId[] = "nodeId";
uint32_t NodeIdLength = 7;
const char ContinuationToken[] = "continuationToken";
uint32_t ContiunationTokenLength = 16;

#define PATH_AND_PATH_LEN(path) path, sizeof(path) - 1

/*
 * command_enumerate_cursors implements
 * enumeration of a cursor page for a transactional
 * cursor.
 */
Datum
command_enumerate_cursors(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errmsg("enumerate_cursors is deprecated. Unexpected")));
}


/*
 * Gets the next page of the cursors based on
 * the page size, the backend cursor that got created already.
 * The page is formatted with the mongo wire protocol response
 * format.
 * The function drains the query up to one of MaxBsonSize (16 MB)
 * or batch size - whichever hits first.
 */
Datum
command_get_cursor_page(PG_FUNCTION_ARGS)
{
	text *cursor = PG_GETARG_TEXT_P(0);
	char *cursorName = text_to_cstring(cursor);
	int32_t batchSize = PG_GETARG_INT32(1);

	text *namespace = PG_GETARG_TEXT_P(2);
	char *namespaceName = text_to_cstring(namespace);

	int64_t cursorId = PG_GETARG_INT64(3);
	bool isFirstPage = PG_GETARG_BOOL(4);

	bool queryFullyDrained = false;

	/* Save the context before doing SPI */
	MemoryContext currentContext = CurrentMemoryContext;
	pgbson_writer writer;
	pgbson_writer cursorDoc;
	pgbson_array_writer arrayWriter;

	/* min bson size is 5 (see IsPgbsonEmptyDocument) */
	uint32_t accumulatedLength = 5;

	/* Write the preamble for the cursor response */
	SetupCursorPagePreamble(&writer, &cursorDoc, &arrayWriter,
							cursorId, namespaceName, isFirstPage,
							&accumulatedLength);
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	/* Find the portal (if it exists) */
	Portal portal = SPI_cursor_find(cursorName);
	if (portal == NULL)
	{
		ereport(ERROR, (errcode(MongoCursorNotFound),
						errmsg("Cursor not found in the store.")));
	}

	if (portal->tupDesc->natts > 1)
	{
		ereport(ERROR, (errmsg(
							"Cursor return more than 1 column not supported. This is a bug")));
	}

	if (portal->tupDesc->attrs[0].atttypid != BsonTypeId())
	{
		ereport(ERROR, (errmsg(
							"Cursor return cannot be anything other than Bson. This is a bug")));
	}

	/* Drain the cursor and fetch the next page based on batchSize provided. */
	HTAB *cursorMap = NULL;
	int32_t numRowsFetched = 0;
	uint64_t currentAccumulatedSize = 0;
	bool isTailableCursor = false;
	TerminationReason reason = FetchCursorAndWriteUntilPageOrSize(portal, batchSize,
																  &arrayWriter,
																  &accumulatedLength,
																  cursorMap,
																  &numRowsFetched,
																  &currentAccumulatedSize,
																  currentContext,
																  isTailableCursor);
	queryFullyDrained = reason == TerminationReason_CursorCompletion;

	SPI_finish();

	/* Initialize the continuation (NULL if drained, {} otherwise) */
	pgbson *continuation = queryFullyDrained ? NULL : PgbsonInitEmpty();

	/* Write the tail of the response ("ok": 1 ) etc. */
	bool persistConnection = false;
	return PostProcessCursorPage(fcinfo, &cursorDoc, &arrayWriter, &writer, cursorId,
								 continuation, persistConnection);
}


/*
 * Gets the next page of the query based on
 * the page size, the query and continuation for a streaming query.
 * The page is formatted with the mongo wire protocol response
 * format.
 * The function drains the query up to one of MaxBsonSize (16 MB)
 * or batch size - whichever hits first.
 * Returns the page and an updated continuation token.
 */
Datum
command_get_cursor_page_query(PG_FUNCTION_ARGS)
{
	text *query = PG_GETARG_TEXT_P(0);
	char *queryText = text_to_cstring(query);
	int32_t batchSize = PG_GETARG_INT32(1);

	text *namespace = PG_GETARG_TEXT_P(2);
	char *namespaceName = text_to_cstring(namespace);

	int64_t cursorId = PG_GETARG_INT64(3);
	bool isFirstPage = PG_GETARG_BOOL(4);

	/* Gets the index into the VARARGS where the continuation token exists. */
	int32_t continuationIndex = PG_GETARG_INT32(5);

	Datum *args;
	bool *nulls;
	Oid *types;

	/* fetch argument values to build the object */
	bool convertUnknown = false;
	int nargs = extract_variadic_args(fcinfo, 6, convertUnknown, &args, &types, &nulls);

	/* Set up nulls for SPI */
	char *nullValues = palloc(sizeof(char) * nargs);
	for (int i = 0; i < nargs; i++)
	{
		nullValues[i] = nulls[i] ? 'n' : ' ';
	}

	bool isSinglePage = cursorId == 0;

	/* Validate the continuation token data for multi-page queries */
	if (!isSinglePage)
	{
		if (continuationIndex >= nargs || continuationIndex < 0)
		{
			ereport(ERROR, (errmsg(
								"Continuation index is invalid: %d must be less than %d",
								continuationIndex, nargs)));
		}
		else if (nulls[continuationIndex] ||
				 (types[continuationIndex] != BsonTypeId() &&
				  types[continuationIndex] != BYTEAOID))
		{
			ereport(ERROR, (errmsg(
								"Continuation data is invalid in parameters: index %d in args %d, is null %d, Oid=%u",
								continuationIndex, nargs, nulls[continuationIndex],
								types[continuationIndex])));
		}
	}

	pgbson_writer writer;
	pgbson_writer cursorDoc;
	pgbson_array_writer arrayWriter;

	/* min bson size is 5 (see IsPgbsonEmptyDocument) */
	uint32_t accumulatedSize = 5;

	/* Write the preamble of the cursor page. */
	SetupCursorPagePreamble(&writer, &cursorDoc, &arrayWriter, cursorId, namespaceName,
							isFirstPage, &accumulatedSize);

	/* Set up the continuation map based on the continuation token provided.
	 * This will be updated per row as the query is drained.
	 */
	HTAB *cursorMap = NULL;

	if (!isSinglePage)
	{
		cursorMap = CreateCursorHashSet();
		pgbson *continuationValue = DatumGetPgBson(args[continuationIndex]);
		BuildContinuationMap(continuationValue, cursorMap);
	}

	int32_t numIterations = 0;
	bool queryFullyDrained = false;
	int32_t accumulatedRows = 0;

	/* Save the context before doing SPI */
	MemoryContext currentContext = CurrentMemoryContext;
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	/* Prepare the query once across multiple iterations */
	SPIPlanPtr queryPlan = SPI_prepare_cursor(queryText, nargs, types,
											  CURSOR_OPT_NO_SCROLL | CURSOR_OPT_BINARY);
	while (true)
	{
		/* Serialize the continuation to be passed to the workers */
		if (cursorMap != NULL)
		{
			bool isTailableCursor = false;
			args[continuationIndex] = PointerGetDatum(SerializeContinuationForWorker(
														  cursorMap, batchSize,
														  isTailableCursor));
		}

		/* Create a new portal for the streaming cursor - this will be removed
		 * At the end of this method. This is okay since on failure the TXN gets rolled back
		 * and with it, the cursor.
		 */
		bool readOnly = true;
		Portal queryPortal = SPI_cursor_open("myStreamingCursor", queryPlan, args,
											 nullValues, readOnly);
		if (cursorMap != NULL)
		{
			if (queryPortal->tupDesc->natts != 2)
			{
				ereport(ERROR, (errmsg(
									"Cursor return more than 2 column not supported: Found %d. This is a bug",
									queryPortal->tupDesc->natts)));
			}

			if (queryPortal->tupDesc->attrs[0].atttypid != BsonTypeId() ||
				queryPortal->tupDesc->attrs[1].atttypid != BsonTypeId())
			{
				ereport(ERROR, (errmsg(
									"Cursor return cannot be anything other than Bson. This is a bug")));
			}
		}
		else
		{
			if (queryPortal->tupDesc->natts < 1)
			{
				ereport(ERROR, (errmsg(
									"Cursor returning less than 1 column not supported. This is a bug")));
			}

			if (queryPortal->tupDesc->attrs[0].atttypid != BsonTypeId())
			{
				ereport(ERROR, (errmsg(
									"Cursor return cannot be anything other than Bson. This is a bug")));
			}
		}

		/* Drain the cursor and fetch the next page based on batchSize provided. */
		uint64_t currentAccumulatedSize = 0;
		bool isTailableCursor = false;
		TerminationReason reason = FetchCursorAndWriteUntilPageOrSize(
			queryPortal, batchSize, &arrayWriter, &accumulatedSize, cursorMap,
			&accumulatedRows, &currentAccumulatedSize, currentContext, isTailableCursor);

		/* Close the portal since the current page is retrieved. */
		SPI_cursor_close(queryPortal);

		numIterations++;

		if (cursorMap == NULL)
		{
			queryFullyDrained = reason == TerminationReason_CursorCompletion;
			break;
		}
		else if (reason == TerminationReason_CursorCompletion)
		{
			/*
			 * ValidateCursorCustomScanPlan ensures that we're the top most
			 * plan in the worker. Therefore, we can safely assume that
			 * we're correctly tracking sizes in the worker. Consequently,
			 * if we terminated with less than WorkerSize, we know it's
			 * a pure cursor termination. This way we avoid an additional
			 * round trip to find out whether it's actually drained.
			 */
			if (currentAccumulatedSize < (uint64_t) MaxWorkerCursorSize)
			{
				queryFullyDrained = true;
				break;
			}
		}
		else
		{
			/* We terminated because of size or batchSize limits */
			break;
		}
	}

	SPI_freeplan(queryPlan);

	SPI_finish();

	/* Initialize the continuation (NULL if drained, { "continuation": [ { "shard": "value" ... } ] } otherwise) */
	pgbson *continuation = (queryFullyDrained || isSinglePage) ? NULL :
						   BuildCursorDocument(cursorMap, numIterations);

	/* Write the tail */
	bool persistConnection = false;
	return PostProcessCursorPage(fcinfo, &cursorDoc, &arrayWriter, &writer, cursorId,
								 continuation, persistConnection);
}


/*
 * Executes a query that returns a single row (e.g. Count/Distinct)
 * And returns the first datum.
 */
pgbson *
DrainSingleResultQuery(Query *query)
{
	/* Create a cursor for this iteration */
	int cursorOptions = CURSOR_OPT_NO_SCROLL | CURSOR_OPT_BINARY;
	Portal queryPortal = CreateNewPortal();
	queryPortal->visible = false;
	queryPortal->cursorOptions = cursorOptions;

	ParamListInfo paramListInfo = NULL;
	PlannedStmt *queryPlan = pg_plan_query(query, NULL, cursorOptions,
										   paramListInfo);

	/* Set the plan in the cursor for this iteration */
	PortalDefineQuery(queryPortal, NULL, "",
					  CMDTAG_SELECT,
					  list_make1(queryPlan),
					  NULL);

	/* Trigger execution (Start the ExecEngine etc.) */
	PortalStart(queryPortal, paramListInfo, 0, GetActiveSnapshot());

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	SPI_cursor_fetch(queryPortal, true, 1);

	if (SPI_processed == 0)
	{
		return NULL;
	}


	bool isNull = false;
	int tupleNumber = 0;
	AttrNumber attrNumber = 1;
	Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
									  SPI_tuptable->tupdesc, attrNumber,
									  &isNull);

	if (isNull)
	{
		return NULL;
	}

	Form_pg_attribute attr = TupleDescAttr(SPI_tuptable->tupdesc, attrNumber - 1);
	Datum retDatum = SPI_datumTransfer(resultDatum, attr->attbyval, attr->attlen);

	SPI_cursor_close(queryPortal);
	SPI_finish();

	return DatumGetPgBson(retDatum);
}


/*
 * Drain a streaming query by planning the query fetch results using a
 * cursor and then drain the cursor until the page size/batch size
 * or the cursor is fully drained.
 */
bool
DrainStreamingQuery(HTAB *cursorMap, Query *query, int batchSize,
					int32_t *numIterations, uint32_t accumulatedSize,
					pgbson_array_writer *arrayWriter)
{
	bool queryFullyDrained = false;
	int32_t accumulatedRows = 0;
	bool isTailableCursor = false;

	MemoryContext currentContext = CurrentMemoryContext;
	while (true)
	{
		Datum continuationParam = (Datum) 0;
		if (cursorMap != NULL)
		{
			pgbson *continuation = SerializeContinuationForWorker(cursorMap, batchSize,
																  isTailableCursor);
			continuationParam = PointerGetDatum(continuation);
		}
		Portal queryPortal = PlanStreamingQuery(query, continuationParam, cursorMap);

		/* Drain the cursor and fetch the next page based on batchSize provided. */
		uint64_t currentAccumulatedSize = 0;
		TerminationReason reason = FetchCursorAndWriteUntilPageOrSize(
			queryPortal, batchSize, arrayWriter, &accumulatedSize, cursorMap,
			&accumulatedRows, &currentAccumulatedSize, currentContext, isTailableCursor);

		/* Close the portal since the current page is retrieved. */
		SPI_cursor_close(queryPortal);

		SPI_finish();

		(*numIterations)++;

		if (cursorMap == NULL)
		{
			queryFullyDrained = reason == TerminationReason_CursorCompletion;
			break;
		}
		else if (reason == TerminationReason_CursorCompletion)
		{
			/*
			 * ValidateCursorCustomScanPlan ensures that we're the top most
			 * plan in the worker. Therefore, we can safely assume that
			 * we're correctly tracking sizes in the worker. Consequently,
			 * if we terminated with less than WorkerSize, we know it's
			 * a pure cursor termination. This way we avoid an additional
			 * round trip to find out whether it's actually drained.
			 */
			if (currentAccumulatedSize < (uint64_t) MaxWorkerCursorSize)
			{
				queryFullyDrained = true;
				break;
			}
		}
		else
		{
			/* We terminated because of size or batchSize limits */
			break;
		}
	}

	return queryFullyDrained;
}


/*
 * Drain a tailable query by planning and executing the query and fetch the results
 * using a cursor and then drain the cursor until there are no more events available
 * currently or until the page size/batch size is reached. Note that the cursor is
 * never fully drained for a tailable cursor.
 */
bool
DrainTailableQuery(HTAB *cursorMap, Query *query, int batchSize,
				   int32_t *numIterations, uint32_t accumulatedSize,
				   pgbson_array_writer *arrayWriter)
{
	/* For a tailable cursor the query is never fully drained. */
	bool queryFullyDrained = false;
	int32_t accumulatedRows = 0;
	bool isTailableCursor = true;

	Datum continuationParam = (Datum) 0;
	if (cursorMap != NULL)
	{
		pgbson *continuation = SerializeContinuationForWorker(cursorMap, batchSize,
															  isTailableCursor);
		continuationParam = PointerGetDatum(continuation);
	}
	MemoryContext currentContext = CurrentMemoryContext;

	/* Plan the streaming query for the tailable cursor. */
	Portal queryPortal = PlanStreamingQuery(query, continuationParam, cursorMap);

	/* Drain the cursor and fetch the next page based on batchSize provided. */
	uint64_t currentAccumulatedSize = 0;

	FetchCursorAndWriteUntilPageOrSize(queryPortal,
									   batchSize,
									   arrayWriter,
									   &accumulatedSize,
									   cursorMap,
									   &accumulatedRows,
									   &
									   currentAccumulatedSize,
									   currentContext,
									   isTailableCursor);

	/* Close the portal since the current page is retrieved. */
	SPI_cursor_close(queryPortal);

	SPI_finish();

	(*numIterations)++;

	return queryFullyDrained;
}


/*
 * Given a query that needs a persistent cursor, creates the portal for that
 * query in-line and then drains it and gets the first page.
 * If "isHoldCursor" is set, marks the cursor as "WITH HOLD": This is true outside of
 * transactions (inside transactions, we bind the cursor lifetime to the x-act).
 * If "closeCursor" is set, the cursor is closed at the end of execution: This is
 * true for single page cursors.
 */
bool
CreateAndDrainPersistedQuery(const char *cursorName, Query *query,
							 int batchSize, int32_t *numIterations, uint32_t
							 accumulatedSize,
							 pgbson_array_writer *arrayWriter, bool isHoldCursor,
							 bool closeCursor)
{
	/* If there's a new with-hold cursor, clean up any old state */
	if (isHoldCursor)
	{
		if (!ThereAreNoReadyPortals())
		{
			elog(NOTICE, "There are open held portals. Closing them");
			PortalHashTableDeleteAll();
		}
	}

	/* Set up cursor flags */
	int cursorOptions = CURSOR_OPT_BINARY;
	if (isHoldCursor && !closeCursor)
	{
		cursorOptions = cursorOptions | CURSOR_OPT_HOLD;
	}

	/* Save the context before doing SPI */
	MemoryContext currentContext = CurrentMemoryContext;

	/* Plan the query */
	ParamListInfo paramList = NULL;
	PlannedStmt *queryPlan = pg_plan_query(query, NULL, cursorOptions, paramList);

	/* Add Scroll if it's explicitly supported (nodeAgg sometimes doesn't support it).
	 * this is similar to spi's SPI_cursor_open_internal
	 */
	if (ExecSupportsBackwardScan(queryPlan->planTree))
	{
		cursorOptions |= CURSOR_OPT_SCROLL;
	}

	/* Create the cursor */
	Portal queryPortal = CreatePortal(cursorName, false, false);
	queryPortal->visible = true;
	queryPortal->cursorOptions = cursorOptions;


	if (query->commandType == CMD_MERGE)
	{
		/* In order to use a portal & SPI in Merge Command we need to set it to true */
		queryPlan->hasReturning = true;
	}
	else if (!closeCursor)
	{
		/* Since this could be holdable, copy the query plan to the portal context  */
		MemoryContextSwitchTo(queryPortal->portalContext);
		queryPlan = copyObject(queryPlan);
		MemoryContextSwitchTo(currentContext);
	}

	/* Set the plan into the portal  */
	PortalDefineQuery(queryPortal, NULL, "",
					  CMDTAG_SELECT,
					  list_make1(queryPlan),
					  NULL);

	/* Start execution */
	PortalStart(queryPortal, paramList, 0, GetActiveSnapshot());

	if (!closeCursor && isHoldCursor)
	{
		HoldPortal(queryPortal);
	}

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	HTAB *cursorMap = NULL;
	int32_t numRowsFetched = 0;
	uint64_t currentAccumulatedSize = 0;
	TerminationReason reason = FetchCursorAndWriteUntilPageOrSize(queryPortal, batchSize,
																  arrayWriter,
																  &accumulatedSize,
																  cursorMap,
																  &numRowsFetched,
																  &currentAccumulatedSize,
																  currentContext, false);
	if (closeCursor || (reason == TerminationReason_CursorCompletion))
	{
		SPI_cursor_close(queryPortal);
	}

	SPI_finish();
	return reason == TerminationReason_CursorCompletion;
}


/*
 * Given a query that is pre-created, Creates a "Portal" for that query
 * and executes that query inline, updating the target writer with the
 * output of the query. This assumes that the query is streamable.
 * The output documents are then written to the array_writer.
 */
static Portal
PlanStreamingQuery(Query *query, Datum parameter, HTAB *cursorMap)
{
	int cursorOptions = CURSOR_OPT_NO_SCROLL | CURSOR_OPT_BINARY;
	ParamListInfo paramListInfo = makeParamList(1);
	paramListInfo->numParams = 1;
	paramListInfo->params[0].isnull = false;
	paramListInfo->params[0].ptype = BsonTypeId();
	paramListInfo->params[0].pflags = PARAM_FLAG_CONST;
	paramListInfo->params[0].value = parameter;

	/* Plan the query */

	/* TODO: We copy the query since the planner might modify it inline.
	 * This can be removed once the Replacement of "CURSOR" with its param
	 * requirement goes away.
	 */
	Query *copiedQuery = copyObject(query);
	PlannedStmt *queryPlan = pg_plan_query(copiedQuery, NULL, cursorOptions,
										   paramListInfo);

	Portal queryPortal = CreateNewPortal();
	queryPortal->visible = false;
	queryPortal->cursorOptions = cursorOptions;

	/* Set the plan in the cursor for this iteration */
	PortalDefineQuery(queryPortal, NULL, "",
					  CMDTAG_SELECT,
					  list_make1(queryPlan),
					  NULL);

	/* Trigger execution (Start the ExecEngine etc.) */
	PortalStart(queryPortal, paramListInfo, 0, GetActiveSnapshot());

	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}


	/* Create a new portal for the streaming cursor - this will be removed
	 * At the end of this method. This is okay since on failure the TXN gets rolled back
	 * and with it, the cursor.
	 */
	if (cursorMap != NULL)
	{
		if (queryPortal->tupDesc->natts != 2)
		{
			ereport(ERROR, (errmsg(
								"Cursor return more than 2 column not supported: Found %d. This is a bug",
								queryPortal->tupDesc->natts)));
		}

		if (queryPortal->tupDesc->attrs[0].atttypid != BsonTypeId() ||
			queryPortal->tupDesc->attrs[1].atttypid != BsonTypeId())
		{
			ereport(ERROR, (errmsg(
								"Cursor return cannot be anything other than Bson. This is a bug")));
		}
	}
	else
	{
		if (queryPortal->tupDesc->natts < 1)
		{
			ereport(ERROR, (errmsg(
								"Cursor returning less than 1 column not supported. This is a bug")));
		}

		if (queryPortal->tupDesc->attrs[0].atttypid != BsonTypeId())
		{
			ereport(ERROR, (errmsg(
								"Cursor return cannot be anything other than Bson. This is a bug")));
		}
	}
	return queryPortal;
}


/*
 * This is a copy of HoldPortal in portalmem.c
 * Essentially we have this called in Commit at the outer end of the request
 * However, since Citus doesn't have a ReScan implementation, if we have partially
 * scanned, when it rewinds, it simply keeps its position. This means we end up
 * missing rows that are part of the  query. By holding the portal up-front, we
 * make sure that all the rows are in our HoldStore and then we enumerate it.
 */
static void
HoldPortal(Portal portal)
{
	/*
	 * Note that PersistHoldablePortal() must release all resources used by
	 * the portal that are local to the creating transaction.
	 */
	PortalCreateHoldStore(portal);
	PersistHoldablePortal(portal);

	/* drop cached plan reference, if any */
	if (portal->cplan)
	{
		ReleaseCachedPlan(portal->cplan, NULL);
		portal->cplan = NULL;

		/*
		 * We must also clear portal->stmts which is now a dangling reference
		 * to the cached plan's plan list.  This protects any code that might
		 * try to examine the Portal later.
		 */
		portal->stmts = NIL;
	}

	/*
	 * Any resources belonging to the portal will be released in the upcoming
	 * transaction-wide cleanup; the portal will no longer have its own
	 * resources.
	 */
	portal->resowner = NULL;

	/*
	 * Having successfully exported the holdable cursor, mark it as not
	 * belonging to this transaction.
	 */
	portal->createSubid = InvalidSubTransactionId;
	portal->activeSubid = InvalidSubTransactionId;
	portal->createLevel = 0;
}


/*
 * Given a prior cursor that was already created, drains the next page
 * of documents from the cursor.
 */
bool
DrainPersistedCursor(const char *cursorName, int batchSize,
					 int32_t *numIterations, uint32_t accumulatedSize,
					 pgbson_array_writer *arrayWriter)
{
	/* Save the context before doing SPI */
	MemoryContext currentContext = CurrentMemoryContext;
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	/* Open the cursor once across multiple iterations */
	Portal portal = SPI_cursor_find(cursorName);

	if (portal == NULL)
	{
		ereport(ERROR, (errcode(MongoCursorNotFound),
						errmsg("Cursor not found in the store.")));
	}

	HTAB *cursorMap = NULL;
	int32_t numRowsFetched = 0;
	uint64_t currentAccumulatedSize = 0;
	TerminationReason reason = FetchCursorAndWriteUntilPageOrSize(portal, batchSize,
																  arrayWriter,
																  &accumulatedSize,
																  cursorMap,
																  &numRowsFetched,
																  &currentAccumulatedSize,
																  currentContext,
																  false);
	if (reason == TerminationReason_CursorCompletion)
	{
		SPI_cursor_close(portal);
	}
	SPI_finish();
	return reason == TerminationReason_CursorCompletion;
}


/*
 * Fetches the next page from the given cursor up until MaxBsonSize
 * or batchSize and writes it to the cursor bson array. Also updates
 * the cursorMap if provided (it's a streaming cursor).
 */
static TerminationReason
FetchCursorAndWriteUntilPageOrSize(Portal portal, int32_t batchSize,
								   pgbson_array_writer *writer,
								   uint32_t *accumulatedSize,
								   HTAB *cursorMap,
								   int32_t *numRowsFetched,
								   uint64_t *currentAccumulatedSize,
								   MemoryContext writerContext,
								   bool isTailableCursor)
{
	/* BatchSize = 0 means we don't actually move the cursor forward. */
	if (batchSize == 0)
	{
		return TerminationReason_BatchItemLimit;
	}

	/* If the cursor has never been enumerated, fetch once when we start
	 * Otherwise, we bailed on this cursor due to size/batch limits which
	 * means it's already positioned to the current row.
	 */
	bool shouldFetch = portal->portalPos == 0;
	while (true)
	{
		/* move forward 1. */
		if (shouldFetch)
		{
			SPI_cursor_fetch(portal, true, 1);
		}
		else
		{
			/* Refetch current row */
			SPI_cursor_fetch(portal, true, 0);
		}

		bool hasMore = SPI_processed >= 1;
		if (!hasMore)
		{
			return TerminationReason_CursorCompletion;
		}

		shouldFetch = true;
		if (SPI_tuptable && SPI_tuptable->tupdesc->natts >= 1)
		{
			bool isNull = false;
			int tupleNumber = 0;
			AttrNumber attrNumber = 1;
			Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											  SPI_tuptable->tupdesc, attrNumber,
											  &isNull);
			pgbson *documentValue = NULL;
			uint32_t datumSize = 0;
			if (isNull)
			{
				/*
				 * For tailable cursors, we can have NULL documents, so process the continuation.
				 * For non-tailable cursors, skip this NULL document and continuation.
				 */
				if (!isTailableCursor)
				{
					continue;
				}
			}
			else
			{
				documentValue = DatumGetPgBsonPacked(resultDatum);
				datumSize = VARSIZE_ANY_EXHDR(documentValue);

				/* if the new total size is > Max Bson Size */
				if (datumSize > BSON_MAX_ALLOWED_SIZE)
				{
					ereport(ERROR, (errcode(MongoBsonObjectTooLarge),
									errmsg("Size %u is larger than MaxDocumentSize %u",
										   datumSize, BSON_MAX_ALLOWED_SIZE)));
				}

				*currentAccumulatedSize += datumSize;

				/* this is the overhead of the array index (The string "1", "2" etc). */
				/* we use a simple const of 9 digits as 16 MB in bytes has 8 digits, so */
				/* realistically we won't have more than 16,777,216 entries with trailing 0. */
				const int perDocOverhead = 9;
				int64_t totalSize = *accumulatedSize + datumSize + perDocOverhead;

				/*
				 * Allow at least one document to get through for the size limit - this accounts for
				 * ensuring that 1 16 MB doc can be returned per response.
				 */
				bool sizeLimitReached = (totalSize >= BSON_MAX_ALLOWED_SIZE &&
										 *numRowsFetched > 0);
				if (sizeLimitReached || *numRowsFetched >= batchSize)
				{
					/* we've exceeded the budget - bail. */
					return sizeLimitReached ?
						   TerminationReason_BatchSizeLimit :
						   TerminationReason_BatchItemLimit;
				}

				(*numRowsFetched)++;
			}

			/* Fetch continuation if it exists */
			pgbson *continuation = NULL;
			if (cursorMap != NULL && SPI_tuptable->tupdesc->natts >= 2)
			{
				bool isContinuationNull = false;
				AttrNumber continuationAttribute = 2;
				Datum continuationDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
														SPI_tuptable->tupdesc,
														continuationAttribute,
														&isContinuationNull);
				if (!isContinuationNull)
				{
					continuation = DatumGetPgBsonPacked(continuationDatum);
				}
			}

			/* copy and insert the tuple */
			MemoryContext spiContext = MemoryContextSwitchTo(writerContext);
			if (documentValue != NULL)
			{
				PgbsonArrayWriterWriteDocument(writer, documentValue);
			}

			/* Update the continuation map in the original context if available */
			if (continuation != NULL)
			{
				UpdateCursorInContinuationMap(continuation, cursorMap, isTailableCursor);
			}

			MemoryContextSwitchTo(spiContext);
			*accumulatedSize += datumSize + 9;
		}
	}
}


/*
 * Cursor entry is hashed by the table name (Shard name).
 */
static uint32
CursorHashEntryHashFunc(const void *obj, size_t objsize)
{
	const CursorContinuationEntry *hashEntry = obj;
	return hash_bytes((const unsigned char *) hashEntry->tableName,
					  (int) hashEntry->tableNameLength);
}


/*
 * Compare 2 Cursor entries: Compare them by shard name.
 */
static int
CursorHashEntryCompareFunc(const void *obj1, const void *obj2,
						   Size objsize)
{
	const CursorContinuationEntry *hashEntry1 = obj1;
	const CursorContinuationEntry *hashEntry2 = obj2;

	if (hashEntry1->tableNameLength != hashEntry2->tableNameLength)
	{
		return hashEntry1->tableNameLength - hashEntry2->tableNameLength;
	}

	return strcmp(hashEntry1->tableName, hashEntry2->tableName);
}


/*
 * Updates a single shard's cursor document into the cursor map.
 */
static void
UpdateCursorInContinuationMapCore(bson_iter_t *singleContinuationDoc, HTAB *cursorMap)
{
	bson_value_t continuationBinaryValue = { 0 };
	CursorContinuationEntry searchEntry = { 0 };
	while (bson_iter_next(singleContinuationDoc))
	{
		if (strcmp(bson_iter_key(singleContinuationDoc),
				   CursorContinuationTableName) == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(singleContinuationDoc))
			{
				ereport(ERROR, (errmsg("Expecting string value for %s",
									   CursorContinuationTableName)));
			}

			searchEntry.tableName = bson_iter_utf8(singleContinuationDoc,
												   &searchEntry.tableNameLength);
		}
		else if (strcmp(bson_iter_key(singleContinuationDoc),
						CursorContinuationValue) == 0)
		{
			continuationBinaryValue = *bson_iter_value(
				singleContinuationDoc);
		}
	}

	if (continuationBinaryValue.value_type == BSON_TYPE_EOD)
	{
		return;
	}

	if (continuationBinaryValue.value_type != BSON_TYPE_BINARY)
	{
		ereport(ERROR, (errmsg("Expecting binary value for %s, found %s",
							   CursorContinuationValue, BsonTypeName(
								   continuationBinaryValue.value_type))));
	}

	if (continuationBinaryValue.value.v_binary.data_len != sizeof(ItemPointerData))
	{
		ereport(ERROR, (errmsg(
							"Invalid length for binary value %d, expecting %d",
							continuationBinaryValue.value.v_binary.data_len,
							(int) sizeof(ItemPointerData))));
	}

	bool found = false;
	CursorContinuationEntry *hashEntry = hash_search(cursorMap, &searchEntry,
													 HASH_ENTER, &found);
	if (!found)
	{
		/* If a new entry was created, ensure the string table name
		 * is in the target memory context
		 */
		hashEntry->tableName = pnstrdup(hashEntry->tableName, hashEntry->tableNameLength);
	}
	hashEntry->continuation =
		*(ItemPointerData *) continuationBinaryValue.value.v_binary.data;
}


/*
 * Updates a single node's change stream cursor document into the cursor map.
 */
static void
UpdateTailableCursorInContinuationMapCore(bson_iter_t *singleContinuationDoc,
										  HTAB *cursorMap)
{
	uint32 nodeId = 0;
	const char *continuationToken = NULL;

	while (bson_iter_next(singleContinuationDoc))
	{
		const char *key = bson_iter_key(singleContinuationDoc);
		if (strcmp(key, NodeId) == 0)
		{
			if (!BSON_ITER_HOLDS_INT32(singleContinuationDoc))
			{
				ereport(ERROR, (errmsg("Expecting int32 value for %s",
									   NodeId)));
			}
			nodeId = bson_iter_int32(singleContinuationDoc);
		}
		else if (strcmp(key, ContinuationToken) == 0)
		{
			if (!BSON_ITER_HOLDS_UTF8(singleContinuationDoc))
			{
				ereport(ERROR, (errmsg("Expecting UTF8 value for %s",
									   ContinuationToken)));
			}
			uint32_t resumeLSNLength = 0;
			continuationToken = pstrdup(bson_iter_utf8(singleContinuationDoc,
													   &resumeLSNLength));
		}
	}
	bool found = false;
	TailableCursorContinuationEntry *hashEntry =
		hash_search(cursorMap, &nodeId, HASH_ENTER, &found);
	if (!found)
	{
		hashEntry->nodeId = nodeId;
	}
	hashEntry->continuationToken = continuationToken;
}


/*
 * At the beginning of the cursor's execution, takes the serialized pgbson
 * and builds the cursor map with the per shard values.
 */
void
BuildContinuationMap(pgbson *continuationValue, HTAB *cursorMap)
{
	bson_iter_t continuationIterator;
	PgbsonInitIterator(continuationValue, &continuationIterator);
	while (bson_iter_next(&continuationIterator))
	{
		const char *currentField = bson_iter_key(&continuationIterator);

		/* Ignore all other valuesin this stage. */
		if (strcmp(currentField, "continuation") != 0)
		{
			continue;
		}

		bson_iter_t continuationArray;
		if (!BSON_ITER_HOLDS_ARRAY(&continuationIterator) ||
			!bson_iter_recurse(&continuationIterator, &continuationArray))
		{
			ereport(ERROR, (errmsg("continuation must be an array.")));
		}

		while (bson_iter_next(&continuationArray))
		{
			bson_iter_t singleContinuationDoc;
			if (!BSON_ITER_HOLDS_DOCUMENT(&continuationArray) ||
				!bson_iter_recurse(&continuationArray, &singleContinuationDoc))
			{
				ereport(ERROR, (errmsg("continuation element must be a document.")));
			}

			/* Update each shard's value into the map */
			UpdateCursorInContinuationMapCore(&singleContinuationDoc, cursorMap);
		}
	}
}


/*
 * At the beginning of the cursor's execution, takes the serialized pgbson
 * and builds the cursor map for tailable cursror with the per node values.
 */
void
BuildTailableCursorContinuationMap(pgbson *continuationValue, HTAB *cursorMap)
{
	bson_iter_t continuationIterator;
	PgbsonInitIterator(continuationValue, &continuationIterator);
	while (bson_iter_next(&continuationIterator))
	{
		const char *currentField = bson_iter_key(&continuationIterator);

		/* Ignore all other values in this stage. */
		if (strcmp(currentField, "continuation") != 0)
		{
			continue;
		}

		bson_iter_t continuationArray;
		if (!BSON_ITER_HOLDS_ARRAY(&continuationIterator) ||
			!bson_iter_recurse(&continuationIterator, &continuationArray))
		{
			ereport(ERROR, (errmsg("continuation must be an array.")));
		}

		while (bson_iter_next(&continuationArray))
		{
			bson_iter_t singleContinuationDoc;
			if (!BSON_ITER_HOLDS_DOCUMENT(&continuationArray) ||
				!bson_iter_recurse(&continuationArray, &singleContinuationDoc))
			{
				ereport(ERROR, (errmsg("continuation element must be a document.")));
			}

			/* Update the change stream continuation in the map. */
			UpdateTailableCursorInContinuationMapCore(&singleContinuationDoc,
													  cursorMap);
		}
	}
}


/*
 * Update a single row's continuation into into the cursor map after draining
 * a tuple into the response page.
 */
static void
UpdateCursorInContinuationMap(pgbson *continuationValue, HTAB *cursorMap, bool
							  isTailableCursor)
{
	bson_iter_t continuationIter;
	PgbsonInitIterator(continuationValue, &continuationIter);
	if (isTailableCursor)
	{
		UpdateTailableCursorInContinuationMapCore(&continuationIter, cursorMap);
	}
	else
	{
		UpdateCursorInContinuationMapCore(&continuationIter, cursorMap);
	}
}


void
SerializeContinuationsToWriter(pgbson_writer *writer, HTAB *cursorMap)
{
	pgbson_array_writer childWriter;
	PgbsonWriterStartArray(writer, "continuation", -1, &childWriter);

	HASH_SEQ_STATUS hashStatus;
	CursorContinuationEntry *entry;

	hash_seq_init(&hashStatus, cursorMap);
	while ((entry = (CursorContinuationEntry *) hash_seq_search(&hashStatus)) != NULL)
	{
		pgbson_writer entryWriter;
		PgbsonArrayWriterStartDocument(&childWriter, &entryWriter);

		PgbsonWriterAppendUtf8(&entryWriter, CursorContinuationTableName,
							   CursorContinuationTableNameLength, entry->tableName);

		bson_value_t continuationValue;
		continuationValue.value_type = BSON_TYPE_BINARY;
		continuationValue.value.v_binary.subtype = BSON_SUBTYPE_BINARY;
		continuationValue.value.v_binary.data = (uint8_t *) &entry->continuation;
		continuationValue.value.v_binary.data_len = sizeof(ItemPointerData);
		PgbsonWriterAppendValue(&entryWriter, CursorContinuationValue,
								CursorContinuationValueLength,
								&continuationValue);
		PgbsonArrayWriterEndDocument(&childWriter, &entryWriter);
	}

	PgbsonWriterEndArray(writer, &childWriter);
}


void
SerializeTailableContinuationsToWriter(pgbson_writer *writer, HTAB *cursorMap)
{
	pgbson_array_writer childWriter;
	PgbsonWriterStartArray(writer, "continuation", -1, &childWriter);

	HASH_SEQ_STATUS hashStatus;
	TailableCursorContinuationEntry *entry;

	hash_seq_init(&hashStatus, cursorMap);
	while ((entry = (TailableCursorContinuationEntry *) hash_seq_search(
				&hashStatus)) != NULL)
	{
		pgbson_writer entryWriter;
		PgbsonArrayWriterStartDocument(&childWriter, &entryWriter);
		PgbsonWriterAppendInt32(&entryWriter, PATH_AND_PATH_LEN(NodeId),
								entry->nodeId);

		PgbsonWriterAppendUtf8(&entryWriter,
							   PATH_AND_PATH_LEN(ContinuationToken),
							   entry->continuationToken);
		PgbsonArrayWriterEndDocument(&childWriter, &entryWriter);
	}
	PgbsonWriterEndArray(writer, &childWriter);
}


/*
 * Serializes the cursor state into a single pgbson document.
 * Takes the form:
 * { "continuation": [ { "tableName": "<table>", "value": <binaryValue> } ] }
 */
static pgbson *
BuildCursorDocument(HTAB *cursorMap, int numIterations)
{
	pgbson_writer finalWriter;

	PgbsonWriterInit(&finalWriter);
	SerializeContinuationsToWriter(&finalWriter, cursorMap);

	/* In the response add the number of iterations (used in tests) */
	PgbsonWriterAppendInt32(&finalWriter, "numIters", 8, numIterations);
	return PgbsonWriterGetPgbson(&finalWriter);
}


/*
 * Serializes continuation state from the map into a bson that can be sent to the
 * workers. This includes continuation state and page size hints for round trips.
 */
static pgbson *
SerializeContinuationForWorker(HTAB *cursorMap, int32_t batchSize, bool isTailable)
{
	pgbson_writer finalWriter;

	PgbsonWriterInit(&finalWriter);

	if (isTailable)
	{
		SerializeTailableContinuationsToWriter(&finalWriter, cursorMap);
	}
	else
	{
		SerializeContinuationsToWriter(&finalWriter, cursorMap);
	}

	/* double the batch size. */
	batchSize <<= 1;

	/* handle overflow */
	if (batchSize < 0)
	{
		batchSize = INT_MAX;
	}

	/* Write the batchCount and batchSize */
	PgbsonWriterAppendInt32(&finalWriter, "getpage_batchCount", -1, batchSize);
	PgbsonWriterAppendInt32(&finalWriter, "getpage_batchSizeHint", -1,
							MaxWorkerCursorSize);

	/* We only track the size of attribute 1 (the bson document attribute). */
	PgbsonWriterAppendInt32(&finalWriter, "getpage_batchSizeAttr", -1, 1);
	return PgbsonWriterGetPgbson(&finalWriter);
}


/*
 * Creates a hashset that tracks the continuations across tuples
 * per query page for streaming cursors.
 */
HTAB *
CreateCursorHashSet()
{
	HASHCTL hashInfo = CreateExtensionHashCTL(
		sizeof(CursorContinuationEntry),
		sizeof(CursorContinuationEntry),
		CursorHashEntryCompareFunc,
		CursorHashEntryHashFunc);
	HTAB *cursorElementHashSet =
		hash_create("Bson Cursor Element Hash Table", 32, &hashInfo,
					DefaultExtensionHashFlags);

	return cursorElementHashSet;
}


/*
 * Creates a hashset that maps the node id to continuation token for tailable cursor.
 */
HTAB *
CreateTailableCursorHashSet()
{
	HASHCTL hashInfo;
	hashInfo.entrysize = sizeof(TailableCursorContinuationEntry);
	hashInfo.keysize = sizeof(uint32_t);
	hashInfo.hash = tag_hash;
	int hashFlags = (HASH_ELEM | HASH_FUNCTION);
	HTAB *cursorElementHashSet =
		hash_create("Bson Tailable Cursor Element Hash Table",
					32,
					&hashInfo,
					hashFlags);
	return cursorElementHashSet;
}


/*
 * Set-up the preamble for the cursor page - including the cursorId (which will be
 * overwritten later), the namespace, and sets up the documents array.
 */
void
SetupCursorPagePreamble(pgbson_writer *topLevelWriter, pgbson_writer *cursorDoc,
						pgbson_array_writer *arrayWriter, int64_t cursorId,
						const char *namespaceName, bool isFirstPage,
						uint32_t *accumulatedLength)
{
	PgbsonWriterInit(topLevelWriter);

	/* Write the preface */
	PgbsonWriterStartDocument(topLevelWriter, "cursor", 6, cursorDoc);

	/* Write the cursor ID, this may be overwritten later */
	PgbsonWriterAppendInt64(cursorDoc, "id", 2, cursorId);

	/* write the namespace */
	PgbsonWriterAppendUtf8(cursorDoc, "ns", 2, namespaceName);

	*accumulatedLength += PgbsonWriterGetSize(cursorDoc);

	/* Write the documents */
	const char *pathName = "firstBatch";
	if (!isFirstPage)
	{
		pathName = "nextBatch";
	}

	uint32_t pathLength = strlen(pathName);
	*accumulatedLength += pathLength;

	PgbsonWriterStartArray(cursorDoc, pathName, pathLength, arrayWriter);
	*accumulatedLength += 5;
}


/*
 * Writes the end of the cursor page. Write the end array, overwrite the cursorId
 * with the actual one (if it's drained, replace it with 0).
 * Also creates the result tuple that's (document, continuation) and returns it.
 */
Datum
PostProcessCursorPage(PG_FUNCTION_ARGS,
					  pgbson_writer *cursorDoc, pgbson_array_writer *arrayWriter,
					  pgbson_writer *topLevelWriter, int64_t cursorId,
					  pgbson *continuation, bool persistConnection)
{
	/* Finish the cursor doc*/
	PgbsonWriterEndArray(cursorDoc, arrayWriter);
	PgbsonWriterEndDocument(topLevelWriter, cursorDoc);
	PgbsonWriterAppendDouble(topLevelWriter, "ok", 2, 1);


	bool queryFullyDrained = continuation == NULL;

	/* If this is a oneshot query (singlePage) mark it as drained. */
	if (cursorId == 0)
	{
		queryFullyDrained = true;
	}
	else if (queryFullyDrained)
	{
		bson_iter_t cursorDocIter;
		PgbsonWriterGetIterator(topLevelWriter, &cursorDocIter);
		if (!bson_iter_find_descendant(&cursorDocIter, "cursor.id", &cursorDocIter))
		{
			ereport(ERROR, (errmsg(
								"Could not find cursor.id in cursor document. This is a bug")));
		}

		/* Query is drained - set the cursor to 0 */
		bson_iter_overwrite_int64(&cursorDocIter, 0);
	}

	/* Returns (continuation bson, cursorPage bson) */
	/* Continuation is either an simple bson doc or NULL (if drained) */
	Datum values[4];
	bool nulls[4];
	memset(values, 0, sizeof(values));
	memset(nulls, 0, sizeof(nulls));

	TupleDesc tupleDescriptor = NULL;
	if (get_call_result_type(fcinfo, NULL, &tupleDescriptor) != TYPEFUNC_COMPOSITE)
	{
		elog(ERROR, "return type must be a row type");
	}

	if (tupleDescriptor->natts < 2 &&
		tupleDescriptor->natts > 4)
	{
		elog(ERROR, "incorrect number of output arguments");
	}

	values[0] = PointerGetDatum(PgbsonWriterGetPgbson(topLevelWriter));
	values[1] = queryFullyDrained ? (Datum) 0 : PointerGetDatum(continuation);
	nulls[0] = false;
	nulls[1] = queryFullyDrained;

	if (tupleDescriptor->natts >= 3)
	{
		values[2] = BoolGetDatum(persistConnection);
		nulls[2] = false;
	}

	if (tupleDescriptor->natts == 4)
	{
		values[3] = Int64GetDatum(cursorId);
		nulls[3] = false;
	}

	HeapTuple ret = heap_form_tuple(tupleDescriptor, values, nulls);
	return HeapTupleGetDatum(ret);
}
