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

#ifndef VERSION_UTILS_PRIVATE_H
#define VERSION_UTILS_PRIVATE_H

/*
 * The current Extensions' version.
 * Note Extension versions are of the form
 * <Major>.<Minor>-<Patch>
 */
typedef struct ExtensionVersion
{
	/* The major version of the extension */
	int Major;

	/* The minor version of the extension */
	int Minor;

	/* The patch version of the extension */
	int Patch;
} ExtensionVersion;

ExtensionVersion RefreshCurrentVersion(void);

bool IsExtensionVersionAtleastThis(ExtensionVersion extVersion, int major, int minor, int
								   patch);
#endif
