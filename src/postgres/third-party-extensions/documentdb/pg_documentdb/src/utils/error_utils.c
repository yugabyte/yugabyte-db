/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/error_utils.c
 *
 * Utilities for throwing the DocumentDB errors from SQL functions
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/builtins.h>
#include "utils/documentdb_errors.h"
#include "utils/version_utils.h"
#include "utils/error_utils.h"
#include "utils/documentdb_pg_compatibility.h"

format_log_hook unredacted_log_emit_hook = NULL;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(command_convert_mongo_error_to_postgres);
PG_FUNCTION_INFO_V1(command_throw_mongo_error);

/*
 * This method is useful in cases where the error codes can't be thrown from SQL directly
 * Postgres imposes a restriction on error codes to not use lower case letters or special
 * characters and only allows usage of [A-Z0-9] characters but today the range of DocumentDB error
 * codes consider all the combination and if such error code is thrown directly from SQL,
 * then it results in `unrecognized error`.
 *
 * e.g.: f100Y (Namespace not sharded error with code 118)
 *
 * This function is useful in those scenarios and its UDF should be called as:
 *
 * PERFORM ApiInternalSchema.throw_mongo_error(118, 'Collection x.y is not sharded.');
 *
 * [Deprecated] : starting 1.22
 * [Update] : starting 1.22, this function is deprecated and should not be used.
 * and return the deprecated error.
 */
Datum
pg_attribute_noreturn()
command_throw_mongo_error(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_WARNING_DEPRECATED_FEATURE),
					errmsg("command_throw_mongo_error function is deprecated now")));
}


/*
 * This method prints the 5 character Postgres error code for the given mongo error code.
 *
 * [Deprecated] : starting 1.22
 * Update: now the 5 character for an error can be easily found from documentdb_errors.h
 */
Datum
command_convert_mongo_error_to_postgres(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(ERRCODE_WARNING_DEPRECATED_FEATURE),
					errmsg("command_convert_mongo_error_to_postgres "
						   "function is deprecated now")));
}
