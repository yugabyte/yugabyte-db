/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/index_stats.c
 *
 * Implementation of the indexStats aggregation stage.
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <utils/builtins.h>
#include <executor/spi.h>

#include "metadata/collection.h"
#include "utils/mongo_errors.h"
#include "io/bson_set_returning_functions.h"
#include "utils/query_utils.h"
#include "metadata/index.h"
#include "utils/timestamp.h"
#include "utils/hashset_utils.h"
#include "commands/coll_stats.h"
#include "planner/helio_planner.h"
#include "commands/diagnostic_commands_common.h"
#include "api_hooks.h"
#include "metadata/metadata_cache.h"


static const char *IndexUsageKey = "index_usage";

PG_FUNCTION_INFO_V1(command_index_stats_aggregation);
PG_FUNCTION_INFO_V1(command_index_stats_worker);


static void IndexStatsCoordinator(Datum databaseName, Datum collectionName,
								  MongoCollection *collection,
								  Tuplestorestate *tupleStore,
								  TupleDesc tupleDescriptor);

static pgbson * IndexStatsWorker(void *fcinfoPointer);

static void MergeWorkerResults(MongoCollection *collection, List *workerResults,
							   Tuplestorestate *tupleStore, TupleDesc tupleDescriptor);


/*
 * Top level entry point for index_stats.
 * This collects data from the workers on the index
 * statistics and merges into an aggregation response.
 */
Datum
command_index_stats_aggregation(PG_FUNCTION_ARGS)
{
	Datum databaseName = PG_GETARG_DATUM(0);
	Datum collectionName = PG_GETARG_DATUM(1);

	TupleDesc descriptor;
	Tuplestorestate *tupleStore = SetupBsonTuplestore(fcinfo, &descriptor);

	/* Similar to collStats index_stats is divided into
	 * querying the worker, and merging on the coordinator.
	 */
	MongoCollection *collection =
		GetMongoCollectionByNameDatum(databaseName,
									  collectionName,
									  AccessShareLock);

	if (collection == NULL)
	{
		PG_RETURN_VOID();
	}

	if (collection->viewDefinition != NULL)
	{
		ereport(ERROR, (errcode(MongoCommandNotSupportedOnView),
						errmsg("Namespace %s.%s is a view, not a collection",
							   TextDatumGetCString(databaseName),
							   TextDatumGetCString(collectionName))));
	}

	IndexStatsCoordinator(databaseName, collectionName, collection, tupleStore,
						  descriptor);
	PG_RETURN_VOID();
}


/*
 * The implementation of indexStats on the query worker (runs on every node).
 */
Datum
command_index_stats_worker(PG_FUNCTION_ARGS)
{
	pgbson *response = RunWorkerDiagnosticLogic(&IndexStatsWorker, fcinfo);
	PG_RETURN_POINTER(response);
}


/*
 * The implementation of indexStats on the query coordinator.
 */
static void
IndexStatsCoordinator(Datum databaseName, Datum collectionName,
					  MongoCollection *collection, Tuplestorestate *tupleStore,
					  TupleDesc tupleDescriptor)
{
	StringInfo cmdStr = makeStringInfo();
	appendStringInfo(cmdStr,
					 "SELECT success, result FROM run_command_on_all_nodes("
					 "FORMAT($$ SELECT %s.index_stats_worker(%%L, %%L) $$, $1, $2))",
					 ApiInternalSchemaName);

	int numValues = 2;
	Datum values[2] = { databaseName, collectionName };
	Oid types[2] = { TEXTOID, TEXTOID };

	List *workerBsons = GetWorkerBsonsFromAllWorkers(cmdStr->data, values, types,
													 numValues, "IndexStats");

	/* Now that we have the worker BSON results, merge them to the final one */
	MergeWorkerResults(collection, workerBsons, tupleStore, tupleDescriptor);
}


/*
 * The core logic that queries each worker and gets the
 * index statistics.
 * The logic is simply to call pg_stat_all_indexes and get
 * the index accesses per index.
 */
static pgbson *
IndexStatsWorker(void *fcinfoPointer)
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
	GetMongoCollectionShardOidsAndNames(collection, &shardOids, &shardNames);

	/* Next get the relation and table size */
	pgbson_writer writer;
	PgbsonWriterInit(&writer);

	/* Only do work if there are shards */
	if (shardOids == NULL)
	{
		return PgbsonWriterGetPgbson(&writer);
	}

	Assert(shardNames != NULL);

	/*
	 * Walk the indexes for these shards and write out their sizes.
	 */
	pgbson_writer indexWriter;
	PgbsonWriterStartDocument(&writer, IndexUsageKey, -1, &indexWriter);

	const char *query =
		"SELECT indexrelid, idx_scan FROM pg_catalog.pg_stat_all_indexes "
		" WHERE relid =ANY ($1)";

	int nargs = 1;
	Oid argTypes[1] = { OIDARRAYOID };
	Datum argValues[1] = { PointerGetDatum(shardOids) };

	bool readOnly = true;
	MemoryContext priorMemoryContext = CurrentMemoryContext;

	HTAB *indexHash = CreatePgbsonElementHashSet();
	SPI_connect();

	Portal statsPortal = SPI_cursor_open_with_args("workerIndexUsageStats", query, nargs,
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

				int64 indexAccesses = DatumGetInt64(resultDatum);

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
					element.bsonValue.value.v_int64 = indexAccesses;

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
								"indexStats tuple table was null for index size stats.")));
		}
	}

	SPI_cursor_close(statsPortal);
	SPI_finish();

	HASH_SEQ_STATUS seq_status;
	pgbsonelement *entry;

	hash_seq_init(&seq_status, indexHash);
	while ((entry = hash_seq_search(&seq_status)) != NULL)
	{
		int64 totalAccesses = BsonValueAsInt64(&entry->bsonValue);
		PgbsonWriterAppendInt64(&indexWriter, entry->path, entry->pathLength,
								totalAccesses);
	}

	hash_destroy(indexHash);

	PgbsonWriterEndDocument(&writer, &indexWriter);
	return PgbsonWriterGetPgbson(&writer);
}


/*
 * Parses the per node worker results and returns a post-processed
 * set of index documents that can be merged.
 */
static List *
ParseWorkerResults(List *workerResults)
{
	ListCell *workerCell;

	List *indexDocs = NIL;
	foreach(workerCell, workerResults)
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
			else if (strcmp(key, IndexUsageKey) == 0)
			{
				bson_value_t *value = palloc(sizeof(bson_value_t));
				*value = *bson_iter_value(&workerIter);
				indexDocs = lappend(indexDocs, value);
			}
			else
			{
				ereport(ERROR, (errmsg("unknown field received from indexStats worker %s",
									   key)));
			}
		}

		if (errorMessage != NULL)
		{
			errorCode = errorCode == 0 ? MongoInternalError : errorCode;
			ereport(ERROR, (errcode(errorCode), errmsg("Error running indexStats %s",
													   errorMessage)));
		}
	}

	return indexDocs;
}


/*
 * This takes the output from each worker and creates an aggregated view
 * which dumps one row per index in the Mongo compatible format
 */
static void
MergeWorkerResults(MongoCollection *collection, List *workerResults,
				   Tuplestorestate *tupleStore, TupleDesc tupleDescriptor)
{
	List *indexDocs = ParseWorkerResults(workerResults);

	bool excludeIdIndex = false;

	/* Since index_stats can be executed in a $lookup/$unionWith
	 * It can run on a worker querying the coordinator - we would need
	 * nested distributed execution.
	 */
	bool enableNestedDistribution = true;
	List *indexes = CollectionIdGetValidIndexes(collection->collectionId, excludeIdIndex,
												enableNestedDistribution);

	HTAB *bsonElementHash = CreatePgbsonElementHashSet();

	/* First run through the worker index docs -
	 * For each doc, add the { "indexName": (int64)indexAccesses }
	 * into the bsonElement hash. If the entry for that indexName
	 * already exists, add to the existing size.
	 */
	ListCell *indexCell;
	foreach(indexCell, indexDocs)
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

	/* Extract postmaster start time */
	TimestampTz timestampValue = PgStartTime;

	/* get milliseconds from epoch (Timestamp has TS_PREC_INV units per seconds) */

	Timestamp epoch = SetEpochTimestamp();

	int overFlow = 0;
	TimestampTz epochTimestampTz = timestamp2timestamptz_opt_overflow(epoch, &overFlow);

	long delta = TimestampDifferenceMilliseconds(epochTimestampTz,
												 timestampValue);

	bson_value_t startTimeValue = { 0 };
	startTimeValue.value_type = BSON_TYPE_DATE_TIME;
	startTimeValue.value.v_datetime = delta;

	/* Now write one row per index based on the collection indexes */
	ListCell *cell;
	foreach(cell, indexes)
	{
		IndexDetails *details = lfirst(cell);
		pgbsonelement elem = { 0 };
		elem.path = details->indexSpec.indexName;
		elem.pathLength = strlen(elem.path);
		bool found;
		pgbsonelement *foundElement = hash_search(bsonElementHash, &elem, HASH_FIND,
												  &found);

		int64_t usages = 0;
		if (found)
		{
			usages = BsonValueAsInt64(&foundElement->bsonValue);
		}

		pgbson_writer writer;
		PgbsonWriterInit(&writer);

		PgbsonWriterAppendUtf8(&writer, "name", 4, details->indexSpec.indexName);
		PgbsonWriterAppendDocument(&writer, "key", 3,
								   details->indexSpec.indexKeyDocument);

		pgbson_writer childWriter;
		PgbsonWriterStartDocument(&writer, "accesses", 8, &childWriter);
		PgbsonWriterAppendInt64(&childWriter, "ops", 3, usages);
		PgbsonWriterAppendValue(&childWriter, "since", 5, &startTimeValue);
		PgbsonWriterEndDocument(&writer, &childWriter);
		PgbsonWriterAppendDocument(&writer, "spec", 4, IndexSpecAsBson(
									   &details->indexSpec));

		/* add the row */
		Datum tuple[1] = { PointerGetDatum(PgbsonWriterGetPgbson(&writer)) };
		bool nulls[1] = { false };
		tuplestore_putvalues(tupleStore, tupleDescriptor, tuple, nulls);
	}

	hash_destroy(bsonElementHash);
}
