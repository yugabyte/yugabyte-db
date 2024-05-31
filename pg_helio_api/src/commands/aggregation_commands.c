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

#include <metadata/metadata_cache.h>
#include <utils/mongo_errors.h>
#include <io/helio_bson_core.h>
#include <commands/cursor_private.h>
#include <aggregation/bson_aggregation_pipeline.h>


/* --------------------------------------------------------- */
/* Data types */
/* --------------------------------------------------------- */


static const int64_t CursorAcceptableBitsMask = 0x1FFFFFFFFFFFFF;

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
	CursorKind_Persisted = 2
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
} QueryGetMoreInfo;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static void ParseGetMoreSpec(text *databaseName, pgbson *getMoreSpec, pgbson *cursorSpec,
							 QueryGetMoreInfo *getMoreInfo);

static pgbson * BuildStreamingContinuationDocument(HTAB *cursorMap, pgbson *querySpec,
												   int64_t cursorId, QueryKind queryKind,
												   int numIterations);

static pgbson * BuildPersistedContinuationDocument(const char *cursorName, int64_t
												   cursorId, QueryKind queryKind,
												   int numIterations);

static Datum HandleFirstPageRequest(PG_FUNCTION_ARGS,
									text *database, pgbson *querySpec, int64_t cursorId,
									QueryData *cursorState,
									QueryKind queryKind, Query *query);

static int64_t GenerateCursorId(void);


/* Generates a base QueryData used for the first page */
inline static QueryData
GenerateFirstPageQueryData(void)
{
	QueryData queryData = { 0 };
	queryData.batchSize = 101;
	return queryData;
}


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

/*
 * Parses an aggregate spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the aggregate query.
 */
Datum
command_aggregate_cursor_first_page(PG_FUNCTION_ARGS)
{
	Datum database = PG_GETARG_DATUM(0);
	pgbson *aggregationSpec = PG_GETARG_PGBSON(1);
	int64_t cursorId = PG_GETARG_INT64(2);

	if (cursorId == 0)
	{
		cursorId = GenerateCursorId();
	}

	bool generateCursorParams = true;
	QueryData queryData = GenerateFirstPageQueryData();
	Query *query = GenerateAggregationQuery(database, aggregationSpec, &queryData,
											generateCursorParams);

	Datum response = HandleFirstPageRequest(
		fcinfo, DatumGetTextP(database), aggregationSpec, cursorId, &queryData,
		QueryKind_Aggregate, query);

	PG_RETURN_DATUM(response);
}


/*
 * Parses an find spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the find query.
 */
Datum
command_find_cursor_first_page(PG_FUNCTION_ARGS)
{
	Datum database = PG_GETARG_DATUM(0);
	pgbson *findSpec = PG_GETARG_PGBSON(1);
	int64_t cursorId = PG_GETARG_INT64(2);

	if (cursorId == 0)
	{
		cursorId = GenerateCursorId();
	}

	/* Parse the find spec for the purposes of query execution */
	QueryData queryData = GenerateFirstPageQueryData();
	bool generateCursorParams = true;
	Query *query = GenerateFindQuery(database, findSpec, &queryData,
									 generateCursorParams);

	Datum response = HandleFirstPageRequest(
		fcinfo, DatumGetTextP(database), findSpec, cursorId, &queryData,
		QueryKind_Find, query);

	PG_RETURN_DATUM(response);
}


/*
 * Parses a listCollections spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the listCollections query.
 */
Datum
command_list_collections_cursor_first_page(PG_FUNCTION_ARGS)
{
	Datum database = PG_GETARG_DATUM(0);
	pgbson *listCollectionsSpec = PG_GETARG_PGBSON(1);
	QueryData queryData = GenerateFirstPageQueryData();
	bool generateCursorParams = false;
	Query *query = GenerateListCollectionsQuery(database, listCollectionsSpec, &queryData,
												generateCursorParams);

	/* TODO: Remove these restrictions */
	queryData.isSingleBatch = true;
	queryData.isStreamableCursor = false;
	queryData.batchSize = INT_MAX;

	int64_t cursorId = 0;
	Datum response = HandleFirstPageRequest(
		fcinfo, DatumGetTextP(database), listCollectionsSpec, cursorId, &queryData,
		QueryKind_ListCollections, query);

	PG_RETURN_DATUM(response);
}


/*
 * Parses a listIndexes spec and creates a query, executes it and returns the first page
 * along with the cursor information associated with the listIndexes query.
 */
Datum
command_list_indexes_cursor_first_page(PG_FUNCTION_ARGS)
{
	Datum database = PG_GETARG_DATUM(0);
	pgbson *listIndexesSpec = PG_GETARG_PGBSON(1);
	QueryData queryData = GenerateFirstPageQueryData();
	bool generateCursorParams = false;
	Query *query = GenerateListIndexesQuery(database, listIndexesSpec, &queryData,
											generateCursorParams);

	/* TODO: Remove these restrictions */
	queryData.isSingleBatch = true;
	queryData.isStreamableCursor = false;
	queryData.batchSize = INT_MAX;

	int64_t cursorId = 0;
	Datum response = HandleFirstPageRequest(
		fcinfo, DatumGetTextP(database), listIndexesSpec, cursorId, &queryData,
		QueryKind_ListIndexes, query);

	PG_RETURN_DATUM(response);
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

	QueryGetMoreInfo getMoreInfo = { 0 };
	ParseGetMoreSpec(database, getMoreSpec, cursorSpec, &getMoreInfo);

	pgbson_writer writer;
	pgbson_writer cursorDoc;
	pgbson_array_writer arrayWriter;

	/* min bson size is 5 (see IsPgbsonEmptyDocument) */
	uint32_t accumulatedSize = 5;

	/* Write the preamble for the cursor response */
	bool isFirstPage = false;
	SetupCursorPagePreamble(&writer, &cursorDoc, &arrayWriter,
							getMoreInfo.cursorId, getMoreInfo.queryData.namespaceName,
							isFirstPage,
							&accumulatedSize);

	bool queryFullyDrained;
	pgbson *continuationDoc;
	switch (getMoreInfo.cursorKind)
	{
		case CursorKind_Persisted:
		{
			int numIterations = 0;
			queryFullyDrained = DrainPersistedCursor(getMoreInfo.cursorName,
													 getMoreInfo.queryData.batchSize,
													 &numIterations,
													 accumulatedSize, &arrayWriter);
			continuationDoc = queryFullyDrained ? NULL :
							  BuildPersistedContinuationDocument(getMoreInfo.cursorName,
																 getMoreInfo.cursorId,
																 getMoreInfo.queryKind,
																 numIterations);
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
					query = GenerateFindQuery(PointerGetDatum(database),
											  getMoreInfo.querySpec, &queryData,
											  generateCursorParams);
					break;
				}

				case QueryKind_Aggregate:
				{
					query = GenerateAggregationQuery(PointerGetDatum(database),
													 getMoreInfo.querySpec, &queryData,
													 generateCursorParams);
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
																 numIterations);
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
	Datum responseDatum = PostProcessCursorPage(fcinfo, &cursorDoc, &arrayWriter, &writer,
												getMoreInfo.cursorId, continuationDoc,
												persistConnection);
	PG_RETURN_DATUM(responseDatum);
}


/*
 * Runs a Distinct query with a given spec against
 * the backend.
 */
Datum
command_distinct_query(PG_FUNCTION_ARGS)
{
	Datum database = PG_GETARG_DATUM(0);
	pgbson *distinctSpec = PG_GETARG_PGBSON(1);

	Query *query = GenerateDistinctQuery(database, distinctSpec);

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
	Datum database = PG_GETARG_DATUM(0);
	pgbson *countSpec = PG_GETARG_PGBSON(1);

	Query *query = GenerateCountQuery(database, countSpec);

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


/*
 * Given a pre-built query (for find/aggregate) handles the cursor request
 * and builds a response for the first page.
 */
static Datum
HandleFirstPageRequest(PG_FUNCTION_ARGS,
					   text *database, pgbson *querySpec, int64_t cursorId,
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
							cursorId, queryData->namespaceName, isFirstPage,
							&accumulatedSize);

	/* now set up the query */
	int32_t numIterations = 0;
	bool queryFullyDrained;
	pgbson *continuationDoc;
	bool persistConnection = false;
	if (queryData->isSingleBatch)
	{
		bool isHoldCursor = false;
		bool closeCursor = true;
		CreateAndDrainPersistedQuery("singleBatchCursor", query,
									 queryData->batchSize,
									 &numIterations,
									 accumulatedSize, &arrayWriter,
									 isHoldCursor, closeCursor);
		queryFullyDrained = true;
		continuationDoc = NULL;
	}
	else if (queryData->isStreamableCursor)
	{
		Assert(queryData->cursorStateParamNumber == 1);
		HTAB *cursorMap = CreateCursorHashSet();
		queryFullyDrained = DrainStreamingQuery(cursorMap, query, queryData->batchSize,
												&numIterations, accumulatedSize,
												&arrayWriter);
		queryFullyDrained = queryFullyDrained || queryData->isSingleBatch;

		continuationDoc = queryFullyDrained ? NULL :
						  BuildStreamingContinuationDocument(cursorMap, querySpec,
															 cursorId, queryKind,
															 numIterations);
		hash_destroy(cursorMap);
	}
	else
	{
		StringInfo cursorStringInfo = makeStringInfo();
		appendStringInfo(cursorStringInfo, "cursor_%ld", cursorId);
		const char *cursorName = cursorStringInfo->data;

		bool isTopLevel = true;
		bool isHoldCursor = !IsInTransactionBlock(isTopLevel);
		persistConnection = isHoldCursor;
		bool closeCursor = false;
		queryFullyDrained = CreateAndDrainPersistedQuery(cursorName, query,
														 queryData->batchSize,
														 &numIterations,
														 accumulatedSize, &arrayWriter,
														 isHoldCursor, closeCursor);
		continuationDoc = queryFullyDrained ? NULL :
						  BuildPersistedContinuationDocument(cursorName, cursorId,
															 queryKind, numIterations);
	}

	return PostProcessCursorPage(fcinfo, &cursorDoc, &arrayWriter, &writer, cursorId,
								 continuationDoc, persistConnection);
}


/*
 * Serializes a cursor document that can be reused by getMore for a streaming query.
 */
static pgbson *
BuildStreamingContinuationDocument(HTAB *cursorMap, pgbson *querySpec, int64_t cursorId,
								   QueryKind queryKind, int numIterations)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendInt32(&writer, "qk", 2, (int) queryKind);

	/* Add the original query spec so that getMore can reuse it */
	PgbsonWriterAppendDocument(&writer, "qc", 2, querySpec);

	PgbsonWriterAppendInt64(&writer, "qi", 2, cursorId);

	SerializeContinuationsToWriter(&writer, cursorMap);

	/* In the response add the number of iterations (used in tests) */
	PgbsonWriterAppendInt32(&writer, "numIters", 8, numIterations);

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Serializes a cursor document that can be reused by getMore for a persitent query.
 */
static pgbson *
BuildPersistedContinuationDocument(const char *cursorName, int64_t cursorId, QueryKind
								   queryKind, int numIterations)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	/* Add the original query spec so that getMore can reuse it */
	PgbsonWriterAppendInt32(&writer, "qk", 2, (int) queryKind);

	PgbsonWriterAppendUtf8(&writer, "qn", 2, cursorName);

	PgbsonWriterAppendInt64(&writer, "qi", 2, cursorId);

	/* In the response add the number of iterations (used in tests) */
	PgbsonWriterAppendInt32(&writer, "numIters", 8, numIterations);

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
						break;
					}

					/* Query cursor name */
					case 'n':
					{
						Assert(pathKey[2] == '\0');
						getMoreInfo->cursorName = bson_iter_utf8(&cursorSpecIter, NULL);
						getMoreInfo->cursorKind = CursorKind_Persisted;
						break;
					}

					/* Query cursor id */
					case 'i':
					{
						Assert(pathKey[2] == '\0');
						getMoreInfo->cursorId = bson_iter_int64(&cursorSpecIter);
						break;
					}

					/* Query cursor kind */
					case 'k':
					{
						Assert(pathKey[2] == '\0');
						getMoreInfo->queryKind = (QueryKind) bson_iter_int32(
							&cursorSpecIter);
						break;
					}
				}

				break;
			}
		}
	}
}


/*
 * Parses the getMore spec and builds the necessary pipeline/query information from a cursor standpoint.
 */
static void
ParseGetMoreSpec(text *databaseName, pgbson *getMoreSpec, pgbson *cursorSpec,
				 QueryGetMoreInfo *getMoreInfo)
{
	/* Default batchSize for getMore */
	getMoreInfo->queryData.batchSize = INT_MAX;

	ParseCursorInputSpec(cursorSpec, getMoreInfo);

	/* Parses the wire protocol getMore */
	int64_t cursorId = ParseGetMore(databaseName, getMoreSpec, &getMoreInfo->queryData);
	if (cursorId != getMoreInfo->cursorId)
	{
		ereport(ERROR, (errmsg(
							"CursorID from GetMore does not match from cursor state, getMore: %ld, cursorState %ld",
							cursorId, getMoreInfo->cursorId)));
	}
}


/*
 * Creates a unique cursorId if one isn't provided.
 * We just use virtual x-id since that's going to be unique per query
 * within a node.
 */
static int64_t
GenerateCursorId(void)
{
	/* 2^53-1 masks integer precision of IEEE 754 double precision floating point numbers
	 * Works around issue with certain versions of the NodeJS driver
	 */
	char cursorBuffer[8];

	/* This is the same logic UUID generation uses - we should be good here */
	if (!pg_strong_random(cursorBuffer, 8))
	{
		ereport(ERROR, (errmsg("Unable to generate a unique cursor id")));
	}

	int64_t cursorId = *(int64_t *) cursorBuffer;
	return (cursorId & CursorAcceptableBitsMask);
}
