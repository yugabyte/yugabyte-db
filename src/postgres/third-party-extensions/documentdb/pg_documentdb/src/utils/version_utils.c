/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/version_utils.c
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
#include <storage/ipc.h>
#include <storage/shmem.h>
#include <utils/memutils.h>
#include "utils/documentdb_errors.h"
#include "api_hooks.h"

extern char *ApiExtensionName;

/*
 * Global value tracking the Current Version deployed across
 * the cluster.
 */
static ExtensionVersion *CurrentVersion = NULL;

int FirstMajorVersionOffset = 0;

#define MaxVersionAllowed DocDB_V0

static char *VersionRefreshQuery = NULL;
static char * GetVersionRefreshQuery(void);

/* This is not exposed in the header file */
static ExtensionVersion RefreshCurrentVersion(void);

Size
VersionCacheShmemSize(void)
{
	return MAXALIGN(sizeof(ExtensionVersion));
}


/*
 * Initializes the version cache in shared memory.
 */
void
InitializeVersionCache(void)
{
	bool found;

	size_t version_cache_size = VersionCacheShmemSize();
	CurrentVersion = (ExtensionVersion *) ShmemInitStruct("DocumentDB Version Cache",
														  version_cache_size, &found);

	if (!found)
	{
		/*
		 * We're the first - initialize.
		 */
		memset(CurrentVersion, 0, version_cache_size);
	}
}


bool
IsVersionRefreshQueryString(const char *queryString)
{
	return queryString != NULL && strcmp(queryString, GetVersionRefreshQuery()) == 0;
}


/*
 * Returns true if the cluster version is exactly major.minor and >= patch
 */
bool
IsClusterVersionAtLeastPatch(MajorVersion major, int minor, int patch)
{
	Assert(major <= MaxVersionAllowed);
	ExtensionVersion version = RefreshCurrentVersion();

	if (version.Major != (int) (major + FirstMajorVersionOffset))
	{
		return false;
	}
	else if (version.Minor != minor)
	{
		return false;
	}

	/* Major and Minor are the expected ones, we should compare the patch version. */
	return version.Patch >= patch;
}


/*
 * Returns true if the cluster version is >= given major.minor.patch version
 */
bool
IsClusterVersionAtleast(MajorVersion majorVersion, int minor, int patch)
{
	Assert(majorVersion <= MaxVersionAllowed);
	ExtensionVersion version = RefreshCurrentVersion();

	int major = majorVersion + FirstMajorVersionOffset;
	if (version.Major < major)
	{
		return false;
	}
	else if (version.Minor < minor)
	{
		return false;
	}
	else if (version.Major != major || version.Minor != minor)
	{
		/* if CurrentVersion.Major or CurrentVersion.Minor are greater than the expected version */
		/* parts we are on a later version, no need to compare the patch. */
		return true;
	}

	/* Major and Minor are the expected ones, we should compare the patch version. */
	return version.Patch >= patch;
}


/*
 * Returns true if the given Extension Version is >= given major.minor.patch version
 */
bool
IsExtensionVersionAtleast(ExtensionVersion extVersion, MajorVersion majorVersion,
						  int minor, int patch)
{
	Assert(majorVersion <= MaxVersionAllowed);
	int major = majorVersion + FirstMajorVersionOffset;
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
	if (CurrentVersion != NULL)
	{
		*CurrentVersion = (ExtensionVersion) {
			0
		};
	}

	pg_write_barrier();
}


const char *
GetCurrentVersionForLogging(void)
{
	ExtensionVersion version = RefreshCurrentVersion();
	StringInfo s = makeStringInfo();
	appendStringInfo(s, "Major = %d, Minor = %d, Patch = %d",
					 version.Major, version.Minor, version.Patch);
	return s->data;
}


const char *
GetCurrentShortVersionStringForLogging(void)
{
	ExtensionVersion version = RefreshCurrentVersion();
	StringInfo s = makeStringInfo();
	appendStringInfo(s, "%d.%d-%d",
					 version.Major, version.Minor, version.Patch);
	return s->data;
}


static ExtensionVersion
RefreshCurrentVersion(void)
{
	ExtensionVersion currentVersion = { 0 };

	if (unlikely(CurrentVersion == NULL))
	{
		/* Shared memory is not initialized */
		return currentVersion;
	}

	pg_memory_barrier();
	currentVersion = *CurrentVersion;

	if (currentVersion.Major > DocDB_V0 ||
		(currentVersion.Major == DocDB_V0 && currentVersion.Minor > 0))
	{
		return currentVersion;
	}

	/*
	 * Temporarily disable unimportant logs related to version lookup
	 * so that regression test outputs don't become flaky (e.g.: due to commands
	 * being executed locally).
	 */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("client_min_messages", "WARNING");

	bool readOnly = true;
	bool isNull = false;

	char *versionString = ExtensionExecuteQueryOnLocalhostViaLibPQ(
		GetVersionRefreshQuery());

	if (strcmp(versionString, "") == 0)
	{
		RollbackGUCChange(savedGUCLevel);
		return currentVersion;
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
	ExtensionVersion newVersion = { 0 };
	newVersion.Major = DatumGetInt32(elements[0]);
	newVersion.Minor = DatumGetInt32(elements[1]);
	newVersion.Patch = DatumGetInt32(elements[2]);

	*CurrentVersion = newVersion;
	pg_write_barrier();

	return newVersion;
}


static char *
GetVersionRefreshQuery()
{
	if (VersionRefreshQuery != NULL)
	{
		return VersionRefreshQuery;
	}

	char *versionQuery = TryGetExtendedVersionRefreshQuery();
	if (versionQuery != NULL)
	{
		VersionRefreshQuery = versionQuery;
	}
	else
	{
		MemoryContext currContext = MemoryContextSwitchTo(TopMemoryContext);
		StringInfo s = makeStringInfo();
		appendStringInfo(s,
						 "SELECT regexp_split_to_array(extversion, '[-\\.]')::int4[] FROM pg_extension WHERE extname = '%s'",
						 ApiExtensionName);
		VersionRefreshQuery = s->data;
		MemoryContextSwitchTo(currContext);
	}

	return VersionRefreshQuery;
}
