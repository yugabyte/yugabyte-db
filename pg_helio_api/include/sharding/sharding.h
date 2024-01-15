/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * sharding/sharding.h
 *
 * Common declarations for sharding functions.
 *
 *-------------------------------------------------------------------------
 */

#ifndef SHARDING_H
#define SHARDING_H

#include "metadata/collection.h"

/*
 * ShardKeyFieldValues is used to keep track of shard key values in a query.
 * If we find a value for each shard key, then we will add a filter on
 * the shard_key_value column.
 *
 * We currently only extract a single shard key value based on top-level
 * filters and $and only.
 */
typedef struct ShardKeyFieldValues
{
	/* ordered array of shard key fields */
	const char **fields;
	int fieldCount;

	/* array specifying whether a field was set in the query */
	bool *isSet;

	/* array of shard key values corresponding to shard key fields */
	bson_value_t *values;
} ShardKeyFieldValues;


int64 ComputeShardKeyHashForDocument(pgbson *shardKey, uint64_t collectionId,
									 pgbson *document);
bool ComputeShardKeyHashForQuery(pgbson *shardKey, uint64_t collectionId, pgbson *query,
								 int64 *shardKeyHash);
bool ComputeShardKeyHashForQueryValue(pgbson *shardKey, uint64_t collectionId, const
									  bson_value_t *query,
									  int64 *shardKeyHash);
void FindShardKeyFieldValuesForQuery(bson_iter_t *queryDocument,
									 ShardKeyFieldValues *shardKeyValues);

#endif
