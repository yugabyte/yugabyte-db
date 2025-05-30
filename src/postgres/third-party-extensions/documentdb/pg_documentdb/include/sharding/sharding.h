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

int64 ComputeShardKeyHashForDocument(pgbson *shardKey, uint64_t collectionId,
									 pgbson *document);
bool ComputeShardKeyHashForQuery(pgbson *shardKey, uint64_t collectionId, pgbson *query,
								 int64 *shardKeyHash);

Expr * ComputeShardKeyExprForQueryValue(pgbson *shardKey, uint64_t collectionId, const
										bson_value_t *queryDocument, int32_t
										collectionVarno);

Expr * CreateShardKeyValueFilter(int collectionVarNo, Const *valueConst);
#endif
