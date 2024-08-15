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

#include "metadata/collection.h"
#include "io/helio_bson_core.h"
#include "lib/stringinfo.h"
#include "api_hooks.h"
#include "api_hooks_def.h"
#include "utils/query_utils.h"
#include "utils/mongo_errors.h"


IsMetadataCoordinator_HookType is_metadata_coordinator_hook = NULL;
RunCommandOnMetadataCoordinator_HookType run_command_on_metadata_coordinator_hook = NULL;
RunQueryWithCommutativeWrites_HookType run_query_with_commutative_writes_hook = NULL;
RunQueryWithSequentialModification_HookType
	run_query_with_sequential_modification_mode_hook = NULL;
DistributePostgresTable_HookType distribute_postgres_table_hook = NULL;
ModifyTableColumnNames_HookType modify_table_column_names_hook = NULL;
RunQueryWithNestedDistribution_HookType run_query_with_nested_distribution_hook = NULL;
IsShardTableForMongoTable_HookType is_shard_table_for_mongo_table_hook = NULL;
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

	ereport(ERROR, (errcode(MongoInternalError),
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
 * Whether or not the the base tables have sharding with distribution (true if DistributePostgreTable
 * is run).
 * the documents table name and the substring where the collectionId was found is provided as an input.
 */
bool
IsShardTableForMongoTable(const char *relName, const char *numEndPointer)
{
	if (is_shard_table_for_mongo_table_hook != NULL)
	{
		return is_shard_table_for_mongo_table_hook(relName, numEndPointer);
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
						const char *colocateWith, bool isUnsharded)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (distribute_postgres_table_hook != NULL)
	{
		return distribute_postgres_table_hook(postgresTable, distributionColumn,
											  colocateWith,
											  isUnsharded);
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
