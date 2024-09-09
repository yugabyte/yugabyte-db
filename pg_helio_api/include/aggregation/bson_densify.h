/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_densify.h
 *
 * Common declarations of functions for handling $densify stage.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_DENSIFY_H
#define BSON_DENSIFY_H


#include "io/helio_bson_core.h"
#include "aggregation/bson_aggregation_pipeline.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

Query * HandleDensify(const bson_value_t *existingValue, Query *query,
					  AggregationPipelineBuildContext *context);

#endif
