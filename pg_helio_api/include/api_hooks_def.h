/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/api_hooks_def.h
 *
 * Definition of hooks for the extension that allow for handling
 * distribution type scenarios. These can be overriden to implement
 * custom distribution logic.
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_API_HOOKS_DEF_H
#define EXTENSION_API_HOOKS_DEF_H

#include "api_hooks_common.h"

#include <nodes/parsenodes.h>

/* Section: General Extension points */

/*
 * Returns true if the current Postgres server is a Query Coordinator
 * that also owns the metadata management of schema (DDL).
 */
typedef bool (*IsMetadataCoordinator_HookType)(void);
extern IsMetadataCoordinator_HookType is_metadata_coordinator_hook;


/*
 * Runs a command on the MetadataCoordinator if the current node is not a
 * Metadata Coordinator. The response is returned as a "record" struct
 * with the nodeId responding, whether or not the command succeeded and
 * the response datum serialized as a string.
 * If success, then this is the response datum in text format.
 * If failed, then this contains the error string from the failure.
 */
typedef DistributedRunCommandResult (*RunCommandOnMetadataCoordinator_HookType)(const
																				char *
																				query);
extern RunCommandOnMetadataCoordinator_HookType run_command_on_metadata_coordinator_hook;

/*
 * Runs a query via SPI with commutative writes on for distributed scenarios.
 * Returns the Datum returned by the executed query.
 */
typedef Datum (*RunQueryWithCommutativeWrites_HookType)(const char *query, int nargs,
														Oid *argTypes,
														Datum *argValues, char *argNulls,
														int expectedSPIOK, bool *isNull);
extern RunQueryWithCommutativeWrites_HookType run_query_with_commutative_writes_hook;


/* Section: Create Table Extension points */

/*
 * Distributes a given postgres table with the provided distribution column.
 * Optionally supports colocating the distributed table with another distributed table.
 */
typedef void (*DistributePostgresTable_HookType)(const char *postgresTable, const
												 char *distributionColumn,
												 const char *colocateWith,
												 bool isUnsharded);
extern DistributePostgresTable_HookType distribute_postgres_table_hook;

/*
 * Given a current table schema built up to create a postgres table, adds a hook to
 * modify the schema if needed.
 */
typedef void (*ModifyCreateTableSchema_HookType)(StringInfo currentSchema, const
												 char *tableName);
extern ModifyCreateTableSchema_HookType modify_create_table_schema_hook;


/*
 * Handle any post actions after the table is created
 */
typedef void (*PostProcessCreateTable_HookType)(const char *postgresTable, uint64_t
												collectionId,
												text *databaseName, text *collectionName);
extern PostProcessCreateTable_HookType post_process_create_table_hook;

/*
 * Handle any post actions after the table is sharded
 */
typedef void (*PostProcessShardCollection_HookType)(const char *tableName, uint64_t
													collectionId,
													text *databaseName,
													text *collectionName,
													pgbson *shardKey);
extern PostProcessShardCollection_HookType post_process_shard_collection_hook;

/*
 * Handle any post action after the collection is drop
 */
typedef void (*PostProcessCollectionDrop_HookType)(uint64_t collectionId,
												   text *databaseName,
												   text *collectionName, bool
												   trackChanges);
extern PostProcessCollectionDrop_HookType post_process_drop_collection_hook;

/*
 * Entrypoint to modify a list of column names for queries
 * For a base RTE (table)
 */
typedef List *(*ModifyTableColumnNames_HookType)(List *tableColumns);
extern ModifyTableColumnNames_HookType modify_table_column_names_hook;


/*
 * Hook for enabling running a query with nested distribution enabled.
 */
typedef void (*RunQueryWithNestedDistribution_HookType)(const char *query,
														int nArgs, Oid *argTypes,
														Datum *argDatums,
														char *argNulls,
														bool readOnly,
														int expectedSPIOK,
														Datum *datums,
														bool *isNull,
														int numValues);
extern RunQueryWithNestedDistribution_HookType run_query_with_nested_distribution_hook;

typedef bool (*IsShardTableForMongoTable_HookType)(const char *relName, const
												   char *numEndPointer);

extern IsShardTableForMongoTable_HookType is_shard_table_for_mongo_table_hook;

typedef void (*HandleColocation_HookType)(MongoCollection *collection,
										  const bson_value_t *colocationOptions);

extern HandleColocation_HookType handle_colocation_hook;

typedef Query *(*RewriteListCollectionsQueryForDistribution_HookType)(Query *query);
extern RewriteListCollectionsQueryForDistribution_HookType
	rewrite_list_collections_query_hook;

typedef const char *(*TryGetShardNameForUnshardedCollection_HookType)(Oid relationOid,
																	  uint64 collectionId,
																	  const char *
																	  tableName);
extern TryGetShardNameForUnshardedCollection_HookType
	try_get_shard_name_for_unsharded_collection_hook;

/*
 * Hook for creating an update tracker if tracking is enabled.
 */
typedef BsonUpdateTracker *(*CreateBsonUpdateTracker_HookType)(void);
extern CreateBsonUpdateTracker_HookType create_update_tracker_hook;

typedef pgbson *(*BuildUpdateDescription_HookType)(BsonUpdateTracker *);
extern BuildUpdateDescription_HookType build_update_description_hook;

/* Update tracker method hooks */
typedef void (*NotifyRemovedField_HookType)(BsonUpdateTracker *tracker, const
											char *relativePath);
extern NotifyRemovedField_HookType notify_remove_field_hook;

typedef void (*NotifyUpdatedField_HookType)(BsonUpdateTracker *tracker, const
											char *relativePath,
											const bson_value_t *value);
extern NotifyUpdatedField_HookType notify_updated_field_hook;

typedef void (*NotifyUpdatedFieldPathView_HookType)(BsonUpdateTracker *tracker, const
													StringView *relativePath,
													const bson_value_t *value);
extern NotifyUpdatedFieldPathView_HookType notify_updated_field_path_view_hook;


typedef const char *(*GetDistributedApplicationName_HookType)(void);
extern GetDistributedApplicationName_HookType get_distributed_application_name_hook;

extern bool DefaultInlineWriteOperations;
#endif
