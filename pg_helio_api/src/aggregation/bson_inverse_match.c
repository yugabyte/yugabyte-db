/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_inverse_match.c
 *
 * Implementation of the $inverseMatch operator against every document in the aggregation.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <utils/builtins.h>

#include "io/helio_bson_core.h"

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_dollar_inverse_match);

Datum
bson_dollar_inverse_match(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(false);
}
