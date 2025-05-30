/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/configs/limit_configs.c
 *
 * Initialization of GUCs that control the limits or behavior of the system.
 * These GUCs are considered long-term requirements of the system.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include <limits.h>
#include "configs/config_initialization.h"

/*
 * Externally defined GUC constants
 * TODO(OSS): Move these as appropriate.
 */

/*
 * enable_create_collection_on_insert GUC determines whether
 * an insert into a non-existent collection should create a collection.
 */
extern bool EnableCreateCollectionOnInsert;

#define DEFAULT_SHARDING_MAX_CHUNKS 128
int ShardingMaxChunks = DEFAULT_SHARDING_MAX_CHUNKS;

#define DEFAULT_QUERY_PLAN_CACHE_SIZE_LIMIT 100
int QueryPlanCacheSizeLimit = DEFAULT_QUERY_PLAN_CACHE_SIZE_LIMIT;

/* TODO: Raise this back to 100,000 once we can optimize sub-transaction */
/* handling with multi-node clusters. */
#define DEFAULT_MAX_WRITE_BATCH_SIZE 25000
int MaxWriteBatchSize = DEFAULT_MAX_WRITE_BATCH_SIZE;

/* Use 512 to line up with Mongo spark client to minimize splitting transactions */
#define DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT 512
int BatchWriteSubTransactionCount = DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT;

/*
 * GUC for "Count Policy" change Threshold for collStats DB command
 * If the document count from stats is more than this threshold, the count policy remains "get count from stats".
 * If the document count from stats is less than this threshold, the count policy becomes "get count at runtime".
 */
#define DEFAULT_COLL_STATS_COUNT_POLICY_THRESHOLD 10000
int CollStatsCountPolicyThreshold = DEFAULT_COLL_STATS_COUNT_POLICY_THRESHOLD;

#define DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM 500
double MaxSegmentLengthInKms = DEFAULT_GEO_MAX_SEGMENT_LENGTH_KM;

#define DEFAULT_MAX_SEGMENT_VERTICES 8
int32 MaxSegmentVertices = DEFAULT_MAX_SEGMENT_VERTICES;

#define DEFAULT_MAX_INDEXES_PER_COLLECTION 64
int32 MaxIndexesPerCollection = DEFAULT_MAX_INDEXES_PER_COLLECTION;

#define DEFAULT_MAX_WILDCARD_INDEX_KEY_SIZE 200
int MaxWildcardIndexKeySize = DEFAULT_MAX_WILDCARD_INDEX_KEY_SIZE;

/* default value for max validator size */
#define DEFAULT_MAX_SCHEMA_VALIDATOR_SIZE 10 * 1024
int MaxSchemaValidatorSize = DEFAULT_MAX_SCHEMA_VALIDATOR_SIZE;

#define SCRAM_DEFAULT_SALT_LEN 28
int ScramDefaultSaltLen = SCRAM_DEFAULT_SALT_LEN;

#define MAX_USER_LIMIT 10
int MaxUserLimit = MAX_USER_LIMIT;

#define DEFAULT_TDIGEST_COMPRESSION_ACCURACY 1500
int TdigestCompressionAccuracy = DEFAULT_TDIGEST_COMPRESSION_ACCURACY;

#define DEFAULT_DOCUMENTDB_PG_READ_ONLY_FOR_DISK_FULL false
bool DocumentDBPGReadOnlyForDiskFull = DEFAULT_DOCUMENTDB_PG_READ_ONLY_FOR_DISK_FULL;

#define DEFAULT_FORCE_RUM_INDEXSCAN_TO_BITMAPHEAPSCAN true
bool ForceRUMIndexScanToBitmapHeapScan = DEFAULT_FORCE_RUM_INDEXSCAN_TO_BITMAPHEAPSCAN;

/* Setting this to true until we have statistics. When dealing with large number of records sequential
 * scan can win even if there is an index to be used, because index cost being not reflected properly.
 * Avoiding that case is our priority. Using index when dealing with small number of records might be worse
 * than using sequential scan, but we are okay with that case as the latency hit would be very small.
 * This affects all of our queries.*/
#define DEFAULT_FORCE_USE_INDEX_IF_AVAILABLE true
bool ForceUseIndexIfAvailable = DEFAULT_FORCE_USE_INDEX_IF_AVAILABLE;

#define DEFAULT_THROW_DEADLOCK_ON_CRUD false
bool ThrowDeadlockOnCrud = DEFAULT_THROW_DEADLOCK_ON_CRUD;

#define DEFAULT_LOCALHOST_CONN_STR "host=localhost"
char *LocalhostConnectionString = DEFAULT_LOCALHOST_CONN_STR;

/* Currently timeout max at 3 hours */
#define DEFAULT_MAX_CUSTOM_COMMAND_TIMEOUT (3600 * 3 * 1000)
int MaxCustomCommandTimeout = DEFAULT_MAX_CUSTOM_COMMAND_TIMEOUT;

#define DEFAULT_BLOCKED_ROLE_PREFIX_LIST ""
char *BlockedRolePrefixList = DEFAULT_BLOCKED_ROLE_PREFIX_LIST;

#define DEFAULT_CURRENT_OP_APPLICATION_NAME ""
char *CurrentOpApplicationName = DEFAULT_CURRENT_OP_APPLICATION_NAME;

#define DEFAULT_AGGREGATION_STAGES_LIMIT 1000
int MaxAggregationStagesAllowed = DEFAULT_AGGREGATION_STAGES_LIMIT;

void
InitializeSystemConfigurations(const char *prefix, const char *newGucPrefix)
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
		psprintf("%s.maxWriteBatchSize", prefix),
		gettext_noop("The max number of write operations permitted in a write batch."),
		NULL,
		&MaxWriteBatchSize,
		DEFAULT_MAX_WRITE_BATCH_SIZE, 1, INT_MAX,
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

	DefineCustomIntVariable(
		psprintf("%s.batchWriteSubTransactionCount", prefix),
		gettext_noop("The size of each sub-transaction within any write command."),
		NULL, &BatchWriteSubTransactionCount,
		DEFAULT_BATCH_WRITE_SUB_TRANSACTION_COUNT, 1, INT_MAX,
		PGC_USERSET,
		GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.IsPgReadOnlyForDiskFull", prefix),
		gettext_noop(
			"Determines whether postgres is in readonly mode since disk is full"),
		NULL, &DocumentDBPGReadOnlyForDiskFull,
		DEFAULT_DOCUMENTDB_PG_READ_ONLY_FOR_DISK_FULL,
		PGC_USERSET, 0, NULL, NULL, NULL);

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

	DefineCustomIntVariable(
		psprintf("%s.maxWildcardIndexKeySize", newGucPrefix),
		gettext_noop("GUC for the max wildcard index key size."),
		NULL, &MaxWildcardIndexKeySize,
		DEFAULT_MAX_WILDCARD_INDEX_KEY_SIZE, 1, INT32_MAX,
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

	DefineCustomIntVariable(
		psprintf("%s.sharding_max_chunks", prefix),
		gettext_noop(
			"Gets the maximum allowed number of chunks for a shard collection operation"),
		NULL, &ShardingMaxChunks, DEFAULT_SHARDING_MAX_CHUNKS, 1, 8192, PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.scramDefaultSaltLen", newGucPrefix),
		gettext_noop("The default scram salt length."),
		NULL, &ScramDefaultSaltLen,
		SCRAM_DEFAULT_SALT_LEN, 1, 64,
		PGC_SUSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.throwDeadlockOnCRUD", newGucPrefix),
		gettext_noop(
			"Determines whether a deadlock on CRUD operations should be thrown as an exception rather than catching it and writing it to the operation result bson."),
		NULL,
		&ThrowDeadlockOnCrud,
		DEFAULT_THROW_DEADLOCK_ON_CRUD,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxUserLimit", newGucPrefix),
		gettext_noop("The default number of users allowed."),
		NULL, &MaxUserLimit,
		MAX_USER_LIMIT, 1, 100,
		PGC_SUSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.maxCustomCommandTimeoutLimit", newGucPrefix),
		gettext_noop("The max allowed custom command limit in milliseconds."),
		NULL, &MaxCustomCommandTimeout,
		DEFAULT_MAX_CUSTOM_COMMAND_TIMEOUT, 0, INT_MAX,
		PGC_SUSET,
		0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.tdigestCompressionAccuracy", newGucPrefix),
		gettext_noop("Accuracy parameter of the t-digest compression."),
		gettext_noop(
			"The number of maximum centroid to use in the t-digest. Range from 10 to 10000. The higher the number, the more accurate will be, but higher memory usage."),
		&TdigestCompressionAccuracy,
		DEFAULT_TDIGEST_COMPRESSION_ACCURACY, 10, 10000,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomStringVariable(
		psprintf("%s.blockedRolePrefixList", newGucPrefix),
		gettext_noop("List of role prefixes that are blocked from being created/deleted. "
					 "The list of role prefixes are comma separated."),
		NULL, &BlockedRolePrefixList, DEFAULT_BLOCKED_ROLE_PREFIX_LIST,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomStringVariable(
		psprintf("%s.current_op_application_name", newGucPrefix),
		gettext_noop(
			"Application name that is tracked for current_op. '' means track all"),
		NULL, &CurrentOpApplicationName, DEFAULT_CURRENT_OP_APPLICATION_NAME,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.aggregation_stages_limit", newGucPrefix),
		gettext_noop("The number of maximum aggregation stages allowed in a pipeline."),
		NULL,
		&MaxAggregationStagesAllowed,
		DEFAULT_AGGREGATION_STAGES_LIMIT, DEFAULT_AGGREGATION_STAGES_LIMIT,
		5 * DEFAULT_AGGREGATION_STAGES_LIMIT, /* Ballpark number for max is 5 times, we should rarely need to update it*/
		PGC_USERSET, 0, NULL, NULL, NULL);
}
