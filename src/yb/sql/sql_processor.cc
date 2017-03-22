//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/sql_processor.h"

#include "yb/sql/statement.h"

METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ParseRequest,
    "Time spent parsing the SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent parsing the SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_AnalyseRequest,
    "Time spent to analyse the parsed SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent to analyse the parsed SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest,
    "Time spent executing the parsed SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent executing the parsed SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyse,
    "Number of rounds to successfully parse a SQL query", yb::MetricUnit::kOperations,
    "Number of rounds to successfully parse a SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ResponseSize,
    "Size of the returned response blob (in bytes)", yb::MetricUnit::kBytes,
    "Size of the returned response blob (in bytes)", 60000000LU, 2);

namespace yb {
namespace sql {

using std::shared_ptr;
using std::string;
using client::YBClient;
using client::YBSession;
using client::YBTableCache;

SqlMetrics::SqlMetrics(const scoped_refptr<yb::MetricEntity> &metric_entity) {
  time_to_parse_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ParseRequest.Instantiate(metric_entity);
  time_to_analyse_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_AnalyseRequest.Instantiate(metric_entity);
  time_to_execute_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest.Instantiate(metric_entity);
  num_rounds_to_analyse_sql_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyse.Instantiate(
          metric_entity);
  sql_response_size_bytes_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ResponseSize.Instantiate(metric_entity);
}

SqlProcessor::SqlProcessor(
    shared_ptr<YBClient> client, shared_ptr<YBTableCache> cache, SqlMetrics* sql_metrics)
    : parser_(new Parser()),
      analyzer_(new Analyzer()),
      executor_(new Executor()),
      sql_env_(new SqlEnv(client, cache)),
      sql_metrics_(sql_metrics),
      is_used_(false) {
}

SqlProcessor::~SqlProcessor() {
}

CHECKED_STATUS SqlProcessor::Parse(const string& sql_stmt,
                                   ParseTree::UniPtr* parse_tree,
                                   shared_ptr<MemTracker> mem_tracker) {
  // Parse the statement and get the generated parse tree.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  RETURN_NOT_OK(parser_->Parse(sql_stmt, mem_tracker));
  const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    sql_metrics_->time_to_parse_sql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  *parse_tree = parser_->Done();
  DCHECK(parse_tree->get() != nullptr) << "Parse tree is null";
  return Status::OK();
}

CHECKED_STATUS SqlProcessor::Analyze(const string& sql_stmt,
                                     ParseTree::UniPtr* parse_tree,
                                     bool refresh_cache) {
  Status s = Status::OK();

  // Semantic analysis - traverse, error-check, and decorate the parse tree nodes with datatypes.
  // In case of error and re-analysis with new table descriptor is needed, retry at most once.
  for (int i = 0; i < 2; i++) {
    const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
    s = analyzer_->Analyze(sql_stmt, move(*parse_tree), sql_env_.get(), refresh_cache);
    const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
    if (sql_metrics_ != nullptr) {
      const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
      sql_metrics_->time_to_parse_sql_query_->Increment(elapsed_time.ToMicroseconds());
      sql_metrics_->num_rounds_to_analyse_sql_->Increment(1);
    }

    const ErrorCode sem_errcode = analyzer_->error_code();
    const bool cache_used = analyzer_->cache_used();
    *parse_tree = analyzer_->Done();
    CHECK(parse_tree->get() != nullptr) << "Parse tree is null";

    // If no error occurs or the table is not found or no cache is used, no need to retry.
    if (sem_errcode == ErrorCode::SUCCESS ||
        sem_errcode == ErrorCode::TABLE_NOT_FOUND ||
        !cache_used) {
      break;
    }

    // Otherwise, the error can be due to the cached table descriptor being stale. In that case,
    // reload the table descriptor and analyze the statement again.
    refresh_cache = true;
  }

  return s;
}

CHECKED_STATUS SqlProcessor::Execute(const string& sql_stmt,
                                     const ParseTree& parse_tree,
                                     const StatementParameters& params,
                                     bool *new_analysis_needed,
                                     ExecuteResult::UniPtr *result) {
  // Code execution.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  Status s = executor_->Execute(sql_stmt, parse_tree, params, sql_env_.get(), result);
  const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    sql_metrics_->time_to_parse_sql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  // If the failure occurs because of stale metadata cache, the parse tree needs to be re-analyzed
  // with new metadata. Symptoms of stale metadata can be TABLET_NOT_FOUND when the tserver fails
  // to execute the YBSqlOp because the tablet is not found (ENG-945), or WRONG_METADATA_VERSION
  // when the schema version the tablet holds is different from the one used by the semantic
  // analyzer.
  *new_analysis_needed = (executor_->error_code() == ErrorCode::TABLET_NOT_FOUND ||
                          executor_->error_code() == ErrorCode::WRONG_METADATA_VERSION);
  executor_->Done();
  return s;
}

CHECKED_STATUS SqlProcessor::Run(const string& sql_stmt,
                                 const StatementParameters& params,
                                 ExecuteResult::UniPtr *result) {
  return Statement(sql_env_->CurrentKeyspace(), sql_stmt).Run(this, params, result);
}

}  // namespace sql
}  // namespace yb
