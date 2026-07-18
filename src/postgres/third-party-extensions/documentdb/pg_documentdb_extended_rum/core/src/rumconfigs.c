/*-------------------------------------------------------------------------
 *
 * rumconfig.c
 *	  utilities routines for the configuration management for RUM indexes.
 *
 * Portions Copyright (c) Microsoft Corporation.  All rights reserved.
 * Portions Copyright (c) 2015-2022, Postgres Professional
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "utils/guc.h"
#include "access/reloptions.h"
#include "pg_documentdb_rum.h"

/* Kind of relation optioms for rum index */
static relopt_kind rum_relopt_kind;

PGDLLEXPORT void InitializeCommonDocumentDBGUCs(const char *rumGucPrefix, const
												char *documentDBRumGucPrefix);
extern PGDLLEXPORT void DocumentDBSetRumUnredactedLogEmitHook(rum_format_log_hook hook);

PGDLLEXPORT bool DocumentDBRumLoadCommonGUCs = true;

#define RUM_DEFAULT_THROW_ERROR_ON_INVALID_DATA_PAGE false
PGDLLEXPORT bool RumThrowErrorOnInvalidDataPage =
	RUM_DEFAULT_THROW_ERROR_ON_INVALID_DATA_PAGE;

#define RUM_DEFAULT_USE_NEW_ITEM_PTR_DECODING true
PGDLLEXPORT bool RumUseNewItemPtrDecoding = RUM_DEFAULT_USE_NEW_ITEM_PTR_DECODING;

#define RUM_ENABLE_PARALLEL_VACUUM_FLAGS true
PGDLLEXPORT bool RumEnableParallelVacuumFlags = RUM_ENABLE_PARALLEL_VACUUM_FLAGS;

/* rumbtree.c */
PGDLLEXPORT bool RumTrackIncompleteSplit = RUM_DEFAULT_TRACK_INCOMPLETE_SPLIT;
PGDLLEXPORT bool RumFixIncompleteSplit = RUM_DEFAULT_FIX_INCOMPLETE_SPLIT;

#define RUM_DEFAULT_ENABLE_INJECT_PAGE_SPLIT_INCOMPLETE false
PGDLLEXPORT bool RumInjectPageSplitIncomplete =
	RUM_DEFAULT_ENABLE_INJECT_PAGE_SPLIT_INCOMPLETE;

/* rumdatapage.c */
PGDLLEXPORT int RumDataPageIntermediateSplitSize = -1;

#define RUM_DEFAULT_SKIP_RESET_ON_DEAD_ENTRY_PAGE false
PGDLLEXPORT bool RumSkipResetOnDeadEntryPage = RUM_DEFAULT_SKIP_RESET_ON_DEAD_ENTRY_PAGE;

/* rumget.c */
PGDLLEXPORT int RumFuzzySearchLimit = 0;

#define RUM_DEFAULT_DISABLE_FAST_SCAN false
PGDLLEXPORT bool RumDisableFastScan = RUM_DEFAULT_DISABLE_FAST_SCAN;

#define DEFAULT_FORCE_RUM_ORDERED_INDEX_SCAN false
PGDLLEXPORT bool RumForceOrderedIndexScan = DEFAULT_FORCE_RUM_ORDERED_INDEX_SCAN;

#define RUM_DEFAULT_PREFER_ORDERED_INDEX_SCAN true
PGDLLEXPORT bool RumPreferOrderedIndexScan = RUM_DEFAULT_PREFER_ORDERED_INDEX_SCAN;

#define RUM_DEFAULT_ENABLE_SKIP_INTERMEDIATE_ENTRY true
PGDLLEXPORT bool RumEnableSkipIntermediateEntry =
	RUM_DEFAULT_ENABLE_SKIP_INTERMEDIATE_ENTRY;

/* ruminsert.c */
#define RUM_DEFAULT_ENABLE_PARALLEL_INDEX_BUILD true
PGDLLEXPORT bool RumEnableParallelIndexBuild = RUM_DEFAULT_ENABLE_PARALLEL_INDEX_BUILD;

#define RUM_DEFAULT_PARALLEL_INDEX_WORKERS_OVERRIDE -1
PGDLLEXPORT int RumParallelIndexWorkersOverride =
	RUM_DEFAULT_PARALLEL_INDEX_WORKERS_OVERRIDE;

/* rumvacuum.c */
#define RUM_DEFAULT_SKIP_RETRY_ON_DELETE_PAGE true
PGDLLEXPORT bool RumSkipRetryOnDeletePage = RUM_DEFAULT_SKIP_RETRY_ON_DELETE_PAGE;

#define RUM_DEFAULT_VACUUM_ENTRY_ITEMS true
PGDLLEXPORT bool RumVacuumEntryItems = RUM_DEFAULT_VACUUM_ENTRY_ITEMS;

#define RUM_DEFAULT_PRUNE_EMPTY_PAGES false
PGDLLEXPORT bool RumPruneEmptyPages = RUM_DEFAULT_PRUNE_EMPTY_PAGES;

#define RUM_DEFAULT_ENABLE_NEW_BULK_DELETE false
PGDLLEXPORT bool RumEnableNewBulkDelete = RUM_DEFAULT_ENABLE_NEW_BULK_DELETE;

#define RUM_DEFAULT_ENABLE_NEW_BULK_DELETE_INLINE_DATA_PAGES true
PGDLLEXPORT bool RumNewBulkDeleteInlineDataPages =
	RUM_DEFAULT_ENABLE_NEW_BULK_DELETE_INLINE_DATA_PAGES;

#define RUM_DEFAULT_SKIP_PRUNE_POSTING_TREE_PAGES false
PGDLLEXPORT bool RumVacuumSkipPrunePostingTreePages =
	RUM_DEFAULT_SKIP_PRUNE_POSTING_TREE_PAGES;

#define RUM_DEFAULT_VACUUM_CYCLE_ID_OVERRIDE -1
int32_t RumVacuumCycleIdOverride = RUM_DEFAULT_VACUUM_CYCLE_ID_OVERRIDE;

#define RUM_DEFAULT_TRAVERSE_PAGE_ONLY_ON_BACKTRACK false
PGDLLEXPORT bool RumTraversePageOnlyOnBackTrack =
	RUM_DEFAULT_TRAVERSE_PAGE_ONLY_ON_BACKTRACK;

#define RUM_DEFAULT_SKIP_GLOBAL_VISIBILITY_CHECK_ON_PRUNE false
PGDLLEXPORT bool RumSkipGlobalVisibilityCheckOnPrune =
	RUM_DEFAULT_SKIP_GLOBAL_VISIBILITY_CHECK_ON_PRUNE;

/* rumget.c */
#define RUM_DEFAULT_ENABLE_SUPPORT_DEAD_INDEX_ITEMS false
PGDLLEXPORT bool RumEnableSupportDeadIndexItems =
	RUM_DEFAULT_ENABLE_SUPPORT_DEAD_INDEX_ITEMS;

/* rumselfuncs.c */
#define RUM_DEFAULT_ENABLE_CUSTOM_COST_ESTIMATE true
PGDLLEXPORT bool RumEnableCustomCostEstimate = RUM_DEFAULT_ENABLE_CUSTOM_COST_ESTIMATE;

PGDLLEXPORT rum_format_log_hook rum_unredacted_log_emit_hook = NULL;


PGDLLEXPORT void
DocumentDBSetRumUnredactedLogEmitHook(rum_format_log_hook hook)
{
	rum_unredacted_log_emit_hook = hook;
}


PGDLLEXPORT void
InitializeCommonDocumentDBGUCs(const char *rumGucPrefix, const
							   char *documentDBRumGucPrefix)
{
	DefineCustomIntVariable(psprintf("%s.rum_fuzzy_search_limit", rumGucPrefix),
							"Sets the maximum allowed result for exact search by RUM.",
							NULL,
							&RumFuzzySearchLimit,
							0, 0, INT_MAX,
							PGC_USERSET, 0,
							NULL, NULL, NULL);

	DefineCustomIntVariable(psprintf("%s.data_page_posting_tree_size", rumGucPrefix),
							"Test GUC that sets the data page size before splits.",
							NULL,
							&RumDataPageIntermediateSplitSize,
							-1, -1, INT_MAX,
							PGC_USERSET, 0,
							NULL, NULL, NULL);

	DefineCustomBoolVariable(psprintf("%s.rum_skip_retry_on_delete_page",
									  documentDBRumGucPrefix),
							 "Sets whether or not to skip retrying on delete pages during vacuuming",
							 NULL,
							 &RumSkipRetryOnDeletePage,
							 RUM_DEFAULT_SKIP_RETRY_ON_DELETE_PAGE,
							 PGC_USERSET, 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.rum_throw_error_on_invalid_data_page", documentDBRumGucPrefix),
		"Sets whether or not to throw an error on invalid data page",
		NULL,
		&RumThrowErrorOnInvalidDataPage,
		RUM_DEFAULT_THROW_ERROR_ON_INVALID_DATA_PAGE,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.rum_disable_fast_scan", documentDBRumGucPrefix),
		"Sets whether or not to disable fast scan",
		NULL,
		&RumDisableFastScan,
		RUM_DEFAULT_DISABLE_FAST_SCAN,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_parallel_index_build", documentDBRumGucPrefix),
		"Sets whether or not to enable parallel index build",
		NULL,
		&RumEnableParallelIndexBuild,
		RUM_DEFAULT_ENABLE_PARALLEL_INDEX_BUILD,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.parallel_index_workers_override", documentDBRumGucPrefix),
		"Sets the number of parallel index workers to use (default: -1, meaning no override)",
		NULL,
		&RumParallelIndexWorkersOverride,
		RUM_DEFAULT_PARALLEL_INDEX_WORKERS_OVERRIDE, -1, INT_MAX,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.forceRumOrderedIndexScan", documentDBRumGucPrefix),
		"Sets whether or not to force a run ordered index scan",
		NULL,
		&RumForceOrderedIndexScan,
		DEFAULT_FORCE_RUM_ORDERED_INDEX_SCAN,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.preferOrderedIndexScan", documentDBRumGucPrefix),
		"Sets whether or not to prefer the ordered scan when available",
		NULL,
		&RumPreferOrderedIndexScan,
		RUM_DEFAULT_PREFER_ORDERED_INDEX_SCAN,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enableSkipIntermediateEntry", documentDBRumGucPrefix),
		"Sets whether or not to skip intermediate entries during scan",
		NULL,
		&RumEnableSkipIntermediateEntry,
		RUM_DEFAULT_ENABLE_SKIP_INTERMEDIATE_ENTRY,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.vacuum_cleanup_entries", documentDBRumGucPrefix),
		"Sets whether or not to clean up entries during vacuuming",
		NULL,
		&RumVacuumEntryItems,
		RUM_DEFAULT_VACUUM_ENTRY_ITEMS,
		PGC_USERSET, 0,
		NULL, NULL, NULL);
	DefineCustomBoolVariable(
		psprintf("%s.rum_use_new_item_ptr_decoding", documentDBRumGucPrefix),
		"Sets whether or not to use new item pointer decoding",
		NULL,
		&RumUseNewItemPtrDecoding,
		RUM_DEFAULT_USE_NEW_ITEM_PTR_DECODING,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_inject_page_split_incomplete", documentDBRumGucPrefix),
		"Test GUC - sets whether or not to enable injecting a failure in the middle of a page split",
		NULL,
		&RumInjectPageSplitIncomplete,
		RUM_DEFAULT_ENABLE_INJECT_PAGE_SPLIT_INCOMPLETE,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_set_vacuum_parallel_flags", documentDBRumGucPrefix),
		"Enables setting the parallel vacuum flags in Postgres",
		NULL,
		&RumEnableParallelVacuumFlags,
		RUM_ENABLE_PARALLEL_VACUUM_FLAGS,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_custom_cost_estimate", documentDBRumGucPrefix),
		"Temporary flag to enable using the custom rum cost estimate logic",
		NULL,
		&RumEnableCustomCostEstimate,
		RUM_DEFAULT_ENABLE_CUSTOM_COST_ESTIMATE,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.prune_rum_empty_pages", documentDBRumGucPrefix),
		"Sets whether or not to prune empty pages during vacuuming",
		NULL,
		&RumPruneEmptyPages,
		RUM_DEFAULT_PRUNE_EMPTY_PAGES,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_new_bulk_delete", documentDBRumGucPrefix),
		"Sets whether or not to the new bulk delete vacuum framework",
		NULL,
		&RumEnableNewBulkDelete,
		RUM_DEFAULT_ENABLE_NEW_BULK_DELETE,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_new_bulk_delete_inline_data_pages", documentDBRumGucPrefix),
		"Sets whether or not to delete data pages inline in the new bulkdel framework",
		NULL,
		&RumNewBulkDeleteInlineDataPages,
		RUM_DEFAULT_ENABLE_NEW_BULK_DELETE_INLINE_DATA_PAGES,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.vacuum_skip_prune_posting_tree_pages", documentDBRumGucPrefix),
		"Sets whether or not to delete data pages inline in the new bulkdel framework",
		NULL,
		&RumVacuumSkipPrunePostingTreePages,
		RUM_DEFAULT_SKIP_PRUNE_POSTING_TREE_PAGES,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.enable_support_dead_index_items", documentDBRumGucPrefix),
		"Sets whether or not to enable support for handling LP_DEAD items",
		NULL,
		&RumEnableSupportDeadIndexItems,
		RUM_DEFAULT_ENABLE_SUPPORT_DEAD_INDEX_ITEMS,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.skip_reset_dead_page_flag", documentDBRumGucPrefix),
		"Sets whether or not to enable support for handling LP_DEAD items",
		NULL,
		&RumSkipResetOnDeadEntryPage,
		RUM_DEFAULT_SKIP_RESET_ON_DEAD_ENTRY_PAGE,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomIntVariable(
		psprintf("%s.vacuum_cycle_id_override", documentDBRumGucPrefix),
		"test only override for setting the vacuum cycle id",
		NULL,
		&RumVacuumCycleIdOverride,
		RUM_DEFAULT_VACUUM_CYCLE_ID_OVERRIDE, -1, UINT16_MAX,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.default_traverse_rum_page_only_on_backtrack",
				 documentDBRumGucPrefix),
		"test only guc to only traverse vacuum pages on the backtrack path",
		NULL,
		&RumTraversePageOnlyOnBackTrack,
		RUM_DEFAULT_TRAVERSE_PAGE_ONLY_ON_BACKTRACK,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	DefineCustomBoolVariable(
		psprintf("%s.skip_global_visibility_check_on_prune",
				 documentDBRumGucPrefix),
		"test only guc to skip checking visibility on pruning pages",
		NULL,
		&RumSkipGlobalVisibilityCheckOnPrune,
		RUM_DEFAULT_TRAVERSE_PAGE_ONLY_ON_BACKTRACK,
		PGC_USERSET, 0,
		NULL, NULL, NULL);

	rum_relopt_kind = add_reloption_kind();

	add_string_reloption(rum_relopt_kind, "attach",
						 "Column name to attach as additional info",
						 NULL, NULL
#if PG_VERSION_NUM >= 130000
						 , AccessExclusiveLock
#endif
						 );
	add_string_reloption(rum_relopt_kind, "to",
						 "Column name to add a order by column",
						 NULL, NULL
#if PG_VERSION_NUM >= 130000
						 , AccessExclusiveLock
#endif
						 );
	add_bool_reloption(rum_relopt_kind, "order_by_attach",
					   "Use (addinfo, itempointer) order instead of just itempointer",
					   false
#if PG_VERSION_NUM >= 130000
					   , AccessExclusiveLock
#endif
					   );
}


PGDLLEXPORT bytea *
documentdb_rumoptions(Datum reloptions, bool validate)
{
#if PG_VERSION_NUM >= 180000
	static const int offsetIfDefault = -1;
	static const relopt_parse_elt tab[] = {
		{ "attach", RELOPT_TYPE_STRING, offsetof(RumOptions, attachColumn),
		  offsetIfDefault },
		{ "to", RELOPT_TYPE_STRING, offsetof(RumOptions, addToColumn), offsetIfDefault },
		{ "order_by_attach", RELOPT_TYPE_BOOL, offsetof(RumOptions, useAlternativeOrder),
		  offsetIfDefault }
	};
#else
	static const relopt_parse_elt tab[] = {
		{ "attach", RELOPT_TYPE_STRING, offsetof(RumOptions, attachColumn) },
		{ "to", RELOPT_TYPE_STRING, offsetof(RumOptions, addToColumn) },
		{ "order_by_attach", RELOPT_TYPE_BOOL, offsetof(RumOptions, useAlternativeOrder) }
	};
#endif

	return (bytea *) build_reloptions(reloptions, validate, rum_relopt_kind,
									  sizeof(RumOptions), tab, lengthof(tab));
}
