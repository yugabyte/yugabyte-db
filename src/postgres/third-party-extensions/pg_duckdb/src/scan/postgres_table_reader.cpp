#include "pgduckdb/scan/postgres_table_reader.hpp"
#include "pgduckdb/pgduckdb_process_lock.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pgduckdb_guc.hpp"

extern "C" {
#include "postgres.h"
#include "miscadmin.h"
#include "access/xact.h"
#include "commands/explain.h"
#if PG_VERSION_NUM >= 180000
#include "commands/explain_format.h"
#include "commands/explain_state.h"
#endif
#include "executor/executor.h"
#include "executor/execParallel.h"
#include "executor/tqueue.h"
#include "optimizer/planmain.h"
#include "optimizer/planner.h"
#include "tcop/tcopprot.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/wait_event.h"
}

#include "pgduckdb/vendor/pg_list.hpp"

#include <cmath>

namespace pgduckdb {

PostgresTableReader::PostgresTableReader()
    : table_scan_query_desc(nullptr), table_scan_planstate(nullptr), parallel_executor_info(nullptr),
      parallel_worker_readers(nullptr), slot(nullptr), nworkers_launched(0), nreaders(0), next_parallel_reader(0),
      entered_parallel_mode(false), cleaned_up(false) {
}

void
PostgresTableReader::Init(const char *table_scan_query, bool count_tuples_only) {
	std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
	PostgresScopedStackReset scoped_stack_reset;
	PostgresMemberGuard(PostgresTableReader::InitUnsafe, table_scan_query, count_tuples_only);
}

void
PostgresTableReader::InitUnsafe(const char *table_scan_query, bool count_tuples_only) {
	List *raw_parsetree_list = pg_parse_query(table_scan_query);
	Assert(list_length(raw_parsetree_list) == 1);
	RawStmt *raw_parsetree = linitial_node(RawStmt, raw_parsetree_list);

#if PG_VERSION_NUM >= 150000
	List *query_list = pg_analyze_and_rewrite_fixedparams(raw_parsetree, table_scan_query, nullptr, 0, nullptr);
#else
	List *query_list = pg_analyze_and_rewrite(raw_parsetree, table_scan_query, nullptr, 0, nullptr);
#endif

	Assert(list_length(query_list) == 1);
	Query *query = linitial_node(Query, query_list);

	Assert(list_length(query->rtable) == 1);
	RangeTblEntry *rte = linitial_node(RangeTblEntry, query->rtable);

	char persistence = get_rel_persistence(rte->relid);

	PlannedStmt *planned_stmt = standard_planner(query, table_scan_query, 0, nullptr);

	table_scan_query_desc = CreateQueryDesc(planned_stmt, table_scan_query, GetActiveSnapshot(), InvalidSnapshot,
	                                        None_Receiver, nullptr, nullptr, 0);

	ExecutorStart(table_scan_query_desc, 0);

	table_scan_planstate = ExecInitNode(planned_stmt->planTree, table_scan_query_desc->estate, 0);

	bool run_scan_with_parallel_workers = persistence != RELPERSISTENCE_TEMP;
	run_scan_with_parallel_workers &= CanTableScanRunInParallel(table_scan_query_desc->planstate->plan);

	/* Temp tables cannot be excuted with parallel workers, and whole plan should be parallel aware */
	if (run_scan_with_parallel_workers) {
		InitRunWithParallelScan(planned_stmt, count_tuples_only);
	}

	if (duckdb_log_pg_explain) {
		ExplainState *es = (ExplainState *)palloc0(sizeof(ExplainState));
		es->str = makeStringInfo();
		es->format = EXPLAIN_FORMAT_TEXT;
		ExplainPrintPlan(es, table_scan_query_desc);
		elog(NOTICE, "(PGDuckDB/PostgresTableReader)\n\nQUERY: %s\nRUNNING: %s.\nEXECUTING: \n%s", table_scan_query,
		     !nreaders ? "IN PROCESS THREAD" : psprintf("ON %d PARALLEL WORKER(S)", nreaders), es->str->data);
	}

	slot = ExecInitExtraTupleSlot(table_scan_query_desc->estate, table_scan_planstate->ps_ResultTupleDesc,
	                              &TTSOpsMinimalTuple);
}

void
PostgresTableReader::InitRunWithParallelScan(PlannedStmt *planned_stmt, bool count_tuples_only) {
	int parallel_workers = 0;
	if (count_tuples_only) {
		/* For count_tuples_only we will try to execute aggregate node on table scan */
		planned_stmt->planTree->parallel_aware = true;
		MarkPlanParallelAware((Plan *)table_scan_query_desc->planstate->plan->lefttree);
		parallel_workers = ParallelWorkerNumber(planned_stmt->planTree->lefttree->plan_rows);
	} else {
		MarkPlanParallelAware(table_scan_query_desc->planstate->plan);
		parallel_workers = ParallelWorkerNumber(planned_stmt->planTree->plan_rows);
	}

	bool interrupts_can_be_process = INTERRUPTS_CAN_BE_PROCESSED();
	if (!interrupts_can_be_process) {
		RESUME_CANCEL_INTERRUPTS();
	}

	EnterParallelMode();
	entered_parallel_mode = true;

	ParallelContext *pcxt;
	parallel_executor_info =
	    ExecInitParallelPlan(table_scan_planstate, table_scan_query_desc->estate, nullptr, parallel_workers, -1);
	pcxt = parallel_executor_info->pcxt;
	LaunchParallelWorkers(pcxt);
	nworkers_launched = pcxt->nworkers_launched;

	if (pcxt->nworkers_launched > 0) {
		ExecParallelCreateReaders(parallel_executor_info);
		nreaders = pcxt->nworkers_launched;
		parallel_worker_readers = (void **)palloc(nreaders * sizeof(TupleQueueReader *));
		memcpy(parallel_worker_readers, parallel_executor_info->reader, nreaders * sizeof(TupleQueueReader *));
	}

	if (!interrupts_can_be_process) {
		HOLD_CANCEL_INTERRUPTS();
	}
}

/*
 * Initializes a tuple slot for the current query.
 *
 * The returned TupleTableSlot is allocated in the context of `table_scan_query_desc->estate`
 * and will be automatically cleaned up when `table_scan_query_desc` is destroyed.
 *
 * Returns NULL if the table reader has already been cleaned up, e.g., if concurrent threads
 * have finished the scan and triggered cleanup).
 */
TupleTableSlot *
PostgresTableReader::InitTupleSlot() {
	D_ASSERT(!cleaned_up);
	return PostgresFunctionGuard(ExecInitExtraTupleSlot, table_scan_query_desc->estate,
	                             table_scan_planstate->ps_ResultTupleDesc, &TTSOpsMinimalTuple);
}

PostgresTableReader::~PostgresTableReader() {
	if (cleaned_up) {
		return;
	}
	std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
	Cleanup();
}

void
PostgresTableReader::Cleanup() {
	D_ASSERT(!cleaned_up);
	cleaned_up = true;
	PostgresScopedStackReset scoped_stack_reset;
	PostgresMemberGuard(PostgresTableReader::CleanupUnsafe);
}

void
PostgresTableReader::CleanupUnsafe() {
	if (table_scan_planstate) {
		ExecEndNode(table_scan_planstate);
		table_scan_planstate = nullptr;
	}

	if (parallel_executor_info != NULL) {
		ExecParallelFinish(parallel_executor_info);
		ExecParallelCleanup(parallel_executor_info);
		parallel_executor_info = nullptr;
	}

	if (parallel_worker_readers) {
		pfree(parallel_worker_readers);
		parallel_worker_readers = nullptr;
	}

	if (table_scan_query_desc) {
		ExecutorFinish(table_scan_query_desc);
		ExecutorEnd(table_scan_query_desc);
		FreeQueryDesc(table_scan_query_desc);
		table_scan_query_desc = nullptr;
	}

	if (entered_parallel_mode) {
		ExitParallelMode();
		entered_parallel_mode = false;
	}
}

/*
 * Logic is straightforward, if `duckdb_max_workers_per_postgres_scan` is set to 0 we don't want any
 * parallelization. For cardinality less equal than 2^16 we only try to run one parallel process. When cardinality
 * is bigger than we should spawn numer of parallel processes set by `duckdb_max_workers_per_postgres_scan` but
 * not bigger than `max_parallel_workers`.
 */

int
PostgresTableReader::ParallelWorkerNumber(Cardinality cardinality) {
	static const int cardinality_threshold = 1 << 16;
	/* No parallel worker scan wanted */
	if (!duckdb_max_workers_per_postgres_scan) {
		return 0;
	}
	/* Use only one worker when scan is done on low cardinality */
	if (cardinality <= cardinality_threshold) {
		return 1;
	}
	return std::min(duckdb_max_workers_per_postgres_scan, max_parallel_workers);
}

bool
PostgresTableReader::CanTableScanRunInParallel(Plan *plan) {
	switch (nodeTag(plan)) {
	case T_SeqScan:
	case T_IndexScan:
	case T_IndexOnlyScan:
	case T_BitmapHeapScan:
		return true;
	case T_Append: {
		ListCell *l;
		foreach (l, ((Append *)plan)->appendplans) {
			if (!CanTableScanRunInParallel((Plan *)lfirst(l))) {
				return false;
			}
		}
		return true;
	}
	/* This is special case for COUNT(*) */
	case T_Agg:
		return true;
	default:
		return false;
	}
}

bool
PostgresTableReader::MarkPlanParallelAware(Plan *plan) {
	switch (nodeTag(plan)) {
	case T_SeqScan:
	case T_IndexScan:
	case T_IndexOnlyScan:
	case T_Append: {
		plan->parallel_aware = true;
		return true;
	}
	case T_BitmapHeapScan: {
		plan->parallel_aware = true;
		return MarkPlanParallelAware(plan->lefttree);
	}
	case T_BitmapIndexScan: {
		((BitmapIndexScan *)plan)->isshared = true;
		return true;
	}
	case T_BitmapAnd: {
		return MarkPlanParallelAware((Plan *)linitial(((BitmapAnd *)plan)->bitmapplans));
	}
	case T_BitmapOr: {
		((BitmapOr *)plan)->isshared = true;
		return MarkPlanParallelAware((Plan *)linitial(((BitmapOr *)plan)->bitmapplans));
	}
	default: {
		std::ostringstream oss;
		oss << "Unknown postgres scan query plan node: " << nodeTag(plan);
		throw duckdb::Exception(duckdb::ExceptionType::EXECUTOR, oss.str().c_str());
	}
	}
}

/*
 * GlobalProcessLock should be held before calling this.
 */
TupleTableSlot *
PostgresTableReader::GetNextTuple() {
	return PostgresMemberGuard(PostgresTableReader::GetNextTupleUnsafe);
}

TupleTableSlot *
PostgresTableReader::GetNextTupleUnsafe() {
	if (nreaders > 0) {
		MinimalTuple worker_minmal_tuple = GetNextWorkerTuple();
		if (HeapTupleIsValid(worker_minmal_tuple)) {
			ExecStoreMinimalTuple(worker_minmal_tuple, slot, false);
			return slot;
		}
	}

	PostgresScopedStackReset scoped_stack_reset;
	table_scan_query_desc->estate->es_query_dsa = parallel_executor_info ? parallel_executor_info->area : NULL;
	TupleTableSlot *thread_scan_slot = ExecProcNode(table_scan_planstate);
	table_scan_query_desc->estate->es_query_dsa = NULL;
	return TupIsNull(thread_scan_slot) ? ExecClearTuple(slot) : thread_scan_slot;
}

/*
 * Reads the next minimal tuple from a Postgres parallel worker and copies it into the provided buffer.
 * This function should only be called when the table scan is running with parallel workers.
 *
 * @param minimal_tuple_buffer Buffer to store the copied minimal tuple.
 * @return true if a tuple was read and copied; false if the scan is complete and no more tuples are available.
 *
 * Note: The caller must hold the GlobalProcessLock before invoking this function.
 */
bool
PostgresTableReader::GetNextMinimalWorkerTuple(std::vector<uint8_t> &minimal_tuple_buffer) {
	MinimalTuple worker_minmal_tuple = GetNextWorkerTuple();
	if (HeapTupleIsValid(worker_minmal_tuple)) {
		// deep copy worker_minmal_tuple to destination buffer
		Size tuple_size = worker_minmal_tuple->t_len + MINIMAL_TUPLE_DATA_OFFSET;
		minimal_tuple_buffer.resize(tuple_size);
		memcpy(minimal_tuple_buffer.data(), worker_minmal_tuple, tuple_size);
		return true;
	}

	minimal_tuple_buffer.resize(0);
	return false;
}

MinimalTuple
PostgresTableReader::GetNextWorkerTuple() {
	int nvisited = 0;
	TupleQueueReader *reader = NULL;
	MinimalTuple minimal_tuple = NULL;
	bool readerdone = false;
	// The loop's stop condition acts as a safeguard in multithreaded scans, ensuring that if one thread calls this
	// function after another thread has already completed the scan, we do not access invalid readers.
	for (; next_parallel_reader < nreaders;) {
		reader = (TupleQueueReader *)parallel_worker_readers[next_parallel_reader];

		minimal_tuple = TupleQueueReaderNext(reader, true, &readerdone);

		if (readerdone) {
			--nreaders;
			if (nreaders == 0) {
				return NULL;
			}

			memmove(&parallel_worker_readers[next_parallel_reader], &parallel_worker_readers[next_parallel_reader + 1],
			        sizeof(TupleQueueReader *) * (nreaders - next_parallel_reader));
			if (next_parallel_reader >= nreaders) {
				next_parallel_reader = 0;
			}
			continue;
		}

		if (minimal_tuple) {
			return minimal_tuple;
		}

		next_parallel_reader++;
		if (next_parallel_reader >= nreaders) {
			next_parallel_reader = 0;
		}

		nvisited++;
		if (nvisited >= nreaders) {
			/*
			 * It should be safe to make this call because function calling GetNextTuple() and transitively
			 * GetNextWorkerTuple() should held GlobalProcesLock.
			 */
			WaitLatch(MyLatch, WL_LATCH_SET | WL_EXIT_ON_PM_DEATH, 0, PG_WAIT_EXTENSION);
			/* No need to use PostgresFunctionGuard here, because ResetLatch is a trivial function */
			ResetLatch(MyLatch);
			nvisited = 0;
		}
	}

	return NULL;
}

} // namespace pgduckdb
