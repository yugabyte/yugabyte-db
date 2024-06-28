/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/utils/type_cache.c
 *
 * Implementation of general Oid caching functions.
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <fmgr.h>
#include <miscadmin.h>
#include <parser/parse_type.h>
#include <nodes/makefuncs.h>
#include <utils/inval.h>
#include <utils/syscache.h>

#include "utils/type_cache.h"

PGDLLEXPORT char *CoreSchemaName = "helio_core";


/*
 * CacheValidityValue represents the possible states of the cache.
 */
typedef enum TypeCacheValidityValue
{
	/* cache was not succesfully initialized */
	TYPE_CACHE_INVALID,

	/* extension exist, cache is valid */
	TYPE_CACHE_VALID
} TypeCacheValidityValue;

/* indicates whether the cache is valid, or needs to be reset */
static TypeCacheValidityValue TypeCacheValidity = TYPE_CACHE_INVALID;
static bool RegisteredCallback = false;

typedef struct OidTypeCacheData
{
	/* OID of the bson type */
	Oid BsonTypeId;

	/* OID of the bsonquery type */
	Oid BsonQueryTypeId;

	/* OID of the helio_core.bsonsequence type */
	Oid HelioCoreBsonSequenceTypeId;

	/* OID of the helio_core.bson type */
	Oid HelioCoreBsonTypeId;
} OidTypeCacheData;

static OidTypeCacheData TypeCache;
static void InitializeOidCaches(void);
static void InvalidateOidCaches(Datum arg, int cacheid, uint32 hashvalue);


/*
 * BsonTypeId returns the OID of the bson type.
 */
PGDLLEXPORT Oid
BsonTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.BsonTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString(CoreSchemaName),
											makeString("bson"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		TypeCache.BsonTypeId = typenameTypeId(NULL, bsonTypeName);
	}

	return TypeCache.BsonTypeId;
}


PGDLLEXPORT Oid
HelioCoreBsonSequenceTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.HelioCoreBsonSequenceTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString("helio_core"),
											makeString("bsonsequence"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		TypeCache.HelioCoreBsonSequenceTypeId = typenameTypeId(NULL, bsonTypeName);
	}

	return TypeCache.HelioCoreBsonSequenceTypeId;
}


/*
 * BsonTypeId returns the OID of the bson type defined in helio_core schema ignoring core schema override.
 */
PGDLLEXPORT Oid
HelioCoreBsonTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.HelioCoreBsonTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString("helio_core"),
											makeString("bson"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		TypeCache.HelioCoreBsonTypeId = typenameTypeId(NULL, bsonTypeName);
	}

	return TypeCache.HelioCoreBsonTypeId;
}


/*
 * BsonQueryTypeId returns the OID of the bsonquery type.
 */
PGDLLEXPORT Oid
BsonQueryTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.BsonQueryTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString(CoreSchemaName),
											makeString("bsonquery"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);

		/* We set missingOk = true for extension upgrade tests. Otherwise this should always exist. */
		bool missingOk = true;
		TypeCache.BsonQueryTypeId = LookupTypeNameOid(NULL, bsonTypeName, missingOk);
	}

	return TypeCache.BsonQueryTypeId;
}


/*
 * Initializes the type caches associated with the extension.
 */
static void
InitializeOidCaches(void)
{
	if (TypeCacheValidity == TYPE_CACHE_VALID)
	{
		return;
	}

	if (!RegisteredCallback)
	{
		CacheRegisterSyscacheCallback(TYPEOID, &InvalidateOidCaches, (Datum) 0);
		RegisteredCallback = true;
	}

	memset(&TypeCache, 0, sizeof(TypeCache));
	TypeCacheValidity = TYPE_CACHE_VALID;
}


/*
 * Invalidates the type caches on events.
 */
static void
InvalidateOidCaches(Datum arg, int cacheid, uint32 hashvalue)
{
	TypeCacheValidity = TYPE_CACHE_INVALID;
}
