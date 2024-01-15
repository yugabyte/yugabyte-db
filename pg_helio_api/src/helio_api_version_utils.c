/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/helio_api_version_utils.c
 *
 * Utilities that Provide extension functions to handle version upgrade
 * scenarios for the current extension.
 *
 *-------------------------------------------------------------------------
 */

#include "utils/version_utils.h"

/*
 * Returns true if the cluster version is >= given major.minor.patch version
 * TODO: finish version story for helio_api. Since we haven't shipped any
 * public version yet, this just returns true.
 */
bool
IsClusterVersionAtleastThis(int major, int minor, int patch)
{
	return true;
}


/*
 * Hook to invalidate the version cache.
 * TODO: implement this once we have a real version cache in OSS.
 */
void
InvalidateVersionCache()
{ }
