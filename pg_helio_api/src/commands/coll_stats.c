/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/commands/coll_stats.c
 *
 * Implementation of the collStats command.
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

#include "utils/mongo_errors.h"
#include "metadata/collection.h"
#include "metadata/index.h"
#include "metadata/metadata_cache.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "planner/helio_planner.h"
#include "utils/hashset_utils.h"
#include "utils/version_utils.h"
#include "commands/parse_error.h"
#include "commands/coll_stats.h"
#include "commands/commands_common.h"
#include "commands/diagnostic_commands_common.h"

extern int CollStatsCountPolicyThreshold;

PG_FUNCTION_INFO_V1(command_coll_stats);
PG_FUNCTION_INFO_V1(command_coll_stats_worker);
PG_FUNCTION_INFO_V1(command_coll_stats_aggregation);


/*
 * Represents bson response that needs to be returned for a collStats command
 * All sizes are in Bytes by default, but some sizes are scaled using the 'scale' input param.
 */
typedef struct
{
	char *ns;
	int64 size;
	int64 count;
	int32 avgObjSize;
	int64 totalIndexSize;
	pgbson *indexSizes;
	int64 storageSize;
	int32 nindexes;
	List *indexBuilds;
	int64 totalSize;
	int32 scaleFactor;
	int32 ok;
} CollStatsResult;

typedef enum CollStatsAggMode
{
	CollStatsAggMode_None = 0x0,

	CollStatsAggMode_Count = 0x1,

	CollStatsAggMode_Storage = 0x2
} CollStatsAggMode;

/* Forward Declaration */
static int64 GetDocumentsCountRunTime(MongoCollection *collection);
static int32 GetAverageColumnWidthRuntime(MongoCollection *collection);
static pgbson * BuildResponseMessage(CollStatsResult *result);
static void WriteCoreStorageStats(CollStatsResult *result, pgbson_writer *writer);
static pgbson * BuildEmptyResponseMessage(CollStatsResult *result);
static void BuildResultData(Datum databaseName, Datum collectionName,
							CollStatsResult *result,
							MongoCollection *collection, int32 scale);


static pgbson * CollStatsCoordinator(Datum databaseName, Datum collectionName, int scale);
static void MergeWorkerResults(CollStatsResult *result, MongoCollection *collection,
							   List *workerResults, int scale);
static pgbson * MergeWorkerIndexDocs(MongoCollection *collection, List *workerIndexDocs,
									 int32 scale, int *indexCount);
static pgbson * CollStatsWorker(void *fcinfoPointer);
static void GetPostgresRelationSizes(ArrayType *relationIds, int64 *totalRelationSize,
									 int64 *totalTableSize);
static int64 GetPostgresDocumentCountStats(ArrayType *relationIds,
										   bool *isSmallCollection);
static int32 GetAverageDocumentSizeFromStats(ArrayType *relationIds);
static void WriteIndexSizesScaledWorker(ArrayType *relationIds, pgbson_writer *writer);
static inline void WriteStatsAsIntOrDouble(pgbson_writer *writer, char *fieldName, int
										   size, int64 value);

/*
 * command_coll_stats is the implementation of the internal logic for
 * dbcommand/collStats.
 */
Datum
command_coll_stats(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("db name cannot be NULL")));
	}
	Datum databaseName = PG_GETARG_DATUM(0);

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("collection name cannot be NULL")));
	}
	Datum collectionName = PG_GETARG_DATUM(1);

	if (PG_ARGISNULL(2))
	{
		/* Scale is optional in the collStats command but here
		 * the sql method must provide scale to this C func */
		ereport(ERROR, (errmsg("scale cannot be NULL")));
	}
	double scaleDouble = PG_GETARG_FLOAT8(2);

	ReportFeatureUsage(FEATURE_COMMAND_COLLSTATS);

	/* Truncate the fractional part of the scale */
	scaleDouble = trunc(scaleDouble);

	/* MongoDB docs don't mention this, but behaviour is to cap 'scale' to int32 */
	int32 scale = scaleDouble > INT32_MAX ? INT32_MAX :
				  scaleDouble < INT32_MIN ? INT32_MIN :
				  (int32) scaleDouble;

	pgbson *response = CollStatsCoordinator(databaseName, collectionName, scale);
	PG_RETURN_POINTER(response);
}


/*
 * command_coll_stats_aggregation is the implementation of the internal logic for
 * dbcommand/collStats for the $collStats aggregation pipeline.
 */
Datum
command_coll_stats_aggregation(PG_FUNCTION_ARGS)
{
	Datum databaseName = PG_GETARG_DATUM(0);
	Datum collectionName = PG_GETARG_DATUM(1);
	pgbson *collStatsSpec = PG_GETARG_PGBSON(2);

	/* We either support count or storage stats currently */
	bson_iter_t collStatsSpecIter;
	PgbsonInitIterator(collStatsSpec, &collStatsSpecIter);

	int64 storageScale = 1;
	CollStatsAggMode aggregateMode = CollStatsAggMode_None;
	while (bson_iter_next(&collStatsSpecIter))
	{
		const char *key = bson_iter_key(&collStatsSpecIter);
		const bson_value_t *value = bson_iter_value(&collStatsSpecIter);

		if (strcmp(key, "latencyStats") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("collStats with latencyStats not supported yet")));
		}
		else if (strcmp(key, "storageStats") == 0)
		{
			/* This is fine: validate */
			EnsureTopLevelFieldValueType("$collStats.storageStats", value,
										 BSON_TYPE_DOCUMENT);

			bson_iter_t storageStatsIter;
			pgbsonelement storageStatsElement = { 0 };
			BsonValueInitIterator(value, &storageStatsIter);
			if (IsBsonValueEmptyDocument(value))
			{
				storageScale = 1;
			}
			else if (!TryGetSinglePgbsonElementFromBsonIterator(&storageStatsIter,
																&storageStatsElement) ||
					 strcmp(storageStatsElement.path, "scale") != 0 ||
					 !BsonValueIsNumber(&storageStatsElement.bsonValue))
			{
				ereport(ERROR, (errcode(MongoBadValue),
								errmsg(
									"storageStats must be an empty document or have a single numeric field 'scale'")));
			}
			else
			{
				storageScale = BsonValueAsInt64(&storageStatsElement.bsonValue);
			}

			aggregateMode |= CollStatsAggMode_Storage;
		}
		else if (strcmp(key, "count") == 0)
		{
			/* This is fine: validate */
			EnsureTopLevelFieldValueType("$collStats.count", value, BSON_TYPE_DOCUMENT);
			aggregateMode |= CollStatsAggMode_Count;
		}
		else if (strcmp(key, "queryExecStats") == 0)
		{
			ereport(ERROR, (errcode(MongoCommandNotSupported),
							errmsg("collStats with queryExecStats not supported yet")));
		}
		else
		{
			ereport(ERROR, (errcode(MongoUnknownBsonField),
							errmsg("BSON field $collStats.%s is an unknown field", key),
							errhint("BSON field $collStats.%s is an unknown field",
									key)));
		}
	}

	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	/* Write the base info */
	StringInfo namespaceString = makeStringInfo();
	appendStringInfo(namespaceString, "%.*s.%.*s",
					 (int) VARSIZE_ANY_EXHDR(databaseName),
					 (char *) VARDATA_ANY(databaseName),
					 (int) VARSIZE_ANY_EXHDR(collectionName),
					 (char *) VARDATA_ANY(collectionName));
	PgbsonWriterAppendUtf8(&writer, "ns", 2, namespaceString->data);

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseName,
									  collectionName,
									  AccessShareLock);
	if (collection == NULL)
	{
		ereport(ERROR, (errcode(MongoNamespaceNotFound),
						errmsg("Collection [%s] not found.", namespaceString->data)));
	}

	if (collection->viewDefinition != NULL)
	{
		/* storageStats & count not supported on views */
		ereport(ERROR, (errcode(MongoCommandNotSupportedOnView),
						errmsg("Namespace %s is a view, not a collection",
							   namespaceString->data)));
	}

	CollStatsResult result = { 0 };
	BuildResultData(databaseName, collectionName, &result, collection, storageScale);
	if ((aggregateMode & CollStatsAggMode_Count) != 0)
	{
		if (result.count < INT32_MAX)
		{
			PgbsonWriterAppendInt32(&writer, "count", 5, (int32_t) result.count);
		}
		else
		{
			PgbsonWriterAppendInt64(&writer, "count", 5, result.count);
		}
	}

	if ((aggregateMode & CollStatsAggMode_Storage) != 0)
	{
		pgbson_writer childWriter;
		PgbsonWriterStartDocument(&writer, "storageStats", 12, &childWriter);
		WriteCoreStorageStats(&result, &childWriter);
		PgbsonWriterEndDocument(&writer, &childWriter);
	}

	PG_RETURN_POINTER(PgbsonWriterGetPgbson(&writer));
}


/*
 * Top level entry point for collstats when executing on the worker node.
 * Gathers statistics needed in the worker that can be merged by the coordinator.
 */
Datum
command_coll_stats_worker(PG_FUNCTION_ARGS)
{
	if (PG_ARGISNULL(0))
	{
		ereport(ERROR, (errmsg("db name cannot be NULL")));
	}

	if (PG_ARGISNULL(1))
	{
		ereport(ERROR, (errmsg("collection name cannot be NULL")));
	}

	pgbson *response = RunWorkerDiagnosticLogic(&CollStatsWorker, fcinfo);
	PG_RETURN_POINTER(response);
}


/*
 * The core logic for coll stats on the entry point function
 * i.e. the Coordinator of the coll stats.
 * This function simply calls the "worker" collstats against
 * all available nodes, and then aggregates/merges the results into
 * the necessary wire protocol format.
 */
static pgbson *
CollStatsCoordinator(Datum databaseName, Datum collectionName, int scale)
{
	if (scale < 1)
	{
		ereport(ERROR, (errcode(MongoLocation51024), errmsg(
							"BSON field 'scale' value must be >= 1, actual value '%d'",
							scale)));
	}

	StringInfo namespaceString = makeStringInfo();

	appendStringInfo(namespaceString, "%.*s.%.*s",
					 (int) VARSIZE_ANY_EXHDR(databaseName),
					 (char *) VARDATA_ANY(databaseName),
					 (int) VARSIZE_ANY_EXHDR(collectionName),
					 (char *) VARDATA_ANY(collectionName));

	CollStatsResult result;
	result.ns = namespaceString->data;
	result.scaleFactor = scale;
	result.ok = 1;

	pgbson *response;
	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseName,
									  collectionName,
									  AccessShareLock);
	if (collection == NULL)
	{
		response = BuildEmptyResponseMessage(&result);
	}
	else
	{
		BuildResultData(databaseName, collectionName, &result, collection, scale);
		response = BuildResponseMessage(&result);
	}

	return response;
}


/*
 * Helper method on the coordinator that populates the
 * CollStatsResult with the merged worker statistics (along with any
 * additional statistics that may be needed from the coordinator).
 */
static void
BuildResultData(Datum databaseName, Datum collectionName, CollStatsResult *result,
				MongoCollection *collection, int32 scale)
{
	List *workerBsons;
	if (DefaultInlineWriteOperations)
	{
		Datum resultDatum = DirectFunctionCall3(command_coll_stats_worker,
												databaseName, collectionName,
												Int32GetDatum(scale));
		workerBsons = list_make1(DatumGetPgBsonPacked(resultDatum));
	}
	else
	{
		StringInfo cmdStr = makeStringInfo();
		appendStringInfo(cmdStr,
						 "SELECT success, result FROM run_command_on_all_nodes("
						 "FORMAT($$ SELECT %s.coll_stats_worker(%%L, %%L, %d) $$, $1, $2))",
						 ApiToApiInternalSchemaName, scale);

		int numValues = 2;
		Datum values[2] = { databaseName, collectionName };
		Oid types[2] = { TEXTOID, TEXTOID };

		workerBsons = GetWorkerBsonsFromAllWorkers(cmdStr->data, values, types,
												   numValues, "CollStats");
	}

	/* Now that we have the worker BSON results, merge them to the final one */
	MergeWorkerResults(result, collection, workerBsons, scale);
}


/*
 * Given a List of bsons that were dispatched by the query workers,
 * and a given collection & scale, merges the results into the target
 * CollStatsResult struct.
 */
static void
MergeWorkerResults(CollStatsResult *result, MongoCollection *collection,
				   List *workerResults, int scale)
{
	/* To merge the results, we apply each shard's results consecutively until we have everything
	 * each field is processed by its intent
	 */

	ListCell *workerCell;

	result->totalSize = 0;
	result->storageSize = 0;
	int64 totalDocCount = 0;
	int64 totalDocColumnSize = 0;
	List *indexDocs = NIL;

	foreach(workerCell, workerResults)
	{
		pgbson *workerBson = lfirst(workerCell);
		bson_iter_t workerIter;
		PgbsonInitIterator(workerBson, &workerIter);

		int64 workerTotalDocCount = 0;
		int32 averageDocSize = 0;

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
				int64 value = BsonValueAsInt64(bson_iter_value(&workerIter));
				result->totalSize += value;
			}
			else if (strcmp(key, "total_tbl_size") == 0)
			{
				/* associative - sum up across nodes */
				int64 value = BsonValueAsInt64(bson_iter_value(&workerIter));
				result->storageSize += value;
			}
			else if (strcmp(key, "total_doc_count") == 0)
			{
				/*
				 * Sum up from total docs - note we don't persist this
				 * since we may need to go to the runtime.
				 */
				workerTotalDocCount = BsonValueAsInt64(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, "avg_doc_size") == 0)
			{
				averageDocSize = BsonValueAsInt32(bson_iter_value(&workerIter));
			}
			else if (strcmp(key, "index_sizes") == 0)
			{
				bson_value_t *value = palloc(sizeof(bson_value_t));
				*value = *bson_iter_value(&workerIter);
				indexDocs = lappend(indexDocs, value);
			}
			else
			{
				ereport(ERROR, (errmsg("unknown field received from collstats worker %s",
									   key)));
			}
		}

		if (errorMessage != NULL)
		{
			errorCode = errorCode == 0 ? MongoInternalError : errorCode;
			ereport(ERROR, (errcode(errorCode), errmsg("Error running collstats %s",
													   errorMessage)));
		}

		totalDocColumnSize += (averageDocSize * workerTotalDocCount);
		totalDocCount += workerTotalDocCount;
	}

	bool isSmallCollection = false;
	if (totalDocCount < CollStatsCountPolicyThreshold)
	{
		ereport(DEBUG1, (errmsg(
							 "[collStats] Small collection. count/avgObjSize are evaluate at runtime.")));
		totalDocCount = GetDocumentsCountRunTime(collection);
		isSmallCollection = true;
	}
	else
	{
		ereport(DEBUG1, (errmsg(
							 "[collStats] Big collection. count/avgObjSize are taken from stats")));
	}

	/* Gather relevant data */
	int32 avgObjSize = totalDocCount == 0 ? 0 :
					   isSmallCollection ? GetAverageColumnWidthRuntime(collection) :
					   (int32) (totalDocColumnSize / totalDocCount);

	/* Build Result Data */
	result->count = totalDocCount;
	result->avgObjSize = avgObjSize;
	result->size = (totalDocCount * avgObjSize) / (int64) scale;
	result->totalIndexSize = (result->totalSize - result->storageSize) / (int64) scale;
	result->totalSize = result->totalSize / (int64) scale;
	result->storageSize = result->storageSize / (int64) scale;


	bool excludeIdIndex = false;
	bool inProgressOnly = true;
	List *indexBuilds = CollectionIdGetIndexNames(collection->collectionId,
												  excludeIdIndex, inProgressOnly);
	result->indexSizes = MergeWorkerIndexDocs(collection, indexDocs, scale,
											  &result->nindexes);
	result->indexBuilds = indexBuilds;
}


/*
 * As part of the worker merge, each worker sends back a list of index sizes.
 * Here we need to merge all the index sizes into a common document
 * that is ordered by index name from the collection_indexes table (for stability).
 */
static pgbson *
MergeWorkerIndexDocs(MongoCollection *collection, List *workerIndexDocs, int32 scale,
					 int *indexCount)
{
	*indexCount = 0;
	HTAB *bsonElementHash = CreatePgbsonElementHashSet();

	/* First run through the worker index docs -
	 * For each doc, add the { "indexName": (int64)indexSize }
	 * into the bsonElement hash. If the entry for that indexName
	 * already exists, add to the existing size.
	 */
	ListCell *indexCell;
	foreach(indexCell, workerIndexDocs)
	{
		bson_value_t *value = lfirst(indexCell);
		bson_iter_t indexDocIter;
		BsonValueInitIterator(value, &indexDocIter);

		while (bson_iter_next(&indexDocIter))
		{
			pgbsonelement element = { 0 };
			element.path = bson_iter_key(&indexDocIter);
			element.pathLength = bson_iter_key_len(&indexDocIter);
			element.bsonValue = *bson_iter_value(&indexDocIter);

			bool found = false;
			pgbsonelement *foundVal = hash_search(bsonElementHash, &element, HASH_ENTER,
												  &found);
			if (found)
			{
				bool overflowedIgnore = false;
				AddNumberToBsonValue(&foundVal->bsonValue, &element.bsonValue,
									 &overflowedIgnore);
			}
		}
	}

	/*
	 * Get the existing set of names (ordered) from the collection_indexes
	 * table.
	 */
	bool excludeIdIndex = false;
	bool inProgressOnly = false;
	List *indexNames = CollectionIdGetIndexNames(collection->collectionId, excludeIdIndex,
												 inProgressOnly);
	*indexCount = list_length(indexNames);

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	ListCell *cell;

	/*
	 * Now write out the aggregate sizes of the indexes in the order in which the indexes
	 * are returned from the collection_indexes table.
	 */
	foreach(cell, indexNames)
	{
		pgbsonelement elem = { 0 };
		elem.path = lfirst(cell);
		elem.pathLength = strlen(elem.path);
		bool found;
		pgbsonelement *foundElement = hash_search(bsonElementHash, &elem, HASH_FIND,
												  &found);
		if (found)
		{
			int64 totalSize = BsonValueAsInt64(&foundElement->bsonValue);
			totalSize = totalSize / (int64) scale;
			PgbsonWriterAppendInt64(&writer, foundElement->path, foundElement->pathLength,
									totalSize);
		}
	}

	/* tidy */
	hash_destroy(bsonElementHash);
	list_free_deep(indexNames);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * This is the core logic for coll_stats that executes on
 * every node in the cluster.
 */
static pgbson *
CollStatsWorker(void *fcinfoPointer)
{
	PG_FUNCTION_ARGS = fcinfoPointer;
	Datum databaseName = PG_GETARG_DATUM(0);
	Datum collectionName = PG_GETARG_DATUM(1);

	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseName,
									  collectionName,
									  AccessShareLock);

	if (collection == NULL)
	{
		ereport(ERROR, (errcode(MongoInvalidNamespace),
						errmsg("Collection not found")));
	}

	/* First step, get the relevant shards on this node (We're already in the query worker) */
	ArrayType *shardNames = NULL;
	ArrayType *shardOids = NULL;
	if (DefaultInlineWriteOperations)
	{
		int singleShardCount = 1;
		Datum resultDatums[1] = { ObjectIdGetDatum(collection->collectionId) };
		Datum resultNames[1] = { CStringGetTextDatum(collection->shardTableName) };
		shardOids = construct_array(resultDatums, singleShardCount, OIDOID,
									sizeof(Oid), true,
									TYPALIGN_INT);
		shardNames = construct_array(resultNames, singleShardCount, TEXTOID, -1,
									 false,
									 TYPALIGN_INT);
	}
	else
	{
		GetMongoCollectionShardOidsAndNames(collection, &shardOids, &shardNames);
	}

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
		bool isSmallCollection = false;
		int64 documentCount = GetPostgresDocumentCountStats(shardOids,
															&isSmallCollection);

		PgbsonWriterAppendInt64(&writer, "total_doc_count", 15, documentCount);

		/*
		 * Look at statistics for document sizes.
		 */
		int32 averageDocSize = GetAverageDocumentSizeFromStats(shardNames);

		PgbsonWriterAppendInt32(&writer, "avg_doc_size", 12, averageDocSize);

		/*
		 * Walk the indexes for these shards and write out their sizes.
		 */
		pgbson_writer indexWriter;
		PgbsonWriterStartDocument(&writer, "index_sizes", 11, &indexWriter);
		WriteIndexSizesScaledWorker(shardOids, &indexWriter);
		PgbsonWriterEndDocument(&writer, &indexWriter);
	}

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Given a mongo collection, retrieves the shard OIDs and shard names that are associated with
 * that table on the current node.
 */
void
GetMongoCollectionShardOidsAndNames(MongoCollection *collection, ArrayType **shardIdArray,
									ArrayType **shardNames)
{
	*shardIdArray = NULL;
	*shardNames = NULL;
	const char *query =
		"SELECT array_agg($2 || '_' || shardid) FROM pg_dist_shard WHERE logicalrelid = $1";

	int nargs = 2;
	Oid argTypes[2] = { OIDOID, TEXTOID };
	Datum argValues[2] = {
		ObjectIdGetDatum(collection->relationId), CStringGetTextDatum(
			collection->tableName)
	};
	bool isReadOnly = true;
	bool isNull = true;
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
	int numItems = ArrayGetNItems(ARR_NDIM(arrayType), ARR_DIMS(arrayType));
	Datum *resultDatums = palloc0(sizeof(Datum) * numItems);
	Datum *resultNameDatums = palloc0(sizeof(Datum) * numItems);
	int resultCount = 0;

	const int slice_ndim = 0;
	ArrayMetaState *mState = NULL;
	ArrayIterator shardIterator = array_create_iterator(arrayType,
														slice_ndim, mState);

	Datum shardName = 0;
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
 * for a given table and array of shards.
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
 * for a given table and array of shards from statistics.
 */
static int64
GetPostgresDocumentCountStats(ArrayType *relationIds, bool *isSmallCollection)
{
	const char *query =
		"SELECT SUM(reltuples)::int8 FROM pg_catalog.pg_class WHERE oid =ANY ($1) AND reltuples > 0";

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
		*isSmallCollection = true;
		return 0;
	}

	int64 countStatistics = DatumGetInt64(result);
	*isSmallCollection = countStatistics < CollStatsCountPolicyThreshold;
	return countStatistics;
}


/*
 * Gets the average size of rows for the shards located on the current node
 * for a given table and array of shards from statistics.
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


/*
 * Gets the index sizes of indexes for shards located on the current node
 * for a given table and array of shard OIDs.
 */
static void
WriteIndexSizesScaledWorker(ArrayType *relationIds, pgbson_writer *writer)
{
	const char *query =
		"SELECT indexrelid, pg_catalog.pg_relation_size(indexrelid)::int8 FROM pg_catalog.pg_index "
		" WHERE indrelid =ANY ($1)";

	int nargs = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(relationIds) };

	bool readOnly = true;
	MemoryContext priorMemoryContext = CurrentMemoryContext;

	HTAB *indexHash = CreatePgbsonElementHashSet();
	SPI_connect();

	Portal statsPortal = SPI_cursor_open_with_args("workerIndexSizeStats", query, nargs,
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
				bool isNull;
				AttrNumber indexIdAttribute = 1;
				Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
												  SPI_tuptable->tupdesc, indexIdAttribute,
												  &isNull);
				if (isNull)
				{
					continue;
				}

				Oid indexOid = DatumGetObjectId(resultDatum);

				AttrNumber sizeAttribute = 2;
				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, sizeAttribute,
											&isNull);
				if (isNull)
				{
					continue;
				}

				int64 indexSize = DatumGetInt64(resultDatum);

				/* Now write the result */
				MemoryContext spiContext = MemoryContextSwitchTo(priorMemoryContext);

				bool useLibPq = false;
				const char *mongoIndexName = ExtensionIndexOidGetIndexName(indexOid,
																		   useLibPq);
				if (mongoIndexName != NULL)
				{
					pgbsonelement element = { 0 };
					element.path = mongoIndexName;
					element.pathLength = strlen(mongoIndexName);
					element.bsonValue.value_type = BSON_TYPE_INT64;
					element.bsonValue.value.v_int64 = indexSize;

					bool found = false;
					pgbsonelement *foundElement = hash_search(indexHash, &element,
															  HASH_ENTER, &found);
					if (found)
					{
						bool overflowedIgnore;
						AddNumberToBsonValue(&foundElement->bsonValue, &element.bsonValue,
											 &overflowedIgnore);
					}
				}

				MemoryContextSwitchTo(spiContext);
			}
		}
		else
		{
			ereport(ERROR, (errmsg(
								"CollStats tuple table was null for index size stats.")));
		}
	}

	SPI_cursor_close(statsPortal);
	SPI_finish();

	HASH_SEQ_STATUS seq_status;
	pgbsonelement *entry;

	hash_seq_init(&seq_status, indexHash);
	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		int64 totalSize = BsonValueAsInt64(&entry->bsonValue);
		PgbsonWriterAppendInt64(writer, entry->path, entry->pathLength, totalSize);
	}

	hash_destroy(indexHash);
}


/*
 * BuildEmptyResponseMessage func builds pgbson response for collStats() when
 * the provided collection is not present
 */
static pgbson *
BuildEmptyResponseMessage(CollStatsResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	/*
	 * Note: Compared to non-empty response, MongoDB returns fewer fields in empty response,
	 * and the sequence of fields is also not aligned with non-empty response.
	 * Setting the writer to write Int32 as empty collection can fit within int32 range
	 */
	PgbsonWriterAppendUtf8(&writer, "ns", 2, result->ns);
	PgbsonWriterAppendInt32(&writer, "size", 4, 0);
	PgbsonWriterAppendInt32(&writer, "count", 5, 0);
	PgbsonWriterAppendInt32(&writer, "storageSize", 11, 0);
	PgbsonWriterAppendInt32(&writer, "totalSize", 9, 0);
	PgbsonWriterAppendInt32(&writer, "nindexes", 8, 0);
	PgbsonWriterAppendInt32(&writer, "totalIndexSize", 14, 0);
	PgbsonWriterAppendDocument(&writer, "indexSizes", 10, PgbsonInitEmpty());
	PgbsonWriterAppendInt32(&writer, "scaleFactor", 11, result->scaleFactor);
	PgbsonWriterAppendInt32(&writer, "ok", 2, result->ok);

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * BuildResponseMessage func builds the pgbson response for the collStats() command
 */
static pgbson *
BuildResponseMessage(CollStatsResult *result)
{
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	PgbsonWriterAppendUtf8(&writer, "ns", 2, result->ns);
	WriteCoreStorageStats(result, &writer);
	PgbsonWriterAppendInt32(&writer, "ok", 2, result->ok);

	return PgbsonWriterGetPgbson(&writer);
}


/*
 * This is a helper function which takes in the writer, fieldName , size and value and decides if we want to write
 * the response as int32 or double.
 */
static inline void
WriteStatsAsIntOrDouble(pgbson_writer *writer, char *fieldName, int size, int64 value)
{
	if (value >= INT32_MIN && value <= INT32_MAX)
	{
		PgbsonWriterAppendInt32(writer, fieldName, size, value);
	}
	else
	{
		PgbsonWriterAppendDouble(writer, fieldName, size, value);
	}
}


/*
 * WriteCoreStorageStats writes the collStats() storage output to the target writer.
 */
static void
WriteCoreStorageStats(CollStatsResult *result, pgbson_writer *writer)
{
	WriteStatsAsIntOrDouble(writer, "size", 4, result->size);
	WriteStatsAsIntOrDouble(writer, "count", 5, result->count);
	PgbsonWriterAppendInt32(writer, "avgObjSize", 10, result->avgObjSize);
	WriteStatsAsIntOrDouble(writer, "storageSize", 11, result->storageSize);
	PgbsonWriterAppendInt32(writer, "nindexes", 8, result->nindexes);

	pgbson_array_writer arrayWriter;
	PgbsonWriterStartArray(writer, "indexBuilds", 11, &arrayWriter);
	ListCell *indexCell = NULL;
	foreach(indexCell, result->indexBuilds)
	{
		char *indexName = lfirst(indexCell);
		bson_value_t elementValue = {
			.value_type = BSON_TYPE_UTF8,
			.value.v_utf8.str = indexName,
			.value.v_utf8.len = strlen(indexName)
		};
		PgbsonArrayWriterWriteValue(&arrayWriter, &elementValue);
	}
	PgbsonWriterEndArray(writer, &arrayWriter);

	WriteStatsAsIntOrDouble(writer, "totalIndexSize", 14, result->totalIndexSize);
	WriteStatsAsIntOrDouble(writer, "totalSize", 9, result->totalSize);
	PgbsonWriterAppendDocument(writer, "indexSizes", 10, result->indexSizes);
	WriteStatsAsIntOrDouble(writer, "scaleFactor", 11, result->scaleFactor);
}


/*
 * GetDocumentsCountRunTime returns document count by counting the documents in the
 * given collection at runtime. It always returns accurate count.
 */
static int64
GetDocumentsCountRunTime(MongoCollection *collection)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT COUNT(*) FROM %s.documents_" UINT64_FORMAT,
					 ApiDataSchemaName, collection->collectionId);
	bool isNull = true;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
													&isNull);
	if (isNull)
	{
		return 0;
	}
	return DatumGetInt64(resultDatum);
}


/*
 * GetAverageColumnWidthRuntime returns average size of given column num of provided collection(table)
 * It calculates the average size of the 'document' column at runtime.
 */
static int32
GetAverageColumnWidthRuntime(MongoCollection *collection)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT avg(pg_column_size(document))::int4 FROM %s.documents_"
					 UINT64_FORMAT,
					 ApiDataSchemaName, collection->collectionId);

	bool isNull = true;
	bool readOnly = true;
	Datum resultDatum = ExtensionExecuteQueryViaSPI(cmdStr->data, readOnly, SPI_OK_SELECT,
													&isNull);
	if (isNull)
	{
		return 0;
	}

	return DatumGetInt32(resultDatum);
}
