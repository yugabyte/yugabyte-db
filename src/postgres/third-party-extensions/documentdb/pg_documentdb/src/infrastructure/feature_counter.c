/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/oss_backend/infrastructure/feature_counter.c
 *
 * Utilities to count and log which feature is being used by a customer
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>
#include <miscadmin.h>
#include <utils/fmgrprotos.h>
#include <fmgr.h>
#include <nodes/execnodes.h>
#include <executor/executor.h>
#include <funcapi.h>

#include <storage/shmem.h>
#include <access/slru.h>
#include <storage/ipc.h>
#include <storage/shmem.h>

#include "io/bson_core.h"
#include "metadata/collection.h"
#include "utils/builtins.h"
#include "utils/feature_counter.h"


#define FEATURE_COUNTER_STATS_COLUMNS 2

static void StoreAllFeatureCounterStats(Tuplestorestate *tupleStore, TupleDesc
										tupleDescriptor, bool resetStatsAfterRead);
static void PopulateFeatureCounters(FeatureCounter *aggregatedFeatureCounter);

static Tuplestorestate * SetupFeatureCounterTuplestore(FunctionCallInfo fcinfo,
													   TupleDesc *tupleDescriptor);


FeatureCounter *FeatureCounterBackendArray = NULL;

/*
 * IMP: Keep the feature enums alphabetically sorted. Sorting is done for better reability.
 * #CodeSync: Keep this in sync with FeatureType enum in feature_counter.h
 *            For each FeatureType enum a FeatureMapping entry should exist.
 */
static char FeatureMapping[MAX_FEATURE_COUNT][MAX_FEATURE_NAME_LENGTH] = {
	/* Feature Mapping region - Aggregation operators */
	[FEATURE_AGG_OPERATOR_ABS] = "agg_operator_abs",
	[FEATURE_AGG_OPERATOR_ACCUMULATOR] = "agg_operator_accumulator",
	[FEATURE_AGG_OPERATOR_ACOS] = "agg_operator_acos",
	[FEATURE_AGG_OPERATOR_ACOSH] = "agg_operator_acosh",
	[FEATURE_AGG_OPERATOR_ADD] = "agg_operator_add",
	[FEATURE_AGG_OPERATOR_ADDTOSET] = "agg_operator_addtoset",
	[FEATURE_AGG_OPERATOR_ALLELEMENTSTRUE] = "agg_operator_allelementstrue",
	[FEATURE_AGG_OPERATOR_AND] = "agg_operator_and",
	[FEATURE_AGG_OPERATOR_ANYELEMENTTRUE] = "agg_operator_anyelementtrue",
	[FEATURE_AGG_OPERATOR_ARRAYELEMAT] = "agg_operator_arrayelemat",
	[FEATURE_AGG_OPERATOR_ARRAYTOOBJECT] = "agg_operator_arraytoobject",
	[FEATURE_AGG_OPERATOR_ASIN] = "agg_operator_asin",
	[FEATURE_AGG_OPERATOR_ASINH] = "agg_operator_asinh",
	[FEATURE_AGG_OPERATOR_ATAN] = "agg_operator_atan",
	[FEATURE_AGG_OPERATOR_ATAN2] = "agg_operator_atan2",
	[FEATURE_AGG_OPERATOR_ATANH] = "agg_operator_atanh",
	[FEATURE_AGG_OPERATOR_AVG] = "agg_operator_avg",
	[FEATURE_AGG_OPERATOR_BINARYSIZE] = "agg_operator_binarysize",
	[FEATURE_AGG_OPERATOR_BITAND] = "agg_operator_bitand",
	[FEATURE_AGG_OPERATOR_BITNOT] = "agg_operator_bitnot",
	[FEATURE_AGG_OPERATOR_BITOR] = "agg_operator_bitor",
	[FEATURE_AGG_OPERATOR_BITXOR] = "agg_operator_bitxor",
	[FEATURE_AGG_OPERATOR_BSONSIZE] = "agg_operator_bsonsize",
	[FEATURE_AGG_OPERATOR_CEIL] = "agg_operator_ceil",
	[FEATURE_AGG_OPERATOR_CMP] = "agg_operator_cmp",
	[FEATURE_AGG_OPERATOR_CONCAT] = "agg_operator_concat",
	[FEATURE_AGG_OPERATOR_CONCATARRAYS] = "agg_operator_concatarrays",
	[FEATURE_AGG_OPERATOR_COND] = "agg_operator_cond",
	[FEATURE_AGG_OPERATOR_CONST] = "agg_operator_const",
	[FEATURE_AGG_OPERATOR_CONVERT] = "agg_operator_convert",
	[FEATURE_AGG_OPERATOR_COS] = "agg_operator_cos",
	[FEATURE_AGG_OPERATOR_COSH] = "agg_operator_cosh",
	[FEATURE_AGG_OPERATOR_DATEADD] = "agg_operator_dateadd",
	[FEATURE_AGG_OPERATOR_DATEDIFF] = "agg_operator_datediff",
	[FEATURE_AGG_OPERATOR_DATESUBTRACT] = "agg_operator_datesubtract",
	[FEATURE_AGG_OPERATOR_DATEFROMPARTS] = "agg_operator_datefromparts",
	[FEATURE_AGG_OPERATOR_DATEFROMSTRING] = "agg_operator_datefromstring",
	[FEATURE_AGG_OPERATOR_DATETOPARTS] = "agg_operator_datetoparts",
	[FEATURE_AGG_OPERATOR_DATETOSTRING] = "agg_operator_datetostring",
	[FEATURE_AGG_OPERATOR_DATETRUNC] = "agg_operator_datetrunc",
	[FEATURE_AGG_OPERATOR_DAYOFMONTH] = "agg_operator_dayofmonth",
	[FEATURE_AGG_OPERATOR_DAYOFWEEK] = "agg_operator_dayofweek",
	[FEATURE_AGG_OPERATOR_DAYOFYEAR] = "agg_operator_dayofyear",
	[FEATURE_AGG_OPERATOR_DEGREESTORADIANS] = "agg_operator_degreestoradians",
	[FEATURE_AGG_OPERATOR_DIVIDE] = "agg_operator_divide",
	[FEATURE_AGG_OPERATOR_EQ] = "agg_operator_eq",
	[FEATURE_AGG_OPERATOR_EXP] = "agg_operator_exp",
	[FEATURE_AGG_OPERATOR_FILTER] = "agg_operator_filter",
	[FEATURE_AGG_OPERATOR_FIRST] = "agg_operator_first",
	[FEATURE_AGG_OPERATOR_FIRSTN] = "agg_operator_firstN",
	[FEATURE_AGG_OPERATOR_FLOOR] = "agg_operator_floor",
	[FEATURE_AGG_OPERATOR_FUNCTION] = "agg_operator_function",
	[FEATURE_AGG_OPERATOR_GETFIELD] = "agg_operator_getfield",
	[FEATURE_AGG_OPERATOR_GT] = "agg_operator_gt",
	[FEATURE_AGG_OPERATOR_GTE] = "agg_operator_gte",
	[FEATURE_AGG_OPERATOR_HOUR] = "agg_operator_hour",
	[FEATURE_AGG_OPERATOR_IFNULL] = "agg_operator_ifnull",
	[FEATURE_AGG_OPERATOR_IN] = "agg_operator_in",
	[FEATURE_AGG_OPERATOR_INDEXOFARRAY] = "agg_operator_indexofarray",
	[FEATURE_AGG_OPERATOR_INDEXOFBYTES] = "agg_operator_indexofbytes",
	[FEATURE_AGG_OPERATOR_INDEXOFCP] = "agg_operator_indexofcp",
	[FEATURE_AGG_OPERATOR_ISARRAY] = "agg_operator_isarray",
	[FEATURE_AGG_OPERATOR_ISNUMBER] = "agg_operator_isnumber",
	[FEATURE_AGG_OPERATOR_ISODAYOFWEEK] = "agg_operator_isodayofweek",
	[FEATURE_AGG_OPERATOR_ISOWEEK] = "agg_operator_isoweek",
	[FEATURE_AGG_OPERATOR_ISOWEEKYEAR] = "agg_operator_isoweekyear",
	[FEATURE_AGG_OPERATOR_LAST] = "agg_operator_last",
	[FEATURE_AGG_OPERATOR_LASTN] = "agg_operator_lastN",
	[FEATURE_AGG_OPERATOR_LET] = "agg_operator_let",
	[FEATURE_AGG_OPERATOR_LITERAL] = "agg_operator_literal",
	[FEATURE_AGG_OPERATOR_LN] = "agg_operator_ln",
	[FEATURE_AGG_OPERATOR_LOG] = "agg_operator_log",
	[FEATURE_AGG_OPERATOR_LOG10] = "agg_operator_log10",
	[FEATURE_AGG_OPERATOR_LT] = "agg_operator_lt",
	[FEATURE_AGG_OPERATOR_LTE] = "agg_operator_lte",
	[FEATURE_AGG_OPERATOR_LTRIM] = "agg_operator_ltrim",
	[FEATURE_AGG_OPERATOR_MAKE_ARRAY] = "agg_operator_makearray",
	[FEATURE_AGG_OPERATOR_MAP] = "agg_operator_map",
	[FEATURE_AGG_OPERATOR_MAX] = "agg_operator_max",
	[FEATURE_AGG_OPERATOR_MAXN] = "agg_operator_maxn",
	[FEATURE_AGG_OPERATOR_MERGEOBJECTS] = "agg_operator_mergeobjects",
	[FEATURE_AGG_OPERATOR_META] = "agg_operator_meta",
	[FEATURE_AGG_OPERATOR_MILLISECOND] = "agg_operator_millisecond",
	[FEATURE_AGG_OPERATOR_MIN] = "agg_operator_min",
	[FEATURE_AGG_OPERATOR_MINN] = "agg_operator_minn",
	[FEATURE_AGG_OPERATOR_MINUTE] = "agg_operator_minute",
	[FEATURE_AGG_OPERATOR_MOD] = "agg_operator_mod",
	[FEATURE_AGG_OPERATOR_MONTH] = "agg_operator_month",
	[FEATURE_AGG_OPERATOR_MULTIPLY] = "agg_operator_multiply",
	[FEATURE_AGG_OPERATOR_NE] = "agg_operator_ne",
	[FEATURE_AGG_OPERATOR_NOT] = "agg_operator_not",
	[FEATURE_AGG_OPERATOR_OBJECTTOARRAY] = "agg_operator_objecttoarray",
	[FEATURE_AGG_OPERATOR_OR] = "agg_operator_or",
	[FEATURE_AGG_OPERATOR_POW] = "agg_operator_pow",
	[FEATURE_AGG_OPERATOR_PUSH] = "agg_operator_push",
	[FEATURE_AGG_OPERATOR_RADIANSTODEGREES] = "agg_operator_radianstodegrees",
	[FEATURE_AGG_OPERATOR_RAND] = "agg_operator_rand",
	[FEATURE_AGG_OPERATOR_RANGE] = "agg_operator_range",
	[FEATURE_AGG_OPERATOR_REDUCE] = "agg_operator_reduce",
	[FEATURE_AGG_OPERATOR_REGEXFIND] = "agg_operator_regexfind",
	[FEATURE_AGG_OPERATOR_REGEXFINDALL] = "agg_operator_regexfindall",
	[FEATURE_AGG_OPERATOR_REGEXMATCH] = "agg_operator_regexmatch",
	[FEATURE_AGG_OPERATOR_REPLACEONE] = "agg_operator_replaceone",
	[FEATURE_AGG_OPERATOR_REPLACEALL] = "agg_operator_replaceall",
	[FEATURE_AGG_OPERATOR_REVERSEARRAY] = "agg_operator_reversearray",
	[FEATURE_AGG_OPERATOR_ROUND] = "agg_operator_round",
	[FEATURE_AGG_OPERATOR_RTRIM] = "agg_operator_rtrim",
	[FEATURE_AGG_OPERATOR_SECOND] = "agg_operator_second",
	[FEATURE_AGG_OPERATOR_SETDIFFERENCE] = "agg_operator_setdifference",
	[FEATURE_AGG_OPERATOR_SETEQUALS] = "agg_operator_setequals",
	[FEATURE_AGG_OPERATOR_SETFIELD] = "agg_operator_setfield",
	[FEATURE_AGG_OPERATOR_SETINTERSECTION] = "agg_operator_setintersection",
	[FEATURE_AGG_OPERATOR_SETISSUBSET] = "agg_operator_setissubset",
	[FEATURE_AGG_OPERATOR_SETUNION] = "agg_operator_setunion",
	[FEATURE_AGG_OPERATOR_SIN] = "agg_operator_sin",
	[FEATURE_AGG_OPERATOR_SINH] = "agg_operator_sinh",
	[FEATURE_AGG_OPERATOR_SIZE] = "agg_operator_size",
	[FEATURE_AGG_OPERATOR_SLICE] = "agg_operator_slice",
	[FEATURE_AGG_OPERATOR_SORTARRAY] = "agg_operator_sortarray",
	[FEATURE_AGG_OPERATOR_SPLIT] = "agg_operator_split",
	[FEATURE_AGG_OPERATOR_SQRT] = "agg_operator_sqrt",
	[FEATURE_AGG_OPERATOR_STDDEVPOP] = "agg_operator_stddevpop",
	[FEATURE_AGG_OPERATOR_STDDEVSAMP] = "agg_operator_stddevsamp",
	[FEATURE_AGG_OPERATOR_STRLENBYTES] = "agg_operator_strlenbytes",
	[FEATURE_AGG_OPERATOR_STRLENCP] = "agg_operator_strlencp",
	[FEATURE_AGG_OPERATOR_STRCASECMP] = "agg_operator_strcasecmp",
	[FEATURE_AGG_OPERATOR_SUBSTR] = "agg_operator_substr",
	[FEATURE_AGG_OPERATOR_SUBSTRBYTES] = "agg_operator_substrbytes",
	[FEATURE_AGG_OPERATOR_SUBSTRCP] = "agg_operator_substrcp",
	[FEATURE_AGG_OPERATOR_SUBTRACT] = "agg_operator_subtract",
	[FEATURE_AGG_OPERATOR_SUM] = "agg_operator_sum",
	[FEATURE_AGG_OPERATOR_SWITCH] = "agg_operator_switch",
	[FEATURE_AGG_OPERATOR_TAN] = "agg_operator_tan",
	[FEATURE_AGG_OPERATOR_TANH] = "agg_operator_tanh",
	[FEATURE_AGG_OPERATOR_TOBOOL] = "agg_operator_tobool",
	[FEATURE_AGG_OPERATOR_TODATE] = "agg_operator_todate",
	[FEATURE_AGG_OPERATOR_TODECIMAL] = "agg_operator_todecimal",
	[FEATURE_AGG_OPERATOR_TODOUBLE] = "agg_operator_todouble",
	[FEATURE_AGG_OPERATOR_TOHASHEDINDEXKEY] = "agg_operator_tohashedindexkey",
	[FEATURE_AGG_OPERATOR_TOINT] = "agg_operator_toint",
	[FEATURE_AGG_OPERATOR_TOLONG] = "agg_operator_tolong",
	[FEATURE_AGG_OPERATOR_TOLOWER] = "agg_operator_tolower",
	[FEATURE_AGG_OPERATOR_TOOBJECTID] = "agg_operator_toobjectid",
	[FEATURE_AGG_OPERATOR_TOSTRING] = "agg_operator_tostring",
	[FEATURE_AGG_OPERATOR_TOUPPER] = "agg_operator_toupper",
	[FEATURE_AGG_OPERATOR_TOUUID] = "agg_operator_touuid",
	[FEATURE_AGG_OPERATOR_TRIM] = "agg_operator_trim",
	[FEATURE_AGG_OPERATOR_TRUNC] = "agg_operator_trunc",
	[FEATURE_AGG_OPERATOR_TSINCREMENT] = "agg_operator_tsincrement",
	[FEATURE_AGG_OPERATOR_TSSECOND] = "agg_operator_tssecond",
	[FEATURE_AGG_OPERATOR_TYPE] = "agg_operator_type",
	[FEATURE_AGG_OPERATOR_UNSETFIELD] = "agg_operator_unsetfield",
	[FEATURE_AGG_OPERATOR_WEEK] = "agg_operator_week",
	[FEATURE_AGG_OPERATOR_YEAR] = "agg_operator_year",
	[FEATURE_AGG_OPERATOR_ZIP] = "agg_operator_zip",

	/* Feature counter region - $group operators */
	[FEATURE_AGGREGATE_GROUP_ADD_TO_SET] = "group_add_to_set",
	[FEATURE_AGGREGATE_GROUP_AVG] = "group_avg",
	[FEATURE_AGGREGATE_GROUP_BOTTOM] = "group_bottom",
	[FEATURE_AGGREGATE_GROUP_BOTTOM_N] = "group_bottom_n",
	[FEATURE_AGGREGATE_GROUP_CONCAT_ARRAYS] = "group_concat_arrays",
	[FEATURE_AGGREGATE_GROUP_COUNT] = "group_count",
	[FEATURE_AGGREGATE_GROUP_FIRST] = "group_first",
	[FEATURE_AGGREGATE_GROUP_FIRST_N] = "group_first_n",
	[FEATURE_AGGREGATE_GROUP_LAST] = "group_last",
	[FEATURE_AGGREGATE_GROUP_LAST_N] = "group_last_n",
	[FEATURE_AGGREGATE_GROUP_MAX] = "group_max",
	[FEATURE_AGGREGATE_GROUP_MAX_N] = "group_max_n",
	[FEATURE_AGGREGATE_GROUP_MEDIAN] = "group_median",
	[FEATURE_AGGREGATE_GROUP_MERGE_OBJECTS] = "group_merge_objects",
	[FEATURE_AGGREGATE_GROUP_MIN] = "group_min",
	[FEATURE_AGGREGATE_GROUP_MIN_N] = "group_min_n",
	[FEATURE_AGGREGATE_GROUP_PERCENTILE] = "group_percentile",
	[FEATURE_AGGREGATE_GROUP_PUSH] = "group_push",
	[FEATURE_AGGREGATE_GROUP_STDDEV_POP] = "group_stddev_pop",
	[FEATURE_AGGREGATE_GROUP_STDDEV_SAMP] = "group_stddev_samp",
	[FEATURE_AGGREGATE_GROUP_SUM] = "group_sum",
	[FEATURE_AGGREGATE_GROUP_TOP] = "group_top",
	[FEATURE_AGGREGATE_GROUP_TOP_N] = "group_top_n",

	/* Feature counter region - $setWindowFields operators */
	[FEATURE_AGGREGATE_WINDOW_ADD_TO_SET] = "window_add_to_set",
	[FEATURE_AGGREGATE_WINDOW_AVG] = "window_avg",
	[FEATURE_AGGREGATE_WINDOW_BOTTOM] = "window_bottom",
	[FEATURE_AGGREGATE_WINDOW_BOTTOM_N] = "window_bottom_n",
	[FEATURE_AGGREGATE_WINDOW_CONCAT_ARRAYS] = "window_concat_arrays",
	[FEATURE_AGGREGATE_WINDOW_COUNT] = "window_count",
	[FEATURE_AGGREGATE_WINDOW_COVARIANCE_POP] = "window_covariance_pop",
	[FEATURE_AGGREGATE_WINDOW_COVARIANCE_SAMP] = "window_covariance_samp",
	[FEATURE_AGGREGATE_WINDOW_DENSE_RANK] = "window_dense_rank",
	[FEATURE_AGGREGATE_WINDOW_DERIVATIVE] = "window_derivative",
	[FEATURE_AGGREGATE_WINDOW_DOCUMENT_NUMBER] = "window_document_number",
	[FEATURE_AGGREGATE_WINDOW_EXP_MOVING_AVG] = "window_exp_moving_avg",
	[FEATURE_AGGREGATE_WINDOW_FIRST] = "window_first",
	[FEATURE_AGGREGATE_WINDOW_FIRST_N] = "window_first_n",
	[FEATURE_AGGREGATE_WINDOW_INTEGRAL] = "window_integral",
	[FEATURE_AGGREGATE_WINDOW_LAST] = "window_last",
	[FEATURE_AGGREGATE_WINDOW_LAST_N] = "window_last_n",
	[FEATURE_AGGREGATE_WINDOW_LINEAR_FILL] = "window_linear_fill",
	[FEATURE_AGGREGATE_WINDOW_LOCF] = "window_locf",
	[FEATURE_AGGREGATE_WINDOW_MAX] = "window_max",
	[FEATURE_AGGREGATE_WINDOW_MAX_N] = "window_max_n",
	[FEATURE_AGGREGATE_WINDOW_MEDIAN] = "window_median",
	[FEATURE_AGGREGATE_WINDOW_MIN] = "window_min",
	[FEATURE_AGGREGATE_WINDOW_MIN_N] = "window_min_n",
	[FEATURE_AGGREGATE_WINDOW_PERCENTILE] = "window_percentile",
	[FEATURE_AGGREGATE_WINDOW_PUSH] = "window_push",
	[FEATURE_AGGREGATE_WINDOW_RANK] = "window_rank",
	[FEATURE_AGGREGATE_WINDOW_SHIFT] = "window_shift",
	[FEATURE_AGGREGATE_WINDOW_STDDEV_POP] = "window_stddev_pop",
	[FEATURE_AGGREGATE_WINDOW_STDDEV_SAMP] = "window_stddev_samp",
	[FEATURE_AGGREGATE_WINDOW_SUM] = "window_sum",
	[FEATURE_AGGREGATE_WINDOW_TOP] = "window_top",
	[FEATURE_AGGREGATE_WINDOW_TOP_N] = "window_top_n",

	/* Find/Aggregate using collation */
	[FEATURE_COLLATION] = "collation",

	/* Feature Mapping region - Commands */
	[FEATURE_COMMAND_AGG_CURSOR_FIRST_PAGE] = "command_agg_cursor_first_page",
	[FEATURE_COMMAND_COLLMOD] = "command_collmod",
	[FEATURE_COMMAND_COLLSTATS] = "command_collstats",
	[FEATURE_COMMAND_COMPACT] = "command_compact",
	[FEATURE_COMMAND_COUNT] = "command_count",
	[FEATURE_COMMAND_CREATE_COLLECTION] = "command_create_collection",
	[FEATURE_COMMAND_CREATE_VALIDATION] = "command_create_validation",
	[FEATURE_COMMAND_CREATE_VIEW] = "command_create_view",
	[FEATURE_COMMAND_CURRENTOP] = "command_current_op",
	[FEATURE_COMMAND_DBSTATS] = "command_dbstats",
	[FEATURE_COMMAND_DELETE] = "command_delete",
	[FEATURE_COMMAND_DISTINCT] = "command_distinct",
	[FEATURE_COMMAND_FINDANDMODIFY] = "command_findAndModify",
	[FEATURE_COMMAND_FIND_CURSOR_FIRST_PAGE] = "command_find_cursor_first_page",
	[FEATURE_COMMAND_GET_MORE] = "command_get_more",
	[FEATURE_COMMAND_INSERT] = "command_insert",
	[FEATURE_COMMAND_INSERT_ONE] = "command_insert_one",
	[FEATURE_COMMAND_INSERT_100] = "command_insert_100",
	[FEATURE_COMMAND_INSERT_500] = "command_insert_500",
	[FEATURE_COMMAND_INSERT_1000] = "command_insert_1000",
	[FEATURE_COMMAND_INSERT_EXTENDED] = "command_insert_extended",
	[FEATURE_COMMAND_INSERT_BULK] = "command_insert_bulk",
	[FEATURE_COMMAND_LIST_COLLECTIONS_CURSOR_FIRST_PAGE] =
		"command_list_collections_cursor_first_page",
	[FEATURE_COMMAND_LIST_DATABASES] =
		"command_list_databases",
	[FEATURE_COMMAND_LIST_INDEXES_CURSOR_FIRST_PAGE] =
		"command_list_indexes_cursor_first_page",
	[FEATURE_COMMAND_SHARD_COLLECTION] = "command_shard_collection",
	[FEATURE_COMMAND_RESHARD_COLLECTION] = "command_reshard_collection",
	[FEATURE_COMMAND_UNSHARD_COLLECTION] = "command_unshard_collection",
	[FEATURE_COMMAND_UPDATE] = "command_update",
	[FEATURE_COMMAND_UPDATE_ONE] = "command_update_one",
	[FEATURE_COMMAND_UPDATE_100] = "command_update_100",
	[FEATURE_COMMAND_UPDATE_500] = "command_update_500",
	[FEATURE_COMMAND_UPDATE_1000] = "command_update_1000",
	[FEATURE_COMMAND_UPDATE_EXTENDED] = "command_update_extended",
	[FEATURE_COMMAND_UPDATE_BULK] = "command_update_bulk",
	[FEATURE_COMMAND_VALIDATE_REPAIR] = "validate_repair",

	[FEATURE_COMMAND_COLLMOD_VIEW] = "collMod_view",
	[FEATURE_COMMAND_COLLMOD_COLOCATION] = "collMod_colocation",
	[FEATURE_COMMAND_COLLMOD_VALIDATION] = "collMod_validation",
	[FEATURE_COMMAND_COLLMOD_TTL_UPDATE] = "collMod_ttl_update",
	[FEATURE_COMMAND_COLLMOD_INDEX_HIDDEN] = "collMod_index_hidden",
	[FEATURE_COMMAND_COLLMOD_INDEX_PREPARE_UNIQUE] = "collMod_index_prepare_unique",
	[FEATURE_COMMAND_COLLMOD_UNIQUE] = "collMod_index_unique",

	/* Feature Connection Status */
	[FEATURE_CONNECTION_STATUS] = "connection_status",

	/* Feature Mapping region - Create index types */
	[FEATURE_CREATE_INDEX_2D] = "create_index_2d",
	[FEATURE_CREATE_INDEX_2DSPHERE] = "create_index_2dsphere",
	[FEATURE_CREATE_INDEX_ALTERNATE_AM] = "create_index_alternate_am",
	[FEATURE_CREATE_INDEX_COMPOSITE_BASED_TERM] = "create_index_composite_based_term",
	[FEATURE_CREATE_INDEX_FTS] = "create_index_fts",
	[FEATURE_CREATE_INDEX_TEXT] = "create_index_text",
	[FEATURE_CREATE_INDEX_TTL] = "create_index_ttl",
	[FEATURE_CREATE_INDEX_UNIQUE] = "create_index_unique",
	[FEATURE_CREATE_INDEX_BUILD_AS_UNIQUE] = "create_index_build_as_unique",
	[FEATURE_CREATE_INDEX_VECTOR] = "create_index_vector",
	[FEATURE_CREATE_INDEX_VECTOR_COS] = "create_index_vector_cos",
	[FEATURE_CREATE_INDEX_VECTOR_COMPRESSION_HALF] =
		"create_index_vector_compression_half",
	[FEATURE_CREATE_INDEX_VECTOR_COMPRESSION_PQ] = "create_index_vector_compression_pq",
	[FEATURE_CREATE_INDEX_VECTOR_IP] = "create_index_vector_ip",
	[FEATURE_CREATE_INDEX_VECTOR_L2] = "create_index_vector_l2",
	[FEATURE_CREATE_INDEX_VECTOR_TYPE_DISKANN] = "create_index_vector_type_diskann",
	[FEATURE_CREATE_INDEX_VECTOR_TYPE_HNSW] = "create_index_vector_type_hnsw",
	[FEATURE_CREATE_INDEX_VECTOR_TYPE_IVFFLAT] = "create_index_vector_type_ivfflat",
	[FEATURE_CREATE_UNIQUE_INDEX_WITH_TERM_TRUNCATION] =
		"create_unique_index_with_term_truncation",

	/* Feature Mapping region - Cursor types */
	[FEATURE_CURSOR_TYPE_PERSISTENT] = "cursor_type_persistent",
	[FEATURE_CURSOR_TYPE_POINT_READ] = "cursor_type_point_read",
	[FEATURE_CURSOR_TYPE_SINGLE_BATCH] = "cursor_type_single_batch",
	[FEATURE_CURSOR_TYPE_STREAMING] = "cursor_type_streaming",
	[FEATURE_CURSOR_TYPE_TAILABLE] = "cursor_type_tailable",
	[FEATURE_CURSOR_CAN_USE_PRIMARY_KEY_SCAN] = "cursor_can_use_primary_key_scan",

	/* Feature mapping region - ExternalIdentityProvider */
	[FEATURE_EXTERNAL_IDENTITY_USER_CREATE] = "external_identity_user_create",
	[FEATURE_EXTERNAL_IDENTITY_USER_DROP] = "external_identity_user_drop",
	[FEATURE_EXTERNAL_IDENTITY_USER_AUTHENTICATE] = "external_identity_user_authenticate",
	[FEATURE_EXTERNAL_IDENTITY_USER_GET] = "external_identity_user_get",

	[FEATURE_INDEX_AM_PREREGISTERED] = "index_am_preregistered",
	[FEATURE_INDEX_HINT] = "index_hint",

	/* Feature counter region - Top-level let support */
	[FEATURE_LET_TOP_LEVEL] = "let_top_level",

	/* Feature Mapping region - Query Operators */
	[FEATURE_QUERY_OPERATOR_ALL] = "query_operator_all",
	[FEATURE_QUERY_OPERATOR_ALWAYSFALSE] = "query_operator_alwaysfalse",
	[FEATURE_QUERY_OPERATOR_ALWAYSTRUE] = "query_operator_alwaystrue",
	[FEATURE_QUERY_OPERATOR_AND] = "query_operator_and",
	[FEATURE_QUERY_OPERATOR_BITS_ALL_CLEAR] = "query_operator_bitsallclear",
	[FEATURE_QUERY_OPERATOR_BITS_ALL_SET] = "query_operator_bitsallset",
	[FEATURE_QUERY_OPERATOR_BITS_ANY_CLEAR] = "query_operator_bitsanyclear",
	[FEATURE_QUERY_OPERATOR_BITS_ANY_SET] = "query_operator_bitsanyset",
	[FEATURE_QUERY_OPERATOR_COMMENT] = "query_operator_comment",
	[FEATURE_QUERY_OPERATOR_ELEMMATCH] = "query_operator_elemmatch",
	[FEATURE_QUERY_OPERATOR_EQ] = "query_operator_eq",
	[FEATURE_QUERY_OPERATOR_EXISTS] = "query_operator_exists",
	[FEATURE_QUERY_OPERATOR_EXPR] = "query_operator_expr",
	[FEATURE_QUERY_OPERATOR_GEOINTERSECTS] = "query_operator_geointersects",
	[FEATURE_QUERY_OPERATOR_GEONEAR] = "query_operator_geonear",
	[FEATURE_QUERY_OPERATOR_GEOWITHIN] = "query_operator_geowithin",
	[FEATURE_QUERY_OPERATOR_GT] = "query_operator_gt",
	[FEATURE_QUERY_OPERATOR_GTE] = "query_operator_gte",
	[FEATURE_QUERY_OPERATOR_IN] = "query_operator_in",
	[FEATURE_QUERY_OPERATOR_JSONSCHEMA] = "query_operator_jsonschema",
	[FEATURE_QUERY_OPERATOR_LT] = "query_operator_lt",
	[FEATURE_QUERY_OPERATOR_LTE] = "query_operator_lte",
	[FEATURE_QUERY_OPERATOR_MOD] = "query_operator_mod",
	[FEATURE_QUERY_OPERATOR_NE] = "query_operator_ne",
	[FEATURE_QUERY_OPERATOR_NEAR] = "query_operator_near",
	[FEATURE_QUERY_OPERATOR_NEARSPHERE] = "query_operator_nearsphere",
	[FEATURE_QUERY_OPERATOR_NIN] = "query_operator_nin",
	[FEATURE_QUERY_OPERATOR_NOR] = "query_operator_nor",
	[FEATURE_QUERY_OPERATOR_NOT] = "query_operator_not",
	[FEATURE_QUERY_OPERATOR_OR] = "query_operator_or",
	[FEATURE_QUERY_OPERATOR_REGEX] = "query_operator_regex",
	[FEATURE_QUERY_OPERATOR_SAMPLERATE] = "query_operator_samplerate",
	[FEATURE_QUERY_OPERATOR_SIZE] = "query_operator_size",
	[FEATURE_QUERY_OPERATOR_TEXT] = "query_operator_text",
	[FEATURE_QUERY_OPERATOR_TYPE] = "query_operator_type",

	/* Feature mapping region - Role CRUD */
	[FEATURE_ROLE_CREATE] = "role_create",

	/* Feature Mapping region - Aggregation stages */
	[FEATURE_STAGE_ADD_FIELDS] = "add_fields",
	[FEATURE_STAGE_BUCKET] = "bucket",
	[FEATURE_STAGE_BUCKET_AUTO] = "bucket_auto",
	[FEATURE_STAGE_COLLSTATS] = "collstats_agg",
	[FEATURE_STAGE_COUNT] = "count",
	[FEATURE_STAGE_CHANGE_STREAM] = "change_stream",
	[FEATURE_STAGE_CURRENTOP] = "current_op_agg",
	[FEATURE_STAGE_DENSIFY] = "densify",
	[FEATURE_STAGE_DOCUMENTS] = "documents_agg",
	[FEATURE_STAGE_FACET] = "facet",
	[FEATURE_STAGE_FILL] = "fill",
	[FEATURE_STAGE_GEONEAR] = "geo_near",
	[FEATURE_STAGE_GRAPH_LOOKUP] = "graphLookup",
	[FEATURE_STAGE_GROUP] = "group",
	[FEATURE_STAGE_GROUP_ACC_FIRSTN_GT10] = "firstN_acc_GT10",
	[FEATURE_STAGE_GROUP_ACC_LASTN_GT10] = "lastN_acc_GT10",
	[FEATURE_STAGE_INDEXSTATS] = "indexStats",
	[FEATURE_STAGE_INTERNAL_INHIBIT_OPTIMIZATION] = "_internalInhibitOptimization",
	[FEATURE_STAGE_INVERSEMATCH] = "inverseMatch",
	[FEATURE_STAGE_LIMIT] = "limit",
	[FEATURE_STAGE_LOOKUP] = "lookup",
	[FEATURE_STAGE_MATCH] = "match",
	[FEATURE_STAGE_MERGE] = "merge",
	[FEATURE_STAGE_OUT] = "out",
	[FEATURE_STAGE_PROJECT] = "project",
	[FEATURE_STAGE_PROJECT_FIND] = "project_find",
	[FEATURE_STAGE_REDACT] = "redact",
	[FEATURE_STAGE_REPLACE_ROOT] = "replace_root",
	[FEATURE_STAGE_REPLACE_WITH] = "replace_with",
	[FEATURE_STAGE_SAMPLE] = "sample",
	[FEATURE_STAGE_SEARCH] = "search",
	[FEATURE_STAGE_SEARCH_VECTOR] = "search_vector",
	[FEATURE_STAGE_SEARCH_VECTOR_COMPRESSION_HALF] = "search_vector_compression_half",
	[FEATURE_STAGE_SEARCH_VECTOR_COMPRESSION_PQ] = "search_vector_compression_pq",
	[FEATURE_STAGE_SEARCH_VECTOR_DEFAULT_NPROBES] = "search_vector_default_nprobes",
	[FEATURE_STAGE_SEARCH_VECTOR_DEFAULT_EFSEARCH] = "search_vector_default_efsearch",
	[FEATURE_STAGE_SEARCH_VECTOR_DEFAULT_LSEARCH] = "search_vector_default_lsearch",
	[FEATURE_STAGE_SEARCH_VECTOR_DISKANN] = "search_vector_diskann",
	[FEATURE_STAGE_SEARCH_VECTOR_EXACT] = "search_vector_exact",
	[FEATURE_STAGE_SEARCH_VECTOR_GEN_EMBEDDINGS] = "search_vector_gen_embeddings",
	[FEATURE_STAGE_SEARCH_VECTOR_HNSW] = "search_vector_hnsw",
	[FEATURE_STAGE_SEARCH_VECTOR_IVFFLAT] = "search_vector_ivfflat",
	[FEATURE_STAGE_SEARCH_VECTOR_PRE_FILTER] = "search_vector_pre_filter",
	[FEATURE_STAGE_SET] = "set",
	[FEATURE_STAGE_SETWINDOWFIELDS] = "setWindowFields",
	[FEATURE_STAGE_SKIP] = "skip",
	[FEATURE_STAGE_SORT] = "sort",
	[FEATURE_STAGE_SORT_BY_COUNT] = "sort_by_count",
	[FEATURE_STAGE_SORT_BY_ID] = "sort_by_id",
	[FEATURE_STAGE_UNIONWITH] = "unionWith",
	[FEATURE_STAGE_UNSET] = "unset",
	[FEATURE_STAGE_UNWIND] = "unwind",
	[FEATURE_STAGE_VECTOR_SEARCH_KNN] = "vector_search_knn",
	[FEATURE_STAGE_VECTOR_SEARCH_NATIVE] = "vector_search_native",

	/* Feature Mapping region - Update operators */
	[FEATURE_UPDATE_OPERATOR_ADDTOSET] = "update_operator_addtoset",
	[FEATURE_UPDATE_OPERATOR_BIT] = "update_operator_bit",
	[FEATURE_UPDATE_OPERATOR_CURRENTDATE] = "update_operator_currentdate",
	[FEATURE_UPDATE_OPERATOR_GEN_EMBEDDINGS] = "update_operator_vector_gen_embeddings",
	[FEATURE_UPDATE_OPERATOR_INC] = "update_operator_inc",
	[FEATURE_UPDATE_OPERATOR_MAX] = "update_operator_max",
	[FEATURE_UPDATE_OPERATOR_MIN] = "update_operator_min",
	[FEATURE_UPDATE_OPERATOR_MUL] = "update_operator_mul",
	[FEATURE_UPDATE_OPERATOR_POP] = "update_operator_pop",
	[FEATURE_UPDATE_OPERATOR_PULL] = "update_operator_pull",
	[FEATURE_UPDATE_OPERATOR_PULLALL] = "update_operator_pullall",
	[FEATURE_UPDATE_OPERATOR_PUSH] = "update_operator_push",
	[FEATURE_UPDATE_OPERATOR_RENAME] = "update_operator_rename",
	[FEATURE_UPDATE_OPERATOR_SET] = "update_operator_set",
	[FEATURE_UPDATE_OPERATOR_SET_ON_INSERT] = "update_operator_set_on_insert",
	[FEATURE_UPDATE_OPERATOR_UNSET] = "update_operator_unset",

	/* Feature usage stats */
	[FEATURE_USAGE_TTL_PURGER_CALLS] = "ttl_purger_calls",
	[FEATURE_USAGE_TTL_SATURATED_BATCHES] = "ttl_saturated_batches",
	[FEATURE_USAGE_TTL_SLOW_BATCHES] = "ttl_slow_batches",

	/* Feature mapping region - User CRUD*/
	[FEATURE_USER_CREATE] = "user_create",
	[FEATURE_USER_DROP] = "user_drop",
	[FEATURE_USER_GET] = "user_get",
	[FEATURE_USER_UPDATE] = "user_update",
};


PG_FUNCTION_INFO_V1(get_feature_counter_stats);

/*
 * get_feature_counter_stats returns all the available information about all
 * the documentdb_api features that have been requested.
 */
Datum
get_feature_counter_stats(PG_FUNCTION_ARGS)
{
	bool resetStatsAfterRead = PG_GETARG_BOOL(0);
	TupleDesc tupleDescriptor = NULL;
	Tuplestorestate *tupleStore = SetupFeatureCounterTuplestore(fcinfo, &tupleDescriptor);

	StoreAllFeatureCounterStats(tupleStore, tupleDescriptor, resetStatsAfterRead);

	PG_RETURN_VOID();
}


Size
SharedFeatureCounterShmemSize(void)
{
	return mul_size(sizeof(FeatureCounter), MaxBackends);
}


/*
 * SharedFeatureCounterShmemInit initializes the shared memory used
 * for keeping track of feature counters across backends.
 */
void
SharedFeatureCounterShmemInit(void)
{
	StaticAssertExpr(MAX_FEATURE_INDEX < MAX_FEATURE_COUNT,
					 "feature enums should be less than size - bump up MAX_FEATURE_COUNT");

	/* Validate that we have names for the feature counters as well */
	for (int i = 0; i < MAX_FEATURE_INDEX; i++)
	{
		if (strlen(FeatureMapping[i]) == 0)
		{
			ereport(PANIC, (errmsg("Feature mapping for index %d not found", i)));
		}
	}

	bool found;

	size_t feature_counter_shmem_size = SharedFeatureCounterShmemSize();
	FeatureCounterBackendArray = (FeatureCounter *)
								 ShmemInitStruct("Feature Counter Array",
												 feature_counter_shmem_size, &found);

	if (!found)
	{
		/*
		 * We're the first - initialize.
		 */
		MemSet(FeatureCounterBackendArray, 0, feature_counter_shmem_size);
	}
}


const char *
GetFeatureCountersAsString(void)
{
	FeatureCounter aggregatedFeatureCounter;
	PopulateFeatureCounters(&aggregatedFeatureCounter);

	/* Format: [{"match":1},{"sort":1}] */
	StringInfo stringInfo = makeStringInfo();
	appendStringInfo(stringInfo, "[");
	const char *separator = "";
	bool hasValues = false;
	for (int i = 0; i < MAX_FEATURE_COUNT; i++)
	{
		if (aggregatedFeatureCounter[i] == 0)
		{
			continue;
		}

		appendStringInfo(stringInfo, "%s{ \"%s\": %d }", separator, FeatureMapping[i],
						 aggregatedFeatureCounter[i]);
		separator = ", ";
		hasValues = true;
	}
	appendStringInfo(stringInfo, "]");

	return hasValues ? stringInfo->data : NULL;
}


/*
 * Resets feature counter state.
 */
void
ResetFeatureCounters(void)
{
	/*
	 *  Some usage metrics might be lost between reading the stats for all the backend processes and resetting the stats.
	 *  However, we are okay with this as we aim to minimize the lock time.
	 */
	size_t feature_counter_shmem_size = mul_size(sizeof(FeatureCounter), MaxBackends);
	pg_write_barrier();
	MemSet(FeatureCounterBackendArray, 0, feature_counter_shmem_size);
}


static void
PopulateFeatureCounters(FeatureCounter *aggregatedFeatureCounter)
{
	MemSet(*aggregatedFeatureCounter, 0, sizeof(FeatureCounter));

	pg_memory_barrier();
	for (int i = 0; i < MaxBackends; i++)
	{
		for (int j = 0; j < MAX_FEATURE_COUNT; j++)
		{
			(*aggregatedFeatureCounter)[j] += FeatureCounterBackendArray[i][j];
		}
	}
}


/*
 *  This method iterate over all the feature usage counts for each backend process
 *  and aggregate them. The aggregated feature counts then are returned as a set of
 *  key value pairs of the form ("feature_name" => usage_count).
 *
 *  Optionally, the shared memory counters are reset is resetStatsAfterRead is set to true.
 */
static void
StoreAllFeatureCounterStats(Tuplestorestate *tupleStore, TupleDesc tupleDescriptor, bool
							resetStatsAfterRead)
{
	Datum values[FEATURE_COUNTER_STATS_COLUMNS] = { 0 };
	bool isNulls[FEATURE_COUNTER_STATS_COLUMNS] = { 0 };

	FeatureCounter aggregatedFeatureCounter;
	PopulateFeatureCounters(&aggregatedFeatureCounter);

	if (resetStatsAfterRead)
	{
		ResetFeatureCounters();
	}

	for (int i = 0; i < MAX_FEATURE_COUNT; i++)
	{
		if (aggregatedFeatureCounter[i] == 0)
		{
			continue;
		}

		values[0] = PointerGetDatum(cstring_to_text(FeatureMapping[i]));
		values[1] = Int32GetDatum(aggregatedFeatureCounter[i]);
		tuplestore_putvalues(tupleStore, tupleDescriptor, values, isNulls);
	}
}


/*
 * Sets up a basic TupleStore for feature counter responses.
 */
static Tuplestorestate *
SetupFeatureCounterTuplestore(FunctionCallInfo fcinfo, TupleDesc *tupleDescriptor)
{
	ReturnSetInfo *resultSet = (ReturnSetInfo *) fcinfo->resultinfo;
	switch (get_call_result_type(fcinfo, NULL, tupleDescriptor))
	{
		case TYPEFUNC_COMPOSITE:
		{
			/* success */
			break;
		}

		case TYPEFUNC_RECORD:
		{
			/* failed to determine actual type of RECORD */
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("function returning record called in context "
							"that cannot accept type record")));
			break;
		}

		default:
		{
			/* result type isn't composite */
			elog(ERROR, "return type must be a row type");
			break;
		}
	}

	MemoryContext perQueryContext = resultSet->econtext->ecxt_per_query_memory;

	MemoryContext oldContext = MemoryContextSwitchTo(perQueryContext);
	Tuplestorestate *tupstore = tuplestore_begin_heap(true, false, work_mem);
	resultSet->returnMode = SFRM_Materialize;
	resultSet->setResult = tupstore;
	resultSet->setDesc = *tupleDescriptor;
	MemoryContextSwitchTo(oldContext);

	return tupstore;
}
