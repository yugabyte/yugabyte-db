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

extern bool LogTTLProgressActivity;
extern int TTLPurgerStatementTimeout;
extern int MaxTTLDeleteBatchSize;
extern int TTLPurgerLockTimeout;
extern char *ApiGucPrefix;
extern int SingleTTLTaskTimeBudget;

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
	/* The collection id for the given entry. */
	uint64 collectionId;

	/* The TTL index id */
	uint64 indexId;

	/* The index key document to validate expiry against. */
	Datum indexKeyDatum;

	/* The partial filter expression for the index to check if a document is applicable for deletion. */
	Datum indexPfeDatum;

	/* The expiry time in seconds for the index key. */
	int32 indexExpireAfterSeconds;
} TtlIndexEntry;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */

static uint64 DeleteExpiredRowsForIndexCore(char *tableName, TtlIndexEntry *indexEntry,
											int64 currentTime, int32 batchSize);
static bool IsTaskTimeBudgetExceeded(instr_time startTime, double *elapsedTime);

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(delete_expired_rows_for_index);
PG_FUNCTION_INFO_V1(delete_expired_rows);

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
		.indexId = indexId,
		.indexKeyDatum = indexKeyDatum,
		.indexPfeDatum = partialFilterDatum,
		.indexExpireAfterSeconds = indexExpiry
	};

	uint64 rowsCount = DeleteExpiredRowsForIndexCore(tableName, &indexEntry, currentTime,
													 ttlDeleteBatchSize);

	PG_RETURN_INT64((int64) rowsCount);
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

	/* Get TTL indexes and their collections */
	appendStringInfo(cmdGetIndexes,
					 "SELECT index_id, collection_id, (index_spec).index_key, "
					 "(index_spec).index_pfe, (index_spec).index_expire_after_seconds FROM %s.collection_indexes "
					 "WHERE index_is_valid AND (index_spec).index_expire_after_seconds >= 0 "
					 "ORDER BY collection_id, index_id",
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

				oldContext = MemoryContextSwitchTo(priorMemoryContext);
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
			if (ConditionalLockRelationOid(currentCollection.relationId, RowShareLock))
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

		volatile bool shouldStop = false;

		/* Delete records on all tables for this collection */
		for (volatile int i = 0; i < numItems; i++)
		{
			char *tableName = text_to_cstring(DatumGetTextP(itemDatums[i]));

			clock_gettime(CLOCK_REALTIME, &timeSpec);

			time_t epochSeconds = timeSpec.tv_sec;
			uint32_t millisecondsInSecond = timeSpec.tv_nsec / 1000000;
			uint64_t epochMilliseconds = (epochSeconds * 1000UL) + millisecondsInSecond;

			PG_TRY();
			{
				DeleteExpiredRowsForIndexCore(tableName, ttlIndexEntry, epochMilliseconds,
											  batchSize);

				double elapsedTime = 0.0;
				if (IsTaskTimeBudgetExceeded(startTime, &elapsedTime))
				{
					/* If exceeded time, mark as should stop but still commit this deletion. */
					shouldStop = true;
				}

				if (LogTTLProgressActivity)
				{
					ereport(LOG, errmsg("TTL job elapsed time: %fms, limit: %dms",
										elapsedTime, SingleTTLTaskTimeBudget));
				}

				/* Commit the deletion. */
				PopAllActiveSnapshots();
				CommitTransactionCommand();
				StartTransactionCommand();
			}
			PG_CATCH();
			{
				ErrorData *edata = CopyErrorDataAndFlush();
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
		}

		if (IsTaskTimeBudgetExceeded(startTime, NULL))
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


/* Based on the task start time it checks if we have exceeded the ttl task budget defined in
 * the SingleTTLTaskTimeBudget GUC. */
static bool
IsTaskTimeBudgetExceeded(instr_time startTime, double *elapsedTime)
{
	instr_time current;
	INSTR_TIME_SET_CURRENT(current);
	INSTR_TIME_SUBTRACT(current, startTime);
	double elapsed = INSTR_TIME_GET_MILLISEC(current);

	if (elapsedTime != NULL)
	{
		*elapsedTime = elapsed;
	}

	if (elapsed > (double) SingleTTLTaskTimeBudget)
	{
		ereport(LOG, errmsg("TTL Index delete rows exceeded time budget: %dms.",
							SingleTTLTaskTimeBudget));
		return true;
	}

	return false;
}


/* Deletes the rows that have expired for the given table name and ttl entry information.
 * It deletes the number of items specified on the batchSize that have expired based on the index entry expiry value. */
static uint64
DeleteExpiredRowsForIndexCore(char *tableName, TtlIndexEntry *indexEntry, int64
							  currentTime, int32 batchSize)
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

	if (indexPfe != NULL)
	{
		appendStringInfo(cmdStrDeleteRows, "AND document OPERATOR(%s.@@) $2::%s",
						 ApiCatalogSchemaName, FullBsonTypeName);
	}

	appendStringInfo(cmdStrDeleteRows, " LIMIT %d FOR UPDATE SKIP LOCKED) ",
					 ttlDeleteBatchSize);

	bool readOnly = false;
	char *argNulls = NULL;
	int argCount = (indexPfe == NULL) ? 1 : 2;
	Oid argTypes[2];
	Datum argValues[2];

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_value_t expiryField = { 0 };
	expiryField.value_type = BSON_TYPE_DATE_TIME;
	expiryField.value.v_datetime = currentTime - indexExpiryMilliseconds;
	PgbsonWriterAppendValue(&writer, indexKey, strlen(indexKey), &expiryField);

	pgbson *value = PgbsonWriterGetPgbson(&writer);
	argTypes[0] = BYTEAOID;
	argValues[0] = PointerGetDatum(CastPgbsonToBytea(value));

	if (argCount == 2)
	{
		argTypes[1] = BYTEAOID;
		argValues[1] = PointerGetDatum(CastPgbsonToBytea(indexPfe));
	}

	SetGUCLocally(psprintf("%s.forceUseIndexIfAvailable", ApiGucPrefix), "true");
	uint64 rowsCount = ExtensionExecuteCappedStatementWithArgsViaSPI(
		cmdStrDeleteRows->data,
		argCount,
		argTypes,
		argValues, argNulls,
		readOnly,
		SPI_OK_DELETE,
		TTLPurgerStatementTimeout, TTLPurgerLockTimeout);

	if (LogTTLProgressActivity)
	{
		ereport(LOG,
				errmsg(
					"Number of rows deleted: %ld, table = %s, index_id=%lu, batch_size=%d, expiry_cutoff=%ld, has_pfe=%s, statement_timeout=%d, lock_timeout=%d",
					(int64) rowsCount, tableName, indexEntry->indexId,
					ttlDeleteBatchSize,
					currentTime - indexExpiryMilliseconds, (argCount == 2) ? "true" :
					"false",
					TTLPurgerStatementTimeout, TTLPurgerLockTimeout));
	}

	if (rowsCount > 0)
	{
		ReportFeatureUsage(FEATURE_TTL_PURGER_CALLS);
	}

	return rowsCount;
}
