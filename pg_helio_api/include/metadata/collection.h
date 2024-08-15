/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/metadata/collection.h
 *
 * Common declarations for Mongo collections
 *
 *-------------------------------------------------------------------------
 */

#ifndef MONGO_COLLECTIONS_H
#define MONGO_COLLECTIONS_H

#include <storage/lockdefs.h>
#include <access/attnum.h>
#include <utils/uuid.h>

#include "io/helio_bson_core.h"


/* The max depth a view can have */
#define MAX_VIEW_DEPTH 20

#define MAX_DATABASE_NAME_LENGTH (64)
#define MAX_COLLECTION_NAME_LENGTH (256)
#define MAX_NAMESPACE_NAME_LENGTH (64 + 256 + 1)
#define MONGO_DATA_TABLE_NAME_PREFIX "documents_"
#define MONGO_DATA_TABLE_NAME_FORMAT MONGO_DATA_TABLE_NAME_PREFIX UINT64_FORMAT

/* constants for document column of a Mongo data table */
#define MONGO_DATA_TABLE_DOCUMENT_VAR_COLLATION (InvalidOid)
#define MONGO_DATA_TABLE_DOCUMENT_VAR_TYPMOD ((int32) (-1))


/* Attribute number constants for the layout of the mongo data table */
#define MONGO_DATA_TABLE_SHARD_KEY_VALUE_VAR_ATTR_NUMBER ((AttrNumber) 1)
#define MONGO_DATA_TABLE_OBJECT_ID_VAR_ATTR_NUMBER ((AttrNumber) 2)
#define MONGO_DATA_TABLE_DOCUMENT_VAR_ATTR_NUMBER ((AttrNumber) 3)
#define MONGO_CHANGE_STREAM_TABLE_DOCUMENT_VAR_ATTR_NUMBER ((AttrNumber) 1)
#define MONGO_CHANGE_STREAM_TABLE_CONTINUATION_VAR_ATTR_NUMBER ((AttrNumber) 2)


/*
 * MongoCollectionName represents the qualified name of a Mongo collection.
 */
typedef struct
{
	/* name of the Mongo database */
	char databaseName[MAX_DATABASE_NAME_LENGTH];

	/* name of the Mongo collection */
	char collectionName[MAX_COLLECTION_NAME_LENGTH];
} MongoCollectionName;

/*
 * Validation Level enum used for schema validation of existing docs.
 * Supported levels: "off", "strict", "moderate"
 */
typedef enum
{
	ValidationLevel_Invalid = 0,
	ValidationLevel_Strict,
	ValidationLevel_Moderate,
	ValidationLevel_Off
} ValidationLevels;

/*
 * Validation Action enum used for schema validation to specify how to handle invalid documents.
 * Supported actions: "warn", "error"
 */
typedef enum
{
	ValidationAction_Invalid = 0,
	ValidationAction_Warn,
	ValidationAction_Error
} ValidationActions;

/* This struct stores Schema Validation options associated with a collection */
typedef struct
{
	pgbson *validator;
	ValidationLevels validationLevel;
	ValidationActions validationAction;
} SchemaValidatorInfo;

/*
 * MongoCollection contains metadata of a single Mongo collection.
 */
typedef struct
{
	/* qualified name of the Mongo collection */
	MongoCollectionName name;

	/* internal identifier of the Mongo collection */
	uint64 collectionId;

	/* name of the Postgres table */
	char tableName[NAMEDATALEN];

	/* OID of the Postgres table */
	Oid relationId;

	/* shard key BSON */
	pgbson *shardKey;

	/* View definition if applicable */
	pgbson *viewDefinition;

	/* The UUID of the collection or view */
	pg_uuid_t collectionUUID;

	/* creation_time column attribute number */
	AttrNumber mongoDataCreationTimeVarAttrNumber;

	/*
	 * An optional name for the shardTable if it has a distributed table associated with it
	 * on the current node or empty string (Default) if unavailable.
	 */
	char shardTableName[NAMEDATALEN];

	/* Schema Validator if applicable */
	SchemaValidatorInfo schemaValidator;
} MongoCollection;


/*
 * ViewDefinition is the decomposed version of a single viewDefinition bson
 */
typedef struct
{
	/* The name of the source collection or view */
	const char *viewSource;

	/* An optional pipeline to apply to the view
	 * If not specified it's BSON_TYPE_EOD.
	 */
	bson_value_t pipeline;
} ViewDefinition;


/* decomposes the viewSpec into a ViewDefinition struct */
void DecomposeViewDefinition(pgbson *viewSpec, ViewDefinition *viewDefinition);
pgbson * CreateViewDefinition(const ViewDefinition *viewDefinition);
pgbson * CreateSchemaValidatorInfoDefinition(const
											 SchemaValidatorInfo *schemaValidatorInfo);
void ValidateViewDefinition(Datum databaseDatum, const char *viewName, const
							ViewDefinition *definition);
void ValidateDatabaseCollection(Datum databaseDatum, Datum collectionDatum);


/* get Mongo collection metadata by name */
MongoCollection * GetMongoCollectionByNameDatum(Datum dbNameDatum,
												Datum collectionNameDatum,
												LOCKMODE lockMode);
MongoCollection * GetMongoCollectionOrViewByNameDatum(Datum dbNameDatum,
													  Datum collectionNameDatum,
													  LOCKMODE lockMode);

MongoCollection * GetTempMongoCollectionByNameDatum(Datum dbNameDatum,
													Datum collectionNameDatum,
													char *collectionName,
													LOCKMODE lockMode);

/*
 * Returns the OID of the physical shard table if applicable and if it
 * is available on the current node. If no such valid shard table can be
 * found (due to the table having multiple shards or it being on a different
 * machine), returns InvalidOid
 */
Oid TryGetCollectionShardTable(MongoCollection *collection, LOCKMODE lockMode);

/*
 * Check if DB exists. Check is done case insensitively. If exists, return
 * TRUE and populates the output parameter dbNameInTable with the db name
 * from the catalog table, else FALSE
 */
bool TryGetDBNameByDatum(Datum databaseNameDatum, char *dbNameInTable);


/*
 * Checks if the given collection belongs to the group of Non writable system
 * namespace. If yes, an ereport is done.
 */
void ValidateCollectionNameForUnauthorizedSystemNs(const char *collectionName,
												   Datum databaseNameDatum);


/*
 * Checks if the given collection name belongs to a valid system namespace
 */
void ValidateCollectionNameForValidSystemNamespace(StringView *collectionView,
												   Datum databaseNameDatum);


/*
 * Data table for given MongoCollection has been created within the current
 * transaction ?
 */
bool IsDataTableCreatedWithinCurrentXact(const MongoCollection *collection);

/* make a copy of given MongoCollection */
MongoCollection * CopyMongoCollection(const MongoCollection *collection);

/* get Mongo collection metadata by collection id */
MongoCollection * GetMongoCollectionByColId(uint64 collectionId, LOCKMODE lockMode);

/* get OID of Mongo documents table by collection id */
Oid GetRelationIdForCollectionId(uint64 collectionId, LOCKMODE lockMode);

/* c-wrapper for create_collection() */
bool CreateCollection(Datum dbNameDatum, Datum collectionNameDatum);

/* c-wrapper for copy_collection_metadata() */
void SetupCollectionForOut(char *srcDbName, char *srcCollectionName, char *destDbName,
						   char *destCollectionName, bool createTemporaryTable);

/* c-wrapper for rename_collection() */
void RenameCollection(Datum dbNameDatum, Datum srcCollectionNameDatum, Datum
					  destCollectionNameDatum, bool dropTarget);

/* c-wrapper for droping the staging collection created during $out */
void DropStagingCollectionForOut(Datum dbNameDatum, Datum srcCollectionNameDatumt);

/* c-wrapper for copy_collection_data() */
void OverWriteDataFromStagingToDest(Datum srcDbNameDatum, Datum srcCollectionNameDatum,
									Datum
									destDbNameDatum, Datum destCollectionNameDatum, bool
									dropSourceCollection);

/* called by metadata_cache.c when cache invalidation occurs */
void ResetCollectionsCache(void);
void InvalidateCollectionByRelationId(Oid relationId);

/* insert/update schema validation meta*/
void UpsertSchemaValidation(Datum databaseDatum,
							Datum collectionNameDatum,
							const bson_value_t *validator,
							char *validationLevel,
							char *validationAction);

const bson_value_t * ParseAndGetValidatorSpec(bson_iter_t *iter, const
											  char *validatorName,
											  bool *hasValue);
char * ParseAndGetValidationLevelOption(bson_iter_t *iter, const
										char *validationLevelName, bool *hasValue);
char * ParseAndGetValidationActionOption(bson_iter_t *iter, const
										 char *validationActionName, bool *hasValue);
#endif
