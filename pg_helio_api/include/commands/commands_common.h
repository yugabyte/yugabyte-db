/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/commands_common.h
 *
 * Common declarations of Mongo commands.
 *
 *-------------------------------------------------------------------------
 */

#ifndef COMMANDS_COMMON_H
#define COMMANDS_COMMON_H

#include <utils/elog.h>
#include <metadata/collection.h>
#include <io/helio_bson_core.h>

/*
 * Maximum size of a output bson document is 16MB.
 */
#define BSON_MAX_ALLOWED_SIZE (16 * 1024 * 1024)

/*
 * Maximum size of document produced by an intermediate stage of an aggregation pipeline.
 * This is a Native Mongo constrains. For example, if the pipeline is [$facet, $unwind],
 * $facet is allowed to generate document that is larger than 16MB, since $unwind can
 * break them into smaller document. However, $facet is not allowed to generate a document
 * that's larger than 100MB.
 */
#define BSON_MAX_ALLOWED_SIZE_INTERMEDIATE (100 * 1024 * 1024)

/* StringView that represents the _id field */
extern PGDLLIMPORT const StringView IdFieldStringView;


/*
 * helio_api.enable_create_collection_on_insert GUC determines whether
 * an insert into a non-existent collection should create a collection.
 */
extern bool EnableCreateCollectionOnInsert;

/*
 * Whether or not write operations are inlined or if they are dispatched
 * to a remote shard. For single node scenarios like HelioDB that don't need
 * distributed dispatch. Reset in scenarios that need distributed dispatch.
 */
extern bool DefaultInlineWriteOperations;
extern int BatchWriteSubTransactionCount;
extern int MaxWriteBatchSize;

/*
 * WriteError can be part of the response of a batch write operation.
 */
typedef struct WriteError
{
	/* index in a write batch */
	int index;

	/* error code */
	int code;

	/* description of the error */
	char *errmsg;
} WriteError;


bool FindShardKeyValueForDocumentId(MongoCollection *collection, pgbson *queryDoc,
									bson_value_t *objectId, int64 *shardKeyValue);

bool IsCommonSpecIgnoredField(const char *fieldName);

WriteError * GetWriteErrorFromErrorData(ErrorData *errorData, int writeErrorIdx);

pgbson * GetObjectIdFilterFromQueryDocument(pgbson *queryDoc);


pgbson * RewriteDocumentAddObjectId(pgbson *document);
pgbson * RewriteDocumentValueAddObjectId(const bson_value_t *value);
pgbson * RewriteDocumentWithCustomObjectId(pgbson *document,
										   pgbson *objectIdToWrite);

void ValidateIdField(const bson_value_t *idValue);

#endif
