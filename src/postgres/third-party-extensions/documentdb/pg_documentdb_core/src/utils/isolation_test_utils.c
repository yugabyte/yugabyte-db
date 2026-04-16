/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/isolation_test_utils.c
 *
 * Utilities for Isolation Tests are defined here
 * and mainly these are surfaced as SQL APIs in isolation_setup schedule
 *
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <fmgr.h>
#include <utils/builtins.h>

#include "io/bson_core.h"


/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */

PG_FUNCTION_INFO_V1(replace_json_braces_get_bson);

/*
 * This method should receive a json with `{` `}` replaced with
 * alternate characters and it replaces it inside the method again and
 * returns a bson out of it.
 *
 * This is mainly done because in isolation tests there is no way to include
 * `}` character in any of the SQL blocks in spec files. So this is handy utility
 * to work with isolation tests
 */
Datum
replace_json_braces_get_bson(PG_FUNCTION_ARGS)
{
	text *bsontext = PG_GETARG_TEXT_P(0);
	text *openedBraceReplacedText = PG_GETARG_TEXT_P(1);
	text *closedBraceReplacedText = PG_GETARG_TEXT_P(2);
	char *bsonString = text_to_cstring(bsontext);
	char *openedBraceReplacedWith = text_to_cstring(openedBraceReplacedText);
	char *closedBraceReplacedWith = text_to_cstring(closedBraceReplacedText);
	Assert(strlen(openedBraceReplacedWith) == 1 && strlen(closedBraceReplacedWith) == 1);

	int bsonLength = strlen(bsonString);
	for (int i = 0; i < bsonLength; i++)
	{
		if (bsonString[i] == openedBraceReplacedWith[0])
		{
			bsonString[i] = '{';
		}
		else if (bsonString[i] == closedBraceReplacedWith[0])
		{
			bsonString[i] = '}';
		}
	}
	pgbson *bson = PgbsonInitFromJson(bsonString);
	PG_RETURN_POINTER(bson);
}
