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
#include <access/amapi.h>
#include <nodes/parsenodes.h>
#include <nodes/pathnodes.h>

#include "metadata/collection.h"

/* Section: General Extension points */

/*
 * Returns true if the current Postgres server is a Query Coordinator
 * that also owns the metadata management of schema (DDL).
 */
typedef bool (*IsMetadataCoordinator_HookType)(void);
extern IsMetadataCoordinator_HookType is_metadata_coordinator_hook;

/*
 * Indicates whether the Change Stream feature is currently enabled
 */
typedef bool (*IsChangeStreamEnabledAndCompatible)(void);
extern IsChangeStreamEnabledAndCompatible is_changestream_enabled_and_compatible_hook;

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


/*
 * Runs a query via SPI with sequential shard execution for distributed scenarios
 * Returns the Datum returned by the executed query.
 */
typedef Datum (*RunQueryWithSequentialModification_HookType)(const char *query, int
															 expectedSPIOK, bool *isNull);
extern RunQueryWithSequentialModification_HookType
	run_query_with_sequential_modification_mode_hook;


/* Section: Create Table Extension points */

/*
 * Distributes a given postgres table with the provided distribution column.
 * Optionally supports colocating the distributed table with another distributed table.
 */
typedef const char *(*DistributePostgresTable_HookType)(const char *postgresTable, const
														char *distributionColumn,
														const char *colocateWith,
														int shardCount);
extern DistributePostgresTable_HookType distribute_postgres_table_hook;


/*
 * Entrypoint to modify a list of column names for queries
 * For a base RTE (table)
 */
typedef List *(*ModifyTableColumnNames_HookType)(List *tableColumns);
extern ModifyTableColumnNames_HookType modify_table_column_names_hook;

/*
 * Creates a user using an external identity provider
 */
typedef bool (*CreateUserWithExernalIdentityProvider_HookType)(const char *userName,
															   char *pgRole, bson_value_t
															   customData);
extern CreateUserWithExernalIdentityProvider_HookType
	create_user_with_exernal_identity_provider_hook;

/*
 * Drops a user using an external identity provider
 */
typedef bool (*DropUserWithExernalIdentityProvider_HookType)(const char *userName);
extern DropUserWithExernalIdentityProvider_HookType
	drop_user_with_exernal_identity_provider_hook;

/*
 * Method to verify if a user is native
 */
typedef bool (*IsUserExternal_HookType)(const char *userName);
extern IsUserExternal_HookType
	is_user_external_hook;


/*
 * Method to get user info from external identity provider
 */
typedef const pgbson *(*GetUserInfoFromExternalIdentityProvider_HookType)();
extern GetUserInfoFromExternalIdentityProvider_HookType
	get_user_info_from_external_identity_provider_hook;


/* Method for username validation */
typedef bool (*UserNameValidation_HookType)(const char *username);
extern UserNameValidation_HookType username_validation_hook;


/* Method for password validation */
typedef bool (*PasswordValidation_HookType)(const char *username, const char *password);
extern PasswordValidation_HookType password_validation_hook;


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

typedef void (*AllowNestedDistributionInCurrentTransaction_HookType)(void);
extern AllowNestedDistributionInCurrentTransaction_HookType
	allow_nested_distribution_in_current_transaction_hook;

typedef bool (*IsShardTableForDocumentDbTable_HookType)(const char *relName, const
														char *numEndPointer);

extern IsShardTableForDocumentDbTable_HookType is_shard_table_for_documentdb_table_hook;

typedef void (*HandleColocation_HookType)(MongoCollection *collection,
										  const bson_value_t *colocationOptions);

extern HandleColocation_HookType handle_colocation_hook;

typedef Query *(*RewriteListCollectionsQueryForDistribution_HookType)(Query *query);
extern RewriteListCollectionsQueryForDistribution_HookType
	rewrite_list_collections_query_hook;

typedef Query *(*RewriteConfigQueryForDistribution_HookType)(Query *query);
extern RewriteConfigQueryForDistribution_HookType rewrite_config_shards_query_hook;
extern RewriteConfigQueryForDistribution_HookType rewrite_config_chunks_query_hook;

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

typedef const char *(*GetDistributedApplicationName_HookType)(void);
extern GetDistributedApplicationName_HookType get_distributed_application_name_hook;

typedef bool (*IsNtoReturnSupported_HookType)(void);
extern IsNtoReturnSupported_HookType is_n_to_return_supported_hook;

typedef bool (*EnsureMetadataTableReplicated_HookType)(const char *);
extern EnsureMetadataTableReplicated_HookType ensure_metadata_table_replicated_hook;

typedef void (*PostSetupCluster_HookType)(bool, bool (shouldUpgradeFunc(void *, int, int,
																		int)), void *);
extern PostSetupCluster_HookType post_setup_cluster_hook;

/*
 * Hook for customizing the validation of vector query spec.
 */
typedef struct VectorSearchOptions VectorSearchOptions;
typedef void
(*TryCustomParseAndValidateVectorQuerySpec_HookType)(const char *key,
													 const bson_value_t *value,
													 VectorSearchOptions *
													 vectorSearchOptions);
extern TryCustomParseAndValidateVectorQuerySpec_HookType
	try_custom_parse_and_validate_vector_query_spec_hook;

extern bool DefaultInlineWriteOperations;
extern bool ShouldUpgradeDataTables;

typedef char *(*TryGetExtendedVersionRefreshQuery_HookType)(void);
extern TryGetExtendedVersionRefreshQuery_HookType
	try_get_extended_version_refresh_query_hook;

typedef void (*GetShardIdsAndNamesForCollection_HookType)(Oid relationOid, const
														  char *tableName,
														  Datum **shardOidArray,
														  Datum **shardNameArray,
														  int32_t *shardCount);
extern GetShardIdsAndNamesForCollection_HookType
	get_shard_ids_and_names_for_collection_hook;


typedef const char *(*GetPidForIndexBuild_HookType)(void);
extern GetPidForIndexBuild_HookType get_pid_for_index_build_hook;


typedef const char *(*TryGetIndexBuildJobOpIdQuery_HookType)(void);
extern TryGetIndexBuildJobOpIdQuery_HookType try_get_index_build_job_op_id_query_hook;


typedef char *(*TryGetCancelIndexBuildQuery_HookType)(int32_t indexId, char cmdType);
extern TryGetCancelIndexBuildQuery_HookType try_get_cancel_index_build_query_hook;


typedef bool (*ShouldScheduleIndexBuilds_HookType)();
extern ShouldScheduleIndexBuilds_HookType should_schedule_index_builds_hook;

typedef List *(*GettShardIndexOids_HookType)(uint64_t collectionId, int indexId, bool
											 ignoreMissing);
extern GettShardIndexOids_HookType get_shard_index_oids_hook;

typedef void (*UpdatePostgresIndex_HookType)(uint64_t collectionId, int indexId, int
											 operation, bool value);
extern UpdatePostgresIndex_HookType update_postgres_index_hook;

typedef const char *(*GetOperationCancellationQuery_HookType)(int64 shardId,
															  StringView *opIdString,
															  int *nargs, Oid **argTypes,
															  Datum **argValues,
															  char **argNulls);
extern GetOperationCancellationQuery_HookType get_operation_cancellation_query_hook;

typedef bool (*DefaultEnableCompositeOpClass_HookType)(void);
extern DefaultEnableCompositeOpClass_HookType default_enable_composite_op_class_hook;

#endif
