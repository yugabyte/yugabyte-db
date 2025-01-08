/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/configs/feature_flag_configs.c
 *
 * Initialization of GUCs that control feature flags that will eventually
 * become defaulted and simply toggle behavior.
 *-------------------------------------------------------------------------
 */

#include <postgres.h>
#include <miscadmin.h>
#include <utils/guc.h>
#include "configs/config_initialization.h"

/* GUCs to enable features that we don't normally allow using for Mongo compatibility */
#define DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS false
bool EnableExtendedIndexFilters = DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS;

#define DEFAULT_ENABLE_IN_QUERY_OPTIMIZATION false
bool EnableInQueryOptimization = DEFAULT_ENABLE_IN_QUERY_OPTIMIZATION;

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

#define DEFAULT_ENABLE_LARGE_UNIQUE_INDEX_KEYS false
bool DefaultEnableLargeUniqueIndexKeys = DEFAULT_ENABLE_LARGE_UNIQUE_INDEX_KEYS;

#define DEFAULT_ENABLE_RUM_INDEX_SCAN false
bool EnableRumIndexScan = DEFAULT_ENABLE_RUM_INDEX_SCAN;

#define DEFAULT_ENABLE_SCHEMA_VALIDATION false
bool EnableSchemaValidation =
	DEFAULT_ENABLE_SCHEMA_VALIDATION;


#define DEFAULT_ENABLE_BYPASSDOCUMENTVALIDATION false
bool EnableBypassDocumentValidation =
	DEFAULT_ENABLE_BYPASSDOCUMENTVALIDATION;

#define DEFAULT_ENABLE_NATIVE_COLOCATION true
bool EnableNativeColocation = DEFAULT_ENABLE_NATIVE_COLOCATION;

#define DEFAULT_ENABLE_NATIVE_TABLE_COLOCATION false
bool EnableNativeTableColocation = DEFAULT_ENABLE_NATIVE_TABLE_COLOCATION;

#define DEFAULT_ENABLE_LET_SUPPORT true
bool EnableLetSupport = DEFAULT_ENABLE_LET_SUPPORT;

#define DEFAULT_ENABLE_LOOKUP_LET_SUPPORT true
bool EnableLookupLetSupport = DEFAULT_ENABLE_LOOKUP_LET_SUPPORT;

#define DEFAULT_ENABLE_LOOKUP_UNWIND_OPTIMIZATION false
bool EnableLookupUnwindSupport = DEFAULT_ENABLE_LOOKUP_UNWIND_OPTIMIZATION;

#define DEFAULT_IGNORE_LET_ON_QUERY false
bool IgnoreLetOnQuerySupport = DEFAULT_IGNORE_LET_ON_QUERY;

#define DEFAULT_ENABLE_INDEX_TERM_TRUNCATION_NESTED_OBJECTS true
bool EnableIndexTermTruncationOnNestedObjects =
	DEFAULT_ENABLE_INDEX_TERM_TRUNCATION_NESTED_OBJECTS;

#define DEFAULT_SKIP_FAIL_ON_COLLATION false
bool SkipFailOnCollation = DEFAULT_SKIP_FAIL_ON_COLLATION;

#define DEFAULT_ENABLE_LOOKUP_ID_JOIN_OPTIMIZATION_ON_COLLATION false
bool EnableLookupIdJoinOptimizationOnCollation =
	DEFAULT_ENABLE_LOOKUP_ID_JOIN_OPTIMIZATION_ON_COLLATION;

#define DEFAULT_ENABLE_FASTPATH_POINTLOOKUP_PLANNER true
bool EnableFastPathPointLookupPlanner =
	DEFAULT_ENABLE_FASTPATH_POINTLOOKUP_PLANNER;

#define DEFAULT_ENABLE_USER_CRUD false
bool EnableUserCrud = DEFAULT_ENABLE_USER_CRUD;

#define DEFAULT_ENABLE_DENSIFY_STAGE true
bool EnableDensifyStage = DEFAULT_ENABLE_DENSIFY_STAGE;

#define DEFAULT_ENABLE_SHARDING_OR_FILTERS true
bool EnableShardingOrFilters = DEFAULT_ENABLE_SHARDING_OR_FILTERS;

#define DEFAULT_ENABLE_NEW_OPERATOR_SELECTIVITY false
bool EnableNewOperatorSelectivityMode = DEFAULT_ENABLE_NEW_OPERATOR_SELECTIVITY;


#define DEFAULT_RECREATE_RETRY_TABLE_ON_SHARDING false
bool RecreateRetryTableOnSharding = DEFAULT_RECREATE_RETRY_TABLE_ON_SHARDING;

#define DEFAULT_ENABLE_MERGE_TARGET_CREATION false
bool EnableMergeTargetCreation = DEFAULT_ENABLE_MERGE_TARGET_CREATION;

#define DEFAULT_ENABLE_MERGE_ACROSS_DB true
bool EnableMergeAcrossDB = DEFAULT_ENABLE_MERGE_ACROSS_DB;

#define DEFAULT_ENABLE_MULTI_INDEX_RUM_JOIN false
bool EnableMultiIndexRumJoin = DEFAULT_ENABLE_MULTI_INDEX_RUM_JOIN;

void
InitializeFeatureFlagConfigurations(const char *prefix, const char *newGucPrefix)
{
	DefineCustomBoolVariable(
		psprintf("%s.enable_extended_index_filters", prefix),
		gettext_noop("Determines whether create_indexes() should allow expressions "
					 "that Mongo doesn't allow using in \"partialFilterExpression\" "
					 "document but postgres could potentially allow."),
		NULL, &EnableExtendedIndexFilters, DEFAULT_ENABLE_EXTENDED_INDEX_FILTERS,
		PGC_USERSET, GUC_NO_SHOW_ALL | GUC_NOT_IN_SAMPLE, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.mongoEnableInQueryOptimization", newGucPrefix),
		gettext_noop("Determines whether in queries are rewritten to equality"),
		NULL,
		&EnableInQueryOptimization,
		DEFAULT_ENABLE_IN_QUERY_OPTIMIZATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableVectorHNSWIndex", prefix),
		gettext_noop(
			"Enables support for HNSW index type and query for vector search in bson documents index."),
		NULL, &EnableVectorHNSWIndex, DEFAULT_ENABLE_VECTOR_HNSW_INDEX,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableVectorPreFilter", prefix),
		gettext_noop(
			"Enables support for vector pre-filtering feature for vector search in bson documents index."),
		NULL, &EnableVectorPreFilter, DEFAULT_ENABLE_VECTOR_PRE_FILTER,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_large_unique_index_keys", newGucPrefix),
		gettext_noop("Whether or not to enable large index keys on unique indexes."),
		NULL, &DefaultEnableLargeUniqueIndexKeys, DEFAULT_ENABLE_LARGE_UNIQUE_INDEX_KEYS,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableNewSelectivityMode", newGucPrefix),
		gettext_noop(
			"Determines whether to use the new selectivity logic."),
		NULL, &EnableNewOperatorSelectivityMode,
		DEFAULT_ENABLE_NEW_OPERATOR_SELECTIVITY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableRumIndexScan", newGucPrefix),
		gettext_noop(
			"Allow rum index scans."),
		NULL,
		&EnableRumIndexScan,
		DEFAULT_ENABLE_RUM_INDEX_SCAN,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableMergeTargetCreation", newGucPrefix),
		gettext_noop(
			"Enables support for target collection creation."),
		NULL, &EnableMergeTargetCreation, DEFAULT_ENABLE_MERGE_TARGET_CREATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableMergeAcrossDB", newGucPrefix),
		gettext_noop(
			"Enables support for merge stage."),
		NULL, &EnableMergeAcrossDB, DEFAULT_ENABLE_MERGE_ACROSS_DB,
		PGC_USERSET, 0, NULL, NULL, NULL);

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

	DefineCustomBoolVariable(
		psprintf("%s.enableBypassDocumentValidation", prefix),
		gettext_noop(
			"Whether or not to support 'bypassDocumentValidation'."),
		NULL,
		&EnableBypassDocumentValidation,
		DEFAULT_ENABLE_BYPASSDOCUMENTVALIDATION,
		PGC_USERSET,
		0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableMultiIndexRumJoin", newGucPrefix),
		gettext_noop(
			"Whether or not to add the cursors on aggregation style queries."),
		NULL,
		&EnableMultiIndexRumJoin,
		DEFAULT_ENABLE_MULTI_INDEX_RUM_JOIN,
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
		psprintf("%s.recreate_retry_table_on_shard", prefix),
		gettext_noop(
			"Gets whether or not to recreate a retry table to match the main table"),
		NULL, &RecreateRetryTableOnSharding, DEFAULT_RECREATE_RETRY_TABLE_ON_SHARDING,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableNativeTableColocation", prefix),
		gettext_noop(
			"Determines whether to turn on colocation of tables across all tables (requires enableNativeColocation to be on)"),
		NULL, &EnableNativeTableColocation, DEFAULT_ENABLE_NATIVE_TABLE_COLOCATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableLetSupport", newGucPrefix),
		gettext_noop(
			"Determines whether to enable support for the let in commands and $lookup"),
		NULL, &EnableLetSupport, DEFAULT_ENABLE_LET_SUPPORT,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableLookupLetSupport", newGucPrefix),
		gettext_noop(
			"Determines whether to enable support for the let in commands and $lookup"),
		NULL, &EnableLookupLetSupport, DEFAULT_ENABLE_LOOKUP_LET_SUPPORT,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableLookupUnwindOptimization", newGucPrefix),
		gettext_noop(
			"Determines whether to enable support for the optimizing $unwind with $lookup prefix"),
		NULL, &EnableLookupUnwindSupport, DEFAULT_ENABLE_LOOKUP_UNWIND_OPTIMIZATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.ignoreLetOnQuerySpec", newGucPrefix),
		gettext_noop(
			"Determines whether to ignore the spec let in commands and $lookup"),
		NULL, &IgnoreLetOnQuerySupport, DEFAULT_IGNORE_LET_ON_QUERY,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableIndexTermTruncationOnNestedObjects", newGucPrefix),
		gettext_noop(
			"Determines whether to truncate index terms with nested objects (arrays/objects of arrays/objects)"),
		NULL, &EnableIndexTermTruncationOnNestedObjects,
		DEFAULT_ENABLE_INDEX_TERM_TRUNCATION_NESTED_OBJECTS,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.skipFailOnCollation", newGucPrefix),
		gettext_noop(
			"Determines whether we can skip failing when collation is specified but collation is not supported"),
		NULL, &SkipFailOnCollation, DEFAULT_SKIP_FAIL_ON_COLLATION,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableLookupIdJoinOptimizationOnCollation", newGucPrefix),
		gettext_noop(
			"Determines whether we can perform _id join opetimization on collation. It would be a customer input confiriming that _id does not contain collation aware data types (i.e., UTF8 and DOCUMENT)."),
		NULL, &EnableLookupIdJoinOptimizationOnCollation,
		DEFAULT_ENABLE_LOOKUP_ID_JOIN_OPTIMIZATION_ON_COLLATION,
		PGC_USERSET, 0, NULL, NULL, NULL);


	DefineCustomBoolVariable(
		psprintf("%s.enableFastPathPointLookupPlanner", newGucPrefix),
		gettext_noop(
			"Determines whether or not the fast path planner for point lookup queries is enabled."),
		NULL, &EnableFastPathPointLookupPlanner,
		DEFAULT_ENABLE_FASTPATH_POINTLOOKUP_PLANNER,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableUserCrud", newGucPrefix),
		gettext_noop(
			"Enables user crud through the data plane."),
		NULL, &EnableUserCrud, DEFAULT_ENABLE_USER_CRUD,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableDensifyStage", newGucPrefix),
		gettext_noop(
			"Enables $densify aggregation stage."),
		NULL, &EnableDensifyStage, DEFAULT_ENABLE_DENSIFY_STAGE,
		PGC_USERSET, 0, NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableShardingOrFilters", newGucPrefix),
		gettext_noop(
			"Whether to enable OR filter based detection for the shard key."),
		NULL, &EnableShardingOrFilters, DEFAULT_ENABLE_SHARDING_OR_FILTERS,
		PGC_USERSET, 0, NULL, NULL, NULL);
}
