/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/aggregation_cursors.c
 *
 * Implementation of the cursor based operations for aggregation/find queries.
 * This wraps around the query
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <funcapi.h>
#include <utils/varlena.h>
#include <access/xact.h>
#include <storage/proc.h>
#include <utils/backend_status.h>

#include <metadata/metadata_cache.h>
#include <utils/documentdb_errors.h>
#include <utils/feature_counter.h>
#include "utils/version_utils.h"
#include <io/bson_core.h>
#include <commands/cursor_private.h>
#include "commands/parse_error.h"
#include <aggregation/bson_aggregation_pipeline.h>
#include "aggregation/aggregation_commands.h"
#include "infrastructure/cursor_store.h"


extern bool EnableNowSystemVariable;
extern bool UseFileBasedPersistedCursors;
extern bool EnableDelayedHoldPortal;

/* --------------------------------------------------------- */
/* Data types */
/* --------------------------------------------------------- */


static const int64_t CursorAcceptableBitsMask = 0x1FFFFFFFFFFFFF;

static uint32_t current_cursor_count = 0;

/*
 * Enum for the type of cursor for this query.
 */
typedef enum CursorKind
{
	/*
	 * The cursor is a streaming cursor.
	 */
	CursorKind_Streaming = 1,

	/*
	 * The cursor is a persisted cursor.
	 */
	CursorKind_Persisted = 2,

	/*
	 * The cursor is a tailable cursor.
	 */
	CursorKind_Tailable = 3
} CursorKind;


/*
 * The type of query command provided
 */
typedef enum QueryKind
{
	/*
	 * The user query is a 'find' query.
	 */
	QueryKind_Find = 1,

	/*
	 * The user query is a 'aggregate' query.
	 */
	QueryKind_Aggregate = 2,

	/*
	 * The user query is a 'listCollections' query.
	 */
	QueryKind_ListCollections = 3,

	/*
	 * The user query is a 'listIndexes' query.
	 */
	QueryKind_ListIndexes = 4,
} QueryKind;


/*
 * Cursor related info for the subsequent pages of a find/aggregate request (getMore)
 */
typedef struct
{
	/*
	 * Whether the first request was streamable or persisted
	 */
	CursorKind cursorKind;

	/*
	 * CursorId associated with this query.
	 */
	int64_t cursorId;

	/*
	 * The persisted cursor name in postgres.
	 */
	const char *cursorName;

	/*
	 * The query spec for a streamable cursor.
	 */
	pgbson *querySpec;

	/*
	 * The original query's query kind (find/aggregate)
	 */
	QueryKind queryKind;

	/*
	 * The current page's cursor info.
	 */
	QueryData queryData;

	/*
	 * The cursor state for the current page if using
	 * file based persisted cursors.
	 */
	bytea *cursorFileState;
} QueryGetMoreInfo;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void ParseGetMoreSpec(text **database, pgbson *getMoreSpec, pgbson *cursorSpec,
							 QueryGetMoreInfo *getMoreInfo, bool setStatementTimeout);

static pgbson * BuildStreamingContinuationDocument(HTAB *cursorMap, pgbson *querySpec,
												   int64_t cursorId, QueryKind queryKind,
												   TimeSystemVariables *
												   timeSystemVariables,
												   int numIterations, bool
												   isTailableCursor);

static pgbson * BuildPersistedContinuationDocument(const char *cursorName, int64_t
												   cursorId, QueryKind queryKind,
												   TimeSystemVariables *
												   timeSystemVariables,
												   int numIterations);

static pgbson * BuildPersistedFileContinuationDocument(const char *cursorName, int64_t
													   cursorId, QueryKind queryKind,
													   TimeSystemVariables *
													   timeSystemVariables,
													   int numIterations,
													   bytea *continuationState);

static Datum HandleFirstPageRequest(pgbson *querySpec, int64_t cursorId,
									QueryData *cursorState,
									QueryKind queryKind, Query *query);

static int64_t GenerateCursorId(int64_t inputValue);


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(command_aggregate_cursor_first_page);
PG_FUNCTION_INFO_V1(command_find_cursor_first_page);
PG_FUNCTION_INFO_V1(command_count_query);
PG_FUNCTION_INFO_V1(command_distinct_query);
PG_FUNCTION_INFO_V1(command_cursor_get_more);
PG_FUNCTION_INFO_V1(command_list_collections_cursor_first_page);
PG_FUNCTION_INFO_V1(command_list_indexes_cursor_first_page);
PG_FUNCTION_INFO_V1(command_delete_cursors);

/*
 * Parses an aggregate spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the aggregate query.
 */
Datum
command_aggregate_cursor_first_page(PG_FUNCTION_ARGS)
{
	text *database = PG_GETARG_TEXT_P(0);
	pgbson *aggregationSpec = PG_GETARG_PGBSON(1);
	int64_t cursorId = PG_ARGISNULL(2) ? 0 : PG_GETARG_INT64(2);

	Datum response = aggregate_cursor_first_page(database, aggregationSpec, cursorId);

	PG_RETURN_DATUM(response);
}


Datum
aggregate_cursor_first_page(text *database, pgbson *aggregationSpec,
							int64_t cursorId)
{
	ReportFeatureUsage(FEATURE_COMMAND_AGG_CURSOR_FIRST_PAGE);

	bool generateCursorParams = true;
	bool setStatementTimeout = true;
	QueryData queryData = GenerateFirstPageQueryData();
	Query *query = GenerateAggregationQuery(database, aggregationSpec, &queryData,
											generateCursorParams, setStatementTimeout);

	Datum response = HandleFirstPageRequest(aggregationSpec, cursorId, &queryData,
											QueryKind_Aggregate, query);
	return response;
}


/*
 * Parses an find spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the find query.
 */
Datum
command_find_cursor_first_page(PG_FUNCTION_ARGS)
{
	text *database = PG_GETARG_TEXT_P(0);
	pgbson *findSpec = PG_GETARG_PGBSON(1);
	int64_t cursorId = PG_ARGISNULL(2) ? 0 : PG_GETARG_INT64(2);

	Datum response = find_cursor_first_page(database, findSpec, cursorId);
	PG_RETURN_DATUM(response);
}


Datum
find_cursor_first_page(text *database, pgbson *findSpec, int64_t cursorId)
{
	ReportFeatureUsage(FEATURE_COMMAND_FIND_CURSOR_FIRST_PAGE);

	/* Parse the find spec for the purposes of query execution */
	QueryData queryData = GenerateFirstPageQueryData();
	bool generateCursorParams = true;
	bool setStatementTimeout = true;
	Query *query = GenerateFindQuery(database, findSpec, &queryData,
									 generateCursorParams,
									 setStatementTimeout);

	Datum response = HandleFirstPageRequest(
		findSpec, cursorId, &queryData,
		QueryKind_Find, query);
	return response;
}


/*
 * Parses a listCollections spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the listCollections query.
 */
Datum
command_list_collections_cursor_first_page(PG_FUNCTION_ARGS)
{
	text *database = PG_GETARG_TEXT_P(0);
	pgbson *listCollectionsSpec = PG_GETARG_PGBSON(1);

	Datum response = list_collections_first_page(database, listCollectionsSpec);
	PG_RETURN_DATUM(response);
}


Datum
list_collections_first_page(text *database, pgbson *listCollectionsSpec)
{
	ReportFeatureUsage(FEATURE_COMMAND_LIST_COLLECTIONS_CURSOR_FIRST_PAGE);

	QueryData queryData = GenerateFirstPageQueryData();
	bool generateCursorParams = false;
	bool setStatementTimeout = true;
	Query *query = GenerateListCollectionsQuery(database, listCollectionsSpec, &queryData,
												generateCursorParams,
												setStatementTimeout);

	/* TODO: Remove these restrictions */
	queryData.cursorKind = QueryCursorType_SingleBatch;
	queryData.batchSize = INT_MAX;

	int64_t cursorId = 0;
	Datum response = HandleFirstPageRequest(
		listCollectionsSpec, cursorId, &queryData,
		QueryKind_ListCollections, query);
	return response;
}


/*
 * Parses a listIndexes spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the listIndexes query.
 */
Datum
command_list_indexes_cursor_first_page(PG_FUNCTION_ARGS)
{
	text *database = PG_GETARG_TEXT_P(0);
	pgbson *listIndexesSpec = PG_GETARG_PGBSON(1);

	Datum response = list_indexes_first_page(database, listIndexesSpec);
	PG_RETURN_DATUM(response);
}


Datum
list_indexes_first_page(text *database, pgbson *listIndexesSpec)
{
	ReportFeatureUsage(FEATURE_COMMAND_LIST_INDEXES_CURSOR_FIRST_PAGE);

	QueryData queryData = GenerateFirstPageQueryData();
	bool generateCursorParams = false;
	bool setStatementTimeout = true;
	Query *query = GenerateListIndexesQuery(database, listIndexesSpec, &queryData,
											generateCursorParams, setStatementTimeout);

	/* TODO: Remove these restrictions */
	queryData.cursorKind = QueryCursorType_SingleBatch;
	queryData.batchSize = INT_MAX;

	int64_t cursorId = 0;
	Datum response = HandleFirstPageRequest(
		listIndexesSpec, cursorId, &queryData,
		QueryKind_ListIndexes, query);
	return response;
}


/*
 * Parses a getMore spec and a continuation cursor spec, extracts the query
 * associated with it executes it and returns the next page
 * along with the cursor information associated with the original query.
 */
Datum
command_cursor_get_more(PG_FUNCTION_ARGS)
{
	text *database = PG_GETARG_TEXT_P(0);
	pgbson *getMoreSpec = PG_GETARG_PGBSON(1);
	pgbson *cursorSpec = PG_GETARG_PGBSON(2);

	/* See sql/udfs/commands_crud/query_cursors_aggregate--latest.sql */
	AttrNumber maxOutAttrNum = 2;
	Datum responseDatum = aggregation_cursor_get_more(database, getMoreSpec,
													  cursorSpec, maxOutAttrNum);
	PG_RETURN_DATUM(responseDatum);
}


Datum
aggregation_cursor_get_more(text *database, pgbson *getMoreSpec,
							pgbson *cursorSpec, AttrNumber maxResponseAttributeNumber)
{
	ReportFeatureUsage(FEATURE_COMMAND_GET_MORE);

	TupleDesc tupleDesc = ConstructCursorResultTupleDesc(maxResponseAttributeNumber);

	QueryGetMoreInfo getMoreInfo = { 0 };
	bool getMoreSetStatementTimeout = true;
	ParseGetMoreSpec(&database, getMoreSpec, cursorSpec, &getMoreInfo,
					 getMoreSetStatementTimeout);

	pgbson_writer writer;
	pgbson_writer cursorDoc;
	pgbson_array_writer arrayWriter;

	/* min bson size is 5 (see IsPgbsonEmptyDocument) */
	uint32_t accumulatedSize = 5;

	/* Write the preamble for the cursor response */
	bool isFirstPage = false;
	SetupCursorPagePreamble(&writer, &cursorDoc, &arrayWriter,
							getMoreInfo.queryData.namespaceName,
							isFirstPage,
							&accumulatedSize);

	bool queryFullyDrained;
	pgbson *continuationDoc;
	pgbson *postBatchResumeToken = NULL;
	switch (getMoreInfo.cursorKind)
	{
		case CursorKind_Persisted:
		{
			if (getMoreInfo.cursorFileState != NULL)
			{
				if (!UseFileBasedPersistedCursors)
				{
					ereport(ERROR,
							(errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							 errmsg("File based persisted cursors are not enabled.")));
				}

				int numIterations = 0;
				getMoreInfo.cursorFileState = DrainPersistedFileCursor(
					getMoreInfo.cursorName,
					getMoreInfo.
					queryData.batchSize,
					&numIterations,
					accumulatedSize,
					&arrayWriter,
					getMoreInfo.
					cursorFileState);
				queryFullyDrained = getMoreInfo.cursorFileState == NULL;
				continuationDoc = queryFullyDrained ? NULL :
								  BuildPersistedFileContinuationDocument(
					getMoreInfo.cursorName,
					getMoreInfo.
					cursorId,
					getMoreInfo.
					queryKind,
					&getMoreInfo.
					queryData.
					timeSystemVariables,
					numIterations,
					getMoreInfo.
					cursorFileState);
			}
			else
			{
				int numIterations = 0;
				queryFullyDrained = DrainPersistedCursor(getMoreInfo.cursorName,
														 getMoreInfo.queryData.batchSize,
														 &numIterations,
														 accumulatedSize, &arrayWriter);
				continuationDoc = queryFullyDrained ? NULL :
								  BuildPersistedContinuationDocument(
					getMoreInfo.cursorName,
					getMoreInfo.cursorId,
					getMoreInfo.queryKind,
					&getMoreInfo.
					queryData.
					timeSystemVariables,
					numIterations);
			}
			break;
		}

		case CursorKind_Streaming:
		{
			Query *query;
			bool generateCursorParams = true;

			/* Some blank query data to pass to the generation. */
			QueryData queryData = { 0 };
			switch (getMoreInfo.queryKind)
			{
				case QueryKind_Find:
				{
					queryData.timeSystemVariables =
						getMoreInfo.queryData.timeSystemVariables;

					bool setStatementTimeout = false;
					query = GenerateFindQuery(database,
											  getMoreInfo.querySpec, &queryData,
											  generateCursorParams,
											  setStatementTimeout);
					break;
				}

				case QueryKind_Aggregate:
				{
					queryData.timeSystemVariables =
						getMoreInfo.queryData.timeSystemVariables;

					bool setStatementTimeout = false;
					query = GenerateAggregationQuery(database,
													 getMoreInfo.querySpec, &queryData,
													 generateCursorParams,
													 setStatementTimeout);
					break;
				}

				default:
				{
					Assert(false);
					pg_unreachable();
				}
			}

			HTAB *cursorMap = CreateCursorHashSet();
			BuildContinuationMap(cursorSpec, cursorMap);


			int numIterations = 0;
			queryFullyDrained = DrainStreamingQuery(cursorMap, query,
													getMoreInfo.queryData.batchSize,
													&numIterations,
													accumulatedSize, &arrayWriter);
			continuationDoc = queryFullyDrained ? NULL :
							  BuildStreamingContinuationDocument(cursorMap,
																 getMoreInfo.querySpec,
																 getMoreInfo.cursorId,
																 getMoreInfo.queryKind,
																 &getMoreInfo.queryData.
																 timeSystemVariables,
																 numIterations, false);
			hash_destroy(cursorMap);
			break;
		}

		case CursorKind_Tailable:
		{
			Query *query;
			bool generateCursorParams = true;
			QueryData queryData = { 0 };
			queryData.timeSystemVariables = getMoreInfo.queryData.timeSystemVariables;

			bool setStatementTimeout = false;
			query = GenerateAggregationQuery(database,
											 getMoreInfo.querySpec, &queryData,
											 generateCursorParams, setStatementTimeout);
			HTAB *cursorMap = CreateTailableCursorHashSet();
			BuildTailableCursorContinuationMap(cursorSpec, cursorMap);
			int numIterations = 0;
			postBatchResumeToken = DrainTailableQuery(cursorMap, query,
													  getMoreInfo.queryData.batchSize,
													  &numIterations,
													  accumulatedSize, &arrayWriter);
			continuationDoc = BuildStreamingContinuationDocument(cursorMap,
																 getMoreInfo.querySpec,
																 getMoreInfo.cursorId,
																 getMoreInfo.queryKind,
																 &getMoreInfo.queryData.
																 timeSystemVariables,
																 numIterations, true);
			hash_destroy(cursorMap);
			break;
		}

		default:
		{
			Assert(false);
			pg_unreachable();
		}
	}

	bool persistConnection = false;
	Datum responseDatum = PostProcessCursorPage(&cursorDoc, &arrayWriter, &writer,
												getMoreInfo.cursorId, continuationDoc,
												persistConnection, postBatchResumeToken,
												tupleDesc);
	return responseDatum;
}


/*
 * Runs a Distinct query with a given spec against
 * the backend.
 */
Datum
command_distinct_query(PG_FUNCTION_ARGS)
{
	ReportFeatureUsage(FEATURE_COMMAND_DISTINCT);

	text *database = PG_GETARG_TEXT_P(0);
	pgbson *distinctSpec = PG_GETARG_PGBSON(1);

	bool setStatementTimeout = true;
	Query *query = GenerateDistinctQuery(database, distinctSpec, setStatementTimeout);

	pgbson *response = DrainSingleResultQuery(query);

	if (response == NULL)
	{
		pgbson_writer defaultWriter;
		PgbsonWriterInit(&defaultWriter);
		PgbsonWriterAppendEmptyArray(&defaultWriter, "values", 6);
		PgbsonWriterAppendDouble(&defaultWriter, "ok", 2, 1);
		response = PgbsonWriterGetPgbson(&defaultWriter);
	}

	PG_RETURN_POINTER(response);
}


/*
 * Runs a Count query with a given spec against
 * the backend.
 */
Datum
command_count_query(PG_FUNCTION_ARGS)
{
	ReportFeatureUsage(FEATURE_COMMAND_COUNT);

	text *database = PG_GETARG_TEXT_P(0);
	pgbson *countSpec = PG_GETARG_PGBSON(1);

	bool setStatementTimeout = true;
	Query *query = GenerateCountQuery(database, countSpec, setStatementTimeout);

	pgbson *response = DrainSingleResultQuery(query);
	if (response == NULL)
	{
		/* Generate default response */
		pgbson_writer defaultWriter;
		PgbsonWriterInit(&defaultWriter);
		PgbsonWriterAppendInt32(&defaultWriter, "n", 1, 0);
		PgbsonWriterAppendDouble(&defaultWriter, "ok", 2, 1);
		response = PgbsonWriterGetPgbson(&defaultWriter);
	}

	PG_RETURN_POINTER(response);
}


Datum
command_delete_cursors(PG_FUNCTION_ARGS)
{
	ArrayType *cursorArray = PG_GETARG_ARRAYTYPE_P(0);

	Datum response = delete_cursors(cursorArray);
	PG_RETURN_DATUM(response);
}


inline static const char *
FormatCursorName(StringInfo cursorStringInfo, int64_t cursorId)
{
	resetStringInfo(cursorStringInfo);
	appendStringInfo(cursorStringInfo, "cursor_%ld", cursorId);
	return cursorStringInfo->data;
}


Datum
delete_cursors(ArrayType *cursorArray)
{
	Datum *cursorIds;
	bool *nulls;
	int nelems;
	if (!UseFileBasedPersistedCursors)
	{
		return PointerGetDatum(PgbsonInitEmpty());
	}

	deconstruct_array(cursorArray, INT8OID, sizeof(int64_t), true, TYPALIGN_INT,
					  &cursorIds, &nulls, &nelems);

	StringInfo cursorStringInfo = makeStringInfo();
	for (int i = 0; i < nelems; i++)
	{
		if (nulls[i])
		{
			continue;
		}

		int64_t cursorId = DatumGetInt64(cursorIds[i]);
		const char *cursorName = FormatCursorName(cursorStringInfo, cursorId);
		DeleteCursorFile(cursorName);
	}

	return PointerGetDatum(PgbsonInitEmpty());
}


Query *
GenerateGetMoreQuery(text *database, pgbson *getMoreSpec, pgbson *continuationSpec,
					 QueryData *queryData, bool addCursorParams, bool setStatementTimeout)
{
	QueryGetMoreInfo getMoreInfo = { 0 };
	ParseGetMoreSpec(&database, getMoreSpec, continuationSpec, &getMoreInfo,
					 setStatementTimeout);

	switch (getMoreInfo.cursorKind)
	{
		case CursorKind_Streaming:
		{
			Query *query;

			HTAB *cursorMap = CreateCursorHashSet();
			BuildContinuationMap(continuationSpec, cursorMap);
			pgbson *workerSpec = SerializeContinuationForWorker(cursorMap,
																getMoreInfo.queryData.
																batchSize, false);

			/* Some blank query data to pass to the generation. */
			QueryData queryData = { 0 };
			switch (getMoreInfo.queryKind)
			{
				case QueryKind_Find:
				{
					queryData.timeSystemVariables =
						getMoreInfo.queryData.timeSystemVariables;
					queryData.cursorStateConst = workerSpec;
					query = GenerateFindQuery(database,
											  getMoreInfo.querySpec, &queryData,
											  addCursorParams,
											  setStatementTimeout);
					break;
				}

				case QueryKind_Aggregate:
				{
					queryData.timeSystemVariables =
						getMoreInfo.queryData.timeSystemVariables;
					queryData.cursorStateConst = workerSpec;
					query = GenerateAggregationQuery(database,
													 getMoreInfo.querySpec, &queryData,
													 addCursorParams,
													 setStatementTimeout);
					break;
				}

				default:
				{
					Assert(false);
					pg_unreachable();
				}
			}

			return query;
		}

		case CursorKind_Persisted:
		case CursorKind_Tailable:
		default:
		{
			/* This path doesn't build a new query on getMore - thunk to just calling the getmore Func */
			return BuildAggregationCursorGetMoreQuery(database, getMoreSpec,
													  continuationSpec);
		}
	}
}


/*
 * Given a pre-built query (for find/aggregate) handles the cursor request
 * and builds a response for the first page.
 */
static Datum
HandleFirstPageRequest(pgbson *querySpec, int64_t cursorId,
					   QueryData *queryData, QueryKind queryKind, Query *query)
{
	pgbson_writer writer;
	pgbson_writer cursorDoc;
	pgbson_array_writer arrayWriter;

	/* min bson size is 5 (see IsPgbsonEmptyDocument) */
	uint32_t accumulatedSize = 5;

	/* Write the preamble for the cursor response */
	bool isFirstPage = true;
	SetupCursorPagePreamble(&writer, &cursorDoc, &arrayWriter,
							queryData->namespaceName, isFirstPage,
							&accumulatedSize);

	/* now set up the query */
	int32_t numIterations = 0;
	bool queryFullyDrained;
	pgbson *continuationDoc;
	bool persistConnection = false;
	pgbson *postBatchResumeToken = NULL;
	switch (queryData->cursorKind)
	{
		case QueryCursorType_SingleBatch:
		{
			ReportFeatureUsage(FEATURE_CURSOR_TYPE_SINGLE_BATCH);
			CreateAndDrainSingleBatchQuery("singleBatchCursor", query,
										   queryData->batchSize,
										   &numIterations,
										   accumulatedSize, &arrayWriter);
			queryFullyDrained = true;
			continuationDoc = NULL;
			cursorId = 0;
			break;
		}

		case QueryCursorType_Tailable:
		{
			ReportFeatureUsage(FEATURE_CURSOR_TYPE_TAILABLE);

			HTAB *tailableCursorMap = CreateTailableCursorHashSet();
			postBatchResumeToken = DrainTailableQuery(tailableCursorMap,
													  query,
													  queryData->batchSize,
													  &numIterations,
													  accumulatedSize,
													  &arrayWriter);
			cursorId = GenerateCursorId(cursorId);
			continuationDoc = BuildStreamingContinuationDocument(tailableCursorMap,
																 querySpec,
																 cursorId, queryKind,
																 &queryData->
																 timeSystemVariables,
																 numIterations, true);
			hash_destroy(tailableCursorMap);
			break;
		}

		case QueryCursorType_Streamable:
		{
			ReportFeatureUsage(FEATURE_CURSOR_TYPE_STREAMING);

			Assert(queryData->cursorStateParamNumber == 1);
			HTAB *cursorMap = CreateCursorHashSet();
			queryFullyDrained = DrainStreamingQuery(cursorMap, query,
													queryData->batchSize,
													&numIterations, accumulatedSize,
													&arrayWriter);

			continuationDoc = NULL;
			if (!queryFullyDrained)
			{
				cursorId = GenerateCursorId(cursorId);
				continuationDoc = BuildStreamingContinuationDocument(cursorMap, querySpec,
																	 cursorId, queryKind,
																	 &queryData->
																	 timeSystemVariables,
																	 numIterations,
																	 false);
			}

			hash_destroy(cursorMap);
			break;
		}

		case QueryCursorType_Persistent:
		{
			ReportFeatureUsage(FEATURE_CURSOR_TYPE_PERSISTENT);

			current_cursor_count++;
			int64_t cursorIdForBackendCursor;

			if (cursorId != 0)
			{
				cursorIdForBackendCursor = cursorId;
			}
			else if (!EnableDelayedHoldPortal)
			{
				cursorId = GenerateCursorId(cursorId);
				cursorIdForBackendCursor = cursorId;
			}
			else
			{
				cursorIdForBackendCursor = (((int64_t) MyProcPid) << 32) |
										   current_cursor_count;
			}

			StringInfo cursorStringInfo = makeStringInfo();
			const char *cursorName = FormatCursorName(cursorStringInfo,
													  cursorIdForBackendCursor);

			bool isTopLevel = true;
			bool isHoldCursor = !IsInTransactionBlock(isTopLevel);
			persistConnection = isHoldCursor;
			bool closeCursor = false;

			if (isHoldCursor && UseFileBasedPersistedCursors)
			{
				persistConnection = false;
				bytea *cursorFileState = CreateAndDrainPersistedQueryWithFiles(cursorName,
																			   query,
																			   queryData->
																			   batchSize,
																			   &
																			   numIterations,
																			   accumulatedSize,
																			   &
																			   arrayWriter,
																			   closeCursor);
				queryFullyDrained = cursorFileState == NULL;

				if (!queryFullyDrained)
				{
					cursorId = GenerateCursorId(cursorId);
					continuationDoc = BuildPersistedFileContinuationDocument(cursorName,
																			 cursorId,
																			 queryKind,
																			 &queryData->
																			 timeSystemVariables,
																			 numIterations,
																			 cursorFileState);
				}
				else
				{
					continuationDoc = NULL;
				}
			}
			else
			{
				queryFullyDrained = CreateAndDrainPersistedQuery(cursorName, query,
																 queryData->batchSize,
																 &numIterations,
																 accumulatedSize,
																 &arrayWriter,
																 isHoldCursor,
																 closeCursor);
				if (!queryFullyDrained)
				{
					cursorId = GenerateCursorId(cursorId);
					continuationDoc = BuildPersistedContinuationDocument(cursorName,
																		 cursorId,
																		 queryKind,
																		 &queryData->
																		 timeSystemVariables,
																		 numIterations);
				}
				else
				{
					continuationDoc = NULL;
				}
			}
			break;
		}

		case QueryCursorType_PointRead:
		{
			ReportFeatureUsage(FEATURE_CURSOR_TYPE_POINT_READ);

			if (queryData->batchSize < 1)
			{
				ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
								errmsg(
									"Point read plan should have batch size >= 1, not %d",
									queryData->batchSize),
								errdetail_log(
									"Point read plan should have batch size >= 1, not %d",
									queryData->batchSize)));
			}

			CreateAndDrainPointReadQuery("pointReadCursor", query,
										 &numIterations,
										 accumulatedSize, &arrayWriter);
			queryFullyDrained = true;
			continuationDoc = NULL;
			break;
		}

		default:
		{
			ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
							errmsg("Unknown query cursor kind detected - %d",
								   queryData->cursorKind)));
		}
	}

	/* See sql/udfs/commands_crud/query_cursors_aggregate--latest.sql */
	AttrNumber maxOutAttrNum = 4;
	TupleDesc tupleDesc = ConstructCursorResultTupleDesc(maxOutAttrNum);

	return PostProcessCursorPage(&cursorDoc, &arrayWriter, &writer, cursorId,
								 continuationDoc, persistConnection,
								 postBatchResumeToken, tupleDesc);
}


/*
 * Serializes a cursor document that can be reused by getMore for a streaming query.
 */
static pgbson *
BuildStreamingContinuationDocument(HTAB *cursorMap, pgbson *querySpec, int64_t cursorId,
								   QueryKind queryKind,
								   TimeSystemVariables *timeSystemVariables, int
								   numIterations, bool
								   isTailableCursor)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "qi", 2, cursorId);
	PgbsonWriterAppendBool(&writer, "qp", 2, false);

	PgbsonWriterAppendInt32(&writer, "qk", 2, (int) queryKind);

	/* Add the original query spec so that getMore can reuse it */
	if (isTailableCursor)
	{
		/* For tailable cursor, save the query with "qt" to differentiate from streaming query. */
		PgbsonWriterAppendDocument(&writer, "qt", 2, querySpec);
	}
	else
	{
		/* For streaming cursor, save the query with "qc" key. */
		PgbsonWriterAppendDocument(&writer, "qc", 2, querySpec);
	}

	if (isTailableCursor)
	{
		SerializeTailableContinuationsToWriter(&writer, cursorMap);
	}
	else
	{
		SerializeContinuationsToWriter(&writer, cursorMap);
	}

	/* In the response add the number of iterations (used in tests) */
	PgbsonWriterAppendInt32(&writer, "numIters", 8, numIterations);

	/* Add time system variables accordingly */
	if (EnableNowSystemVariable)
	{
		if (timeSystemVariables != NULL && timeSystemVariables->nowValue.value_type !=
			BSON_TYPE_EOD)
		{
			PgbsonWriterAppendValue(&writer, "sn", 2, &timeSystemVariables->nowValue);
		}
	}

	return PgbsonWriterGetPgbson(&writer);
}


static pgbson *
BuildPersistedFileContinuationDocument(const char *cursorName, int64_t
									   cursorId, QueryKind queryKind,
									   TimeSystemVariables *
									   timeSystemVariables,
									   int numIterations,
									   bytea *continuationState)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "qi", 2, cursorId);
	PgbsonWriterAppendBool(&writer, "qp", 2, true);

	/* Add the original query spec so that getMore can reuse it */
	PgbsonWriterAppendInt32(&writer, "qk", 2, (int) queryKind);
	PgbsonWriterAppendUtf8(&writer, "qn", 2, cursorName);

	bson_value_t continuationValue;
	continuationValue.value_type = BSON_TYPE_BINARY;
	continuationValue.value.v_binary.subtype = BSON_SUBTYPE_BINARY;
	continuationValue.value.v_binary.data = (uint8_t *) continuationState;
	continuationValue.value.v_binary.data_len = VARSIZE(continuationState);
	PgbsonWriterAppendValue(&writer, "qf", 2, &continuationValue);

	/* In the response add the number of iterations (used in tests) */
	PgbsonWriterAppendInt32(&writer, "numIters", 8, numIterations);

	/* Add time system variables accordingly */
	if (EnableNowSystemVariable)
	{
		if (timeSystemVariables != NULL && timeSystemVariables->nowValue.value_type !=
			BSON_TYPE_EOD)
		{
			PgbsonWriterAppendValue(&writer, "sn", 2, &timeSystemVariables->nowValue);
		}
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Serializes a cursor document that can be reused by getMore for a persitent query.
 */
static pgbson *
BuildPersistedContinuationDocument(const char *cursorName, int64_t cursorId, QueryKind
								   queryKind, TimeSystemVariables *timeSystemVariables,
								   int
								   numIterations)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	PgbsonWriterAppendInt64(&writer, "qi", 2, cursorId);
	PgbsonWriterAppendBool(&writer, "qp", 2, true);

	/* Add the original query spec so that getMore can reuse it */
	PgbsonWriterAppendInt32(&writer, "qk", 2, (int) queryKind);
	PgbsonWriterAppendUtf8(&writer, "qn", 2, cursorName);

	/* In the response add the number of iterations (used in tests) */
	PgbsonWriterAppendInt32(&writer, "numIters", 8, numIterations);

	/* Add time system variables accordingly */
	if (EnableNowSystemVariable)
	{
		if (timeSystemVariables != NULL && timeSystemVariables->nowValue.value_type !=
			BSON_TYPE_EOD)
		{
			PgbsonWriterAppendValue(&writer, "sn", 2, &timeSystemVariables->nowValue);
		}
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Parses the serialized cursor spec of the prior iteration. This is the inverse
 * function of BuildStreamingContinuationDocument and BuildPersistedContinuationDocument
 */
static void
ParseCursorInputSpec(pgbson *cursorSpec, QueryGetMoreInfo *getMoreInfo)
{
	bson_iter_t cursorSpecIter;
	PgbsonInitIterator(cursorSpec, &cursorSpecIter);
	while (bson_iter_next(&cursorSpecIter))
	{
		const char *pathKey = bson_iter_key(&cursorSpecIter);
		switch (pathKey[0])
		{
			case 'q':
			{
				switch (pathKey[1])
				{
					/* Query command */
					case 'c':
					{
						/* This is the query command */
						Assert(pathKey[2] == '\0');
						getMoreInfo->querySpec = PgbsonInitFromDocumentBsonValue(
							bson_iter_value(&cursorSpecIter));
						getMoreInfo->cursorKind = CursorKind_Streaming;
						continue;
					}

					/* Query tailable */
					case 't':
					{
						/* This is the query command */
						Assert(pathKey[2] == '\0');
						getMoreInfo->querySpec = PgbsonInitFromDocumentBsonValue(
							bson_iter_value(&cursorSpecIter));
						getMoreInfo->cursorKind = CursorKind_Tailable;
						continue;
					}

					/* Query cursor name */
					case 'n':
					{
						Assert(pathKey[2] == '\0');
						getMoreInfo->cursorName = bson_iter_utf8(&cursorSpecIter, NULL);
						getMoreInfo->cursorKind = CursorKind_Persisted;
						continue;
					}

					/* Query cursor id */
					case 'i':
					{
						Assert(pathKey[2] == '\0');
						getMoreInfo->cursorId = bson_iter_int64(&cursorSpecIter);
						continue;
					}

					/* Query cursor kind */
					case 'k':
					{
						Assert(pathKey[2] == '\0');
						getMoreInfo->queryKind = (QueryKind) bson_iter_int32(
							&cursorSpecIter);
						continue;
					}

					case 'f':
					{
						/* Query file state for the cursor */
						Assert(pathKey[2] == '\0');
						bson_subtype_t subtype;
						uint32_t binaryLength = 0;
						const uint8_t *binaryData = NULL;
						bson_iter_binary(&cursorSpecIter, &subtype,
										 &binaryLength, &binaryData);

						bytea *cursorState = palloc(binaryLength);
						memcpy(cursorState, binaryData, binaryLength);
						getMoreInfo->cursorFileState = cursorState;
						continue;
					}

					/* Continuation persistence - ignored */
					case 'p':
					{
						continue;
					}
				}

				continue;
			}

			case 's':
			{
				switch (pathKey[1])
				{
					/* $$NOW time system variable (now)*/
					case 'n':
					{
						const bson_value_t *nowDateValue = bson_iter_value(
							&cursorSpecIter);
						getMoreInfo->queryData.timeSystemVariables.nowValue =
							*nowDateValue;
						continue;
					}
				}
				continue;
			}
		}
	}
}


/*
 * Parses the getMore spec and builds the necessary pipeline/query information from a cursor standpoint.
 */
static void
ParseGetMoreSpec(text **databaseName, pgbson *getMoreSpec, pgbson *cursorSpec,
				 QueryGetMoreInfo *getMoreInfo, bool setStatementTimeout)
{
	/* Default batchSize for getMore */
	getMoreInfo->queryData.batchSize = INT_MAX;

	ParseCursorInputSpec(cursorSpec, getMoreInfo);

	/* Parses the wire protocol getMore */
	int64_t cursorId = ParseGetMore(databaseName, getMoreSpec, &getMoreInfo->queryData,
									setStatementTimeout);
	if (cursorId != getMoreInfo->cursorId)
	{
		ereport(ERROR, (errmsg(
							"CursorID from GetMore does not match from cursor state, getMore: %ld, cursorState %ld",
							cursorId, getMoreInfo->cursorId)));
	}
}


/*
 * Creates a unique cursorId if one isn't provided.
 */
static int64_t
GenerateCursorId(int64_t inputValue)
{
	if (inputValue != 0)
	{
		return inputValue;
	}

	/* 2^53-1 masks integer precision of IEEE 754 double precision floating point numbers
	 * Works around issue with certain versions of the NodeJS driver
	 */
	char cursorBuffer[8];

	/* This is the same logic UUID generation uses - we should be good here */
	if (!pg_strong_random(cursorBuffer, 8))
	{
		ereport(ERROR, (errmsg("Failed to create a unique identifier for the cursor")));
	}

	int64_t cursorId = *(int64_t *) cursorBuffer;
	return (cursorId & CursorAcceptableBitsMask);
}
