/* -------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson/io/bson_analyze.c
 *
 * Implementation of the BSON analyze logic.
 *
 *-------------------------------------------------------------------------
 */


#include <postgres.h>
#include <fmgr.h>
#include <commands/vacuum.h>

PG_FUNCTION_INFO_V1(bson_typanalyze);


/*
 * Implement type analyze for bson.
 * Right now thunks to the default - but in the future
 * will be extended to support more.
 */
Datum
bson_typanalyze(PG_FUNCTION_ARGS)
{
	VacAttrStats *stats = (VacAttrStats *) PG_GETARG_POINTER(0);
	PG_RETURN_BOOL(std_typanalyze(stats));
}
