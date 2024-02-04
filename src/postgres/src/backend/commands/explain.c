/*-------------------------------------------------------------------------
 *
 * explain.c
 *	  Explain query execution plans
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994-5, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/commands/explain.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/xact.h"
#include "catalog/pg_type.h"
#include "commands/createas.h"
#include "commands/defrem.h"
#include "commands/prepare.h"
#include "executor/nodeHash.h"
#include "foreign/fdwapi.h"
#include "jit/jit.h"
#include "nodes/extensible.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "parser/analyze.h"
#include "parser/parsetree.h"
#include "rewrite/rewriteHandler.h"
#include "storage/bufmgr.h"
#include "tcop/tcopprot.h"
#include "utils/builtins.h"
#include "utils/guc_tables.h"
#include "utils/json.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/ruleutils.h"
#include "utils/snapmgr.h"
#include "utils/tuplesort.h"
#include "utils/typcache.h"
#include "utils/xml.h"

/* Yugabyte includes */
#include "pg_yb_utils.h"
#include "utils/guc.h"

/* Hook for plugins to get control in ExplainOneQuery() */
ExplainOneQuery_hook_type ExplainOneQuery_hook = NULL;

/* Hook for plugins to get control in explain_get_index_name() */
explain_get_index_name_hook_type explain_get_index_name_hook = NULL;


/* OR-able flags for ExplainXMLTag() */
#define X_OPENING 0
#define X_CLOSING 1
#define X_CLOSE_IMMEDIATE 2
#define X_NOWHITESPACE 4

#define CEILING_K(s) ((s + 1023) / 1024)

static void ExplainOneQuery(Query *query, int cursorOptions,
							IntoClause *into, ExplainState *es,
							const char *queryString, ParamListInfo params,
							QueryEnvironment *queryEnv);
static void ExplainPrintJIT(ExplainState *es, int jit_flags,
							JitInstrumentation *ji);
static void report_triggers(ResultRelInfo *rInfo, bool show_relname,
							ExplainState *es);
static double elapsed_time(instr_time *starttime);
static bool ExplainPreScanNode(PlanState *planstate, Bitmapset **rels_used);
static void ExplainNode(PlanState *planstate, List *ancestors,
						const char *relationship, const char *plan_name,
						ExplainState *es);
static void show_plan_tlist(PlanState *planstate, List *ancestors,
							ExplainState *es);
static void show_expression(Node *node, const char *qlabel,
							PlanState *planstate, List *ancestors,
							bool useprefix, ExplainState *es);
static void show_qual(List *qual, const char *qlabel,
					  PlanState *planstate, List *ancestors,
					  bool useprefix, ExplainState *es);
static void show_scan_qual(List *qual, const char *qlabel,
						   PlanState *planstate, List *ancestors,
						   ExplainState *es);
static void show_upper_qual(List *qual, const char *qlabel,
							PlanState *planstate, List *ancestors,
							ExplainState *es);
static void show_sort_keys(SortState *sortstate, List *ancestors,
						   ExplainState *es);
static void show_incremental_sort_keys(IncrementalSortState *incrsortstate,
									   List *ancestors, ExplainState *es);
static void show_merge_append_keys(MergeAppendState *mstate, List *ancestors,
								   ExplainState *es);
static void show_agg_keys(AggState *astate, List *ancestors,
						  ExplainState *es);
static void show_grouping_sets(PlanState *planstate, Agg *agg,
							   List *ancestors, ExplainState *es);
static void show_grouping_set_keys(PlanState *planstate,
								   Agg *aggnode, Sort *sortnode,
								   List *context, bool useprefix,
								   List *ancestors, ExplainState *es);
static void show_group_keys(GroupState *gstate, List *ancestors,
							ExplainState *es);
static void show_sort_group_keys(PlanState *planstate, const char *qlabel,
								 int nkeys, int nPresortedKeys, AttrNumber *keycols,
								 Oid *sortOperators, Oid *collations, bool *nullsFirst,
								 List *ancestors, ExplainState *es);
static void show_sortorder_options(StringInfo buf, Node *sortexpr,
								   Oid sortOperator, Oid collation, bool nullsFirst);
static void show_tablesample(TableSampleClause *tsc, PlanState *planstate,
							 List *ancestors, ExplainState *es);
static void show_sort_info(SortState *sortstate, ExplainState *es);
static void show_incremental_sort_info(IncrementalSortState *incrsortstate,
									   ExplainState *es);
static void show_hash_info(HashState *hashstate, ExplainState *es);
static void show_memoize_info(MemoizeState *mstate, List *ancestors,
							  ExplainState *es);
static void show_hashagg_info(AggState *hashstate, ExplainState *es);
static void show_tidbitmap_info(BitmapHeapScanState *planstate,
								ExplainState *es);
static void show_instrumentation_count(const char *qlabel, int which,
									   PlanState *planstate, ExplainState *es);
static void show_foreignscan_info(ForeignScanState *fsstate, ExplainState *es);
static void show_eval_params(Bitmapset *bms_params, ExplainState *es);
static const char *explain_get_index_name(Oid indexId);
static void show_buffer_usage(ExplainState *es, const BufferUsage *usage,
							  bool planning);
static void show_wal_usage(ExplainState *es, const WalUsage *usage);
static void show_yb_rpc_stats(PlanState *planstate, bool indexScan, ExplainState *es);
static void ExplainIndexScanDetails(Oid indexid, ScanDirection indexorderdir,
									double yb_estimated_num_nexts,
									double yb_estimated_num_seeks,
									ExplainState *es);
static void ExplainScanTarget(Scan *plan, ExplainState *es);
static void ExplainModifyTarget(ModifyTable *plan, ExplainState *es);
static void ExplainTargetRel(Plan *plan, Index rti, ExplainState *es);
static void show_modifytable_info(ModifyTableState *mtstate, List *ancestors,
								  ExplainState *es);
static void ExplainMemberNodes(PlanState **planstates, int nplans,
							   List *ancestors, ExplainState *es);
static void ExplainMissingMembers(int nplans, int nchildren, ExplainState *es);
static void ExplainSubPlans(List *plans, List *ancestors,
							const char *relationship, ExplainState *es);
static void ExplainCustomChildren(CustomScanState *css,
								  List *ancestors, ExplainState *es);
static ExplainWorkersState *ExplainCreateWorkersState(int num_workers);
static void ExplainOpenWorker(int n, ExplainState *es);
static void ExplainCloseWorker(int n, ExplainState *es);
static void ExplainFlushWorkersState(ExplainState *es);
static void ExplainProperty(const char *qlabel, const char *unit,
							const char *value, bool numeric, ExplainState *es);
static void ExplainOpenSetAsideGroup(const char *objtype, const char *labelname,
									 bool labeled, int depth, ExplainState *es);
static void ExplainSaveGroup(ExplainState *es, int depth, int *state_save);
static void ExplainRestoreGroup(ExplainState *es, int depth, int *state_save);
static void ExplainDummyGroup(const char *objtype, const char *labelname,
							  ExplainState *es);
static void ExplainXMLTag(const char *tagname, int flags, ExplainState *es);
static void ExplainIndentText(ExplainState *es);
static void ExplainJSONLineEnding(ExplainState *es);
static void ExplainYAMLLineStarting(ExplainState *es);
static void escape_yaml(StringInfo buf, const char *str);
static void YbAppendPgMemInfo(ExplainState *es, const Size peakMem);
static void
YbAggregateExplainableRPCRequestStat(ExplainState			 *es,
									 const YbInstrumentation *instr);
static void YbExplainDistinctPrefixLen(
	int yb_distinct_prefixlen, ExplainState *es);

typedef enum YbStatLabel
{
	YB_STAT_LABEL_FIRST = 0,

	YB_STAT_LABEL_CATALOG_READ = YB_STAT_LABEL_FIRST,
	YB_STAT_LABEL_CATALOG_WRITE,

	YB_STAT_LABEL_STORAGE_READ,
	YB_STAT_LABEL_STORAGE_WRITE,

	YB_STAT_LABEL_STORAGE_TABLE_READ,
	YB_STAT_LABEL_STORAGE_TABLE_WRITE,

	YB_STAT_LABEL_STORAGE_INDEX_READ,
	YB_STAT_LABEL_STORAGE_INDEX_WRITE,

	YB_STAT_LABEL_STORAGE_FLUSH,

	YB_STAT_LABEL_LAST
} YbStatLabel;

typedef struct YbStatLabelData
{
	const char *requests;
	const char *execution_time;

	/* Indicates if field can vary between runs of the same query */
	const bool is_non_deterministic;
} YbStatLabelData;

typedef struct YbExplainState
{
	ExplainState *es;
	bool		  display_zero;
} YbExplainState;

#define BUILD_STAT_LABEL_DATA(NAME, IS_NON_DETERMINISTIC) \
	{ \
		NAME " Requests", NAME " Execution Time", IS_NON_DETERMINISTIC \
	}

#define BUILD_NON_DETERMINISTIC_STAT_LABEL_DATA(NAME) \
	BUILD_STAT_LABEL_DATA(NAME, true)

#define BUILD_DETERMINISTIC_STAT_LABEL_DATA(NAME) \
	BUILD_STAT_LABEL_DATA(NAME, false)

const YbStatLabelData yb_stat_label_data[] = {
	[YB_STAT_LABEL_CATALOG_READ] =
		BUILD_NON_DETERMINISTIC_STAT_LABEL_DATA("Catalog Read"),
	[YB_STAT_LABEL_CATALOG_WRITE] =
		BUILD_NON_DETERMINISTIC_STAT_LABEL_DATA("Catalog Write"),

	[YB_STAT_LABEL_STORAGE_READ] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Read"),
	[YB_STAT_LABEL_STORAGE_WRITE] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Write"),

	[YB_STAT_LABEL_STORAGE_TABLE_READ] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Table Read"),
	[YB_STAT_LABEL_STORAGE_TABLE_WRITE] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Table Write"),

	[YB_STAT_LABEL_STORAGE_INDEX_READ] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Index Read"),
	[YB_STAT_LABEL_STORAGE_INDEX_WRITE] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Index Write"),

	[YB_STAT_LABEL_STORAGE_FLUSH] =
		BUILD_DETERMINISTIC_STAT_LABEL_DATA("Storage Flush"),
};

#undef BUILD_STAT_LABEL_DATA

#define BUILD_METRIC_LABEL(NAME) ("Metric " NAME)

// These labels are identical to exported metric names.
const char *yb_metric_gauge_label[] = {
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MISS] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_ADD] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_add"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_ADD_FAILURES] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_add_failures"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_INDEX_MISS] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_index_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_INDEX_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_index_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_FILTER_MISS] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_filter_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_FILTER_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_filter_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_DATA_MISS] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_data_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_DATA_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_data_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_BYTES_READ] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_bytes_read"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_BYTES_WRITE] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_bytes_write"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_USEFUL] =
		BUILD_METRIC_LABEL("rocksdb_bloom_filter_useful"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_CHECKED] =
		BUILD_METRIC_LABEL("rocksdb_bloom_filter_checked"),
	[YB_STORAGE_GAUGE_REGULARDB_MEMTABLE_HIT] =
		BUILD_METRIC_LABEL("rocksdb_memtable_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_MEMTABLE_MISS] =
		BUILD_METRIC_LABEL("rocksdb_memtable_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_GET_HIT_L0] =
		BUILD_METRIC_LABEL("rocksdb_get_hit_l0"),
	[YB_STORAGE_GAUGE_REGULARDB_GET_HIT_L1] =
		BUILD_METRIC_LABEL("rocksdb_get_hit_l1"),
	[YB_STORAGE_GAUGE_REGULARDB_GET_HIT_L2_AND_UP] =
		BUILD_METRIC_LABEL("rocksdb_get_hit_l2_and_up"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_KEYS_WRITTEN] =
		BUILD_METRIC_LABEL("rocksdb_number_keys_written"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_KEYS_READ] =
		BUILD_METRIC_LABEL("rocksdb_number_keys_read"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_KEYS_UPDATED] =
		BUILD_METRIC_LABEL("rocksdb_number_keys_updated"),
	[YB_STORAGE_GAUGE_REGULARDB_BYTES_WRITTEN] =
		BUILD_METRIC_LABEL("rocksdb_bytes_written"),
	[YB_STORAGE_GAUGE_REGULARDB_BYTES_READ] =
		BUILD_METRIC_LABEL("rocksdb_bytes_read"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_SEEK] =
		BUILD_METRIC_LABEL("rocksdb_number_db_seek"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_NEXT] =
		BUILD_METRIC_LABEL("rocksdb_number_db_next"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_PREV] =
		BUILD_METRIC_LABEL("rocksdb_number_db_prev"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_SEEK_FOUND] =
		BUILD_METRIC_LABEL("rocksdb_number_db_seek_found"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_NEXT_FOUND] =
		BUILD_METRIC_LABEL("rocksdb_number_db_next_found"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DB_PREV_FOUND] =
		BUILD_METRIC_LABEL("rocksdb_number_db_prev_found"),
	[YB_STORAGE_GAUGE_REGULARDB_ITER_BYTES_READ] =
		BUILD_METRIC_LABEL("rocksdb_iter_bytes_read"),
	[YB_STORAGE_GAUGE_REGULARDB_NO_FILE_CLOSES] =
		BUILD_METRIC_LABEL("rocksdb_no_file_closes"),
	[YB_STORAGE_GAUGE_REGULARDB_NO_FILE_OPENS] =
		BUILD_METRIC_LABEL("rocksdb_no_file_opens"),
	[YB_STORAGE_GAUGE_REGULARDB_NO_FILE_ERRORS] =
		BUILD_METRIC_LABEL("rocksdb_no_file_errors"),
	[YB_STORAGE_GAUGE_REGULARDB_STALL_L0_SLOWDOWN_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_stall_l0_slowdown_micros"),
	[YB_STORAGE_GAUGE_REGULARDB_STALL_MEMTABLE_COMPACTION_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_stall_memtable_compaction_micros"),
	[YB_STORAGE_GAUGE_REGULARDB_STALL_L0_NUM_FILES_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_stall_l0_num_files_micros"),
	[YB_STORAGE_GAUGE_REGULARDB_STALL_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_stall_micros"),
	[YB_STORAGE_GAUGE_REGULARDB_DB_MUTEX_WAIT_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_db_mutex_wait_micros"),
	[YB_STORAGE_GAUGE_REGULARDB_RATE_LIMIT_DELAY_MILLIS] =
		BUILD_METRIC_LABEL("rocksdb_rate_limit_delay_millis"),
	[YB_STORAGE_GAUGE_REGULARDB_NO_ITERATORS] =
		BUILD_METRIC_LABEL("rocksdb_no_iterators"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_MULTIGET_CALLS] =
		BUILD_METRIC_LABEL("rocksdb_number_multiget_calls"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_MULTIGET_KEYS_READ] =
		BUILD_METRIC_LABEL("rocksdb_number_multiget_keys_read"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_MULTIGET_BYTES_READ] =
		BUILD_METRIC_LABEL("rocksdb_number_multiget_bytes_read"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_FILTERED_DELETES] =
		BUILD_METRIC_LABEL("rocksdb_number_filtered_deletes"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_MERGE_FAILURES] =
		BUILD_METRIC_LABEL("rocksdb_number_merge_failures"),
	[YB_STORAGE_GAUGE_REGULARDB_SEQUENCE_NUMBER] =
		BUILD_METRIC_LABEL("rocksdb_sequence_number"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_PREFIX_CHECKED] =
		BUILD_METRIC_LABEL("rocksdb_bloom_filter_prefix_checked"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOOM_FILTER_PREFIX_USEFUL] =
		BUILD_METRIC_LABEL("rocksdb_bloom_filter_prefix_useful"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_OF_RESEEKS_IN_ITERATION] =
		BUILD_METRIC_LABEL("rocksdb_number_of_reseeks_in_iteration"),
	[YB_STORAGE_GAUGE_REGULARDB_GET_UPDATES_SINCE_CALLS] =
		BUILD_METRIC_LABEL("rocksdb_get_updates_since_calls"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_MISS] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_compressed_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_compressed_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_ADD] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_compressed_add"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_COMPRESSED_ADD_FAILURES] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_compressed_add_failures"),
	[YB_STORAGE_GAUGE_REGULARDB_WAL_FILE_SYNCED] =
		BUILD_METRIC_LABEL("rocksdb_wal_file_synced"),
	[YB_STORAGE_GAUGE_REGULARDB_WAL_FILE_BYTES] =
		BUILD_METRIC_LABEL("rocksdb_wal_file_bytes"),
	[YB_STORAGE_GAUGE_REGULARDB_WRITE_DONE_BY_SELF] =
		BUILD_METRIC_LABEL("rocksdb_write_done_by_self"),
	[YB_STORAGE_GAUGE_REGULARDB_WRITE_DONE_BY_OTHER] =
		BUILD_METRIC_LABEL("rocksdb_write_done_by_other"),
	[YB_STORAGE_GAUGE_REGULARDB_WRITE_WITH_WAL] =
		BUILD_METRIC_LABEL("rocksdb_write_with_wal"),
	[YB_STORAGE_GAUGE_REGULARDB_COMPACT_READ_BYTES] =
		BUILD_METRIC_LABEL("rocksdb_compact_read_bytes"),
	[YB_STORAGE_GAUGE_REGULARDB_COMPACT_WRITE_BYTES] =
		BUILD_METRIC_LABEL("rocksdb_compact_write_bytes"),
	[YB_STORAGE_GAUGE_REGULARDB_FLUSH_WRITE_BYTES] =
		BUILD_METRIC_LABEL("rocksdb_flush_write_bytes"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_DIRECT_LOAD_TABLE_PROPERTIES] =
		BUILD_METRIC_LABEL("rocksdb_number_direct_load_table_properties"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_SUPERVERSION_ACQUIRES] =
		BUILD_METRIC_LABEL("rocksdb_number_superversion_acquires"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_SUPERVERSION_RELEASES] =
		BUILD_METRIC_LABEL("rocksdb_number_superversion_releases"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_SUPERVERSION_CLEANUPS] =
		BUILD_METRIC_LABEL("rocksdb_number_superversion_cleanups"),
	[YB_STORAGE_GAUGE_REGULARDB_NUMBER_BLOCK_NOT_COMPRESSED] =
		BUILD_METRIC_LABEL("rocksdb_number_block_not_compressed"),
	[YB_STORAGE_GAUGE_REGULARDB_MERGE_OPERATION_TOTAL_TIME] =
		BUILD_METRIC_LABEL("rocksdb_merge_operation_total_time"),
	[YB_STORAGE_GAUGE_REGULARDB_FILTER_OPERATION_TOTAL_TIME] =
		BUILD_METRIC_LABEL("rocksdb_filter_operation_total_time"),
	[YB_STORAGE_GAUGE_REGULARDB_ROW_CACHE_HIT] =
		BUILD_METRIC_LABEL("rocksdb_row_cache_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_ROW_CACHE_MISS] =
		BUILD_METRIC_LABEL("rocksdb_row_cache_miss"),
	[YB_STORAGE_GAUGE_REGULARDB_NO_TABLE_CACHE_ITERATORS] =
		BUILD_METRIC_LABEL("rocksdb_no_table_cache_iterators"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_single_touch_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_ADD] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_single_touch_add"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_READ] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_single_touch_bytes_read"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_single_touch_bytes_write"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_HIT] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_multi_touch_hit"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_ADD] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_multi_touch_add"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_READ] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_multi_touch_bytes_read"),
	[YB_STORAGE_GAUGE_REGULARDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE] =
		BUILD_METRIC_LABEL("rocksdb_block_cache_multi_touch_bytes_write"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_ADD] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_add"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_ADD_FAILURES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_add_failures"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_INDEX_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_index_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_INDEX_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_index_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_FILTER_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_filter_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_FILTER_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_filter_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_DATA_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_data_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_DATA_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_data_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_BYTES_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_bytes_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_BYTES_WRITE] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_bytes_write"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_USEFUL] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bloom_filter_useful"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_CHECKED] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bloom_filter_checked"),
	[YB_STORAGE_GAUGE_INTENTSDB_MEMTABLE_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_memtable_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_MEMTABLE_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_memtable_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_GET_HIT_L0] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_get_hit_l0"),
	[YB_STORAGE_GAUGE_INTENTSDB_GET_HIT_L1] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_get_hit_l1"),
	[YB_STORAGE_GAUGE_INTENTSDB_GET_HIT_L2_AND_UP] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_get_hit_l2_and_up"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_KEYS_WRITTEN] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_keys_written"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_KEYS_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_keys_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_KEYS_UPDATED] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_keys_updated"),
	[YB_STORAGE_GAUGE_INTENTSDB_BYTES_WRITTEN] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bytes_written"),
	[YB_STORAGE_GAUGE_INTENTSDB_BYTES_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bytes_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_SEEK] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_db_seek"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_NEXT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_db_next"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_PREV] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_db_prev"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_SEEK_FOUND] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_db_seek_found"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_NEXT_FOUND] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_db_next_found"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DB_PREV_FOUND] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_db_prev_found"),
	[YB_STORAGE_GAUGE_INTENTSDB_ITER_BYTES_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_iter_bytes_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_NO_FILE_CLOSES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_no_file_closes"),
	[YB_STORAGE_GAUGE_INTENTSDB_NO_FILE_OPENS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_no_file_opens"),
	[YB_STORAGE_GAUGE_INTENTSDB_NO_FILE_ERRORS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_no_file_errors"),
	[YB_STORAGE_GAUGE_INTENTSDB_STALL_L0_SLOWDOWN_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_stall_l0_slowdown_micros"),
	[YB_STORAGE_GAUGE_INTENTSDB_STALL_MEMTABLE_COMPACTION_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_stall_memtable_compaction_micros"),
	[YB_STORAGE_GAUGE_INTENTSDB_STALL_L0_NUM_FILES_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_stall_l0_num_files_micros"),
	[YB_STORAGE_GAUGE_INTENTSDB_STALL_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_stall_micros"),
	[YB_STORAGE_GAUGE_INTENTSDB_DB_MUTEX_WAIT_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_db_mutex_wait_micros"),
	[YB_STORAGE_GAUGE_INTENTSDB_RATE_LIMIT_DELAY_MILLIS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_rate_limit_delay_millis"),
	[YB_STORAGE_GAUGE_INTENTSDB_NO_ITERATORS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_no_iterators"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MULTIGET_CALLS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_multiget_calls"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MULTIGET_KEYS_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_multiget_keys_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MULTIGET_BYTES_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_multiget_bytes_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_FILTERED_DELETES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_filtered_deletes"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_MERGE_FAILURES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_merge_failures"),
	[YB_STORAGE_GAUGE_INTENTSDB_SEQUENCE_NUMBER] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_sequence_number"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_PREFIX_CHECKED] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bloom_filter_prefix_checked"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOOM_FILTER_PREFIX_USEFUL] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bloom_filter_prefix_useful"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_OF_RESEEKS_IN_ITERATION] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_of_reseeks_in_iteration"),
	[YB_STORAGE_GAUGE_INTENTSDB_GET_UPDATES_SINCE_CALLS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_get_updates_since_calls"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_compressed_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_compressed_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_ADD] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_compressed_add"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_COMPRESSED_ADD_FAILURES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_compressed_add_failures"),
	[YB_STORAGE_GAUGE_INTENTSDB_WAL_FILE_SYNCED] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_wal_file_synced"),
	[YB_STORAGE_GAUGE_INTENTSDB_WAL_FILE_BYTES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_wal_file_bytes"),
	[YB_STORAGE_GAUGE_INTENTSDB_WRITE_DONE_BY_SELF] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_write_done_by_self"),
	[YB_STORAGE_GAUGE_INTENTSDB_WRITE_DONE_BY_OTHER] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_write_done_by_other"),
	[YB_STORAGE_GAUGE_INTENTSDB_WRITE_WITH_WAL] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_write_with_wal"),
	[YB_STORAGE_GAUGE_INTENTSDB_COMPACT_READ_BYTES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_compact_read_bytes"),
	[YB_STORAGE_GAUGE_INTENTSDB_COMPACT_WRITE_BYTES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_compact_write_bytes"),
	[YB_STORAGE_GAUGE_INTENTSDB_FLUSH_WRITE_BYTES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_flush_write_bytes"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_DIRECT_LOAD_TABLE_PROPERTIES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_direct_load_table_properties"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_SUPERVERSION_ACQUIRES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_superversion_acquires"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_SUPERVERSION_RELEASES] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_superversion_releases"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_SUPERVERSION_CLEANUPS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_superversion_cleanups"),
	[YB_STORAGE_GAUGE_INTENTSDB_NUMBER_BLOCK_NOT_COMPRESSED] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_number_block_not_compressed"),
	[YB_STORAGE_GAUGE_INTENTSDB_MERGE_OPERATION_TOTAL_TIME] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_merge_operation_total_time"),
	[YB_STORAGE_GAUGE_INTENTSDB_FILTER_OPERATION_TOTAL_TIME] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_filter_operation_total_time"),
	[YB_STORAGE_GAUGE_INTENTSDB_ROW_CACHE_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_row_cache_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_ROW_CACHE_MISS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_row_cache_miss"),
	[YB_STORAGE_GAUGE_INTENTSDB_NO_TABLE_CACHE_ITERATORS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_no_table_cache_iterators"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_single_touch_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_ADD] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_single_touch_add"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_single_touch_bytes_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_SINGLE_TOUCH_BYTES_WRITE] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_single_touch_bytes_write"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_HIT] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_multi_touch_hit"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_ADD] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_multi_touch_add"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_multi_touch_bytes_read"),
	[YB_STORAGE_GAUGE_INTENTSDB_BLOCK_CACHE_MULTI_TOUCH_BYTES_WRITE] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_block_cache_multi_touch_bytes_write"),
	[YB_STORAGE_GAUGE_ACTIVE_WRITE_QUERY_OBJECTS] =
		BUILD_METRIC_LABEL("active_write_query_objects"),
};

const char *yb_metric_counter_label[] = {
	[YB_STORAGE_COUNTER_NOT_LEADER_REJECTIONS] =
		BUILD_METRIC_LABEL("not_leader_rejections"),
	[YB_STORAGE_COUNTER_LEADER_MEMORY_PRESSURE_REJECTIONS] =
		BUILD_METRIC_LABEL("leader_memory_pressure_rejections"),
	[YB_STORAGE_COUNTER_MAJORITY_SST_FILES_REJECTIONS] =
		BUILD_METRIC_LABEL("majority_sst_file_rejections"),
	[YB_STORAGE_COUNTER_TRANSACTION_CONFLICTS] =
		BUILD_METRIC_LABEL("transaction_conflicts"),
	[YB_STORAGE_COUNTER_EXPIRED_TRANSACTIONS] =
		BUILD_METRIC_LABEL("expired_transactions"),
	[YB_STORAGE_COUNTER_RESTART_READ_REQUESTS] =
		BUILD_METRIC_LABEL("restart_read_requests"),
	[YB_STORAGE_COUNTER_CONSISTENT_PREFIX_READ_REQUESTS] =
		BUILD_METRIC_LABEL("consistent_prefix_read_requests"),
	[YB_STORAGE_COUNTER_PGSQL_CONSISTENT_PREFIX_READ_ROWS] =
		BUILD_METRIC_LABEL("pgsql_consistent_prefix_read_rows"),
	[YB_STORAGE_COUNTER_TABLET_DATA_CORRUPTIONS] =
		BUILD_METRIC_LABEL("tablet_data_corruptions"),
	[YB_STORAGE_COUNTER_ROWS_INSERTED] =
		BUILD_METRIC_LABEL("rows_inserted"),
	[YB_STORAGE_COUNTER_FAILED_BATCH_LOCK] =
		BUILD_METRIC_LABEL("failed_batch_lock"),
	[YB_STORAGE_COUNTER_DOCDB_KEYS_FOUND] =
		BUILD_METRIC_LABEL("docdb_keys_found"),
	[YB_STORAGE_COUNTER_DOCDB_OBSOLETE_KEYS_FOUND] =
		BUILD_METRIC_LABEL("docdb_obsolete_keys_found"),
	[YB_STORAGE_COUNTER_DOCDB_OBSOLETE_KEYS_FOUND_PAST_CUTOFF] =
		BUILD_METRIC_LABEL("docdb_obsolete_keys_found_past_cutoff"),
};

const char *yb_metric_event_label[] = {
	[YB_STORAGE_EVENT_REGULARDB_DB_GET] =
		BUILD_METRIC_LABEL("rocksdb_db_get_micros"),
	[YB_STORAGE_EVENT_REGULARDB_DB_WRITE] =
		BUILD_METRIC_LABEL("rocksdb_db_write_micros"),
	[YB_STORAGE_EVENT_REGULARDB_COMPACTION_TIME] =
		BUILD_METRIC_LABEL("rocksdb_compaction_times_micros"),
	[YB_STORAGE_EVENT_REGULARDB_WAL_FILE_SYNC_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_wal_file_sync_micros"),
	[YB_STORAGE_EVENT_REGULARDB_DB_MULTIGET] =
		BUILD_METRIC_LABEL("rocksdb_db_multiget_micros"),
	[YB_STORAGE_EVENT_REGULARDB_READ_BLOCK_COMPACTION_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_read_block_compaction_micros"),
	[YB_STORAGE_EVENT_REGULARDB_READ_BLOCK_GET_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_read_block_get_micros"),
	[YB_STORAGE_EVENT_REGULARDB_WRITE_RAW_BLOCK_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_write_raw_block_micros"),
	[YB_STORAGE_EVENT_REGULARDB_NUM_FILES_IN_SINGLE_COMPACTION] =
		BUILD_METRIC_LABEL("rocksdb_num_files_in_singlecompaction"),
	[YB_STORAGE_EVENT_REGULARDB_DB_SEEK] =
		BUILD_METRIC_LABEL("rocksdb_db_seek_micros"),
	[YB_STORAGE_EVENT_REGULARDB_SST_READ_MICROS] =
		BUILD_METRIC_LABEL("rocksdb_sst_read_micros"),
	[YB_STORAGE_EVENT_REGULARDB_BYTES_PER_READ] =
		BUILD_METRIC_LABEL("rocksdb_bytes_per_read"),
	[YB_STORAGE_EVENT_REGULARDB_BYTES_PER_WRITE] =
		BUILD_METRIC_LABEL("rocksdb_bytes_per_write"),
	[YB_STORAGE_EVENT_REGULARDB_BYTES_PER_MULTIGET] =
		BUILD_METRIC_LABEL("rocksdb_bytes_per_multiget"),
	[YB_STORAGE_EVENT_INTENTSDB_DB_GET] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_db_get_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_DB_WRITE] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_db_write_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_COMPACTION_TIME] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_compaction_times_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_WAL_FILE_SYNC_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_wal_file_sync_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_DB_MULTIGET] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_db_multiget_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_READ_BLOCK_COMPACTION_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_read_block_compaction_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_READ_BLOCK_GET_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_read_block_get_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_WRITE_RAW_BLOCK_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_write_raw_block_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_NUM_FILES_IN_SINGLE_COMPACTION] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_num_files_in_singlecompaction"),
	[YB_STORAGE_EVENT_INTENTSDB_DB_SEEK] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_db_seek_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_SST_READ_MICROS] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_sst_read_micros"),
	[YB_STORAGE_EVENT_INTENTSDB_BYTES_PER_READ] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bytes_per_read"),
	[YB_STORAGE_EVENT_INTENTSDB_BYTES_PER_WRITE] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bytes_per_write"),
	[YB_STORAGE_EVENT_INTENTSDB_BYTES_PER_MULTIGET] =
		BUILD_METRIC_LABEL("intentsdb_rocksdb_bytes_per_multiget"),
	[YB_STORAGE_EVENT_SNAPSHOT_READ_INFLIGHT_WAIT_DURATION] =
		BUILD_METRIC_LABEL("snapshot_read_inflight_wait_duration"),
	[YB_STORAGE_EVENT_QL_READ_LATENCY] =
		BUILD_METRIC_LABEL("ql_read_latency"),
	[YB_STORAGE_EVENT_WRITE_LOCK_LATENCY] =
		BUILD_METRIC_LABEL("write_lock_latency"),
	[YB_STORAGE_EVENT_QL_WRITE_LATENCY] =
		BUILD_METRIC_LABEL("ql_write_latency"),
	[YB_STORAGE_EVENT_READ_TIME_WAIT] =
		BUILD_METRIC_LABEL("read_time_wait"),
	[YB_STORAGE_EVENT_TOTAL_WAIT_QUEUE_TIME] =
		BUILD_METRIC_LABEL("total_wait_queue_time"),
};

#undef BUILD_METRIC_LABEL

/* Explains a single stat with no associated timing */
static void
YbExplainStatWithoutTiming(YbExplainState *yb_es, YbStatLabel label,
						   double count)
{
	if (!(count > 0 || yb_es->display_zero) ||
		(yb_explain_hide_non_deterministic_fields &&
		 yb_stat_label_data[label].is_non_deterministic))
		return;

	const YbStatLabelData *label_data = &yb_stat_label_data[label];
	ExplainState		  *es = yb_es->es;
	ExplainPropertyFloat(label_data->requests, NULL, count, 0, es);
}

/* Explains a single RPC related stat and its associated timing */
static void
YbExplainRpcRequestStat(YbExplainState *yb_es, YbStatLabel label, double count,
						double timing)
{
	if (!(count > 0 || yb_es->display_zero) ||
		(yb_explain_hide_non_deterministic_fields &&
		 yb_stat_label_data[label].is_non_deterministic))
		return;

	const YbStatLabelData *label_data = &yb_stat_label_data[label];
	ExplainState   *es = yb_es->es;
	ExplainPropertyFloat(label_data->requests, NULL, count, 0, es);

	/* Display timing info only when there is at least 1 RPC request. This
	 * enables the output to be concise. */
	if (yb_es->es->timing && count > 0)
		ExplainPropertyFloat(label_data->execution_time, "ms",
							 timing / 1000000.0, 3, yb_es->es);
}

static void
YbExplainRpcRequestNumericMetric(YbExplainState *yb_es, const char* label, double value,
								 bool is_mean) {
	if (value == 0)
		return;

	ExplainState   *es = yb_es->es;
	ExplainPropertyFloat(label, NULL, value, is_mean ? 3 : 0, es);
}

/* Explains a single RPC gauge metric */
static void
YbExplainRpcRequestGauge(YbExplainState *yb_es, YbPgGaugeMetrics metric, double value,
						 bool is_mean) {
	YbExplainRpcRequestNumericMetric(yb_es, yb_metric_gauge_label[metric], value, is_mean);
}

/* Explains a single RPC counter metric */
static void
YbExplainRpcRequestCounter(YbExplainState *yb_es, YbPgCounterMetrics metric, double value,
						   bool is_mean) {
	YbExplainRpcRequestNumericMetric(yb_es, yb_metric_counter_label[metric], value, is_mean);
}

/* Explains a single RPC event metric */
static void
YbExplainRpcRequestEvent(YbExplainState *yb_es, YbPgEventMetrics metric,
						 const YbPgEventMetric* value, double nloops, bool is_mean) {
	if (value->count == 0)
		return;

	const char  *label = yb_metric_event_label[metric];
	ExplainState   *es = yb_es->es;

	int ndigits = is_mean ? 3 : 0;

	char *buf;
	buf = psprintf("sum: %.*f, count: %.*f",
				   ndigits, value->sum / nloops, ndigits, value->count / nloops);
	ExplainProperty(label, NULL, buf, false, es);
	pfree(buf);
}

/* Maps a row mark type to a string. */
static const char *
YbRowMarkTypeToPgsqlString(RowMarkType row_mark_type)
{
	switch (row_mark_type)
	{
		case ROW_MARK_EXCLUSIVE:
			return "FOR UPDATE";
		case ROW_MARK_NOKEYEXCLUSIVE:
			return "FOR NO KEY UPDATE";
		case ROW_MARK_SHARE:
			return "FOR SHARE";
		case ROW_MARK_KEYSHARE:
			return "FOR KEY SHARE";
		default:
			return "";
	}
}

/* Explains a scan lock using row marks. */
static void
YbExplainScanLocks(YbLockMechanism yb_lock_mechanism, ExplainState *es)
{
	ListCell   *l;
	const char *lock_mode;

	if (!es->pstmt->rowMarks)
		return;

	if (!IsolationIsSerializable() && yb_lock_mechanism == YB_NO_SCAN_LOCK)
		return;

	foreach(l, es->pstmt->rowMarks)
	{
		PlanRowMark *erm = (PlanRowMark *) lfirst(l);
		if (erm->markType != ROW_MARK_REFERENCE &&
			erm->markType != ROW_MARK_COPY)
		{
			lock_mode = YbRowMarkTypeToPgsqlString(erm->markType);
			break;
		}
	}

	if (es->format == EXPLAIN_FORMAT_TEXT)
		appendStringInfo(es->str, " (Locked %s)", lock_mode);
	else
		ExplainPropertyText("Lock Type", lock_mode, es);
}

/* Explains a LockRows node */
static void
YbExplainLockRows(ExplainState *es)
{
	/* We only have something interesting to do in SERIALIZABLE isolation. */
	if (!IsolationIsSerializable())
		return;

	if (es->format == EXPLAIN_FORMAT_TEXT)
		appendStringInfoString(es->str, " (no-op)");
	else
		ExplainPropertyBool("Executes", false, es);
}

static bool
YbIsTimingNeeded(ExplainState *es, bool timing_set)
{
	/* Disable timing if only deterministic fields are requested */
	if (yb_explain_hide_non_deterministic_fields)
	{
		if (timing_set && es->timing)
		{
			ereport(WARNING,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("GUC yb_explain_hide_non_deterministic_fields "
							"disables EXPLAIN option TIMING")));
		}
		return false;
	}

	/* Else if timing option is explicitly set in the query, honor it */
	if (timing_set)
		return es->timing;

	/* Else, use timing if the query needs it */
	return es->analyze;
}

/*
 * ExplainQuery -
 *	  execute an EXPLAIN command
 */
void
ExplainQuery(ParseState *pstate, ExplainStmt *stmt,
			 ParamListInfo params, DestReceiver *dest)
{
	ExplainState *es = NewExplainState();
	TupOutputState *tstate;
	JumbleState *jstate = NULL;
	Query	   *query;
	List	   *rewritten;
	ListCell   *lc;
	bool		timing_set = false;
	bool		summary_set = false;

	/* Parse options list. */
	foreach(lc, stmt->options)
	{
		DefElem    *opt = (DefElem *) lfirst(lc);

		if (strcmp(opt->defname, "analyze") == 0)
			es->analyze = defGetBoolean(opt);
		else if (strcmp(opt->defname, "verbose") == 0)
			es->verbose = defGetBoolean(opt);
		else if (strcmp(opt->defname, "costs") == 0)
			es->costs = defGetBoolean(opt);
		else if (strcmp(opt->defname, "buffers") == 0)
			es->buffers = defGetBoolean(opt);
		else if (strcmp(opt->defname, "wal") == 0)
			es->wal = defGetBoolean(opt);
		else if (strcmp(opt->defname, "settings") == 0)
			es->settings = defGetBoolean(opt);
		else if (strcmp(opt->defname, "dist") == 0)
			es->rpc = defGetBoolean(opt);
		else if (strcmp(opt->defname, "debug") == 0)
			es->debug = defGetBoolean(opt);
		else if (strcmp(opt->defname, "timing") == 0)
		{
			timing_set = true;
			es->timing = defGetBoolean(opt);
		}
		else if (strcmp(opt->defname, "summary") == 0)
		{
			summary_set = true;
			es->summary = defGetBoolean(opt);
		}
		else if (strcmp(opt->defname, "format") == 0)
		{
			char	   *p = defGetString(opt);

			if (strcmp(p, "text") == 0)
				es->format = EXPLAIN_FORMAT_TEXT;
			else if (strcmp(p, "xml") == 0)
				es->format = EXPLAIN_FORMAT_XML;
			else if (strcmp(p, "json") == 0)
				es->format = EXPLAIN_FORMAT_JSON;
			else if (strcmp(p, "yaml") == 0)
				es->format = EXPLAIN_FORMAT_YAML;
			else
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("unrecognized value for EXPLAIN option \"%s\": \"%s\"",
								opt->defname, p),
						 parser_errposition(pstate, opt->location)));
		}
		else
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
					 errmsg("unrecognized EXPLAIN option \"%s\"",
							opt->defname),
					 parser_errposition(pstate, opt->location)));
	}

	if (es->analyze)
		yb_run_with_explain_analyze = true;

	if (es->wal && !es->analyze)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("EXPLAIN option WAL requires ANALYZE")));

	/* if hiding of non-deterministic fields is requested, turn off debug and verbose modes */
	if (yb_explain_hide_non_deterministic_fields)
	{
		if (es->debug)
		{
			ereport(WARNING,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("GUC yb_explain_hide_non_deterministic_fields "
							"disables EXPLAIN option DEBUG")));
			es->debug = false;
		}

		if (es->verbose)
		{
			ereport(WARNING,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("GUC yb_explain_hide_non_deterministic_fields "
							"disables EXPLAIN option VERBOSE")));
			es->verbose = false;
		}
	}

	/* check if timing is required */
	es->timing = YbIsTimingNeeded(es, timing_set);

	/* check that timing is used with EXPLAIN ANALYZE */
	if (es->timing && !es->analyze)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("EXPLAIN option TIMING requires ANALYZE")));

	/* if the summary was not set explicitly, set default value */
	es->summary = (summary_set) ? es->summary : es->analyze;

	if (es->rpc && !es->analyze)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("EXPLAIN option DIST requires ANALYZE")));

	/* Turn on timing of RPC requests in accordance to the flags passed */
	YbToggleSessionStatsTimer(es->timing);
	if (es->debug) {
		YbSetMetricsCaptureType(YB_YQL_METRICS_CAPTURE_ALL);
	}

	query = castNode(Query, stmt->query);
	if (IsQueryIdEnabled())
		jstate = JumbleQuery(query, pstate->p_sourcetext);

	if (post_parse_analyze_hook)
		(*post_parse_analyze_hook) (pstate, query, jstate);

	/*
	 * Parse analysis was done already, but we still have to run the rule
	 * rewriter.  We do not do AcquireRewriteLocks: we assume the query either
	 * came straight from the parser, or suitable locks were acquired by
	 * plancache.c.
	 */
	rewritten = QueryRewrite(castNode(Query, stmt->query));

	/* emit opening boilerplate */
	ExplainBeginOutput(es);

	if (rewritten == NIL)
	{
		/*
		 * In the case of an INSTEAD NOTHING, tell at least that.  But in
		 * non-text format, the output is delimited, so this isn't necessary.
		 */
		if (es->format == EXPLAIN_FORMAT_TEXT)
			appendStringInfoString(es->str, "Query rewrites to nothing\n");
	}
	else
	{
		ListCell   *l;

		/* Explain every plan */
		foreach(l, rewritten)
		{
			ExplainOneQuery(lfirst_node(Query, l),
							CURSOR_OPT_PARALLEL_OK, NULL, es,
							pstate->p_sourcetext, params, pstate->p_queryEnv);

			/* Separate plans with an appropriate separator */
			if (lnext(rewritten, l) != NULL)
				ExplainSeparatePlans(es);
		}
	}

	/* emit closing boilerplate */
	ExplainEndOutput(es);
	Assert(es->indent == 0);

	/* output tuples */
	tstate = begin_tup_output_tupdesc(dest, ExplainResultDesc(stmt),
									  &TTSOpsVirtual);
	if (es->format == EXPLAIN_FORMAT_TEXT)
		do_text_output_multiline(tstate, es->str->data);
	else
		do_text_output_oneline(tstate, es->str->data);
	end_tup_output(tstate);

	/* Turn off timing RPC requests and metrics capture so that future queries are not timed
	 * and metrics are not sent by default */
	YbToggleSessionStatsTimer(false);
	YbSetMetricsCaptureType(YB_YQL_METRICS_CAPTURE_NONE);
	pfree(es->str->data);
}

/*
 * Create a new ExplainState struct initialized with default options.
 */
ExplainState *
NewExplainState(void)
{
	ExplainState *es = (ExplainState *) palloc0(sizeof(ExplainState));

	/* Set default options (most fields can be left as zeroes). */
	es->costs = true;
	/* Prepare output buffer. */
	es->str = makeStringInfo();

	return es;
}

/*
 * ExplainResultDesc -
 *	  construct the result tupledesc for an EXPLAIN
 */
TupleDesc
ExplainResultDesc(ExplainStmt *stmt)
{
	TupleDesc	tupdesc;
	ListCell   *lc;
	Oid			result_type = TEXTOID;

	/* Check for XML format option */
	foreach(lc, stmt->options)
	{
		DefElem    *opt = (DefElem *) lfirst(lc);

		if (strcmp(opt->defname, "format") == 0)
		{
			char	   *p = defGetString(opt);

			if (strcmp(p, "xml") == 0)
				result_type = XMLOID;
			else if (strcmp(p, "json") == 0)
				result_type = JSONOID;
			else
				result_type = TEXTOID;
			/* don't "break", as ExplainQuery will use the last value */
		}
	}

	/* Need a tuple descriptor representing a single TEXT or XML column */
	tupdesc = CreateTemplateTupleDesc(1);
	TupleDescInitEntry(tupdesc, (AttrNumber) 1, "QUERY PLAN",
					   result_type, -1, 0);
	return tupdesc;
}

/*
 * ExplainOneQuery -
 *	  print out the execution plan for one Query
 *
 * "into" is NULL unless we are explaining the contents of a CreateTableAsStmt.
 */
static void
ExplainOneQuery(Query *query, int cursorOptions,
				IntoClause *into, ExplainState *es,
				const char *queryString, ParamListInfo params,
				QueryEnvironment *queryEnv)
{
	/* planner will not cope with utility statements */
	if (query->commandType == CMD_UTILITY)
	{
		ExplainOneUtility(query->utilityStmt, into, es, queryString, params,
						  queryEnv);
		return;
	}

	/* if an advisor plugin is present, let it manage things */
	if (ExplainOneQuery_hook)
		(*ExplainOneQuery_hook) (query, cursorOptions, into, es,
								 queryString, params, queryEnv);
	else
	{
		PlannedStmt *plan;
		instr_time	planstart,
					planduration;
		BufferUsage bufusage_start,
					bufusage;

		if (es->buffers)
			bufusage_start = pgBufferUsage;
		INSTR_TIME_SET_CURRENT(planstart);

		/* plan the query */
		plan = pg_plan_query(query, queryString, cursorOptions, params);

		INSTR_TIME_SET_CURRENT(planduration);
		INSTR_TIME_SUBTRACT(planduration, planstart);

		/* calc differences of buffer counters. */
		if (es->buffers)
		{
			memset(&bufusage, 0, sizeof(BufferUsage));
			BufferUsageAccumDiff(&bufusage, &pgBufferUsage, &bufusage_start);
		}

		/* run it (if needed) and produce output */
		ExplainOnePlan(plan, into, es, queryString, params, queryEnv,
					   &planduration, (es->buffers ? &bufusage : NULL));
	}
}

/*
 * ExplainOneUtility -
 *	  print out the execution plan for one utility statement
 *	  (In general, utility statements don't have plans, but there are some
 *	  we treat as special cases)
 *
 * "into" is NULL unless we are explaining the contents of a CreateTableAsStmt.
 *
 * This is exported because it's called back from prepare.c in the
 * EXPLAIN EXECUTE case.  In that case, we'll be dealing with a statement
 * that's in the plan cache, so we have to ensure we don't modify it.
 */
void
ExplainOneUtility(Node *utilityStmt, IntoClause *into, ExplainState *es,
				  const char *queryString, ParamListInfo params,
				  QueryEnvironment *queryEnv)
{
	if (utilityStmt == NULL)
		return;

	if (IsA(utilityStmt, CreateTableAsStmt))
	{
		/*
		 * We have to rewrite the contained SELECT and then pass it back to
		 * ExplainOneQuery.  Copy to be safe in the EXPLAIN EXECUTE case.
		 */
		CreateTableAsStmt *ctas = (CreateTableAsStmt *) utilityStmt;
		List	   *rewritten;

		/*
		 * Check if the relation exists or not.  This is done at this stage to
		 * avoid query planning or execution.
		 */
		if (CreateTableAsRelExists(ctas))
		{
			if (ctas->objtype == OBJECT_TABLE)
				ExplainDummyGroup("CREATE TABLE AS", NULL, es);
			else if (ctas->objtype == OBJECT_MATVIEW)
				ExplainDummyGroup("CREATE MATERIALIZED VIEW", NULL, es);
			else
				elog(ERROR, "unexpected object type: %d",
					 (int) ctas->objtype);
			return;
		}

		rewritten = QueryRewrite(castNode(Query, copyObject(ctas->query)));
		Assert(list_length(rewritten) == 1);
		ExplainOneQuery(linitial_node(Query, rewritten),
						CURSOR_OPT_PARALLEL_OK, ctas->into, es,
						queryString, params, queryEnv);
	}
	else if (IsA(utilityStmt, DeclareCursorStmt))
	{
		/*
		 * Likewise for DECLARE CURSOR.
		 *
		 * Notice that if you say EXPLAIN ANALYZE DECLARE CURSOR then we'll
		 * actually run the query.  This is different from pre-8.3 behavior
		 * but seems more useful than not running the query.  No cursor will
		 * be created, however.
		 */
		DeclareCursorStmt *dcs = (DeclareCursorStmt *) utilityStmt;
		List	   *rewritten;

		rewritten = QueryRewrite(castNode(Query, copyObject(dcs->query)));
		Assert(list_length(rewritten) == 1);
		ExplainOneQuery(linitial_node(Query, rewritten),
						dcs->options, NULL, es,
						queryString, params, queryEnv);
	}
	else if (IsA(utilityStmt, ExecuteStmt))
		ExplainExecuteQuery((ExecuteStmt *) utilityStmt, into, es,
							queryString, params, queryEnv);
	else if (IsA(utilityStmt, NotifyStmt))
	{
		if (es->format == EXPLAIN_FORMAT_TEXT)
			appendStringInfoString(es->str, "NOTIFY\n");
		else
			ExplainDummyGroup("Notify", NULL, es);
	}
	else
	{
		if (es->format == EXPLAIN_FORMAT_TEXT)
			appendStringInfoString(es->str,
								   "Utility statements have no plan structure\n");
		else
			ExplainDummyGroup("Utility Statement", NULL, es);
	}
}

/*
 * ExplainOnePlan -
 *		given a planned query, execute it if needed, and then print
 *		EXPLAIN output
 *
 * "into" is NULL unless we are explaining the contents of a CreateTableAsStmt,
 * in which case executing the query should result in creating that table.
 *
 * This is exported because it's called back from prepare.c in the
 * EXPLAIN EXECUTE case, and because an index advisor plugin would need
 * to call it.
 */
void
ExplainOnePlan(PlannedStmt *plannedstmt, IntoClause *into, ExplainState *es,
			   const char *queryString, ParamListInfo params,
			   QueryEnvironment *queryEnv, const instr_time *planduration,
			   const BufferUsage *bufusage)
{
	DestReceiver *dest;
	QueryDesc  *queryDesc;
	instr_time	starttime;
	double		totaltime = 0;
	int			eflags;
	int			instrument_option = 0;
	bool		show_variable_fields = !yb_explain_hide_non_deterministic_fields;

	Assert(plannedstmt->commandType != CMD_UTILITY);

	if (es->analyze && es->timing)
		instrument_option |= INSTRUMENT_TIMER;
	else if (es->analyze)
		instrument_option |= INSTRUMENT_ROWS;

	if (es->buffers)
		instrument_option |= INSTRUMENT_BUFFERS;
	if (es->wal)
		instrument_option |= INSTRUMENT_WAL;

	/*
	 * We always collect timing for the entire statement, even when node-level
	 * timing is off, so we don't look at es->timing here.  (We could skip
	 * this if !es->summary, but it's hardly worth the complication.)
	 */
	INSTR_TIME_SET_CURRENT(starttime);

	/*
	 * Use a snapshot with an updated command ID to ensure this query sees
	 * results of any previously executed queries.
	 */
	PushCopiedSnapshot(GetActiveSnapshot());
	UpdateActiveSnapshotCommandId();

	/*
	 * Normally we discard the query's output, but if explaining CREATE TABLE
	 * AS, we'd better use the appropriate tuple receiver.
	 */
	if (into)
		dest = CreateIntoRelDestReceiver(into);
	else
		dest = None_Receiver;

	/* Create a QueryDesc for the query */
	queryDesc = CreateQueryDesc(plannedstmt, queryString,
								GetActiveSnapshot(), InvalidSnapshot,
								dest, params, queryEnv, instrument_option);

	/* Select execution options */
	if (es->analyze)
		eflags = 0;				/* default run-to-completion flags */
	else
		eflags = EXEC_FLAG_EXPLAIN_ONLY;
	if (into)
		eflags |= GetIntoRelEFlags(into);

	/* call ExecutorStart to prepare the plan for execution */
	ExecutorStart(queryDesc, eflags);

	int64 peakMem = 0;

	/* Execute the plan for statistics if asked for */
	if (es->analyze)
	{
		ScanDirection dir;

		/* EXPLAIN ANALYZE CREATE TABLE AS WITH NO DATA is weird */
		if (into && into->skipData)
			dir = NoMovementScanDirection;
		else
			dir = ForwardScanDirection;

		/* Refresh the session stats before the start of the query */
		if (es->rpc)
		{
			YbRefreshSessionStatsBeforeExecution();
		}

		/* run the plan */
		ExecutorRun(queryDesc, dir, 0L, true);

		/* take a snapshot on the max PG memory consumption */
		peakMem = PgMemTracker.stmt_max_mem_bytes;

		/* run cleanup too */
		ExecutorFinish(queryDesc);

		/* Fetch stats collected at the query level (ie. not corresponding to
		 * any execution node) */
		if (es->rpc)
		{
			YbInstrumentation *yb_instr = &queryDesc->yb_query_stats->yb_instr;
			YbUpdateSessionStats(yb_instr);
			YbAggregateExplainableRPCRequestStat(es, yb_instr);
		}

		/* We can't run ExecutorEnd 'till we're done printing the stats... */
		totaltime += elapsed_time(&starttime);
	}

	ExplainOpenGroup("Query", NULL, true, es);

	/* Create textual dump of plan tree */
	ExplainPrintPlan(es, queryDesc);

	/*
	 * COMPUTE_QUERY_ID_REGRESS means COMPUTE_QUERY_ID_AUTO, but we don't show
	 * the queryid in any of the EXPLAIN plans to keep stable the results
	 * generated by regression test suites.
	 */
	if (es->verbose && plannedstmt->queryId != UINT64CONST(0) &&
		compute_query_id != COMPUTE_QUERY_ID_REGRESS)
	{
		/*
		 * Output the queryid as an int64 rather than a uint64 so we match
		 * what would be seen in the BIGINT pg_stat_statements.queryid column.
		 */
		ExplainPropertyInteger("Query Identifier", NULL, (int64)
							   plannedstmt->queryId, es);
	}

	/* Show buffer usage in planning */
	if (bufusage && show_variable_fields)
	{
		ExplainOpenGroup("Planning", "Planning", true, es);
		show_buffer_usage(es, bufusage, true);
		ExplainCloseGroup("Planning", "Planning", true, es);
	}

	if (es->summary && planduration && show_variable_fields)
	{
		double		plantime = INSTR_TIME_GET_DOUBLE(*planduration);

		ExplainPropertyFloat("Planning Time", "ms", 1000.0 * plantime, 3, es);
	}

	/* Print info about runtime of triggers */
	if (es->analyze)
		ExplainPrintTriggers(es, queryDesc);

	/*
	 * Print info about JITing. Tied to es->costs because we don't want to
	 * display this in regression tests, as it'd cause output differences
	 * depending on build options.  Might want to separate that out from COSTS
	 * at a later stage.
	 */
	if (es->costs)
		ExplainPrintJITSummary(es, queryDesc);

	/*
	 * Close down the query and free resources.  Include time for this in the
	 * total execution time (although it should be pretty minimal).
	 */
	INSTR_TIME_SET_CURRENT(starttime);

	ExecutorEnd(queryDesc);

	FreeQueryDesc(queryDesc);

	PopActiveSnapshot();

	/* We need a CCI just in case query expanded to multiple plans */
	if (es->analyze)
		CommandCounterIncrement();

	totaltime += elapsed_time(&starttime);

	/*
	 * We only report execution time if we actually ran the query (that is,
	 * the user specified ANALYZE), and if summary reporting is enabled (the
	 * user can set SUMMARY OFF to not have the timing information included in
	 * the output).  By default, ANALYZE sets SUMMARY to true.
	 */
	if (es->summary && es->analyze)
	{
		if (show_variable_fields)
			ExplainPropertyFloat("Execution Time", "ms", 1000.0 * totaltime, 3,
								 es);

		if (es->rpc)
		{
			/*
			 * Total RPC wait time is the sum of Read waits, Flush waits and Catalog waits.
			 */
			double total_rpc_wait = 0.0;
			if (es->yb_stats.read.count > 0.0)
				total_rpc_wait += es->yb_stats.read.wait_time;

			if (es->yb_stats.flush.count > 0.0)
				total_rpc_wait += es->yb_stats.flush.wait_time;

			if (es->yb_stats.catalog_read.count > 0.0)
				total_rpc_wait += es->yb_stats.catalog_read.wait_time;

			YbExplainState yb_es = {es, true};
			YbExplainRpcRequestStat(&yb_es, YB_STAT_LABEL_STORAGE_READ,
									es->yb_stats.read.count,
									es->yb_stats.read.wait_time);
			YbExplainStatWithoutTiming(&yb_es, YB_STAT_LABEL_STORAGE_WRITE,
									   es->yb_stats.write_count);
			YbExplainRpcRequestStat(&yb_es, YB_STAT_LABEL_CATALOG_READ,
									es->yb_stats.catalog_read.count,
									es->yb_stats.catalog_read.wait_time);
			YbExplainStatWithoutTiming(&yb_es, YB_STAT_LABEL_CATALOG_WRITE,
									   es->yb_stats.catalog_write_count);
			YbExplainRpcRequestStat(&yb_es, YB_STAT_LABEL_STORAGE_FLUSH,
									es->yb_stats.flush.count,
									es->yb_stats.flush.wait_time);

			if (es->debug) {
				for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i) {
					YbExplainRpcRequestGauge(&yb_es, i, es->yb_stats.storage_gauge_metrics[i],
											 false /* is_mean */);
				}
				for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i) {
					YbExplainRpcRequestCounter(&yb_es, i, es->yb_stats.storage_counter_metrics[i],
											   false /* is_mean */);
				}
				for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i) {
					YbExplainRpcRequestEvent(&yb_es, i,
											 &es->yb_stats.storage_event_metrics[i],
											 1.0 /* nloops */, false /* is_mean */);
				}
			}

			if (es->timing)
				ExplainPropertyFloat("Storage Execution Time", "ms",
									 total_rpc_wait / 1000000.0, 3, es);
		}

		if (IsYugaByteEnabled() && yb_enable_memory_tracking && show_variable_fields)
			YbAppendPgMemInfo(es, peakMem);
	}

	ExplainCloseGroup("Query", NULL, true, es);
}

/*
 * ExplainPrintSettings -
 *    Print summary of modified settings affecting query planning.
 */
static void
ExplainPrintSettings(ExplainState *es)
{
	int			num;
	struct config_generic **gucs;

	/* bail out if information about settings not requested */
	if (!es->settings)
		return;

	/* request an array of relevant settings */
	gucs = get_explain_guc_options(&num);

	if (es->format != EXPLAIN_FORMAT_TEXT)
	{
		ExplainOpenGroup("Settings", "Settings", true, es);

		for (int i = 0; i < num; i++)
		{
			char	   *setting;
			struct config_generic *conf = gucs[i];

			setting = GetConfigOptionByName(conf->name, NULL, true);

			ExplainPropertyText(conf->name, setting, es);
		}

		ExplainCloseGroup("Settings", "Settings", true, es);
	}
	else
	{
		StringInfoData str;

		/* In TEXT mode, print nothing if there are no options */
		if (num <= 0)
			return;

		initStringInfo(&str);

		for (int i = 0; i < num; i++)
		{
			char	   *setting;
			struct config_generic *conf = gucs[i];

			if (i > 0)
				appendStringInfoString(&str, ", ");

			setting = GetConfigOptionByName(conf->name, NULL, true);

			if (setting)
				appendStringInfo(&str, "%s = '%s'", conf->name, setting);
			else
				appendStringInfo(&str, "%s = NULL", conf->name);
		}

		ExplainPropertyText("Settings", str.data, es);
	}
}

/*
 * ExplainPrintPlan -
 *	  convert a QueryDesc's plan tree to text and append it to es->str
 *
 * The caller should have set up the options fields of *es, as well as
 * initializing the output buffer es->str.  Also, output formatting state
 * such as the indent level is assumed valid.  Plan-tree-specific fields
 * in *es are initialized here.
 *
 * NB: will not work on utility statements
 */
void
ExplainPrintPlan(ExplainState *es, QueryDesc *queryDesc)
{
	Bitmapset  *rels_used = NULL;
	PlanState  *ps;

	/* Set up ExplainState fields associated with this plan tree */
	Assert(queryDesc->plannedstmt != NULL);
	es->pstmt = queryDesc->plannedstmt;
	es->rtable = queryDesc->plannedstmt->rtable;
	ExplainPreScanNode(queryDesc->planstate, &rels_used);
	es->rtable_names = select_rtable_names_for_explain(es->rtable, rels_used);
	es->deparse_cxt = deparse_context_for_plan_tree(queryDesc->plannedstmt,
													es->rtable_names);
	es->printed_subplans = NULL;

	/*
	 * Sometimes we mark a Gather node as "invisible", which means that it's
	 * not to be displayed in EXPLAIN output.  The purpose of this is to allow
	 * running regression tests with force_parallel_mode=regress to get the
	 * same results as running the same tests with force_parallel_mode=off.
	 * Such marking is currently only supported on a Gather at the top of the
	 * plan.  We skip that node, and we must also hide per-worker detail data
	 * further down in the plan tree.
	 */
	ps = queryDesc->planstate;
	if (IsA(ps, GatherState) && ((Gather *) ps->plan)->invisible)
	{
		ps = outerPlanState(ps);
		es->hide_workers = true;
	}
	ExplainNode(ps, NIL, NULL, NULL, es);

	/*
	 * If requested, include information about GUC parameters with values that
	 * don't match the built-in defaults.
	 */
	ExplainPrintSettings(es);
}

/*
 * ExplainPrintTriggers -
 *	  convert a QueryDesc's trigger statistics to text and append it to
 *	  es->str
 *
 * The caller should have set up the options fields of *es, as well as
 * initializing the output buffer es->str.  Other fields in *es are
 * initialized here.
 */
void
ExplainPrintTriggers(ExplainState *es, QueryDesc *queryDesc)
{
	ResultRelInfo *rInfo;
	bool		show_relname;
	List	   *resultrels;
	List	   *routerels;
	List	   *targrels;
	ListCell   *l;

	resultrels = queryDesc->estate->es_opened_result_relations;
	routerels = queryDesc->estate->es_tuple_routing_result_relations;
	targrels = queryDesc->estate->es_trig_target_relations;

	ExplainOpenGroup("Triggers", "Triggers", false, es);

	show_relname = (list_length(resultrels) > 1 ||
					routerels != NIL || targrels != NIL);
	foreach(l, resultrels)
	{
		rInfo = (ResultRelInfo *) lfirst(l);
		report_triggers(rInfo, show_relname, es);
	}

	foreach(l, routerels)
	{
		rInfo = (ResultRelInfo *) lfirst(l);
		report_triggers(rInfo, show_relname, es);
	}

	foreach(l, targrels)
	{
		rInfo = (ResultRelInfo *) lfirst(l);
		report_triggers(rInfo, show_relname, es);
	}

	ExplainCloseGroup("Triggers", "Triggers", false, es);
}

/*
 * ExplainPrintJITSummary -
 *    Print summarized JIT instrumentation from leader and workers
 */
void
ExplainPrintJITSummary(ExplainState *es, QueryDesc *queryDesc)
{
	JitInstrumentation ji = {0};

	if (!(queryDesc->estate->es_jit_flags & PGJIT_PERFORM))
		return;

	/*
	 * Work with a copy instead of modifying the leader state, since this
	 * function may be called twice
	 */
	if (queryDesc->estate->es_jit)
		InstrJitAgg(&ji, &queryDesc->estate->es_jit->instr);

	/* If this process has done JIT in parallel workers, merge stats */
	if (queryDesc->estate->es_jit_worker_instr)
		InstrJitAgg(&ji, queryDesc->estate->es_jit_worker_instr);

	ExplainPrintJIT(es, queryDesc->estate->es_jit_flags, &ji);
}

/*
 * ExplainPrintJIT -
 *	  Append information about JITing to es->str.
 */
static void
ExplainPrintJIT(ExplainState *es, int jit_flags, JitInstrumentation *ji)
{
	instr_time	total_time;

	/* don't print information if no JITing happened */
	if (!ji || ji->created_functions == 0)
		return;

	/* calculate total time */
	INSTR_TIME_SET_ZERO(total_time);
	INSTR_TIME_ADD(total_time, ji->generation_counter);
	INSTR_TIME_ADD(total_time, ji->inlining_counter);
	INSTR_TIME_ADD(total_time, ji->optimization_counter);
	INSTR_TIME_ADD(total_time, ji->emission_counter);

	ExplainOpenGroup("JIT", "JIT", true, es);

	/* for higher density, open code the text output format */
	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		ExplainIndentText(es);
		appendStringInfoString(es->str, "JIT:\n");
		es->indent++;

		ExplainPropertyInteger("Functions", NULL, ji->created_functions, es);

		ExplainIndentText(es);
		appendStringInfo(es->str, "Options: %s %s, %s %s, %s %s, %s %s\n",
						 "Inlining", jit_flags & PGJIT_INLINE ? "true" : "false",
						 "Optimization", jit_flags & PGJIT_OPT3 ? "true" : "false",
						 "Expressions", jit_flags & PGJIT_EXPR ? "true" : "false",
						 "Deforming", jit_flags & PGJIT_DEFORM ? "true" : "false");

		if (es->analyze && es->timing)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str,
							 "Timing: %s %.3f ms, %s %.3f ms, %s %.3f ms, %s %.3f ms, %s %.3f ms\n",
							 "Generation", 1000.0 * INSTR_TIME_GET_DOUBLE(ji->generation_counter),
							 "Inlining", 1000.0 * INSTR_TIME_GET_DOUBLE(ji->inlining_counter),
							 "Optimization", 1000.0 * INSTR_TIME_GET_DOUBLE(ji->optimization_counter),
							 "Emission", 1000.0 * INSTR_TIME_GET_DOUBLE(ji->emission_counter),
							 "Total", 1000.0 * INSTR_TIME_GET_DOUBLE(total_time));
		}

		es->indent--;
	}
	else
	{
		ExplainPropertyInteger("Functions", NULL, ji->created_functions, es);

		ExplainOpenGroup("Options", "Options", true, es);
		ExplainPropertyBool("Inlining", jit_flags & PGJIT_INLINE, es);
		ExplainPropertyBool("Optimization", jit_flags & PGJIT_OPT3, es);
		ExplainPropertyBool("Expressions", jit_flags & PGJIT_EXPR, es);
		ExplainPropertyBool("Deforming", jit_flags & PGJIT_DEFORM, es);
		ExplainCloseGroup("Options", "Options", true, es);

		if (es->analyze && es->timing)
		{
			ExplainOpenGroup("Timing", "Timing", true, es);

			ExplainPropertyFloat("Generation", "ms",
								 1000.0 * INSTR_TIME_GET_DOUBLE(ji->generation_counter),
								 3, es);
			ExplainPropertyFloat("Inlining", "ms",
								 1000.0 * INSTR_TIME_GET_DOUBLE(ji->inlining_counter),
								 3, es);
			ExplainPropertyFloat("Optimization", "ms",
								 1000.0 * INSTR_TIME_GET_DOUBLE(ji->optimization_counter),
								 3, es);
			ExplainPropertyFloat("Emission", "ms",
								 1000.0 * INSTR_TIME_GET_DOUBLE(ji->emission_counter),
								 3, es);
			ExplainPropertyFloat("Total", "ms",
								 1000.0 * INSTR_TIME_GET_DOUBLE(total_time),
								 3, es);

			ExplainCloseGroup("Timing", "Timing", true, es);
		}
	}

	ExplainCloseGroup("JIT", "JIT", true, es);
}

/*
 * ExplainQueryText -
 *	  add a "Query Text" node that contains the actual text of the query
 *
 * The caller should have set up the options fields of *es, as well as
 * initializing the output buffer es->str.
 *
 */
void
ExplainQueryText(ExplainState *es, QueryDesc *queryDesc)
{
	if (queryDesc->sourceText)
		ExplainPropertyText("Query Text", queryDesc->sourceText, es);
}

/*
 * report_triggers -
 *		report execution stats for a single relation's triggers
 */
static void
report_triggers(ResultRelInfo *rInfo, bool show_relname, ExplainState *es)
{
	int			nt;

	if (!rInfo->ri_TrigDesc || !rInfo->ri_TrigInstrument)
		return;
	for (nt = 0; nt < rInfo->ri_TrigDesc->numtriggers; nt++)
	{
		Trigger    *trig = rInfo->ri_TrigDesc->triggers + nt;
		Instrumentation *instr = rInfo->ri_TrigInstrument + nt;
		char	   *relname;
		char	   *conname = NULL;

		/* Must clean up instrumentation state */
		InstrEndLoop(instr);

		/*
		 * We ignore triggers that were never invoked; they likely aren't
		 * relevant to the current query type.
		 */
		if (instr->ntuples == 0)
			continue;

		ExplainOpenGroup("Trigger", NULL, true, es);

		relname = RelationGetRelationName(rInfo->ri_RelationDesc);
		if (OidIsValid(trig->tgconstraint))
			conname = get_constraint_name(trig->tgconstraint);

		/*
		 * In text format, we avoid printing both the trigger name and the
		 * constraint name unless VERBOSE is specified.  In non-text formats
		 * we just print everything.
		 */
		if (es->format == EXPLAIN_FORMAT_TEXT)
		{
			if (es->verbose || conname == NULL)
				appendStringInfo(es->str, "Trigger %s", trig->tgname);
			else
				appendStringInfoString(es->str, "Trigger");
			if (conname)
				appendStringInfo(es->str, " for constraint %s", conname);
			if (show_relname)
				appendStringInfo(es->str, " on %s", relname);
			if (es->timing)
				appendStringInfo(es->str, ": time=%.3f calls=%.0f\n",
								 1000.0 * instr->total, instr->ntuples);
			else
				appendStringInfo(es->str, ": calls=%.0f\n", instr->ntuples);
		}
		else
		{
			ExplainPropertyText("Trigger Name", trig->tgname, es);
			if (conname)
				ExplainPropertyText("Constraint Name", conname, es);
			ExplainPropertyText("Relation", relname, es);
			if (es->timing)
				ExplainPropertyFloat("Time", "ms", 1000.0 * instr->total, 3,
									 es);
			ExplainPropertyFloat("Calls", NULL, instr->ntuples, 0, es);
		}

		if (conname)
			pfree(conname);

		ExplainCloseGroup("Trigger", NULL, true, es);
	}
}

/* Compute elapsed time in seconds since given timestamp */
static double
elapsed_time(instr_time *starttime)
{
	instr_time	endtime;

	INSTR_TIME_SET_CURRENT(endtime);
	INSTR_TIME_SUBTRACT(endtime, *starttime);
	return INSTR_TIME_GET_DOUBLE(endtime);
}

/*
 * ExplainPreScanNode -
 *	  Prescan the planstate tree to identify which RTEs are referenced
 *
 * Adds the relid of each referenced RTE to *rels_used.  The result controls
 * which RTEs are assigned aliases by select_rtable_names_for_explain.
 * This ensures that we don't confusingly assign un-suffixed aliases to RTEs
 * that never appear in the EXPLAIN output (such as inheritance parents).
 */
static bool
ExplainPreScanNode(PlanState *planstate, Bitmapset **rels_used)
{
	Plan	   *plan = planstate->plan;

	switch (nodeTag(plan))
	{
		case T_SeqScan:
		case T_YbSeqScan:
		case T_SampleScan:
		case T_IndexScan:
		case T_IndexOnlyScan:
		case T_BitmapHeapScan:
		case T_TidScan:
		case T_TidRangeScan:
		case T_SubqueryScan:
		case T_FunctionScan:
		case T_TableFuncScan:
		case T_ValuesScan:
		case T_CteScan:
		case T_NamedTuplestoreScan:
		case T_WorkTableScan:
			*rels_used = bms_add_member(*rels_used,
										((Scan *) plan)->scanrelid);
			break;
		case T_ForeignScan:
			*rels_used = bms_add_members(*rels_used,
										 ((ForeignScan *) plan)->fs_relids);
			break;
		case T_CustomScan:
			*rels_used = bms_add_members(*rels_used,
										 ((CustomScan *) plan)->custom_relids);
			break;
		case T_ModifyTable:
			*rels_used = bms_add_member(*rels_used,
										((ModifyTable *) plan)->nominalRelation);
			if (((ModifyTable *) plan)->exclRelRTI)
				*rels_used = bms_add_member(*rels_used,
											((ModifyTable *) plan)->exclRelRTI);
			break;
		case T_Append:
			*rels_used = bms_add_members(*rels_used,
										 ((Append *) plan)->apprelids);
			break;
		case T_MergeAppend:
			*rels_used = bms_add_members(*rels_used,
										 ((MergeAppend *) plan)->apprelids);
			break;
		default:
			break;
	}

	return planstate_tree_walker(planstate, ExplainPreScanNode, rels_used);
}

/*
 * ExplainNode -
 *	  Appends a description of a plan tree to es->str
 *
 * planstate points to the executor state node for the current plan node.
 * We need to work from a PlanState node, not just a Plan node, in order to
 * get at the instrumentation data (if any) as well as the list of subplans.
 *
 * ancestors is a list of parent Plan and SubPlan nodes, most-closely-nested
 * first.  These are needed in order to interpret PARAM_EXEC Params.
 *
 * relationship describes the relationship of this plan node to its parent
 * (eg, "Outer", "Inner"); it can be null at top level.  plan_name is an
 * optional name to be attached to the node.
 *
 * In text format, es->indent is controlled in this function since we only
 * want it to change at plan-node boundaries (but a few subroutines will
 * transiently increment it).  In non-text formats, es->indent corresponds
 * to the nesting depth of logical output groups, and therefore is controlled
 * by ExplainOpenGroup/ExplainCloseGroup.
 */
static void
ExplainNode(PlanState *planstate, List *ancestors,
			const char *relationship, const char *plan_name,
			ExplainState *es)
{
	Plan	   *plan = planstate->plan;
	const char *pname;			/* node type name for text output */
	const char *sname;			/* node type name for non-text output */
	const char *strategy = NULL;
	const char *partialmode = NULL;
	const char *operation = NULL;
	const char *custom_name = NULL;
	ExplainWorkersState *save_workers_state = es->workers_state;
	int			save_indent = es->indent;
	bool		haschildren;

	if (planstate->instrument)
	{
		YbAggregateExplainableRPCRequestStat(es,
											 &planstate->instrument->yb_instr);
	}

	/*
	 * Prepare per-worker output buffers, if needed.  We'll append the data in
	 * these to the main output string further down.
	 */
	if (planstate->worker_instrument && es->analyze && !es->hide_workers)
		es->workers_state = ExplainCreateWorkersState(planstate->worker_instrument->num_workers);
	else
		es->workers_state = NULL;

	/* Identify plan node type, and print generic details */
	switch (nodeTag(plan))
	{
		case T_Result:
			pname = sname = "Result";
			break;
		case T_ProjectSet:
			pname = sname = "ProjectSet";
			break;
		case T_ModifyTable:
			sname = "ModifyTable";
			switch (((ModifyTable *) plan)->operation)
			{
				case CMD_INSERT:
					pname = operation = "Insert";
					break;
				case CMD_UPDATE:
					pname = operation = "Update";
					break;
				case CMD_DELETE:
					pname = operation = "Delete";
					break;
				case CMD_MERGE:
					pname = operation = "Merge";
					break;
				default:
					pname = "???";
					break;
			}
			break;
		case T_Append:
			pname = sname = "Append";
			break;
		case T_MergeAppend:
			pname = sname = "Merge Append";
			break;
		case T_RecursiveUnion:
			pname = sname = "Recursive Union";
			break;
		case T_BitmapAnd:
			pname = sname = "BitmapAnd";
			break;
		case T_BitmapOr:
			pname = sname = "BitmapOr";
			break;
		case T_NestLoop:
			pname = sname = "Nested Loop";
			break;
		case T_YbBatchedNestLoop:
			pname = sname = "YB Batched Nested Loop";
			break;
		case T_MergeJoin:
			pname = "Merge";	/* "Join" gets added by jointype switch */
			sname = "Merge Join";
			break;
		case T_HashJoin:
			pname = "Hash";		/* "Join" gets added by jointype switch */
			sname = "Hash Join";
			break;
		case T_SeqScan:
			pname = sname = "Seq Scan";
			break;
		case T_YbSeqScan:
			pname = sname = "Seq Scan";
			break;
		case T_SampleScan:
			pname = sname = "Sample Scan";
			break;
		case T_Gather:
			pname = sname = "Gather";
			break;
		case T_GatherMerge:
			pname = sname = "Gather Merge";
			break;
		case T_IndexScan:
			pname = sname = "Index Scan";
			if (((IndexScan *) plan)->yb_distinct_prefixlen > 0)
				pname = sname = "Distinct Index Scan";
			break;
		case T_IndexOnlyScan:
			pname = sname = "Index Only Scan";
			if (((IndexOnlyScan *) plan)->yb_distinct_prefixlen > 0)
				pname = sname = "Distinct Index Only Scan";
			break;
		case T_BitmapIndexScan:
			pname = sname = "Bitmap Index Scan";
			break;
		case T_BitmapHeapScan:
			pname = sname = "Bitmap Heap Scan";
			break;
		case T_TidScan:
			pname = sname = "Tid Scan";
			break;
		case T_TidRangeScan:
			pname = sname = "Tid Range Scan";
			break;
		case T_SubqueryScan:
			pname = sname = "Subquery Scan";
			break;
		case T_FunctionScan:
			pname = sname = "Function Scan";
			break;
		case T_TableFuncScan:
			pname = sname = "Table Function Scan";
			break;
		case T_ValuesScan:
			pname = sname = "Values Scan";
			break;
		case T_CteScan:
			pname = sname = "CTE Scan";
			break;
		case T_NamedTuplestoreScan:
			pname = sname = "Named Tuplestore Scan";
			break;
		case T_WorkTableScan:
			pname = sname = "WorkTable Scan";
			break;
		case T_ForeignScan:
			sname = "Foreign Scan";
			switch (((ForeignScan *) plan)->operation)
			{
				case CMD_SELECT:
					/* Don't need to expose implementation details */
					if (IsYBRelation(((ScanState*) planstate)->ss_currentRelation))
						sname = pname = "YB Foreign Scan";
					else
						pname = "Foreign Scan";
					operation = "Select";
					break;
				case CMD_INSERT:
					pname = "Foreign Insert";
					operation = "Insert";
					break;
				case CMD_UPDATE:
					pname = "Foreign Update";
					operation = "Update";
					break;
				case CMD_DELETE:
					pname = "Foreign Delete";
					operation = "Delete";
					break;
				default:
					pname = "???";
					break;
			}
			break;
		case T_CustomScan:
			sname = "Custom Scan";
			custom_name = ((CustomScan *) plan)->methods->CustomName;
			if (custom_name)
				pname = psprintf("Custom Scan (%s)", custom_name);
			else
				pname = sname;
			break;
		case T_Material:
			pname = sname = "Materialize";
			break;
		case T_Memoize:
			pname = sname = "Memoize";
			break;
		case T_Sort:
			pname = sname = "Sort";
			break;
		case T_IncrementalSort:
			pname = sname = "Incremental Sort";
			break;
		case T_Group:
			pname = sname = "Group";
			break;
		case T_Agg:
			{
				Agg		   *agg = (Agg *) plan;

				sname = "Aggregate";
				switch (agg->aggstrategy)
				{
					case AGG_PLAIN:
						pname = "Aggregate";
						strategy = "Plain";
						break;
					case AGG_SORTED:
						pname = "GroupAggregate";
						strategy = "Sorted";
						break;
					case AGG_HASHED:
						pname = "HashAggregate";
						strategy = "Hashed";
						break;
					case AGG_MIXED:
						pname = "MixedAggregate";
						strategy = "Mixed";
						break;
					default:
						pname = "Aggregate ???";
						strategy = "???";
						break;
				}

				if (DO_AGGSPLIT_SKIPFINAL(agg->aggsplit))
				{
					if (((AggState*) planstate)->yb_pushdown_supported)
						/*
						 * If partial aggregate is pushed down, it does not
						 * really do anything, since entire operation is
						 * delegated to DocDB.
						 */
						partialmode = "Noop";
					else
						partialmode = "Partial";
					pname = psprintf("%s %s", partialmode, pname);
				}
				else if (DO_AGGSPLIT_COMBINE(agg->aggsplit) ||
						 ((AggState*) planstate)->yb_pushdown_supported)
				{
					partialmode = "Finalize";
					pname = psprintf("%s %s", partialmode, pname);
				}
				else
					partialmode = "Simple";
			}
			break;
		case T_WindowAgg:
			pname = sname = "WindowAgg";
			break;
		case T_Unique:
			pname = sname = "Unique";
			break;
		case T_SetOp:
			sname = "SetOp";
			switch (((SetOp *) plan)->strategy)
			{
				case SETOP_SORTED:
					pname = "SetOp";
					strategy = "Sorted";
					break;
				case SETOP_HASHED:
					pname = "HashSetOp";
					strategy = "Hashed";
					break;
				default:
					pname = "SetOp ???";
					strategy = "???";
					break;
			}
			break;
		case T_LockRows:
			pname = sname = "LockRows";
			break;
		case T_Limit:
			pname = sname = "Limit";
			break;
		case T_Hash:
			pname = sname = "Hash";
			break;
		default:
			pname = sname = "???";
			break;
	}

	ExplainOpenGroup("Plan",
					 relationship ? NULL : "Plan",
					 true, es);

	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		if (plan_name)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str, "%s\n", plan_name);
			es->indent++;
		}
		if (es->indent)
		{
			ExplainIndentText(es);
			appendStringInfoString(es->str, "->  ");
			es->indent += 2;
		}
		if (plan->parallel_aware)
			appendStringInfoString(es->str, "Parallel ");
		if (plan->async_capable)
			appendStringInfoString(es->str, "Async ");
		appendStringInfoString(es->str, pname);
		es->indent++;
	}
	else
	{
		ExplainPropertyText("Node Type", sname, es);
		if (strategy)
			ExplainPropertyText("Strategy", strategy, es);
		if (partialmode)
			ExplainPropertyText("Partial Mode", partialmode, es);
		if (operation)
			ExplainPropertyText("Operation", operation, es);
		if (relationship)
			ExplainPropertyText("Parent Relationship", relationship, es);
		if (plan_name)
			ExplainPropertyText("Subplan Name", plan_name, es);
		if (custom_name)
			ExplainPropertyText("Custom Plan Provider", custom_name, es);
		ExplainPropertyBool("Parallel Aware", plan->parallel_aware, es);
		ExplainPropertyBool("Async Capable", plan->async_capable, es);
	}

	switch (nodeTag(plan))
	{
		case T_SeqScan:
		case T_YbSeqScan:
		case T_SampleScan:
		case T_BitmapHeapScan:
		case T_TidScan:
		case T_TidRangeScan:
		case T_SubqueryScan:
		case T_FunctionScan:
		case T_TableFuncScan:
		case T_ValuesScan:
		case T_CteScan:
		case T_WorkTableScan:
			YbExplainScanLocks(YB_NO_SCAN_LOCK, es);
			ExplainScanTarget((Scan *) plan, es);
			break;
		case T_ForeignScan:
		case T_CustomScan:
			YbExplainScanLocks(YB_NO_SCAN_LOCK, es);
			if (((Scan *) plan)->scanrelid > 0)
				ExplainScanTarget((Scan *) plan, es);
			break;
		case T_IndexScan:
			{
				IndexScan  *indexscan = (IndexScan *) plan;

				YbExplainScanLocks(indexscan->yb_lock_mechanism, es);
				ExplainIndexScanDetails(indexscan->indexid,
										indexscan->indexorderdir,
										indexscan->yb_estimated_num_nexts,
										indexscan->yb_estimated_num_seeks,
										es);
				ExplainScanTarget((Scan *) indexscan, es);
			}
			break;
		case T_IndexOnlyScan:
			{
				IndexOnlyScan *indexonlyscan = (IndexOnlyScan *) plan;

				ExplainIndexScanDetails(indexonlyscan->indexid,
										indexonlyscan->indexorderdir,
										indexonlyscan->yb_estimated_num_nexts,
										indexonlyscan->yb_estimated_num_seeks,
										es);
				ExplainScanTarget((Scan *) indexonlyscan, es);
			}
			break;
		case T_BitmapIndexScan:
			{
				BitmapIndexScan *bitmapindexscan = (BitmapIndexScan *) plan;
				const char *indexname =
				explain_get_index_name(bitmapindexscan->indexid);

				if (es->format == EXPLAIN_FORMAT_TEXT)
					appendStringInfo(es->str, " on %s",
									 quote_identifier(indexname));
				else
					ExplainPropertyText("Index Name", indexname, es);
			}
			break;
		case T_ModifyTable:
			ExplainModifyTarget((ModifyTable *) plan, es);
			break;
		case T_NestLoop:
		case T_YbBatchedNestLoop:
		case T_MergeJoin:
		case T_HashJoin:
			{
				const char *jointype;

				switch (((Join *) plan)->jointype)
				{
					case JOIN_INNER:
						jointype = "Inner";
						break;
					case JOIN_LEFT:
						jointype = "Left";
						break;
					case JOIN_FULL:
						jointype = "Full";
						break;
					case JOIN_RIGHT:
						jointype = "Right";
						break;
					case JOIN_SEMI:
						jointype = "Semi";
						break;
					case JOIN_ANTI:
						jointype = "Anti";
						break;
					default:
						jointype = "???";
						break;
				}
				if (es->format == EXPLAIN_FORMAT_TEXT)
				{
					/*
					 * For historical reasons, the join type is interpolated
					 * into the node type name...
					 */
					if (((Join *) plan)->jointype != JOIN_INNER)
						appendStringInfo(es->str, " %s Join", jointype);
					else if (!IsA(plan, NestLoop))
						appendStringInfoString(es->str, " Join");
				}
				else
					ExplainPropertyText("Join Type", jointype, es);
			}
			break;
		case T_SetOp:
			{
				const char *setopcmd;

				switch (((SetOp *) plan)->cmd)
				{
					case SETOPCMD_INTERSECT:
						setopcmd = "Intersect";
						break;
					case SETOPCMD_INTERSECT_ALL:
						setopcmd = "Intersect All";
						break;
					case SETOPCMD_EXCEPT:
						setopcmd = "Except";
						break;
					case SETOPCMD_EXCEPT_ALL:
						setopcmd = "Except All";
						break;
					default:
						setopcmd = "???";
						break;
				}
				if (es->format == EXPLAIN_FORMAT_TEXT)
					appendStringInfo(es->str, " %s", setopcmd);
				else
					ExplainPropertyText("Command", setopcmd, es);
			}
			break;
		case T_LockRows:
			YbExplainLockRows(es);
			break;
		default:
			break;
	}

	if (es->costs)
	{
		if (es->format == EXPLAIN_FORMAT_TEXT)
		{
			appendStringInfo(es->str, "  (cost=%.2f..%.2f rows=%.0f width=%d)",
							 plan->startup_cost, plan->total_cost,
							 plan->plan_rows, plan->plan_width);
		}
		else
		{
			ExplainPropertyFloat("Startup Cost", NULL, plan->startup_cost,
								 2, es);
			ExplainPropertyFloat("Total Cost", NULL, plan->total_cost,
								 2, es);
			ExplainPropertyFloat("Plan Rows", NULL, plan->plan_rows,
								 0, es);
			ExplainPropertyInteger("Plan Width", NULL, plan->plan_width,
								   es);
		}
	}

	/*
	 * We have to forcibly clean up the instrumentation state because we
	 * haven't done ExecutorEnd yet.  This is pretty grotty ...
	 *
	 * Note: contrib/auto_explain could cause instrumentation to be set up
	 * even though we didn't ask for it here.  Be careful not to print any
	 * instrumentation results the user didn't ask for.  But we do the
	 * InstrEndLoop call anyway, if possible, to reduce the number of cases
	 * auto_explain has to contend with.
	 */
	if (planstate->instrument)
		InstrEndLoop(planstate->instrument);

	if (es->analyze &&
		planstate->instrument && planstate->instrument->nloops > 0)
	{
		double		nloops = planstate->instrument->nloops;
		double		startup_ms = 1000.0 * planstate->instrument->startup / nloops;
		double		total_ms = 1000.0 * planstate->instrument->total / nloops;
		double		rows = planstate->instrument->ntuples / nloops;

		if (es->format == EXPLAIN_FORMAT_TEXT)
		{
			if (es->timing)
				appendStringInfo(es->str,
								 " (actual time=%.3f..%.3f rows=%.0f loops=%.0f)",
								 startup_ms, total_ms, rows, nloops);
			else
				appendStringInfo(es->str,
								 " (actual rows=%.0f loops=%.0f)",
								 rows, nloops);
		}
		else
		{
			if (es->timing)
			{
				ExplainPropertyFloat("Actual Startup Time", "ms", startup_ms,
									 3, es);
				ExplainPropertyFloat("Actual Total Time", "ms", total_ms,
									 3, es);
			}
			ExplainPropertyFloat("Actual Rows", NULL, rows, 0, es);
			ExplainPropertyFloat("Actual Loops", NULL, nloops, 0, es);
		}
	}
	else if (es->analyze)
	{
		if (es->format == EXPLAIN_FORMAT_TEXT)
			appendStringInfoString(es->str, " (never executed)");
		else
		{
			if (es->timing)
			{
				ExplainPropertyFloat("Actual Startup Time", "ms", 0.0, 3, es);
				ExplainPropertyFloat("Actual Total Time", "ms", 0.0, 3, es);
			}
			ExplainPropertyFloat("Actual Rows", NULL, 0.0, 0, es);
			ExplainPropertyFloat("Actual Loops", NULL, 0.0, 0, es);
		}
	}

	/* in text format, first line ends here */
	if (es->format == EXPLAIN_FORMAT_TEXT)
		appendStringInfoChar(es->str, '\n');

	/* prepare per-worker general execution details */
	if (es->workers_state && es->verbose)
	{
		WorkerInstrumentation *w = planstate->worker_instrument;

		for (int n = 0; n < w->num_workers; n++)
		{
			Instrumentation *instrument = &w->instrument[n];
			double		nloops = instrument->nloops;
			double		startup_ms;
			double		total_ms;
			double		rows;

			if (nloops <= 0)
				continue;
			startup_ms = 1000.0 * instrument->startup / nloops;
			total_ms = 1000.0 * instrument->total / nloops;
			rows = instrument->ntuples / nloops;

			ExplainOpenWorker(n, es);

			if (es->format == EXPLAIN_FORMAT_TEXT)
			{
				ExplainIndentText(es);
				if (es->timing)
					appendStringInfo(es->str,
									 "actual time=%.3f..%.3f rows=%.0f loops=%.0f\n",
									 startup_ms, total_ms, rows, nloops);
				else
					appendStringInfo(es->str,
									 "actual rows=%.0f loops=%.0f\n",
									 rows, nloops);
			}
			else
			{
				if (es->timing)
				{
					ExplainPropertyFloat("Actual Startup Time", "ms",
										 startup_ms, 3, es);
					ExplainPropertyFloat("Actual Total Time", "ms",
										 total_ms, 3, es);
				}
				ExplainPropertyFloat("Actual Rows", NULL, rows, 0, es);
				ExplainPropertyFloat("Actual Loops", NULL, nloops, 0, es);
			}

			ExplainCloseWorker(n, es);
		}
	}

	/* target list */
	if (es->verbose)
		show_plan_tlist(planstate, ancestors, es);

	/* unique join */
	switch (nodeTag(plan))
	{
		case T_NestLoop:
		case T_YbBatchedNestLoop:
		case T_MergeJoin:
		case T_HashJoin:
			/* try not to be too chatty about this in text mode */
			if (es->format != EXPLAIN_FORMAT_TEXT ||
				(es->verbose && ((Join *) plan)->inner_unique))
				ExplainPropertyBool("Inner Unique",
									((Join *) plan)->inner_unique,
									es);
			break;
		default:
			break;
	}

	const bool is_yb_rpc_stats_required = es->rpc && es->analyze &&
										  planstate->instrument->nloops > 0;

	/* quals, sort keys, etc */
	switch (nodeTag(plan))
	{
		case T_IndexScan:
			show_scan_qual(((IndexScan *) plan)->indexqualorig,
						   "Index Cond", planstate, ancestors, es);
			if (((IndexScan *) plan)->indexqualorig)
				show_instrumentation_count("Rows Removed by Index Recheck", 2,
										   planstate, es);
			/*
			 * Quals are shown in the order they are applied: index pushdown,
			 * relation pushdown, local clauses.
			 */
			show_scan_qual(((IndexScan *) plan)->indexorderbyorig,
						   "Order By", planstate, ancestors, es);
			/*
			 * YB: Distinct prefix during Distinct Index Scan.
			 * Shown after ORDER BY clause and before remote filters since
			 * that's currently the order of operations in DocDB.
			 */
			YbExplainDistinctPrefixLen(
				((IndexScan *) plan)->yb_distinct_prefixlen, es);
			show_scan_qual(((IndexScan *) plan)->yb_idx_pushdown.quals,
						   "Remote Index Filter", planstate, ancestors, es);
			show_scan_qual(((IndexScan *) plan)->yb_rel_pushdown.quals,
						   "Remote Filter", planstate, ancestors, es);
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, true/*indexScan*/, es);
			if (es->debug && yb_enable_base_scans_cost_model)
			{
				ExplainPropertyFloat(
					"Estimated Seeks", NULL,
					((IndexScan *) plan)->yb_estimated_num_seeks, 0, es);
				ExplainPropertyFloat(
					"Estimated Nexts", NULL,
					((IndexScan *) plan)->yb_estimated_num_nexts, 0, es);
			}
			break;
		case T_IndexOnlyScan:
			show_scan_qual(((IndexOnlyScan *) plan)->indexqual,
						   "Index Cond", planstate, ancestors, es);
			if (((IndexOnlyScan *) plan)->recheckqual)
				show_instrumentation_count("Rows Removed by Index Recheck", 2,
										   planstate, es);
			show_scan_qual(((IndexOnlyScan *) plan)->indexorderby,
						   "Order By", planstate, ancestors, es);
			/*
			 * YB: Distinct prefix during HybridScan.
			 * Shown after ORDER BY clause and before remote filters since
			 * that's currently the order of operations in DocDB.
			 */
			YbExplainDistinctPrefixLen(
				((IndexOnlyScan *) plan)->yb_distinct_prefixlen, es);
			/*
			 * Remote filter is applied first, so it is output first.
			 */
			show_scan_qual(((IndexOnlyScan *) plan)->yb_pushdown.quals,
						   "Remote Filter", planstate, ancestors, es);
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			if (es->analyze)
				ExplainPropertyFloat("Heap Fetches", NULL,
									 planstate->instrument->ntuples2, 0, es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, true/*indexScan*/, es);
			if (es->debug && yb_enable_base_scans_cost_model)
			{
				ExplainPropertyFloat(
					"Estimated Seeks", NULL,
					((IndexOnlyScan *) plan)->yb_estimated_num_seeks, 0, es);
				ExplainPropertyFloat(
					"Estimated Nexts", NULL,
					((IndexOnlyScan *) plan)->yb_estimated_num_nexts, 0, es);
			}
			break;
		case T_BitmapIndexScan:
			show_scan_qual(((BitmapIndexScan *) plan)->indexqualorig,
						   "Index Cond", planstate, ancestors, es);
			break;
		case T_BitmapHeapScan:
			show_scan_qual(((BitmapHeapScan *) plan)->bitmapqualorig,
						   "Recheck Cond", planstate, ancestors, es);
			if (((BitmapHeapScan *) plan)->bitmapqualorig)
				show_instrumentation_count("Rows Removed by Index Recheck", 2,
										   planstate, es);
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			if (es->analyze)
				show_tidbitmap_info((BitmapHeapScanState *) planstate, es);
			break;
		case T_SampleScan:
			show_tablesample(((SampleScan *) plan)->tablesample,
							 planstate, ancestors, es);
			/* fall through to print additional fields the same as SeqScan */
			switch_fallthrough();
		case T_SeqScan:
		case T_ValuesScan:
		case T_CteScan:
		case T_NamedTuplestoreScan:
		case T_WorkTableScan:
		case T_SubqueryScan:
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, false/*indexScan*/, es);
			break;
		case T_YbSeqScan:
			/*
			 * Remote filter is applied first, so it is output first.
			 */
			show_scan_qual(((YbSeqScan *) plan)->yb_pushdown.quals,
						   "Remote Filter", planstate, ancestors, es);
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, false /*indexScan*/, es);
			if (es->debug && yb_enable_base_scans_cost_model)
			{
				ExplainPropertyFloat(
					"Estimated Seeks", NULL,
					((YbSeqScan *) plan)->yb_estimated_num_seeks, 0, es);
				ExplainPropertyFloat(
					"Estimated Nexts", NULL,
					((YbSeqScan *) plan)->yb_estimated_num_nexts, 0, es);
			}
			break;
		case T_Gather:
			{
				Gather	   *gather = (Gather *) plan;

				show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
				if (plan->qual)
					show_instrumentation_count("Rows Removed by Filter", 1,
											   planstate, es);
				ExplainPropertyInteger("Workers Planned", NULL,
									   gather->num_workers, es);

				/* Show params evaluated at gather node */
				if (gather->initParam)
					show_eval_params(gather->initParam, es);

				if (es->analyze)
				{
					int			nworkers;

					nworkers = ((GatherState *) planstate)->nworkers_launched;
					ExplainPropertyInteger("Workers Launched", NULL,
										   nworkers, es);
				}

				if (gather->single_copy || es->format != EXPLAIN_FORMAT_TEXT)
					ExplainPropertyBool("Single Copy", gather->single_copy, es);
			}
			break;
		case T_GatherMerge:
			{
				GatherMerge *gm = (GatherMerge *) plan;

				show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
				if (plan->qual)
					show_instrumentation_count("Rows Removed by Filter", 1,
											   planstate, es);
				ExplainPropertyInteger("Workers Planned", NULL,
									   gm->num_workers, es);

				/* Show params evaluated at gather-merge node */
				if (gm->initParam)
					show_eval_params(gm->initParam, es);

				if (es->analyze)
				{
					int			nworkers;

					nworkers = ((GatherMergeState *) planstate)->nworkers_launched;
					ExplainPropertyInteger("Workers Launched", NULL,
										   nworkers, es);
				}
			}
			break;
		case T_FunctionScan:
			if (es->verbose)
			{
				List	   *fexprs = NIL;
				ListCell   *lc;

				foreach(lc, ((FunctionScan *) plan)->functions)
				{
					RangeTblFunction *rtfunc = (RangeTblFunction *) lfirst(lc);

					fexprs = lappend(fexprs, rtfunc->funcexpr);
				}
				/* We rely on show_expression to insert commas as needed */
				show_expression((Node *) fexprs,
								"Function Call", planstate, ancestors,
								es->verbose, es);
			}
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			break;
		case T_TableFuncScan:
			if (es->verbose)
			{
				TableFunc  *tablefunc = ((TableFuncScan *) plan)->tablefunc;

				show_expression((Node *) tablefunc,
								"Table Function Call", planstate, ancestors,
								es->verbose, es);
			}
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			break;
		case T_TidScan:
			{
				/*
				 * The tidquals list has OR semantics, so be sure to show it
				 * as an OR condition.
				 */
				List	   *tidquals = ((TidScan *) plan)->tidquals;

				if (list_length(tidquals) > 1)
					tidquals = list_make1(make_orclause(tidquals));
				show_scan_qual(tidquals, "TID Cond", planstate, ancestors, es);
				show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
				if (plan->qual)
					show_instrumentation_count("Rows Removed by Filter", 1,
											   planstate, es);
			}
			break;
		case T_TidRangeScan:
			{
				/*
				 * The tidrangequals list has AND semantics, so be sure to
				 * show it as an AND condition.
				 */
				List	   *tidquals = ((TidRangeScan *) plan)->tidrangequals;

				if (list_length(tidquals) > 1)
					tidquals = list_make1(make_andclause(tidquals));
				show_scan_qual(tidquals, "TID Cond", planstate, ancestors, es);
				show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
				if (plan->qual)
					show_instrumentation_count("Rows Removed by Filter", 1,
											   planstate, es);
			}
			break;
		case T_ForeignScan:
			show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			show_scan_qual(((ForeignScan *) plan)->fdw_recheck_quals,
						   "Remote Filter", planstate, ancestors, es);
			show_foreignscan_info((ForeignScanState *) planstate, es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, false/*indexScan*/, es);
			break;
		case T_CustomScan:
			{
				CustomScanState *css = (CustomScanState *) planstate;

				show_scan_qual(plan->qual, "Filter", planstate, ancestors, es);
				if (plan->qual)
					show_instrumentation_count("Rows Removed by Filter", 1,
											   planstate, es);
				if (css->methods->ExplainCustomScan)
					css->methods->ExplainCustomScan(css, ancestors, es);
			}
			break;
		case T_NestLoop:
		case T_YbBatchedNestLoop:
			show_upper_qual(((NestLoop *) plan)->join.joinqual,
							"Join Filter", planstate, ancestors, es);
			if (((NestLoop *) plan)->join.joinqual)
				show_instrumentation_count("Rows Removed by Join Filter", 1,
										   planstate, es);
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 2,
										   planstate, es);
			break;
		case T_MergeJoin:
			show_upper_qual(((MergeJoin *) plan)->mergeclauses,
							"Merge Cond", planstate, ancestors, es);
			show_upper_qual(((MergeJoin *) plan)->join.joinqual,
							"Join Filter", planstate, ancestors, es);
			if (((MergeJoin *) plan)->join.joinqual)
				show_instrumentation_count("Rows Removed by Join Filter", 1,
										   planstate, es);
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 2,
										   planstate, es);
			break;
		case T_HashJoin:
			show_upper_qual(((HashJoin *) plan)->hashclauses,
							"Hash Cond", planstate, ancestors, es);
			show_upper_qual(((HashJoin *) plan)->join.joinqual,
							"Join Filter", planstate, ancestors, es);
			if (((HashJoin *) plan)->join.joinqual)
				show_instrumentation_count("Rows Removed by Join Filter", 1,
										   planstate, es);
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 2,
										   planstate, es);
			break;
		case T_Agg:
			show_agg_keys(castNode(AggState, planstate), ancestors, es);
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			show_hashagg_info((AggState *) planstate, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			break;
		case T_WindowAgg:
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			show_upper_qual(((WindowAgg *) plan)->runConditionOrig,
							"Run Condition", planstate, ancestors, es);
			break;
		case T_Group:
			show_group_keys(castNode(GroupState, planstate), ancestors, es);
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			break;
		case T_Sort:
			show_sort_keys(castNode(SortState, planstate), ancestors, es);
			show_sort_info(castNode(SortState, planstate), es);
			break;
		case T_IncrementalSort:
			show_incremental_sort_keys(castNode(IncrementalSortState, planstate),
									   ancestors, es);
			show_incremental_sort_info(castNode(IncrementalSortState, planstate),
									   es);
			break;
		case T_MergeAppend:
			show_merge_append_keys(castNode(MergeAppendState, planstate),
								   ancestors, es);
			break;
		case T_Result:
			show_upper_qual((List *) ((Result *) plan)->resconstantqual,
							"One-Time Filter", planstate, ancestors, es);
			show_upper_qual(plan->qual, "Filter", planstate, ancestors, es);
			if (plan->qual)
				show_instrumentation_count("Rows Removed by Filter", 1,
										   planstate, es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, false /*indexScan*/, es);
			break;
		case T_ModifyTable:
			show_modifytable_info(castNode(ModifyTableState, planstate), ancestors,
								  es);
			if (is_yb_rpc_stats_required)
				show_yb_rpc_stats(planstate, false /*indexScan*/, es);
			break;
		case T_Hash:
			show_hash_info(castNode(HashState, planstate), es);
			break;
		case T_Memoize:
			show_memoize_info(castNode(MemoizeState, planstate), ancestors,
							  es);
			break;
		default:
			break;
	}

	/* YB aggregate pushdown */
	if (IsYugaByteEnabled())
	{
		List **aggrefs = YbPlanStateTryGetAggrefs(planstate);
		if (aggrefs && *aggrefs != NIL)
			ExplainPropertyBool("Partial Aggregate", true, es);
	}

	/*
	 * Prepare per-worker JIT instrumentation.  As with the overall JIT
	 * summary, this is printed only if printing costs is enabled.
	 */
	if (es->workers_state && es->costs && es->verbose)
	{
		SharedJitInstrumentation *w = planstate->worker_jit_instrument;

		if (w)
		{
			for (int n = 0; n < w->num_workers; n++)
			{
				ExplainOpenWorker(n, es);
				ExplainPrintJIT(es, planstate->state->es_jit_flags,
								&w->jit_instr[n]);
				ExplainCloseWorker(n, es);
			}
		}
	}

	/* Show buffer/WAL usage */
	if (es->buffers && planstate->instrument)
		show_buffer_usage(es, &planstate->instrument->bufusage, false);
	if (es->wal && planstate->instrument)
		show_wal_usage(es, &planstate->instrument->walusage);

	/* Prepare per-worker buffer/WAL usage */
	if (es->workers_state && (es->buffers || es->wal) && es->verbose)
	{
		WorkerInstrumentation *w = planstate->worker_instrument;

		for (int n = 0; n < w->num_workers; n++)
		{
			Instrumentation *instrument = &w->instrument[n];
			double		nloops = instrument->nloops;

			if (nloops <= 0)
				continue;

			ExplainOpenWorker(n, es);
			if (es->buffers)
				show_buffer_usage(es, &instrument->bufusage, false);
			if (es->wal)
				show_wal_usage(es, &instrument->walusage);
			ExplainCloseWorker(n, es);
		}
	}

	/* Show per-worker details for this plan node, then pop that stack */
	if (es->workers_state)
		ExplainFlushWorkersState(es);
	es->workers_state = save_workers_state;

	/*
	 * If partition pruning was done during executor initialization, the
	 * number of child plans we'll display below will be less than the number
	 * of subplans that was specified in the plan.  To make this a bit less
	 * mysterious, emit an indication that this happened.  Note that this
	 * field is emitted now because we want it to be a property of the parent
	 * node; it *cannot* be emitted within the Plans sub-node we'll open next.
	 */
	switch (nodeTag(plan))
	{
		case T_Append:
			ExplainMissingMembers(((AppendState *) planstate)->as_nplans,
								  list_length(((Append *) plan)->appendplans),
								  es);
			break;
		case T_MergeAppend:
			ExplainMissingMembers(((MergeAppendState *) planstate)->ms_nplans,
								  list_length(((MergeAppend *) plan)->mergeplans),
								  es);
			break;
		default:
			break;
	}

	/* Get ready to display the child plans */
	haschildren = planstate->initPlan ||
		outerPlanState(planstate) ||
		innerPlanState(planstate) ||
		IsA(plan, Append) ||
		IsA(plan, MergeAppend) ||
		IsA(plan, BitmapAnd) ||
		IsA(plan, BitmapOr) ||
		IsA(plan, SubqueryScan) ||
		(IsA(planstate, CustomScanState) &&
		 ((CustomScanState *) planstate)->custom_ps != NIL) ||
		planstate->subPlan;
	if (haschildren)
	{
		ExplainOpenGroup("Plans", "Plans", false, es);
		/* Pass current Plan as head of ancestors list for children */
		ancestors = lcons(plan, ancestors);
	}

	/* initPlan-s */
	if (planstate->initPlan)
		ExplainSubPlans(planstate->initPlan, ancestors, "InitPlan", es);

	/* lefttree */
	if (outerPlanState(planstate))
		ExplainNode(outerPlanState(planstate), ancestors,
					"Outer", NULL, es);

	/* righttree */
	if (innerPlanState(planstate))
		ExplainNode(innerPlanState(planstate), ancestors,
					"Inner", NULL, es);

	/* special child plans */
	switch (nodeTag(plan))
	{
		case T_Append:
			ExplainMemberNodes(((AppendState *) planstate)->appendplans,
							   ((AppendState *) planstate)->as_nplans,
							   ancestors, es);
			break;
		case T_MergeAppend:
			ExplainMemberNodes(((MergeAppendState *) planstate)->mergeplans,
							   ((MergeAppendState *) planstate)->ms_nplans,
							   ancestors, es);
			break;
		case T_BitmapAnd:
			ExplainMemberNodes(((BitmapAndState *) planstate)->bitmapplans,
							   ((BitmapAndState *) planstate)->nplans,
							   ancestors, es);
			break;
		case T_BitmapOr:
			ExplainMemberNodes(((BitmapOrState *) planstate)->bitmapplans,
							   ((BitmapOrState *) planstate)->nplans,
							   ancestors, es);
			break;
		case T_SubqueryScan:
			ExplainNode(((SubqueryScanState *) planstate)->subplan, ancestors,
						"Subquery", NULL, es);
			break;
		case T_CustomScan:
			ExplainCustomChildren((CustomScanState *) planstate,
								  ancestors, es);
			break;
		default:
			break;
	}

	/* subPlan-s */
	if (planstate->subPlan)
		ExplainSubPlans(planstate->subPlan, ancestors, "SubPlan", es);

	/* end of child plans */
	if (haschildren)
	{
		ancestors = list_delete_first(ancestors);
		ExplainCloseGroup("Plans", "Plans", false, es);
	}

	/* in text format, undo whatever indentation we added */
	if (es->format == EXPLAIN_FORMAT_TEXT)
		es->indent = save_indent;

	ExplainCloseGroup("Plan",
					  relationship ? NULL : "Plan",
					  true, es);
}

/*
 * Show the targetlist of a plan node
 */
static void
show_plan_tlist(PlanState *planstate, List *ancestors, ExplainState *es)
{
	Plan	   *plan = planstate->plan;
	List	   *context;
	List	   *result = NIL;
	bool		useprefix;
	ListCell   *lc;

	/* No work if empty tlist (this occurs eg in bitmap indexscans) */
	if (plan->targetlist == NIL)
		return;
	/* The tlist of an Append isn't real helpful, so suppress it */
	if (IsA(plan, Append))
		return;
	/* Likewise for MergeAppend and RecursiveUnion */
	if (IsA(plan, MergeAppend))
		return;
	if (IsA(plan, RecursiveUnion))
		return;

	/*
	 * Likewise for ForeignScan that executes a direct INSERT/UPDATE/DELETE
	 *
	 * Note: the tlist for a ForeignScan that executes a direct INSERT/UPDATE
	 * might contain subplan output expressions that are confusing in this
	 * context.  The tlist for a ForeignScan that executes a direct UPDATE/
	 * DELETE always contains "junk" target columns to identify the exact row
	 * to update or delete, which would be confusing in this context.  So, we
	 * suppress it in all the cases.
	 */
	if (IsA(plan, ForeignScan) &&
		((ForeignScan *) plan)->operation != CMD_SELECT)
		return;

	/* Set up deparsing context */
	context = set_deparse_context_plan(es->deparse_cxt,
									   plan,
									   ancestors);
	useprefix = list_length(es->rtable) > 1;

	/* Deparse each result column (we now include resjunk ones) */
	foreach(lc, plan->targetlist)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);

		result = lappend(result,
						 deparse_expression((Node *) tle->expr, context,
											useprefix, false));
	}

	/* Print results */
	ExplainPropertyList("Output", result, es);
}

/*
 * Show a generic expression
 */
static void
show_expression(Node *node, const char *qlabel,
				PlanState *planstate, List *ancestors,
				bool useprefix, ExplainState *es)
{
	List	   *context;
	char	   *exprstr;

	/* Set up deparsing context */
	context = set_deparse_context_plan(es->deparse_cxt,
									   planstate->plan,
									   ancestors);

	/* Deparse the expression */
	if (YBCPgIsYugaByteEnabled())
		exprstr =
			yb_deparse_expression(node, context, useprefix, false,
								  es->verbose);
	else
		exprstr = deparse_expression(node, context, useprefix, false);

	/* And add to es->str */
	ExplainPropertyText(qlabel, exprstr, es);
}

/*
 * Show a qualifier expression (which is a List with implicit AND semantics)
 */
static void
show_qual(List *qual, const char *qlabel,
		  PlanState *planstate, List *ancestors,
		  bool useprefix, ExplainState *es)
{
	Node	   *node;

	/* No work if empty qual */
	if (qual == NIL)
		return;

	/* Convert AND list to explicit AND */
	node = (Node *) make_ands_explicit(qual);

	/* And show it */
	show_expression(node, qlabel, planstate, ancestors, useprefix, es);
}

/*
 * Show a qualifier expression for a scan plan node
 */
static void
show_scan_qual(List *qual, const char *qlabel,
			   PlanState *planstate, List *ancestors,
			   ExplainState *es)
{
	bool		useprefix;

	useprefix = (IsA(planstate->plan, SubqueryScan) || es->verbose);
	show_qual(qual, qlabel, planstate, ancestors, useprefix, es);
}

/*
 * Show a qualifier expression for an upper-level plan node
 */
static void
show_upper_qual(List *qual, const char *qlabel,
				PlanState *planstate, List *ancestors,
				ExplainState *es)
{
	bool		useprefix;

	useprefix = (list_length(es->rtable) > 1 || es->verbose);
	show_qual(qual, qlabel, planstate, ancestors, useprefix, es);
}

/*
 * Show the sort keys for a Sort node.
 */
static void
show_sort_keys(SortState *sortstate, List *ancestors, ExplainState *es)
{
	Sort	   *plan = (Sort *) sortstate->ss.ps.plan;

	show_sort_group_keys((PlanState *) sortstate, "Sort Key",
						 plan->numCols, 0, plan->sortColIdx,
						 plan->sortOperators, plan->collations,
						 plan->nullsFirst,
						 ancestors, es);
}

/*
 * Show the sort keys for a IncrementalSort node.
 */
static void
show_incremental_sort_keys(IncrementalSortState *incrsortstate,
						   List *ancestors, ExplainState *es)
{
	IncrementalSort *plan = (IncrementalSort *) incrsortstate->ss.ps.plan;

	show_sort_group_keys((PlanState *) incrsortstate, "Sort Key",
						 plan->sort.numCols, plan->nPresortedCols,
						 plan->sort.sortColIdx,
						 plan->sort.sortOperators, plan->sort.collations,
						 plan->sort.nullsFirst,
						 ancestors, es);
}

/*
 * Likewise, for a MergeAppend node.
 */
static void
show_merge_append_keys(MergeAppendState *mstate, List *ancestors,
					   ExplainState *es)
{
	MergeAppend *plan = (MergeAppend *) mstate->ps.plan;

	show_sort_group_keys((PlanState *) mstate, "Sort Key",
						 plan->numCols, 0, plan->sortColIdx,
						 plan->sortOperators, plan->collations,
						 plan->nullsFirst,
						 ancestors, es);
}

/*
 * Show the grouping keys for an Agg node.
 */
static void
show_agg_keys(AggState *astate, List *ancestors,
			  ExplainState *es)
{
	Agg		   *plan = (Agg *) astate->ss.ps.plan;

	if (plan->numCols > 0 || plan->groupingSets)
	{
		/* The key columns refer to the tlist of the child plan */
		ancestors = lcons(plan, ancestors);

		if (plan->groupingSets)
			show_grouping_sets(outerPlanState(astate), plan, ancestors, es);
		else
			show_sort_group_keys(outerPlanState(astate), "Group Key",
								 plan->numCols, 0, plan->grpColIdx,
								 NULL, NULL, NULL,
								 ancestors, es);

		ancestors = list_delete_first(ancestors);
	}
}

static void
show_grouping_sets(PlanState *planstate, Agg *agg,
				   List *ancestors, ExplainState *es)
{
	List	   *context;
	bool		useprefix;
	ListCell   *lc;

	/* Set up deparsing context */
	context = set_deparse_context_plan(es->deparse_cxt,
									   planstate->plan,
									   ancestors);
	useprefix = (list_length(es->rtable) > 1 || es->verbose);

	ExplainOpenGroup("Grouping Sets", "Grouping Sets", false, es);

	show_grouping_set_keys(planstate, agg, NULL,
						   context, useprefix, ancestors, es);

	foreach(lc, agg->chain)
	{
		Agg		   *aggnode = lfirst(lc);
		Sort	   *sortnode = (Sort *) aggnode->plan.lefttree;

		show_grouping_set_keys(planstate, aggnode, sortnode,
							   context, useprefix, ancestors, es);
	}

	ExplainCloseGroup("Grouping Sets", "Grouping Sets", false, es);
}

static void
show_grouping_set_keys(PlanState *planstate,
					   Agg *aggnode, Sort *sortnode,
					   List *context, bool useprefix,
					   List *ancestors, ExplainState *es)
{
	Plan	   *plan = planstate->plan;
	char	   *exprstr;
	ListCell   *lc;
	List	   *gsets = aggnode->groupingSets;
	AttrNumber *keycols = aggnode->grpColIdx;
	const char *keyname;
	const char *keysetname;

	if (aggnode->aggstrategy == AGG_HASHED || aggnode->aggstrategy == AGG_MIXED)
	{
		keyname = "Hash Key";
		keysetname = "Hash Keys";
	}
	else
	{
		keyname = "Group Key";
		keysetname = "Group Keys";
	}

	ExplainOpenGroup("Grouping Set", NULL, true, es);

	if (sortnode)
	{
		show_sort_group_keys(planstate, "Sort Key",
							 sortnode->numCols, 0, sortnode->sortColIdx,
							 sortnode->sortOperators, sortnode->collations,
							 sortnode->nullsFirst,
							 ancestors, es);
		if (es->format == EXPLAIN_FORMAT_TEXT)
			es->indent++;
	}

	ExplainOpenGroup(keysetname, keysetname, false, es);

	foreach(lc, gsets)
	{
		List	   *result = NIL;
		ListCell   *lc2;

		foreach(lc2, (List *) lfirst(lc))
		{
			Index		i = lfirst_int(lc2);
			AttrNumber	keyresno = keycols[i];
			TargetEntry *target = get_tle_by_resno(plan->targetlist,
												   keyresno);

			if (!target)
				elog(ERROR, "no tlist entry for key %d", keyresno);
			/* Deparse the expression, showing any top-level cast */
			exprstr = deparse_expression((Node *) target->expr, context,
										 useprefix, true);

			result = lappend(result, exprstr);
		}

		if (!result && es->format == EXPLAIN_FORMAT_TEXT)
			ExplainPropertyText(keyname, "()", es);
		else
			ExplainPropertyListNested(keyname, result, es);
	}

	ExplainCloseGroup(keysetname, keysetname, false, es);

	if (sortnode && es->format == EXPLAIN_FORMAT_TEXT)
		es->indent--;

	ExplainCloseGroup("Grouping Set", NULL, true, es);
}

/*
 * Show the grouping keys for a Group node.
 */
static void
show_group_keys(GroupState *gstate, List *ancestors,
				ExplainState *es)
{
	Group	   *plan = (Group *) gstate->ss.ps.plan;

	/* The key columns refer to the tlist of the child plan */
	ancestors = lcons(plan, ancestors);
	show_sort_group_keys(outerPlanState(gstate), "Group Key",
						 plan->numCols, 0, plan->grpColIdx,
						 NULL, NULL, NULL,
						 ancestors, es);
	ancestors = list_delete_first(ancestors);
}

/*
 * Common code to show sort/group keys, which are represented in plan nodes
 * as arrays of targetlist indexes.  If it's a sort key rather than a group
 * key, also pass sort operators/collations/nullsFirst arrays.
 */
static void
show_sort_group_keys(PlanState *planstate, const char *qlabel,
					 int nkeys, int nPresortedKeys, AttrNumber *keycols,
					 Oid *sortOperators, Oid *collations, bool *nullsFirst,
					 List *ancestors, ExplainState *es)
{
	Plan	   *plan = planstate->plan;
	List	   *context;
	List	   *result = NIL;
	List	   *resultPresorted = NIL;
	StringInfoData sortkeybuf;
	bool		useprefix;
	int			keyno;

	if (nkeys <= 0)
		return;

	initStringInfo(&sortkeybuf);

	/* Set up deparsing context */
	context = set_deparse_context_plan(es->deparse_cxt,
									   plan,
									   ancestors);
	useprefix = (list_length(es->rtable) > 1 || es->verbose);

	for (keyno = 0; keyno < nkeys; keyno++)
	{
		/* find key expression in tlist */
		AttrNumber	keyresno = keycols[keyno];
		TargetEntry *target = get_tle_by_resno(plan->targetlist,
											   keyresno);
		char	   *exprstr;

		if (!target)
			elog(ERROR, "no tlist entry for key %d", keyresno);
		/* Deparse the expression, showing any top-level cast */
		exprstr = deparse_expression((Node *) target->expr, context,
									 useprefix, true);
		resetStringInfo(&sortkeybuf);
		appendStringInfoString(&sortkeybuf, exprstr);
		/* Append sort order information, if relevant */
		if (sortOperators != NULL)
			show_sortorder_options(&sortkeybuf,
								   (Node *) target->expr,
								   sortOperators[keyno],
								   collations[keyno],
								   nullsFirst[keyno]);
		/* Emit one property-list item per sort key */
		result = lappend(result, pstrdup(sortkeybuf.data));
		if (keyno < nPresortedKeys)
			resultPresorted = lappend(resultPresorted, exprstr);
	}

	ExplainPropertyList(qlabel, result, es);
	if (nPresortedKeys > 0)
		ExplainPropertyList("Presorted Key", resultPresorted, es);
}

/*
 * Append nondefault characteristics of the sort ordering of a column to buf
 * (collation, direction, NULLS FIRST/LAST)
 */
static void
show_sortorder_options(StringInfo buf, Node *sortexpr,
					   Oid sortOperator, Oid collation, bool nullsFirst)
{
	Oid			sortcoltype = exprType(sortexpr);
	bool		reverse = false;
	TypeCacheEntry *typentry;

	typentry = lookup_type_cache(sortcoltype,
								 TYPECACHE_LT_OPR | TYPECACHE_GT_OPR);

	/*
	 * Print COLLATE if it's not default for the column's type.  There are
	 * some cases where this is redundant, eg if expression is a column whose
	 * declared collation is that collation, but it's hard to distinguish that
	 * here (and arguably, printing COLLATE explicitly is a good idea anyway
	 * in such cases).
	 */
	if (OidIsValid(collation) && collation != get_typcollation(sortcoltype))
	{
		char	   *collname = get_collation_name(collation);

		if (collname == NULL)
			elog(ERROR, "cache lookup failed for collation %u", collation);
		appendStringInfo(buf, " COLLATE %s", quote_identifier(collname));
	}

	/* Print direction if not ASC, or USING if non-default sort operator */
	if (sortOperator == typentry->gt_opr)
	{
		appendStringInfoString(buf, " DESC");
		reverse = true;
	}
	else if (sortOperator != typentry->lt_opr)
	{
		char	   *opname = get_opname(sortOperator);

		if (opname == NULL)
			elog(ERROR, "cache lookup failed for operator %u", sortOperator);
		appendStringInfo(buf, " USING %s", opname);
		/* Determine whether operator would be considered ASC or DESC */
		(void) get_equality_op_for_ordering_op(sortOperator, &reverse);
	}

	/* Add NULLS FIRST/LAST only if it wouldn't be default */
	if (nullsFirst && !reverse)
	{
		appendStringInfoString(buf, " NULLS FIRST");
	}
	else if (!nullsFirst && reverse)
	{
		appendStringInfoString(buf, " NULLS LAST");
	}
}

/*
 * Show TABLESAMPLE properties
 */
static void
show_tablesample(TableSampleClause *tsc, PlanState *planstate,
				 List *ancestors, ExplainState *es)
{
	List	   *context;
	bool		useprefix;
	char	   *method_name;
	List	   *params = NIL;
	char	   *repeatable;
	ListCell   *lc;

	/* Set up deparsing context */
	context = set_deparse_context_plan(es->deparse_cxt,
									   planstate->plan,
									   ancestors);
	useprefix = list_length(es->rtable) > 1;

	/* Get the tablesample method name */
	method_name = get_func_name(tsc->tsmhandler);

	/* Deparse parameter expressions */
	foreach(lc, tsc->args)
	{
		Node	   *arg = (Node *) lfirst(lc);

		params = lappend(params,
						 deparse_expression(arg, context,
											useprefix, false));
	}
	if (tsc->repeatable)
		repeatable = deparse_expression((Node *) tsc->repeatable, context,
										useprefix, false);
	else
		repeatable = NULL;

	/* Print results */
	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		bool		first = true;

		ExplainIndentText(es);
		appendStringInfo(es->str, "Sampling: %s (", method_name);
		foreach(lc, params)
		{
			if (!first)
				appendStringInfoString(es->str, ", ");
			appendStringInfoString(es->str, (const char *) lfirst(lc));
			first = false;
		}
		appendStringInfoChar(es->str, ')');
		if (repeatable)
			appendStringInfo(es->str, " REPEATABLE (%s)", repeatable);
		appendStringInfoChar(es->str, '\n');
	}
	else
	{
		ExplainPropertyText("Sampling Method", method_name, es);
		ExplainPropertyList("Sampling Parameters", params, es);
		if (repeatable)
			ExplainPropertyText("Repeatable Seed", repeatable, es);
	}
}

/*
 * If it's EXPLAIN ANALYZE, show tuplesort stats for a sort node
 */
static void
show_sort_info(SortState *sortstate, ExplainState *es)
{
	if (!es->analyze)
		return;

	if (sortstate->sort_Done && sortstate->tuplesortstate != NULL)
	{
		Tuplesortstate *state = (Tuplesortstate *) sortstate->tuplesortstate;
		TuplesortInstrumentation stats;
		const char *sortMethod;
		const char *spaceType;
		int64		spaceUsed;
		bool		show_variable_fields = !yb_explain_hide_non_deterministic_fields;

		tuplesort_get_stats(state, &stats);
		sortMethod = tuplesort_method_name(stats.sortMethod);
		if (show_variable_fields)
		{
			spaceType = tuplesort_space_type_name(stats.spaceType);
			spaceUsed = stats.spaceUsed;
		}

		if (es->format == EXPLAIN_FORMAT_TEXT)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str, "Sort Method: %s", sortMethod);

			if (show_variable_fields)
				appendStringInfo(es->str, "  %s: " INT64_FORMAT "kB\n",
								 spaceType, spaceUsed);
			else
				appendStringInfo(es->str, "\n");
		}
		else
		{
			ExplainPropertyText("Sort Method", sortMethod, es);
			if (show_variable_fields)
			{
				ExplainPropertyInteger("Sort Space Used", "kB", spaceUsed, es);
				ExplainPropertyText("Sort Space Type", spaceType, es);
			}
		}
	}

	/*
	 * You might think we should just skip this stanza entirely when
	 * es->hide_workers is true, but then we'd get no sort-method output at
	 * all.  We have to make it look like worker 0's data is top-level data.
	 * This is easily done by just skipping the OpenWorker/CloseWorker calls.
	 * Currently, we don't worry about the possibility that there are multiple
	 * workers in such a case; if there are, duplicate output fields will be
	 * emitted.
	 */
	if (sortstate->shared_info != NULL)
	{
		int			n;

		for (n = 0; n < sortstate->shared_info->num_workers; n++)
		{
			TuplesortInstrumentation *sinstrument;
			const char *sortMethod;
			const char *spaceType;
			int64		spaceUsed;

			sinstrument = &sortstate->shared_info->sinstrument[n];
			if (sinstrument->sortMethod == SORT_TYPE_STILL_IN_PROGRESS)
				continue;		/* ignore any unfilled slots */
			sortMethod = tuplesort_method_name(sinstrument->sortMethod);
			spaceType = tuplesort_space_type_name(sinstrument->spaceType);
			spaceUsed = sinstrument->spaceUsed;

			if (es->workers_state)
				ExplainOpenWorker(n, es);

			if (es->format == EXPLAIN_FORMAT_TEXT)
			{
				ExplainIndentText(es);
				appendStringInfo(es->str,
								 "Sort Method: %s  %s: " INT64_FORMAT "kB\n",
								 sortMethod, spaceType, spaceUsed);
			}
			else
			{
				ExplainPropertyText("Sort Method", sortMethod, es);
				ExplainPropertyInteger("Sort Space Used", "kB", spaceUsed, es);
				ExplainPropertyText("Sort Space Type", spaceType, es);
			}

			if (es->workers_state)
				ExplainCloseWorker(n, es);
		}
	}
}

/*
 * Incremental sort nodes sort in (a potentially very large number of) batches,
 * so EXPLAIN ANALYZE needs to roll up the tuplesort stats from each batch into
 * an intelligible summary.
 *
 * This function is used for both a non-parallel node and each worker in a
 * parallel incremental sort node.
 */
static void
show_incremental_sort_group_info(IncrementalSortGroupInfo *groupInfo,
								 const char *groupLabel, bool indent, ExplainState *es)
{
	ListCell   *methodCell;
	List	   *methodNames = NIL;

	/* Generate a list of sort methods used across all groups. */
	for (int bit = 0; bit < NUM_TUPLESORTMETHODS; bit++)
	{
		TuplesortMethod sortMethod = (1 << bit);

		if (groupInfo->sortMethods & sortMethod)
		{
			const char *methodName = tuplesort_method_name(sortMethod);

			methodNames = lappend(methodNames, unconstify(char *, methodName));
		}
	}

	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		if (indent)
			appendStringInfoSpaces(es->str, es->indent * 2);
		appendStringInfo(es->str, "%s Groups: " INT64_FORMAT "  Sort Method", groupLabel,
						 groupInfo->groupCount);
		/* plural/singular based on methodNames size */
		if (list_length(methodNames) > 1)
			appendStringInfoString(es->str, "s: ");
		else
			appendStringInfoString(es->str, ": ");
		foreach(methodCell, methodNames)
		{
			appendStringInfoString(es->str, (char *) methodCell->ptr_value);
			if (foreach_current_index(methodCell) < list_length(methodNames) - 1)
				appendStringInfoString(es->str, ", ");
		}

		if (groupInfo->maxMemorySpaceUsed > 0)
		{
			int64		avgSpace = groupInfo->totalMemorySpaceUsed / groupInfo->groupCount;
			const char *spaceTypeName;

			spaceTypeName = tuplesort_space_type_name(SORT_SPACE_TYPE_MEMORY);
			appendStringInfo(es->str, "  Average %s: " INT64_FORMAT "kB  Peak %s: " INT64_FORMAT "kB",
							 spaceTypeName, avgSpace,
							 spaceTypeName, groupInfo->maxMemorySpaceUsed);
		}

		if (groupInfo->maxDiskSpaceUsed > 0)
		{
			int64		avgSpace = groupInfo->totalDiskSpaceUsed / groupInfo->groupCount;

			const char *spaceTypeName;

			spaceTypeName = tuplesort_space_type_name(SORT_SPACE_TYPE_DISK);
			appendStringInfo(es->str, "  Average %s: " INT64_FORMAT "kB  Peak %s: " INT64_FORMAT "kB",
							 spaceTypeName, avgSpace,
							 spaceTypeName, groupInfo->maxDiskSpaceUsed);
		}
	}
	else
	{
		StringInfoData groupName;

		initStringInfo(&groupName);
		appendStringInfo(&groupName, "%s Groups", groupLabel);
		ExplainOpenGroup("Incremental Sort Groups", groupName.data, true, es);
		ExplainPropertyInteger("Group Count", NULL, groupInfo->groupCount, es);

		ExplainPropertyList("Sort Methods Used", methodNames, es);

		if (groupInfo->maxMemorySpaceUsed > 0)
		{
			int64		avgSpace = groupInfo->totalMemorySpaceUsed / groupInfo->groupCount;
			const char *spaceTypeName;
			StringInfoData memoryName;

			spaceTypeName = tuplesort_space_type_name(SORT_SPACE_TYPE_MEMORY);
			initStringInfo(&memoryName);
			appendStringInfo(&memoryName, "Sort Space %s", spaceTypeName);
			ExplainOpenGroup("Sort Space", memoryName.data, true, es);

			ExplainPropertyInteger("Average Sort Space Used", "kB", avgSpace, es);
			ExplainPropertyInteger("Peak Sort Space Used", "kB",
								   groupInfo->maxMemorySpaceUsed, es);

			ExplainCloseGroup("Sort Space", memoryName.data, true, es);
		}
		if (groupInfo->maxDiskSpaceUsed > 0)
		{
			int64		avgSpace = groupInfo->totalDiskSpaceUsed / groupInfo->groupCount;
			const char *spaceTypeName;
			StringInfoData diskName;

			spaceTypeName = tuplesort_space_type_name(SORT_SPACE_TYPE_DISK);
			initStringInfo(&diskName);
			appendStringInfo(&diskName, "Sort Space %s", spaceTypeName);
			ExplainOpenGroup("Sort Space", diskName.data, true, es);

			ExplainPropertyInteger("Average Sort Space Used", "kB", avgSpace, es);
			ExplainPropertyInteger("Peak Sort Space Used", "kB",
								   groupInfo->maxDiskSpaceUsed, es);

			ExplainCloseGroup("Sort Space", diskName.data, true, es);
		}

		ExplainCloseGroup("Incremental Sort Groups", groupName.data, true, es);
	}
}

/*
 * If it's EXPLAIN ANALYZE, show tuplesort stats for an incremental sort node
 */
static void
show_incremental_sort_info(IncrementalSortState *incrsortstate,
						   ExplainState *es)
{
	IncrementalSortGroupInfo *fullsortGroupInfo;
	IncrementalSortGroupInfo *prefixsortGroupInfo;

	fullsortGroupInfo = &incrsortstate->incsort_info.fullsortGroupInfo;

	if (!es->analyze)
		return;

	/*
	 * Since we never have any prefix groups unless we've first sorted a full
	 * groups and transitioned modes (copying the tuples into a prefix group),
	 * we don't need to do anything if there were 0 full groups.
	 *
	 * We still have to continue after this block if there are no full groups,
	 * though, since it's possible that we have workers that did real work
	 * even if the leader didn't participate.
	 */
	if (fullsortGroupInfo->groupCount > 0)
	{
		show_incremental_sort_group_info(fullsortGroupInfo, "Full-sort", true, es);
		prefixsortGroupInfo = &incrsortstate->incsort_info.prefixsortGroupInfo;
		if (prefixsortGroupInfo->groupCount > 0)
		{
			if (es->format == EXPLAIN_FORMAT_TEXT)
				appendStringInfoChar(es->str, '\n');
			show_incremental_sort_group_info(prefixsortGroupInfo, "Pre-sorted", true, es);
		}
		if (es->format == EXPLAIN_FORMAT_TEXT)
			appendStringInfoChar(es->str, '\n');
	}

	if (incrsortstate->shared_info != NULL)
	{
		int			n;
		bool		indent_first_line;

		for (n = 0; n < incrsortstate->shared_info->num_workers; n++)
		{
			IncrementalSortInfo *incsort_info =
			&incrsortstate->shared_info->sinfo[n];

			/*
			 * If a worker hasn't processed any sort groups at all, then
			 * exclude it from output since it either didn't launch or didn't
			 * contribute anything meaningful.
			 */
			fullsortGroupInfo = &incsort_info->fullsortGroupInfo;

			/*
			 * Since we never have any prefix groups unless we've first sorted
			 * a full groups and transitioned modes (copying the tuples into a
			 * prefix group), we don't need to do anything if there were 0
			 * full groups.
			 */
			if (fullsortGroupInfo->groupCount == 0)
				continue;

			if (es->workers_state)
				ExplainOpenWorker(n, es);

			indent_first_line = es->workers_state == NULL || es->verbose;
			show_incremental_sort_group_info(fullsortGroupInfo, "Full-sort",
											 indent_first_line, es);
			prefixsortGroupInfo = &incsort_info->prefixsortGroupInfo;
			if (prefixsortGroupInfo->groupCount > 0)
			{
				if (es->format == EXPLAIN_FORMAT_TEXT)
					appendStringInfoChar(es->str, '\n');
				show_incremental_sort_group_info(prefixsortGroupInfo, "Pre-sorted", true, es);
			}
			if (es->format == EXPLAIN_FORMAT_TEXT)
				appendStringInfoChar(es->str, '\n');

			if (es->workers_state)
				ExplainCloseWorker(n, es);
		}
	}
}

/*
 * Show information on hash buckets/batches.
 */
static void
show_hash_info(HashState *hashstate, ExplainState *es)
{
	HashInstrumentation hinstrument = {0};

	/*
	 * Collect stats from the local process, even when it's a parallel query.
	 * In a parallel query, the leader process may or may not have run the
	 * hash join, and even if it did it may not have built a hash table due to
	 * timing (if it started late it might have seen no tuples in the outer
	 * relation and skipped building the hash table).  Therefore we have to be
	 * prepared to get instrumentation data from all participants.
	 */
	if (hashstate->hinstrument)
		memcpy(&hinstrument, hashstate->hinstrument,
			   sizeof(HashInstrumentation));

	/*
	 * Merge results from workers.  In the parallel-oblivious case, the
	 * results from all participants should be identical, except where
	 * participants didn't run the join at all so have no data.  In the
	 * parallel-aware case, we need to consider all the results.  Each worker
	 * may have seen a different subset of batches and we want to report the
	 * highest memory usage across all batches.  We take the maxima of other
	 * values too, for the same reasons as in ExecHashAccumInstrumentation.
	 */
	if (hashstate->shared_info)
	{
		SharedHashInfo *shared_info = hashstate->shared_info;
		int			i;

		for (i = 0; i < shared_info->num_workers; ++i)
		{
			HashInstrumentation *worker_hi = &shared_info->hinstrument[i];

			hinstrument.nbuckets = Max(hinstrument.nbuckets,
									   worker_hi->nbuckets);
			hinstrument.nbuckets_original = Max(hinstrument.nbuckets_original,
												worker_hi->nbuckets_original);
			hinstrument.nbatch = Max(hinstrument.nbatch,
									 worker_hi->nbatch);
			hinstrument.nbatch_original = Max(hinstrument.nbatch_original,
											  worker_hi->nbatch_original);
			hinstrument.space_peak = Max(hinstrument.space_peak,
										 worker_hi->space_peak);
		}
	}

	if (hinstrument.nbatch > 0)
	{
		long		spacePeakKb = (hinstrument.space_peak + 1023) / 1024;

		if (es->format != EXPLAIN_FORMAT_TEXT)
		{
			ExplainPropertyInteger("Hash Buckets", NULL,
								   hinstrument.nbuckets, es);
			ExplainPropertyInteger("Original Hash Buckets", NULL,
								   hinstrument.nbuckets_original, es);
			ExplainPropertyInteger("Hash Batches", NULL,
								   hinstrument.nbatch, es);
			ExplainPropertyInteger("Original Hash Batches", NULL,
								   hinstrument.nbatch_original, es);
			ExplainPropertyInteger("Peak Memory Usage", "kB",
								   spacePeakKb, es);
		}
		else if (hinstrument.nbatch_original != hinstrument.nbatch ||
				 hinstrument.nbuckets_original != hinstrument.nbuckets)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str,
							 "Buckets: %d (originally %d)  Batches: %d (originally %d)  Memory Usage: %ldkB\n",
							 hinstrument.nbuckets,
							 hinstrument.nbuckets_original,
							 hinstrument.nbatch,
							 hinstrument.nbatch_original,
							 spacePeakKb);
		}
		else
		{
			ExplainIndentText(es);
			appendStringInfo(es->str,
							 "Buckets: %d  Batches: %d  Memory Usage: %ldkB\n",
							 hinstrument.nbuckets, hinstrument.nbatch,
							 spacePeakKb);
		}
	}
}

/*
 * Show information on memoize hits/misses/evictions and memory usage.
 */
static void
show_memoize_info(MemoizeState *mstate, List *ancestors, ExplainState *es)
{
	Plan	   *plan = ((PlanState *) mstate)->plan;
	ListCell   *lc;
	List	   *context;
	StringInfoData keystr;
	char	   *separator = "";
	bool		useprefix;
	int64		memPeakKb;

	initStringInfo(&keystr);

	/*
	 * It's hard to imagine having a memoize node with fewer than 2 RTEs, but
	 * let's just keep the same useprefix logic as elsewhere in this file.
	 */
	useprefix = list_length(es->rtable) > 1 || es->verbose;

	/* Set up deparsing context */
	context = set_deparse_context_plan(es->deparse_cxt,
									   plan,
									   ancestors);

	foreach(lc, ((Memoize *) plan)->param_exprs)
	{
		Node	   *expr = (Node *) lfirst(lc);

		appendStringInfoString(&keystr, separator);

		appendStringInfoString(&keystr, deparse_expression(expr, context,
														   useprefix, false));
		separator = ", ";
	}

	if (es->format != EXPLAIN_FORMAT_TEXT)
	{
		ExplainPropertyText("Cache Key", keystr.data, es);
		ExplainPropertyText("Cache Mode", mstate->binary_mode ? "binary" : "logical", es);
	}
	else
	{
		ExplainIndentText(es);
		appendStringInfo(es->str, "Cache Key: %s\n", keystr.data);
		ExplainIndentText(es);
		appendStringInfo(es->str, "Cache Mode: %s\n", mstate->binary_mode ? "binary" : "logical");
	}

	pfree(keystr.data);

	if (!es->analyze)
		return;

	if (mstate->stats.cache_misses > 0)
	{
		/*
		 * mem_peak is only set when we freed memory, so we must use mem_used
		 * when mem_peak is 0.
		 */
		if (mstate->stats.mem_peak > 0)
			memPeakKb = (mstate->stats.mem_peak + 1023) / 1024;
		else
			memPeakKb = (mstate->mem_used + 1023) / 1024;

		if (es->format != EXPLAIN_FORMAT_TEXT)
		{
			ExplainPropertyInteger("Cache Hits", NULL, mstate->stats.cache_hits, es);
			ExplainPropertyInteger("Cache Misses", NULL, mstate->stats.cache_misses, es);
			ExplainPropertyInteger("Cache Evictions", NULL, mstate->stats.cache_evictions, es);
			ExplainPropertyInteger("Cache Overflows", NULL, mstate->stats.cache_overflows, es);
			ExplainPropertyInteger("Peak Memory Usage", "kB", memPeakKb, es);
		}
		else
		{
			ExplainIndentText(es);
			appendStringInfo(es->str,
							 "Hits: " UINT64_FORMAT "  Misses: " UINT64_FORMAT "  Evictions: " UINT64_FORMAT "  Overflows: " UINT64_FORMAT "  Memory Usage: " INT64_FORMAT "kB\n",
							 mstate->stats.cache_hits,
							 mstate->stats.cache_misses,
							 mstate->stats.cache_evictions,
							 mstate->stats.cache_overflows,
							 memPeakKb);
		}
	}

	if (mstate->shared_info == NULL)
		return;

	/* Show details from parallel workers */
	for (int n = 0; n < mstate->shared_info->num_workers; n++)
	{
		MemoizeInstrumentation *si;

		si = &mstate->shared_info->sinstrument[n];

		/*
		 * Skip workers that didn't do any work.  We needn't bother checking
		 * for cache hits as a miss will always occur before a cache hit.
		 */
		if (si->cache_misses == 0)
			continue;

		if (es->workers_state)
			ExplainOpenWorker(n, es);

		/*
		 * Since the worker's MemoizeState.mem_used field is unavailable to
		 * us, ExecEndMemoize will have set the
		 * MemoizeInstrumentation.mem_peak field for us.  No need to do the
		 * zero checks like we did for the serial case above.
		 */
		memPeakKb = (si->mem_peak + 1023) / 1024;

		if (es->format == EXPLAIN_FORMAT_TEXT)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str,
							 "Hits: " UINT64_FORMAT "  Misses: " UINT64_FORMAT "  Evictions: " UINT64_FORMAT "  Overflows: " UINT64_FORMAT "  Memory Usage: " INT64_FORMAT "kB\n",
							 si->cache_hits, si->cache_misses,
							 si->cache_evictions, si->cache_overflows,
							 memPeakKb);
		}
		else
		{
			ExplainPropertyInteger("Cache Hits", NULL,
								   si->cache_hits, es);
			ExplainPropertyInteger("Cache Misses", NULL,
								   si->cache_misses, es);
			ExplainPropertyInteger("Cache Evictions", NULL,
								   si->cache_evictions, es);
			ExplainPropertyInteger("Cache Overflows", NULL,
								   si->cache_overflows, es);
			ExplainPropertyInteger("Peak Memory Usage", "kB", memPeakKb,
								   es);
		}

		if (es->workers_state)
			ExplainCloseWorker(n, es);
	}
}

/*
 * Show information on hash aggregate memory usage and batches.
 */
static void
show_hashagg_info(AggState *aggstate, ExplainState *es)
{
	Agg		   *agg = (Agg *) aggstate->ss.ps.plan;
	int64		memPeakKb = (aggstate->hash_mem_peak + 1023) / 1024;

	if (agg->aggstrategy != AGG_HASHED &&
		agg->aggstrategy != AGG_MIXED)
		return;

	if (es->format != EXPLAIN_FORMAT_TEXT)
	{

		if (es->costs)
			ExplainPropertyInteger("Planned Partitions", NULL,
								   aggstate->hash_planned_partitions, es);

		/*
		 * During parallel query the leader may have not helped out.  We
		 * detect this by checking how much memory it used.  If we find it
		 * didn't do any work then we don't show its properties.
		 */
		if (es->analyze && aggstate->hash_mem_peak > 0)
		{
			ExplainPropertyInteger("HashAgg Batches", NULL,
								   aggstate->hash_batches_used, es);
			ExplainPropertyInteger("Peak Memory Usage", "kB", memPeakKb, es);
			ExplainPropertyInteger("Disk Usage", "kB",
								   aggstate->hash_disk_used, es);
		}
	}
	else
	{
		bool		gotone = false;

		if (es->costs && aggstate->hash_planned_partitions > 0)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str, "Planned Partitions: %d",
							 aggstate->hash_planned_partitions);
			gotone = true;
		}

		/*
		 * During parallel query the leader may have not helped out.  We
		 * detect this by checking how much memory it used.  If we find it
		 * didn't do any work then we don't show its properties.
		 */
		if (es->analyze && aggstate->hash_mem_peak > 0)
		{
			if (!gotone)
				ExplainIndentText(es);
			else
				appendStringInfoString(es->str, "  ");

			appendStringInfo(es->str, "Batches: %d  Memory Usage: " INT64_FORMAT "kB",
							 aggstate->hash_batches_used, memPeakKb);
			gotone = true;

			/* Only display disk usage if we spilled to disk */
			if (aggstate->hash_batches_used > 1)
			{
				appendStringInfo(es->str, "  Disk Usage: " UINT64_FORMAT "kB",
								 aggstate->hash_disk_used);
			}
		}

		if (gotone)
			appendStringInfoChar(es->str, '\n');
	}

	/* Display stats for each parallel worker */
	if (es->analyze && aggstate->shared_info != NULL)
	{
		for (int n = 0; n < aggstate->shared_info->num_workers; n++)
		{
			AggregateInstrumentation *sinstrument;
			uint64		hash_disk_used;
			int			hash_batches_used;

			sinstrument = &aggstate->shared_info->sinstrument[n];
			/* Skip workers that didn't do anything */
			if (sinstrument->hash_mem_peak == 0)
				continue;
			hash_disk_used = sinstrument->hash_disk_used;
			hash_batches_used = sinstrument->hash_batches_used;
			memPeakKb = (sinstrument->hash_mem_peak + 1023) / 1024;

			if (es->workers_state)
				ExplainOpenWorker(n, es);

			if (es->format == EXPLAIN_FORMAT_TEXT)
			{
				ExplainIndentText(es);

				appendStringInfo(es->str, "Batches: %d  Memory Usage: " INT64_FORMAT "kB",
								 hash_batches_used, memPeakKb);

				/* Only display disk usage if we spilled to disk */
				if (hash_batches_used > 1)
					appendStringInfo(es->str, "  Disk Usage: " UINT64_FORMAT "kB",
									 hash_disk_used);
				appendStringInfoChar(es->str, '\n');
			}
			else
			{
				ExplainPropertyInteger("HashAgg Batches", NULL,
									   hash_batches_used, es);
				ExplainPropertyInteger("Peak Memory Usage", "kB", memPeakKb,
									   es);
				ExplainPropertyInteger("Disk Usage", "kB", hash_disk_used, es);
			}

			if (es->workers_state)
				ExplainCloseWorker(n, es);
		}
	}
}

/*
 * If it's EXPLAIN ANALYZE, show exact/lossy pages for a BitmapHeapScan node
 */
static void
show_tidbitmap_info(BitmapHeapScanState *planstate, ExplainState *es)
{
	if (es->format != EXPLAIN_FORMAT_TEXT)
	{
		ExplainPropertyInteger("Exact Heap Blocks", NULL,
							   planstate->exact_pages, es);
		ExplainPropertyInteger("Lossy Heap Blocks", NULL,
							   planstate->lossy_pages, es);
	}
	else
	{
		if (planstate->exact_pages > 0 || planstate->lossy_pages > 0)
		{
			ExplainIndentText(es);
			appendStringInfoString(es->str, "Heap Blocks:");
			if (planstate->exact_pages > 0)
				appendStringInfo(es->str, " exact=%ld", planstate->exact_pages);
			if (planstate->lossy_pages > 0)
				appendStringInfo(es->str, " lossy=%ld", planstate->lossy_pages);
			appendStringInfoChar(es->str, '\n');
		}
	}
}

/*
 * If it's EXPLAIN ANALYZE, show instrumentation information for a plan node
 *
 * "which" identifies which instrumentation counter to print
 */
static void
show_instrumentation_count(const char *qlabel, int which,
						   PlanState *planstate, ExplainState *es)
{
	double		nfiltered;
	double		nloops;

	if (!es->analyze || !planstate->instrument)
		return;

	if (which == 2)
		nfiltered = planstate->instrument->nfiltered2;
	else
		nfiltered = planstate->instrument->nfiltered1;
	nloops = planstate->instrument->nloops;

	/* In text mode, suppress zero counts; they're not interesting enough */
	if (nfiltered > 0 || es->format != EXPLAIN_FORMAT_TEXT)
	{
		if (nloops > 0)
			ExplainPropertyFloat(qlabel, NULL, nfiltered / nloops, 0, es);
		else
			ExplainPropertyFloat(qlabel, NULL, 0.0, 0, es);
	}
}

/*
 * Show extra information for a ForeignScan node.
 */
static void
show_foreignscan_info(ForeignScanState *fsstate, ExplainState *es)
{
	FdwRoutine *fdwroutine = fsstate->fdwroutine;

	/* Let the FDW emit whatever fields it wants */
	if (((ForeignScan *) fsstate->ss.ps.plan)->operation != CMD_SELECT)
	{
		if (fdwroutine->ExplainDirectModify != NULL)
			fdwroutine->ExplainDirectModify(fsstate, es);
	}
	else
	{
		if (fdwroutine->ExplainForeignScan != NULL)
			fdwroutine->ExplainForeignScan(fsstate, es);
	}
}

/*
 * Show initplan params evaluated at Gather or Gather Merge node.
 */
static void
show_eval_params(Bitmapset *bms_params, ExplainState *es)
{
	int			paramid = -1;
	List	   *params = NIL;

	Assert(bms_params);

	while ((paramid = bms_next_member(bms_params, paramid)) >= 0)
	{
		char		param[32];

		snprintf(param, sizeof(param), "$%d", paramid);
		params = lappend(params, pstrdup(param));
	}

	if (params)
		ExplainPropertyList("Params Evaluated", params, es);
}

/*
 * Fetch the name of an index in an EXPLAIN
 *
 * We allow plugins to get control here so that plans involving hypothetical
 * indexes can be explained.
 *
 * Note: names returned by this function should be "raw"; the caller will
 * apply quoting if needed.  Formerly the convention was to do quoting here,
 * but we don't want that in non-text output formats.
 */
static const char *
explain_get_index_name(Oid indexId)
{
	const char *result;

	if (explain_get_index_name_hook)
		result = (*explain_get_index_name_hook) (indexId);
	else
		result = NULL;
	if (result == NULL)
	{
		/* default behavior: look it up in the catalogs */
		result = get_rel_name(indexId);
		if (result == NULL)
			elog(ERROR, "cache lookup failed for index %u", indexId);
	}
	return result;
}

/*
 * Show buffer usage details.
 */
static void
show_buffer_usage(ExplainState *es, const BufferUsage *usage, bool planning)
{
	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		bool		has_shared = (usage->shared_blks_hit > 0 ||
								  usage->shared_blks_read > 0 ||
								  usage->shared_blks_dirtied > 0 ||
								  usage->shared_blks_written > 0);
		bool		has_local = (usage->local_blks_hit > 0 ||
								 usage->local_blks_read > 0 ||
								 usage->local_blks_dirtied > 0 ||
								 usage->local_blks_written > 0);
		bool		has_temp = (usage->temp_blks_read > 0 ||
								usage->temp_blks_written > 0);
		bool		has_timing = (!INSTR_TIME_IS_ZERO(usage->blk_read_time) ||
								  !INSTR_TIME_IS_ZERO(usage->blk_write_time));
		bool		has_temp_timing = (!INSTR_TIME_IS_ZERO(usage->temp_blk_read_time) ||
									   !INSTR_TIME_IS_ZERO(usage->temp_blk_write_time));
		bool		show_planning = (planning && (has_shared ||
												  has_local || has_temp || has_timing ||
												  has_temp_timing));

		if (show_planning)
		{
			ExplainIndentText(es);
			appendStringInfoString(es->str, "Planning:\n");
			es->indent++;
		}

		/* Show only positive counter values. */
		if (has_shared || has_local || has_temp)
		{
			ExplainIndentText(es);
			appendStringInfoString(es->str, "Buffers:");

			if (has_shared)
			{
				appendStringInfoString(es->str, " shared");
				if (usage->shared_blks_hit > 0)
					appendStringInfo(es->str, " hit=%lld",
									 (long long) usage->shared_blks_hit);
				if (usage->shared_blks_read > 0)
					appendStringInfo(es->str, " read=%lld",
									 (long long) usage->shared_blks_read);
				if (usage->shared_blks_dirtied > 0)
					appendStringInfo(es->str, " dirtied=%lld",
									 (long long) usage->shared_blks_dirtied);
				if (usage->shared_blks_written > 0)
					appendStringInfo(es->str, " written=%lld",
									 (long long) usage->shared_blks_written);
				if (has_local || has_temp)
					appendStringInfoChar(es->str, ',');
			}
			if (has_local)
			{
				appendStringInfoString(es->str, " local");
				if (usage->local_blks_hit > 0)
					appendStringInfo(es->str, " hit=%lld",
									 (long long) usage->local_blks_hit);
				if (usage->local_blks_read > 0)
					appendStringInfo(es->str, " read=%lld",
									 (long long) usage->local_blks_read);
				if (usage->local_blks_dirtied > 0)
					appendStringInfo(es->str, " dirtied=%lld",
									 (long long) usage->local_blks_dirtied);
				if (usage->local_blks_written > 0)
					appendStringInfo(es->str, " written=%lld",
									 (long long) usage->local_blks_written);
				if (has_temp)
					appendStringInfoChar(es->str, ',');
			}
			if (has_temp)
			{
				appendStringInfoString(es->str, " temp");
				if (usage->temp_blks_read > 0)
					appendStringInfo(es->str, " read=%lld",
									 (long long) usage->temp_blks_read);
				if (usage->temp_blks_written > 0)
					appendStringInfo(es->str, " written=%lld",
									 (long long) usage->temp_blks_written);
			}
			appendStringInfoChar(es->str, '\n');
		}

		/* As above, show only positive counter values. */
		if (has_timing || has_temp_timing)
		{
			ExplainIndentText(es);
			appendStringInfoString(es->str, "I/O Timings:");

			if (has_timing)
			{
				appendStringInfoString(es->str, " shared/local");
				if (!INSTR_TIME_IS_ZERO(usage->blk_read_time))
					appendStringInfo(es->str, " read=%0.3f",
									 INSTR_TIME_GET_MILLISEC(usage->blk_read_time));
				if (!INSTR_TIME_IS_ZERO(usage->blk_write_time))
					appendStringInfo(es->str, " write=%0.3f",
									 INSTR_TIME_GET_MILLISEC(usage->blk_write_time));
				if (has_temp_timing)
					appendStringInfoChar(es->str, ',');
			}
			if (has_temp_timing)
			{
				appendStringInfoString(es->str, " temp");
				if (!INSTR_TIME_IS_ZERO(usage->temp_blk_read_time))
					appendStringInfo(es->str, " read=%0.3f",
									 INSTR_TIME_GET_MILLISEC(usage->temp_blk_read_time));
				if (!INSTR_TIME_IS_ZERO(usage->temp_blk_write_time))
					appendStringInfo(es->str, " write=%0.3f",
									 INSTR_TIME_GET_MILLISEC(usage->temp_blk_write_time));
			}
			appendStringInfoChar(es->str, '\n');
		}

		if (show_planning)
			es->indent--;
	}
	else
	{
		ExplainPropertyInteger("Shared Hit Blocks", NULL,
							   usage->shared_blks_hit, es);
		ExplainPropertyInteger("Shared Read Blocks", NULL,
							   usage->shared_blks_read, es);
		ExplainPropertyInteger("Shared Dirtied Blocks", NULL,
							   usage->shared_blks_dirtied, es);
		ExplainPropertyInteger("Shared Written Blocks", NULL,
							   usage->shared_blks_written, es);
		ExplainPropertyInteger("Local Hit Blocks", NULL,
							   usage->local_blks_hit, es);
		ExplainPropertyInteger("Local Read Blocks", NULL,
							   usage->local_blks_read, es);
		ExplainPropertyInteger("Local Dirtied Blocks", NULL,
							   usage->local_blks_dirtied, es);
		ExplainPropertyInteger("Local Written Blocks", NULL,
							   usage->local_blks_written, es);
		ExplainPropertyInteger("Temp Read Blocks", NULL,
							   usage->temp_blks_read, es);
		ExplainPropertyInteger("Temp Written Blocks", NULL,
							   usage->temp_blks_written, es);
		if (track_io_timing)
		{
			ExplainPropertyFloat("I/O Read Time", "ms",
								 INSTR_TIME_GET_MILLISEC(usage->blk_read_time),
								 3, es);
			ExplainPropertyFloat("I/O Write Time", "ms",
								 INSTR_TIME_GET_MILLISEC(usage->blk_write_time),
								 3, es);
			ExplainPropertyFloat("Temp I/O Read Time", "ms",
								 INSTR_TIME_GET_MILLISEC(usage->temp_blk_read_time),
								 3, es);
			ExplainPropertyFloat("Temp I/O Write Time", "ms",
								 INSTR_TIME_GET_MILLISEC(usage->temp_blk_write_time),
								 3, es);
		}
	}
}

/*
 * Show WAL usage details.
 */
static void
show_wal_usage(ExplainState *es, const WalUsage *usage)
{
	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		/* Show only positive counter values. */
		if ((usage->wal_records > 0) || (usage->wal_fpi > 0) ||
			(usage->wal_bytes > 0))
		{
			ExplainIndentText(es);
			appendStringInfoString(es->str, "WAL:");

			if (usage->wal_records > 0)
				appendStringInfo(es->str, " records=%lld",
								 (long long) usage->wal_records);
			if (usage->wal_fpi > 0)
				appendStringInfo(es->str, " fpi=%lld",
								 (long long) usage->wal_fpi);
			if (usage->wal_bytes > 0)
				appendStringInfo(es->str, " bytes=" UINT64_FORMAT,
								 usage->wal_bytes);
			appendStringInfoChar(es->str, '\n');
		}
	}
	else
	{
		ExplainPropertyInteger("WAL Records", NULL,
							   usage->wal_records, es);
		ExplainPropertyInteger("WAL FPI", NULL,
							   usage->wal_fpi, es);
		ExplainPropertyUInteger("WAL Bytes", NULL,
								usage->wal_bytes, es);
	}
}

/*
 * Show YB RPC stats.
 */
static void
show_yb_rpc_stats(PlanState *planstate, bool indexScan, ExplainState *es)
{
	YbInstrumentation *yb_instr = &planstate->instrument->yb_instr;
	double nloops = planstate->instrument->nloops;

	/* Read stats */
	double table_reads = yb_instr->tbl_reads.count / nloops;
	double table_read_wait = yb_instr->tbl_reads.wait_time / nloops;
	double index_reads = yb_instr->index_reads.count / nloops;
	double index_read_wait = yb_instr->index_reads.wait_time / nloops;

	/* Write stats */
	double table_writes = yb_instr->tbl_writes / nloops;
	double index_writes = yb_instr->index_writes / nloops;
	double flushes = yb_instr->write_flushes.count / nloops;
	double flushes_wait = yb_instr->write_flushes.wait_time / nloops;

	YbExplainState yb_es = {es, false};

	YbExplainRpcRequestStat(&yb_es, YB_STAT_LABEL_STORAGE_TABLE_READ,
							table_reads, table_read_wait);
	YbExplainRpcRequestStat(&yb_es, YB_STAT_LABEL_STORAGE_INDEX_READ,
							index_reads, index_read_wait);
	YbExplainStatWithoutTiming(&yb_es, YB_STAT_LABEL_STORAGE_TABLE_WRITE,
							   table_writes);
	YbExplainStatWithoutTiming(&yb_es, YB_STAT_LABEL_STORAGE_INDEX_WRITE,
							   index_writes);
	YbExplainRpcRequestStat(&yb_es, YB_STAT_LABEL_STORAGE_FLUSH, flushes,
							flushes_wait);

	if (es->debug) {
		for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i) {
			YbExplainRpcRequestGauge(&yb_es, i, yb_instr->storage_gauge_metrics[i] / nloops,
									 true /* is_mean */);
		}
		for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i) {
			YbExplainRpcRequestCounter(&yb_es, i, yb_instr->storage_counter_metrics[i] / nloops,
									   true /* is_mean */);
		}
		for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i) {
			YbExplainRpcRequestEvent(&yb_es, i, &yb_instr->storage_event_metrics[i],
									 nloops, true /* is_mean */);
		}
	}
}

/*
 * Add some additional details about an IndexScan or IndexOnlyScan
 */
static void
ExplainIndexScanDetails(Oid indexid, ScanDirection indexorderdir,
						double yb_estimated_num_nexts, double yb_estimated_num_seeks,
						ExplainState *es)
{
	const char *indexname = explain_get_index_name(indexid);

	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		if (ScanDirectionIsBackward(indexorderdir))
			appendStringInfoString(es->str, " Backward");
		appendStringInfo(es->str, " using %s", quote_identifier(indexname));
	}
	else
	{
		const char *scandir;

		switch (indexorderdir)
		{
			case BackwardScanDirection:
				scandir = "Backward";
				break;
			case NoMovementScanDirection:
				scandir = "NoMovement";
				break;
			case ForwardScanDirection:
				scandir = "Forward";
				break;
			default:
				scandir = "???";
				break;
		}
		ExplainPropertyText("Scan Direction", scandir, es);
		ExplainPropertyText("Index Name", indexname, es);
		if (es->debug && yb_enable_base_scans_cost_model)
		{
			ExplainPropertyFloat("Estimated Seeks", NULL, yb_estimated_num_seeks, 0, es);
			ExplainPropertyFloat("Estimated Nexts", NULL, yb_estimated_num_nexts, 0, es);
		}
	}
}

/*
 * Show the target of a Scan node
 */
static void
ExplainScanTarget(Scan *plan, ExplainState *es)
{
	ExplainTargetRel((Plan *) plan, plan->scanrelid, es);
}

/*
 * Show the target of a ModifyTable node
 *
 * Here we show the nominal target (ie, the relation that was named in the
 * original query).  If the actual target(s) is/are different, we'll show them
 * in show_modifytable_info().
 */
static void
ExplainModifyTarget(ModifyTable *plan, ExplainState *es)
{
	ExplainTargetRel((Plan *) plan, plan->nominalRelation, es);
}

/*
 * Show the target relation of a scan or modify node
 */
static void
ExplainTargetRel(Plan *plan, Index rti, ExplainState *es)
{
	char	   *objectname = NULL;
	char	   *namespace = NULL;
	const char *objecttag = NULL;
	RangeTblEntry *rte;
	char	   *refname;

	rte = rt_fetch(rti, es->rtable);
	refname = (char *) list_nth(es->rtable_names, rti - 1);
	if (refname == NULL)
		refname = rte->eref->aliasname;

	switch (nodeTag(plan))
	{
		case T_SeqScan:
		case T_YbSeqScan:
		case T_SampleScan:
		case T_IndexScan:
		case T_IndexOnlyScan:
		case T_BitmapHeapScan:
		case T_TidScan:
		case T_TidRangeScan:
		case T_ForeignScan:
		case T_CustomScan:
		case T_ModifyTable:
			/* Assert it's on a real relation */
			Assert(rte->rtekind == RTE_RELATION);
			objectname = get_rel_name(rte->relid);
			if (es->verbose)
				namespace = get_namespace_name_or_temp(get_rel_namespace(rte->relid));
			objecttag = "Relation Name";
			break;
		case T_FunctionScan:
			{
				FunctionScan *fscan = (FunctionScan *) plan;

				/* Assert it's on a RangeFunction */
				Assert(rte->rtekind == RTE_FUNCTION);

				/*
				 * If the expression is still a function call of a single
				 * function, we can get the real name of the function.
				 * Otherwise, punt.  (Even if it was a single function call
				 * originally, the optimizer could have simplified it away.)
				 */
				if (list_length(fscan->functions) == 1)
				{
					RangeTblFunction *rtfunc = (RangeTblFunction *) linitial(fscan->functions);

					if (IsA(rtfunc->funcexpr, FuncExpr))
					{
						FuncExpr   *funcexpr = (FuncExpr *) rtfunc->funcexpr;
						Oid			funcid = funcexpr->funcid;

						objectname = get_func_name(funcid);
						if (es->verbose)
							namespace = get_namespace_name_or_temp(get_func_namespace(funcid));
					}
				}
				objecttag = "Function Name";
			}
			break;
		case T_TableFuncScan:
			Assert(rte->rtekind == RTE_TABLEFUNC);
			objectname = "xmltable";
			objecttag = "Table Function Name";
			break;
		case T_ValuesScan:
			Assert(rte->rtekind == RTE_VALUES);
			break;
		case T_CteScan:
			/* Assert it's on a non-self-reference CTE */
			Assert(rte->rtekind == RTE_CTE);
			Assert(!rte->self_reference);
			objectname = rte->ctename;
			objecttag = "CTE Name";
			break;
		case T_NamedTuplestoreScan:
			Assert(rte->rtekind == RTE_NAMEDTUPLESTORE);
			objectname = rte->enrname;
			objecttag = "Tuplestore Name";
			break;
		case T_WorkTableScan:
			/* Assert it's on a self-reference CTE */
			Assert(rte->rtekind == RTE_CTE);
			Assert(rte->self_reference);
			objectname = rte->ctename;
			objecttag = "CTE Name";
			break;
		default:
			break;
	}

	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		appendStringInfoString(es->str, " on");
		if (namespace != NULL)
			appendStringInfo(es->str, " %s.%s", quote_identifier(namespace),
							 quote_identifier(objectname));
		else if (objectname != NULL)
			appendStringInfo(es->str, " %s", quote_identifier(objectname));
		if (objectname == NULL || strcmp(refname, objectname) != 0)
			appendStringInfo(es->str, " %s", quote_identifier(refname));
	}
	else
	{
		if (objecttag != NULL && objectname != NULL)
			ExplainPropertyText(objecttag, objectname, es);
		if (namespace != NULL)
			ExplainPropertyText("Schema", namespace, es);
		ExplainPropertyText("Alias", refname, es);
	}
}

/*
 * Show extra information for a ModifyTable node
 *
 * We have three objectives here.  First, if there's more than one target
 * table or it's different from the nominal target, identify the actual
 * target(s).  Second, give FDWs a chance to display extra info about foreign
 * targets.  Third, show information about ON CONFLICT.
 */
static void
show_modifytable_info(ModifyTableState *mtstate, List *ancestors,
					  ExplainState *es)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	const char *operation;
	const char *foperation;
	bool		labeltargets;
	int			j;
	List	   *idxNames = NIL;
	ListCell   *lst;

	switch (node->operation)
	{
		case CMD_INSERT:
			operation = "Insert";
			foperation = "Foreign Insert";
			break;
		case CMD_UPDATE:
			operation = "Update";
			foperation = "Foreign Update";
			break;
		case CMD_DELETE:
			operation = "Delete";
			foperation = "Foreign Delete";
			break;
		case CMD_MERGE:
			operation = "Merge";
			/* XXX unsupported for now, but avoid compiler noise */
			foperation = "Foreign Merge";
			break;
		default:
			operation = "???";
			foperation = "Foreign ???";
			break;
	}

	/* Should we explicitly label target relations? */
	labeltargets = (mtstate->mt_nrels > 1 ||
					(mtstate->mt_nrels == 1 &&
					 mtstate->resultRelInfo[0].ri_RangeTableIndex != node->nominalRelation));

	if (labeltargets)
		ExplainOpenGroup("Target Tables", "Target Tables", false, es);

	for (j = 0; j < mtstate->mt_nrels; j++)
	{
		ResultRelInfo *resultRelInfo = mtstate->resultRelInfo + j;
		FdwRoutine *fdwroutine = resultRelInfo->ri_FdwRoutine;

		if (labeltargets)
		{
			/* Open a group for this target */
			ExplainOpenGroup("Target Table", NULL, true, es);

			/*
			 * In text mode, decorate each target with operation type, so that
			 * ExplainTargetRel's output of " on foo" will read nicely.
			 */
			if (es->format == EXPLAIN_FORMAT_TEXT)
			{
				ExplainIndentText(es);
				appendStringInfoString(es->str,
									   fdwroutine ? foperation : operation);
			}

			/* Identify target */
			ExplainTargetRel((Plan *) node,
							 resultRelInfo->ri_RangeTableIndex,
							 es);

			if (es->format == EXPLAIN_FORMAT_TEXT)
			{
				appendStringInfoChar(es->str, '\n');
				es->indent++;
			}
		}

		/* Give FDW a chance if needed */
		if (!resultRelInfo->ri_usesFdwDirectModify &&
			fdwroutine != NULL &&
			fdwroutine->ExplainForeignModify != NULL)
		{
			List	   *fdw_private = (List *) list_nth(node->fdwPrivLists, j);

			fdwroutine->ExplainForeignModify(mtstate,
											 resultRelInfo,
											 fdw_private,
											 j,
											 es);
		}

		if (labeltargets)
		{
			/* Undo the indentation we added in text format */
			if (es->format == EXPLAIN_FORMAT_TEXT)
				es->indent--;

			/* Close the group */
			ExplainCloseGroup("Target Table", NULL, true, es);
		}
	}

	/* Gather names of ON CONFLICT arbiter indexes */
	foreach(lst, node->arbiterIndexes)
	{
		char	   *indexname = get_rel_name(lfirst_oid(lst));

		idxNames = lappend(idxNames, indexname);
	}

	if (node->onConflictAction != ONCONFLICT_NONE)
	{
		ExplainPropertyText("Conflict Resolution",
							node->onConflictAction == ONCONFLICT_NOTHING ?
							"NOTHING" : "UPDATE",
							es);

		/*
		 * Don't display arbiter indexes at all when DO NOTHING variant
		 * implicitly ignores all conflicts
		 */
		if (idxNames)
			ExplainPropertyList("Conflict Arbiter Indexes", idxNames, es);

		/* ON CONFLICT DO UPDATE WHERE qual is specially displayed */
		if (node->onConflictWhere)
		{
			show_upper_qual((List *) node->onConflictWhere, "Conflict Filter",
							&mtstate->ps, ancestors, es);
			show_instrumentation_count("Rows Removed by Conflict Filter", 1, &mtstate->ps, es);
		}

		/* EXPLAIN ANALYZE display of actual outcome for each tuple proposed */
		if (es->analyze && mtstate->ps.instrument)
		{
			double		total;
			double		insert_path;
			double		other_path;

			InstrEndLoop(outerPlanState(mtstate)->instrument);

			/* count the number of source rows */
			total = outerPlanState(mtstate)->instrument->ntuples;
			other_path = mtstate->ps.instrument->ntuples2;
			insert_path = total - other_path;

			ExplainPropertyFloat("Tuples Inserted", NULL,
								 insert_path, 0, es);
			ExplainPropertyFloat("Conflicting Tuples", NULL,
								 other_path, 0, es);
		}
	}
	else if (node->operation == CMD_MERGE)
	{
		/* EXPLAIN ANALYZE display of tuples processed */
		if (es->analyze && mtstate->ps.instrument)
		{
			double		total;
			double		insert_path;
			double		update_path;
			double		delete_path;
			double		skipped_path;

			InstrEndLoop(outerPlanState(mtstate)->instrument);

			/* count the number of source rows */
			total = outerPlanState(mtstate)->instrument->ntuples;
			insert_path = mtstate->mt_merge_inserted;
			update_path = mtstate->mt_merge_updated;
			delete_path = mtstate->mt_merge_deleted;
			skipped_path = total - insert_path - update_path - delete_path;
			Assert(skipped_path >= 0);

			if (es->format == EXPLAIN_FORMAT_TEXT)
			{
				if (total > 0)
				{
					ExplainIndentText(es);
					appendStringInfoString(es->str, "Tuples:");
					if (insert_path > 0)
						appendStringInfo(es->str, " inserted=%.0f", insert_path);
					if (update_path > 0)
						appendStringInfo(es->str, " updated=%.0f", update_path);
					if (delete_path > 0)
						appendStringInfo(es->str, " deleted=%.0f", delete_path);
					if (skipped_path > 0)
						appendStringInfo(es->str, " skipped=%.0f", skipped_path);
					appendStringInfoChar(es->str, '\n');
				}
			}
			else
			{
				ExplainPropertyFloat("Tuples Inserted", NULL, insert_path, 0, es);
				ExplainPropertyFloat("Tuples Updated", NULL, update_path, 0, es);
				ExplainPropertyFloat("Tuples Deleted", NULL, delete_path, 0, es);
				ExplainPropertyFloat("Tuples Skipped", NULL, skipped_path, 0, es);
			}
		}
	}

	if (labeltargets)
		ExplainCloseGroup("Target Tables", "Target Tables", false, es);
}

/*
 * Explain the constituent plans of an Append, MergeAppend,
 * BitmapAnd, or BitmapOr node.
 *
 * The ancestors list should already contain the immediate parent of these
 * plans.
 */
static void
ExplainMemberNodes(PlanState **planstates, int nplans,
				   List *ancestors, ExplainState *es)
{
	int			j;

	for (j = 0; j < nplans; j++)
		ExplainNode(planstates[j], ancestors,
					"Member", NULL, es);
}

/*
 * Report about any pruned subnodes of an Append or MergeAppend node.
 *
 * nplans indicates the number of live subplans.
 * nchildren indicates the original number of subnodes in the Plan;
 * some of these may have been pruned by the run-time pruning code.
 */
static void
ExplainMissingMembers(int nplans, int nchildren, ExplainState *es)
{
	if (nplans < nchildren || es->format != EXPLAIN_FORMAT_TEXT)
		ExplainPropertyInteger("Subplans Removed", NULL,
							   nchildren - nplans, es);
}

/*
 * Explain a list of SubPlans (or initPlans, which also use SubPlan nodes).
 *
 * The ancestors list should already contain the immediate parent of these
 * SubPlans.
 */
static void
ExplainSubPlans(List *plans, List *ancestors,
				const char *relationship, ExplainState *es)
{
	ListCell   *lst;

	foreach(lst, plans)
	{
		SubPlanState *sps = (SubPlanState *) lfirst(lst);
		SubPlan    *sp = sps->subplan;

		/*
		 * There can be multiple SubPlan nodes referencing the same physical
		 * subplan (same plan_id, which is its index in PlannedStmt.subplans).
		 * We should print a subplan only once, so track which ones we already
		 * printed.  This state must be global across the plan tree, since the
		 * duplicate nodes could be in different plan nodes, eg both a bitmap
		 * indexscan's indexqual and its parent heapscan's recheck qual.  (We
		 * do not worry too much about which plan node we show the subplan as
		 * attached to in such cases.)
		 */
		if (bms_is_member(sp->plan_id, es->printed_subplans))
			continue;
		es->printed_subplans = bms_add_member(es->printed_subplans,
											  sp->plan_id);

		/*
		 * Treat the SubPlan node as an ancestor of the plan node(s) within
		 * it, so that ruleutils.c can find the referents of subplan
		 * parameters.
		 */
		ancestors = lcons(sp, ancestors);

		ExplainNode(sps->planstate, ancestors,
					relationship, sp->plan_name, es);

		ancestors = list_delete_first(ancestors);
	}
}

/*
 * Explain a list of children of a CustomScan.
 */
static void
ExplainCustomChildren(CustomScanState *css, List *ancestors, ExplainState *es)
{
	ListCell   *cell;
	const char *label =
	(list_length(css->custom_ps) != 1 ? "children" : "child");

	foreach(cell, css->custom_ps)
		ExplainNode((PlanState *) lfirst(cell), ancestors, label, NULL, es);
}

/*
 * Create a per-plan-node workspace for collecting per-worker data.
 *
 * Output related to each worker will be temporarily "set aside" into a
 * separate buffer, which we'll merge into the main output stream once
 * we've processed all data for the plan node.  This makes it feasible to
 * generate a coherent sub-group of fields for each worker, even though the
 * code that produces the fields is in several different places in this file.
 * Formatting of such a set-aside field group is managed by
 * ExplainOpenSetAsideGroup and ExplainSaveGroup/ExplainRestoreGroup.
 */
static ExplainWorkersState *
ExplainCreateWorkersState(int num_workers)
{
	ExplainWorkersState *wstate;

	wstate = (ExplainWorkersState *) palloc(sizeof(ExplainWorkersState));
	wstate->num_workers = num_workers;
	wstate->worker_inited = (bool *) palloc0(num_workers * sizeof(bool));
	wstate->worker_str = (StringInfoData *)
		palloc0(num_workers * sizeof(StringInfoData));
	wstate->worker_state_save = (int *) palloc(num_workers * sizeof(int));
	return wstate;
}

/*
 * Begin or resume output into the set-aside group for worker N.
 */
static void
ExplainOpenWorker(int n, ExplainState *es)
{
	ExplainWorkersState *wstate = es->workers_state;

	Assert(wstate);
	Assert(n >= 0 && n < wstate->num_workers);

	/* Save prior output buffer pointer */
	wstate->prev_str = es->str;

	if (!wstate->worker_inited[n])
	{
		/* First time through, so create the buffer for this worker */
		initStringInfo(&wstate->worker_str[n]);
		es->str = &wstate->worker_str[n];

		/*
		 * Push suitable initial formatting state for this worker's field
		 * group.  We allow one extra logical nesting level, since this group
		 * will eventually be wrapped in an outer "Workers" group.
		 */
		ExplainOpenSetAsideGroup("Worker", NULL, true, 2, es);

		/*
		 * In non-TEXT formats we always emit a "Worker Number" field, even if
		 * there's no other data for this worker.
		 */
		if (es->format != EXPLAIN_FORMAT_TEXT)
			ExplainPropertyInteger("Worker Number", NULL, n, es);

		wstate->worker_inited[n] = true;
	}
	else
	{
		/* Resuming output for a worker we've already emitted some data for */
		es->str = &wstate->worker_str[n];

		/* Restore formatting state saved by last ExplainCloseWorker() */
		ExplainRestoreGroup(es, 2, &wstate->worker_state_save[n]);
	}

	/*
	 * In TEXT format, prefix the first output line for this worker with
	 * "Worker N:".  Then, any additional lines should be indented one more
	 * stop than the "Worker N" line is.
	 */
	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		if (es->str->len == 0)
		{
			ExplainIndentText(es);
			appendStringInfo(es->str, "Worker %d:  ", n);
		}

		es->indent++;
	}
}

/*
 * End output for worker N --- must pair with previous ExplainOpenWorker call
 */
static void
ExplainCloseWorker(int n, ExplainState *es)
{
	ExplainWorkersState *wstate = es->workers_state;

	Assert(wstate);
	Assert(n >= 0 && n < wstate->num_workers);
	Assert(wstate->worker_inited[n]);

	/*
	 * Save formatting state in case we do another ExplainOpenWorker(), then
	 * pop the formatting stack.
	 */
	ExplainSaveGroup(es, 2, &wstate->worker_state_save[n]);

	/*
	 * In TEXT format, if we didn't actually produce any output line(s) then
	 * truncate off the partial line emitted by ExplainOpenWorker.  (This is
	 * to avoid bogus output if, say, show_buffer_usage chooses not to print
	 * anything for the worker.)  Also fix up the indent level.
	 */
	if (es->format == EXPLAIN_FORMAT_TEXT)
	{
		while (es->str->len > 0 && es->str->data[es->str->len - 1] != '\n')
			es->str->data[--(es->str->len)] = '\0';

		es->indent--;
	}

	/* Restore prior output buffer pointer */
	es->str = wstate->prev_str;
}

/*
 * Print per-worker info for current node, then free the ExplainWorkersState.
 */
static void
ExplainFlushWorkersState(ExplainState *es)
{
	ExplainWorkersState *wstate = es->workers_state;

	ExplainOpenGroup("Workers", "Workers", false, es);
	for (int i = 0; i < wstate->num_workers; i++)
	{
		if (wstate->worker_inited[i])
		{
			/* This must match previous ExplainOpenSetAsideGroup call */
			ExplainOpenGroup("Worker", NULL, true, es);
			appendStringInfoString(es->str, wstate->worker_str[i].data);
			ExplainCloseGroup("Worker", NULL, true, es);

			pfree(wstate->worker_str[i].data);
		}
	}
	ExplainCloseGroup("Workers", "Workers", false, es);

	pfree(wstate->worker_inited);
	pfree(wstate->worker_str);
	pfree(wstate->worker_state_save);
	pfree(wstate);
}

/*
 * Explain a property, such as sort keys or targets, that takes the form of
 * a list of unlabeled items.  "data" is a list of C strings.
 */
void
ExplainPropertyList(const char *qlabel, List *data, ExplainState *es)
{
	ListCell   *lc;
	bool		first = true;

	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			ExplainIndentText(es);
			appendStringInfo(es->str, "%s: ", qlabel);
			foreach(lc, data)
			{
				if (!first)
					appendStringInfoString(es->str, ", ");
				appendStringInfoString(es->str, (const char *) lfirst(lc));
				first = false;
			}
			appendStringInfoChar(es->str, '\n');
			break;

		case EXPLAIN_FORMAT_XML:
			ExplainXMLTag(qlabel, X_OPENING, es);
			foreach(lc, data)
			{
				char	   *str;

				appendStringInfoSpaces(es->str, es->indent * 2 + 2);
				appendStringInfoString(es->str, "<Item>");
				str = escape_xml((const char *) lfirst(lc));
				appendStringInfoString(es->str, str);
				pfree(str);
				appendStringInfoString(es->str, "</Item>\n");
			}
			ExplainXMLTag(qlabel, X_CLOSING, es);
			break;

		case EXPLAIN_FORMAT_JSON:
			ExplainJSONLineEnding(es);
			appendStringInfoSpaces(es->str, es->indent * 2);
			escape_json(es->str, qlabel);
			appendStringInfoString(es->str, ": [");
			foreach(lc, data)
			{
				if (!first)
					appendStringInfoString(es->str, ", ");
				escape_json(es->str, (const char *) lfirst(lc));
				first = false;
			}
			appendStringInfoChar(es->str, ']');
			break;

		case EXPLAIN_FORMAT_YAML:
			ExplainYAMLLineStarting(es);
			appendStringInfo(es->str, "%s: ", qlabel);
			foreach(lc, data)
			{
				appendStringInfoChar(es->str, '\n');
				appendStringInfoSpaces(es->str, es->indent * 2 + 2);
				appendStringInfoString(es->str, "- ");
				escape_yaml(es->str, (const char *) lfirst(lc));
			}
			break;
	}
}

/*
 * Explain a property that takes the form of a list of unlabeled items within
 * another list.  "data" is a list of C strings.
 */
void
ExplainPropertyListNested(const char *qlabel, List *data, ExplainState *es)
{
	ListCell   *lc;
	bool		first = true;

	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
		case EXPLAIN_FORMAT_XML:
			ExplainPropertyList(qlabel, data, es);
			return;

		case EXPLAIN_FORMAT_JSON:
			ExplainJSONLineEnding(es);
			appendStringInfoSpaces(es->str, es->indent * 2);
			appendStringInfoChar(es->str, '[');
			foreach(lc, data)
			{
				if (!first)
					appendStringInfoString(es->str, ", ");
				escape_json(es->str, (const char *) lfirst(lc));
				first = false;
			}
			appendStringInfoChar(es->str, ']');
			break;

		case EXPLAIN_FORMAT_YAML:
			ExplainYAMLLineStarting(es);
			appendStringInfoString(es->str, "- [");
			foreach(lc, data)
			{
				if (!first)
					appendStringInfoString(es->str, ", ");
				escape_yaml(es->str, (const char *) lfirst(lc));
				first = false;
			}
			appendStringInfoChar(es->str, ']');
			break;
	}
}

/*
 * Explain a simple property.
 *
 * If "numeric" is true, the value is a number (or other value that
 * doesn't need quoting in JSON).
 *
 * If unit is non-NULL the text format will display it after the value.
 *
 * This usually should not be invoked directly, but via one of the datatype
 * specific routines ExplainPropertyText, ExplainPropertyInteger, etc.
 */
static void
ExplainProperty(const char *qlabel, const char *unit, const char *value,
				bool numeric, ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			ExplainIndentText(es);
			if (unit)
				appendStringInfo(es->str, "%s: %s %s\n", qlabel, value, unit);
			else
				appendStringInfo(es->str, "%s: %s\n", qlabel, value);
			break;

		case EXPLAIN_FORMAT_XML:
			{
				char	   *str;

				appendStringInfoSpaces(es->str, es->indent * 2);
				ExplainXMLTag(qlabel, X_OPENING | X_NOWHITESPACE, es);
				str = escape_xml(value);
				appendStringInfoString(es->str, str);
				pfree(str);
				ExplainXMLTag(qlabel, X_CLOSING | X_NOWHITESPACE, es);
				appendStringInfoChar(es->str, '\n');
			}
			break;

		case EXPLAIN_FORMAT_JSON:
			ExplainJSONLineEnding(es);
			appendStringInfoSpaces(es->str, es->indent * 2);
			escape_json(es->str, qlabel);
			appendStringInfoString(es->str, ": ");
			if (numeric)
				appendStringInfoString(es->str, value);
			else
				escape_json(es->str, value);
			break;

		case EXPLAIN_FORMAT_YAML:
			ExplainYAMLLineStarting(es);
			appendStringInfo(es->str, "%s: ", qlabel);
			if (numeric)
				appendStringInfoString(es->str, value);
			else
				escape_yaml(es->str, value);
			break;
	}
}

/*
 * Explain a string-valued property.
 */
void
ExplainPropertyText(const char *qlabel, const char *value, ExplainState *es)
{
	ExplainProperty(qlabel, NULL, value, false, es);
}

/*
 * Explain an integer-valued property.
 */
void
ExplainPropertyInteger(const char *qlabel, const char *unit, int64 value,
					   ExplainState *es)
{
	char		buf[32];

	snprintf(buf, sizeof(buf), INT64_FORMAT, value);
	ExplainProperty(qlabel, unit, buf, true, es);
}

/*
 * Explain an unsigned integer-valued property.
 */
void
ExplainPropertyUInteger(const char *qlabel, const char *unit, uint64 value,
						ExplainState *es)
{
	char		buf[32];

	snprintf(buf, sizeof(buf), UINT64_FORMAT, value);
	ExplainProperty(qlabel, unit, buf, true, es);
}

/*
 * Explain a float-valued property, using the specified number of
 * fractional digits.
 */
void
ExplainPropertyFloat(const char *qlabel, const char *unit, double value,
					 int ndigits, ExplainState *es)
{
	char	   *buf;

	buf = psprintf("%.*f", ndigits, value);
	ExplainProperty(qlabel, unit, buf, true, es);
	pfree(buf);
}

/*
 * Explain a bool-valued property.
 */
void
ExplainPropertyBool(const char *qlabel, bool value, ExplainState *es)
{
	ExplainProperty(qlabel, NULL, value ? "true" : "false", true, es);
}

/*
 * Open a group of related objects.
 *
 * objtype is the type of the group object, labelname is its label within
 * a containing object (if any).
 *
 * If labeled is true, the group members will be labeled properties,
 * while if it's false, they'll be unlabeled objects.
 */
void
ExplainOpenGroup(const char *objtype, const char *labelname,
				 bool labeled, ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			ExplainXMLTag(objtype, X_OPENING, es);
			es->indent++;
			break;

		case EXPLAIN_FORMAT_JSON:
			ExplainJSONLineEnding(es);
			appendStringInfoSpaces(es->str, 2 * es->indent);
			if (labelname)
			{
				escape_json(es->str, labelname);
				appendStringInfoString(es->str, ": ");
			}
			appendStringInfoChar(es->str, labeled ? '{' : '[');

			/*
			 * In JSON format, the grouping_stack is an integer list.  0 means
			 * we've emitted nothing at this grouping level, 1 means we've
			 * emitted something (and so the next item needs a comma). See
			 * ExplainJSONLineEnding().
			 */
			es->grouping_stack = lcons_int(0, es->grouping_stack);
			es->indent++;
			break;

		case EXPLAIN_FORMAT_YAML:

			/*
			 * In YAML format, the grouping stack is an integer list.  0 means
			 * we've emitted nothing at this grouping level AND this grouping
			 * level is unlabeled and must be marked with "- ".  See
			 * ExplainYAMLLineStarting().
			 */
			ExplainYAMLLineStarting(es);
			if (labelname)
			{
				appendStringInfo(es->str, "%s: ", labelname);
				es->grouping_stack = lcons_int(1, es->grouping_stack);
			}
			else
			{
				appendStringInfoString(es->str, "- ");
				es->grouping_stack = lcons_int(0, es->grouping_stack);
			}
			es->indent++;
			break;
	}
}

/*
 * Close a group of related objects.
 * Parameters must match the corresponding ExplainOpenGroup call.
 */
void
ExplainCloseGroup(const char *objtype, const char *labelname,
				  bool labeled, ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			es->indent--;
			ExplainXMLTag(objtype, X_CLOSING, es);
			break;

		case EXPLAIN_FORMAT_JSON:
			es->indent--;
			appendStringInfoChar(es->str, '\n');
			appendStringInfoSpaces(es->str, 2 * es->indent);
			appendStringInfoChar(es->str, labeled ? '}' : ']');
			es->grouping_stack = list_delete_first(es->grouping_stack);
			break;

		case EXPLAIN_FORMAT_YAML:
			es->indent--;
			es->grouping_stack = list_delete_first(es->grouping_stack);
			break;
	}
}

/*
 * Open a group of related objects, without emitting actual data.
 *
 * Prepare the formatting state as though we were beginning a group with
 * the identified properties, but don't actually emit anything.  Output
 * subsequent to this call can be redirected into a separate output buffer,
 * and then eventually appended to the main output buffer after doing a
 * regular ExplainOpenGroup call (with the same parameters).
 *
 * The extra "depth" parameter is the new group's depth compared to current.
 * It could be more than one, in case the eventual output will be enclosed
 * in additional nesting group levels.  We assume we don't need to track
 * formatting state for those levels while preparing this group's output.
 *
 * There is no ExplainCloseSetAsideGroup --- in current usage, we always
 * pop this state with ExplainSaveGroup.
 */
static void
ExplainOpenSetAsideGroup(const char *objtype, const char *labelname,
						 bool labeled, int depth, ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			es->indent += depth;
			break;

		case EXPLAIN_FORMAT_JSON:
			es->grouping_stack = lcons_int(0, es->grouping_stack);
			es->indent += depth;
			break;

		case EXPLAIN_FORMAT_YAML:
			if (labelname)
				es->grouping_stack = lcons_int(1, es->grouping_stack);
			else
				es->grouping_stack = lcons_int(0, es->grouping_stack);
			es->indent += depth;
			break;
	}
}

/*
 * Pop one level of grouping state, allowing for a re-push later.
 *
 * This is typically used after ExplainOpenSetAsideGroup; pass the
 * same "depth" used for that.
 *
 * This should not emit any output.  If state needs to be saved,
 * save it at *state_save.  Currently, an integer save area is sufficient
 * for all formats, but we might need to revisit that someday.
 */
static void
ExplainSaveGroup(ExplainState *es, int depth, int *state_save)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			es->indent -= depth;
			break;

		case EXPLAIN_FORMAT_JSON:
			es->indent -= depth;
			*state_save = linitial_int(es->grouping_stack);
			es->grouping_stack = list_delete_first(es->grouping_stack);
			break;

		case EXPLAIN_FORMAT_YAML:
			es->indent -= depth;
			*state_save = linitial_int(es->grouping_stack);
			es->grouping_stack = list_delete_first(es->grouping_stack);
			break;
	}
}

/*
 * Re-push one level of grouping state, undoing the effects of ExplainSaveGroup.
 */
static void
ExplainRestoreGroup(ExplainState *es, int depth, int *state_save)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			es->indent += depth;
			break;

		case EXPLAIN_FORMAT_JSON:
			es->grouping_stack = lcons_int(*state_save, es->grouping_stack);
			es->indent += depth;
			break;

		case EXPLAIN_FORMAT_YAML:
			es->grouping_stack = lcons_int(*state_save, es->grouping_stack);
			es->indent += depth;
			break;
	}
}

/*
 * Emit a "dummy" group that never has any members.
 *
 * objtype is the type of the group object, labelname is its label within
 * a containing object (if any).
 */
static void
ExplainDummyGroup(const char *objtype, const char *labelname, ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			ExplainXMLTag(objtype, X_CLOSE_IMMEDIATE, es);
			break;

		case EXPLAIN_FORMAT_JSON:
			ExplainJSONLineEnding(es);
			appendStringInfoSpaces(es->str, 2 * es->indent);
			if (labelname)
			{
				escape_json(es->str, labelname);
				appendStringInfoString(es->str, ": ");
			}
			escape_json(es->str, objtype);
			break;

		case EXPLAIN_FORMAT_YAML:
			ExplainYAMLLineStarting(es);
			if (labelname)
			{
				escape_yaml(es->str, labelname);
				appendStringInfoString(es->str, ": ");
			}
			else
			{
				appendStringInfoString(es->str, "- ");
			}
			escape_yaml(es->str, objtype);
			break;
	}
}

/*
 * Emit the start-of-output boilerplate.
 *
 * This is just enough different from processing a subgroup that we need
 * a separate pair of subroutines.
 */
void
ExplainBeginOutput(ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			appendStringInfoString(es->str,
								   "<explain xmlns=\"http://www.postgresql.org/2009/explain\">\n");
			es->indent++;
			break;

		case EXPLAIN_FORMAT_JSON:
			/* top-level structure is an array of plans */
			appendStringInfoChar(es->str, '[');
			es->grouping_stack = lcons_int(0, es->grouping_stack);
			es->indent++;
			break;

		case EXPLAIN_FORMAT_YAML:
			es->grouping_stack = lcons_int(0, es->grouping_stack);
			break;
	}
}

/*
 * Emit the end-of-output boilerplate.
 */
void
ExplainEndOutput(ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* nothing to do */
			break;

		case EXPLAIN_FORMAT_XML:
			es->indent--;
			appendStringInfoString(es->str, "</explain>");
			break;

		case EXPLAIN_FORMAT_JSON:
			es->indent--;
			appendStringInfoString(es->str, "\n]");
			es->grouping_stack = list_delete_first(es->grouping_stack);
			break;

		case EXPLAIN_FORMAT_YAML:
			es->grouping_stack = list_delete_first(es->grouping_stack);
			break;
	}
}

/*
 * Put an appropriate separator between multiple plans
 */
void
ExplainSeparatePlans(ExplainState *es)
{
	switch (es->format)
	{
		case EXPLAIN_FORMAT_TEXT:
			/* add a blank line */
			appendStringInfoChar(es->str, '\n');
			break;

		case EXPLAIN_FORMAT_XML:
		case EXPLAIN_FORMAT_JSON:
		case EXPLAIN_FORMAT_YAML:
			/* nothing to do */
			break;
	}
}

/*
 * Emit opening or closing XML tag.
 *
 * "flags" must contain X_OPENING, X_CLOSING, or X_CLOSE_IMMEDIATE.
 * Optionally, OR in X_NOWHITESPACE to suppress the whitespace we'd normally
 * add.
 *
 * XML restricts tag names more than our other output formats, eg they can't
 * contain white space or slashes.  Replace invalid characters with dashes,
 * so that for example "I/O Read Time" becomes "I-O-Read-Time".
 */
static void
ExplainXMLTag(const char *tagname, int flags, ExplainState *es)
{
	const char *s;
	const char *valid = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";

	if ((flags & X_NOWHITESPACE) == 0)
		appendStringInfoSpaces(es->str, 2 * es->indent);
	appendStringInfoCharMacro(es->str, '<');
	if ((flags & X_CLOSING) != 0)
		appendStringInfoCharMacro(es->str, '/');
	for (s = tagname; *s; s++)
		appendStringInfoChar(es->str, strchr(valid, *s) ? *s : '-');
	if ((flags & X_CLOSE_IMMEDIATE) != 0)
		appendStringInfoString(es->str, " /");
	appendStringInfoCharMacro(es->str, '>');
	if ((flags & X_NOWHITESPACE) == 0)
		appendStringInfoCharMacro(es->str, '\n');
}

/*
 * Indent a text-format line.
 *
 * We indent by two spaces per indentation level.  However, when emitting
 * data for a parallel worker there might already be data on the current line
 * (cf. ExplainOpenWorker); in that case, don't indent any more.
 */
static void
ExplainIndentText(ExplainState *es)
{
	Assert(es->format == EXPLAIN_FORMAT_TEXT);
	if (es->str->len == 0 || es->str->data[es->str->len - 1] == '\n')
		appendStringInfoSpaces(es->str, es->indent * 2);
}

/*
 * Emit a JSON line ending.
 *
 * JSON requires a comma after each property but the last.  To facilitate this,
 * in JSON format, the text emitted for each property begins just prior to the
 * preceding line-break (and comma, if applicable).
 */
static void
ExplainJSONLineEnding(ExplainState *es)
{
	Assert(es->format == EXPLAIN_FORMAT_JSON);
	if (linitial_int(es->grouping_stack) != 0)
		appendStringInfoChar(es->str, ',');
	else
		linitial_int(es->grouping_stack) = 1;
	appendStringInfoChar(es->str, '\n');
}

/*
 * Indent a YAML line.
 *
 * YAML lines are ordinarily indented by two spaces per indentation level.
 * The text emitted for each property begins just prior to the preceding
 * line-break, except for the first property in an unlabeled group, for which
 * it begins immediately after the "- " that introduces the group.  The first
 * property of the group appears on the same line as the opening "- ".
 */
static void
ExplainYAMLLineStarting(ExplainState *es)
{
	Assert(es->format == EXPLAIN_FORMAT_YAML);
	if (linitial_int(es->grouping_stack) == 0)
	{
		linitial_int(es->grouping_stack) = 1;
	}
	else
	{
		appendStringInfoChar(es->str, '\n');
		appendStringInfoSpaces(es->str, es->indent * 2);
	}
}

/*
 * YAML is a superset of JSON; unfortunately, the YAML quoting rules are
 * ridiculously complicated -- as documented in sections 5.3 and 7.3.3 of
 * http://yaml.org/spec/1.2/spec.html -- so we chose to just quote everything.
 * Empty strings, strings with leading or trailing whitespace, and strings
 * containing a variety of special characters must certainly be quoted or the
 * output is invalid; and other seemingly harmless strings like "0xa" or
 * "true" must be quoted, lest they be interpreted as a hexadecimal or Boolean
 * constant rather than a string.
 */
static void
escape_yaml(StringInfo buf, const char *str)
{
	escape_json(buf, str);
}

/*
 * Append YbPgMemTracker related info to EXPLAIN output,
 * currently only the max memory info.
 */
static void
YbAppendPgMemInfo(ExplainState *es, const Size peakMem)
{
	Size peakMemKb = CEILING_K(peakMem);
	ExplainPropertyInteger("Peak Memory Usage", "kB", peakMemKb, es);
}

static void
YbAggregateExplainableRPCRequestStat(ExplainState			 *es,
									 const YbInstrumentation *yb_instr)
{
	// Storage Reads
	es->yb_stats.read.count +=
		yb_instr->tbl_reads.count + yb_instr->index_reads.count;
	es->yb_stats.read.wait_time +=
		yb_instr->tbl_reads.wait_time + yb_instr->index_reads.wait_time;

	// Storage Writes
	es->yb_stats.write_count += yb_instr->tbl_writes + yb_instr->index_writes;

	// Catalog Reads
	es->yb_stats.catalog_read.count += yb_instr->catalog_reads.count;
	es->yb_stats.catalog_read.wait_time += yb_instr->catalog_reads.wait_time;

	// Catalog Writes
	es->yb_stats.catalog_write_count += yb_instr->catalog_writes;

	// Storage Flushes
	es->yb_stats.flush.count += yb_instr->write_flushes.count;
	es->yb_stats.flush.wait_time += yb_instr->write_flushes.wait_time;

	// RPC Storage Metrics
	for (int i = 0; i < YB_STORAGE_GAUGE_COUNT; ++i) {
		es->yb_stats.storage_gauge_metrics[i] += yb_instr->storage_gauge_metrics[i];
	}
	for (int i = 0; i < YB_STORAGE_COUNTER_COUNT; ++i) {
		es->yb_stats.storage_counter_metrics[i] += yb_instr->storage_counter_metrics[i];
	}
	for (int i = 0; i < YB_STORAGE_EVENT_COUNT; ++i) {
		YbPgEventMetric* agg = &es->yb_stats.storage_event_metrics[i];
		const YbPgEventMetric* val = &yb_instr->storage_event_metrics[i];
		agg->sum += val->sum;
		agg->count += val->count;
	}
}

/*
 * YB:
 * Explain Output
 * --------------
 * Distinct Index Scan
 *       ...
 * 	 Distinct Prefix: <prefix length>
 *       ...
 *
 * Adds Distinct Prefix to explain info
 */
static void
YbExplainDistinctPrefixLen(int yb_distinct_prefixlen, ExplainState *es)
{
	if (yb_distinct_prefixlen > 0)
		ExplainPropertyInteger(
			"Distinct Prefix", NULL, yb_distinct_prefixlen, es);
}
