/*-------------------------------------------------------------------------
 *
 * duckdb_table_am.c
 *	  duckdb table access method code
 *
 *	  All the functions use snake_case naming on purpose to match the Postgres
 *	  style for easy grepping.
 *
 * Portions Copyright (c) 1996-2024, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 */

#include "duckdb/common/string.hpp"
#include "duckdb/common/unordered_map.hpp"

#include "pgduckdb/pgduckdb_ddl.hpp"

extern "C" {
#include "postgres.h"

#include "access/tableam.h"
#include "access/heapam.h"
#include "access/amapi.h"
#include "access/xact.h"
#include "catalog/index.h"
#include "commands/vacuum.h"
#include "executor/tuptable.h"
#include "utils/syscache.h"

#include "pgduckdb/pgduckdb_ruleutils.h"
}

extern "C" {

#define NOT_IMPLEMENTED()                                                                                              \
	ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED), errmsg("duckdb does not implement %s", __func__)))

PG_FUNCTION_INFO_V1(duckdb_am_handler);

/* ------------------------------------------------------------------------
 * Slot related callbacks for duckdb AM
 * ------------------------------------------------------------------------
 */

static const TupleTableSlotOps *
duckdb_slot_callbacks(Relation /*relation*/) {
	/*
	 * Here we would most likely want to invent your own set of slot
	 * callbacks for our AM. For now we just use the minimal tuple slot, we
	 * only implement this function to make sure ANALYZE does not fail.
	 */
	return &TTSOpsMinimalTuple;
}

/* ------------------------------------------------------------------------
 * Table Scan Callbacks for duckdb AM
 * ------------------------------------------------------------------------
 */

typedef struct DuckdbScanDescData {
	TableScanDescData rs_base; /* AM independent part of the descriptor */

	/* Add more fields here as needed by the AM. */
} DuckdbScanDescData;
typedef struct DuckdbScanDescData *DuckdbScanDesc;

static TableScanDesc
duckdb_scan_begin(Relation relation, Snapshot snapshot, int nkeys, ScanKey /*key*/, ParallelTableScanDesc parallel_scan,
                  uint32 flags) {
	DuckdbScanDesc scan = (DuckdbScanDesc)palloc(sizeof(DuckdbScanDescData));

	scan->rs_base.rs_rd = relation;
	scan->rs_base.rs_snapshot = snapshot;
	scan->rs_base.rs_nkeys = nkeys;
	scan->rs_base.rs_flags = flags;
	scan->rs_base.rs_parallel = parallel_scan;

	return (TableScanDesc)scan;
}

static void
duckdb_scan_end(TableScanDesc sscan) {
	DuckdbScanDesc scan = (DuckdbScanDesc)sscan;

	pfree(scan);
}

static void
duckdb_scan_rescan(TableScanDesc /*sscan*/, ScanKey /*key*/, bool /*set_params*/, bool /*allow_strat*/,
                   bool /*allow_sync*/, bool /*allow_pagemode*/) {
	NOT_IMPLEMENTED();
}

static bool
duckdb_scan_getnextslot(TableScanDesc /*sscan*/, ScanDirection /*direction*/, TupleTableSlot *slot) {
	/* If we are executing ALTER TABLE we return empty tuple */
	if (pgduckdb::top_level_duckdb_ddl_type == pgduckdb::DDLType::ALTER_TABLE) {
		ExecClearTuple(slot);
		return false;
	}
	NOT_IMPLEMENTED();
}

/* ------------------------------------------------------------------------
 * Index Scan Callbacks for duckdb AM
 * ------------------------------------------------------------------------
 */

static IndexFetchTableData *
duckdb_index_fetch_begin(Relation /*rel*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_index_fetch_reset(IndexFetchTableData * /*scan*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_index_fetch_end(IndexFetchTableData * /*scan*/) {
	NOT_IMPLEMENTED();
}

static bool
duckdb_index_fetch_tuple(struct IndexFetchTableData * /*scan*/, ItemPointer /*tid*/, Snapshot /*snapshot*/,
                         TupleTableSlot * /*slot*/, bool * /*call_again*/, bool * /*all_dead*/) {
	NOT_IMPLEMENTED();
}

/* ------------------------------------------------------------------------
 * Callbacks for non-modifying operations on individual tuples for
 * duckdb AM.
 * ------------------------------------------------------------------------
 */

static bool
duckdb_fetch_row_version(Relation /*relation*/, ItemPointer /*tid*/, Snapshot /*snapshot*/, TupleTableSlot * /*slot*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_get_latest_tid(TableScanDesc /*sscan*/, ItemPointer /*tid*/) {
	NOT_IMPLEMENTED();
}

static bool
duckdb_tuple_tid_valid(TableScanDesc /*scan*/, ItemPointer /*tid*/) {
	NOT_IMPLEMENTED();
}

static bool
duckdb_tuple_satisfies_snapshot(Relation /*rel*/, TupleTableSlot * /*slot*/, Snapshot /*snapshot*/) {
	NOT_IMPLEMENTED();
}

static TransactionId
duckdb_index_delete_tuples(Relation /*rel*/, TM_IndexDeleteOp * /*delstate*/) {
	NOT_IMPLEMENTED();
}

/* ----------------------------------------------------------------------------
 *  Functions for manipulations of physical tuples for duckdb AM.
 * ----------------------------------------------------------------------------
 */

static void
duckdb_tuple_insert(Relation /*relation*/, TupleTableSlot * /*slot*/, CommandId /*cid*/, int /*options*/,
                    BulkInsertState /*bistate*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_tuple_insert_speculative(Relation /*relation*/, TupleTableSlot * /*slot*/, CommandId /*cid*/, int /*options*/,
                                BulkInsertState /*bistate*/, uint32 /*specToken*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_tuple_complete_speculative(Relation /*relation*/, TupleTableSlot * /*slot*/, uint32 /*spekToken*/,
                                  bool /*succeeded*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_multi_insert(Relation /*relation*/, TupleTableSlot ** /*slots*/, int /*ntuples*/, CommandId /*cid*/,
                    int /*options*/, BulkInsertState /*bistate*/) {
	NOT_IMPLEMENTED();
}

static TM_Result
duckdb_tuple_delete(Relation /*relation*/, ItemPointer /*tid*/, CommandId /*cid*/, Snapshot /*snapshot*/,
                    Snapshot /*crosscheck*/, bool /*wait*/, TM_FailureData * /*tmfd*/, bool /*changingPart*/) {
	NOT_IMPLEMENTED();
}

#if PG_VERSION_NUM >= 160000

static TM_Result
duckdb_tuple_update(Relation /*relation*/, ItemPointer /*otid*/, TupleTableSlot * /*slot*/, CommandId /*cid*/,
                    Snapshot /*snapshot*/, Snapshot /*crosscheck*/, bool /*wait*/, TM_FailureData * /*tmfd*/,
                    LockTupleMode * /*lockmode*/, TU_UpdateIndexes * /*update_indexes*/) {
	NOT_IMPLEMENTED();
}

#else

static TM_Result
duckdb_tuple_update(Relation /*rel*/, ItemPointer /*otid*/, TupleTableSlot * /*slot*/, CommandId /*cid*/,
                    Snapshot /*snapshot*/, Snapshot /*crosscheck*/, bool /*wait*/, TM_FailureData * /*tmfd*/,
                    LockTupleMode * /*lockmode*/, bool * /*update_indexes*/) {
	NOT_IMPLEMENTED();
}

#endif

static TM_Result
duckdb_tuple_lock(Relation /*relation*/, ItemPointer /*tid*/, Snapshot /*snapshot*/, TupleTableSlot * /*slot*/,
                  CommandId /*cid*/, LockTupleMode /*mode*/, LockWaitPolicy /*wait_policy*/, uint8 /*flags*/,
                  TM_FailureData * /*tmfd*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_finish_bulk_insert(Relation /*relation*/, int /*options*/) {
	if (pgduckdb::top_level_duckdb_ddl_type == pgduckdb::DDLType::ALTER_TABLE) {
		return;
	}
	NOT_IMPLEMENTED();
}

/* ------------------------------------------------------------------------
 * DDL related callbacks for duckdb AM.
 * ------------------------------------------------------------------------
 */

#if PG_VERSION_NUM >= 160000

static void
duckdb_relation_set_new_filelocator(Relation rel, const RelFileLocator * /*newrnode*/, char /*persistence*/,
                                    TransactionId * /*freezeXid*/, MultiXactId * /*minmulti*/) {
	HeapTuple tp = SearchSysCache1(RELOID, ObjectIdGetDatum(rel->rd_id));
	if (!HeapTupleIsValid(tp)) {
		/* nothing to do, the table will be created in DuckDB later by the
		 * duckdb_create_table_trigger event trigger */
		return;
	}
	ReleaseSysCache(tp);
	DuckdbTruncateTable(rel->rd_id);
}

#else

static void
duckdb_relation_set_new_filenode(Relation rel, const RelFileNode * /*newrnode*/, char /*persistence*/,
                                 TransactionId * /*freezeXid*/, MultiXactId * /*minmulti*/) {
	HeapTuple tp = SearchSysCache1(RELOID, ObjectIdGetDatum(rel->rd_id));
	if (!HeapTupleIsValid(tp)) {
		/* nothing to do, the table will be created in DuckDB later by the
		 * duckdb_create_table_trigger event trigger */
		return;
	}
	ReleaseSysCache(tp);
	DuckdbTruncateTable(rel->rd_id);
}

#endif

static void
duckdb_relation_nontransactional_truncate(Relation rel) {
	DuckdbTruncateTable(rel->rd_id);
}

#if PG_VERSION_NUM >= 160000

static void
duckdb_copy_data(Relation /*rel*/, const RelFileLocator * /*newrnode*/) {
	NOT_IMPLEMENTED();
}

#else

static void
duckdb_copy_data(Relation /*rel*/, const RelFileNode * /*newrnode*/) {
	NOT_IMPLEMENTED();
}

#endif

static void
duckdb_copy_for_cluster(Relation /*OldTable*/, Relation /*NewTable*/, Relation /*OldIndex*/, bool /*use_sort*/,
                        TransactionId /*OldestXmin*/, TransactionId * /*xid_cutoff*/, MultiXactId * /*multi_cutoff*/,
                        double * /*num_tuples*/, double * /*tups_vacuumed*/, double * /*tups_recently_dead*/) {
	NOT_IMPLEMENTED();
}

static void
duckdb_vacuum(Relation /*onerel*/, VacuumParams * /*params*/, BufferAccessStrategy /*bstrategy*/) {
	NOT_IMPLEMENTED();
}

#if PG_VERSION_NUM >= 170000

static bool
duckdb_scan_analyze_next_block(TableScanDesc /*scan*/, ReadStream * /*stream*/) {
	/* no data in postgres, so no point to analyze next block */
	return false;
}

#else

static bool
duckdb_scan_analyze_next_block(TableScanDesc /*scan*/, BlockNumber /*blockno*/, BufferAccessStrategy /*bstrategy*/) {
	/* no data in postgres, so no point to analyze next block */
	return false;
}
#endif

static bool
duckdb_scan_analyze_next_tuple(TableScanDesc /*scan*/, TransactionId /*OldestXmin*/, double * /*liverows*/,
                               double * /*deadrows*/, TupleTableSlot * /*slot*/) {
	NOT_IMPLEMENTED();
}

static double
duckdb_index_build_range_scan(Relation /*tableRelation*/, Relation /*indexRelation*/, IndexInfo * /*indexInfo*/,
                              bool /*allow_sync*/, bool /*anyvisible*/, bool /*progress*/,
                              BlockNumber /*start_blockno*/, BlockNumber /*numblocks*/, IndexBuildCallback /*callback*/,
                              void * /*callback_state*/, TableScanDesc /*scan*/) {
	if (pgduckdb::top_level_duckdb_ddl_type == pgduckdb::DDLType::ALTER_TABLE) {
		return 0;
	}
	NOT_IMPLEMENTED();
}

static void
duckdb_index_validate_scan(Relation /*tableRelation*/, Relation /*indexRelation*/, IndexInfo * /*indexInfo*/,
                           Snapshot /*snapshot*/, ValidateIndexState * /*state*/) {
	NOT_IMPLEMENTED();
}

/* ------------------------------------------------------------------------
 * Miscellaneous callbacks for the duckdb AM
 * ------------------------------------------------------------------------
 */

static uint64
duckdb_relation_size(Relation /*rel*/, ForkNumber /*forkNumber*/) {
	/*
	 * For now we just return 0. We should probably want return something more
	 * useful in the future though.
	 */
	return 0;
}

/*
 * Check to see whether the table needs a TOAST table.
 */
static bool
duckdb_relation_needs_toast_table(Relation /*rel*/) {

	/* we don't need toast, because everything is stored in duckdb */
	return false;
}

/* ------------------------------------------------------------------------
 * Planner related callbacks for the duckdb AM
 * ------------------------------------------------------------------------
 */

static void
duckdb_estimate_rel_size(Relation /*rel*/, int32 *attr_widths, BlockNumber *pages, double *tuples, double *allvisfrac) {
	/* no data available */
	if (attr_widths)
		*attr_widths = 0;
	if (pages)
		*pages = 0;
	if (tuples)
		*tuples = 0;
	if (allvisfrac)
		*allvisfrac = 0;
}

/* ------------------------------------------------------------------------
 * Executor related callbacks for the duckdb AM
 * ------------------------------------------------------------------------
 */

#if PG_VERSION_NUM >= 180000

static bool
duckdb_scan_bitmap_next_tuple(TableScanDesc /*scan*/, TupleTableSlot * /*slot*/, bool * /*recheck*/,
                              uint64 * /*lossy_pages*/, uint64 * /*exact_pages*/) {
	NOT_IMPLEMENTED();
}

#else

static bool
duckdb_scan_bitmap_next_block(TableScanDesc /*scan*/, TBMIterateResult * /*tbmres*/) {
	NOT_IMPLEMENTED();
}

static bool
duckdb_scan_bitmap_next_tuple(TableScanDesc /*scan*/, TBMIterateResult * /*tbmres*/, TupleTableSlot * /*slot*/) {
	NOT_IMPLEMENTED();
}

#endif

static bool
duckdb_scan_sample_next_block(TableScanDesc /*scan*/, SampleScanState * /*scanstate*/) {
	NOT_IMPLEMENTED();
}

static bool
duckdb_scan_sample_next_tuple(TableScanDesc /*scan*/, SampleScanState * /*scanstate*/, TupleTableSlot * /*slot*/) {
	NOT_IMPLEMENTED();
}

/* ------------------------------------------------------------------------
 * Definition of the duckdb table access method.
 * ------------------------------------------------------------------------
 */

static const TableAmRoutine duckdb_methods = {.type = T_TableAmRoutine,

                                              .slot_callbacks = duckdb_slot_callbacks,

                                              .scan_begin = duckdb_scan_begin,
                                              .scan_end = duckdb_scan_end,
                                              .scan_rescan = duckdb_scan_rescan,
                                              .scan_getnextslot = duckdb_scan_getnextslot,

                                              /* optional callbacks */
                                              .scan_set_tidrange = NULL,
                                              .scan_getnextslot_tidrange = NULL,

                                              /* these are common helper functions */
                                              .parallelscan_estimate = table_block_parallelscan_estimate,
                                              .parallelscan_initialize = table_block_parallelscan_initialize,
                                              .parallelscan_reinitialize = table_block_parallelscan_reinitialize,

                                              .index_fetch_begin = duckdb_index_fetch_begin,
                                              .index_fetch_reset = duckdb_index_fetch_reset,
                                              .index_fetch_end = duckdb_index_fetch_end,
                                              .index_fetch_tuple = duckdb_index_fetch_tuple,

                                              .tuple_fetch_row_version = duckdb_fetch_row_version,
                                              .tuple_tid_valid = duckdb_tuple_tid_valid,
                                              .tuple_get_latest_tid = duckdb_get_latest_tid,
                                              .tuple_satisfies_snapshot = duckdb_tuple_satisfies_snapshot,
                                              .index_delete_tuples = duckdb_index_delete_tuples,

                                              .tuple_insert = duckdb_tuple_insert,
                                              .tuple_insert_speculative = duckdb_tuple_insert_speculative,
                                              .tuple_complete_speculative = duckdb_tuple_complete_speculative,
                                              .multi_insert = duckdb_multi_insert,
                                              .tuple_delete = duckdb_tuple_delete,
                                              .tuple_update = duckdb_tuple_update,
                                              .tuple_lock = duckdb_tuple_lock,
                                              .finish_bulk_insert = duckdb_finish_bulk_insert,

#if PG_VERSION_NUM >= 160000
                                              .relation_set_new_filelocator = duckdb_relation_set_new_filelocator,
#else
                                              .relation_set_new_filenode = duckdb_relation_set_new_filenode,
#endif
                                              .relation_nontransactional_truncate =
                                                  duckdb_relation_nontransactional_truncate,
                                              .relation_copy_data = duckdb_copy_data,
                                              .relation_copy_for_cluster = duckdb_copy_for_cluster,
                                              .relation_vacuum = duckdb_vacuum,
                                              .scan_analyze_next_block = duckdb_scan_analyze_next_block,
                                              .scan_analyze_next_tuple = duckdb_scan_analyze_next_tuple,
                                              .index_build_range_scan = duckdb_index_build_range_scan,
                                              .index_validate_scan = duckdb_index_validate_scan,

                                              .relation_size = duckdb_relation_size,
                                              .relation_needs_toast_table = duckdb_relation_needs_toast_table,
                                              /* can be null because relation_needs_toast_table returns false */
                                              .relation_toast_am = NULL,
                                              .relation_fetch_toast_slice = NULL,

                                              .relation_estimate_size = duckdb_estimate_rel_size,
#if PG_VERSION_NUM < 180000
                                              .scan_bitmap_next_block = duckdb_scan_bitmap_next_block,
#endif
                                              .scan_bitmap_next_tuple = duckdb_scan_bitmap_next_tuple,
                                              .scan_sample_next_block = duckdb_scan_sample_next_block,
                                              .scan_sample_next_tuple = duckdb_scan_sample_next_tuple};

Datum
duckdb_am_handler(FunctionCallInfo /*funcinfo*/) {
	PG_RETURN_POINTER(&duckdb_methods);
}
}

static duckdb::unordered_map<const TableAmRoutine * /*am*/, duckdb::string /*name*/> duckdb_table_ams = {
    {&duckdb_methods, "duckdb"}};

extern "C" __attribute__((visibility("default"))) bool
RegisterDuckdbTableAm(const char *name, const TableAmRoutine *am) {
	return duckdb_table_ams.emplace(am, name).second;
}

namespace pgduckdb {
bool
IsDuckdbTableAm(const TableAmRoutine *am) {
	return am == &duckdb_methods;
}

const char *
DuckdbTableAmGetName(const TableAmRoutine *am) {
	auto it = duckdb_table_ams.find(am);
	return it == duckdb_table_ams.end() ? nullptr : it->second.c_str();
}

const char *
DuckdbTableAmGetName(Oid relid) {
	if (relid == InvalidOid) {
		return nullptr;
	}

	auto rel = RelationIdGetRelation(relid);
	const char *name = DuckdbTableAmGetName(rel->rd_tableam);
	RelationClose(rel);
	return name;
}
} // namespace pgduckdb
