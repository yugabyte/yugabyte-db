/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_aggregation_statistics.h
 *
 * Exports for the window operator for stage: $setWindowField
 *
 *-------------------------------------------------------------------------
 */
#include "aggregation/bson_aggregation_window_operators.h"

bool ParseInputWeightForExpMovingAvg(const bson_value_t *opValue,
									 bson_value_t *inputExpression,
									 bson_value_t *weightExpression,
									 bson_value_t *decimalWeightValue);
