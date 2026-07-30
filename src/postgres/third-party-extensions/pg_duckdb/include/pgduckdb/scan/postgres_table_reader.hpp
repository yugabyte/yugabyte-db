#pragma once

#include "pgduckdb/pg/declarations.hpp"

#include <vector>

#include "pgduckdb/utility/cpp_only_file.hpp" // Must be last include.

namespace pgduckdb {

class PostgresTableReader {
public:
	PostgresTableReader();
	~PostgresTableReader();
	TupleTableSlot *GetNextTuple();
	void Init(const char *table_scan_query, bool count_tuples_only);
	void Cleanup();
	bool GetNextMinimalWorkerTuple(std::vector<uint8_t> &minimal_tuple_buffer);
	TupleTableSlot *InitTupleSlot();
	int
	NumWorkersLaunched() const {
		return nworkers_launched;
	}

private:
	PostgresTableReader(const PostgresTableReader &) = delete;
	PostgresTableReader &operator=(const PostgresTableReader &) = delete;

	void InitUnsafe(const char *table_scan_query, bool count_tuples_only);
	void InitRunWithParallelScan(PlannedStmt *, bool);
	void CleanupUnsafe();

	TupleTableSlot *GetNextTupleUnsafe();
	MinimalTuple GetNextWorkerTuple();
	int ParallelWorkerNumber(Cardinality cardinality);
	bool CanTableScanRunInParallel(Plan *plan);
	bool MarkPlanParallelAware(Plan *plan);

	QueryDesc *table_scan_query_desc;
	PlanState *table_scan_planstate;
	ParallelExecutorInfo *parallel_executor_info;
	void **parallel_worker_readers;
	TupleTableSlot *slot;
	int nworkers_launched;
	int nreaders;
	int next_parallel_reader;
	bool entered_parallel_mode;
	bool cleaned_up;
};

} // namespace pgduckdb
