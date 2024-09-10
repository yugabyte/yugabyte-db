/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/db_stats.c
 *
 * Implementation of the dbStats command.
 *-------------------------------------------------------------------------
 */
#include <math.h>
#include <postgres.h>
#include <executor/spi.h>
#include <fmgr.h>
#include <funcapi.h>
#include <utils/guc.h>
#include <utils/builtins.h>
#include "utils/lsyscache.h"
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <miscadmin.h>
#include <sys/statvfs.h>

#include "utils/helio_errors.h"
#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "metadata/index.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "planner/helio_planner.h"
#include "commands/diagnostic_commands_common.h"


PG_FUNCTION_INFO_V1(command_db_stats);
PG_FUNCTION_INFO_V1(command_db_stats_worker);


/*
 * Represents bson response that needs to be returned for a dbStats command
 * All sizes are in Bytes by default, but some sizes are scaled using the 'scale' input param.
 */
typedef struct
{
	char *db;
	int64 collections;              /* total number of collections in db */
	int64 views;                    /* total number of views in db */
	int64 objects;                  /* total number of documents in db */
	double avgObjSize;              /* "dataSize" divided by "objects" */
	double dataSize;                /* Total size of the uncompressed data held in the database */
	double storageSize;             /* does not include index size */
	int64 indexes;
	double indexSize;
	double totalSize;               /* storageSize + indexSize */
	int32 scaleFactor;
	int ok;
} DbStatsResult;

/* Forward Declaration */

static pgbson * DbStatsCoordinator(Datum databaseName, int32 scale);
static pgbson * DbStatsWorker(void *fcinfoPointer);
static void BuildResultData(Datum databaseName, DbStatsResult *result, int32 scale);
static pgbson * BuildResponseMessage(DbStatsResult *result);
static void MergeWorkerResults(DbStatsResult *result, List *workerResults, int32 scale);
static void GetAllMongoCollectionShardOidsAndNamesInDB(ArrayType *collectionIdArray,
													   ArrayType **shardIdArray,
													   ArrayType **shardNames);
static void GetPostgresRelationSizes(ArrayType *relationIds, int64 *totalRelationSize,
									 int64 *totalTableSize);
static int64 GetPostgresDocumentCountStats(ArrayType *relationIds);
static int32 GetAverageDocumentSizeFromStats(ArrayType *relationIds);
static List * GetAllCollectionIdsInDb(Datum databaseNameDatum, int64 *views);


/*
 * command_db_stats is the implementation of the internal logic for
 * dbcommand/dbStats.
 */
Datum
command_db_stats(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("db name cannot be NULL")));
	}
	Datum databaseName = PG_GETARG_DATUM(0);

	if (PG_ARGISNULL(1))
	{
		/* Scale is optional in the dbStats command but here
		 * the sql method must provide scale to this C func */
		ereport(ERROR, (errmsg("scale cannot be NULL")));
	}
	double scaleDouble = PG_GETARG_FLOAT8(1);

	if (PG_ARGISNULL(2))
	{
		/* freeStorage is optional in the dbStats command but here
		 * the sql method must provide freeStorage to this C func */
		ereport(ERROR, (errmsg("freeStorage cannot be NULL")));
	}

	/* We don't yet support freeStrorage statistics, so skip reading freeStorage value */

	ReportFeatureUsage(FEATURE_COMMAND_DBSTATS);

	/* Truncate the fractional part of the scale */
	scaleDouble = trunc(scaleDouble);

	/* MongoDB docs don't mention this, but behaviour is to cap 'scale' to int32 */
	int32 scale = scaleDouble > INT32_MAX ? INT32_MAX :
				  scaleDouble < INT32_MIN ? INT32_MIN :
				  (int32) scaleDouble;

	pgbson *response = DbStatsCoordinator(databaseName, scale);
	PG_RETURN_POINTER(response);
}


/*
 * Top level entry point for dbStats when executing on the worker node.
 * Gathers statistics needed in the worker that can be merged by the coordinator.
 */
Datum
command_db_stats_worker(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("collectionId array can not be NULL")));
	}

	pgbson *response = RunWorkerDiagnosticLogic(&DbStatsWorker, fcinfo);
	PG_RETURN_POINTER(response);
}


/*
 * The core logic for dbStats on the entry point function
 * i.e. the Coordinator of the dbStats.
 * This function simply calls the "worker" dbStats against
 * all available nodes, and then aggregates/merges the results into
 * the necessary wire protocol format.
 */
static pgbson *
DbStatsCoordinator(Datum databaseName, int32 scale)
{
	if (scale < 1)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_LOCATION51024), errmsg(
							"scale has to be > 0")));
	}

	StringInfo namespaceString = makeStringInfo();

	appendStringInfo(namespaceString, "%.*s",
					 (int) VARSIZE_ANY_EXHDR(databaseName),
					 (char *) VARDATA_ANY(databaseName));

	DbStatsResult result = { 0 };
	result.db = namespaceString->data;
	result.scaleFactor = scale;
	result.ok = 1;

	BuildResultData(databaseName, &result, scale);
	pgbson *response = BuildResponseMessage(&result);

	return response;
}


/*
 * Helper method on the coordinator that populates the
 * DbStatsResult with the merged worker statistics (along with any
 * additional statistics that may be needed from the coordinator).
 */
static void
BuildResultData(Datum databaseName, DbStatsResult *result, int32 scale)
{
	int64 numViews = 0;
	List *collectionIdsList = GetAllCollectionIdsInDb(databaseName, &numViews);
	int64 numCollections = list_length(collectionIdsList);

	result->collections = numCollections;
	result->views = numViews;

	if (numCollections == 0)
	{
		/* When no collections, skip gathering data from workers */
		return;
	}

	Datum *collectionIdDatums = palloc0(sizeof(Datum) * numCollections);

	int64 i = 0;
	uint64 collectionId;
	ListCell *cell;
	foreach(cell, collectionIdsList)
	{
		CHECK_FOR_INTERRUPTS();
		collectionId = *(uint64 *) lfirst(cell);
		collectionIdDatums[i] = Int64GetDatum(collectionId);
		i++;
	}

	ArrayType *collectionIdArray = construct_array(collectionIdDatums, numCollections,
												   INT8OID,
												   sizeof(uint64), true,
												   TYPALIGN_INT);

	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT success, result FROM run_command_on_all_nodes("
					 "FORMAT($$ SELECT %s.db_stats_worker(%%L) $$, $1))",
					 ApiInternalSchemaName);

	int numValues = 1;
	Datum values[1] = { PointerGetDatum(collectionIdArray) };
	Oid types[1] = { INT8ARRAYOID };

	List *workerBsons = GetWorkerBsonsFromAllWorkers(cmdStr->data, values, types,
													 numValues, "DbStats");

	/* Now that we have the worker BSON results, merge them to the final one */
	MergeWorkerResults(result, workerBsons, scale);

	/* Get the count of Indexes for all the collections in array.
	 * Querying index metadata needs to happen on the coordinator since
	 * the index metadata is only present at coordinator
	 */
	result->indexes = CollectionIdsGetIndexCount(collectionIdArray);
}


/*
 * Gets the index sizes of indexes for shards located on the current node
 * for a given table and array of shard OIDs.
 */
static int64
GetIndexSizesWorker(ArrayType *relationIds)
{
	const char *query =
		"SELECT pg_catalog.pg_relation_size(indexrelid)::int8 FROM pg_catalog.pg_index "
		" WHERE indrelid = ANY ($1)";

	int nargs = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(relationIds) };

	bool readOnly = true;
	SPI_connect();

	Portal statsPortal = SPI_cursor_open_with_args("workerIndexSizeStats", query, nargs,
												   argTypes, argValues,
												   NULL, readOnly, 0);
	bool hasData = true;
	int64 indexesSize = 0;
	while (hasData)
	{
		SPI_cursor_fetch(statsPortal, true, INT_MAX);

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
				AttrNumber sizeAttribute = 1;
				Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
												  SPI_tuptable->tupdesc, sizeAttribute,
												  &isNull);
				if (isNull)
				{
					continue;
				}

				indexesSize += DatumGetInt64(resultDatum);
			}
		}
		else
		{
			ereport(ERROR, (errmsg(
								"DbStats tuple table was null for index size stats.")));
		}
	}

	SPI_cursor_close(statsPortal);
	SPI_finish();

	return indexesSize;
}


/*
 * BuildResponseMessage func builds the pgbson response for the dbStats() command
 */
static pgbson *
BuildResponseMessage(DbStatsResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendUtf8(&writer, "db", 2, result->db);
	PgbsonWriterAppendInt64(&writer, "collections", 11, result->collections);
	PgbsonWriterAppendInt64(&writer, "views", 5, result->views);
	PgbsonWriterAppendInt64(&writer, "objects", 7, result->objects);
	PgbsonWriterAppendDouble(&writer, "avgObjSize", 10, result->avgObjSize);
	PgbsonWriterAppendDouble(&writer, "dataSize", 8, result->dataSize);
	PgbsonWriterAppendDouble(&writer, "storageSize", 11, result->storageSize);
	PgbsonWriterAppendInt64(&writer, "indexes", 7, result->indexes);
	PgbsonWriterAppendDouble(&writer, "indexSize", 9, result->indexSize);
	PgbsonWriterAppendDouble(&writer, "totalSize", 9, result->totalSize);
	PgbsonWriterAppendInt32(&writer, "scaleFactor", 11, result->scaleFactor);
	PgbsonWriterAppendInt32(&writer, "ok", 2, result->ok);

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Given a List of bsons that were dispatched by the query workers,
 * and a given collection & scale, merges the results into the target
 * DbStatsResult struct.
 */
static void
MergeWorkerResults(DbStatsResult *result,
				   List *workerResults, int32 scale)
{
	/* To merge the results, we apply each shard's results consecutively until we have everything
	 * each field is processed by its intent
	 */

	ListCell *workerCell;

	int64 totalDocCount = 0;
	int64 totalAllDocsSize = 0;
	int64 totalStorageSize = 0;
	int64 totalIndexSize = 0;
	int64 totalTotalSize = 0;

	foreach(workerCell, workerResults)
	{
		pgbson *workerBson = lfirst(workerCell);
		bson_iter_t workerIter;
		PgbsonInitIterator(workerBson, &workerIter);

		int64 workerDocCount = 0;
		int32 workerAvgDocSize = 0;
		int64 workerStorageSize = 0;
		int64 workerIndexSize = 0;
		int64 workerTotalSize = 0;

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
			else if (strcmp(key, "total_rel_size") == 0)
			{
				/* associative - sum up across nodes */
				workerTotalSize = BsonValueAsInt64(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, "total_tbl_size") == 0)
			{
				/* associative - sum up across nodes */
				workerStorageSize = BsonValueAsInt64(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, "total_doc_count") == 0)
			{
				/* associative - sum up across nodes */
				workerDocCount = BsonValueAsInt64(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, "avg_doc_size") == 0)
			{
				workerAvgDocSize = BsonValueAsInt32(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, "indexes_size") == 0)
			{
				workerIndexSize = BsonValueAsInt64(bson_iter_value(&workerIter));
			}
			else
			{
				ereport(ERROR, (errmsg("unknown field received from dbStats worker %s",
									   key),
								errdetail_log(
									"unknown field received from dbStats worker %s",
									key)));
			}
		}

		if (errorMessage != NULL)
		{
			errorCode = errorCode == 0 ? ERRCODE_HELIO_INTERNALERROR : errorCode;
			ereport(ERROR, (errcode(errorCode),
							errmsg("Error running dbStats %s", errorMessage),
							errdetail_log("Error running dbStats %s", errorMessage)));
		}

		totalDocCount += workerDocCount;
		totalAllDocsSize += (workerAvgDocSize * workerDocCount);
		totalStorageSize += workerStorageSize;
		totalIndexSize += workerIndexSize;
		totalTotalSize += workerTotalSize;
	}

	double avgObjSize = totalDocCount == 0 ? 0 :
						(double) totalAllDocsSize / totalDocCount;

	/* Build Result Data */
	result->objects = totalDocCount;
	result->avgObjSize = avgObjSize;
	result->dataSize = totalDocCount * avgObjSize;
	result->storageSize = totalStorageSize;
	result->indexSize = totalIndexSize;
	result->totalSize = totalTotalSize;

	if (scale > 1)
	{
		result->dataSize /= scale;
		result->storageSize /= scale;
		result->indexSize /= scale;
		result->totalSize /= scale;
	}
}


/*
 * This is the core logic for db_stats that executes on
 * every node in the cluster.
 */
static pgbson *
DbStatsWorker(void *fcinfoPointer)
{
	PG_FUNCTION_ARGS = fcinfoPointer;
	ArrayType *collectionIdArray = PG_GETARG_ARRAYTYPE_P(0);

	/* First step, get the relevant shards on this node (We're already in the query worker) */
	ArrayType *shardNames = NULL;
	ArrayType *shardOids = NULL;
	GetAllMongoCollectionShardOidsAndNamesInDB(collectionIdArray, &shardOids,
											   &shardNames);

	/* Next get the relation and table size */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	/* Only do work if there are shards */
	if (shardOids != NULL)
	{
		Assert(shardNames != NULL);

		/*
		 * Given the relevant shard tables, get the total size of the overall table
		 * that is relevant to this node (sum up the table sizes across the shards
		 * located in this node).
		 */
		int64 totalRelationSize, totalTableSize;
		GetPostgresRelationSizes(shardOids, &totalRelationSize, &totalTableSize);

		/* Write it out to the target writer */
		PgbsonWriterAppendInt64(&writer, "total_rel_size", 14, totalRelationSize);
		PgbsonWriterAppendInt64(&writer, "total_tbl_size", 14, totalTableSize);

		/*
		 * Next get statistics details: Fetch document count from statistics
		 * We don't do runtime counts here - instead we do it in the coordinator
		 * if it's needed.
		 */
		int64 documentCount = GetPostgresDocumentCountStats(shardOids);

		PgbsonWriterAppendInt64(&writer, "total_doc_count", 15, documentCount);

		/*
		 * Look at statistics for document sizes.
		 */
		int32 workerAvgDocSize = GetAverageDocumentSizeFromStats(shardNames);
		PgbsonWriterAppendInt32(&writer, "avg_doc_size", 12, workerAvgDocSize);

		/*
		 * Get cumulative size of all indexes for given relationIds.
		 */
		int64 indexesSize = GetIndexSizesWorker(shardOids);
		PgbsonWriterAppendInt64(&writer, "indexes_size", 12, indexesSize);
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * This function returns a list of collectionId of all the collections in given db
 * It also fills the "views" pointer with the number of views on the collection.
 */
static List *
GetAllCollectionIdsInDb(Datum databaseNameDatum, int64 *views)
{
	List *collectionIdsList = NIL;
	const char *query =
		FormatSqlQuery("SELECT * FROM %s.collections WHERE database_name = $1",
					   ApiCatalogSchemaName);

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { databaseNameDatum };

	bool readOnly = true;
	MemoryContext priorMemoryContext = CurrentMemoryContext;

	SPI_connect();

	Portal statsPortal = SPI_cursor_open_with_args("workerDbStatsGetCollectionsInDb",
												   query, nargs,
												   argTypes, argValues,
												   NULL, readOnly, 0);
	bool hasData = true;
	while (hasData)
	{
		SPI_cursor_fetch(statsPortal, true, INT_MAX);

		hasData = SPI_processed >= 1;
		if (!hasData)
		{
			break;
		}

		if (SPI_tuptable)
		{
			for (int tupleNumber = 0; tupleNumber < (int) SPI_processed; tupleNumber++)
			{
				bool isNull = false;

				/* Attr 2 is collection_name */
				AttrNumber collectionNameAttr = 2;
				Datum collectionNameDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
														  SPI_tuptable->tupdesc,
														  collectionNameAttr,
														  &isNull);

				if (isNull)
				{
					continue;
				}

				if (strcmp(TextDatumGetCString(collectionNameDatum),
						   "system.dbSentinel") == 0)
				{
					/* Skip the sentinel*/
					continue;
				}

				/* Attr 3 is collection_id */
				AttrNumber collectionIdAttr = 3;
				Datum collectionIdDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
														SPI_tuptable->tupdesc,
														collectionIdAttr,
														&isNull);
				if (isNull)
				{
					ereport(ERROR, (errmsg(
										"collection_id should not be NULL in catalog")));
				}

				/* Attr 6 is view_definition */
				AttrNumber viewAttr = 6;
				SPI_getbinval(SPI_tuptable->vals[tupleNumber],
							  SPI_tuptable->tupdesc, viewAttr,
							  &isNull);
				if (isNull)
				{
					/* If view_definition is NULL it's a collection. Add it's collectionId to the list */
					MemoryContext spiContext = MemoryContextSwitchTo(priorMemoryContext);
					uint64 *collectionIdPtr = palloc(sizeof(uint64));
					*collectionIdPtr = DatumGetInt64(collectionIdDatum);
					collectionIdsList = lappend(collectionIdsList, collectionIdPtr);
					MemoryContextSwitchTo(spiContext);
				}
				else
				{
					/* If view_definition is not NULL, is a View, increase the view counter */
					(*views)++;
				}
			}
		}
		else
		{
			ereport(ERROR, (errmsg("%s.collections table was null for dbStats.",
								   ApiCatalogSchemaName)));
		}
	}

	SPI_cursor_close(statsPortal);
	SPI_finish();

	return collectionIdsList;
}


/*
 * Given an array of collecionIds, this function retrieves the shard OIDs and shard names that are
 * associated with given all collectionIds on the current node.
 */
static void
GetAllMongoCollectionShardOidsAndNamesInDB(ArrayType *collectionIdArray,
										   ArrayType **shardIdArray,
										   ArrayType **shardNames)
{
	*shardIdArray = NULL;
	*shardNames = NULL;

	Datum *collectionIdDatums;
	int numCollections;
	bool *nulls;
	deconstruct_array(collectionIdArray, INT8OID, sizeof(uint64), true, TYPALIGN_INT,
					  &collectionIdDatums, &nulls, &numCollections);

	Datum *relationIdDatums = palloc0(sizeof(Datum) * numCollections);

	uint64 collectionId;
	for (int i = 0; i < numCollections; i++)
	{
		CHECK_FOR_INTERRUPTS();
		collectionId = DatumGetInt64(collectionIdDatums[i]);
		relationIdDatums[i] = ObjectIdGetDatum(GetRelationIdForCollectionId(collectionId,
																			NoLock));
	}

	ArrayType *relationIdArray = construct_array(relationIdDatums, numCollections, OIDOID,
												 sizeof(Oid), true,
												 TYPALIGN_INT);

	/* Here the logicalrelid is of the format ApiDataSchemaName.documents_<collectionId>
	 *      e.g. helio_data.documents_1
	 * This query gets all the shardIds of given logicalrelid, and appends it to "documents_<collectionId>"
	 * part of the logicalrelid
	 *      e.g. documents_1_10240
	 */
	const char *query =
		"SELECT array_agg(right(logicalrelid::regclass::text,-11) || '_' || shardid) FROM pg_dist_shard WHERE logicalrelid = ANY($1)";

	int nargs = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(relationIdArray) };

	bool isReadOnly = true;
	bool isNull = false;
	Datum shardIds = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes,
														 argValues,
														 NULL, isReadOnly, SPI_OK_SELECT,
														 &isNull);

	if (isNull)
	{
		return;
	}

	ArrayType *arrayType = DatumGetArrayTypeP(shardIds);

	/* Need to build the result */
	int64 numItems = ArrayGetNItems(ARR_NDIM(arrayType), ARR_DIMS(arrayType));

	Datum *resultDatums = palloc0(sizeof(Datum) * numItems);
	Datum *resultNameDatums = palloc0(sizeof(Datum) * numItems);

	const int slice_ndim = 0;
	ArrayMetaState *mState = NULL;
	ArrayIterator shardIterator = array_create_iterator(arrayType,
														slice_ndim, mState);

	Datum shardName = 0;
	int64 resultCount = 0;
	while (array_iterate(shardIterator, &shardName, &isNull))
	{
		if (isNull)
		{
			continue;
		}

		RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, TextDatumGetCString(
											  shardName), -1);
		bool missingOk = true;
		Oid shardRelationId = RangeVarGetRelid(rangeVar, AccessShareLock, missingOk);
		if (shardRelationId != InvalidOid)
		{
			Assert(resultCount < numItems);
			resultDatums[resultCount] = shardRelationId;
			resultNameDatums[resultCount] = PointerGetDatum(DatumGetTextPCopy(
																shardName));
			resultCount++;
		}
	}

	array_free_iterator(shardIterator);

	/* Now that we have the shard list as a Datum*, create an array type */
	if (resultCount > 0)
	{
		*shardIdArray = construct_array(resultDatums, resultCount, OIDOID,
										sizeof(Oid), true,
										TYPALIGN_INT);
		*shardNames = construct_array(resultNameDatums, resultCount, TEXTOID, -1,
									  false,
									  TYPALIGN_INT);
	}

	pfree(resultDatums);
	pfree(resultNameDatums);
	pfree(arrayType);
}


/*
 * Gets the sum of the relation sizes and table sizes for the shards located on the current node
 * for a given relationIds. (the relationIds can be of different collections)
 */
static void
GetPostgresRelationSizes(ArrayType *relationIds, int64 *totalRelationSize,
						 int64 *totalTableSize)
{
	const char *query =
		"SELECT SUM(pg_catalog.pg_total_relation_size(r))::int8, SUM(pg_catalog.pg_table_size(r))::int8 FROM unnest($1) r";

	int nargs = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(relationIds) };

	bool readOnly = true;
	Datum resultValues[2];
	bool nullValues[2];
	int numResults = 2;
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(query, nargs, argTypes, argValues, NULL,
												  readOnly,
												  SPI_OK_SELECT, resultValues, nullValues,
												  numResults);

	*totalRelationSize = 0;
	*totalTableSize = 0;

	if (nullValues[0] || nullValues[1])
	{
		return;
	}

	*totalRelationSize = DatumGetInt64(resultValues[0]);
	*totalTableSize = DatumGetInt64(resultValues[1]);
}


/*
 * Gets the count of rows for the shards located on the current node
 * for a given array of relationIds from statistics.
 */
static int64
GetPostgresDocumentCountStats(ArrayType *relationIds)
{
	const char *query =
		"SELECT SUM(reltuples::int8)::int8 FROM pg_catalog.pg_class WHERE oid =ANY ($1) AND reltuples::int8 > 0";

	int nargs = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(relationIds) };
	bool readOnly = true;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													   NULL, readOnly, SPI_OK_SELECT,
													   &isNull);

	if (isNull)
	{
		return 0;
	}

	return DatumGetInt64(result);
}


/*
 * Gets the average size of rows for the shards located on the current node
 * for a given array of relationIds from statistics.
 */
static int32
GetAverageDocumentSizeFromStats(ArrayType *relationNames)
{
	const char *query =
		"SELECT AVG(s.avg_width)::int4 AS avg_width FROM pg_catalog.pg_stats s "
		" WHERE s.schemaname = $2 AND s.tablename =ANY ($1) and s.attname = 'document'";
	int nargs = 2;
	Oid argTypes[2] = { TEXTARRAYOID, TEXTOID };
	Datum argValues[2] = {
		PointerGetDatum(relationNames), CStringGetTextDatum(ApiDataSchemaName)
	};
	bool readOnly = true;

	bool isNull = false;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													   NULL, readOnly, SPI_OK_SELECT,
													   &isNull);

	if (isNull)
	{
		return 0;
	}

	return DatumGetInt32(result);
}
