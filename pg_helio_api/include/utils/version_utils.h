/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/utils/version_utils.h
 *
 * Utilities that Provide extension functions to handle version upgrade
 * scenarios for Pgmongo.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#ifndef VERSION_UTILS_H
#define VERSION_UTILS_H

bool IsClusterVersionAtleastThis(int major, int minor, int patch);
void InvalidateVersionCache(void);

#endif
