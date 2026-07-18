/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/ttl/ttl_index.c
 *
 * Functions for ttl index related operations.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <stdlib.h>

#include <catalog/namespace.h>
#include <commands/sequence.h>
#include <executor/spi.h>
#include <portability/instr_time.h>

#include "io/bson_core.h"
#include "metadata/collection.h"
#include "query/bson_compare.h"
#include "metadata/metadata_cache.h"
#include "storage/lmgr.h"
#include "utils/list_utils.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"
#include "utils/guc_utils.h"
#include "utils/error_utils.h"
#include "utils/index_utils.h"
#include "utils/version_utils.h"

extern bool LogTTLProgressActivity;
extern bool RepeatPurgeIndexesForTTLTask;
extern int TTLPurgerStatementTimeout;
extern int MaxTTLDeleteBatchSize;
extern int TTLPurgerLockTimeout;
extern char *ApiGucPrefix;
extern int SingleTTLTaskTimeBudget;
extern int TTLTaskMaxRunTimeInMS;
extern bool EnableTtlJobsOnReadOnly;
extern bool ForceIndexScanForTTLTask;
extern bool UseIndexHintsForTTLTask;
extern bool EnableTTLDescSort;
extern bool EnableIndexOrderbyPushdown;
extern double TTLDeleteSaturationThreshold;
extern int TTLSlowBatchDeleteThresholdInMS;
extern bool EnableSelectiveTTLLogging;
extern bool EnableTTLBatchObservability;

bool UseV2TTLIndexPurger = true;

/* --------------------------------------------------------- */
/* Data-types */
/* --------------------------------------------------------- */

/*
 * TtlIndexEntry is a struct that holds the information of a TTL index.
 * It is used to delete expired rows for a given TTL index.
 */
typedef struct TtlIndexEntry
{
	/* ID of the collection */
	uint64 collectionId;

	/* The shardid being pruned */
	uint64 shardId;

	/* The TTL index id */
	uint64 indexId;

	/* The index key document to validate expiry against. */
	Datum indexKeyDatum;

	/* The partial filter expression for the index to check if a document is applicable for deletion. */
	Datum indexPfeDatum;

	/* The expiry time in seconds for the index key. */
	int32 indexExpireAfterSeconds;

	/* Is the index sparse */
	bool isSparse;

	/* Is Ordered Index Scan available */
	bool indexIsOrdered;

	/* Name of the index */
	char *indexName;
} TtlIndexEntry;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static uint64 DeleteExpiredRowsForIndexCore(char *tableName, TtlIndexEntry *indexEntry,
											int64 currentTime, int32 batchSize, instr_time
											startTime, int budget,
											bool *IsTaskTimeBudgetExceeded);
static bool IsTaskTimeBudgetExceeded(instr_time startTime, double *elapsedTime, int
									 budget);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(delete_expired_rows_for_index);
PG_FUNCTION_INFO_V1(delete_expired_rows);
PG_FUNCTION_INFO_V1(delete_expired_rows_background);

/*
 * delete_expired_rows deletes a batch of expired documents for an input ttl index.
 * It returns the total number of documents selected for deletion. Note that the actual
 * number of documents deleted might be different (more details in the function body).
 *
 * The function also logs the number of rows selected and the number of rows deleted per
 * invocation.
 *
 * Here is the full SQL query we use to delete and to log the number of deleted rows.
 *
 *  WITH deleted_rows as
 *  (
 *      DELETE FROM ApiDataSchemaName.documents_collectionId_shardId
 *      WHERE ctid IN
 *      (
 *          SELECT ctid FROM ApiDataSchemaName.documents_collectionId_shardId
 *          WHERE ApiCatalogSchemaName.bson_dollar_lt(document, '{ "ttl" : { "$date" : { "$numberLong" : "100" } } }'::ApiCatalogSchemaName.bson)
 *          LIMIT 100
 *      )
 *      RETURNING ctid
 *  )
 *  SELECT count(*) FROM deleted_rows;
 *
 */
Datum
delete_expired_rows_for_index(PG_FUNCTION_ARGS)
{
	if (PG_NARGS() < 8)
	{
		/* This can happen if the binary version is 1.8+
		 * but ALTER EXTENSION hasn't been called yet.
		 */
		ereport(LOG, (errmsg("Skipping TTL Purge because of binary/schema mismatch")));
		PG_RETURN_INT64(0);
	}

	uint64 collectionId = DatumGetInt64(PG_GETARG_DATUM(0));

	uint64 indexId = DatumGetInt64(PG_GETARG_DATUM(1));
	Datum indexKeyDatum = PG_GETARG_DATUM(2);
	Datum partialFilterDatum = PG_ARGISNULL(3) ? (Datum) 0 : PG_GETARG_DATUM(3);
	int64 currentTime = DatumGetInt64(PG_GETARG_DATUM(4));

	int32 indexExpiry = DatumGetInt32(PG_GETARG_DATUM(5));
	int ttlDeleteBatchSize = DatumGetInt32(PG_GETARG_DATUM(6));
	uint64 shardId = DatumGetInt64(PG_GETARG_DATUM(7));

	char tableName[NAMEDATALEN];
	sprintf(tableName, DOCUMENT_DATA_TABLE_NAME_FORMAT "_" UINT64_FORMAT, collectionId,
			shardId);

	TtlIndexEntry indexEntry = {
		.collectionId = collectionId,
		.shardId = shardId,
		.indexId = indexId,
		.indexKeyDatum = indexKeyDatum,
		.indexPfeDatum = partialFilterDatum,
		.indexExpireAfterSeconds = indexExpiry
	};

	instr_time startTime;
	INSTR_TIME_SET_CURRENT(startTime);
	bool isTimeBudgetExceeded = false;
	uint64 rowsCount = DeleteExpiredRowsForIndexCore(tableName, &indexEntry, currentTime,
													 ttlDeleteBatchSize, startTime,
													 SingleTTLTaskTimeBudget,
													 &isTimeBudgetExceeded);

	PG_RETURN_INT64((int64) rowsCount);
}


/* Function to randomize the list of ttl indexes in order to avoid starvation.
 * Another option was to use ORDER BY RANDOM() while getting the list of ttl index. */
static void
shuffle_list(List *list)
{
	for (int i = list_length(list) - 1; i > 0; i--)
	{
		int j = rand() % (i + 1);
		ListCell temp = list->elements[i];
		list->elements[i] = list->elements[j];
		list->elements[j] = temp;
	}
}


/* This is the entry point for the delete_expired_rows UDF. This is called by a background job to do the
 * TTL index purging. Basically this function does it in 2 phases.
 * 1. It gets all the TTL indexes from the documentdb_api_catalog.collection_indexes table.
 * 2. For every TTL index, gets is partial filter expression and expiration seconds, it gets the index PG table and shard tables for distributed scenarios
 *    and calls into DeleteExpiredRowsForIndexCore to delete the documents that meet the index conditions.
 */
Datum
delete_expired_rows(PG_FUNCTION_ARGS)
{
	if (!UseV2TTLIndexPurger)
	{
		PG_RETURN_VOID();
	}

	int32_t batchSize = PG_GETARG_INT32(0);

	StringInfo cmdGetIndexes = makeStringInfo();

	/* Retrieve TTL indexes along with their associated collections */
	appendStringInfo(cmdGetIndexes,
					 "SELECT index_id, collection_id, (index_spec).index_key, "
					 "(index_spec).index_pfe, (index_spec).index_expire_after_seconds,"
					 "(index_spec).index_is_sparse, "
					 "COALESCE(%s.bson_get_value_text((index_spec).index_options::%s,'enableCompositeTerm'::text)::bool, %s.bson_get_value_text((index_spec).index_options::%s, 'enableOrderedIndex'::text)::bool, false) as index_is_ordered, "
					 "(index_spec).index_name FROM %s.collection_indexes "
					 "WHERE index_is_valid AND (index_spec).index_expire_after_seconds >= 0 "
					 "ORDER BY collection_id, index_id",
					 ApiCatalogToCoreSchemaName, FullBsonTypeName,
					 ApiCatalogToCoreSchemaName, FullBsonTypeName,
					 ApiCatalogSchemaName);

	List *ttlIndexEntries = NIL;
	SPIParseOpenOptions parseOptions =
	{
		.read_only = true,
		.cursorOptions = 0,
		.params = NULL
	};

	MemoryContext priorMemoryContext = CurrentMemoryContext;

	/* Get all the TTL index entries first since deleting the rows needs another SPI connection with special settings.
	 * So to avoid having nested SPI contexts and do a better transaction control, we separate the steps. */
	SPI_connect();
	Portal indexesPortal = SPI_cursor_parse_open("ttlJobPortal", cmdGetIndexes->data,
												 &parseOptions);

	if (indexesPortal == NULL)
	{
		ereport(ERROR, errmsg(
					"TTL delete_expired_rows unexpectedly failed to open cursor."));
	}

	MemoryContext oldContext = NULL;
	bool hasData = true;
	while (hasData)
	{
		SPI_cursor_fetch(indexesPortal, true, INT_MAX);

		hasData = SPI_processed >= 1;
		if (!hasData)
		{
			break;
		}

		if (SPI_tuptable)
		{
			if (SPI_tuptable->tupdesc->natts < 5)
			{
				ereport(ERROR, errmsg(
							"TTL delete_expired_rows hit an unexpected error, number of columns from collection_indexes was less than 5"));
			}

			for (int tupleNumber = 0; tupleNumber < (int) SPI_processed; tupleNumber++)
			{
				TtlIndexEntry *ttlIndexEntry = MemoryContextAllocZero(priorMemoryContext,
																	  sizeof(TtlIndexEntry));

				/* Get index_id column */
				bool isNull;
				Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
												  SPI_tuptable->tupdesc, 1,
												  &isNull);
				if (isNull)
				{
					ereport(ERROR, errmsg(
								"TTL delete_expired_rows hit an unexpected error, index_id was NULL"));
				}

				ttlIndexEntry->indexId = DatumGetInt32(resultDatum);

				/* Get collection_id column */
				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 2,
											&isNull);
				if (isNull)
				{
					ereport(ERROR, errmsg(
								"TTL delete_expired_rows hit an unexpected error, collection_id was NULL"));
				}

				ttlIndexEntry->collectionId = DatumGetUInt64(resultDatum);

				/* Get index_key column */
				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 3,
											&isNull);
				if (isNull)
				{
					ereport(ERROR, errmsg(
								"TTL delete_expired_rows hit an unexpected error, index_key was NULL"));
				}

				ttlIndexEntry->indexKeyDatum = SPI_datumTransfer(resultDatum, false, -1);

				/* Get index_pfe column */
				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 4,
											&isNull);

				ttlIndexEntry->indexPfeDatum = isNull ? (Datum) 0 : SPI_datumTransfer(
					resultDatum, false, -1);

				/* Get index_expire_after_seconds column */
				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 5,
											&isNull);
				if (isNull)
				{
					ereport(ERROR, errmsg(
								"TTL delete_expired_rows hit an unexpected error, index_expire_after_seconds was NULL"));
				}

				ttlIndexEntry->indexExpireAfterSeconds = DatumGetInt32(resultDatum);

				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 6,
											&isNull);
				ttlIndexEntry->isSparse = DatumGetBool(resultDatum);

				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 7,
											&isNull);
				ttlIndexEntry->indexIsOrdered = DatumGetBool(resultDatum);

				resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											SPI_tuptable->tupdesc, 8,
											&isNull);

				oldContext = MemoryContextSwitchTo(priorMemoryContext);
				ttlIndexEntry->indexName = pstrdup(TextDatumGetCString(resultDatum));
				ttlIndexEntries = lappend(ttlIndexEntries, ttlIndexEntry);
				MemoryContextSwitchTo(oldContext);
			}
		}
	}

	SPI_cursor_close(indexesPortal);
	SPI_finish();

	if (list_length(ttlIndexEntries) < 1)
	{
		/* No TTL indexes to cleanup. */
		PG_RETURN_VOID();
	}

	/* We have the TTL index records, now cleanup as much as we can in individual transactions before the time budget expires. */
	instr_time startTime;
	INSTR_TIME_SET_CURRENT(startTime);
	MongoCollection currentCollection =
	{
		.collectionId = -1
	};

	struct timespec timeSpec;
	int numItems = 0;
	Datum *itemDatums = NULL;
	ArrayType *shardIdsArray = NULL;
	ArrayType *shardNamesArray = NULL;
	ListCell *ttlEntryCell = NULL;
	bool shouldCleanupCollection = false;
	uint64 rowsDeletedInCurrentLoop = 0;

	int timeBudget = RepeatPurgeIndexesForTTLTask ? TTLTaskMaxRunTimeInMS :
					 SingleTTLTaskTimeBudget;

	while (!IsTaskTimeBudgetExceeded(startTime, NULL, timeBudget))
	{
		rowsDeletedInCurrentLoop = 0;
		if (RepeatPurgeIndexesForTTLTask)
		{
			shuffle_list(ttlIndexEntries);
		}
		foreach(ttlEntryCell, ttlIndexEntries)
		{
			TtlIndexEntry *ttlIndexEntry = (TtlIndexEntry *) lfirst(ttlEntryCell);
			uint64 collectionId = ttlIndexEntry->collectionId;

			/* We're cleaning up a new collection, let's get the shards and relation information. */
			if (currentCollection.collectionId != collectionId)
			{
				oldContext = MemoryContextSwitchTo(priorMemoryContext);
				currentCollection.collectionId = collectionId;
				memset(currentCollection.tableName, 0, NAMEDATALEN);
				sprintf(currentCollection.tableName, DOCUMENT_DATA_TABLE_NAME_FORMAT,
						collectionId);
				currentCollection.relationId = GetRelationIdForCollectionId(
					collectionId, NoLock);

				if (shardIdsArray != NULL)
				{
					pfree(shardIdsArray);
					shardIdsArray = NULL;
				}

				if (shardNamesArray != NULL)
				{
					pfree(shardNamesArray);
					shardNamesArray = NULL;
				}

				/* Check if delete will be able to lock the table, if not skip this collection. */
				if (ConditionalLockRelationOid(currentCollection.relationId,
											   RowShareLock))
				{
					UnlockRelationOid(currentCollection.relationId, RowShareLock);

					shouldCleanupCollection =
						GetMongoCollectionShardOidsAndNames(&currentCollection,
															&shardIdsArray,
															&shardNamesArray);
				}
				else
				{
					shouldCleanupCollection = false;
					ereport(LOG, errmsg(
								"TTL job skipping collection_id=%lu because is locked.",
								collectionId));
				}

				if (itemDatums != NULL)
				{
					pfree(itemDatums);
					itemDatums = NULL;
				}

				if (shouldCleanupCollection)
				{
					deconstruct_array(shardNamesArray, TEXTOID, -1, false,
									  TYPALIGN_INT, &itemDatums, NULL, &numItems);
				}

				MemoryContextSwitchTo(oldContext);
			}

			/* No tables to cleanup on this node. */
			if (!shouldCleanupCollection)
			{
				continue;
			}

			/*
			 * Before we begin the actual deletions, check if we need
			 * to handle if the transaction is read-only.
			 */
			if (XactReadOnly)
			{
				if (EnableTtlJobsOnReadOnly)
				{
					/* To enable read-write we need to be the first query
					 * in the transaction. Process_utility will have already
					 * set a snapshot on this transaction so we can't reset
					 * the transaction read-only flag.
					 * Consequently, commit this transaction that's read-only
					 * and start a new one so we can mark the read-only flag
					 * as false.
					 */
					PopAllActiveSnapshots();
					CommitTransactionCommand();
					StartTransactionCommand();
					SetGUCLocally("transaction_read_only", "false");
				}
				else
				{
					ereport(INFO, errmsg(
								"TTL job skipping because transaction is read-only."));
					continue;
				}
			}

			volatile bool shouldStop = false;

			/* Delete records on all tables for this collection */
			for (volatile int i = 0; i < numItems; i++)
			{
				char *tableName = text_to_cstring(DatumGetTextP(itemDatums[i]));

				clock_gettime(CLOCK_REALTIME, &timeSpec);

				time_t epochSeconds = timeSpec.tv_sec;
				uint32_t millisecondsInSecond = timeSpec.tv_nsec / 1000000;
				uint64_t epochMilliseconds = (epochSeconds * 1000UL) +
											 millisecondsInSecond;

				PG_TRY();
				{
					bool isTimeBudgetExceeded = false;
					uint64 deletedRows =
						DeleteExpiredRowsForIndexCore(
							tableName, ttlIndexEntry, epochMilliseconds,
							batchSize, startTime, timeBudget, &isTimeBudgetExceeded);
					if (isTimeBudgetExceeded)
					{
						/* If exceeded time, mark as should stop but still commit this deletion. */
						shouldStop = true;
					}

					/* Commit the deletion. */
					PopAllActiveSnapshots();
					CommitTransactionCommand();
					StartTransactionCommand();

					rowsDeletedInCurrentLoop += deletedRows;
				}
				PG_CATCH();
				{
					oldContext = MemoryContextSwitchTo(priorMemoryContext);
					ErrorData *edata = CopyErrorDataAndFlush();
					MemoryContextSwitchTo(oldContext);

					ereport(WARNING, errmsg(
								"TTL job failed when processing collection_id=%lu and index_id=%lu with error: %s",
								collectionId, ttlIndexEntry->indexId, edata->message));

					shouldStop = true;

					/* Abort the transaction and continue with the next TTL indexes */
					PopAllActiveSnapshots();
					AbortCurrentTransaction();
					StartTransactionCommand();
				}
				PG_END_TRY();

				if (shouldStop)
				{
					goto end;
				}

				/* Before starting the next loop, set the transaction characteristics */
				if (XactReadOnly && EnableTtlJobsOnReadOnly)
				{
					SetGUCLocally("transaction_read_only", "false");
				}
			}

			if (IsTaskTimeBudgetExceeded(startTime, NULL, timeBudget))
			{
				goto end;
			}
		}

		if (rowsDeletedInCurrentLoop == 0 || !RepeatPurgeIndexesForTTLTask)
		{
			goto end;
		}
	}

end:
	oldContext = MemoryContextSwitchTo(priorMemoryContext);
	list_free_deep(ttlIndexEntries);

	if (itemDatums != NULL)
	{
		pfree(itemDatums);
	}

	if (shardIdsArray != NULL)
	{
		pfree(shardIdsArray);
	}

	if (shardNamesArray != NULL)
	{
		pfree(shardNamesArray);
	}

	MemoryContextSwitchTo(oldContext);
	PG_RETURN_VOID();
}


/*
 * Drop-in replacement for delete_expired_rows. This will be called periodically by the
 * background worker framework and will coexist with the previous UDF until it reaches
 * stability.
 */
Datum
delete_expired_rows_background(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/* Based on the task start time it checks if we have exceeded the ttl task budget defined in
 * the SingleTTLTaskTimeBudget GUC. */
static bool
IsTaskTimeBudgetExceeded(instr_time startTime, double *elapsedTime, int budget)
{
	instr_time current;
	INSTR_TIME_SET_CURRENT(current);
	INSTR_TIME_SUBTRACT(current, startTime);
	double elapsed = INSTR_TIME_GET_MILLISEC(current);

	if (elapsedTime != NULL)
	{
		*elapsedTime = elapsed;
	}

	if (elapsed > (double) budget)
	{
		ereport(LOG, errmsg("TTL Index delete rows exceeded time budget: %dms.",
							budget));
		return true;
	}

	return false;
}


/* Deletes the rows that have expired for the given table name and ttl entry information.
 * It deletes the number of items specified on the batchSize that have expired based on the index entry expiry value. */
static uint64
DeleteExpiredRowsForIndexCore(char *tableName, TtlIndexEntry *indexEntry, int64
							  currentTime, int32 batchSize, instr_time startTime, int
							  budget, bool *isTaskTimeBudgetExceeded)
{
	int32 ttlDeleteBatchSize = (batchSize != -1) ? batchSize :
							   MaxTTLDeleteBatchSize;
	pgbson *indexKeyDocument = DatumGetPgBson(indexEntry->indexKeyDatum);
	pgbson *indexPfe = (indexEntry->indexPfeDatum != (Datum) 0) ?
					   DatumGetPgBson(indexEntry->indexPfeDatum) : NULL;

	/* TTL expireAfterSeconds is an int32 but we cast it as int64 to avoid overflow in milliseconds calculation */
	int64 indexExpiryMilliseconds = (int64) indexEntry->indexExpireAfterSeconds * 1000L;
	bson_iter_t pathSpecIter;
	PgbsonInitIterator(indexKeyDocument, &pathSpecIter);
	bson_iter_next(&pathSpecIter);
	const char *indexKey = bson_iter_key(&pathSpecIter);

	/*
	 *  Note that the TTL condition is repeated here, once for selecting a batch of records and once more while deleting.
	 *  The reason is that the SELECT subquery won't lock the records for update, so by the time we DELETE them, the records
	 *  could have been concurrently updated for them to come out of expiry window. Deleting those record won't be correct.
	 *  Hence we repeat the expiry condition once again in the DELETE statement.
	 *
	 *  As a side comment, we use the SELECT subquery to select a batch of records, because "DELETE" does not support LIMIT
	 *  (it's a Postgres limitation).
	 *
	 *  An index can have a partial filter expression (pfe) specified during creation which is also applicable for TTL index. We apply
	 *  the pfe predicate while selecting and deleting the expired records.
	 */

	StringInfo cmdStrDeleteRows = makeStringInfo();

	/* optimization for unsharded collection to avoid 2PC */
	appendStringInfo(cmdStrDeleteRows,
					 "DELETE FROM %s.%s"
					 " WHERE ctid IN (SELECT ctid FROM %s.%s"
					 " WHERE %s.bson_dollar_lt(document, $1::%s) ",
					 ApiDataSchemaName, tableName,
					 ApiDataSchemaName, tableName,
					 ApiCatalogSchemaName, FullBsonTypeName);

	int argCount = 1;

	/*
	 *  So far, we had force an IndexScan for queries that select and delete TTL-eligible documents by locally disabling
	 *  sequential scans and bitmap index scans. The GUC documentdb_rum.preferOrderedIndexScan is set to true by default,
	 *  which causes the IndexScan to be planned as an ordered index scan. Ordered index scans are significantly more
	 *  efficient than bitmap index scans or sequential scans when there are many documents to delete. Moreover,
	 *  repeated bitmap index scans—which may need to traverse all index pages to create a bitmap—can put pressure on disk I/O usage.
	 *
	 *  We are now transitioning away from the above method, as we currently support index hints. In the SQL query above, we provide the
	 *  corresponding TTL index as a hint for the TTL task query. Even though it's called a hint, by design it forces the use
	 *  of the specified index. We intend to roll back these GUC overrides after the 1.106 schema release, which is expected to
	 *  include support for index hints.
	 *
	 *  TODO: Finally, when we have support for IndexOnly scan in RUM index, we would move from IndexScan to IndexOnlyScan, since,
	 *  for TTL deletes, we just fetch the ctids of the eligible rows and delete them. We don't need to fetch the corresponding tuples
	 *  from the Index pages.
	 */

	bool disableSeqAndBitmapScan = !IsClusterVersionAtleast(DocDB_V0, 106, 0) &&
								   ForceIndexScanForTTLTask &&
								   indexEntry->indexIsOrdered;

	bool useIndexHintsForTTLQuery = IsClusterVersionAtleast(DocDB_V0, 106, 0) &&
									UseIndexHintsForTTLTask &&
									indexEntry->indexIsOrdered;
	if (useIndexHintsForTTLQuery)
	{
		appendStringInfo(cmdStrDeleteRows,
						 " AND %s.bson_dollar_index_hint(document, $2::text, $3::%s, $4)",
						 ApiInternalSchemaNameV2,
						 FullBsonTypeName);
		argCount += 3;
	}

	if (indexPfe != NULL)
	{
		if (useIndexHintsForTTLQuery)
		{
			appendStringInfo(cmdStrDeleteRows, "AND document OPERATOR(%s.@@) $5::%s",
							 ApiCatalogSchemaName, FullBsonTypeName);
		}
		else
		{
			appendStringInfo(cmdStrDeleteRows, "AND document OPERATOR(%s.@@) $2::%s",
							 ApiCatalogSchemaName, FullBsonTypeName);
		}
		argCount++;
	}

	bool useDescendingSort = EnableTTLDescSort &&
							 EnableIndexOrderbyPushdown &&
							 indexEntry->indexIsOrdered;

	/* Fetch the entries to be deleted in descending order if the index is orderd */
	if (useDescendingSort)
	{
		appendStringInfo(cmdStrDeleteRows,
						 " AND %s.bson_dollar_fullscan(document, '{ \"%s\": -1 }'::%s)"
						 " ORDER BY %s.bson_orderby(document, '{ \"%s\": -1 }'::%s)",
						 ApiInternalSchemaNameV2, indexKey, FullBsonTypeName,
						 ApiCatalogSchemaName, indexKey, FullBsonTypeName);
	}

	appendStringInfo(cmdStrDeleteRows, " LIMIT %d FOR UPDATE SKIP LOCKED) ",
					 ttlDeleteBatchSize);

	bool readOnly = false;
	char *argNulls = NULL;
	Oid argTypes[5];
	Datum argValues[5];

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_value_t expiryField = { 0 };
	expiryField.value_type = BSON_TYPE_DATE_TIME;
	expiryField.value.v_datetime = currentTime - indexExpiryMilliseconds;
	PgbsonWriterAppendValue(&writer, indexKey, strlen(indexKey), &expiryField);

	pgbson *value = PgbsonWriterGetPgbson(&writer);
	argTypes[0] = BYTEAOID;
	argValues[0] = PointerGetDatum(CastPgbsonToBytea(value));

	if (useIndexHintsForTTLQuery)
	{
		argTypes[1] = TEXTOID;
		argValues[1] = CStringGetTextDatum(indexEntry->indexName);

		argTypes[2] = BYTEAOID;
		argValues[2] = PointerGetDatum(CastPgbsonToBytea(indexKeyDocument));

		argTypes[3] = BOOLOID;
		argValues[3] = BoolGetDatum(indexEntry->isSparse);
	}

	if (indexPfe != NULL)
	{
		if (useIndexHintsForTTLQuery)
		{
			argTypes[4] = BYTEAOID;
			argValues[4] = PointerGetDatum(CastPgbsonToBytea(indexPfe));
		}
		else
		{
			argTypes[1] = BYTEAOID;
			argValues[1] = PointerGetDatum(CastPgbsonToBytea(indexPfe));
		}
	}

	SetGUCLocally(psprintf("%s.forceUseIndexIfAvailable", ApiGucPrefix), "true");

	if (disableSeqAndBitmapScan)
	{
		SetGUCLocally("enable_seqscan", "false");
		SetGUCLocally("enable_bitmapscan", "false");
	}

	uint64 rowsCount = ExtensionExecuteCappedStatementWithArgsViaSPI(
		cmdStrDeleteRows->data,
		argCount,
		argTypes,
		argValues, argNulls,
		readOnly,
		SPI_OK_DELETE,
		TTLPurgerStatementTimeout, TTLPurgerLockTimeout);


	double saturationRatio = 0.0;
	double batchDeleteElapsedTime = 0.0;

	/* selectiveLogging determines if also emit a log entry in addition to feature counters */
	bool logFeatureCounterEvent = false;

	if (IsTaskTimeBudgetExceeded(startTime, &batchDeleteElapsedTime, budget))
	{
		*isTaskTimeBudgetExceeded = true;

		/* If a delete query has timed out we assume a saturation ratio of 1.0.
		 * `ttl_slow_batches` will cover this scenario, but we count this as a fully
		 * saturated batch for future profing the metric and more than one way to
		 * catch issues*/
		saturationRatio = 1.0;
	}

	if (EnableTTLBatchObservability)
	{
		/*
		 *  Saturation ratio for a single batch delete query
		 *      "DELETE from <SHARD> where ctid in (<ctid of the expired rows>)
		 *      LIMIT <batchsize> FOR UPDATE SKIP LOCKED"
		 *
		 *  is defined as  (rows deleted / batch size).
		 */
		saturationRatio = (ttlDeleteBatchSize > 0) ?
						  (double) rowsCount / ttlDeleteBatchSize : 0;


		/*
		 *  We emit a `ttl_saturated_batches` feature counter if the saturation ratio is
		 *  > 0.9 (default threshold).
		 */
		if (saturationRatio >= TTLDeleteSaturationThreshold)
		{
			ReportFeatureUsage(FEATURE_USAGE_TTL_SATURATED_BATCHES);
			logFeatureCounterEvent = EnableSelectiveTTLLogging;
		}

		/*
		 *  We emit a `ttl_slow_batches` feature counter if the a TTL delete query on a shard
		 *  has timed out or taken more than 10 seconds (default threshold).
		 */
		if (*isTaskTimeBudgetExceeded ||
			batchDeleteElapsedTime >= TTLSlowBatchDeleteThresholdInMS)
		{
			ReportFeatureUsage(FEATURE_USAGE_TTL_SLOW_BATCHES);
			logFeatureCounterEvent = EnableSelectiveTTLLogging;
		}
	}

	if (*isTaskTimeBudgetExceeded || logFeatureCounterEvent || LogTTLProgressActivity)
	{
		uint64 shardId = indexEntry->shardId;
		if (shardId == 0 && strncmp(tableName, "documents_", 10) == 0)
		{
			/* Compute the shardId from the table if applicable */
			char *numEndPointer = NULL;
			uint64 parsedCollectionId = strtoull(&tableName[10], &numEndPointer, 10);
			if (parsedCollectionId == indexEntry->collectionId &&
				numEndPointer != NULL && numEndPointer[0] == '_' &&
				numEndPointer[1] != '\0')
			{
				shardId = strtoull(&numEndPointer[1], &numEndPointer, 10);
			}
		}

		elog_unredacted(
			"Number of rows deleted: %ld, collectionId = %lu, shardId=%lu, index_id=%lu, "
			"batch_size=%d, expiry_cutoff=%ld, "
			"LogTTLProgressActivity=%d,TTLSlowBatchDeleteThresholdInMS=%d, TTLDeleteSaturationThreshold=%.2f, "
			"has_pfe=%d, isTaskTimeBudgetExceeded=%d, logFeatureCounterEvent=%d, "
			"duration= %.2f, saturation_ratio=%.2f, "
			"statement_timeout=%d, lock_timeout=%d, used_hints=%d, disabled_seq_scan=%d, "
			"index_is_ordered=%d, use_desc_sort=%d",
			(int64) rowsCount, indexEntry->collectionId,
			shardId, indexEntry->indexId, ttlDeleteBatchSize,
			currentTime - indexExpiryMilliseconds, LogTTLProgressActivity,
			TTLSlowBatchDeleteThresholdInMS, TTLDeleteSaturationThreshold,
			(argCount == 2), *isTaskTimeBudgetExceeded, logFeatureCounterEvent,
			batchDeleteElapsedTime, saturationRatio,
			TTLPurgerStatementTimeout, TTLPurgerLockTimeout,
			useIndexHintsForTTLQuery, disableSeqAndBitmapScan,
			indexEntry->indexIsOrdered, useDescendingSort);
	}

	if (rowsCount > 0)
	{
		ReportFeatureUsage(FEATURE_USAGE_TTL_PURGER_CALLS);
	}

	return rowsCount;
}
