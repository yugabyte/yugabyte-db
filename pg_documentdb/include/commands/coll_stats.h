/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/commands/coll_stats.h
 *
 * Exports from the implementation of ApiSchema.coll_stats.
 *
 *-------------------------------------------------------------------------
 */
#ifndef COLL_STATS_H
#define COLL_STATS_H

#include "metadata/collection.h"
#include "utils/array.h"

bool GetMongoCollectionShardOidsAndNames(MongoCollection *collection,
										 ArrayType **shardIdArray,
										 ArrayType **shardNames);

#endif
