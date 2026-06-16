#include <duckdb/common/types.hpp>
#include <duckdb/planner/filter/optional_filter.hpp>
#include <duckdb/planner/filter/expression_filter.hpp>
#include <duckdb/planner/expression/bound_comparison_expression.hpp>
#include <duckdb/planner/expression/bound_constant_expression.hpp>
#include <duckdb/planner/expression/bound_function_expression.hpp>
#include <duckdb/planner/expression/bound_between_expression.hpp>
#include <duckdb/planner/expression/bound_conjunction_expression.hpp>
#include <duckdb/planner/expression/bound_operator_expression.hpp>

#include "pgduckdb/scan/postgres_scan.hpp"
#include "pgduckdb/scan/postgres_table_reader.hpp"
#include "pgduckdb/pgduckdb_types.hpp"
#include "pgduckdb/pgduckdb_utils.hpp"
#include "pgduckdb/pg/memory.hpp"
#include "pgduckdb/pg/relations.hpp"
#include "pgduckdb/pgduckdb_guc.hpp"

#include "pgduckdb/pgduckdb_process_lock.hpp"
#include "pgduckdb/logger.hpp"

#include <numeric> // std::accumulate
#include <optional>

namespace pgduckdb {

//
// PostgresScanGlobalState
//

namespace {

inline std::optional<duckdb::string>
UnsupportedExpression(const char *reason, const duckdb::Expression &expr) {
	pd_log(DEBUG1, "Unsupported %s: %s", reason, expr.ToString().c_str());
	return std::nullopt;
}

std::optional<duckdb::string> ExpressionToString(const duckdb::Expression &expr, const duckdb::string &column_name);

duckdb::string
FilterJoin(duckdb::vector<duckdb::string> &filters, duckdb::string &&delimiter) {
	return std::accumulate(filters.begin() + 1, filters.end(), filters[0],
	                       [&delimiter](duckdb::string l, duckdb::string r) { return l + delimiter + r; });
}

std::optional<duckdb::string>
FuncArgToLikeString(const duckdb::string &func_name, const duckdb::Expression &expr) {
	if (expr.type != duckdb::ExpressionType::VALUE_CONSTANT) {
		// We only support literal VARCHARs for the needle argument, because we
		// need to append the '%' wildcard for postgres to understand it as a
		// LIKE pattern.
		return std::nullopt;
	}

	auto &value = expr.Cast<duckdb::BoundConstantExpression>().value;
	if (value.IsNull()) {
		return "NULL";
	} else if (value.type().id() != duckdb::LogicalTypeId::VARCHAR) {
		return std::nullopt; // Unsupported type for needle
	}

	auto str_val = duckdb::StringUtil::Replace(value.ToString(), "'", "''");
	str_val = duckdb::StringUtil::Replace(str_val, "\\", "\\\\");
	str_val = duckdb::StringUtil::Replace(str_val, "%", "\\%");
	str_val = duckdb::StringUtil::Replace(str_val, "_", "\\_");
	if (func_name == "contains") {
		return "'%" + str_val + "%'";
	} else if (func_name == "suffix") {
		return "'%" + str_val + "'";
	} else if (func_name == "prefix") {
		return "'" + str_val + "%'";
	} else {
		throw duckdb::Exception(duckdb::ExceptionType::EXECUTOR, "Unsupported function: '" + func_name + "'");
	}
}

std::optional<duckdb::string>
FuncToLikeString(const duckdb::string &func_name, const duckdb::BoundFunctionExpression &func_expr,
                 const duckdb::string &column_name) {
	if (func_expr.children.size() < 2 || func_expr.children.size() > 3) {
		return UnsupportedExpression("function arg count", func_expr);
	}

	auto &haystack = *func_expr.children[0];
	auto &needle = *func_expr.children[1];

	if (haystack.return_type != duckdb::LogicalTypeId::VARCHAR) {
		return UnsupportedExpression("type for haystack", haystack);
	}

	auto haystack_str = ExpressionToString(haystack, column_name);
	if (!haystack_str) {
		return UnsupportedExpression("haystack expression", haystack);
	}

	auto needle_str = func_name == "like_escape" || func_name == "ilike_escape"
	                      ? ExpressionToString(needle, column_name)
	                      : FuncArgToLikeString(func_name, needle);
	if (!needle_str) {
		return UnsupportedExpression("needle expression", needle);
	}

	std::ostringstream oss;
	oss << *haystack_str;
	if (func_name == "ilike_escape") {
		oss << " ILIKE ";
	} else {
		oss << " LIKE ";
	}

	oss << *needle_str;

	if (func_expr.children.size() == 3) {
		// If there's a third argument, it should be the escape character
		auto &escape_char = *func_expr.children[2];
		auto escape_str = ExpressionToString(escape_char, column_name);
		if (!escape_str) {
			return UnsupportedExpression("escape character expression", escape_char);
		} else if (*escape_str != "'\\'") {
			oss << " ESCAPE " << *escape_str;
		}
	}
	return oss.str(); // Return the complete LIKE expression as a string
}

std::optional<duckdb::string>
ExpressionToString(const duckdb::Expression &expr, const duckdb::string &column_name) {
	switch (expr.type) {
	case duckdb::ExpressionType::OPERATOR_NOT: {
		auto &not_expr = expr.Cast<duckdb::BoundOperatorExpression>();
		auto arg_str = ExpressionToString(*not_expr.children[0], column_name);
		if (!arg_str) {
			return UnsupportedExpression("child expression in", expr);
		}
		return "NOT (" + *arg_str + ")";
	}

	case duckdb::ExpressionType::OPERATOR_IS_NULL:
	case duckdb::ExpressionType::OPERATOR_IS_NOT_NULL: {
		auto &is_null_expr = expr.Cast<duckdb::BoundOperatorExpression>();
		auto arg_str = ExpressionToString(*is_null_expr.children[0], column_name);
		if (!arg_str) {
			return UnsupportedExpression("child expression in", expr);
		}
		auto operator_str = (expr.type == duckdb::ExpressionType::OPERATOR_IS_NULL ? "IS NULL" : "IS NOT NULL");
		return "(" + *arg_str + ") " + operator_str;
	}

	case duckdb::ExpressionType::COMPARE_EQUAL:
	case duckdb::ExpressionType::COMPARE_NOTEQUAL:
	case duckdb::ExpressionType::COMPARE_LESSTHAN:
	case duckdb::ExpressionType::COMPARE_GREATERTHAN:
	case duckdb::ExpressionType::COMPARE_LESSTHANOREQUALTO:
	case duckdb::ExpressionType::COMPARE_GREATERTHANOREQUALTO:
	case duckdb::ExpressionType::COMPARE_DISTINCT_FROM:
	case duckdb::ExpressionType::COMPARE_NOT_DISTINCT_FROM: {
		auto &comp_expr = expr.Cast<duckdb::BoundComparisonExpression>();
		auto arg0_str = ExpressionToString(*comp_expr.left, column_name);
		auto arg1_str = ExpressionToString(*comp_expr.right, column_name);
		if (!arg0_str || !arg1_str) {
			return UnsupportedExpression("child expression in", expr);
		}
		return "(" + *arg0_str + " " + duckdb::ExpressionTypeToOperator(expr.type) + " " + *arg1_str + ")";
	}

	case duckdb::ExpressionType::COMPARE_BETWEEN: {
		auto &between_expr = expr.Cast<duckdb::BoundBetweenExpression>();
		auto input_str = ExpressionToString(*between_expr.input, column_name);
		auto lower_str = ExpressionToString(*between_expr.lower, column_name);
		auto upper_str = ExpressionToString(*between_expr.upper, column_name);
		if (!input_str || !lower_str || !upper_str) {
			return UnsupportedExpression("child expression in", expr);
		}

		/*
		 * Would be nice to use the existing on BoundBetweenExpression for
		 * this, but those are not const. Once following PR is marged and
		 * released, feel free to use it:
		 * https://github.com/duckdb/duckdb/pull/17773
		 */
		auto lower_comp = between_expr.lower_inclusive ? duckdb::ExpressionType::COMPARE_GREATERTHANOREQUALTO
		                                               : duckdb::ExpressionType::COMPARE_GREATERTHAN;
		auto upper_comp = between_expr.upper_inclusive ? duckdb::ExpressionType::COMPARE_LESSTHANOREQUALTO
		                                               : duckdb::ExpressionType::COMPARE_LESSTHAN;

		return "((" + *input_str + " " + duckdb::ExpressionTypeToOperator(lower_comp) + " " + *lower_str + ") AND (" +
		       *input_str + " " + duckdb::ExpressionTypeToOperator(upper_comp) + " " + *upper_str + "))";
	}

		// XXX: IN and NOT IN are not listed here on purpose. DuckDB transforms a 2
		// element in IN query to a hash join. Once we find a query that actually
		// keeps the IN expression, we can implement IN/NOT IN support here. Maybe
		// that happens when we start adding postgres indexes to the duckdb
		// metadata.

	case duckdb::ExpressionType::CONJUNCTION_AND:
	case duckdb::ExpressionType::CONJUNCTION_OR: {
		auto &comp_expr = expr.Cast<duckdb::BoundConjunctionExpression>();
		std::string query_filters;

		for (auto &child : comp_expr.children) {
			auto child_str = ExpressionToString(*child, column_name);
			if (!child_str) {
				return UnsupportedExpression("child expression in", expr);
			}
			if (!query_filters.empty()) {
				query_filters += " " + duckdb::ExpressionTypeToOperator(expr.type) + " ";
			}
			query_filters += *child_str;
		}
		return "(" + query_filters + ")";
	}

	case duckdb::ExpressionType::BOUND_FUNCTION: {
		auto &func_expr = expr.Cast<duckdb::BoundFunctionExpression>();
		const auto &func_name = func_expr.function.name;
		if (func_name == "contains" || func_name == "suffix" || func_name == "prefix" || func_name == "like_escape" ||
		    func_name == "ilike_escape") {
			return FuncToLikeString(func_name, func_expr, column_name);
		}

		if (func_name == "lower" || func_name == "upper") {
			// For lower and upper functions, we can just return the column name
			// with the function applied, as Postgres will handle it correctly.
			auto child_str = ExpressionToString(*func_expr.children[0], column_name);
			if (!child_str) {
				return UnsupportedExpression("child expression in", expr);
			}
			return func_name + "(" + *child_str + ")";
		}

		if ((func_name == "~~" || func_name == "!~~") && func_expr.children.size() == 2 && func_expr.is_operator) {
			auto &haystack = *func_expr.children[0];
			if (haystack.return_type != duckdb::LogicalTypeId::VARCHAR) {
				return UnsupportedExpression("type for haystack", expr);
			}
			auto child_str0 = ExpressionToString(*func_expr.children[0], column_name);
			auto child_str1 = ExpressionToString(*func_expr.children[1], column_name);
			if (!child_str0 || !child_str1) {
				return UnsupportedExpression("child expression in", expr);
			}
			return child_str0->append(" OPERATOR(pg_catalog." + func_name + ") ").append(*child_str1);
		}

		return UnsupportedExpression("function", expr);
	}

	case duckdb::ExpressionType::BOUND_REF:
	case duckdb::ExpressionType::BOUND_COLUMN_REF:
		return column_name;

	case duckdb::ExpressionType::VALUE_CONSTANT: {
		auto &value = expr.Cast<duckdb::BoundConstantExpression>().value;
		if (value.IsNull()) {
			return "NULL";
		} else if (value.type().id() == duckdb::LogicalTypeId::VARCHAR) {
			return value.ToSQLString();
		} else {
			// For now we only support VARCHAR constants, we could probably
			// easily support more types. Currently we only to push down LIKE
			// expressions and lower/upper calls though. So there's no need to
			// complicate things for now.
			return UnsupportedExpression("constant expression", expr);
		}
	}

	default:
		return UnsupportedExpression("expression type", expr);
	}
}

} // namespace

int
PostgresScanGlobalState::ExtractQueryFilters(duckdb::TableFilter *filter, const char *column_name,
                                             duckdb::string &query_filters, bool is_inside_optional_filter) {
	switch (filter->filter_type) {
	case duckdb::TableFilterType::CONSTANT_COMPARISON:
	case duckdb::TableFilterType::IS_NULL:
	case duckdb::TableFilterType::IS_NOT_NULL:
	case duckdb::TableFilterType::IN_FILTER: {
		query_filters += filter->ToString(column_name).c_str();
		return 1;
	}
	case duckdb::TableFilterType::CONJUNCTION_OR:
	case duckdb::TableFilterType::CONJUNCTION_AND: {
		auto conjuction_filter = reinterpret_cast<duckdb::ConjunctionFilter *>(filter);
		duckdb::vector<std::string> conjuction_child_filters;
		for (idx_t i = 0; i < conjuction_filter->child_filters.size(); i++) {
			std::string child_filter;
			if (ExtractQueryFilters(conjuction_filter->child_filters[i].get(), column_name, child_filter,
			                        is_inside_optional_filter)) {
				conjuction_child_filters.emplace_back(child_filter);
			}
		}
		duckdb::string conjuction_delimiter =
		    filter->filter_type == duckdb::TableFilterType::CONJUNCTION_OR ? " OR " : " AND ";
		if (conjuction_child_filters.size()) {
			query_filters += "(" + FilterJoin(conjuction_child_filters, std::move(conjuction_delimiter)) + ")";
		}
		return conjuction_child_filters.size();
	}
	case duckdb::TableFilterType::OPTIONAL_FILTER: {
		auto optional_filter = reinterpret_cast<duckdb::OptionalFilter *>(filter);
		return ExtractQueryFilters(optional_filter->child_filter.get(), column_name, query_filters, true);
	}
	case duckdb::TableFilterType::EXPRESSION_FILTER: {
		auto &expression_filter = filter->Cast<duckdb::ExpressionFilter>();
		query_filters += *ExpressionToString(*expression_filter.expr, column_name);
		return 1;
	}
	/* DYNAMIC_FILTER is push down filter from topN execution. STRUCT_EXTRACT is
	 * only received if struct_extract function is used. Default will catch all
	 * filter that could be added in future in DuckDB.
	 */
	case duckdb::TableFilterType::DYNAMIC_FILTER:
	case duckdb::TableFilterType::STRUCT_EXTRACT:
	default: {
		if (is_inside_optional_filter) {
			pd_log(DEBUG1, "(DuckDB/ExtractQueryFilters) Unsupported optional filter: %s",
			       filter->ToString(column_name).c_str());
			return 0;
		}
		throw duckdb::Exception(duckdb::ExceptionType::EXECUTOR,
		                        "Invalid Filter Type: " + filter->ToString(column_name));
	}
	}
}

void
PostgresScanGlobalState::ConstructTableScanQuery(const duckdb::TableFunctionInitInput &input) {
	/* SELECT COUNT(*) FROM */
	if (input.column_ids.size() == 1 && input.column_ids[0] == UINT64_MAX) {
		scan_query << "SELECT COUNT(*) FROM " << pgduckdb::GenerateQualifiedRelationName(rel);
		count_tuples_only = true;
		return;
	}
	/*
	 * We need to read columns from the Postgres tuple in column order, but for
	 * outputting them we care about the DuckDB order. A map automatically
	 * orders them based on key, which in this case is the Postgres column
	 * order
	 */
	duckdb::map<AttrNumber, duckdb::idx_t> pg_column_order;
	duckdb::idx_t scan_index = 0;
	for (const auto &pg_column : input.column_ids) {
		/* Postgres AttrNumbers are 1-based */
		pg_column_order[pg_column + 1] = scan_index++;
	}

	auto table_filters = input.filters.get();

	std::vector<duckdb::pair<AttrNumber, duckdb::idx_t>> columns_to_scan;
	std::vector<duckdb::TableFilter *> column_filters(input.column_ids.size(), 0);

	for (auto const &[att_num, duckdb_scanned_index] : pg_column_order) {
		columns_to_scan.emplace_back(att_num, duckdb_scanned_index);

		if (!table_filters) {
			continue;
		}

		auto column_filter_it = table_filters->filters.find(duckdb_scanned_index);
		if (column_filter_it != table_filters->filters.end()) {
			column_filters[duckdb_scanned_index] = column_filter_it->second.get();
		}
	}

	/* We need to check do we consider projection_ids or column_ids list to be used
	 * for writing to output vector. Projection ids list will be used when
	 * columns that are used for query filtering are not used afterwards; otherwise
	 * column ids list will be used and all read tuple columns need to passed
	 * to upper layers of query execution.
	 */
	if (input.CanRemoveFilterColumns()) {
		for (const auto &projection_id : input.projection_ids) {
			output_columns.emplace_back(input.column_ids[projection_id] + 1);
		}
	} else {
		for (const auto &column_id : input.column_ids) {
			output_columns.emplace_back(column_id + 1);
		}
	}

	scan_query << "SELECT ";

	bool first = true;
	for (auto const &attr_num : output_columns) {
		if (!first) {
			scan_query << ", ";
		}
		first = false;
		auto attr = GetAttr(table_tuple_desc, attr_num - 1);
		scan_query << pgduckdb::QuoteIdentifier(GetAttName(attr));
	}

	scan_query << " FROM " << GenerateQualifiedRelationName(rel);

	duckdb::vector<duckdb::string> query_filters;
	for (auto const &[attr_num, duckdb_scanned_index] : columns_to_scan) {
		auto filter = column_filters[duckdb_scanned_index];
		if (!filter) {
			continue;
		}
		duckdb::string column_query_filters;
		auto attr = GetAttr(table_tuple_desc, attr_num - 1);
		auto col = pgduckdb::QuoteIdentifier(GetAttName(attr));
		if (ExtractQueryFilters(filter, col, column_query_filters, false)) {
			query_filters.emplace_back(column_query_filters);
		}
	}

	if (query_filters.size()) {
		scan_query << " WHERE ";
		scan_query << FilterJoin(query_filters, " AND ");
	}
}

PostgresScanGlobalState::PostgresScanGlobalState(Snapshot _snapshot, Relation _rel,
                                                 const duckdb::TableFunctionInitInput &input)
    : snapshot(_snapshot), rel(_rel), table_tuple_desc(RelationGetDescr(rel)), count_tuples_only(false),
      output_columns(), total_row_count(0), registered_local_states(0), scan_query(),
      table_reader_global_state(nullptr), duckdb_scan_memory_ctx(nullptr), max_threads(1) {
	ConstructTableScanQuery(input);
	table_reader_global_state = duckdb::make_shared_ptr<PostgresTableReader>();
	table_reader_global_state->Init(scan_query.str().c_str(), count_tuples_only);
	// Dedicated Postgres memory context for temporary allocations during type conversion in scans.
	duckdb_scan_memory_ctx = pg::MemoryContextCreate(CurrentMemoryContext, "DuckdbScanContext");

	// Parallelism in scanning has two layers:
	//   1. The Postgres table_reader may launch parallel worker processes to scan the table.
	//   2. DuckDB can use multiple threads (controlled by max_threads) to consume results from the table_reader.
	//
	// We restrict DuckDB to a single thread (max_threads = 1) in the following cases:
	//   - The scan is a count-only query (count_tuples_only), as result processing typically isn't the performance
	//     bottleneck.
	//   - The table_reader does not launch any parallel Postgres workers, indicating a small scan that executes in the
	//     current process.
	if (table_reader_global_state->NumWorkersLaunched() > 0 && !count_tuples_only) {
		max_threads = duckdb_threads_for_postgres_scan;
	}

	pd_log(DEBUG1, "(DuckDB/PostgresSeqScanGlobalState) Running %" PRIu64 " threads: '%s'", (uint64_t)MaxThreads(),
	       scan_query.str().c_str());
}

bool
PostgresScanGlobalState::RegisterLocalState() {
	if (registered_local_states < 0) {
		return false;
	}
	registered_local_states++;
	return true;
}

void
PostgresScanGlobalState::UnregisterLocalState() {
	std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
	registered_local_states--;
	// Cleanup up the table reader global state when all registered local states are gone.
	// And set the flag to negative to indicate no more local states are allowed to be registered.
	if (registered_local_states == 0) {
		registered_local_states = -1;
		table_reader_global_state->Cleanup();
	}
}

PostgresScanGlobalState::~PostgresScanGlobalState() {
}

//
// PostgresScanLocalState
//

PostgresScanLocalState::PostgresScanLocalState(PostgresScanGlobalState *_global_state)
    : global_state(_global_state), output_vector_size(0), exhausted_scan(false) {
	std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
	bool registered = global_state->RegisterLocalState();
	if (!registered || global_state->MaxThreads() <= 1) {
		return;
	}
	for (int i = 0; i < LOCAL_STATE_SLOT_BATCH_SIZE; i++) {
		slots[i] = global_state->table_reader_global_state->InitTupleSlot();
	}
}

PostgresScanLocalState::~PostgresScanLocalState() {
}

//
// PostgresSeqScanFunctionData
//

PostgresScanFunctionData::PostgresScanFunctionData(Relation _rel, uint64_t _cardinality, Snapshot _snapshot)
    : complex_filters(), rel(_rel), cardinality(_cardinality), snapshot(_snapshot) {
}

PostgresScanFunctionData::~PostgresScanFunctionData() {
}

//
// PostgresScanFunction
//

static bool
PostgresScanPushdownExpression(duckdb::ClientContext &, const duckdb::LogicalGet &, duckdb::Expression &expr) {
	return ExpressionToString(expr, "dummy") != std::nullopt;
}

PostgresScanTableFunction::PostgresScanTableFunction()
    : TableFunction("pgduckdb_postgres_scan", {}, PostgresScanFunction, nullptr, PostgresScanInitGlobal,
                    PostgresScanInitLocal) {
	named_parameters["cardinality"] = duckdb::LogicalType::UBIGINT;
	named_parameters["relid"] = duckdb::LogicalType::UINTEGER;
	named_parameters["snapshot"] = duckdb::LogicalType::POINTER;
	projection_pushdown = true;
	filter_pushdown = true;
	filter_prune = true;
	cardinality = PostgresScanCardinality;
	pushdown_expression = PostgresScanPushdownExpression;
	to_string = ToString;
}

duckdb::InsertionOrderPreservingMap<duckdb::string>
PostgresScanTableFunction::ToString(duckdb::TableFunctionToStringInput &input) {
	auto &bind_data = input.bind_data->Cast<PostgresScanFunctionData>();
	duckdb::InsertionOrderPreservingMap<duckdb::string> result;
	result["Table"] = GetRelationName(bind_data.rel);
	return result;
}

duckdb::unique_ptr<duckdb::GlobalTableFunctionState>
PostgresScanTableFunction::PostgresScanInitGlobal(duckdb::ClientContext &, duckdb::TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->CastNoConst<PostgresScanFunctionData>();
	return duckdb::make_uniq<PostgresScanGlobalState>(bind_data.snapshot, bind_data.rel, input);
}

duckdb::unique_ptr<duckdb::LocalTableFunctionState>
PostgresScanTableFunction::PostgresScanInitLocal(duckdb::ExecutionContext &, duckdb::TableFunctionInitInput &,
                                                 duckdb::GlobalTableFunctionState *gstate) {
	auto global_state = reinterpret_cast<PostgresScanGlobalState *>(gstate);
	return duckdb::make_uniq<PostgresScanLocalState>(global_state);
}
void
SetOutputCardinality(duckdb::DataChunk &output, PostgresScanLocalState &local_state) {
	idx_t output_cardinality =
	    local_state.output_vector_size <= STANDARD_VECTOR_SIZE ? local_state.output_vector_size : STANDARD_VECTOR_SIZE;
	output.SetCardinality(output_cardinality);
	local_state.output_vector_size -= output_cardinality;
}

/*
 * Fetches a single tuple from the underlying PostgreSQL table and appends it to the DuckDB output chunk.
 * This function is intended for use in single-threaded scans.
 *
 * @return True if a tuple was successfully fetched and inserted; false if there are no more tuples to scan.
 */
static bool
ScanSingleTuple(duckdb::DataChunk &output, PostgresScanLocalState &local_state) {
	TupleTableSlot *slot = local_state.global_state->table_reader_global_state->GetNextTuple();
	if (pgduckdb::TupleIsNull(slot)) {
		return false;
	}

	SlotGetAllAttrs(slot);
	// This memory context is used as a scratchpad space for any allocation required to add the tuple
	// into the chunk, such as decoding jsonb columns to their json string representation. We need to
	// only use this memory context here, and not for the full loop, because GetNextTuple() needs the
	// actual tuple to survive until the next call to GetNextTuple(), to be able to do index scans.
	// Cf. issue 796 and 802
	MemoryContext old_context = pg::MemoryContextSwitchTo(local_state.global_state->duckdb_scan_memory_ctx);
	InsertTupleIntoChunk(output, local_state, slot);
	pg::MemoryContextSwitchTo(old_context);
	return true;
}

void
PostgresScanTableFunction::PostgresScanFunction(duckdb::ClientContext &, duckdb::TableFunctionInput &data,
                                                duckdb::DataChunk &output) {
	auto &local_state = data.local_state->Cast<PostgresScanLocalState>();

	/* We have exhausted table scan */
	if (local_state.exhausted_scan) {
		SetOutputCardinality(output, local_state);
		return;
	}

	local_state.output_vector_size = 0;

	D_ASSERT(STANDARD_VECTOR_SIZE % LOCAL_STATE_SLOT_BATCH_SIZE == 0);
	bool is_parallel_scan = local_state.global_state->MaxThreads() > 1;
	size_t batch_size = is_parallel_scan ? LOCAL_STATE_SLOT_BATCH_SIZE : STANDARD_VECTOR_SIZE;
	size_t num_batches = STANDARD_VECTOR_SIZE / batch_size;

	// For single-threaded scans, only one batch is processed and the global lock is acquired for each batch.
	// For parallel scans, multiple batches are processed; the global lock is held only during tuple retrieval
	// from the PostgreSQL parallel worker, allowing the rest of the processing to proceed concurrently.
	for (size_t batch_idx = 0; batch_idx < num_batches; batch_idx++) {
		size_t valid_slots = 0;
		{
			std::lock_guard<std::recursive_mutex> lock(GlobalProcessLock::GetLock());
			for (size_t i = 0; i < batch_size; i++) {
				bool ret = is_parallel_scan
				               ? local_state.global_state->table_reader_global_state->GetNextMinimalWorkerTuple(
				                     local_state.minimal_tuple_buffer[i])
				               : ScanSingleTuple(output, local_state);
				if (!ret) {
					local_state.exhausted_scan = true;
					break;
				}

				++valid_slots;
			}

			if (!is_parallel_scan) {
				pg::MemoryContextReset(local_state.global_state->duckdb_scan_memory_ctx);
				D_ASSERT(num_batches == 1);
				break;
			}
		}

		// The follow-up convertion logic is thread-safe.
		D_ASSERT(is_parallel_scan);
		for (size_t i = 0; i < valid_slots; i++) {
			MinimalTuple minmal_tuple = reinterpret_cast<MinimalTuple>(local_state.minimal_tuple_buffer[i].data());
			local_state.slots[i] = ExecStoreMinimalTupleUnsafe(minmal_tuple, local_state.slots[i], false);
			SlotGetAllAttrs(local_state.slots[i]);
		}
		InsertTuplesIntoChunk(output, local_state, local_state.slots, valid_slots);
	}

	if (local_state.exhausted_scan) {
		local_state.global_state->UnregisterLocalState();
	}
	SetOutputCardinality(output, local_state);
}

duckdb::unique_ptr<duckdb::NodeStatistics>
PostgresScanTableFunction::PostgresScanCardinality(duckdb::ClientContext &, const duckdb::FunctionData *data) {
	auto &bind_data = data->Cast<PostgresScanFunctionData>();
	return duckdb::make_uniq<duckdb::NodeStatistics>(bind_data.cardinality, bind_data.cardinality);
}

} // namespace pgduckdb
