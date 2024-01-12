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

char *CoreSchemaName = "helio_core";


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
} OidTypeCacheData;

static OidTypeCacheData TypeCache;
static void InitializeOidCaches(void);
static void InvalidateOidCaches(Datum arg, int cacheid, uint32 hashvalue);


/*
 * BsonTypeId returns the OID of the bson type.
 */
Oid
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


/*
 * BsonQueryTypeId returns the OID of the bsonquery type.
 */
Oid
BsonQueryTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.BsonQueryTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString(CoreSchemaName),
											makeString("bsonquery"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		TypeCache.BsonQueryTypeId = typenameTypeId(NULL, bsonTypeName);
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
