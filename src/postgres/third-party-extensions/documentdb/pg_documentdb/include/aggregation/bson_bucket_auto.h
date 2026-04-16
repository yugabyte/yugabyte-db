/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_bucket_auto.h
 *
 * Common declarations of functions for handling $bucketAuto stage.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_BUCKETAUTO_H
#define BSON_BUCKETAUTO_H

#include "io/bson_core.h"
#include "aggregation/bson_aggregation_pipeline.h"
#include "aggregation/bson_aggregation_pipeline_private.h"

Query * HandleBucketAuto(const bson_value_t *existingValue, Query *query,
						 AggregationPipelineBuildContext *context);

#endif
