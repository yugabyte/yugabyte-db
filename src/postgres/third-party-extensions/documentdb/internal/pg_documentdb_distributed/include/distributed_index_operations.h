/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/distributed_index_operations.h
 *
 * The implementation for distributed index operations.
 *
 *-------------------------------------------------------------------------
 */
#ifndef DOCUMENTDB_DISTRIBUTED_INDEX_OPS_H
#define DOCUMENTDB_DISTRIBUTED_INDEX_OPS_H

void UpdateDistributedPostgresIndex(uint64_t collectionId, int indexId, int operation,
									bool value);

#endif
