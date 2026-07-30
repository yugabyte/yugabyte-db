/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/api_hooks.c
 *
 * Declaration and base implementation of API Hooks.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <limits.h>

#include "index_am/documentdb_rum.h"
#include "metadata/collection.h"
#include "metadata/index.h"
#include "io/bson_core.h"
#include "lib/stringinfo.h"
#include "api_hooks.h"
#include "api_hooks_def.h"
#include "utils/query_utils.h"
#include "utils/documentdb_errors.h"
#include "vector/vector_spec.h"

extern bool DefaultUseCompositeOpClass;


IsMetadataCoordinator_HookType is_metadata_coordinator_hook = NULL;
RunCommandOnMetadataCoordinator_HookType run_command_on_metadata_coordinator_hook = NULL;
RunQueryWithCommutativeWrites_HookType run_query_with_commutative_writes_hook = NULL;
RunQueryWithSequentialModification_HookType
	run_query_with_sequential_modification_mode_hook = NULL;
DistributePostgresTable_HookType distribute_postgres_table_hook = NULL;
ModifyTableColumnNames_HookType modify_table_column_names_hook = NULL;
RunQueryWithNestedDistribution_HookType run_query_with_nested_distribution_hook = NULL;
AllowNestedDistributionInCurrentTransaction_HookType
	allow_nested_distribution_in_current_transaction_hook = NULL;
IsShardTableForDocumentDbTable_HookType is_shard_table_for_documentdb_table_hook = NULL;
HandleColocation_HookType handle_colocation_hook = NULL;
RewriteListCollectionsQueryForDistribution_HookType rewrite_list_collections_query_hook =
	NULL;
RewriteConfigQueryForDistribution_HookType rewrite_config_shards_query_hook = NULL;
RewriteConfigQueryForDistribution_HookType rewrite_config_chunks_query_hook = NULL;
TryGetShardNameForUnshardedCollection_HookType
	try_get_shard_name_for_unsharded_collection_hook = NULL;
GetDistributedApplicationName_HookType get_distributed_application_name_hook = NULL;
IsChangeStreamEnabledAndCompatible is_changestream_enabled_and_compatible_hook = NULL;
IsNtoReturnSupported_HookType is_n_to_return_supported_hook = NULL;
EnsureMetadataTableReplicated_HookType ensure_metadata_table_replicated_hook = NULL;
PostSetupCluster_HookType post_setup_cluster_hook = NULL;
TryCustomParseAndValidateVectorQuerySpec_HookType
	try_custom_parse_and_validate_vector_query_spec_hook = NULL;
TryGetExtendedVersionRefreshQuery_HookType try_get_extended_version_refresh_query_hook =
	NULL;
GetShardIdsAndNamesForCollection_HookType get_shard_ids_and_names_for_collection_hook =
	NULL;
CreateUserWithExernalIdentityProvider_HookType
	create_user_with_exernal_identity_provider_hook = NULL;
DropUserWithExernalIdentityProvider_HookType
	drop_user_with_exernal_identity_provider_hook = NULL;
GetUserInfoFromExternalIdentityProvider_HookType
	get_user_info_from_external_identity_provider_hook = NULL;
IsUserExternal_HookType
	is_user_external_hook = NULL;
GetPidForIndexBuild_HookType get_pid_for_index_build_hook = NULL;
TryGetIndexBuildJobOpIdQuery_HookType try_get_index_build_job_op_id_query_hook =
	NULL;
TryGetCancelIndexBuildQuery_HookType try_get_cancel_index_build_query_hook =
	NULL;
ShouldScheduleIndexBuilds_HookType should_schedule_index_builds_hook = NULL;

GettShardIndexOids_HookType get_shard_index_oids_hook = NULL;
UpdatePostgresIndex_HookType update_postgres_index_hook = NULL;
GetOperationCancellationQuery_HookType get_operation_cancellation_query_hook = NULL;

UserNameValidation_HookType
	username_validation_hook = NULL;
PasswordValidation_HookType
	password_validation_hook = NULL;

DefaultEnableCompositeOpClass_HookType
	default_enable_composite_op_class_hook = NULL;

/*
 * Single node scenario is always a metadata coordinator
 */
bool
IsMetadataCoordinator(void)
{
	if (is_metadata_coordinator_hook != NULL)
	{
		return is_metadata_coordinator_hook();
	}

	return true;
}


/*
 * Runs a command on the cluster's metadata holding coordinator node.
 */
DistributedRunCommandResult
RunCommandOnMetadataCoordinator(const char *query)
{
	if (run_command_on_metadata_coordinator_hook != NULL)
	{
		return run_command_on_metadata_coordinator_hook(query);
	}

	ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
					errmsg("Unexpected. Should not call RunCommandOnMetadataCoordinator"
						   "When the node is a MetadataCoordinator")));
}


/*
 * Hook to run a query with commutative writes.
 * In single node all writes are commutative, so it just calls SPI directly with the args specified.
 */
Datum
RunQueryWithCommutativeWrites(const char *query, int nargs, Oid *argTypes,
							  Datum *argValues, char *argNulls,
							  int expectedSPIOK, bool *isNull)
{
	if (run_query_with_commutative_writes_hook != NULL)
	{
		return run_query_with_commutative_writes_hook(query, nargs, argTypes, argValues,
													  argNulls, expectedSPIOK, isNull);
	}

	bool readOnly = false;
	if (nargs > 0)
	{
		return ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
												   argNulls, readOnly, expectedSPIOK,
												   isNull);
	}

	return ExtensionExecuteQueryViaSPI(query, readOnly, expectedSPIOK, isNull);
}


/*
 * Sets the system to allow nested query execution.
 */
void
RunMultiValueQueryWithNestedDistribution(const char *query, int nArgs, Oid *argTypes,
										 Datum *argDatums, char *argNulls, bool readOnly,
										 int expectedSPIOK, Datum *datums, bool *isNull,
										 int numValues)
{
	if (run_query_with_nested_distribution_hook != NULL)
	{
		run_query_with_nested_distribution_hook(query, nArgs, argTypes, argDatums,
												argNulls,
												readOnly, expectedSPIOK, datums,
												isNull, numValues);
	}
	else
	{
		ExtensionExecuteMultiValueQueryWithArgsViaSPI(
			query, nArgs, argTypes, argDatums, argNulls,
			readOnly, expectedSPIOK, datums, isNull, numValues);
	}
}


/*
 * Enables any settings needed for nested distribution
 * Noops for single node.
 */
void
AllowNestedDistributionInCurrentTransaction(void)
{
	if (allow_nested_distribution_in_current_transaction_hook != NULL)
	{
		allow_nested_distribution_in_current_transaction_hook();
	}
}


/*
 * Hook to run a query with sequential shard distribution for DDLs writes.
 * In single node all writes are sequential shard distribution, so it just calls SPI directly with the args specified.
 */
Datum
RunQueryWithSequentialModification(const char *query, int expectedSPIOK, bool *isNull)
{
	if (run_query_with_sequential_modification_mode_hook != NULL)
	{
		return run_query_with_sequential_modification_mode_hook(query, expectedSPIOK,
																isNull);
	}

	bool readOnly = false;
	return ExtensionExecuteQueryViaSPI(query, readOnly, expectedSPIOK, isNull);
}


/*
 * Whether or not the the base tables have sharding with distribution (true if DistributePostgresTable
 * is run).
 * the documents table name and the substring where the collectionId was found is provided as an input.
 */
bool
IsShardTableForDocumentDbTable(const char *relName, const char *numEndPointer)
{
	if (is_shard_table_for_documentdb_table_hook != NULL)
	{
		return is_shard_table_for_documentdb_table_hook(relName, numEndPointer);
	}

	/* Without distribution all documents_ tables are shard tables */
	return true;
}


/*
 * Distributes a Postgres table across all the available node based on the
 * specified distribution column.
 */
const char *
DistributePostgresTable(const char *postgresTable, const char *distributionColumn,
						const char *colocateWith, int shardCount)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (distribute_postgres_table_hook != NULL)
	{
		return distribute_postgres_table_hook(postgresTable, distributionColumn,
											  colocateWith,
											  shardCount);
	}

	return distributionColumn;
}


/*
 * Entrypoint to modify a list of column names for queries
 * For a base RTE (table)
 */
List *
ModifyTableColumnNames(List *inputColumnNames)
{
	if (modify_table_column_names_hook != NULL)
	{
		return modify_table_column_names_hook(inputColumnNames);
	}

	return inputColumnNames;
}


/*
 * Creates a user with an external identity provider
 */
bool
CreateUserWithExternalIdentityProvider(const char *userName, char *pgRole, bson_value_t
									   customData)
{
	if (create_user_with_exernal_identity_provider_hook != NULL)
	{
		return create_user_with_exernal_identity_provider_hook(userName, pgRole,
															   customData);
	}

	return false;
}


/*
 * Drops a user with an external identity provider
 */
bool
DropUserWithExternalIdentityProvider(const char *userName)
{
	if (drop_user_with_exernal_identity_provider_hook != NULL)
	{
		return drop_user_with_exernal_identity_provider_hook(userName);
	}

	return false;
}


/*
 * Get user info from external identity provider
 */
const pgbson *
GetUserInfoFromExternalIdentityProvider(const char *userName)
{
	if (get_user_info_from_external_identity_provider_hook != NULL)
	{
		return get_user_info_from_external_identity_provider_hook(userName);
	}

	return NULL;
}


/*
 * Is user external
 */
bool
IsUserExternal(const char *userName)
{
	if (is_user_external_hook != NULL)
	{
		return is_user_external_hook(userName);
	}

	return false;
}


/*
 * Default password validation implementation, just returns true
 */
bool
IsPasswordValid(const char *username, const char *password)
{
	if (password_validation_hook != NULL)
	{
		return password_validation_hook(username, password);
	}
	return true;
}


/*
 * Default username validation implementation
 * Returns true if username is valid, false otherwise
 */
bool
IsUsernameValid(const char *username)
{
	if (username_validation_hook != NULL)
	{
		return username_validation_hook(username);
	}

	return true;
}


void
HandleColocation(MongoCollection *collection, const bson_value_t *colocationOptions)
{
	/* By default single node collections are always colocated */
	if (handle_colocation_hook != NULL)
	{
		handle_colocation_hook(collection, colocationOptions);
	}
}


Query *
MutateListCollectionsQueryForDistribution(Query *listCollectionsQuery)
{
	if (rewrite_list_collections_query_hook != NULL)
	{
		return rewrite_list_collections_query_hook(listCollectionsQuery);
	}

	return listCollectionsQuery;
}


Query *
MutateShardsQueryForDistribution(Query *shardsQuery)
{
	if (rewrite_config_shards_query_hook != NULL)
	{
		return rewrite_config_shards_query_hook(shardsQuery);
	}

	return shardsQuery;
}


Query *
MutateChunksQueryForDistribution(Query *chunksQuery)
{
	if (rewrite_config_chunks_query_hook != NULL)
	{
		return rewrite_config_chunks_query_hook(chunksQuery);
	}

	return chunksQuery;
}


const char *
TryGetShardNameForUnshardedCollection(Oid relationOid, uint64 collectionId, const
									  char *tableName)
{
	if (try_get_shard_name_for_unsharded_collection_hook != NULL)
	{
		return try_get_shard_name_for_unsharded_collection_hook(relationOid, collectionId,
																tableName);
	}

	return NULL;
}


const char *
GetDistributedApplicationName(void)
{
	if (get_distributed_application_name_hook != NULL)
	{
		return get_distributed_application_name_hook();
	}

	return NULL;
}


/*
 * Checks if Change Stream feature is available and
 * compatible with the current server version.
 */
bool
IsChangeStreamFeatureAvailableAndCompatible(void)
{
	if (is_changestream_enabled_and_compatible_hook != NULL)
	{
		return is_changestream_enabled_and_compatible_hook();
	}
	return false;
}


/*
 * Checks if server version supports ntoreturn spec.
 */
bool
IsNtoReturnSupported(void)
{
	if (is_n_to_return_supported_hook != NULL)
	{
		return is_n_to_return_supported_hook();
	}

	return true;
}


/*
 * Ensure that the given metadata table is replicated on all nodes
 * as applicable.
 * Returns true if something changed and was replicated.
 */
bool
EnsureMetadataTableReplicated(const char *tableName)
{
	if (ensure_metadata_table_replicated_hook != NULL)
	{
		return ensure_metadata_table_replicated_hook(tableName);
	}

	/* Single node default - it's always replicated */
	return false;
}


/*
 * The hook allows the extension to do any additional setup
 * after the cluster has been initialized or upgraded.
 */
void
PostSetupClusterHook(bool isInitialize, bool (shouldUpgradeFunc(void *, int, int, int)),
					 void *state)
{
	if (post_setup_cluster_hook != NULL)
	{
		return post_setup_cluster_hook(isInitialize, shouldUpgradeFunc, state);
	}
}


/*
 * Try to validate vector query spec by customized logic.
 */
void
TryCustomParseAndValidateVectorQuerySpec(const char *key,
										 const bson_value_t *value,
										 VectorSearchOptions *vectorSearchOptions)
{
	if (try_custom_parse_and_validate_vector_query_spec_hook != NULL)
	{
		try_custom_parse_and_validate_vector_query_spec_hook(key,
															 value,
															 vectorSearchOptions);
	}
}


char *
TryGetExtendedVersionRefreshQuery(void)
{
	if (try_get_extended_version_refresh_query_hook != NULL)
	{
		return try_get_extended_version_refresh_query_hook();
	}

	return NULL;
}


void
GetShardIdsAndNamesForCollection(Oid relationOid, const char *tableName,
								 Datum **shardOidArray, Datum **shardNameArray,
								 int32_t *shardCount)
{
	if (get_shard_ids_and_names_for_collection_hook != NULL)
	{
		get_shard_ids_and_names_for_collection_hook(relationOid, tableName, shardOidArray,
													shardNameArray, shardCount);
	}
	else
	{
		/* Non distributed case, is just the main table */
		*shardCount = 1;
		*shardOidArray = palloc(sizeof(Datum) * 1);
		*shardNameArray = palloc(sizeof(Datum) * 1);

		(*shardOidArray)[0] = ObjectIdGetDatum(relationOid);
		(*shardNameArray)[0] = CStringGetTextDatum(tableName);
	}
}


const char *
GetPidForIndexBuild()
{
	if (get_pid_for_index_build_hook != NULL)
	{
		return get_pid_for_index_build_hook();
	}

	return NULL;
}


const char *
TryGetIndexBuildJobOpIdQuery(void)
{
	if (try_get_index_build_job_op_id_query_hook != NULL)
	{
		return try_get_index_build_job_op_id_query_hook();
	}

	return NULL;
}


char *
TryGetCancelIndexBuildQuery(int32_t indexId, char cmdType)
{
	if (try_get_cancel_index_build_query_hook != NULL)
	{
		return try_get_cancel_index_build_query_hook(indexId, cmdType);
	}

	return NULL;
}


bool
ShouldScheduleIndexBuildJobs(void)
{
	if (should_schedule_index_builds_hook != NULL)
	{
		return should_schedule_index_builds_hook();
	}

	return true;
}


List *
GetShardIndexOids(uint64_t collectionId, int indexId, bool ignoreMissing)
{
	if (get_shard_index_oids_hook != NULL)
	{
		return get_shard_index_oids_hook(collectionId, indexId, ignoreMissing);
	}

	return NIL;
}


void
UpdatePostgresIndexWithOverride(uint64_t collectionId, int indexId, int operation, bool
								value,
								void (*default_update)(uint64_t, int, int, bool))
{
	if (update_postgres_index_hook != NULL)
	{
		update_postgres_index_hook(collectionId, indexId, operation, value);
	}
	else
	{
		default_update(collectionId, indexId, operation, value);
	}
}


const char *
GetOperationCancellationQuery(int64 shardId, StringView *opIdView, int *nargs,
							  Oid **argTypes,
							  Datum **argValues, char **argNulls,
							  const char *(*default_get_query)(int64, StringView *, int *,
															   Oid **, Datum **, char **))
{
	if (get_operation_cancellation_query_hook != NULL)
	{
		return get_operation_cancellation_query_hook(shardId, opIdView, nargs, argTypes,
													 argValues, argNulls);
	}
	else if (default_get_query == NULL)
	{
		return NULL;
	}
	return default_get_query(shardId, opIdView, nargs, argTypes, argValues, argNulls);
}


bool
ShouldUseCompositeOpClassByDefault()
{
	if (default_enable_composite_op_class_hook != NULL)
	{
		return default_enable_composite_op_class_hook();
	}

	return DefaultUseCompositeOpClass;
}
