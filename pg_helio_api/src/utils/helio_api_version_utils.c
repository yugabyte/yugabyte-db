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
#include "utils/version_utils_private.h"
#include "utils/query_utils.h"
#include "utils/guc_utils.h"
#include <utils/builtins.h>

/*
 * Global value tracking the Current Version deployed across
 * the cluster.
 */
ExtensionVersion CurrentVersion = { 0 };

char *VersionRefreshQuery =
	"SELECT regexp_split_to_array(extversion, '[-\\.]')::int4[] FROM pg_extension WHERE extname = 'pg_helio_api'";

/*
 * Returns true if the cluster version is >= given major.minor.patch version
 */
bool
IsClusterVersionAtleastThis(int major, int minor, int patch)
{
	RefreshCurrentVersion();

	if (CurrentVersion.Major < major)
	{
		return false;
	}
	else if (CurrentVersion.Minor < minor)
	{
		return false;
	}
	else if (CurrentVersion.Major != major || CurrentVersion.Minor != minor)
	{
		/* if CurrentVersion.Major or CurrentVersion.Minor are greater than the expected version */
		/* parts we are on a later version, no need to compare the patch. */
		return true;
	}

	/* Major and Minor are the expected ones, we should compare the patch version. */
	return CurrentVersion.Patch >= patch;
}


/*
 * Returns true if the given Extension Version is >= given major.minor.patch version
 */
bool
IsExtensionVersionAtleastThis(ExtensionVersion extVersion, int major, int minor, int
							  patch)
{
	if (extVersion.Major < major)
	{
		return false;
	}
	else if (extVersion.Minor < minor)
	{
		return false;
	}
	else if (extVersion.Major != major || extVersion.Minor != minor)
	{
		/* if extVersion.Major or extVersion.Minor are greater than the expected version */
		/* parts we are on a later version, no need to compare the patch. */
		return true;
	}

	/* Major and Minor are the expected ones, we should compare the patch version. */
	return extVersion.Patch >= patch;
}


/*
 * Hook to invalidate the version cache.
 */
void
InvalidateVersionCache()
{
	CurrentVersion = (ExtensionVersion) {
		0
	};
}


void
RefreshCurrentVersion(void)
{
	if (CurrentVersion.Major != 0)
	{
		return;
	}

	/*
	 * Temporarily disable unimportant logs related to version lookup
	 * so that regression test outputs don't become flaky (e.g.: due to commands
	 * being executed by Citus locally).
	 */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("client_min_messages", "WARNING");
	bool readOnly = true;
	bool isNull = false;

	char *versionString = ExtensionExecuteQueryOnLocalhostViaLibPQ(VersionRefreshQuery);

	if (strcmp(versionString, "") == 0)
	{
		RollbackGUCChange(savedGUCLevel);
		return;
	}

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(versionString) };
	char *argNulls = NULL;

	/* LibPQ returns an array as a string in the form of {Major,Minor,Hotfix}, so instead
	 * of parsing the string directly, let's use postgres to cast it to an array as a datum
	 * and just use the deconstructed array to get the values out of it. */
	Datum versionDatum = ExtensionExecuteQueryWithArgsViaSPI("SELECT $1::int4[]",
															 nargs, argTypes, argValues,
															 argNulls, readOnly,
															 SPI_OK_SELECT, &isNull);

	ArrayType *arrayValue = DatumGetArrayTypeP(versionDatum);
	RollbackGUCChange(savedGUCLevel);

	Datum *elements = NULL;
	int numElements = 0;
	deconstruct_array(arrayValue, INT4OID, sizeof(int), true, TYPALIGN_INT,
					  &elements, NULL, &numElements);

	Assert(numElements == 3);
	CurrentVersion.Major = DatumGetInt32(elements[0]);
	CurrentVersion.Minor = DatumGetInt32(elements[1]);
	CurrentVersion.Patch = DatumGetInt32(elements[2]);
}
