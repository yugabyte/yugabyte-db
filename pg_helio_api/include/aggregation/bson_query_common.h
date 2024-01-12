/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/bson/bson_query_common.h
 *
 * Private and common declarations of functions for handling bson query
 * Shared across runtime and index implementations.
 *
 *-------------------------------------------------------------------------
 */

#ifndef BSON_QUERY_COMMON_H
#define BSON_QUERY_COMMON_H

#include "io/bson_core.h"
#include "utils/mongo_errors.h"

/*
 * This struct defines the parameters for a range query.
 */
typedef struct DollarRangeParams
{
	bson_value_t minValue;
	bson_value_t maxValue;
	bool isMinInclusive;
	bool isMaxInclusive;
} DollarRangeParams;

DollarRangeParams * ParseQueryDollarRange(pgbson *rangeFilter);

#endif
