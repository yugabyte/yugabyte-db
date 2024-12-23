/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_update.h
 *
 * Common declarations of functions for handling bson updates.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_UPDATE_H
#define BSON_UPDATE_H

#include "io/helio_bson_core.h"

typedef enum
{
	UpdateType_ReplaceDocument,
	UpdateType_Operator,
	UpdateType_AggregationPipeline
} UpdateType;

UpdateType DetermineUpdateType(pgbson *updateSpec);
void ValidateUpdateDocument(pgbson *updateSpec, pgbson *querySpec, pgbson *arrayFilters);
pgbson * BsonUpdateDocument(pgbson *sourceDocument, pgbson *updateSpec,
							pgbson *querySpec, pgbson *arrayFilters);

#endif
