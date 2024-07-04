/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_bucket.h
 *
 * Common function declarations for method used for $bucket aggregation stage
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_EXPRESSION_BUCKET_OPERATOR_H
#define BSON_EXPRESSION_BUCKET_OPERATOR_H

#include "io/helio_bson_core.h"

void RewriteBucketGroupSpec(const bson_value_t *bucketSpec, bson_value_t *groupSpec);

#endif
