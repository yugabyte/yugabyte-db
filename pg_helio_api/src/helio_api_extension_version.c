/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/helio_api_extension_version.c
 *
 * Implementation of version information for the extension.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "utils/builtins.h"

#include "metadata/extension_version.h"

/* exports for SQL callable functions */
PG_FUNCTION_INFO_V1(get_helio_api_extended_binary_version);
PG_FUNCTION_INFO_V1(get_helio_api_binary_version);

/* GIT_VERSION is passed in as a compiler flag during builds that have git installed */
#ifdef GIT_VERSION
#define GIT_REF " gitref: " GIT_VERSION
#else
#define GIT_REF
#endif

Datum
get_helio_api_extended_binary_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(EXTENSION_VERSION_STR GIT_REF));
}


Datum
get_helio_api_binary_version(PG_FUNCTION_ARGS)
{
	PG_RETURN_TEXT_P(cstring_to_text(EXTENSION_VERSION_STR));
}
