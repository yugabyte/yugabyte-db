/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/aggregation/bson_aggregation_window_functions.c
 *
 *  Implementation of Window function based accumulator for $setWindowFields.
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <catalog/pg_type.h>

#include "io/helio_bson_core.h"
#include "windowapi.h"


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(bson_rank);
PG_FUNCTION_INFO_V1(bson_dense_rank);


Datum
bson_rank(PG_FUNCTION_ARGS)
{
	Datum rank = window_rank(fcinfo);
	bson_value_t finalValue = { .value_type = BSON_TYPE_INT32 };
	finalValue.value.v_int32 = DatumGetInt64(rank);
	PG_RETURN_POINTER(BsonValueToDocumentPgbson(&finalValue));
}


Datum
bson_dense_rank(PG_FUNCTION_ARGS)
{
	Datum rank = window_dense_rank(fcinfo);
	bson_value_t finalValue = { .value_type = BSON_TYPE_INT32 };
	finalValue.value.v_int32 = DatumGetInt64(rank);
	PG_RETURN_POINTER(BsonValueToDocumentPgbson(&finalValue));
}
