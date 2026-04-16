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

#include "io/bson_core.h"

typedef enum
{
	UpdateType_ReplaceDocument,
	UpdateType_Operator,
	UpdateType_AggregationPipeline
} UpdateType;

UpdateType DetermineUpdateType(const bson_value_t *updateSpec);
void ValidateUpdateDocument(const bson_value_t *updateSpec, const bson_value_t *querySpec,
							const bson_value_t *arrayFilters, const
							bson_value_t *variableSpec);
pgbson * BsonUpdateDocument(pgbson *sourceDocument, const bson_value_t *updateSpec,
							const bson_value_t *querySpec, const
							bson_value_t *arrayFilters, const bson_value_t *variableSpec);

#endif
