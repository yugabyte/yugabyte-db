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

#include "io/helio_bson_core.h"
#include "query/helio_bson_compare.h"
#include "metadata/metadata_cache.h"
#include "utils/list_utils.h"
#include "utils/query_utils.h"
#include "utils/feature_counter.h"

extern bool LogTTLProgressActivity;
extern int TTLPurgerStatementTimeout;
extern int MaxTTLDeleteBatchSize;
extern int TTLPurgerLockTimeout;


PG_FUNCTION_INFO_V1(delete_expired_rows_for_index);

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
	pgbson *indexKeyDocument = PG_GETARG_PGBSON(2);
	pgbson *partialFilterDocument = PG_GETARG_MAYBE_NULL_PGBSON(3);
	int64 currentTime = DatumGetInt64(PG_GETARG_DATUM(4));
	int indexExpiryCutOffInMS = DatumGetInt32(PG_GETARG_DATUM(5)) * 1000;
	int ttlDeleteBatchSize = DatumGetInt32(PG_GETARG_DATUM(6));
	uint64 shardId = DatumGetInt64(PG_GETARG_DATUM(7));

	ttlDeleteBatchSize = (ttlDeleteBatchSize != -1) ? ttlDeleteBatchSize :
						 MaxTTLDeleteBatchSize;

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
	 *
	 *  Postgres currently supports "FOR UPDATE SKIP LOCKED" which can be specified during the SELECT statement -  this instructs
	 *  the query engine to lock the selected records with the intention of update, while skipping any records that been concurrently
	 *  locked by some other transactions to improve throughput. We don't use "FOR UPDATE SKIP LOCKED" because it not supported by
	 *  Citus for sharded collection.
	 */

	StringInfo cmdStrDelteRows = makeStringInfo();

	/* optimization for unsharded collection to avoid 2PC */
	appendStringInfo(cmdStrDelteRows,
					 "DELETE FROM %s.documents_" UINT64_FORMAT "_" UINT64_FORMAT
					 " WHERE ctid IN (SELECT ctid FROM %s.documents_"
					 UINT64_FORMAT "_" UINT64_FORMAT
					 " WHERE %s.bson_dollar_lt(document, $1::%s) ",
					 ApiDataSchemaName, collectionId, shardId,
					 ApiDataSchemaName, collectionId, shardId,
					 ApiCatalogSchemaName, FullBsonTypeName);

	if (partialFilterDocument != NULL)
	{
		appendStringInfo(cmdStrDelteRows, "AND document OPERATOR(%s.@@) $2::%s",
						 ApiCatalogSchemaName, FullBsonTypeName);
	}

	appendStringInfo(cmdStrDelteRows, " LIMIT %d FOR UPDATE SKIP LOCKED) ",
					 ttlDeleteBatchSize);

	bool readOnly = false;
	char *argNulls = NULL;
	int argCount = (partialFilterDocument == NULL) ? 1 : 2;
	Oid argTypes[2];
	Datum argValues[2];

	pgbson_writer writer;
	PgbsonWriterInit(&writer);
	bson_value_t expiryField = { 0 };
	expiryField.value_type = BSON_TYPE_DATE_TIME;
	expiryField.value.v_datetime = currentTime - indexExpiryCutOffInMS;
	PgbsonWriterAppendValue(&writer, indexKey, strlen(indexKey), &expiryField);

	argTypes[0] = BYTEAOID;
	argValues[0] = PointerGetDatum(CastPgbsonToBytea(PgbsonWriterGetPgbson(&writer)));

	if (argCount == 2)
	{
		argTypes[1] = BYTEAOID;
		argValues[1] = PointerGetDatum(CastPgbsonToBytea(partialFilterDocument));
	}

	uint64 rowsCount = ExtensionExecuteCappedStatementWithArgsViaSPI(
		cmdStrDelteRows->data,
		argCount,
		argTypes,
		argValues, argNulls,
		readOnly,
		SPI_OK_DELETE,
		TTLPurgerStatementTimeout, TTLPurgerLockTimeout);

	if (LogTTLProgressActivity)
	{
		elog(LOG,
			 "Number of rows deleted: %ld, collection_id = %lu, shard_id=%lu, index_id=%lu, batch_size=%d, expiry_cutoff=%ld, has_pfe=%s, statement_timeout=%d, lock_timeout=%d",
			 (int64) rowsCount, collectionId, shardId, indexId,
			 ttlDeleteBatchSize,
			 currentTime - indexExpiryCutOffInMS, (argCount == 2) ? "true" : "false",
			 TTLPurgerStatementTimeout, TTLPurgerLockTimeout);
	}

	if (rowsCount > 0)
	{
		ReportFeatureUsage(FEATURE_TTL_PURGER_CALLS);
	}

	PG_RETURN_INT64((int64) rowsCount);
}
