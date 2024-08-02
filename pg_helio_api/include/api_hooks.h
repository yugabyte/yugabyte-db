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
#include "metadata/collection.h"

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
void RunMultiValueQueryWithNestedDistribution(const char *query, int nargs, Oid *argTypes,
											  Datum *argValues, char *argNulls, bool
											  readOnly,
											  int expectedSPIOK, Datum *datums,
											  bool *isNull, int numValues);


/*
 * Sets up the system to allow sequential execution for commands the current
 * transaction scope.
 * Note: This should be used for DDL commands.
 */
Datum RunQueryWithSequentialModification(const char *query, int expectedSPIOK,
										 bool *isNull);

/*
 * Whether or not the the base tables have sharding with distribution (true if DistributePostgreTable
 * is run).
 * the documents table name and the substring where the collectionId was found is provided as an input.
 */
bool IsShardTableForMongoTable(const char *relName, const char *numEndPointer);


/* Section: Create Table Extension points */

/*
 * Distributes a given postgres table with the provided distribution column.
 * Optionally supports colocating the distributed table with another distributed table.
 * Returns the distribution column used (may be equal to the one passed on or NULL).
 */
const char * DistributePostgresTable(const char *postgresTable, const
									 char *distributionColumn,
									 const char *colocateWith, bool isUnsharded);


/*
 * Entrypoint to modify a list of column names for queries
 * For a base RTE (table)
 */
List * ModifyTableColumnNames(List *tableColumns);

/*
 * Hook for handling colocation of tables
 */
void HandleColocation(MongoCollection *collection, const bson_value_t *colocationOptions);


/*
 * Mutate's listCollections query generation for distribution data.
 * This is an optional hook and can manage listCollection to update shardCount
 * and colocation information as required. Noops for single node.
 */
Query * MutateListCollectionsQueryForDistribution(Query *cosmosMetadataQuery);


/*
 * Mutates the shards query for handling distributed scenario.
 */
Query * MutateShardsQueryForDistribution(Query *metadataQuery);


/*
 * Mutates the chunks query for handling distributed scenario.
 */
Query * MutateChunksQueryForDistribution(Query *cosmosMetadataQuery);


/*
 * Given a table OID, if the table is not the actual physical shard holding the data (say in a
 * distributed setup), tries to return the full shard name of the actual table if it can be found locally
 * or NULL otherwise (e.g. for ApiDataSchema.documents_1 returns ApiDataSchema.documents_1_12341 or NULL)
 */
const char * TryGetShardNameForUnshardedCollection(Oid relationOid, uint64 collectionId,
												   const char *tableName);

const char * GetDistributedApplicationName(void);


/*
 * This checks whether the current server version supports ntoreturn spec.
 */
bool IsNtoReturnSupported(void);


/*
 * Returns if the change stream feature is enabled.
 */
bool IsChangeStreamFeatureAvailableAndCompatible(void);
#endif
