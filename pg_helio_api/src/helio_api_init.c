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
#include <postmaster/bgworker.h>
#include <storage/ipc.h>

#include "helio_api_init.h"
#include "metadata/metadata_guc.h"
#include "planner/helio_planner.h"
#include "customscan/custom_scan_registrations.h"
#include "commands/connection_management.h"
#include "utils/feature_counter.h"
#include "utils/version_utils.h"
#include "vector/vector_spec.h"

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

static shmem_startup_hook_type prev_shmem_startup_hook = NULL;

/*
 * helio_api.enable_create_collection_on_insert GUC determines whether
 * an insert into a non-existent collection should create a collection.
 */
extern bool EnableCreateCollectionOnInsert;

/* In single node mode, we always inline write operations */
bool DefaultInlineWriteOperations = true;

#define DEFAULT_USE_LOCAL_EXECUTION_SHARD_QUERIES false
bool UseLocalExecutionShardQueries = DEFAULT_USE_LOCAL_EXECUTION_SHARD_QUERIES;


#define DEFAULT_ENABLE_NEW_OPERATOR_SELECTIVITY false
bool EnableNewOperatorSelectivityMode = DEFAULT_ENABLE_NEW_OPERATOR_SELECTIVITY;

/* --------------------------------------------------------- */
/* Forward declaration */
/* --------------------------------------------------------- */


/* callbacks for transaction management */
static void HelioTransactionCallback(XactEvent event, void *arg);
static void HelioSubTransactionCallback(SubXactEvent event, SubTransactionId mySubid,
										SubTransactionId parentSubid, void *arg);
static void InitHelioBackgroundWorkerGucs(void);

static void HelioSharedMemoryInit(void);

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
/* handling with multi-node clusters. */
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

#define DEFAULT_ENABLE_IN_QUERY_OPTIMIZATION false
bool EnableInQueryOptimization = DEFAULT_ENABLE_IN_QUERY_OPTIMIZATION;

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

/* Use 512 to line up with Mongo spark client to minimize splitting transactions */
#define DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT 512
int BatchWriteSubTransactionCount = DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT;

#define DEFAULT_ENABLE_GEOSPATIAL true
bool EnableGeospatialSupport = DEFAULT_ENABLE_GEOSPATIAL;

/*
 * GUC to enable HNSW index type and query for vector search.
 * This is disabled by default.
 */
#define DEFAULT_ENABLE_VECTOR_HNSW_INDEX true
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

#define DEFAULT_ENABLE_INDEX_BUILD_BACKGROUND false
bool EnableIndexBuildBackground = DEFAULT_ENABLE_INDEX_BUILD_BACKGROUND;

#define DEFAULT_ENABLE_GENERATE_NON_EXISTS_TERM true
bool EnableGenerateNonExistsTerm = DEFAULT_ENABLE_GENERATE_NON_EXISTS_TERM;

#define DEFAULT_MAX_INDEX_BUILD_ATTEMPTS 3
int MaxIndexBuildAttempts = DEFAULT_MAX_INDEX_BUILD_ATTEMPTS;

#define DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC 2
int32_t IndexBuildScheduleInSec = DEFAULT_INDEX_BUILD_SCHEDULE_IN_SEC;

#define DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC 1200
int IndexQueueEvictionIntervalInSec = DEFAULT_INDEX_BUILD_EVICTION_INTERVAL_IN_SEC;

#define DEFAULT_ENABLE_INDEX_TERM_TRUNCATION false
bool EnableIndexTermTruncation = DEFAULT_ENABLE_INDEX_TERM_TRUNCATION;


#define DEFAULT_FORCE_INDEX_TERM_TRUNCATION false
bool ForceIndexTermTruncation = DEFAULT_FORCE_INDEX_TERM_TRUNCATION;

#define DEFAULT_ENABLE_LARGE_INDEX_KEYS true
bool DefaultEnableLargeIndexKeys = DEFAULT_ENABLE_LARGE_INDEX_KEYS;

#define DEFAULT_INDEX_TRUNCATION_LIMIT_OVERRIDE INT_MAX
int IndexTruncationLimitOverride = DEFAULT_INDEX_TRUNCATION_LIMIT_OVERRIDE;

#define DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM 500
double MaxSegmentLengthInKms = DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM;

#define DEFAULT_MAX_SEGMENT_VERTICES 8
int32 MaxSegmentVertices = DEFAULT_MAX_SEGMENT_VERTICES;

#define DEFAULT_MAX_INDEXES_PER_COLLECTION 64
int32 MaxIndexesPerCollection = DEFAULT_MAX_INDEXES_PER_COLLECTION;

#define DEFAULT_UNSHARDED_BATCH_DELETE true
bool EnableUnshardedBatchDelete = DEFAULT_UNSHARDED_BATCH_DELETE;

#define DEFAULT_ENABLE_MERGE_OBJECTS_SUPPORT false
bool EnableGroupMergeObjectsSupport = DEFAULT_ENABLE_MERGE_OBJECTS_SUPPORT;

#define DEFAULT_ENABLE_RUM_INDEX_SCAN false
bool EnableRumIndexScan = DEFAULT_ENABLE_RUM_INDEX_SCAN;

#define DEFAULT_ENABLE_MERGE_STAGE false
bool EnableMergeStage = DEFAULT_ENABLE_MERGE_STAGE;

#define DEFAULT_ENABLE_MERGE_TARGET_CREATION false
bool EnableMergeTargetCreation = DEFAULT_ENABLE_MERGE_TARGET_CREATION;

#define DEFAULT_ENABLE_MERGE_ACROSS_DB false
bool EnableMergeAcrossDB = DEFAULT_ENABLE_MERGE_ACROSS_DB;

#define DEFAULT_ENABLE_CURSORS_ON_AGGREGATION_QUERY_REWRITE false
bool EnableCursorsOnAggregationQueryRewrite =
	DEFAULT_ENABLE_CURSORS_ON_AGGREGATION_QUERY_REWRITE;

#define DEFAULT_MAX_WILDCARD_INDEX_KEY_SIZE 200
int MaxWildcardIndexKeySize = DEFAULT_MAX_WILDCARD_INDEX_KEY_SIZE;

#define DEFAULT_ENABLE_SCHEMA_VALIDATION false
bool EnableSchemaValidation =
	DEFAULT_ENABLE_SCHEMA_VALIDATION;

/* default value for max validator size */
#define DEFAULT_MAX_SCHEMA_VALIDATOR_SIZE 10 * 1024
int MaxSchemaValidatorSize = DEFAULT_MAX_SCHEMA_VALIDATOR_SIZE;

#define DEFAULT_ENABLE_MULTI_INDEX_RUM_JOIN false
bool EnableMultiIndexRumJoin = DEFAULT_ENABLE_MULTI_INDEX_RUM_JOIN;

#define DEFAULT_ENABLE_BG_WORKER false
bool EnableBackgroundWorker = DEFAULT_ENABLE_BG_WORKER;

#define DEFAULT_BG_DATABASE_NAME "postgres"
char *BackgroundWorkerDatabaseName = DEFAULT_BG_DATABASE_NAME;

#define DEFAULT_BG_LATCH_TIMEOUT_SEC 10
int LatchTimeOutSec = DEFAULT_BG_LATCH_TIMEOUT_SEC;

#define DEFAULT_ENABLE_STAGE_SET_WINDOW_FIELDS false
bool EnableSetWindowFields = DEFAULT_ENABLE_STAGE_SET_WINDOW_FIELDS;

#define DEFAULT_ENABLE_NATIVE_COLOCATION true
bool EnableNativeColocation = DEFAULT_ENABLE_NATIVE_COLOCATION;

#define DEFAULT_ENABLE_NATIVE_TABLE_COLOCATION false
bool EnableNativeTableColocation = DEFAULT_ENABLE_NATIVE_TABLE_COLOCATION;

#define DEFAULT_ENABLE_LET_SUPPORT false
bool EnableLetSupport = DEFAULT_ENABLE_LET_SUPPORT;

#define DEFAULT_ENABLE_LOOKUP_LET_SUPPORT false
bool EnableLookupLetSupport = DEFAULT_ENABLE_LOOKUP_LET_SUPPORT;

#define DEFAULT_ENABLE_LOOKUP_UNWIND_OPTIMIZATION false
bool EnableLookupUnwindSupport = DEFAULT_ENABLE_LOOKUP_UNWIND_OPTIMIZATION;

#define DEFAULT_IGNORE_LET_ON_QUERY false
bool IgnoreLetOnQuerySupport = DEFAULT_IGNORE_LET_ON_QUERY;

#define DEFAULT_ENABLE_INDEX_TERM_TRUNCATION_NESTED_OBJECTS true
bool EnableIndexTermTruncationOnNestedObjects =
	DEFAULT_ENABLE_INDEX_TERM_TRUNCATION_NESTED_OBJECTS;

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
		PGC_USERSET, GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_extended_index_filters", prefix),
		gettext_noop("Determines whether create_indexes() should allow expressions "
					 "that Mongo doesn't allow using in \"partialFilterExpression\" "
					 "document but postgres could potentially allow."),
		NULL, &EnableExtendedIndexFilters, DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS,
		PGC_USERSET, GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE, NULL, NULL, NULL);

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
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
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
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
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

	DefineCustomBoolVariable(
		"helio_api.mongoEnableInQueryOptimization",
		gettext_noop("Determines whether in queries are rewritten to equality"),
		NULL,
		&EnableInQueryOptimization,
		DEFAULT_ENABLE_IN_QUERY_OPTIMIZATION,
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
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.simulateRecoveryState", prefix),
		gettext_noop(
			"Simulates a database recovery state and throws an error for read-write operations."),
		NULL, &SimulateRecoveryState, DEFAULT_SIMULATE_RECOVERY_STATE,
		PGC_USERSET, 0, NULL, NULL, NULL);

	/* Added variable for testing cursor continuations */
	DefineCustomIntVariable(
		psprintf("%s.maxWorkerCursorSize", prefix),
		gettext_noop(
			"The maximum size a single cursor response page should be in a worker."),
		NULL, &MaxWorkerCursorSize,
		DEFAULT_MAX_WORKER_CURSOR_SIZE, 1, BSON_MAX_ALLOWED_SIZE,
		PGC_USERSET,
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
		NULL, NULL, NULL);
	DefineCustomIntVariable(
		psprintf("%s.batchWriteSubTransactionCount", prefix),
		gettext_noop("The size of each sub-transaction within any write command."),
		NULL, &BatchWriteSubTransactionCount,
		DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
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

	DefineCustomBoolVariable(
		psprintf("%s.enableIndexBuildBackground", prefix),
		gettext_noop("Enables support for Index Builds in background."),
		NULL, &EnableIndexBuildBackground, DEFAULT_ENABLE_INDEX_BUILD_BACKGROUND,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableGenerateNonExistsTerm", prefix),
		gettext_noop(
			"Enables generating the non exists term for new documents in a collection."),
		NULL, &EnableGenerateNonExistsTerm, DEFAULT_ENABLE_GENERATE_NON_EXISTS_TERM,
		PGC_USERSET, 0, NULL, NULL, NULL);

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
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.forceIndexTermTruncation", prefix),
		gettext_noop(
			"Whether to force the feature for index term truncation"),
		NULL, &ForceIndexTermTruncation, DEFAULT_ENABLE_INDEX_TERM_TRUNCATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enable_large_index_keys",
		gettext_noop("Whether or not to enable large index keys support"),
		NULL, &DefaultEnableLargeIndexKeys, DEFAULT_ENABLE_LARGE_INDEX_KEYS,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.indexTermLimitOverride", prefix),
		gettext_noop(
			"Override for the index term truncation limit (primarily for tests)."),
		NULL, &IndexTruncationLimitOverride,
		DEFAULT_INDEX_TRUNCATION_LIMIT_OVERRIDE, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
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

	DefineCustomBoolVariable(
		"helio_api.enableUnshardedBatchDelete",
		gettext_noop(
			"Feature flag to enable pushing an unsharded batch update to the worker"),
		NULL,
		&EnableUnshardedBatchDelete, DEFAULT_UNSHARDED_BATCH_DELETE, PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.useLocalExecutionShardQueries",
		gettext_noop(
			"Determines whether or not to push local shard queries to the shard directly."),
		NULL, &UseLocalExecutionShardQueries, DEFAULT_USE_LOCAL_EXECUTION_SHARD_QUERIES,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableNewSelectivityMode",
		gettext_noop(
			"Determines whether to use the new selectivity logic."),
		NULL, &EnableNewOperatorSelectivityMode,
		DEFAULT_ENABLE_NEW_OPERATOR_SELECTIVITY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableGroupMergeObjectsSupport",
		gettext_noop("Feature flag for the group mergeObjects support"), NULL,
		&EnableGroupMergeObjectsSupport, DEFAULT_ENABLE_MERGE_OBJECTS_SUPPORT,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableRumIndexScan",
		gettext_noop(
			"Allow rum index scans."),
		NULL,
		&EnableRumIndexScan,
		DEFAULT_ENABLE_RUM_INDEX_SCAN,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableMergeStage",
		gettext_noop(
			"Enables support for merge stage in pg_helio_api."),
		NULL, &EnableMergeStage, DEFAULT_ENABLE_MERGE_STAGE,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableMergeTargetCreation",
		gettext_noop(
			"Enables support for target collection creation in pg_helio_api."),
		NULL, &EnableMergeTargetCreation, DEFAULT_ENABLE_MERGE_TARGET_CREATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableMergeAcrossDB",
		gettext_noop(
			"Enables support for merge stage in pg_helio_api."),
		NULL, &EnableMergeAcrossDB, DEFAULT_ENABLE_MERGE_ACROSS_DB,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableCursorsOnAggregationQueryRewrite",
		gettext_noop(
			"Whether or not to add the cursors on aggregation style queries."),
		NULL,
		&EnableCursorsOnAggregationQueryRewrite,
		DEFAULT_ENABLE_CURSORS_ON_AGGREGATION_QUERY_REWRITE,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		"helio_api.maxWildcardIndexKeySize",
		gettext_noop("GUC for the max wildcard index key size."),
		NULL, &MaxWildcardIndexKeySize,
		DEFAULT_MAX_WILDCARD_INDEX_KEY_SIZE, 1, INT32_MAX,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableSchemaValidation", prefix),
		gettext_noop(
			"Whether or not to support schema validation."),
		NULL,
		&EnableSchemaValidation,
		DEFAULT_ENABLE_SCHEMA_VALIDATION,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxSchemaValidatorSize", prefix),
		gettext_noop(
			"Maximum size of the schema validator."),
		NULL,
		&MaxSchemaValidatorSize,
		DEFAULT_MAX_SCHEMA_VALIDATOR_SIZE, 0, 16 * 1024 * 1024,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableMultiIndexRumJoin",
		gettext_noop(
			"Whether or not to add the cursors on aggregation style queries."),
		NULL,
		&EnableMultiIndexRumJoin,
		DEFAULT_ENABLE_MULTI_INDEX_RUM_JOIN,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableHelioBackgroundWorker",
		gettext_noop("Enable Helio Background worker."),
		NULL, &EnableBackgroundWorker, DEFAULT_ENABLE_BG_WORKER,
		PGC_SUSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableStageSetWindowFields",
		gettext_noop(
			"Enables support for setWindowFields stage in pg_helio_api."),
		NULL,
		&EnableSetWindowFields,
		DEFAULT_ENABLE_STAGE_SET_WINDOW_FIELDS,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableNativeColocation", prefix),
		gettext_noop(
			"Determines whether to turn on colocation of tables in a given mongo database (and disabled outside the database)"),
		NULL, &EnableNativeColocation, DEFAULT_ENABLE_NATIVE_COLOCATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableNativeTableColocation", prefix),
		gettext_noop(
			"Determines whether to turn on colocation of tables across all tables (requires enableNativeColocation to be on)"),
		NULL, &EnableNativeTableColocation, DEFAULT_ENABLE_NATIVE_TABLE_COLOCATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableLetSupport",
		gettext_noop(
			"Determines whether to enable support for the let in commands and $lookup"),
		NULL, &EnableLetSupport, DEFAULT_ENABLE_LET_SUPPORT,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableLookupLetSupport",
		gettext_noop(
			"Determines whether to enable support for the let in commands and $lookup"),
		NULL, &EnableLookupLetSupport, DEFAULT_ENABLE_LOOKUP_LET_SUPPORT,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableLookupUnwindOptimization",
		gettext_noop(
			"Determines whether to enable support for the optimizing $unwind with $lookup prefix"),
		NULL, &EnableLookupUnwindSupport, DEFAULT_ENABLE_LOOKUP_UNWIND_OPTIMIZATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.ignoreLetOnQuerySpec",
		gettext_noop(
			"Determines whether to ignore the spec let in commands and $lookup"),
		NULL, &IgnoreLetOnQuerySupport, DEFAULT_IGNORE_LET_ON_QUERY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		"helio_api.enableIndexTermTruncationOnNestedObjects",
		gettext_noop(
			"Determines whether to truncate index terms with nested objects (arrays/objects of arrays/objects)"),
		NULL, &EnableIndexTermTruncationOnNestedObjects,
		DEFAULT_ENABLE_INDEX_TERM_TRUNCATION_NESTED_OBJECTS,
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
	RegisterRumJoinScanNodes();
}


/* Initialized the background worker */
void
InitializeHelioBackgroundWorker(char *libraryName)
{
	/* Initialize GUCs */
	InitHelioBackgroundWorkerGucs();

	if (!EnableBackgroundWorker)
	{
		return;
	}

	BackgroundWorker worker;
	memset(&worker, 0, sizeof(worker));

	/* set up common data for the worker */
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS | BGWORKER_BACKEND_DATABASE_CONNECTION;
	worker.bgw_start_time = BgWorkerStart_RecoveryFinished;
	worker.bgw_restart_time = 10;
	worker.bgw_main_arg = Int32GetDatum(0);
	worker.bgw_notify_pid = 0;

	sprintf(worker.bgw_library_name, "%s", libraryName);
	sprintf(worker.bgw_function_name, "HelioBackgroundWorkerMain");
	snprintf(worker.bgw_name, BGW_MAXLEN, "helio bg worker leader");
	snprintf(worker.bgw_type, BGW_MAXLEN, "helio_bg_worker_leader");

	RegisterBackgroundWorker(&worker);
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


void
InitializeSharedMemoryHooks(void)
{
	prev_shmem_startup_hook = shmem_startup_hook;
	shmem_startup_hook = HelioSharedMemoryInit;
}


/* --------------------------------------------------------- */
/* Private methods */
/* --------------------------------------------------------- */


static void
HelioSharedMemoryInit(void)
{
	SharedFeatureCounterShmemInit();
	InitializeVersionCache();

	if (prev_shmem_startup_hook != NULL)
	{
		prev_shmem_startup_hook();
	}
}


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


static void
InitHelioBackgroundWorkerGucs(void)
{
	DefineCustomStringVariable(
		"helio_bg_worker.database_name",
		gettext_noop("Database to which background worker will connect."),
		NULL,
		&BackgroundWorkerDatabaseName,
		DEFAULT_BG_DATABASE_NAME,
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		"helio_bg_worker.latch_timeout",
		gettext_noop("Latch timeout inside main thread of helio_bg worker leader."),
		NULL,
		&LatchTimeOutSec,
		DEFAULT_BG_LATCH_TIMEOUT_SEC,
		0,
		200,
		PGC_POSTMASTER,
		GUC_SUPERUSER_ONLY,
		NULL, NULL, NULL);
}
