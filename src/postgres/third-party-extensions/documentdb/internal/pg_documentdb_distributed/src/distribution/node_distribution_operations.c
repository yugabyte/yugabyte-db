/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/node_distribution_operations.c
 *
 * Implementation of scenarios that require distribution on a per node basis.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/timestamp.h>
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>

#include "utils/query_utils.h"
#include "utils/documentdb_errors.h"
#include "utils/error_utils.h"
#include "io/bson_core.h"
#include "metadata/metadata_cache.h"
#include "node_distributed_operations.h"
#include "api_hooks.h"


static ArrayType *
ChooseShardNamesForTable(const char *distributedTableName)
{
	const char *query =
		"WITH r1 AS (SELECT MIN($1 || '_' || sh.shardid) AS shardName FROM pg_dist_shard sh JOIN pg_dist_placement pl "
		" on pl.shardid = sh.shardid WHERE logicalrelid = $1::regclass GROUP by groupid) "
		" SELECT ARRAY_AGG(r1.shardName) FROM r1";

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(distributedTableName) };
	bool isReadOnly = true;
	bool isNull = true;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													   NULL, isReadOnly, SPI_OK_SELECT,
													   &isNull);

	if (isNull)
	{
		return NULL;
	}

	return DatumGetArrayTypeP(result);
}


static bool
CoordinatorHasShardsForTable(const char *distributedTableName)
{
	const char *query =
		"select COUNT(1) from citus_shards cs join pg_dist_node pd on cs.nodename = pd.nodename and cs.nodeport = pd.nodeport where cs.table_name = $1::regclass and pd.groupid = 0";

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(distributedTableName) };
	bool isReadOnly = true;
	bool isNull = true;
	Datum result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													   NULL, isReadOnly, SPI_OK_SELECT,
													   &isNull);

	if (isNull)
	{
		return false;
	}

	return DatumGetInt32(result) > 0;
}


List *
ExecutePerNodeCommand(Oid nodeFunction, pgbson *nodeFunctionArg, bool readOnly, const
					  char *distributedTableName, bool backFillCoordinator)
{
	ArrayType *chosenShards = ChooseShardNamesForTable(distributedTableName);
	if (chosenShards == NULL)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg("Failed to get shards for table"),
						errdetail_log(
							"Failed to get shard names for distributed table %s",
							distributedTableName)));
	}

	MemoryContext targetContext = CurrentMemoryContext;
	if (SPI_connect() != SPI_OK_CONNECT)
	{
		ereport(ERROR, (errmsg("could not connect to SPI manager")));
	}

	/* We build the query similar to update_worker and such where we have
	 * SELECT node_distributed_function(nodeFunction, nodeFunctionArg, 0, chosenShards, fullyQualified) FROM distributedTableName;
	 * Citus will apply distributed routing and send it to every shard. In the shard planner relpathlisthook, we'll rewrite
	 * the query to be
	 * SELECT node_distributed_function(nodeFunction, nodeFunctionArg, shardOid, chosenShards, fullyQualified);
	 *
	 * Then each shard will validate if it matches one of the chosenShards - if it does, then it runs nodeFunction,
	 * otherwise it noops.
	 * This ensures transactional processing of the command across all nodes that are hosting the shards, but each node runs
	 * the logic exactly once.
	 *
	 * We don't create an aggregate here so that we avoid any distributed planning overhead of aggregates.
	 * Allociate this string in the SPI context so it's freed on SPI_Finish().
	 */
	StringInfoData s;
	initStringInfo(&s);
	appendStringInfo(&s,
					 "SELECT %s.command_node_worker($1::oid, $2::%s.bson, 0, $3::text[], TRUE, NULL) FROM %s",
					 ApiInternalSchemaNameV2, CoreSchemaNameV2, distributedTableName);
	int nargs = 3;
	Oid argTypes[3] = { OIDOID, BsonTypeId(), TEXTARRAYOID };
	Datum argValues[3] = {
		ObjectIdGetDatum(nodeFunction),
		PointerGetDatum(nodeFunctionArg),
		PointerGetDatum(chosenShards)
	};
	char argNulls[3] = { ' ', ' ', ' ' };

	List *resultList = NIL;

	int tupleCountLimit = 0;
	if (SPI_execute_with_args(s.data, nargs, argTypes, argValues, argNulls,
							  readOnly, tupleCountLimit) != SPI_OK_SELECT)
	{
		ereport(ERROR, (errmsg("could not run SPI query")));
	}

	for (uint64 i = 0; i < SPI_processed && SPI_tuptable; i++)
	{
		AttrNumber attrNumber = 1;
		bool isNull = false;
		Datum resultDatum = SPI_getbinval(SPI_tuptable->vals[i],
										  SPI_tuptable->tupdesc, attrNumber, &isNull);
		if (isNull)
		{
			/* this shard did not process any responses*/
			continue;
		}

		pgbson *resultBson = DatumGetPgBson(resultDatum);
		MemoryContext oldContext = MemoryContextSwitchTo(targetContext);
		pgbson *copiedBson = CopyPgbsonIntoMemoryContext(resultBson, targetContext);
		resultList = lappend(resultList, copiedBson);
		MemoryContextSwitchTo(oldContext);
	}

	SPI_finish();

	/* If requested, also run on the coordinator if it doesn't have shards for the table as the command_node_worker
	 * only runs on nodes with shards for the given table. We need to ensure metadata and system catalog are consistent in the coordinator
	 * specially for management operations like add node, rebalancing, etc. */
	if (backFillCoordinator && IsMetadataCoordinator() && !CoordinatorHasShardsForTable(
			distributedTableName))
	{
		Datum result = OidFunctionCall1(nodeFunction,
										PointerGetDatum(nodeFunctionArg));
		pgbson *resultBson = DatumGetPgBson(result);
		resultList = lappend(resultList, resultBson);
	}

	return resultList;
}
