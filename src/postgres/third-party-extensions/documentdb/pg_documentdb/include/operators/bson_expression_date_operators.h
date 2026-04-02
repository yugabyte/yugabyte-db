/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/bson_expression_date_operators.h
 *
 * Common function declarations for method used for date processing in aggregation expressions
 *
 *-------------------------------------------------------------------------
 */

 #ifndef BSON_EXPRESSION_DATE_OPERATORS_H
 #define BSON_EXPRESSION_DATE_OPERATORS_H

 #include "io/bson_core.h"

void StringToDateWithDefaultFormat(bson_value_t *dateString, bson_value_t *result);

 #endif
