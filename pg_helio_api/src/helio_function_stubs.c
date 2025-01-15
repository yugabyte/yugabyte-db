/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/helio_function_stubs.c
 *
 * Function stubs for renamed C functions.
 * When renaming C functions, old extension upgrade scripts will cease to work.
 * In order to maintain compatibility, we add stubs to the new functions and map from
 * the old to the new functions here.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>

extern Datum documentdb_core_bson_to_bson(PG_FUNCTION_ARGS);
extern Datum documentdb_extension_create_user(PG_FUNCTION_ARGS);
extern Datum documentdb_extension_drop_user(PG_FUNCTION_ARGS);
extern Datum documentdb_extension_update_user(PG_FUNCTION_ARGS);
extern Datum documentdb_extension_get_users(PG_FUNCTION_ARGS);
extern void DocumentDBBackgroundWorkerMain(Datum);

PG_FUNCTION_INFO_V1(helio_core_bson_to_bson);
Datum
helio_core_bson_to_bson(PG_FUNCTION_ARGS)
{
	return documentdb_core_bson_to_bson(fcinfo);
}


PG_FUNCTION_INFO_V1(helio_extension_create_user);
Datum
helio_extension_create_user(PG_FUNCTION_ARGS)
{
	return documentdb_extension_create_user(fcinfo);
}


PG_FUNCTION_INFO_V1(helio_extension_drop_user);
Datum
helio_extension_drop_user(PG_FUNCTION_ARGS)
{
	return documentdb_extension_drop_user(fcinfo);
}


PG_FUNCTION_INFO_V1(helio_extension_update_user);
Datum
helio_extension_update_user(PG_FUNCTION_ARGS)
{
	return documentdb_extension_update_user(fcinfo);
}


PG_FUNCTION_INFO_V1(helio_extension_get_users);
Datum
helio_extension_get_users(PG_FUNCTION_ARGS)
{
	return documentdb_extension_get_users(fcinfo);
}


PGDLLEXPORT void HelioBackgroundWorkerMain(Datum);
void
HelioBackgroundWorkerMain(Datum main_arg)
{
	DocumentDBBackgroundWorkerMain(main_arg);
}
