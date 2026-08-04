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
void ParseNamespaceName(const char *namespacePath, char **databaseName,
						char **collectionName);
int64 ComputeShardKeyHashForDocument(pgbson *shardKey, uint64_t collectionId,
									 pgbson *document);
bool ComputeShardKeyHashForQuery(pgbson *shardKey, uint64_t collectionId, pgbson *query,
								 int64 *shardKeyHash, bool *isShardKeyCollationAware);
bool ComputeShardKeyHashForQueryValue(pgbson *shardKey, uint64_t collectionId, const
									  bson_value_t *query,
									  int64 *shardKeyHash,
									  bool *isShardKeyCollationAware);

Expr * ComputeShardKeyExprForQueryValue(pgbson *shardKey, uint64_t collectionId, const
										bson_value_t *queryDocument,
										int32_t collectionVarno,
										bool *isShardKeyCollationAware);

Expr * CreateShardKeyValueFilter(int collectionVarNo, Const *valueConst);
#endif
