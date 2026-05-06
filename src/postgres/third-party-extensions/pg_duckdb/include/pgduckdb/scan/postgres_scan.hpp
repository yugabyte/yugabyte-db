#pragma once

#include "duckdb.hpp"

#include "pgduckdb/pg/declarations.hpp"
#include "pgduckdb/utility/allocator.hpp"

#include "pgduckdb/scan/postgres_table_reader.hpp"

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

// Global State

struct PostgresScanGlobalState : public duckdb::GlobalTableFunctionState {
	explicit PostgresScanGlobalState(Snapshot, Relation rel, const duckdb::TableFunctionInitInput &input);
	~PostgresScanGlobalState();
	idx_t
	MaxThreads() const override {
		return max_threads;
	}
	void ConstructTableScanQuery(const duckdb::TableFunctionInitInput &input);
	bool RegisterLocalState();
	void UnregisterLocalState();

private:
	int ExtractQueryFilters(duckdb::TableFilter *filter, const char *column_name, duckdb::string &filters,
	                        bool is_optional_filter_parent);
	PostgresScanGlobalState(const PostgresScanGlobalState &) = delete;
	PostgresScanGlobalState &operator=(const PostgresScanGlobalState &) = delete;

public:
	Snapshot snapshot;
	Relation rel;
	TupleDesc table_tuple_desc;
	bool count_tuples_only;
	duckdb::vector<AttrNumber> output_columns;
	std::atomic<std::uint32_t> total_row_count;
	std::atomic<std::int32_t> registered_local_states;
	std::ostringstream scan_query;
	duckdb::shared_ptr<PostgresTableReader> table_reader_global_state;
	MemoryContext duckdb_scan_memory_ctx;
	idx_t max_threads;
};

// Local State
#define LOCAL_STATE_SLOT_BATCH_SIZE 32
struct PostgresScanLocalState : public duckdb::LocalTableFunctionState {
	PostgresScanLocalState(PostgresScanGlobalState *global_state);
	~PostgresScanLocalState() override;

	PostgresScanGlobalState *global_state;
	TupleTableSlot *slots[LOCAL_STATE_SLOT_BATCH_SIZE];
	std::vector<uint8_t> minimal_tuple_buffer[LOCAL_STATE_SLOT_BATCH_SIZE];

	size_t output_vector_size;
	bool exhausted_scan;

private:
	PostgresScanLocalState(const PostgresScanLocalState &) = delete;
	PostgresScanLocalState &operator=(const PostgresScanLocalState &) = delete;
};

// PostgresScanFunctionData

struct PostgresScanFunctionData : public duckdb::TableFunctionData {
	PostgresScanFunctionData(Relation rel, uint64_t cardinality, Snapshot snapshot);
	~PostgresScanFunctionData() override;
	duckdb::vector<duckdb::string> complex_filters;
	Relation rel;
	uint64_t cardinality;
	Snapshot snapshot;

private:
	PostgresScanFunctionData(const PostgresScanFunctionData &) = delete;
	PostgresScanFunctionData &operator=(const PostgresScanFunctionData &) = delete;
};

// PostgresScanTableFunction

struct PostgresScanTableFunction : public duckdb::TableFunction {
	PostgresScanTableFunction();

	static duckdb::unique_ptr<duckdb::GlobalTableFunctionState>
	PostgresScanInitGlobal(duckdb::ClientContext &context, duckdb::TableFunctionInitInput &input);

	static duckdb::unique_ptr<duckdb::LocalTableFunctionState>
	PostgresScanInitLocal(duckdb::ExecutionContext &context, duckdb::TableFunctionInitInput &input,
	                      duckdb::GlobalTableFunctionState *gstate);

	static void PostgresScanFunction(duckdb::ClientContext &context, duckdb::TableFunctionInput &data,
	                                 duckdb::DataChunk &output);

	static duckdb::unique_ptr<duckdb::NodeStatistics> PostgresScanCardinality(duckdb::ClientContext &context,
	                                                                          const duckdb::FunctionData *data);
	static duckdb::InsertionOrderPreservingMap<duckdb::string> ToString(duckdb::TableFunctionToStringInput &input);
};

} // namespace pgduckdb
