//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/sql_processor.h"

#include <memory>

#include "yb/sql/statement.h"

METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ParseRequest,
    "Time spent parsing the SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent parsing the SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_AnalyzeRequest,
    "Time spent to analyze the parsed SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent to analyze the parsed SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest,
    "Time spent executing the parsed SQL query", yb::MetricUnit::kMicroseconds,
    "Time spent executing the parsed SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyze,
    "Number of rounds to successfully parse a SQL query", yb::MetricUnit::kOperations,
    "Number of rounds to successfully parse a SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_SelectStmt,
    "Time spent processing a Select stmt", yb::MetricUnit::kMicroseconds,
    "Time spent processing a Select stmt", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_InsertStmt,
    "Time spent processing a Insert stmt", yb::MetricUnit::kMicroseconds,
    "Time spent processing a Insert stmt", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_UpdateStmt,
    "Time spent processing a Update stmt", yb::MetricUnit::kMicroseconds,
    "Time spent processing a Update stmt", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_DeleteStmt,
    "Time spent processing a Delete stmt", yb::MetricUnit::kMicroseconds,
    "Time spent processing a Delete stmt", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_OtherStmts,
    "Time spent processing any stmt other than Select/Insert/Update/Delete",
    yb::MetricUnit::kMicroseconds,
    "Time spent processing any stmt other than Select/Insert/Update/Delete", 60000000LU, 2);
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
  time_to_analyze_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_AnalyzeRequest.Instantiate(metric_entity);
  time_to_execute_sql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest.Instantiate(metric_entity);
  num_rounds_to_analyze_sql_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyze.Instantiate(
          metric_entity);

  sql_select_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_SelectStmt.Instantiate(metric_entity);
  sql_insert_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_InsertStmt.Instantiate(metric_entity);
  sql_update_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_UpdateStmt.Instantiate(metric_entity);
  sql_delete_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_DeleteStmt.Instantiate(metric_entity);
  sql_others_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_OtherStmts.Instantiate(metric_entity);

  sql_response_size_bytes_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ResponseSize.Instantiate(metric_entity);
}

SqlProcessor::SqlProcessor(
    std::weak_ptr<rpc::Messenger> messenger, shared_ptr<YBClient> client,
    shared_ptr<YBTableCache> cache, SqlMetrics* sql_metrics)
    : parser_(new Parser()),
      analyzer_(new Analyzer()),
      executor_(new Executor(sql_metrics)),
      sql_env_(new SqlEnv(messenger, client, cache)),
      sql_metrics_(sql_metrics),
      is_used_(false) {}

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
      sql_metrics_->time_to_analyze_sql_query_->Increment(elapsed_time.ToMicroseconds());
      sql_metrics_->num_rounds_to_analyze_sql_->Increment(1);
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

void SqlProcessor::ExecuteAsync(const string& sql_stmt,
                                const ParseTree& parse_tree,
                                const StatementParameters& params,
                                Callback<void(bool new_analysis_needed, const Status &s,
                                              ExecutedResult::SharedPtr result)> cb) {
  sql_env_->Reset();
  // Code execution.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  executor_->ExecuteAsync(sql_stmt, parse_tree, params, sql_env_.get(),
                          Bind(&SqlProcessor::ProcessExecuteResponse, Unretained(this),
                               begin_time, cb));
}

void SqlProcessor::ProcessExecuteResponse(const MonoTime &begin_time,
                                          Callback<void(bool, const Status &s,
                                                        ExecutedResult::SharedPtr result)> cb,
                                          const Status &s,
                                          ExecutedResult::SharedPtr result) {
  const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    sql_metrics_->time_to_execute_sql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  // If the failure occurs because of stale metadata cache, the parse tree needs to be re-analyzed
  // with new metadata. Symptoms of stale metadata can be TABLET_NOT_FOUND when the tserver fails
  // to execute the YBSqlOp because the tablet is not found (ENG-945), or WRONG_METADATA_VERSION
  // when the schema version the tablet holds is different from the one used by the semantic
  // analyzer.
  bool new_analysis_needed =
      (executor_->error_code() == ErrorCode::TABLET_NOT_FOUND ||
       executor_->error_code() == ErrorCode::WRONG_METADATA_VERSION);
  executor_->Done();
  cb.Run(new_analysis_needed, s, result);
}

// RunAsync callback added to keep the Statement object (first parameter) in-scope while it is
// being run asynchronously. WHen called, just forward the status and result to the actual
// callback cb.
void RunAsyncDone(
    shared_ptr<Statement> stmt, StatementExecutedCallback cb, const Status& s,
    ExecutedResult::SharedPtr result) {
  cb.Run(s, result);
}

void SqlProcessor::RunAsync(
    const string& sql_stmt, const StatementParameters& params, StatementExecutedCallback cb) {
  sql_env_->Reset();
  shared_ptr<Statement> stmt = std::make_shared<Statement>(sql_env_->CurrentKeyspace(), sql_stmt);
  stmt->RunAsync(this, params, Bind(&RunAsyncDone, stmt, cb));
}

void SqlProcessor::SetCurrentCall(rpc::InboundCallPtr call) {
  sql_env_->SetCurrentCall(std::move(call));
}

}  // namespace sql
}  // namespace yb
