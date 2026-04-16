/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * transaction/retryable_writes.h
 *
 * Common declarations for retryable writes-related functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef RETRYABLE_WRITES_H
#define RETRYABLE_WRITES_H


/*
 * RetryableWriteResult stores information about the result of a retryable
 * write to be able to return the same result in case of a retry.
 */
typedef struct RetryableWriteResult
{
	/* object ID that was inserted, updated, or deleted */
	pgbson *objectId;

	/* whether rows affected by the write */
	bool rowsAffected;

	/* shard key value of the write */
	int64 shardKeyValue;

	/*
	 * Value of the (maybe projected) old or new document that next trial
	 * should report (or NULL if not applicable to the command or the command
	 * couldn't match any documents).
	 */
	pgbson *resultDocument;
} RetryableWriteResult;


bool FindRetryRecordInAnyShard(uint64 collectionId, text *transactionId,
							   RetryableWriteResult *writeResult);
bool DeleteRetryRecord(uint64 collectionId, int64 shardKeyValue,
					   text *transactionId, RetryableWriteResult *writeResult);
void InsertRetryRecord(uint64 collectionId, int64 shardKeyValue,
					   text *transactionId, pgbson *objectId, bool rowsAffected,
					   pgbson *resultDocument);

#endif
