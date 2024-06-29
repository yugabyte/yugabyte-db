/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/commands/users.c
 *
 * Implementation of user CRUD functions.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "utils/mongo_errors.h"
#include "utils/query_utils.h"

PG_FUNCTION_INFO_V1(helio_extension_create_user);
PG_FUNCTION_INFO_V1(helio_extension_drop_user);
PG_FUNCTION_INFO_V1(helio_extension_update_user);
PG_FUNCTION_INFO_V1(helio_extension_get_users);

/*
 * helio_extension_create_user implements the
 * core logic to create a user
 */
Datum
helio_extension_create_user(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoCommandNotSupported),
					errmsg("Method not supported"),
					errhint("Method not supported")));
}


/*
 * helio_extension_drop_user implements the
 * core logic to drop a user
 */
Datum
helio_extension_drop_user(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoCommandNotSupported),
					errmsg("Method not supported"),
					errhint("Method not supported")));
}


/*
 * helio_extension_update_user implements the
 * core logic to update a user
 */
Datum
helio_extension_update_user(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoCommandNotSupported),
					errmsg("Method not supported"),
					errhint("Method not supported")));
}


/*
 * helio_extension_get_users implements the
 * core logic to get user info
 */
Datum
helio_extension_get_users(PG_FUNCTION_ARGS)
{
	ereport(ERROR, (errcode(MongoCommandNotSupported),
					errmsg("Method not supported"),
					errhint("Method not supported")));
}
