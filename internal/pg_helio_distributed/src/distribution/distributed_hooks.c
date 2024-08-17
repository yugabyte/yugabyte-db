/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/distribution/distributed_hooks.c
 *
 * Implementation of API Hooks for a distributed execution.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/builtins.h>
#include <utils/timestamp.h>
#include <utils/mongo_errors.h>
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>

#include "io/helio_bson_core.h"
#include "utils/query_utils.h"
#include "utils/guc_utils.h"
#include "utils/version_utils.h"
#include "metadata/metadata_cache.h"

#include "metadata/collection.h"
#include "api_hooks_def.h"

#include "shard_colocation.h"
#include "distributed_hooks.h"

extern bool UseLocalExecutionShardQueries;

/* Cached value for the current Global PID - can cache once
 * Since nodeId, Pid are stable.
 */
#define INVALID_CITUS_INTERNAL_BACKEND_GPID 0
static uint64 HelioCitusGlobalPid = 0;

/*
 * In Citus we query citus_is_coordinator() to get if
 * the current node is a metadata coordinator
 */
static bool
IsMetadataCoordinatorCore(void)
{
	bool readOnly = true;
	bool isNull = false;
	Datum resultBoolDatum = ExtensionExecuteQueryViaSPI(
		"SELECT citus_is_coordinator()", readOnly, SPI_OK_SELECT, &isNull);

	return !isNull && DatumGetBool(resultBoolDatum);
}


/*
 * Runs a command on the cluster's metadata holding coordinator node.
 */
static DistributedRunCommandResult
RunCommandOnMetadataCoordinatorCore(const char *query)
{
	const char *baseQuery =
		"SELECT nodeId, success, result FROM run_command_on_coordinator($1)";

	int nargs = 1;
	Oid argTypes[1] = { TEXTOID };
	Datum argValues[1] = { CStringGetTextDatum(query) };
	char argNulls[1] = { ' ' };
	bool readOnly = true;

	int numResultValues = 3;
	Datum resultDatums[3] = { 0 };
	bool resultNulls[3] = { 0 };
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		baseQuery, nargs, argTypes, argValues, argNulls, readOnly, SPI_OK_SELECT,
		resultDatums, resultNulls, numResultValues);

	DistributedRunCommandResult result = { 0 };

	/* TODO: handle error in coordinator correctly as it could be an exception we need to honor. */
	if (resultNulls[0] || resultNulls[1])
	{
		result.success = false;
		return result;
	}

	result.nodeId = DatumGetInt32(resultDatums[0]);
	result.success = DatumGetBool(resultDatums[1]);
	result.response = NULL;
	if (!resultNulls[2])
	{
		result.response = DatumGetTextP(resultDatums[2]);
	}

	return result;
}


/*
 * Hook to run a query with commutative writes.
 *
 * This sets citus.all_modifications_commutative to true, before executing the query.
 * Enabling this setting allows Citus to optimize the execution of these modifications
 * across distributed shards in parallel, potentially improving performance for certain workloads.
 *
 * This setting should be used cautiously in various queries. Currently the use cases here are around
 * modifying reference tables based on the primary key only (where we know we only update the one
 * row).
 * See https://github.com/citusdata/citus/blob/a2315fdc677675b420913ca4f81116e165d52397/src/backend/distributed/executor/distributed_execution_locks.c#L149
 * for more details.
 */
static Datum
RunQueryWithCommutativeWritesCore(const char *query, int nargs, Oid *argTypes,
								  Datum *argValues, char *argNulls,
								  int expectedSPIOK, bool *isNull)
{
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("citus.all_modifications_commutative", "true");

	Datum result;
	bool readOnly = false;
	if (nargs > 0)
	{
		result = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes, argValues,
													 argNulls, readOnly, expectedSPIOK,
													 isNull);
	}
	else
	{
		result = ExtensionExecuteQueryViaSPI(query, readOnly, expectedSPIOK, isNull);
	}

	RollbackGUCChange(savedGUCLevel);
	return result;
}


static Datum
RunQueryWithSequentialModificationCore(const char *query, int expectedSPIOK, bool *isNull)
{
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("citus.multi_shard_modify_mode", "sequential");

	bool readOnly = false;
	Datum result = ExtensionExecuteQueryViaSPI(query, readOnly, expectedSPIOK, isNull);
	RollbackGUCChange(savedGUCLevel);
	return result;
}


static bool
IsShardTableForMongoTableCore(const char *relName, const char *numEndPointer)
{
	/* It's definitely a documents query - it's a shard query if there's a documents_<num>_<num>
	 * So treat it as such if there's 2 '_'.
	 * This is a hack but need to find a better way to recognize
	 * a worker query.
	 * Note that this logic is a simpler form of the RelationIsAKnownShard
	 * function in Citus. However, that function does extract the shard_id
	 * and does a Scan on the pg_dist table as well to determine if it's really
	 * a shard. However, this is too expensive for the hotpath of every query.
	 * Consequently this simple check *should* be sufficient in the hot path.
	 *
	 * TODO: Could we do something like IsCitusTableType where we cache the results of
	 * this? Ideally we could map this to something in the Mongo Collection Cache. However
	 * the inverse lookup if it's not in the cache is not easily done in the query path.
	 */
	return numEndPointer != NULL && *numEndPointer == '_';
}


/*
 * Distributes a Postgres table across all the available node based on the
 * specified distribution column.
 *
 * returns the actual distribution column used in the table.
 */
static const char *
DistributePostgresTableCore(const char *postgresTable, const char *distributionColumn,
							const char *colocateWith, bool isUnsharded)
{
	const char *distributionColumnUsed = distributionColumn;

	/*
	 * By default, Citus triggers are off as there are potential pitfalls if
	 * not used properly, such as, doing operations on the remote node. We use
	 * them here only for local operations.
	 */
	SetGUCLocally("citus.enable_unsafe_triggers", "on");

	/*
	 * Make sure that create_distributed_table does not parallelize shard creation,
	 * since that would prevent us from pushing down an insert_one or update_one
	 * call in the same transaction. When Citus pushes down a function call, it needs
	 * to see both a distributed table and a shard, and if those are created over
	 * separate connections that is not possible until commit.
	 *
	 * Setting multi_shard_modify_mode to sequential to enforce using a single
	 * connection is a temporary workaround until this is solved in Citus.
	 * https://github.com/citusdata/citus/issues/6169
	 */
	SetGUCLocally("citus.multi_shard_modify_mode", "sequential");

	/* Because ApiDataSchema.changes is created inside initialize/complete
	 * We need to skip checking cluster version there so do other checks first.
	 */
	const char *createQuery =
		"SELECT create_distributed_table($1::regclass, $2, colocate_with => $3)";
	int nargs = 3;
	Oid argTypes[3] = { TEXTOID, TEXTOID, TEXTOID };
	Datum argValues[3] = {
		CStringGetTextDatum(postgresTable),
		(Datum) 0,
		(Datum) 0
	};

	char argNulls[3] = { ' ', 'n', 'n' };

	if (distributionColumnUsed != NULL)
	{
		argValues[1] = CStringGetTextDatum(distributionColumnUsed);
		argNulls[1] = ' ';
	}

	if (colocateWith != NULL)
	{
		argValues[2] = CStringGetTextDatum(colocateWith);
		argNulls[2] = ' ';
	}
	else
	{
		bool innerReadOnly = true;
		bool isNull = true;
		ExtensionExecuteQueryViaSPI(
			"SELECT 1 FROM pg_catalog.pg_dist_partition "
			" WHERE logicalrelid = 'mongo_data.changes'::regclass",
			innerReadOnly, SPI_OK_SELECT, &isNull);
		if (!isNull)
		{
			argValues[2] = CStringGetTextDatum("mongo_data.changes");
			argNulls[2] = ' ';
		}
		else
		{
			/* If ApiDataSchema.changes doesn't exist - fall back into "none" */
			argValues[2] = CStringGetTextDatum("none");
			argNulls[2] = ' ';
		}
	}

	bool readOnly = false;
	bool isNull = false;
	ExtensionExecuteQueryWithArgsViaSPI(createQuery, nargs,
										argTypes, argValues, argNulls, readOnly,
										SPI_OK_SELECT, &isNull);

	return distributionColumnUsed;
}


/*
 * Allows nested distributed execution in the current query for citus.
 */
static void
RunMultiValueQueryWithNestedDistributionCore(const char *query, int nArgs, Oid *argTypes,
											 Datum *argDatums, char *argNulls, bool
											 readOnly,
											 int expectedSPIOK, Datum *datums,
											 bool *isNull, int numValues)
{
	int gucLevel = NewGUCNestLevel();
	SetGUCLocally("citus.allow_nested_distributed_execution", "true");
	ExtensionExecuteMultiValueQueryWithArgsViaSPI(query, nArgs, argTypes, argDatums,
												  argNulls, readOnly,
												  expectedSPIOK, datums, isNull,
												  numValues);
	RollbackGUCChange(gucLevel);
}


/*
 * Given a relationId and a collectionId for the relation, tries to get
 * the shard tableName for the table if it is an unsharded table with a single shard.
 * e.g. for documents_1 returns documents_1_102011.
 */
static const char *
TryGetShardNameForUnshardedCollectionCore(Oid relationId, uint64 collectionId, const
										  char *tableName)
{
	if (!UseLocalExecutionShardQueries)
	{
		/* Defensive - only turn this on with feature flag */
		return NULL;
	}

	const char *shardIdDetailsQuery =
		"SELECT shardid, shardminvalue, shardmaxvalue FROM pg_dist_shard WHERE logicalrelid = $1 LIMIT 1";

	Oid shardCountArgTypes[1] = { OIDOID };
	Datum shardCountArgValues[1] = { ObjectIdGetDatum(relationId) };
	char *argNullNone = NULL;
	bool readOnly = true;

	int numValues = 3;
	Datum resultDatums[3] = { 0 };
	bool resultNulls[3] = { 0 };

	ExtensionExecuteMultiValueQueryWithArgsViaSPI(
		shardIdDetailsQuery, 1, shardCountArgTypes, shardCountArgValues, argNullNone,
		readOnly, SPI_OK_SELECT, resultDatums, resultNulls, numValues);
	if (resultNulls[0])
	{
		/* Not a distributed table */
		return NULL;
	}

	int64_t shardIdValue = DatumGetInt64(resultDatums[0]);

	/* Only support this for single shard distributed
	 * This is only true if shardminvalue  and shardmaxvalue are NULL
	 */
	if (!resultNulls[1] || !resultNulls[2])
	{
		/* has at least some shard values */
		return NULL;
	}

	/* Construct the shard table name */
	char *shardTableName = psprintf("%s_%ld", tableName, shardIdValue);

	/* Now that we have a shard table name, try to find it in pg_class without locking it */
	Oid shardTableOid = get_relname_relid(shardTableName, ApiDataNamespaceOid());
	if (shardTableOid != InvalidOid)
	{
		return shardTableName;
	}
	else
	{
		return NULL;
	}
}


/*
 * Gets distributed application for citus based applications.
 */
static const char *
GetDistributedApplicationNameCore(void)
{
	if (HelioCitusGlobalPid == INVALID_CITUS_INTERNAL_BACKEND_GPID)
	{
		bool isNull;
		Datum result = ExtensionExecuteQueryViaSPI(
			"SELECT pg_catalog.citus_backend_gpid()", true, SPI_OK_SELECT, &isNull);

		if (isNull)
		{
			return NULL;
		}

		HelioCitusGlobalPid = DatumGetUInt64(result);

		if (HelioCitusGlobalPid == INVALID_CITUS_INTERNAL_BACKEND_GPID)
		{
			return NULL;
		}
	}

	/*
	 * Match the application name pattern for the citus run_command* internal backend
	 * so these don't count in the quota for max_client_backends for citus.
	 */
	return psprintf("citus_run_command gpid=%lu HelioDBInternal", HelioCitusGlobalPid);
}


/*
 * Register hook overrides for Helio.
 */
void
InitializeHelioDistributedHooks(void)
{
	is_metadata_coordinator_hook = IsMetadataCoordinatorCore;
	run_command_on_metadata_coordinator_hook = RunCommandOnMetadataCoordinatorCore;
	run_query_with_commutative_writes_hook = RunQueryWithCommutativeWritesCore;
	run_query_with_sequential_modification_mode_hook =
		RunQueryWithSequentialModificationCore;
	distribute_postgres_table_hook = DistributePostgresTableCore;
	run_query_with_nested_distribution_hook =
		RunMultiValueQueryWithNestedDistributionCore;
	is_shard_table_for_mongo_table_hook = IsShardTableForMongoTableCore;
	try_get_shard_name_for_unsharded_collection_hook =
		TryGetShardNameForUnshardedCollectionCore;
	get_distributed_application_name_hook = GetDistributedApplicationNameCore;
	UpdateColocationHooks();
}
