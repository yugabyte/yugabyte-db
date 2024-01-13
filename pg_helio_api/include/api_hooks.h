/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/api_hooks.h
 *
 * Exports related to hooks for the public API surface that enable distribution.
 *
 *-------------------------------------------------------------------------
 */

#ifndef EXTENSION_API_HOOKS_H
#define EXTENSION_API_HOOKS_H

#include "api_hooks_common.h"

/* Section: General Extension points */


/*
 * Returns true if the current Postgres server is a Query Coordinator
 * that also owns the metadata management of schema (DDL).
 */
bool IsMetadataCoordinator(void);


/*
 * Runs a command on the MetadataCoordinator if the current node is not a
 * Metadata Coordinator. The response is returned as a "record" struct
 * with the nodeId responding, whether or not the command succeeded and
 * the response datum serialized as a string.
 * If success, then this is the response datum in text format.
 * If failed, then this contains the error string from the failure.
 */
DistributedRunCommandResult RunCommandOnMetadataCoordinator(const char *query);

/*
 * Runs a query via SPI with commutative writes on for distributed scenarios.
 * Returns the Datum returned by the executed query.
 */
Datum RunQueryWithCommutativeWrites(const char *query, int nargs, Oid *argTypes,
									Datum *argValues, char *argNulls,
									int expectedSPIOK, bool *isNull);


/*
 * Sets up the system to allow nested distributed query execution for the current
 * transaction scope.
 * Note: This should be used very cautiously in any place where data correctness is
 * required.
 */
void RunMultiValueQueryWithNestedDistribution(const char *query, bool readOnly, int
											  expectedSPIOK,
											  Datum *datums, bool *isNull, int numValues);


/* Section: Create Table Extension points */

/*
 * Distributes a given postgres table with the provided distribution column.
 * Optionally supports colocating the distributed table with another distributed table.
 */
void DistributePostgresTable(const char *postgresTable, const char *distributionColumn,
							 const char *colocateWith);

/*
 * Given a current table schema built up to create a postgres table, adds a hook to
 * modify the schema if needed.
 */
void ModifyCreateTableSchema(StringInfo currentSchema, const char *tableName);


/*
 * Handle any post actions after the table is created
 */
void PostProcessCreateTable(const char *postgresTable, uint64_t collectionId,
							text *databaseName, text *collectionName);

/*
 * Handle any post actions after the table is sharded
 */
void PostProcessShardCollection(const char *tableName, uint64_t collectionId,
								text *databaseName, text *collectionName,
								pgbson *shardKey);

/*
 * Handle any post actions after the collection is dropped
 */
void PostProcessCollectionDrop(uint64_t collectionId, text *databaseName,
							   text *collectionName, bool trackChanges);

/*
 * Entrypoint to modify a list of column names for queries
 * For a base RTE (table)
 */
List * ModifyTableColumnNames(List *tableColumns);


/*
 * Private: Feature flag for update tracking.
 */
bool IsUpdateTrackingEnabled(uint64 collectionId,
							 const char **updateTrackingColumn,
							 const char **updateTrackingCommand);

#endif
