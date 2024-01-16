/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_update_common.h
 *
 * Private and common declarations of functions for handling bson updates
 * Shared across update implementations.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_UPDATE_COMMON_H
#define BSON_UPDATE_COMMON_H

#include "io/helio_bson_core.h"
#include "utils/mongo_errors.h"


struct AggregationPipelineUpdateState;
struct BsonIntermediatePathNode;
typedef struct BsonUpdateTracker BsonUpdateTracker;

/* aggregation */

struct AggregationPipelineUpdateState * GetAggregationPipelineUpdateState(
	pgbson *updateSpec);

pgbson * ProcessAggregationPipelineUpdate(pgbson *sourceDoc,
										  const struct AggregationPipelineUpdateState *
										  updateState,
										  bool isUpsert);


/* Update workflows */

const struct BsonIntermediatePathNode * GetOperatorUpdateState(pgbson *updateSpec,
															   pgbson *querySpec,
															   pgbson *arrayFilters);
pgbson * ProcessUpdateOperatorWithState(pgbson *sourceDoc,
										const struct BsonIntermediatePathNode *
										updateState,
										bool isUpsert,
										BsonUpdateTracker *updateDescription);


/*
 * Throws an error that the _id has been detected as changed in the process of updating the document.
 * Call it when UpdateType is OperatorUpdate and _id has changed.
 */
inline static void
pg_attribute_noreturn()
ThrowIdPathModifiedErrorForOperatorUpdate()
{
	ereport(ERROR, (errcode(MongoImmutableField),
					errmsg(
						"Performing an update on the path '_id' would modify the immutable field '_id'")));
}


/*
 * Throws an error that the path in the projection tree has a prior update requested that would conflict
 * with the update requested.
 */
inline static void
pg_attribute_noreturn()
ThrowPathConflictError(const char * requestedPath, const char * existingPath)
{
	ereport(ERROR, (errcode(MongoConflictingUpdateOperators),
					errmsg("Updating the path '%s' would create a conflict at '%s'",
						   requestedPath, existingPath)));
}

#endif
