/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/cluster_versioning.c
 *
 * Utilities that Provide extension functions to handle version upgrade
 * scenarios for the current extension.
 *
 *-------------------------------------------------------------------------
 */

#include "utils/version_utils.h"
#include "metadata/metadata_cache.h"
#include <utils/builtins.h>
#include "utils/query_utils.h"
#include "executor/spi.h"
#include "utils/inval.h"
#include "utils/version_utils_private.h"

PG_FUNCTION_INFO_V1(invalidate_cluster_version);
PG_FUNCTION_INFO_V1(get_current_cached_cluster_version);

/*
 * Invalidates the version cache and the metadata cache.
 */
Datum
invalidate_cluster_version(PG_FUNCTION_ARGS)
{
	InvalidateVersionCache();

	/* Also invalidate the metadata_cache */
	InvalidateCollectionsCache();
	CacheInvalidateRelcacheAll();
	PG_RETURN_VOID();
}


/*
 * Returns the text version of the cluster version
 * Used for debugging and testing purposes.
 */
Datum
get_current_cached_cluster_version(PG_FUNCTION_ARGS)
{
	ExtensionVersion version = RefreshCurrentVersion();
	StringInfo s = makeStringInfo();
	appendStringInfo(s, "Major = %d, Minor = %d, Patch = %d",
					 version.Major, version.Minor, version.Patch);

	PG_RETURN_DATUM(CStringGetTextDatum(s->data));
}
