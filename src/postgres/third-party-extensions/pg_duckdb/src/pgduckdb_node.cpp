
#include "duckdb.hpp"
#include "duckdb/common/exception/conversion_exception.hpp"
#include "duckdb/common/exception.hpp"

#include "pgduckdb/pgduckdb_hooks.hpp"
#include "pgduckdb/pgduckdb_planner.hpp"
#include "pgduckdb/pgduckdb_types.hpp"
#include "pgduckdb/vendor/pg_explain.hpp"
#include "pgduckdb/pg/explain.hpp"

extern "C" {
#include "postgres.h"
#include "miscadmin.h"
#include "tcop/pquery.h"
#include "nodes/params.h"
#include "utils/ruleutils.h"
}

#include "pgduckdb/pgduckdb_node.hpp"
#include "pgduckdb/pgduckdb_duckdb.hpp"
#include "pgduckdb/utility/cpp_wrapper.hpp"

bool duckdb_explain_analyze = false;
bool duckdb_explain_ctas = false;
duckdb::ExplainFormat duckdb_explain_format = duckdb::ExplainFormat::DEFAULT;

#define NEED_JSON_PLAN(explain_format) (explain_format == duckdb::ExplainFormat::JSON)

/* global variables */
CustomScanMethods duckdb_scan_scan_methods;

/* static variables */
static CustomExecMethods duckdb_scan_exec_methods;

typedef struct DuckdbScanState {
	CustomScanState css; /* must be first field */
	const CustomScan *custom_scan;
	const Query *query;
	ParamListInfo params;
	duckdb::Connection *duckdb_connection;
	duckdb::PreparedStatement *prepared_statement;
	bool is_executed;
	bool fetch_next;
	duckdb::unique_ptr<duckdb::QueryResult> query_results;
	duckdb::idx_t column_count;
	duckdb::unique_ptr<duckdb::DataChunk> current_data_chunk;
	duckdb::idx_t current_row;
} DuckdbScanState;

static void
CleanupDuckdbScanState(DuckdbScanState *state) {
	MemoryContextReset(state->css.ss.ps.ps_ExprContext->ecxt_per_tuple_memory);
	ExecClearTuple(state->css.ss.ss_ScanTupleSlot);

	state->query_results.reset();
	state->current_data_chunk.reset();

	if (state->prepared_statement) {
		delete state->prepared_statement;
		state->prepared_statement = nullptr;
	}
}

/* static callbacks */
static Node *Duckdb_CreateCustomScanState(CustomScan *cscan);
static void Duckdb_BeginCustomScan(CustomScanState *node, EState *estate, int eflags);
static TupleTableSlot *Duckdb_ExecCustomScan(CustomScanState *node);
static void Duckdb_EndCustomScan(CustomScanState *node);
static void Duckdb_ReScanCustomScan(CustomScanState *node);
static void Duckdb_ExplainCustomScan(CustomScanState *node, List *ancestors, ExplainState *es);
static inline void formatDuckDbPlanForPG(const char *duckdb_plan, ExplainState *es);

static Node *
Duckdb_CreateCustomScanState(CustomScan *cscan) {
	DuckdbScanState *duckdb_scan_state = (DuckdbScanState *)newNode(sizeof(DuckdbScanState), T_CustomScanState);
	CustomScanState *custom_scan_state = &duckdb_scan_state->css;
	duckdb_scan_state->custom_scan = cscan;

	duckdb_scan_state->query = (const Query *)linitial(cscan->custom_private);
	custom_scan_state->methods = &duckdb_scan_exec_methods;
	return (Node *)custom_scan_state;
}

void
Duckdb_BeginCustomScan_Cpp(CustomScanState *cscanstate, EState *estate, int /*eflags*/) {
	DuckdbScanState *duckdb_scan_state = (DuckdbScanState *)cscanstate;

	StringInfo explain_prefix = makeStringInfo();

	bool is_explain_query = ActivePortal && ActivePortal->commandTag == CMDTAG_EXPLAIN;

	if (is_explain_query) {
		appendStringInfoString(explain_prefix, "EXPLAIN ");

		if (NEED_JSON_PLAN(duckdb_explain_format))
			appendStringInfoChar(explain_prefix, '(');

		if (duckdb_explain_analyze) {
			if (duckdb_explain_ctas) {
				throw duckdb::NotImplementedException(
				    "Cannot use EXPLAIN ANALYZE with CREATE TABLE ... AS when using DuckDB execution");
			}
			if (NEED_JSON_PLAN(duckdb_explain_format))
				appendStringInfoString(explain_prefix, "ANALYZE, ");
			else
				appendStringInfoString(explain_prefix, "ANALYZE ");
		}

		if (NEED_JSON_PLAN(duckdb_explain_format)) {
			appendStringInfoString(explain_prefix, "FORMAT JSON )");
		}
	}

	duckdb::unique_ptr<duckdb::PreparedStatement> prepared_query =
	    DuckdbPrepare(duckdb_scan_state->query, explain_prefix->data);

	if (prepared_query->HasError()) {
		throw duckdb::Exception(duckdb::ExceptionType::EXECUTOR,
		                        "DuckDB re-planning failed: " + prepared_query->GetError());
	}

	if (!is_explain_query) {
		auto &prepared_result_types = prepared_query->GetTypes();

		size_t target_list_length = static_cast<size_t>(list_length(duckdb_scan_state->custom_scan->custom_scan_tlist));

		if (prepared_result_types.size() != target_list_length) {
			elog(ERROR,
			     "(PGDuckDB/CreatePlan) Number of columns returned by DuckDB query changed between planning and "
			     "execution, expected %zu got %zu",
			     target_list_length, prepared_result_types.size());
		}

		for (size_t i = 0; i < prepared_result_types.size(); i++) {
			Oid postgres_column_oid = pgduckdb::GetPostgresDuckDBType(prepared_result_types[i], true);

			TargetEntry *target_entry =
			    list_nth_node(TargetEntry, duckdb_scan_state->custom_scan->custom_scan_tlist, i);
			Var *var = castNode(Var, target_entry->expr);
			if (var->vartype != postgres_column_oid) {
				elog(ERROR, "Types returned by duckdb query changed between planning and execution, expected %d got %d",
				     var->vartype, postgres_column_oid);
			}
		}
	}

	duckdb_scan_state->duckdb_connection = pgduckdb::DuckDBManager::GetConnection();
	duckdb_scan_state->prepared_statement = prepared_query.release();
	duckdb_scan_state->params = estate->es_param_list_info;
	duckdb_scan_state->is_executed = false;
	duckdb_scan_state->fetch_next = true;
	duckdb_scan_state->css.ss.ps.ps_ResultTupleDesc = duckdb_scan_state->css.ss.ss_ScanTupleSlot->tts_tupleDescriptor;
	HOLD_CANCEL_INTERRUPTS();
}

void
Duckdb_BeginCustomScan(CustomScanState *cscanstate, EState *estate, int eflags) {
	InvokeCPPFunc(Duckdb_BeginCustomScan_Cpp, cscanstate, estate, eflags);
}

static void
ExecuteQuery(DuckdbScanState *state) {
	auto &prepared = *state->prepared_statement;
	auto pg_params = state->params;
	const auto num_params = pg_params ? pg_params->numParams : 0;
	duckdb::case_insensitive_map_t<duckdb::BoundParameterData> named_values;

	for (int i = 0; i < num_params; i++) {
		ParamExternData *pg_param;
		ParamExternData tmp_workspace;
		duckdb::Value duckdb_param;

		/* give hook a chance in case parameter is dynamic */
		if (pg_params->paramFetch != NULL) {
			pg_param = pg_params->paramFetch(pg_params, i + 1, false, &tmp_workspace);
		} else {
			pg_param = &pg_params->params[i];
		}

		if (prepared.named_param_map.count(duckdb::to_string(i + 1)) == 0) {
			continue;
		}

		if (pg_param->isnull) {
			duckdb_param = duckdb::Value();
		} else if (OidIsValid(pg_param->ptype)) {
			duckdb_param = pgduckdb::ConvertPostgresParameterToDuckValue(pg_param->value, pg_param->ptype);
		} else {
			std::ostringstream oss;
			oss << "parameter '" << i << "' has an invalid type (" << pg_param->ptype << ") during query execution";
			throw duckdb::Exception(duckdb::ExceptionType::EXECUTOR, oss.str().c_str());
		}
		named_values[duckdb::to_string(i + 1)] = duckdb::BoundParameterData(duckdb_param);
	}

	// Set `allow_stream_result` to false if the query contains a Postgres table to force a fully materialized DuckDB
	// result. This is required for cases like CTAS from a Postgres table, where allowing streaming results could lead
	// to race conditions on Postgres resources.
	// Checkout discussion: https://github.com/duckdb/pg_duckdb/discussions/866
	bool allow_stream_result = !pgduckdb::ContainsPostgresTable((Node *)state->query, NULL);
	auto pending = prepared.PendingQuery(named_values, allow_stream_result);
	if (pending->HasError()) {
		return pending->ThrowError();
	}

	duckdb::PendingExecutionResult execution_result = duckdb::PendingExecutionResult::RESULT_NOT_READY;
	while (true) {
		execution_result = pending->ExecuteTask();
		if (duckdb::PendingQueryResult::IsResultReady(execution_result)) {
			break;
		}

		if (QueryCancelPending) {
			auto &connection = state->duckdb_connection;
			// Send an interrupt
			connection->Interrupt();
			auto &executor = duckdb::Executor::Get(*connection->context);
			// Wait for all tasks to terminate
			executor.CancelTasks();

			try {
				// When the "Query cancelled" exception below is thrown,
				// various destructors are called, among which `PostgresTableReader`'s
				// which cleanup the PG state. If an exception is thrown during
				// the stack unwinding and call to PG function, it results in an
				// undefined behavior which materialize as a process crash.
				// So to avoid that, we eagerly consume the pending tasks.
				do {
					execution_result = pending->ExecuteTask();
				} while (execution_result != duckdb::PendingExecutionResult::EXECUTION_ERROR &&
				         execution_result != duckdb::PendingExecutionResult::NO_TASKS_AVAILABLE &&
				         execution_result != duckdb::PendingExecutionResult::EXECUTION_FINISHED);

				pending->Close();
			} catch (std::exception &ex) {
			}
			// Delete the scan state
			// Process the interrupt on the Postgres side
			ProcessInterrupts();
			throw duckdb::Exception(duckdb::ExceptionType::EXECUTOR, "Query cancelled");
		}
	}

	if (execution_result == duckdb::PendingExecutionResult::EXECUTION_ERROR) {
		return pending->ThrowError();
	}

	state->query_results = pending->Execute();
	state->column_count = state->query_results->ColumnCount();
	state->is_executed = true;
}

static TupleTableSlot *
Duckdb_ExecCustomScan_Cpp(CustomScanState *node) {
	DuckdbScanState *duckdb_scan_state = (DuckdbScanState *)node;
	try {
		TupleTableSlot *slot = duckdb_scan_state->css.ss.ss_ScanTupleSlot;
		MemoryContext old_context;

		if (ActivePortal && ActivePortal->commandTag == CMDTAG_EXPLAIN) {
			ExecClearTuple(slot);
			return slot;
		}

		bool already_executed = duckdb_scan_state->is_executed;
		if (!already_executed) {
			ExecuteQuery(duckdb_scan_state);
		}

		if (duckdb_scan_state->fetch_next) {
			duckdb_scan_state->current_data_chunk = duckdb_scan_state->query_results->Fetch();
			duckdb_scan_state->current_row = 0;
			duckdb_scan_state->fetch_next = false;
			if (!duckdb_scan_state->current_data_chunk || duckdb_scan_state->current_data_chunk->size() == 0) {
				MemoryContextReset(duckdb_scan_state->css.ss.ps.ps_ExprContext->ecxt_per_tuple_memory);
				ExecClearTuple(slot);
				return slot;
			}
		}

		MemoryContextReset(duckdb_scan_state->css.ss.ps.ps_ExprContext->ecxt_per_tuple_memory);
		ExecClearTuple(slot);

		/* MemoryContext used for allocation */
		old_context = MemoryContextSwitchTo(duckdb_scan_state->css.ss.ps.ps_ExprContext->ecxt_per_tuple_memory);

		for (idx_t col = 0; col < duckdb_scan_state->column_count; col++) {
			// FIXME: we should not use the Value API here, it's complicating the LIST conversion logic
			auto value = duckdb_scan_state->current_data_chunk->GetValue(col, duckdb_scan_state->current_row);
			if (value.IsNull()) {
				slot->tts_isnull[col] = true;
			} else {
				slot->tts_isnull[col] = false;
				if (!pgduckdb::ConvertDuckToPostgresValue(slot, value, col)) {
					throw duckdb::ConversionException("Value conversion failed");
				}
			}
		}

		MemoryContextSwitchTo(old_context);

		duckdb_scan_state->current_row++;
		if (duckdb_scan_state->current_row >= duckdb_scan_state->current_data_chunk->size()) {
			duckdb_scan_state->current_data_chunk.reset();
			duckdb_scan_state->fetch_next = true;
		}

		ExecStoreVirtualTuple(slot);
		return slot;
	} catch (std::exception &ex) {
		/*
		 * In case any error happens we need to still cleanup the scan state,
		 * otherwise we do not clean up the prepared statement and various
		 * other DuckDB objects.
		 *
		 * NOTE: We only clean this up on error, not on success. On success we
		 * still need these objects to be around for the next call to
		 * ExecCustomScan. If the full scan completes successfully, the cleanup
		 * will be done in EndCustomScan.
		 */
		CleanupDuckdbScanState(duckdb_scan_state);
		throw;
	}
}

static TupleTableSlot *
Duckdb_ExecCustomScan(CustomScanState *node) {
	return InvokeCPPFunc(Duckdb_ExecCustomScan_Cpp, node);
}

void
Duckdb_EndCustomScan_Cpp(CustomScanState *node) {
	DuckdbScanState *duckdb_scan_state = (DuckdbScanState *)node;
	CleanupDuckdbScanState(duckdb_scan_state);
	/*
	 * BUG: In rare error casess it's possible that we call this when we are
	 * currently accepting interupts, in those cases we should not resume them
	 * yet again otherwise QueryCancelHoldoffCount becomes negative. We should
	 * fix this, but it's not easy to reproduce this issue. So for now we at
	 * least make sure that users without assert builds won't hit it in
	 * production. So check that we are allowed to decrement it.
	 */
#ifdef USE_ASSERT_CHECKING
	RESUME_CANCEL_INTERRUPTS();
#else
	if (QueryCancelHoldoffCount > 0) {
		RESUME_CANCEL_INTERRUPTS();
	}
#endif
}

void
Duckdb_EndCustomScan(CustomScanState *node) {
	InvokeCPPFunc(Duckdb_EndCustomScan_Cpp, node);
}

void
Duckdb_ReScanCustomScan(CustomScanState * /*node*/) {
}

void
Duckdb_ExplainCustomScan_Cpp(CustomScanState *node, ExplainState *es) {
	/*
	 * XXX: The code to set duckdb_explain_analyze and duckdb_explain_format,
	 * is copied from ExplainOneQueryHook. Sadly that hook is not run for
	 * EXPLAIN EXECUTE ..., and the code here runs too late to actually impact
	 * the query that we send to DuckDB. However, putting it here as well is a
	 * hacky bandaid to make the code below not crash. And it has the
	 * sideeffect that if you run EXPLAIN EXECUTE twice in a row, you will get
	 * the intended output. Since EXPLAIN EXECUTE is pretty rare for people to
	 * run, we consider this fine for now.
	 */
	duckdb_explain_analyze = pgduckdb::pg::IsExplainAnalyze(es);
	duckdb_explain_format = pgduckdb::pg::DuckdbExplainFormat(es);

	DuckdbScanState *duckdb_scan_state = (DuckdbScanState *)node;
	ExecuteQuery(duckdb_scan_state);

	auto chunk = duckdb_scan_state->query_results->Fetch();
	if (!chunk || chunk->size() == 0) {
		return;
	}

	/* Is it safe to hardcode this as result of DuckDB explain? */
	auto value = chunk->GetValue(1, 0).GetValue<duckdb::string>();

	/* Fully consume the stream */
	do {
		chunk = duckdb_scan_state->query_results->Fetch();
	} while (chunk && chunk->size() > 0);

	std::ostringstream explain_output;
	explain_output << "\n\n" << value << "\n";
	if (NEED_JSON_PLAN(duckdb_explain_format)) {

		// Formatting, copied formatting in JSON mode
		if (linitial_int(es->grouping_stack) != 0)
			appendStringInfoChar(es->str, ',');
		else
			linitial_int(es->grouping_stack) = 1;
		appendStringInfoChar(es->str, '\n');
		appendStringInfoSpaces(es->str, es->indent * 2);
		appendStringInfoString(es->str, "\"DuckDB Execution Plan\": ");
		formatDuckDbPlanForPG(value.c_str(), es);
	} else
		pgduckdb::pg::ExplainPropertyText("DuckDB Execution Plan", explain_output.str().c_str(), es);
}

static inline void
formatDuckDbPlanForPG(const char *duckdb_plan, ExplainState *es) {
	const char *ptr = duckdb_plan;
	while (*ptr != '\0') {
		appendStringInfoChar(es->str, *ptr);
		if (*ptr == '\n') {
			// Add indentation after each newline
			appendStringInfoSpaces(es->str, es->indent * 2);
		}

		ptr++;
	}
}

void
Duckdb_ExplainCustomScan(CustomScanState *node, List * /*ancestors*/, ExplainState *es) {
	InvokeCPPFunc(Duckdb_ExplainCustomScan_Cpp, node, es);
}

void
DuckdbInitNode() {
	/* setup scan methods */
	memset(&duckdb_scan_scan_methods, 0, sizeof(duckdb_scan_scan_methods));
	duckdb_scan_scan_methods.CustomName = "DuckDBScan";
	duckdb_scan_scan_methods.CreateCustomScanState = Duckdb_CreateCustomScanState;
	RegisterCustomScanMethods(&duckdb_scan_scan_methods);

	/* setup exec methods */
	memset(&duckdb_scan_exec_methods, 0, sizeof(duckdb_scan_exec_methods));
	duckdb_scan_exec_methods.CustomName = "DuckDBScan";

	duckdb_scan_exec_methods.BeginCustomScan = Duckdb_BeginCustomScan;
	duckdb_scan_exec_methods.ExecCustomScan = Duckdb_ExecCustomScan;
	duckdb_scan_exec_methods.EndCustomScan = Duckdb_EndCustomScan;
	duckdb_scan_exec_methods.ReScanCustomScan = Duckdb_ReScanCustomScan;

	duckdb_scan_exec_methods.EstimateDSMCustomScan = NULL;
	duckdb_scan_exec_methods.InitializeDSMCustomScan = NULL;
	duckdb_scan_exec_methods.ReInitializeDSMCustomScan = NULL;
	duckdb_scan_exec_methods.InitializeWorkerCustomScan = NULL;
	duckdb_scan_exec_methods.ShutdownCustomScan = NULL;

	duckdb_scan_exec_methods.ExplainCustomScan = Duckdb_ExplainCustomScan;
}
