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

PGDLLEXPORT char *CoreSchemaName = "documentdb_core";
PGDLLEXPORT char *CoreSchemaNameV2 = "documentdb_core";

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

	/* OID of the CoreSchemaNameV2.bsonsequence type */
	Oid DocumentsCoreBsonSequenceTypeId;

	/* OID of the CoreSchemaNameV2.bson type */
	Oid DocumentsCoreBsonTypeId;
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
DocumentDBCoreBsonSequenceTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.DocumentsCoreBsonSequenceTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString(CoreSchemaNameV2),
											makeString("bsonsequence"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		TypeCache.DocumentsCoreBsonSequenceTypeId = typenameTypeId(NULL, bsonTypeName);
	}

	return TypeCache.DocumentsCoreBsonSequenceTypeId;
}


/*
 * BsonTypeId returns the OID of the bson type defined in CoreSchemaNameV2 schema.
 */
PGDLLEXPORT Oid
DocumentDBCoreBsonTypeId(void)
{
	InitializeOidCaches();

	if (TypeCache.DocumentsCoreBsonTypeId == InvalidOid)
	{
		List *bsonTypeNameList = list_make2(makeString(CoreSchemaNameV2),
											makeString("bson"));
		TypeName *bsonTypeName = makeTypeNameFromNameList(bsonTypeNameList);
		TypeCache.DocumentsCoreBsonTypeId = typenameTypeId(NULL, bsonTypeName);
	}

	return TypeCache.DocumentsCoreBsonTypeId;
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
