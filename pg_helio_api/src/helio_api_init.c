/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/bson_init.c
 *
 * Initialization of the shared library initialization for bson.
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <limits.h>
#include <access/xact.h>

#include "helio_api_init.h"
#include "metadata/metadata_guc.h"
#include "planner/helio_planner.h"
#include "customscan/custom_scan_registrations.h"
#include "commands/connection_management.h"

/* --------------------------------------------------------- */
/* Data Types & Enum values */
/* --------------------------------------------------------- */

/*
 * Possible read concern levels
 * Do not have an enum for write concern level
 * as it depends on number of nodes in the cluster
 */
typedef enum ReadConcernLevel
{
	ReadConcernLevel_Undefined = 0,
	ReadConcernLevel_LOCAL = 1,
	ReadConcernLevel_AVAILABLE = 2,
	ReadConcernLevel_MAJORITY = 3,
	ReadConcernLevel_LINEARIZABLE = 4,
	ReadConcernLevel_SNAPSHOT = 5
} ReadConcernLevel;

static const struct config_enum_entry READ_CONCERN_LEVEL_CONFIG_ENTRIES[] =
{
	{ "Undefined", ReadConcernLevel_Undefined, true },
	{ "local", ReadConcernLevel_LOCAL, true },
	{ "available", ReadConcernLevel_AVAILABLE, true },
	{ "majority", ReadConcernLevel_MAJORITY, false },
	{ "linearizable", ReadConcernLevel_LINEARIZABLE, true },
	{ "snapshot", ReadConcernLevel_SNAPSHOT, true },
	{ NULL, 0, false },
};

/*
 * Externally defined GUC constants
 * TODO(OSS): Move these as appropriate.
 */
#define BSON_MAX_ALLOWED_SIZE (16 * 1024 * 1024)

/*
 * helio_api.enable_create_collection_on_insert GUC determines whether
 * an insert into a non-existent collection should create a collection.
 */
extern bool EnableCreateCollectionOnInsert;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


/* callbacks for transaction management */
static void HelioTransactionCallback(XactEvent event, void *arg);
static void HelioSubTransactionCallback(SubXactEvent event, SubTransactionId mySubid,
										SubTransactionId parentSubid, void *arg);

/* --------------------------------------------------------- */
/* GUCs and default values */
/* --------------------------------------------------------- */

#define DEFAULT_QUERY_PLAN_CACHE_SIZE_LIMIT 100
int QueryPlanCacheSizeLimit = DEFAULT_QUERY_PLAN_CACHE_SIZE_LIMIT;

#define DEFAULT_LOCALHOST_CONN_STR "host=localhost"
char *LocalhostConnectionString = DEFAULT_LOCALHOST_CONN_STR;

#define DEFAULT_NEXT_COLLECTION_ID NEXT_COLLECTION_ID_UNSET
int NextCollectionId = DEFAULT_NEXT_COLLECTION_ID;

#define DEFAULT_NEXT_COLLECTION_INDEX_ID NEXT_COLLECTION_INDEX_ID_UNSET
int NextCollectionIndexId = DEFAULT_NEXT_COLLECTION_INDEX_ID;

/* TODO: Raise this back to 100,000 once we can optimize sub-transaction */
/* handling with multi-node clusters. Bug: https://msdata.visualstudio.com/CosmosDB/_workitems/edit/2355443 */
#define DEFAULT_MAX_WRITE_BATCH_SIZE 25000
int MaxWriteBatchSize = DEFAULT_MAX_WRITE_BATCH_SIZE;

#define DEFAULT_MAX_TTL_DELETE_BATCH_SIZE 10000
int MaxTTLDeleteBatchSize = DEFAULT_MAX_TTL_DELETE_BATCH_SIZE;

#define DEFAULT_TTL_PURGER_STATEMENT_TIMEOUT 60000
int TTLPurgerStatementTimeout = DEFAULT_TTL_PURGER_STATEMENT_TIMEOUT;

#define DEFAULT_TTL_PURGER_LOCK_TIMEOUT 10000
int TTLPurgerLockTimeout = DEFAULT_TTL_PURGER_LOCK_TIMEOUT;

#define DEFAULT_SINGLE_TTL_TASK_TIME_BUDGET 20000
int SingleTTLTaskTimeBudget = DEFAULT_SINGLE_TTL_TASK_TIME_BUDGET;

#define DEFAULT_LOG_TTL_PROGRESS_ACTIVITY false
bool LogTTLProgressActivity = DEFAULT_LOG_TTL_PROGRESS_ACTIVITY;

#define DEFAULT_FORCE_RUM_INDEXSCAN_TO_BITMAPHEAPSCAN true
bool ForceRUMIndexScanToBitmapHeapScan = DEFAULT_FORCE_RUM_INDEXSCAN_TO_BITMAPHEAPSCAN;

/* GUCs to enable features that we don't normally allow using for Mongo compatibility */
#define DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS false
bool EnableExtendedIndexFilters = DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS;

#define DEFAULT_ENABLE_DEVELOPER_EXPLAIN false
bool EnableDeveloperExplain = DEFAULT_ENABLE_DEVELOPER_EXPLAIN;

/* Setting this to true until we have statistics. When dealing with large number of records sequential
 * scan can win even if there is an index to be used, because index cost being not reflected properly.
 * Avoiding that case is our priority. Using index when dealing with small number of records might be worse
 * than using sequential scan, but we are okay with that case as the latency hit would be very small.
 * This affects all of our queries.*/
#define DEFAULT_FORCE_USE_INDEX_IF_AVAILABLE true
bool ForceUseIndexIfAvailable = DEFAULT_FORCE_USE_INDEX_IF_AVAILABLE;

#define DEFAULT_SIMULATE_RECOVERY_STATE false
bool SimulateRecoveryState = DEFAULT_SIMULATE_RECOVERY_STATE;

#define DEFAULT_MAX_WORKER_CURSOR_SIZE BSON_MAX_ALLOWED_SIZE
int32_t MaxWorkerCursorSize = DEFAULT_MAX_WORKER_CURSOR_SIZE;

#define DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT 500
int BatchWriteSubTransactionCount = DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT;

#define DEFAULT_ENABLE_GEOSPATIAL false
bool EnableGeospatialSupport = DEFAULT_ENABLE_GEOSPATIAL;

/*
 * GUC to enable HNSW index type and query for vector search.
 * This is disabled by default.
 */
#define DEFAULT_ENABLE_VECTOR_HNSW_INDEX false
bool EnableVectorHNSWIndex = DEFAULT_ENABLE_VECTOR_HNSW_INDEX;

/*
 * GUC to enable vector pre-filtering feature for vector search.
 * This is disabled by default.
 */
#define DEFAULT_ENABLE_VECTOR_PRE_FILTER false
bool EnableVectorPreFilter = DEFAULT_ENABLE_VECTOR_PRE_FILTER;

#define DEFAULT_MAX_NUM_ACTIVE_USERS_INDEX_BUILDS 2
int MaxNumActiveUsersIndexBuilds = DEFAULT_MAX_NUM_ACTIVE_USERS_INDEX_BUILDS;

#define DEFAULT_HELIO_PG_READ_ONLY_FOR_DISK_FULL false
bool HelioPGReadOnlyForDiskFull = DEFAULT_HELIO_PG_READ_ONLY_FOR_DISK_FULL;

/* GUCs for cluster-wide read-write concern */
#define DEFAULT_READ_CONCERN_LEVEL ReadConcernLevel_MAJORITY
int DefaultReadConcernLevel = DEFAULT_READ_CONCERN_LEVEL;

#define DEFAULT_WRITE_CONCERN_LEVEL "majority"
char *DefaultWriteConcernLevel = DEFAULT_WRITE_CONCERN_LEVEL;

/*
 * GUC for "Count Policy" change Threshold for collStats DB command
 * If the document count from stats is more than this threshold, the count policy remains "get count from stats".
 * If the document count from stats is less than this threshold, the count policy becomes "get count at runtime".
 */
#define DEFAULT_COLL_STATS_COUNT_POLICY_THRESHOLD 10000
int CollStatsCountPolicyThreshold = DEFAULT_COLL_STATS_COUNT_POLICY_THRESHOLD;

#define DEFAULT_MAX_INDEX_BUILD_ATTEMPTS 3
int MaxIndexBuildAttempts = DEFAULT_MAX_INDEX_BUILD_ATTEMPTS;

#define DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC 60
int32_t IndexBuildScheduleInSec = DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC;

#define DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC 1200
int IndexQueueEvictionIntervalInSec = DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC;

#define DEFAULT_ENABLE_INDEX_TERM_TRUNCATION false
bool EnableIndexTermTruncation = DEFAULT_ENABLE_INDEX_TERM_TRUNCATION;


#define DEFAULT_FORCE_INDEX_TERM_TRUNCATION false
bool ForceIndexTermTruncation = DEFAULT_FORCE_INDEX_TERM_TRUNCATION;

#define DEFAULT_INDEX_TRUNCATION_LIMIT_OVERRIDE INT_MAX
int IndexTruncationLimitOverride = DEFAULT_INDEX_TRUNCATION_LIMIT_OVERRIDE;

#define DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM 500
double MaxSegmentLengthInKms = DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM;

#define DEFAULT_MAX_SEGMENT_VERTICES 8
int32 MaxSegmentVertices = DEFAULT_MAX_SEGMENT_VERTICES;

#define DEFAULT_MAX_INDEXES_PER_COLLECTION 64
int32 MaxIndexesPerCollection = DEFAULT_MAX_INDEXES_PER_COLLECTION;

/* --------------------------------------------------------- */
/* Top level exports */
/* --------------------------------------------------------- */


/*
 * Initializes core configurations pertaining to the bson type management.
 */
void
InitApiConfigurations(char *prefix)
{
	DefineCustomStringVariable(
		psprintf("%s.localhost_connection_string", prefix),
		gettext_noop("Sets the hostname (and potentially other parameters "
					 "except port number and database name) when connecting "
					 "back to itself for operations that needs to be done via "
					 "a libpq connection."),
		NULL, &LocalhostConnectionString, DEFAULT_LOCALHOST_CONN_STR,
		PGC_SUSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_create_collection_on_insert", prefix),
		gettext_noop("Create a collection when inserting into a non-existent collection"),
		NULL, &EnableCreateCollectionOnInsert, true,
		PGC_USERSET, GUC_NO_SHOW_ALL, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_extended_index_filters", prefix),
		gettext_noop("Determines whether create_indexes() should allow expressions "
					 "that Mongo doesn't allow using in \"partialFilterExpression\" "
					 "document but postgres could potentially allow."),
		NULL, &EnableExtendedIndexFilters, DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS,
		PGC_USERSET, GUC_NO_SHOW_ALL, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.query_plan_cache_size", prefix),
		gettext_noop("Set the size of the query plan cache"),
		NULL,
		&QueryPlanCacheSizeLimit,
		DEFAULT_QUERY_PLAN_CACHE_SIZE_LIMIT, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.next_collection_id", prefix),
		gettext_noop("Set the next collection id to use when creationing a collection."),
		gettext_noop("Collection ids are normally generated using a sequence. If "
					 "next_collection_id is set to a value different than "
					 "DEFAULT_NEXT_COLLECTION_ID, then collection will instead be "
					 "generated by incrementing from the value of this GUC and this "
					 "will be reflected in the GUC. This is mainly useful to ensure "
					 "consistent collection ids when running tests in parallel."),
		&NextCollectionId,
		DEFAULT_NEXT_COLLECTION_ID, DEFAULT_NEXT_COLLECTION_ID, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.next_collection_index_id", prefix),
		gettext_noop("Set the next collection index id to use when creating a "
					 "collection index."),
		gettext_noop("Collection index ids are normally generated using a sequence. "
					 "If next_collection_index_id is set to a value different than "
					 "DEFAULT_NEXT_COLLECTION_INDEX_ID, then collection index ids "
					 "will instead be generated by incrementing from the value of "
					 "this GUC and this will be reflected in the GUC. This is mainly "
					 "useful to ensure consistent collection index ids when running "
					 "tests in parallel."),
		&NextCollectionIndexId,
		DEFAULT_NEXT_COLLECTION_INDEX_ID, DEFAULT_NEXT_COLLECTION_INDEX_ID, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxWriteBatchSize", prefix),
		gettext_noop("The max number of write operations permitted in a write batch."),
		NULL,
		&MaxWriteBatchSize,
		DEFAULT_MAX_WRITE_BATCH_SIZE, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.mongoEnableDeveloperExplain", prefix),
		gettext_noop("Determines whether the explain can show internal information"),
		NULL,
		&EnableDeveloperExplain,
		DEFAULT_ENABLE_DEVELOPER_EXPLAIN,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxTTLDeleteBatchSize", prefix),
		gettext_noop(
			"The max number of delete operations permitted while deleting a batch of expired documents."),
		NULL,
		&MaxTTLDeleteBatchSize,
		DEFAULT_MAX_TTL_DELETE_BATCH_SIZE, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.logTTLProgressActivity", prefix),
		gettext_noop(
			"Whether to log activity done by a ttl purger. It's turned off by default to reduce noise."),
		NULL, &LogTTLProgressActivity, DEFAULT_LOG_TTL_PROGRESS_ACTIVITY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.TTLPurgerStatementTimeout", prefix),
		gettext_noop(
			"Statement timeout in milliseconds of the TTL purger delete query."),
		NULL,
		&TTLPurgerStatementTimeout,
		DEFAULT_TTL_PURGER_STATEMENT_TIMEOUT, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.SingleTTLTaskTimeBudget", prefix),
		gettext_noop(
			"Time budget assigned in milliseconds for single invocation of ttl task."),
		NULL,
		&SingleTTLTaskTimeBudget,
		DEFAULT_SINGLE_TTL_TASK_TIME_BUDGET, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.TTLPurgerLockTimeout", prefix),
		gettext_noop(
			"Lock timeout in milliseconds of the TTL purger delete query."),
		NULL,
		&TTLPurgerLockTimeout,
		DEFAULT_TTL_PURGER_LOCK_TIMEOUT, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.forceRumIndexScantoBitmapHeapScan", prefix),
		gettext_noop(
			"Force RUM Index Scan to BitMap Heap Scan"),
		NULL,
		&ForceRUMIndexScanToBitmapHeapScan,
		DEFAULT_FORCE_RUM_INDEXSCAN_TO_BITMAPHEAPSCAN,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.forceUseIndexIfAvailable", prefix),
		gettext_noop(
			"Forces the query planner to push to the RUM index if it's applicable - do not pick the index path purely based on cost."),
		NULL, &ForceUseIndexIfAvailable, DEFAULT_FORCE_USE_INDEX_IF_AVAILABLE,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.coll_stats_count_policy_threshold", prefix),
		gettext_noop("Set the collStats 'count policy' change threshold"),
		gettext_noop("If the documents count becomes less than this Threshold, "
					 "the count policy changes to get the count at runtime"),
		&CollStatsCountPolicyThreshold,
		DEFAULT_COLL_STATS_COUNT_POLICY_THRESHOLD, 1, INT_MAX - 1,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.simulateRecoveryState", prefix),
		gettext_noop(
			"Simulates a database recovery state and throws an error for read-write operations."),
		NULL, &SimulateRecoveryState, DEFAULT_SIMULATE_RECOVERY_STATE,
		PGC_USERSET, GUC_NO_SHOW_ALL, NULL, NULL, NULL);

	/* Added variable for testing cursor continuations */
	DefineCustomIntVariable(
		psprintf("%s.maxWorkerCursorSize", prefix),
		gettext_noop(
			"The maximum size a single cursor response page should be in a worker."),
		NULL, &MaxWorkerCursorSize,
		DEFAULT_MAX_WORKER_CURSOR_SIZE, 1, BSON_MAX_ALLOWED_SIZE,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);
	DefineCustomIntVariable(
		psprintf("%s.batchWriteSubTransactionCount", prefix),
		gettext_noop("The size of each sub-transaction within any write command."),
		NULL, &BatchWriteSubTransactionCount,
		DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableGeospatial", prefix),
		gettext_noop(
			"Enables support for geospatial indexes and queries in pg_helio_api."),
		NULL, &EnableGeospatialSupport, DEFAULT_ENABLE_GEOSPATIAL,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableVectorHNSWIndex", prefix),
		gettext_noop(
			"Enables support for HNSW index type and query for vector search in pg_helio_api."),
		NULL, &EnableVectorHNSWIndex, DEFAULT_ENABLE_VECTOR_HNSW_INDEX,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableVectorPreFilter", prefix),
		gettext_noop(
			"Enables support for vector pre-filtering feature for vector search in pg_helio_api."),
		NULL, &EnableVectorPreFilter, DEFAULT_ENABLE_VECTOR_PRE_FILTER,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxNumActiveUsersIndexBuilds", prefix),
		gettext_noop("Max number of active users Index Builds that can run."),
		NULL, &MaxNumActiveUsersIndexBuilds,
		DEFAULT_MAX_NUM_ACTIVE_USERS_INDEX_BUILDS, 1, INT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.IsPgReadOnlyForDiskFull", prefix),
		gettext_noop(
			"Determines whether postgres is in readonly mode since disk is full"),
		NULL, &HelioPGReadOnlyForDiskFull, DEFAULT_HELIO_PG_READ_ONLY_FOR_DISK_FULL,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomEnumVariable(
		psprintf("%s.defaultReadConcern", prefix),
		gettext_noop("Cluster-wide read concern; set via marlin."),
		NULL, &DefaultReadConcernLevel, DEFAULT_READ_CONCERN_LEVEL,
		READ_CONCERN_LEVEL_CONFIG_ENTRIES,
		PGC_SUSET, 0, NULL, NULL, NULL);

	DefineCustomStringVariable(
		psprintf("%s.defaultWriteConcern", prefix),
		gettext_noop("Cluster-wide write concern; set via marlin."),
		NULL, &DefaultWriteConcernLevel, DEFAULT_WRITE_CONCERN_LEVEL,
		PGC_SUSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxIndexBuildAttempts", prefix),
		gettext_noop(
			"The maximum number of attempts to build an index for a failed requests."),
		NULL, &MaxIndexBuildAttempts,
		DEFAULT_MAX_INDEX_BUILD_ATTEMPTS, 1, SHRT_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.indexBuildScheduleInSec", prefix),
		gettext_noop("The index build cron-job schedule in seconds."),
		NULL, &IndexBuildScheduleInSec,
		DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC, 1, 60,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.indexQueueEvictionIntervalInSec", prefix),
		gettext_noop(
			"Interval in seconds for skippable build index requests to be evicted from the queue."),
		NULL, &IndexQueueEvictionIntervalInSec,
		DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableIndexTermTruncation", prefix),
		gettext_noop(
			"Whether to enable the feature for index term truncation"),
		NULL, &EnableIndexTermTruncation, DEFAULT_ENABLE_INDEX_TERM_TRUNCATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.forceIndexTermTruncation", prefix),
		gettext_noop(
			"Whether to force the feature for index term truncation"),
		NULL, &ForceIndexTermTruncation, DEFAULT_ENABLE_INDEX_TERM_TRUNCATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.indexTermLimitOverride", prefix),
		gettext_noop(
			"Override for the index term truncation limit (primarily for tests)."),
		NULL, &IndexTruncationLimitOverride,
		DEFAULT_INDEX_TRUNCATION_LIMIT_OVERRIDE, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL,
		NULL, NULL, NULL);

	DefineCustomRealVariable(
		psprintf("%s.geo2dsphereSegmentMaxLength", prefix),
		gettext_noop(
			"Maximum segment length (in km) allowed for geospatial spherical queries. Set 0 if segmentation needs to be disabled."),
		NULL, &MaxSegmentLengthInKms, DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM, 0, 6372,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.geo2dsphereSegmentMaxVertices", prefix),
		gettext_noop(
			"Maximum segment vertices allowed for geospatial spherical queries. If sphereSegmentMaxLength is 0 then this config has no effect overall."),
		NULL, &MaxSegmentVertices, DEFAULT_MAX_SEGMENT_VERTICES, 0, 32,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxIndexesPerCollection", prefix),
		gettext_noop(
			"Max number of indexes per collection."),
		NULL, &MaxIndexesPerCollection, DEFAULT_MAX_INDEXES_PER_COLLECTION, 0, 300,
		PGC_USERSET, 0, NULL, NULL, NULL);
}


/*
 * Install custom hooks that Postgres exposes for Helio API.
 */
void
InstallHelioApiPostgresHooks(void)
{
	ExtensionPreviousIndexNameHook = explain_get_index_name_hook;
	explain_get_index_name_hook = ExtensionExplainGetIndexName;

	/* override planner paths hook for overriding indexed and non-indexed paths. */
	ExtensionPreviousSetRelPathlistHook = set_rel_pathlist_hook;
	set_rel_pathlist_hook = ExtensionRelPathlistHook;

	RegisterXactCallback(HelioTransactionCallback, NULL);
	RegisterSubXactCallback(HelioSubTransactionCallback, NULL);

	RegisterScanNodes();
	RegisterQueryScanNodes();
}


/*
 * Uninstalls custom hooks that Postgres exposes for Helio API.
 */
void
UninstallHelioApiPostgresHooks(void)
{
	explain_get_index_name_hook = ExtensionPreviousIndexNameHook;
	ExtensionPreviousIndexNameHook = NULL;

	set_rel_pathlist_hook = ExtensionPreviousSetRelPathlistHook;
	ExtensionPreviousSetRelPathlistHook = NULL;

	UnregisterXactCallback(HelioTransactionCallback, NULL);
	UnregisterSubXactCallback(HelioSubTransactionCallback, NULL);
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */


static void
HelioTransactionCallback(XactEvent event, void *arg)
{
	switch (event)
	{
		case XACT_EVENT_ABORT:
		case XACT_EVENT_PARALLEL_ABORT:
		{
			ConnMgrTryCancelActiveConnection();
			break;
		}

		default:
		{
			break;
		}
	}
}


static void
HelioSubTransactionCallback(SubXactEvent event, SubTransactionId mySubid,
							SubTransactionId parentSubid, void *arg)
{
	switch (event)
	{
		case SUBXACT_EVENT_ABORT_SUB:
		{
			ConnMgrTryCancelActiveConnection();
			break;
		}

		default:
		{
			break;
		}
	}
}
