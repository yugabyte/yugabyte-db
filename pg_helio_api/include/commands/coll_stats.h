/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/coll_stats.h
 *
 * Exports from the implementation of mongo_api_v1.coll_stats.
 *
 *-------------------------------------------------------------------------
 */
#ifndef COLL_STATS_H
#define COLL_STATS_H

#include "metadata/collection.h"
#include "utils/array.h"

void GetMongoCollectionShardOidsAndNames(MongoCollection *collection,
										 ArrayType **shardIdArray,
										 ArrayType **shardNames);

#endif
