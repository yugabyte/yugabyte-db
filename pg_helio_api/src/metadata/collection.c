/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/metadata/collection.c
 *
 * Implementation of collection metadata cache.
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"

#include "access/xact.h"
#include "catalog/pg_attribute.h"
#include "commands/extension.h"
#include "executor/spi.h"
#include "lib/stringinfo.h"
#include "nodes/makefuncs.h"
#include "nodes/pg_list.h"
#include "parser/parse_func.h"
#include "storage/lmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/version_utils.h"

#include "metadata/collection.h"
#include "metadata/metadata_cache.h"
#include "utils/mongo_errors.h"
#include "metadata/relation_utils.h"
#include "utils/query_utils.h"
#include "utils/guc_utils.h"
#include "metadata/metadata_guc.h"
#include "api_hooks.h"
#include "commands/parse_error.h"
#include "utils/feature_counter.h"

#define CREATE_COLLECTION_FUNC_NARGS 2


/*
 * NameToCollectionCacheEntry is an entry in NameToCollectionHash that maps
 * a qualified collection name to a collection.
 */
typedef struct NameToCollectionCacheEntry
{
	/* Mongo qualified name of the collection */
	MongoCollectionName name;

	/* collection metadata */
	MongoCollection collection;

	/* set to false when an invalidation for the relation is received */
	bool isValid;
} NameToCollectionCacheEntry;

/*
 * RelationIdToCollectionCacheEntry is an entry in RelationIdToCollectionHash
 * that maps a relation OID to a collection.
 */
typedef struct RelationIdToCollectionCacheEntry
{
	/* OID of the Postgres relation underlying the collection */
	Oid relationId;

	/* collection metadata */
	MongoCollection collection;

	/* set to false when an invalidation for the relation is received */
	bool isValid;
} RelationIdToCollectionCacheEntry;


static const char CharactersNotAllowedInDatabaseNames[7] = {
	'/', '\\', '.', ' ', '"', '$', '\0'
};
static const int CharactersNotAllowedInDatabaseNamesLength =
	sizeof(CharactersNotAllowedInDatabaseNames);

static const char CharactersNotAllowedInCollectionNames[2] = { '$', '\0' };
static const int CharactersNotAllowedInCollectionNamesLength =
	sizeof(CharactersNotAllowedInCollectionNames);

static const char *ValidSystemCollectionNames[5] = {
	"system.users", "system.js", "system.views", "system.profile", "system.dbSentinel"
};
static const int ValidSystemCollectionNamesLength = 5;

/* Not allowing any writes to the below system namespaces */
static const char *NonWritableSystemCollectionNames[4] = {
	"system.users", "system.js", "system.views", "system.profile"
};
static const int NonWritableSystemCollectionNamesLength = 4;

static const uint32_t MaxDatabaseCollectionLength = 235;
static const StringView SystemPrefix = { .length = 7, .string = "system." };

extern bool UseLocalExecutionShardQueries;
extern bool EnableSchemaValidation;
extern int MaxSchemaValidatorSize;

/* user-defined functions */
PG_FUNCTION_INFO_V1(command_collection_table);
PG_FUNCTION_INFO_V1(command_invalidate_collection_cache);
PG_FUNCTION_INFO_V1(command_get_next_collection_id);
PG_FUNCTION_INFO_V1(command_ensure_valid_db_coll);
PG_FUNCTION_INFO_V1(validate_dbname);
PG_FUNCTION_INFO_V1(command_get_collection);
PG_FUNCTION_INFO_V1(command_get_collection_or_view);

/* forward declarations */
static void InitializeCollectionsHash(void);
static bool GetMongoCollectionFromCatalogById(uint64 collectionId, Oid relationId,
											  MongoCollection *collection);
static bool GetMongoCollectionFromCatalogByNameDatum(Datum databaseNameDatum,
													 Datum collectionNameDatum,
													 MongoCollection *collection);
static Oid GetRelationIdForCollectionTableName(char *collectionTableName,
											   LOCKMODE lockMode);
static AttrNumber GetMongoDataCreationTimeVarAttrNumber(Oid collectionOid);
static MongoCollection * GetMongoCollectionByNameDatumCore(Datum databaseNameDatum,
														   Datum collectionNameDatum,
														   LOCKMODE lockMode);
static Datum GetCollectionOrViewCore(PG_FUNCTION_ARGS, bool allowViews);

/*
 * CollectionCacheIsValid determines whether the collections hashes are
 * valid. It is set to false before initialization, if OOMs occurred while
 * in critical sections of cache construction, and after global invalidations.
 */
static bool CollectionCacheIsValid = false;

/* memory context in which we allocate collections hashes */
static MemoryContext CollectionsCacheContext = NULL;

/* (database name, collection name) -> collection hash */
static HTAB *NameToCollectionHash = NULL;

/* (relation OID) -> collection hash */
static HTAB *RelationIdToCollectionHash = NULL;


/*
 * InitializeCollectionsHash (re)creates the collections hashes if they are
 * not valid.
 *
 * At the end of this function, either CollectionCacheIsValid is true or
 * an OOM was thrown. In the latter case, we will try again on the next
 * call.
 */
static void
InitializeCollectionsHash(void)
{
	/* make sure the metadata cache is initalized */
	InitializeHelioApiExtensionCache();

	/* should not call this function if extension does not exist */
	Assert(IsHelioApiExtensionActive());

	if (CollectionCacheIsValid)
	{
		/* already built the hashes */
		return;
	}

	if (CollectionsCacheContext == NULL)
	{
		CollectionsCacheContext = AllocSetContextCreate(CacheMemoryContext,
														"Collection cache context",
														ALLOCSET_DEFAULT_SIZES);
	}

	/* reset any previously allocated memory */
	MemoryContextReset(CollectionsCacheContext);

	int hashFlags = HASH_ELEM | HASH_BLOBS | HASH_CONTEXT;

	/* create the (database name, collection name) -> collection hash */
	HASHCTL info;
	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(MongoCollectionName);
	info.entrysize = sizeof(NameToCollectionCacheEntry);
	info.hcxt = CollectionsCacheContext;

	NameToCollectionHash = hash_create("Name to Collection ID Hash", 32,
									   &info, hashFlags);

	/* create the (relation OID) -> collection hash */
	memset(&info, 0, sizeof(info));
	info.keysize = sizeof(Oid);
	info.entrysize = sizeof(RelationIdToCollectionCacheEntry);
	info.hcxt = CollectionsCacheContext;

	RelationIdToCollectionHash = hash_create("Relation ID to Collection ID Hash", 32,
											 &info, hashFlags);

	CollectionCacheIsValid = true;
}


/*
 * ResetCollectionsCache is called when we rebuild the cache from scratch.
 * We need not worry about freeing memory here, since HelioApiMetadataCacheContext
 * is reset as part of the process. We only set ResetCollectionsCache such
 * that we rebuild the hashes in InitializeCollectionsHash.
 */
void
ResetCollectionsCache(void)
{
	CollectionCacheIsValid = false;
}


/*
 * InvalidateCollectionByRelationId is called when receiving invalidation of a specific
 * relation ID.
 *
 * This can happen any time postgres code calls AcceptInvalidationMessages(), e.g
 * after obtaining a relation lock. We remove entries from the cache. They will
 * still be temporarily usable until new entries are added to the cache.
 */
void
InvalidateCollectionByRelationId(Oid relationId)
{
	if (!CollectionCacheIsValid)
	{
		/* hashes are not valid, just wait for cache rebuild */
		return;
	}

	/* delete entry from the relation OID -> collection cache (if any) */
	bool foundInCache = false;
	RelationIdToCollectionCacheEntry *entryById =
		hash_search(RelationIdToCollectionHash, &relationId, HASH_REMOVE,
					&foundInCache);

	if (foundInCache)
	{
		MongoCollection *collection = &(entryById->collection);

		/* delete entry from the collection name -> collection cache */
		NameToCollectionCacheEntry *entryByName =
			hash_search(NameToCollectionHash, &(collection->name),
						HASH_REMOVE, &foundInCache);

		/*
		 * We currently always create entries together and only
		 * call this function with a valid cache.
		 */
		Assert(foundInCache);

		if (foundInCache)
		{
			/* signal to callers that the entry is no longer valid */
			entryByName->isValid = false;
		}

		/* signal to callers that the entry is no longer valid */
		entryById->isValid = false;
	}
}


/*
 * CopyMongoCollection returns a copy of given MongoCollection.
 */
MongoCollection *
CopyMongoCollection(const MongoCollection *collection)
{
	MongoCollection *copiedCollection = palloc0(sizeof(MongoCollection));

	*copiedCollection = *collection;
	copiedCollection->shardKey = !copiedCollection->shardKey ? NULL :
								 CopyPgbsonIntoMemoryContext(copiedCollection->shardKey,
															 CurrentMemoryContext);
	copiedCollection->viewDefinition = !copiedCollection->viewDefinition ? NULL :
									   CopyPgbsonIntoMemoryContext(
		copiedCollection->viewDefinition,
		CurrentMemoryContext);

	copiedCollection->schemaValidator.validator =
		!copiedCollection->schemaValidator.validator ? NULL :
		CopyPgbsonIntoMemoryContext(
			copiedCollection->schemaValidator.validator,
			CurrentMemoryContext);

	return copiedCollection;
}


/*
 * GetMongoCollectionByColId() gets the MongoCollection metadata by collectionId.
 */
MongoCollection *
GetMongoCollectionByColId(uint64 collectionId, LOCKMODE lockMode)
{
	/* make sure hashes exist */
	InitializeCollectionsHash();

	Oid documentsTableOid = GetRelationIdForCollectionId(collectionId, lockMode);

	if (!OidIsValid(documentsTableOid))
	{
		/* table was dropped */
		return NULL;
	}

	bool foundInCache = false;

	RelationIdToCollectionCacheEntry *entryByRelId =
		hash_search(RelationIdToCollectionHash, &documentsTableOid, HASH_FIND,
					&foundInCache);

	if (foundInCache)
	{
		/* now that we have a lock, check for invalidations */
		AcceptInvalidationMessages();

		/*
		 * After acquiring the lock on the table, we may have received an invalidation
		 * that could indicate a rename. In that case, CollectionCacheIsValid or
		 * entry->isValid is set to false and we treat this as a cache miss by
		 * continuing below.
		 */
		if (!CollectionCacheIsValid)
		{
			InitializeCollectionsHash();
		}
		else if (entryByRelId->isValid)
		{
			return CopyMongoCollection(&(entryByRelId->collection));
		}
	}

	MongoCollection collection;
	memset(&collection, 0, sizeof(collection));

	/* Read the collection metadata from ApiCatalogSchemaName.collections */
	bool collectionExists =
		GetMongoCollectionFromCatalogById(collectionId, documentsTableOid,
										  &collection);

	if (!collectionExists)
	{
		/* no collection record with this id */
		return NULL;
	}

	/* get the relation ID and lock the table */
	if (collection.viewDefinition != NULL)
	{
		/* Views not supported in this path */
		return NULL;
	}

	collection.relationId = GetRelationIdForCollectionTableName(collection.tableName,
																lockMode);

	if (!OidIsValid(collection.relationId))
	{
		/* record exists, but table was dropped (maybe just after reading the record) */
		return NULL;
	}

	/* if we experience OOM below, reset the cache to prevent corruption */
	CollectionCacheIsValid = false;

	/* copy the shard key BSON into the cache memory context */
	if (collection.shardKey != NULL)
	{
		collection.shardKey = CopyPgbsonIntoMemoryContext(collection.shardKey,
														  CollectionsCacheContext);
	}

	if (collection.viewDefinition != NULL)
	{
		collection.viewDefinition = CopyPgbsonIntoMemoryContext(collection.viewDefinition,
																CollectionsCacheContext);
	}

	/* add to relation ID -> collection hash */
	entryByRelId = hash_search(RelationIdToCollectionHash, &(collection.relationId),
							   HASH_ENTER, &foundInCache);

	entryByRelId->collection = collection;
	entryByRelId->isValid = true;

	/* no OOMs, keep the cache */
	CollectionCacheIsValid = true;

	return CopyMongoCollection(&(entryByRelId->collection));
}


/*
 * GetMongoCollectionOrViewByNameDatum returns collection metadata by database and
 * collection name or NULL if the collection does not exist. Also returns views
 * if applicable
 */
MongoCollection *
GetMongoCollectionOrViewByNameDatum(Datum databaseNameDatum, Datum collectionNameDatum,
									LOCKMODE lockMode)
{
	return GetMongoCollectionByNameDatumCore(databaseNameDatum, collectionNameDatum,
											 lockMode);
}


/*
 * GetMongoCollectionByName returns collection metadata by database and
 * collection name or NULL if the collection does not exist.
 */
MongoCollection *
GetMongoCollectionByNameDatum(Datum databaseNameDatum, Datum collectionNameDatum,
							  LOCKMODE lockMode)
{
	MongoCollection *collection =
		GetMongoCollectionByNameDatumCore(databaseNameDatum, collectionNameDatum,
										  lockMode);

	if (collection != NULL && collection->viewDefinition != NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTEDONVIEW),
						errmsg("Namespace %s.%s is a view, not a collection",
							   collection->name.databaseName,
							   collection->name.collectionName)));
	}

	return collection;
}


/*
 * Given a collection, tries to get a shardOID if one is applicable (there is
 * a single shard corresponding to that collection) and if it's available
 * locally. If such a shard is found, locks it with the given lock mode
 * and returns it to the caller.
 */
Oid
TryGetCollectionShardTable(MongoCollection *collection, LOCKMODE lockMode)
{
	/* If we don't have a local shard, bail. */
	if (collection->shardTableName[0] == '\0')
	{
		return InvalidOid;
	}

	if (!UseLocalExecutionShardQueries)
	{
		return InvalidOid;
	}

	/* Don't allow direct shard access in a multi-statement transaction
	 * This is because switching states between remote execution and local execution can produce
	 * isolation issues. So only support this if we're a single command.
	 */
	if (IsTransactionBlock())
	{
		return InvalidOid;
	}

	Oid relationShardOid = GetRelationIdForCollectionTableName(collection->shardTableName,
															   lockMode);
	ereport(DEBUG3, (errmsg("Has relation shard: %d", relationShardOid != InvalidOid)));
	return relationShardOid;
}


/*
 * GetMongoCollectionByName returns collection metadata by database and
 * collection name or NULL if the collection does not exist.
 */
static MongoCollection *
GetMongoCollectionByNameDatumCore(Datum databaseNameDatum, Datum collectionNameDatum,
								  LOCKMODE lockMode)
{
	/* make sure hashes exist */
	InitializeCollectionsHash();

	int databaseNameLength = VARSIZE_ANY_EXHDR(databaseNameDatum);
	if (databaseNameLength >= MAX_DATABASE_NAME_LENGTH)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE), errmsg(
							"database name is too long")));
	}

	int collectionNameLength = VARSIZE_ANY_EXHDR(collectionNameDatum);
	if (collectionNameLength >= MAX_COLLECTION_NAME_LENGTH)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE), errmsg(
							"collection name is too long")));
	}

	MongoCollectionName qualifiedName;
	memset(&qualifiedName, 0, sizeof(qualifiedName));

	/* copy text bytes directly, buffers are already 0-initialized above */
	memcpy(qualifiedName.databaseName, VARDATA_ANY(databaseNameDatum),
		   databaseNameLength);
	memcpy(qualifiedName.collectionName, VARDATA_ANY(collectionNameDatum),
		   collectionNameLength);

	bool foundInCache = false;

	NameToCollectionCacheEntry *entryByName =
		hash_search(NameToCollectionHash, &qualifiedName, HASH_FIND, &foundInCache);

	if (foundInCache)
	{
		/* refresh relation ID based on the name */
		if (entryByName->collection.viewDefinition == NULL)
		{
			entryByName->collection.relationId =
				GetRelationIdForCollectionTableName(entryByName->collection.tableName,
													lockMode);
			if (!OidIsValid(entryByName->collection.relationId))
			{
				/* table was dropped */
				return NULL;
			}
		}

		/* now that we have a lock, check for invalidations */
		AcceptInvalidationMessages();

		/*
		 * After acquiring the lock on the table, we may have received an invalidation
		 * that could indicate a rename. In that case, CollectionCacheIsValid or
		 * entry->isValid is set to false and we treat this as a cache miss by
		 * continuing below.
		 */
		if (!CollectionCacheIsValid)
		{
			InitializeCollectionsHash();
		}
		else if (entryByName->isValid)
		{
			return CopyMongoCollection(&(entryByName->collection));
		}
	}

	MongoCollection collection;
	memset(&collection, 0, sizeof(collection));

	/*
	 * Temporarily disable unimportant logs related to collection catalog lookup
	 * so that regression test outputs don't become flaky (e.g.: due to commands
	 * being executed by Citus locally).
	 */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("client_min_messages", "WARNING");

	/*
	 * Read the collection metadata from ApiCatalogSchemaName.collections or error
	 * out if the collection does not exist. (We do not cache negative entries,
	 * since we expect them to be rare)
	 */
	bool collectionExists =
		GetMongoCollectionFromCatalogByNameDatum(databaseNameDatum,
												 collectionNameDatum,
												 &collection);

	/* rollback the GUC change that we made for client_min_messages */
	RollbackGUCChange(savedGUCLevel);

	if (!collectionExists)
	{
		/* no collection record with this name */
		return NULL;
	}

	/* get the relation ID and lock the table */
	if (collection.viewDefinition == NULL)
	{
		collection.relationId =
			GetRelationIdForCollectionTableName(collection.tableName, lockMode);

		if (!OidIsValid(collection.relationId))
		{
			/* record exists, but table was dropped (maybe just after reading the record) */
			return NULL;
		}

		if (collection.shardKey == NULL)
		{
			savedGUCLevel = NewGUCNestLevel();
			SetGUCLocally("client_min_messages", "WARNING");
			const char *shardName = TryGetShardNameForUnshardedCollection(
				collection.relationId, collection.collectionId, collection.tableName);
			RollbackGUCChange(savedGUCLevel);
			if (shardName != NULL)
			{
				strcpy(collection.shardTableName, shardName);
			}
		}
	}


	/* if we experience OOM below, reset the cache to prevent corruption */
	CollectionCacheIsValid = false;

	/* copy the shard key BSON into the cache memory context */
	if (collection.shardKey != NULL)
	{
		collection.shardKey = CopyPgbsonIntoMemoryContext(collection.shardKey,
														  CollectionsCacheContext);
	}

	if (collection.viewDefinition != NULL)
	{
		collection.viewDefinition = CopyPgbsonIntoMemoryContext(collection.viewDefinition,
																CollectionsCacheContext);
	}
	else
	{
		collection.mongoDataCreationTimeVarAttrNumber =
			GetMongoDataCreationTimeVarAttrNumber(collection.relationId);
	}

	/* get schema validation meta*/
	if (collection.schemaValidator.validator != NULL)
	{
		collection.schemaValidator.validator = CopyPgbsonIntoMemoryContext(
			collection.schemaValidator.validator,
			CollectionsCacheContext);
	}

	/* collection exists, so write a name -> collection cache entry */
	entryByName =
		hash_search(NameToCollectionHash, &qualifiedName, HASH_ENTER, &foundInCache);

	/*
	 * We lazily copy the whole collection struct instead of trying to be overly
	 * clever about keeping only a single copy.
	 */
	entryByName->collection = collection;
	entryByName->isValid = true;

	/* also add to relation ID -> collection hash */
	RelationIdToCollectionCacheEntry *entryByRelId =
		hash_search(RelationIdToCollectionHash, &(collection.relationId),
					HASH_ENTER, &foundInCache);

	entryByRelId->collection = collection;
	entryByRelId->isValid = true;

	/* no OOMs, keep the cache */
	CollectionCacheIsValid = true;

	return CopyMongoCollection(&(entryByName->collection));
}


MongoCollection *
GetTempMongoCollectionByNameDatum(Datum databaseNameDatum, Datum collectionNameDatum,
								  char *collectionName,
								  LOCKMODE lockMode)
{
	MongoCollection *collection = palloc0(sizeof(MongoCollection));

	int databaseNameLength = VARSIZE_ANY_EXHDR(databaseNameDatum);
	if (databaseNameLength >= MAX_DATABASE_NAME_LENGTH)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"database name is too long")));
	}

	int collectionNameLength = VARSIZE_ANY_EXHDR(collectionNameDatum);
	if (collectionNameLength >= MAX_COLLECTION_NAME_LENGTH)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE), errmsg(
							"collection name is too long")));
	}

	/* copy text bytes directly, buffers are already 0-initialized above */
	memcpy(collection->name.databaseName, VARDATA_ANY(databaseNameDatum),
		   databaseNameLength);
	memcpy(collection->name.collectionName, VARDATA_ANY(collectionNameDatum),
		   collectionNameLength);

	collection->shardKey = NULL;
	collection->viewDefinition = NULL;
	collection->collectionId = UINT64_MAX; /*unused */
	collection->relationId = InvalidOid; /* unused */
	sprintf(collection->tableName, "documents_temp");
	collection->shardTableName[0] = '\0';

	return collection;
}


/*
 * IsDataTableCreatedWithinCurrentXact returns true if data table for given
 * collection has been created within the current transaction.
 */
bool
IsDataTableCreatedWithinCurrentXact(const MongoCollection *collection)
{
	HeapTuple pgCatalogTuple =
		SearchSysCache1(RELOID, ObjectIdGetDatum(collection->relationId));
	if (!HeapTupleIsValid(pgCatalogTuple))
	{
		ereport(ERROR, (errmsg("data table for collection with id "
							   UINT64_FORMAT " doesn't exist",
							   collection->collectionId)));
	}

	bool dataTableCreatedWithinCurrentXact =
		HeapTupleHeaderGetXmin(pgCatalogTuple->t_data) == GetCurrentTransactionId();

	ReleaseSysCache(pgCatalogTuple);

	return dataTableCreatedWithinCurrentXact;
}


/*
 * GetMongoCollectionFromCatalogById returns whether a collection
 * with the given id exist in ApiCatalogSchemaName.collections and writes
 * the metadata to the collection struct.
 */
static bool
GetMongoCollectionFromCatalogById(uint64 collectionId, Oid relationId,
								  MongoCollection *collection)
{
	bool collectionExists = false;

	StringInfoData query;
	const int argCount = 1;
	Oid argTypes[1];
	Datum argValues[1];

	memset(collection, 0, sizeof(MongoCollection));

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query,
					 "SELECT * FROM %s.collections WHERE collection_id = $1",
					 ApiCatalogSchemaName);

	argTypes[0] = INT8OID;
	argValues[0] = UInt64GetDatum(collectionId);

	SPI_execute_with_args(query.data, argCount, argTypes, argValues, NULL, false, 1);
	if (SPI_processed > 0)
	{
		TupleDesc tupleDescriptor = SPI_tuptable->tupdesc;
		HeapTuple tuple = SPI_tuptable->vals[0];
		bool isNull = false;

		Datum databaseNameDatum = heap_getattr(tuple, 1, tupleDescriptor, &isNull);
		if (isNull)
		{
			ereport(ERROR, (errmsg("database_name should not be NULL in catalog")));
		}

		memcpy(collection->name.databaseName, VARDATA_ANY(databaseNameDatum),
			   VARSIZE_ANY_EXHDR(databaseNameDatum));

		Datum collectionNameDatum = heap_getattr(tuple, 2, tupleDescriptor, &isNull);
		if (isNull)
		{
			ereport(ERROR, (errmsg("collection_name should not be NULL in catalog")));
		}

		memcpy(collection->name.collectionName, VARDATA_ANY(collectionNameDatum),
			   VARSIZE_ANY_EXHDR(collectionNameDatum));

		/* Attr 3 is the collection_id */
		collection->collectionId = collectionId;

		Datum shardKeyDatum = heap_getattr(tuple, 4, tupleDescriptor, &isNull);
		if (!isNull)
		{
			pgbson *shardKeyBson = (pgbson *) DatumGetPointer(shardKeyDatum);
			collection->shardKey =
				CopyPgbsonIntoMemoryContext(shardKeyBson, CurrentMemoryContext);
		}

		/* Attr 5 is the collection_uuid */
		Datum uuidDatum = heap_getattr(tuple, 5, tupleDescriptor, &isNull);
		if (!isNull)
		{
			collection->collectionUUID = *DatumGetUUIDP(uuidDatum);
		}

		if (tupleDescriptor->natts >= 6)
		{
			Datum viewDatum = heap_getattr(tuple, 6, tupleDescriptor, &isNull);
			if (!isNull)
			{
				pgbson *viewDefinition = (pgbson *) DatumGetPointer(viewDatum);
				collection->viewDefinition =
					CopyPgbsonIntoMemoryContext(viewDefinition, CurrentMemoryContext);
			}
		}

		/* Attr 7,8,9 are for Schema Validation */
		if (tupleDescriptor->natts >= 9)
		{
			/* validator stored as pgbson */
			Datum validatorDatum = heap_getattr(tuple, 7, tupleDescriptor, &isNull);
			if (!isNull)
			{
				pgbson *validator = (pgbson *) DatumGetPointer(validatorDatum);

				/* The pgbson returned by DatumGetPointer doesn't have 4B VARHDR, add it with cloning func */
				validator = PgbsonCloneFromPgbson(validator);
				collection->schemaValidator.validator =
					CopyPgbsonIntoMemoryContext(validator, CurrentMemoryContext);
			}

			/* validation level stored as text */
			Datum validationLevelDatum = heap_getattr(tuple, 8, tupleDescriptor, &isNull);
			if (!isNull)
			{
				const char *validationLevelText = TextDatumGetCString(
					validationLevelDatum);
				collection->schemaValidator.validationLevel =
					strcmp(validationLevelText, "off") == 0 ? ValidationLevel_Off :
					strcmp(validationLevelText, "strict") == 0 ? ValidationLevel_Strict :
					strcmp(validationLevelText, "moderate") == 0 ?
					ValidationLevel_Moderate :
					ValidationLevel_Invalid;
			}

			/* validation action stored as text */
			Datum validationActionDatum = heap_getattr(tuple, 9, tupleDescriptor,
													   &isNull);
			if (!isNull)
			{
				const char *validationActionText = TextDatumGetCString(
					validationActionDatum);
				collection->schemaValidator.validationAction =
					strcmp(validationActionText, "warn") == 0 ? ValidationAction_Warn :
					strcmp(validationActionText, "error") == 0 ? ValidationAction_Error :
					ValidationAction_Invalid;
			}
		}

		/* table name is: documents_<collection id> */
		snprintf(collection->tableName, NAMEDATALEN, MONGO_DATA_TABLE_NAME_FORMAT,
				 collection->collectionId);

		collection->collectionId = collectionId;
		collection->relationId = relationId;
		if (collection->shardKey == NULL && collection->viewDefinition == NULL)
		{
			const char *shardName = TryGetShardNameForUnshardedCollection(relationId,
																		  collectionId,
																		  collection->
																		  tableName);
			if (shardName == NULL)
			{
				collection->shardTableName[0] = '\0';
			}
			else
			{
				strcpy(collection->shardTableName, shardName);
			}
		}

		collectionExists = true;
	}

	pfree(query.data);

	SPI_finish();

	return collectionExists;
}


/*
 * GetMongoCollectionFromCatalogByNameDatum returns whether a collection
 * with the given name (as database and collection name datums) exist in
 * ApiCatalogSchemaName.collections and writes the metadata to the collection
 * struct.
 */
static bool
GetMongoCollectionFromCatalogByNameDatum(Datum databaseNameDatum,
										 Datum collectionNameDatum,
										 MongoCollection *collection)
{
	bool collectionExists = false;

	StringInfoData query;
	const int argCount = 2;
	Oid argTypes[2];
	Datum argValues[2];
	MemoryContext outerContext = CurrentMemoryContext;

	memset(collection, 0, sizeof(MongoCollection));

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query,
					 "SELECT * FROM %s.collections WHERE database_name = $1 AND collection_name = $2",
					 ApiCatalogSchemaName);

	argTypes[0] = TEXTOID;
	argValues[0] = databaseNameDatum;

	argTypes[1] = TEXTOID;
	argValues[1] = collectionNameDatum;

	SPI_execute_with_args(query.data, argCount, argTypes, argValues, NULL, false, 1);
	if (SPI_processed > 0)
	{
		TupleDesc tupleDescriptor = SPI_tuptable->tupdesc;
		HeapTuple tuple = SPI_tuptable->vals[0];
		bool isNull = false;

		/* Attr 1 is database_name */
		/* Attr 2 is collection_name */

		Datum collectionIdDatum = heap_getattr(tuple, 3, tupleDescriptor, &isNull);
		if (isNull)
		{
			ereport(ERROR, (errmsg("collection_id should not be NULL in catalog")));
		}

		collection->collectionId = Int64GetDatum(collectionIdDatum);

		/* copy text bytes, buffers are already 0-initialized by memset above */
		memcpy(collection->name.databaseName, VARDATA_ANY(databaseNameDatum),
			   VARSIZE_ANY_EXHDR(databaseNameDatum));
		memcpy(collection->name.collectionName, VARDATA_ANY(collectionNameDatum),
			   VARSIZE_ANY_EXHDR(collectionNameDatum));

		/* table name is: documents_<collection id> */
		snprintf(collection->tableName, NAMEDATALEN, MONGO_DATA_TABLE_NAME_FORMAT,
				 collection->collectionId);

		Datum shardKeyDatum = heap_getattr(tuple, 4, tupleDescriptor, &isNull);
		if (!isNull)
		{
			pgbson *shardKeyBson = (pgbson *) DatumGetPointer(shardKeyDatum);
			collection->shardKey = CopyPgbsonIntoMemoryContext(shardKeyBson,
															   outerContext);
		}

		/* Attr 5 is collection_uuid */
		Datum uuidDatum = heap_getattr(tuple, 5, tupleDescriptor, &isNull);
		if (!isNull)
		{
			collection->collectionUUID = *DatumGetUUIDP(uuidDatum);
		}

		if (tupleDescriptor->natts >= 6)
		{
			Datum viewDatum = heap_getattr(tuple, 6, tupleDescriptor, &isNull);
			if (!isNull)
			{
				pgbson *viewDefinition = (pgbson *) DatumGetPointer(viewDatum);
				collection->viewDefinition =
					CopyPgbsonIntoMemoryContext(viewDefinition, outerContext);
			}
		}

		/* Attr 7,8,9 are for Schema Validation */
		if (tupleDescriptor->natts >= 9)
		{
			/* validator stored as pgbson */
			Datum validatorDatum = heap_getattr(tuple, 7, tupleDescriptor, &isNull);
			if (!isNull)
			{
				pgbson *validator = (pgbson *) DatumGetPointer(validatorDatum);
				validator = PgbsonCloneFromPgbson(validator);
				collection->schemaValidator.validator =
					CopyPgbsonIntoMemoryContext(validator, outerContext);
			}

			/* validation level stored as text */
			Datum validationLevelDatum = heap_getattr(tuple, 8, tupleDescriptor, &isNull);
			if (!isNull)
			{
				const char *validationLevelText = TextDatumGetCString(
					validationLevelDatum);
				collection->schemaValidator.validationLevel =
					strcmp(validationLevelText, "off") == 0 ? ValidationLevel_Off :
					strcmp(validationLevelText, "strict") == 0 ? ValidationLevel_Strict :
					strcmp(validationLevelText, "moderate") == 0 ?
					ValidationLevel_Moderate :
					ValidationLevel_Invalid;
			}

			/* validation action stored as text */
			Datum validationActionDatum = heap_getattr(tuple, 9, tupleDescriptor,
													   &isNull);
			if (!isNull)
			{
				const char *validationActionText = TextDatumGetCString(
					validationActionDatum);
				collection->schemaValidator.validationAction =
					strcmp(validationActionText, "warn") == 0 ? ValidationAction_Warn :
					strcmp(validationActionText, "error") == 0 ? ValidationAction_Error :
					ValidationAction_Invalid;
			}
		}

		collectionExists = true;
	}

	pfree(query.data);

	SPI_finish();

	return collectionExists;
}


/*
 * GetRelationIdForCollectionId returns the OID of the Postgres relation backing
 * the Mongo collection with given collection table id.
 *
 * Returns InvalidOid if no such collection exists.
 */
Oid
GetRelationIdForCollectionId(uint64 collectionId, LOCKMODE lockMode)
{
	StringInfo collectionTableNameStr = makeStringInfo();
	appendStringInfo(collectionTableNameStr, MONGO_DATA_TABLE_NAME_FORMAT, collectionId);

	Oid relationId = GetRelationIdForCollectionTableName(collectionTableNameStr->data,
														 lockMode);

	pfree(collectionTableNameStr->data);

	return relationId;
}


/*
 * GetRelationIdForCollectionTableName returns the OID of the Postgres relation backing
 * the Mongo collection with given collection table name.
 *
 * Returns InvalidOid if no such collection exists.
 */
static Oid
GetRelationIdForCollectionTableName(char *collectionTableName, LOCKMODE lockMode)
{
	bool missingOK = true;
	RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, collectionTableName, -1);

	return RangeVarGetRelid(rangeVar, lockMode, missingOK);
}


/*
 * GetMongoDataCreationTimeVarAttrNumber returns the attribute number of creation_time column.
 */
static AttrNumber
GetMongoDataCreationTimeVarAttrNumber(Oid collectionOid)
{
	HeapTuple tuple = SearchSysCacheAttName(collectionOid, "creation_time");

	if (!HeapTupleIsValid(tuple))
	{
		/* If collection doesn't exist, we'll arrive here */
		return (AttrNumber) 4;
	}

	Form_pg_attribute targetatt = (Form_pg_attribute) GETSTRUCT(tuple);
	int16 attnum = targetatt->attnum;
	ReleaseSysCache(tuple);

	return attnum;
}


/*
 * Check if DB exists. Check is done case insensitively. If exists, return
 * TRUE and populates the output parameter dbNameInTable with the db name
 * from the catalog table, else FALSE
 */
bool
TryGetDBNameByDatum(Datum databaseNameDatum, char *dbNameInTable)
{
	bool dbExists = false;
	StringInfoData query;
	const int argCount = 1;
	Oid argTypes[1];
	Datum argValues[1];

	SPI_connect();

	initStringInfo(&query);
	appendStringInfo(&query,
					 "SELECT database_name FROM %s.collections WHERE LOWER(database_name) = LOWER($1) LIMIT 1",
					 ApiCatalogSchemaName);

	argTypes[0] = TEXTOID;
	argValues[0] = databaseNameDatum;

	SPI_execute_with_args(query.data, argCount, argTypes, argValues, NULL, false, 1);
	if (SPI_processed > 0)
	{
		TupleDesc tupleDescriptor = SPI_tuptable->tupdesc;
		HeapTuple tuple = SPI_tuptable->vals[0];
		bool isNull = false;

		Datum databaseNameDatumInt = heap_getattr(tuple, 1, tupleDescriptor, &isNull);
		if (isNull)
		{
			ereport(ERROR, (errmsg("database_name should not be NULL in catalog")));
		}

		memcpy(dbNameInTable, VARDATA_ANY(databaseNameDatumInt),
			   VARSIZE_ANY_EXHDR(databaseNameDatumInt));
		dbExists = true;
	}

	pfree(query.data);

	SPI_finish();

	return dbExists;
}


/*
 * Checks if the given collection belongs to the group of Non writable system
 * namespace. If yes, an ereport is done.
 */
void
ValidateCollectionNameForUnauthorizedSystemNs(const char *collectionName,
											  Datum databaseNameDatum)
{
	for (int i = 0; i < NonWritableSystemCollectionNamesLength; i++)
	{
		if (strcmp(collectionName, NonWritableSystemCollectionNames[i]) == 0)
		{
			StringView databaseView = {
				.length = VARSIZE_ANY_EXHDR(databaseNameDatum),
				.string = VARDATA_ANY(databaseNameDatum)
			};

			/* Need to disallow user writes on NonWritableSystemCollectionNames */
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
							errmsg("cannot write to %.*s.%s",
								   databaseView.length, databaseView.string,
								   NonWritableSystemCollectionNames[i])));
		}
	}
}


/*
 * Checks if the given collection name belongs to a valid system namespace
 */
void
ValidateCollectionNameForValidSystemNamespace(StringView *collectionView,
											  Datum databaseNameDatum)
{
	if (StringViewStartsWithStringView(collectionView, &SystemPrefix))
	{
		bool found = false;
		for (int i = 0; i < ValidSystemCollectionNamesLength; i++)
		{
			char *collectionName = CreateStringFromStringView(collectionView);
			if (strcmp(collectionName, ValidSystemCollectionNames[i]) == 0)
			{
				found = true;
				ValidateCollectionNameForUnauthorizedSystemNs(
					collectionName, databaseNameDatum);
				break;
			}
		}

		if (!found)
		{
			StringView databaseView = {
				.length = VARSIZE_ANY_EXHDR(databaseNameDatum),
				.string = VARDATA_ANY(databaseNameDatum)
			};
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
							errmsg("Invalid system namespace: %.*s.%.*s",
								   databaseView.length, databaseView.string,
								   collectionView->length, collectionView->string)));
		}
	}
}


/*
 * command_collection_table returns the OID of a table that stores the data
 * for a given Mongo a collection and locks the table for reads.
 */
Datum
command_collection_table(PG_FUNCTION_ARGS)
{
	Datum databaseNameDatum = PG_GETARG_DATUM(0);
	Datum collectionNameDatum = PG_GETARG_DATUM(1);

	MongoCollection *collection = GetMongoCollectionByNameDatum(databaseNameDatum,
																collectionNameDatum,
																AccessShareLock);
	if (collection == NULL)
	{
		PG_RETURN_NULL();
	}

	PG_RETURN_OID(collection->relationId);
}


/*
 * command_invalidate_collection_cache sends an invalidation message that clears
 * the collection caches.
 */
Datum
command_invalidate_collection_cache(PG_FUNCTION_ARGS)
{
	CacheInvalidateRelcacheAll();
	PG_RETURN_VOID();
}


/*
 * command_get_collection_or_view returns the output of the ApiCatalogSchemaName.collections
 * table (all attributes) by database and collection name whether the collection is a
 * collection or a view
 */
Datum
command_get_collection_or_view(PG_FUNCTION_ARGS)
{
	bool allowViews = true;
	Datum returnedDatum = GetCollectionOrViewCore(fcinfo, allowViews);
	PG_RETURN_DATUM(returnedDatum);
}


/*
 * command_get_collection returns the output of the ApiCatalogSchemaName.collections
 * table (all attributes) by database and collection name if the result is a collection.
 * The query fails if the result is a view.
 */
Datum
command_get_collection(PG_FUNCTION_ARGS)
{
	bool allowViews = false;
	Datum returnedDatum = GetCollectionOrViewCore(fcinfo, allowViews);
	PG_RETURN_DATUM(returnedDatum);
}


/*
 * GetCollectionOrViewCore applies the core logic of returning the ApiCatalogSchemaName.collections
 * by database and collection name. Applies filtering based on whether allowViews is specified.
 */
static Datum
GetCollectionOrViewCore(PG_FUNCTION_ARGS, bool allowViews)
{
	Datum databaseDatum = PG_GETARG_DATUM(0);
	Datum collectionName = PG_GETARG_DATUM(1);

	Oid *resultTypeId = NULL;
	TupleDesc resultTupDesc = NULL;
	get_call_result_type(fcinfo, resultTypeId, &resultTupDesc);

	Datum *resultValues = palloc0(sizeof(Datum) * resultTupDesc->natts);
	bool *resultIsNulls = palloc0(sizeof(bool) * resultTupDesc->natts);

	SPI_connect();

	int tupleCountLimit = 1;
	const char *query =
		FormatSqlQuery(
			"SELECT * FROM %s.collections WHERE database_name = $1 and collection_name = $2",
			ApiCatalogSchemaName);
	int nargs = 2;
	Oid argTypes[2] = { TEXTOID, TEXTOID };
	Datum argValues[2] = { databaseDatum, collectionName };
	char *argNulls = NULL;
	bool readOnly = true;
	if (SPI_execute_with_args(query, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != SPI_OK_SELECT)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	bool hasCollection = false;
	if (SPI_processed >= 1 && SPI_tuptable)
	{
		hasCollection = true;
		AttrNumber i = 1;
		for (i = 1; i <= SPI_tuptable->tupdesc->natts && i <= resultTupDesc->natts; i++)
		{
			int tupleNumber = 0;
			bool isNull = true;
			Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[tupleNumber],
											  SPI_tuptable->tupdesc, i, &isNull);
			if (isNull)
			{
				resultIsNulls[i - 1] = true;
				resultValues[i - 1] = (Datum) 0;
			}
			else
			{
				resultIsNulls[i - 1] = false;
				resultValues[i - 1] = SPI_datumTransfer(resultDatum,
														SPI_tuptable->tupdesc->attrs[i -
																					 1].
														attbyval,
														SPI_tuptable->tupdesc->attrs[i -
																					 1].
														attlen);
			}
		}

		for (; i <= resultTupDesc->natts; i++)
		{
			resultIsNulls[i - 1] = true;
			resultValues[i - 1] = (Datum) 0;
		}
	}

	SPI_finish();

	if (hasCollection)
	{
		if (!allowViews && resultTupDesc->natts > 5 && !resultIsNulls[5])
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTEDONVIEW),
							errmsg("Namespace %s.%s is a view, not a collection",
								   TextDatumGetCString(databaseDatum),
								   TextDatumGetCString(collectionName))));
		}

		HeapTuple resultTup = heap_form_tuple(resultTupDesc, resultValues, resultIsNulls);
		PG_RETURN_DATUM(HeapTupleGetDatum(resultTup));
	}
	else
	{
		PG_RETURN_NULL();
	}
}


/*
 * CreateCollection is a C wrapper around create_collection. It returns
 * whether a new collection was created (in case of a race, another
 * transaction may have created it).
 */
bool
CreateCollection(Datum dbNameDatum, Datum collectionNameDatum)
{
	const char *cmdStr = FormatSqlQuery("SELECT %s.create_collection($1, $2)",
										ApiSchemaName);

	Oid argTypes[CREATE_COLLECTION_FUNC_NARGS] = { TEXTOID, TEXTOID };
	Datum argValues[CREATE_COLLECTION_FUNC_NARGS] = {
		dbNameDatum,
		collectionNameDatum,
	};

	/* all args are non-null */
	char *argNulls = NULL;

	bool isNull = true;
	bool readOnly = false;
	Datum resultDatum = ExtensionExecuteQueryWithArgsViaSPI(cmdStr,
															CREATE_COLLECTION_FUNC_NARGS,
															argTypes, argValues, argNulls,
															readOnly, SPI_OK_SELECT,
															&isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("create_collection unexpectedly "
							   "returned NULL datum")));
	}

	return DatumGetBool(resultDatum);
}


/*
 * RenameCollection is a C wrapper around rename_collection. It returns
 * whether whether the collection was renamed.
 */
void
RenameCollection(Datum dbNameDatum, Datum srcCollectionNameDatum, Datum
				 destCollectionNameDatum, bool dropTarget)
{
	const char *cmdStr = FormatSqlQuery("SELECT %s.rename_collection($1, $2, $3, $4)",
										ApiSchemaName);

	Oid argTypes[4] = { TEXTOID, TEXTOID, TEXTOID, BOOLOID };
	Datum argValues[4] = {
		dbNameDatum,
		srcCollectionNameDatum,
		destCollectionNameDatum,
		BoolGetDatum(dropTarget)
	};

	/* all args are non-null */
	char *argNulls = NULL;

	bool isNull = true;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr,
										4,
										argTypes, argValues, argNulls,
										readOnly, SPI_OK_SELECT,
										&isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("rename_collection unexpectedly "
							   "returned NULL datum")));
	}
}


/*
 * DropStagingCollectionForOut is a C wrapper for dropping a TEMP non-mongo collection
 * created during $out. Typically, the temp collection is promoted to be the target
 * user collection if $out succeeds. But, we would need to delete it if $out fails for
 * some reason. On such a delete, we want to turn of change tracking.
 */
void
DropStagingCollectionForOut(Datum dbNameDatum, Datum srcCollectionNameDatum)
{
	/*
	 *  Note that chage tracking is turned off for this delete
	 *  helio_api.drop_collection(
	 *      daatabaseName, collectionName, write_concern, uuid, track_changes)
	 */
	const char *cmdStr = FormatSqlQuery(
		"SELECT %s.drop_collection($1, $2, null, null, false)",
		ApiSchemaName);

	Oid argTypes[2] = { TEXTOID, TEXTOID };
	Datum argValues[2] = {
		dbNameDatum,
		srcCollectionNameDatum
	};

	/* all args are non-null */
	char *argNulls = NULL;

	bool isNull = true;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr,
										2,
										argTypes, argValues, argNulls,
										readOnly, SPI_OK_SELECT,
										&isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("drop_collection unexpectedly "
							   "returned NULL datum")));
	}
}


/*
 * OverWriteDataFromStagingToDest is a C wrapper around copy_collection_data. It returns
 * whether whether data was copied from the source to the destination.
 */
void
OverWriteDataFromStagingToDest(Datum srcDbNameDatum, Datum srcCollectionNameDatum, Datum
							   destDbNameDatum, Datum destCollectionNameDatum, bool
							   dropSourceCollection)
{
	const char *cmdStr =
		FormatSqlQuery("SELECT %s.copy_collection_data($1, $2, $3, $4, $5)",
					   ApiInternalSchemaName);

	Oid argTypes[5] = { TEXTOID, TEXTOID, TEXTOID, TEXTOID, BOOLOID };
	Datum argValues[5] = {
		srcDbNameDatum,
		srcCollectionNameDatum,
		destDbNameDatum,
		destCollectionNameDatum,
		BoolGetDatum(dropSourceCollection)
	};

	/* all args are non-null */
	char *argNulls = NULL;

	bool isNull = true;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr,
										5,
										argTypes, argValues, argNulls,
										readOnly, SPI_OK_SELECT,
										&isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg("copy_collection_data unexpectedly "
							   "returned NULL datum")));
	}
}


/*
 * CopyCollectionMetadata is a C wrapper around collection set method for $out. It returns
 * whether the destination collection was set up properly with the necessary indexes copied
 * from the source collection.
 */
void
SetupCollectionForOut(char *srcDbName, char *srcCollectionName, char *destDbName, char *
					  destCollectionName, bool createTemporaryTable)
{
	const char *cmdStr = createTemporaryTable ?
						 FormatSqlQuery(
		"SELECT %s.setup_temporary_out_collection($1, $2, $3, $4)", ApiInternalSchemaName)
						 :
						 FormatSqlQuery(
		"SELECT %s.setup_renameable_out_collection($1, $2, $3, $4)",
		ApiInternalSchemaName);

	Oid argTypes[4] = { TEXTOID, TEXTOID, TEXTOID, TEXTOID };
	Datum argValues[4] = {
		CStringGetTextDatum(srcDbName),
		CStringGetTextDatum(srcCollectionName),
		CStringGetTextDatum(destDbName),
		CStringGetTextDatum(destCollectionName)
	};

	/* all args are non-null */
	char *argNulls = NULL;

	bool isNull = true;
	bool readOnly = false;
	ExtensionExecuteQueryWithArgsViaSPI(cmdStr,
										4,
										argTypes, argValues, argNulls,
										readOnly, SPI_OK_SELECT,
										&isNull);
	if (isNull)
	{
		ereport(ERROR, (errmsg(
							"Setup Collection For Out unexpected returned NULL datum. createTemporaryTable = %d",
							createTemporaryTable)));
	}
}


/*
 * command_get_next_collection_id returns next unique collection id based on
 * the value of ApiGucPrefix.next_collection_id GUC if it is set.
 *
 * Otherwise, uses the next value of collections_collection_id_seq sequence.
 *
 * Note that ApiGucPrefix.next_collection_id GUC is only expected to be set in
 * regression tests to ensure consistent collection ids when running tests
 * in parallel.
 */
Datum
command_get_next_collection_id(PG_FUNCTION_ARGS)
{
	if (NextCollectionId != NEXT_COLLECTION_ID_UNSET)
	{
		int collectionId = NextCollectionId++;
		PG_RETURN_DATUM(UInt64GetDatum(collectionId));
	}

	PG_RETURN_DATUM(SequenceGetNextValAsUser(ApiCatalogCollectionIdSequenceId(),
											 HelioApiExtensionOwner()));
}


/*
 * Validation function that ensures that the database/collections created in
 * helioapi are valid.
 */
Datum
command_ensure_valid_db_coll(PG_FUNCTION_ARGS)
{
	ValidateDatabaseCollection(PG_GETARG_DATUM(0), PG_GETARG_DATUM(1));
	PG_RETURN_BOOL(true);
}


/*
 * Validation function that ensures that the database/collections created in
 * helioapi are valid.
 */
void
ValidateDatabaseCollection(Datum databaseDatum, Datum collectionDatum)
{
	text *databaseName = DatumGetTextP(databaseDatum);
	text *collectionName = DatumGetTextP(collectionDatum);

	StringView databaseView = {
		.length = VARSIZE_ANY_EXHDR(databaseName), .string = VARDATA_ANY(databaseName)
	};
	StringView collectionView = {
		.length = VARSIZE_ANY_EXHDR(collectionName), .string = VARDATA_ANY(collectionName)
	};

	if (databaseView.length >= MAX_DATABASE_NAME_LENGTH)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
						errmsg("Database %.*s must be less than 64 characters",
							   databaseView.length, databaseView.string)));
	}

	for (int i = 0; i < CharactersNotAllowedInDatabaseNamesLength; i++)
	{
		if (StringViewContains(&databaseView, CharactersNotAllowedInDatabaseNames[i]))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
							errmsg("Database %.*s has an invalid character %c",
								   databaseView.length, databaseView.string,
								   CharactersNotAllowedInDatabaseNames[i])));
		}
	}

	if (collectionView.string == NULL || collectionView.length == 0)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
						errmsg("Invalid namespace specified '%.*s.'",
							   databaseView.length, databaseView.string)));
	}

	if (StringViewStartsWith(&collectionView, '.'))
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
						errmsg("Collection names cannot start with '.': %.*s",
							   collectionView.length, collectionView.string)));
	}

	for (int i = 0; i < CharactersNotAllowedInCollectionNamesLength; i++)
	{
		if (StringViewContains(&collectionView, CharactersNotAllowedInCollectionNames[i]))
		{
			ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
							errmsg("Invalid collection name: %.*s",
								   collectionView.length, collectionView.string)));
		}
	}

	if (databaseView.length + collectionView.length + 1 > MaxDatabaseCollectionLength)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
						errmsg("Full namespace must not exceed %u bytes.",
							   MaxDatabaseCollectionLength)));
	}

	ValidateCollectionNameForValidSystemNamespace(&collectionView,
												  PointerGetDatum(databaseName));
}


/*
 * Validation function that ensures that the database name is unique
 * case insensitively
 */
Datum
validate_dbname(PG_FUNCTION_ARGS)
{
	text *databaseName = PG_GETARG_TEXT_P(0);

	StringView databaseView = {
		.length = VARSIZE_ANY_EXHDR(databaseName), .string = VARDATA_ANY(databaseName)
	};

	if (databaseView.length >= MAX_DATABASE_NAME_LENGTH)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_INVALIDNAMESPACE),
						errmsg("Database %.*s must be less than 64 characters",
							   databaseView.length, databaseView.string)));
	}

	char dbNameInTable[MAX_DATABASE_NAME_LENGTH] = { 0 };
	if (TryGetDBNameByDatum(PG_GETARG_DATUM(0), (char *) dbNameInTable))
	{
		if (!StringViewEqualsCString(&databaseView, (char *) dbNameInTable))
		{
			ereport(ERROR,
					(errcode(ERRCODE_HELIO_DBALREADYEXISTS),
					 errmsg("db already exists with different case already have: "
							"[%s] trying to create [%.*s]", dbNameInTable,
							databaseView.length, databaseView.string)));
		}
	}

	PG_RETURN_VOID();
}


/*
 * Updates the schema validation information of an existing collection
 */
void
UpsertSchemaValidation(Datum databaseDatum,
					   Datum collectionNameDatum,
					   const bson_value_t *validator, char *validationLevel,
					   char *validationAction)
{
	StringInfo query = makeStringInfo();
	appendStringInfo(query, "UPDATE %s.collections set ", ApiCatalogSchemaName);

	Oid argsTypes[5] = { TEXTOID, TEXTOID };
	Datum argValues[5] = {
		databaseDatum, collectionNameDatum
	};

	int nargs = 2;
	char argNulls[5] = { ' ', ' ', 'n', 'n', 'n' };
	bool isNullIgnore = false;

	bool isFirst = true;

	/* todo: add spec validation for validator, like unsupported keywrods, etc. */
	if (validator != NULL && validator->value_type != BSON_TYPE_EOD)
	{
		appendStringInfo(query, "validator = $%d ", ++nargs);
		argNulls[nargs - 1] = ' ';
		argsTypes[nargs - 1] = BsonTypeId();
		argValues[nargs - 1] = PointerGetDatum(PgbsonInitFromDocumentBsonValue(
												   validator));
		isFirst = false;
	}
	if (!isFirst && validationLevel != NULL)
	{
		appendStringInfo(query, ", ");
	}
	if (validationLevel != NULL)
	{
		appendStringInfo(query, "validation_level = $%d ", ++nargs);
		argNulls[nargs - 1] = ' ';
		argsTypes[nargs - 1] = TEXTOID;
		argValues[nargs - 1] = CStringGetTextDatum(validationLevel);
		isFirst = false;
	}
	if (!isFirst && validationAction != NULL)
	{
		appendStringInfo(query, ", ");
	}
	if (validationAction != NULL)
	{
		appendStringInfo(query, "validation_action = $%d ", ++nargs);
		argNulls[nargs - 1] = ' ';
		argsTypes[nargs - 1] = TEXTOID;
		argValues[nargs - 1] = CStringGetTextDatum(validationAction);
	}

	appendStringInfo(query, "WHERE database_name = $1 AND collection_name = $2");

	RunQueryWithCommutativeWrites(query->data, nargs, argsTypes, argValues, argNulls,
								  SPI_OK_UPDATE, &isNullIgnore);
}


/*
 * This function parses and checks the bson value for "validator" option
 * given in "create"/"collMod" command
 */
const bson_value_t *
ParseAndGetValidatorSpec(bson_iter_t *iter, const char *validatorName, bool *hasValue)
{
	if (!EnableSchemaValidation)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg("validator not supported yet")));
	}

	/* "validator" values of "null" and "undefined" are valid */
	if (BSON_ITER_HOLDS_UNDEFINED(iter) ||
		BSON_ITER_HOLDS_NULL(iter))
	{
		return NULL;
	}

	EnsureTopLevelFieldType(validatorName, iter, BSON_TYPE_DOCUMENT);
	const bson_value_t *validator = bson_iter_value(iter);

	/* Large and overly complex validation rules can impact database performance, especially during write operations. */
	if (validator->value.v_doc.data_len > (uint32_t) MaxSchemaValidatorSize)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"validator of size > %dKB is not supported. Contact Azure Support if you need to increase this limit.",
							MaxSchemaValidatorSize / 1024),
						errmsg(
							"validator of size > %dKB is not supported. Contact Azure Support if you need to increase this limit.",
							MaxSchemaValidatorSize / 1024)));
	}

	*hasValue = true;
	return validator;
}


/*
 * This function parses and checks the bson value for "validationAction" option
 * given in "create"/"collMod" command
 */
char *
ParseAndGetValidationActionOption(bson_iter_t *iter, const char *validationActionName,
								  bool *hasValue)
{
	if (!EnableSchemaValidation)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg("validator not supported yet")));
	}

	/* "validationAction" values of "null" and "undefined" are valid. */
	if (BSON_ITER_HOLDS_UNDEFINED(iter) ||
		BSON_ITER_HOLDS_NULL(iter))
	{
		return NULL;
	}

	EnsureTopLevelFieldType(validationActionName, iter, BSON_TYPE_UTF8);
	const char *validationAction = bson_iter_utf8(iter, NULL);
	if (strcmp(validationAction, "warn") == 0 ||
		strcmp(validationAction, "error") == 0
		)
	{
		*hasValue = true;
		return pstrdup(validationAction);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"Enumeration value '%s' for field '%s' is not a valid value.",
							validationAction, validationActionName),
						errdetail_log(
							"Enumeration value '%s' for field '%s' is not a valid value.",
							validationAction, validationActionName)));
	}
}


/*
 * This function parses and checks the bson value for "validationLevel" option
 * given in "create"/"collMod" command
 */
char *
ParseAndGetValidationLevelOption(bson_iter_t *iter, const char *validationLevelName,
								 bool *hasValue)
{
	if (!EnableSchemaValidation)
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_COMMANDNOTSUPPORTED),
						errmsg("validator not supported yet")));
	}

	/* "validationLevel" values of "null" and "undefined" are valid. */
	if (BSON_ITER_HOLDS_UNDEFINED(iter) ||
		BSON_ITER_HOLDS_NULL(iter))
	{
		return NULL;
	}

	EnsureTopLevelFieldType(validationLevelName, iter, BSON_TYPE_UTF8);
	const char *validationLevel = bson_iter_utf8(iter, NULL);
	if (strcmp(validationLevel, "off") == 0 ||
		strcmp(validationLevel, "strict") == 0 ||
		strcmp(validationLevel, "moderate") == 0
		)
	{
		*hasValue = true;
		return pstrdup(validationLevel);
	}
	else
	{
		ereport(ERROR, (errcode(ERRCODE_HELIO_BADVALUE),
						errmsg(
							"Enumeration value '%s' for field '%s' is not a valid value.",
							validationLevel, validationLevelName),
						errdetail_log(
							"Enumeration value '%s' for field '%s' is not a valid value.",
							validationLevel, validationLevelName)));
	}
}
