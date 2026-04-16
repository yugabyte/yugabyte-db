/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/version_utils.h
 *
 * Utilities that Provide extension functions to handle version upgrade
 * scenarios.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#ifndef VERSION_UTILS_H
#define VERSION_UTILS_H

typedef enum DocumentsMajorVersion
{
	DocDB_V0 = 0,
} MajorVersion;

bool IsClusterVersionAtleast(MajorVersion major, int minor, int patch);
bool IsClusterVersionAtLeastPatch(MajorVersion major, int minor, int patch);
void InvalidateVersionCache(void);
void InitializeVersionCache(void);

#endif
