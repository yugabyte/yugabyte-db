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
#include <nodes/makefuncs.h>
#include <catalog/namespace.h>
#include <utils/lsyscache.h>
#include <utils/memutils.h>

#include "io/bson_core.h"
#include "utils/query_utils.h"
#include "utils/guc_utils.h"
#include "utils/version_utils.h"
#include "metadata/metadata_cache.h"
#include "utils/documentdb_errors.h"

#include "metadata/collection.h"
#include "api_hooks_def.h"

#include "shard_colocation.h"
#include "distributed_hooks.h"

extern bool UseLocalExecutionShardQueries;
extern char *ApiDistributedSchemaName;

extern bool EnableMetadataReferenceTableSync;
extern char *DistributedOperationsQuery;

/* Cached value for the current Global PID - can cache once
 * Since nodeId, Pid are stable.
 */
#define INVALID_CITUS_INTERNAL_BACKEND_GPID 0
static uint64 DocumentDBCitusGlobalPid = 0;

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
							const char *colocateWith, int shardCount)
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
		"SELECT create_distributed_table($1::regclass, $2, colocate_with => $3, shard_count => $4)";
	int nargs = 4;
	Oid argTypes[4] = { TEXTOID, TEXTOID, TEXTOID, INT4OID };
	Datum argValues[4] = {
		CStringGetTextDatum(postgresTable),
		(Datum) 0,
		(Datum) 0,
		(Datum) 0,
	};

	if (distributionColumnUsed == NULL && shardCount != 0)
	{
		ereport(ERROR, (errcode(ERRCODE_DOCUMENTDB_INTERNALERROR),
						errmsg(
							"Unexpected - distribution column is null but shardCount is %d",
							shardCount),
						errdetail_log(
							"Unexpected - distribution column is null but shardCount is %d",
							shardCount)));
	}

	char argNulls[4] = { ' ', 'n', 'n', 'n' };

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
			FormatSqlQuery("SELECT 1 FROM pg_catalog.pg_dist_partition pdp "
						   " JOIN pg_class pc on pdp.logicalrelid = pc.oid "
						   " WHERE relname = 'changes' AND relnamespace = '%s'::regnamespace",
						   ApiDataSchemaName),
			innerReadOnly, SPI_OK_SELECT, &isNull);
		if (!isNull)
		{
			char changesTableName[NAMEDATALEN] = { 0 };
			sprintf(changesTableName, "%s.changes", ApiDataSchemaName);
			argValues[2] = CStringGetTextDatum(changesTableName);
			argNulls[2] = ' ';
		}
		else
		{
			/* If ApiDataSchema.changes doesn't exist - fall back into "none" */
			argValues[2] = CStringGetTextDatum("none");
			argNulls[2] = ' ';
		}
	}

	if (shardCount > 0)
	{
		argValues[3] = Int32GetDatum(shardCount);
		argNulls[3] = ' ';
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
 * If shards are unavailable returns NULL - can be retried.
 * If the shard is remote and not loca - returns ""
 */
static const char *
TryGetShardNameForUnshardedCollectionCore(Oid relationId, uint64 collectionId, const
										  char *tableName)
{
	if (!UseLocalExecutionShardQueries)
	{
		/* Defensive - only turn this on with feature flag */
		return "";
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
		return "";
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
		return "";
	}
}


/*
 * Gets distributed application for citus based applications.
 */
static const char *
GetDistributedApplicationNameCore(void)
{
	if (DocumentDBCitusGlobalPid == INVALID_CITUS_INTERNAL_BACKEND_GPID)
	{
		bool isNull;
		Datum result = ExtensionExecuteQueryViaSPI(
			"SELECT pg_catalog.citus_backend_gpid()", true, SPI_OK_SELECT, &isNull);

		if (isNull)
		{
			return NULL;
		}

		DocumentDBCitusGlobalPid = DatumGetUInt64(result);

		if (DocumentDBCitusGlobalPid == INVALID_CITUS_INTERNAL_BACKEND_GPID)
		{
			return NULL;
		}
	}

	/*
	 * Match the application name pattern for the citus run_command* internal backend
	 * so these don't count in the quota for max_client_backends for citus.
	 */
	return psprintf("citus_run_command gpid=%lu %s",
					DocumentDBCitusGlobalPid, GetExtensionApplicationName());
}


static bool
ExecuteMetadataChecksForReferenceTables(const char *tableName)
{
	/* First get the shard_id for the table */
	StringInfo queryStringInfo = makeStringInfo();
	appendStringInfo(queryStringInfo,
					 "SELECT shardid FROM pg_catalog.pg_dist_shard WHERE logicalrelid = '%s.%s'::regclass",
					 ApiCatalogSchemaName, tableName);

	bool isNull = false;
	Datum result = ExtensionExecuteQueryViaSPI(queryStringInfo->data, false,
											   SPI_OK_SELECT, &isNull);

	if (isNull)
	{
		return false;
	}

	int64 shardId = DatumGetInt64(result);

	/* Get the number of nodes for the primary group */
	result = ExtensionExecuteQueryViaSPI(
		"SELECT COUNT(*)::int4 FROM pg_catalog.pg_dist_node WHERE isactive AND noderole = 'primary'",
		false, SPI_OK_SELECT, &isNull);
	if (isNull)
	{
		return false;
	}

	int numNodes = DatumGetInt32(result);

	resetStringInfo(queryStringInfo);
	appendStringInfo(queryStringInfo,
					 "SELECT COUNT(*)::int4 FROM pg_catalog.pg_dist_placement WHERE shardid = %ld",
					 shardId);
	result = ExtensionExecuteQueryViaSPI(queryStringInfo->data, false, SPI_OK_SELECT,
										 &isNull);
	if (isNull)
	{
		return false;
	}

	int numPlacements = DatumGetInt32(result);

	if (numPlacements != numNodes)
	{
		/* There was an add node but the metadata table needed wasn't replicated: Call replicate_reference_tables first */
		ExtensionExecuteQueryOnLocalhostViaLibPQ(
			"SELECT pg_catalog.replicate_reference_tables('block_writes')");
		return true;
	}
	else
	{
		return false;
	}
}


static bool
EnsureMetadataTableReplicatedCore(const char *tableName)
{
	if (!EnableMetadataReferenceTableSync)
	{
		return false;
	}

	/* Set min messagees to reduce log spam in tests */
	int savedGUCLevel = NewGUCNestLevel();
	SetGUCLocally("client_min_messages", "WARNING");
	bool result = ExecuteMetadataChecksForReferenceTables(tableName);
	RollbackGUCChange(savedGUCLevel);
	return result;
}


static char *
TryGetExtendedVersionRefreshQueryCore(void)
{
	/* Update the version check query to consider distributed versions */
	MemoryContext currContext = MemoryContextSwitchTo(TopMemoryContext);
	StringInfo s = makeStringInfo();
	appendStringInfo(s,
					 "SELECT regexp_split_to_array(TRIM(%s.bson_get_value_text(metadata, 'last_deploy_version'), '\"'), '[-\\.]')::int4[] FROM %s.%s_cluster_data",
					 CoreSchemaName, ApiDistributedSchemaName, ExtensionObjectPrefix);
	MemoryContextSwitchTo(currContext);

	elog(LOG, "Version refresh query is %s", s->data);

	return s->data;
}


static void
GetShardIdsAndNamesForCollectionCore(Oid relationOid, const char *tableName,
									 Datum **shardOidArray, Datum **shardNameArray,
									 int32_t *shardCount)
{
	*shardOidArray = NULL;
	*shardNameArray = NULL;
	*shardCount = 0;
	const char *query =
		"SELECT array_agg($2 || '_' || shardid) FROM pg_dist_shard WHERE logicalrelid = $1";

	int nargs = 2;
	Oid argTypes[2] = { OIDOID, TEXTOID };
	Datum argValues[2] = {
		ObjectIdGetDatum(relationOid), CStringGetTextDatum(tableName)
	};
	bool isReadOnly = true;
	bool isNull = true;
	Datum shardIds = ExtensionExecuteQueryWithArgsViaSPI(query, nargs, argTypes,
														 argValues,
														 NULL, isReadOnly, SPI_OK_SELECT,
														 &isNull);

	if (isNull)
	{
		return;
	}

	ArrayType *arrayType = DatumGetArrayTypeP(shardIds);

	/* Need to build the result */
	int numItems = ArrayGetNItems(ARR_NDIM(arrayType), ARR_DIMS(arrayType));
	Datum *resultDatums = palloc0(sizeof(Datum) * numItems);
	Datum *resultNameDatums = palloc0(sizeof(Datum) * numItems);
	int resultCount = 0;

	const int slice_ndim = 0;
	ArrayMetaState *mState = NULL;
	ArrayIterator shardIterator = array_create_iterator(arrayType,
														slice_ndim, mState);

	Datum shardName = 0;
	while (array_iterate(shardIterator, &shardName, &isNull))
	{
		if (isNull)
		{
			continue;
		}

		RangeVar *rangeVar = makeRangeVar(ApiDataSchemaName, TextDatumGetCString(
											  shardName), -1);
		bool missingOk = true;
		Oid shardRelationId = RangeVarGetRelid(rangeVar, AccessShareLock, missingOk);
		if (shardRelationId != InvalidOid)
		{
			Assert(resultCount < numItems);
			resultDatums[resultCount] = shardRelationId;
			resultNameDatums[resultCount] = PointerGetDatum(DatumGetTextPCopy(
																shardName));
			resultCount++;
		}
	}

	array_free_iterator(shardIterator);

	/* Now that we have the shard list as a Datum*, create an array type */
	if (resultCount > 0)
	{
		*shardOidArray = resultDatums;
		*shardNameArray = resultNameDatums;
		*shardCount = resultCount;
	}
	else
	{
		pfree(resultDatums);
		pfree(resultNameDatums);
	}

	pfree(arrayType);
}


/*
 * Register hook overrides for DocumentDB.
 */
void
InitializeDocumentDBDistributedHooks(void)
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
	ensure_metadata_table_replicated_hook = EnsureMetadataTableReplicatedCore;
	DefaultInlineWriteOperations = false;
	ShouldUpgradeDataTables = false;
	UpdateColocationHooks();

	try_get_extended_version_refresh_query_hook = TryGetExtendedVersionRefreshQueryCore;
	get_shard_ids_and_names_for_collection_hook = GetShardIdsAndNamesForCollectionCore;

	DistributedOperationsQuery =
		"SELECT * FROM pg_stat_activity LEFT JOIN pg_catalog.get_all_active_transactions() ON process_id = pid";
}
