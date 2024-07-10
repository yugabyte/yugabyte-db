/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregation_window_operators.h
 *
 * Exports for the window operator for stage: $setWindowField
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_AGGREGATION_WINDOW_OPERATORS_H
#define BSON_AGGREGATION_WINDOW_OPERATORS_H

#include <nodes/parsenodes.h>

#include "io/helio_bson_core.h"
#include "metadata/metadata_cache.h"
#include "aggregation/bson_aggregation_pipeline.h"

#include "aggregation/bson_aggregation_pipeline_private.h"

Query * HandleSetWindowFields(const bson_value_t *existingValue, Query *query,
							  AggregationPipelineBuildContext *context);
#endif
