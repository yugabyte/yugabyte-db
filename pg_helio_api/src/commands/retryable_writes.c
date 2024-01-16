/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/transaction/retryable_writes.
 *
 * Implementation of retryable write bookkeeping.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"

#include "io/helio_bson_core.h"
#include "infrastructure/helio_plan_cache.h"
#include "commands/retryable_writes.h"
#include "metadata/metadata_cache.h"


/*
 * FindRetryRecordByObjectId searches for a retry record in any shard
 * returns whether it was found, and sets the rowsAffected.
 *
 * We do this only when doing a single-row write with an _id filter on a
 * collection that is not sharded by _id. In other cases, we check for
 * retryable writes directly on the shard.
 */
bool
FindRetryRecordInAnyShard(uint64 collectionId, text *transactionId,
						  RetryableWriteResult *writeResult)
{
	StringInfoData query;
	const int argCount = 1;
	Oid argTypes[1];
	Datum argValues[1];
	MemoryContext originalContext = CurrentMemoryContext;
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;

	bool retryRecordExists = false;

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query,
					 "SELECT object_id, rows_affected, shard_key_value "
					 " FROM %s.retry_" UINT64_FORMAT
					 " WHERE transaction_id = $1",
					 ApiDataSchemaName, collectionId);

	argTypes[0] = TEXTOID;
	argValues[0] = PointerGetDatum(transactionId);

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlan(collectionId, QUERY_ID_RETRY_RECORD_SELECT,
									  query.data, argTypes, argCount);

	spiStatus = SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
	Assert(spiStatus == SPI_OK_SELECT);

	if (SPI_processed > 0)
	{
		retryRecordExists = true;

		if (writeResult != NULL)
		{
			bool isNull = false;

			int columnNumber = 1;
			Datum objectIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
												SPI_tuptable->tupdesc, columnNumber,
												&isNull);
			if (!isNull)
			{
				/* copy object ID into outer memory context */
				bool typeByValue = false;
				int typeLength = -1;
				objectIdDatum = SPI_datumTransfer(objectIdDatum, typeByValue, typeLength);

				writeResult->objectId = (pgbson *) DatumGetPointer(objectIdDatum);
			}
			else
			{
				/* write affected 0 rows */
				writeResult->objectId = NULL;
			}

			columnNumber = 2;
			Datum rowsAffectedDatum = SPI_getbinval(SPI_tuptable->vals[0],
													SPI_tuptable->tupdesc, columnNumber,
													&isNull);
			Assert(!isNull);

			writeResult->rowsAffected = BoolGetDatum(rowsAffectedDatum);

			columnNumber = 3;
			Datum shardKeyValueDatum = SPI_getbinval(SPI_tuptable->vals[0],
													 SPI_tuptable->tupdesc, columnNumber,
													 &isNull);
			Assert(!isNull);

			writeResult->shardKeyValue = Int64GetDatum(shardKeyValueDatum);
		}
	}

	pfree(query.data);

	SPI_finish();
	MemoryContextSwitchTo(originalContext);

	return retryRecordExists;
}


/*
 * DeleteRetryRecordGetObjectId deletes a retry record and returns the object ID
 * that was stored in the record.
 */
bool
DeleteRetryRecord(uint64 collectionId, int64 shardKeyValue,
				  text *transactionId, RetryableWriteResult *writeResult)
{
	StringInfoData query;
	const int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;
	bool foundRetryRecord = false;

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query,
					 "DELETE FROM %s.retry_" UINT64_FORMAT
					 " WHERE shard_key_value = $1 AND transaction_id = $2"
					 " RETURNING object_id, rows_affected, result_document",
					 ApiDataSchemaName, collectionId);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyValue);

	argTypes[1] = TEXTOID;
	argValues[1] = PointerGetDatum(transactionId);

	char *argNulls = NULL;
	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlan(collectionId, QUERY_ID_RETRY_RECORD_DELETE,
									  query.data, argTypes, argCount);

	spiStatus = SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
	Assert(spiStatus == SPI_OK_DELETE_RETURNING);

	if (SPI_processed > 0)
	{
		foundRetryRecord = true;

		if (writeResult != NULL)
		{
			bool isNull = false;
			Datum objectIdDatum = SPI_getbinval(SPI_tuptable->vals[0],
												SPI_tuptable->tupdesc, 1,
												&isNull);
			if (!isNull)
			{
				/* copy object ID into outer memory context */
				bool typeByValue = false;
				int typeLength = -1;
				objectIdDatum = SPI_datumTransfer(objectIdDatum, typeByValue, typeLength);

				writeResult->objectId = (pgbson *) DatumGetPointer(objectIdDatum);
			}
			else
			{
				/* write affected 0 rows */
				writeResult->objectId = NULL;
			}

			Datum rowsAffectedDatum = SPI_getbinval(SPI_tuptable->vals[0],
													SPI_tuptable->tupdesc, 2,
													&isNull);
			Assert(!isNull);

			writeResult->rowsAffected = BoolGetDatum(rowsAffectedDatum);
			writeResult->shardKeyValue = shardKeyValue;

			Datum resultDocumentDatum = SPI_getbinval(SPI_tuptable->vals[0],
													  SPI_tuptable->tupdesc, 3,
													  &isNull);
			if (!isNull)
			{
				bool typeByValue = false;
				int typeLength = -1;
				resultDocumentDatum = SPI_datumTransfer(resultDocumentDatum, typeByValue,
														typeLength);

				writeResult->resultDocument =
					(pgbson *) DatumGetPointer(resultDocumentDatum);
			}
			else
			{
				writeResult->resultDocument = NULL;
			}
		}
	}

	pfree(query.data);

	SPI_finish();

	return foundRetryRecord;
}


/*
 * InsertRetryRecord inserts a retryable write record into the retry
 * table of a collection.
 */
void
InsertRetryRecord(uint64 collectionId, int64 shardKeyValue, text *transactionId,
				  pgbson *objectId, bool rowsAffected, pgbson *resultDocument)
{
	StringInfoData query;
	const int argCount = 5;
	Oid argTypes[5];
	Datum argValues[5];
	char argNulls[] = { ' ', ' ', ' ', ' ', ' ' };
	int spiStatus PG_USED_FOR_ASSERTS_ONLY = 0;

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query,
					 "INSERT INTO %s.retry_" UINT64_FORMAT
					 " (shard_key_value, transaction_id, object_id, "
					 "  rows_affected, result_document) "
					 " VALUES ($1, $2, $3::%s, $4, $5::%s)",
					 ApiDataSchemaName, collectionId,
					 FullBsonTypeName, FullBsonTypeName);

	argTypes[0] = INT8OID;
	argValues[0] = Int64GetDatum(shardKeyValue);

	argTypes[1] = TEXTOID;
	argValues[1] = PointerGetDatum(transactionId);

	argTypes[2] = BYTEAOID;

	if (objectId != NULL)
	{
		argValues[2] = PointerGetDatum(objectId);
		argNulls[2] = ' ';
	}
	else
	{
		argNulls[2] = 'n';
	}

	argTypes[3] = BOOLOID;
	argValues[3] = BoolGetDatum(rowsAffected);

	argTypes[4] = BYTEAOID;

	if (resultDocument != NULL)
	{
		argValues[4] = PointerGetDatum(resultDocument);
		argNulls[4] = ' ';
	}
	else
	{
		argNulls[4] = 'n';
	}

	bool readOnly = false;
	long maxTupleCount = 0;

	SPIPlanPtr plan = GetSPIQueryPlan(collectionId, QUERY_ID_RETRY_RECORD_INSERT,
									  query.data, argTypes, argCount);

	spiStatus = SPI_execute_plan(plan, argValues, argNulls, readOnly, maxTupleCount);
	Assert(spiStatus == SPI_OK_INSERT);
	Assert(SPI_processed == 1);

	pfree(query.data);

	SPI_finish();
}
