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

#include "io/bson_core.h"
#include "lib/stringinfo.h"
#include "api_hooks.h"
#include "api_hooks_def.h"
#include "utils/query_utils.h"
#include "utils/mongo_errors.h"


IsMetadataCoordinator_HookType is_metadata_coordinator_hook = NULL;
RunCommandOnMetadataCoordinator_HookType run_command_on_metadata_coordinator_hook = NULL;
RunQueryWithCommutativeWrites_HookType run_query_with_commutative_writes_hook = NULL;
DistributePostgresTable_HookType distribute_postgres_table_hook = NULL;
ModifyCreateTableSchema_HookType modify_create_table_schema_hook = NULL;
PostProcessCreateTable_HookType post_process_create_table_hook = NULL;
PostProcessShardCollection_HookType post_process_shard_collection_hook = NULL;
PostProcessCollectionDrop_HookType post_process_drop_collection_hook = NULL;
ModifyTableColumnNames_HookType modify_table_column_names_hook = NULL;
RunQueryWithNestedDistribution_HookType run_query_with_nested_distribution_hook = NULL;

IsUpdateTrackingEnabled_HookType is_update_tracking_enabled_hook = NULL;


/* Internal hook for update tracking */
bool
IsUpdateTrackingEnabled(uint64 collection, const char **updateColumn,
						const char **updateValue)
{
	if (is_update_tracking_enabled_hook != NULL)
	{
		return is_update_tracking_enabled_hook(collection, updateColumn, updateValue);
	}

	return false;
}


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
RunMultiValueQueryWithNestedDistribution(const char *query, bool readOnly, int
										 expectedSPIOK,
										 Datum *datums, bool *isNull, int numValues)
{
	if (run_query_with_nested_distribution_hook != NULL)
	{
		run_query_with_nested_distribution_hook(query, readOnly, expectedSPIOK, datums,
												isNull, numValues);
	}
	else
	{
		ExtensionExecuteMultiValueQueryViaSPI(query, readOnly, expectedSPIOK, datums,
											  isNull, numValues);
	}
}


/*
 * Distributes a Postgres table across all the available node based on the
 * specified distribution column.
 */
void
DistributePostgresTable(const char *postgresTable, const char *distributionColumn,
						const char *colocateWith)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (distribute_postgres_table_hook != NULL)
	{
		distribute_postgres_table_hook(postgresTable, distributionColumn, colocateWith);
	}
}


/*
 * Hook for modifying table schema as necessary.
 * This is Noop for the Single node case.
 *
 */
void
ModifyCreateTableSchema(StringInfo schema, const char *tableName)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (modify_create_table_schema_hook != NULL)
	{
		modify_create_table_schema_hook(schema, tableName);
	}
}


/*
 * Handles any post processing for the table.
 * Noop for a single node scenario
 *
 */
void
PostProcessCreateTable(const char *tableName, uint64_t collectionId,
					   text *databaseName, text *collectionName)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (post_process_create_table_hook != NULL)
	{
		post_process_create_table_hook(tableName, collectionId, databaseName,
									   collectionName);
	}
}


/*
 * Handles any post processing for the table that is being sharded.
 * Noop for a single node scenario
 *
 */
void
PostProcessShardCollection(const char *tableName, uint64_t collectionId,
						   text *databaseName, text *collectionName, pgbson *shardKey)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (post_process_shard_collection_hook != NULL)
	{
		post_process_shard_collection_hook(tableName, collectionId, databaseName,
										   collectionName, shardKey);
	}
}


/*
 * Handles any post processing for a collection being dropped.
 * Noop for a single node scenario
 *
 */
void
PostProcessCollectionDrop(uint64_t collectionId, text *databaseName,
						  text *collectionName, bool trackChanges)
{
	/* Noop for single node scenarios: Don't do anything unless overriden */
	if (post_process_drop_collection_hook != NULL)
	{
		post_process_drop_collection_hook(collectionId, databaseName,
										  collectionName, trackChanges);
	}
}


/*
 * Entrypoint to modify a list of column names for queries
 * For a base RTE (table)
 */
List *
ModifyTableColumnNames(List *inputColumnNames)
{
	if (modify_create_table_schema_hook != NULL)
	{
		return modify_table_column_names_hook(inputColumnNames);
	}

	return inputColumnNames;
}
