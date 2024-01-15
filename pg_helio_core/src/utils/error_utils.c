/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/error_utils.c
 *
 * Utilities for throwing the Mongo errors from SQL functions
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/builtins.h>

#include "utils/mongo_errors.h"


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(command_convert_mongo_error_to_postgres);
PG_FUNCTION_INFO_V1(command_throw_mongo_error);

/*
 * This method is useful in cases where the error codes can't be thrown from SQL directly
 * Postgres imposes a restriction on error codes to not use lower case letters or special
 * characters and only allows usage of [A-Z0-9] characters but today the range of mongo error
 * codes consider all the combination and if such error code is thrown directly from SQL,
 * then it results in `unrecognized error`.
 *
 * e.g.: f100Y (Namespace not sharded mongo error with code 118)
 *
 * This function is useful in those scenarios and its UDF should be called as:
 *
 * PERFORM mongo_api_internal.throw_mongo_error(118, 'Collection x.y is not sharded.');
 */
Datum
pg_attribute_noreturn()
command_throw_mongo_error(PG_FUNCTION_ARGS)
{
	int mongoErrorCode = PG_GETARG_INT32(0);
	text *msg = PG_GETARG_TEXT_P(1);
	int apiErrorCode = mongoErrorCode + _ERRCODE_MONGO_ERROR_FIRST;
	Assert(EreportCodeIsMongoError(apiErrorCode) && msg != NULL);

	ereport(ERROR, (errcode(apiErrorCode), errmsg("%s", text_to_cstring(msg))));
}


/*
 * This method prints the 5 character Postgres error code for the given mongo error code.
 */
Datum
command_convert_mongo_error_to_postgres(PG_FUNCTION_ARGS)
{
	int mongoErrorCode = PG_GETARG_INT32(0);
	int result = mongoErrorCode + _ERRCODE_MONGO_ERROR_FIRST;

	ereport(INFO, (errcode(result), errmsg("Converted mongo error code to PG")));
	PG_RETURN_VOID();
}
