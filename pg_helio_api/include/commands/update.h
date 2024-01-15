/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/update.h
 *
 * Exports related to implementation of a single-document update.
 *
 *-------------------------------------------------------------------------
 */
#ifndef UPDATE_H
#define UPDATE_H

#include <postgres.h>

#include "metadata/collection.h"


/*
 * UpdateReturnValue specifies whether an update should return
 * no document, the old document, or the new document.
 */
typedef enum
{
	UPDATE_RETURNS_NONE,
	UPDATE_RETURNS_OLD,
	UPDATE_RETURNS_NEW
} UpdateReturnValue;

/*
 * UpdateOneParams describes update operation for a single document.
 */
typedef struct
{
	/* update only documents matching this query */
	pgbson *query;

	/* apply this update */
	pgbson *update;

	/* whether to use upsert if no documents match */
	int isUpsert;

	/* sort order to use when selecting 1 row */
	pgbson *sort;

	/* whether to return a document */
	UpdateReturnValue returnDocument;

	/* fields to return if returning a document */
	pgbson *returnFields;

	/* array filters specified in the update */
	pgbson *arrayFilters;
} UpdateOneParams;


/*
 * UpdateOneResult reflects the result of a single-row update
 * on a single shard, which may be a delete.
 */
typedef struct
{
	/* whether one row matched the query and was updated */
	bool isRowUpdated;

	/* whether we found a document but it was not affected by the update spec */
	bool updateSkipped;

	/* update result came from a retry record */
	bool isRetry;

	/* shard key value changed, reinsertDocument document needs to be inserted */
	pgbson *reinsertDocument;

	/*
	 * Value of the (maybe projected) original or new  document, if requested
	 * and matched any.
	 */
	pgbson *resultDocument;

	/* upserted document ID */
	pgbson *upsertedObjectId;
} UpdateOneResult;


void UpdateOne(MongoCollection *collection, UpdateOneParams *updateOneParams,
			   int64 shardKeyHash, text *transactionId, UpdateOneResult *result);

#endif
