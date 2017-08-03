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
using client::YBMetaDataCache;

// Runs the StatementExecutedCallback cb and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)    \
  do {                             \
    ::yb::Status _s = (s);         \
    if (PREDICT_FALSE(!_s.ok())) { \
      (cb).Run(_s, nullptr);       \
      return;                      \
    }                              \
  } while (0)

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
    shared_ptr<YBMetaDataCache> cache, SqlMetrics* sql_metrics,
    cqlserver::CQLRpcServerEnv* cql_rpcserver_env)
    : parser_(new Parser()),
      analyzer_(new Analyzer()),
      executor_(new Executor(sql_metrics)),
      sql_env_(new SqlEnv(messenger, client, cache, cql_rpcserver_env)),
      sql_metrics_(sql_metrics) {
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

CHECKED_STATUS SqlProcessor::Analyze(
    const string& sql_stmt, ParseTree::UniPtr* parse_tree, bool* reparse) {
  // Semantic analysis - traverse, error-check, and decorate the parse tree nodes with datatypes.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  const Status s = analyzer_->Analyze(sql_stmt, std::move(*parse_tree), sql_env_.get());
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

  // When "reparse" out-parameter is provided, the statement has not been reparsed. When a statement
  // is parsed for the first time, semantic analysis may fail because stale table metadata cache was
  // used. If that happens, clear the cache and tell the caller to reparse. The only exception is
  // when the table is not found in which case no cache is used.
  if (reparse != nullptr &&
      sem_errcode != ErrorCode::SUCCESS &&
      sem_errcode != ErrorCode::KEYSPACE_NOT_FOUND &&
      sem_errcode != ErrorCode::TABLE_NOT_FOUND &&
      sem_errcode != ErrorCode::TYPE_NOT_FOUND &&
      cache_used) {
    (*parse_tree)->ClearAnalyzedTableCache(sql_env_.get());
    (*parse_tree)->ClearAnalyzedUDTypeCache(sql_env_.get());
    *reparse = true;
    return Status::OK();
  }

  return s;
}

void SqlProcessor::ExecuteAsync(const string& sql_stmt,
                                const ParseTree& parse_tree,
                                const StatementParameters& params,
                                StatementExecutedCallback cb,
                                const bool reparsed) {
  sql_env_->Reset();
  // Code execution.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  executor_->ExecuteAsync(sql_stmt, parse_tree, params, sql_env_.get(),
                          Bind(&SqlProcessor::ExecuteAsyncDone, Unretained(this), begin_time,
                               Unretained(&parse_tree), std::move(cb), reparsed));
}

void SqlProcessor::ExecuteAsyncDone(const MonoTime &begin_time,
                                    const ParseTree *parse_tree,
                                    StatementExecutedCallback cb,
                                    const bool reparsed,
                                    const Status &s,
                                    const ExecutedResult::SharedPtr& result) {
  const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    sql_metrics_->time_to_execute_sql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  const ErrorCode exec_errcode = executor_->error_code();
  executor_->Done();

  if (!reparsed) {
    // If execution fails because the statement was analyzed with stale metadata cache, the
    // statement needs to be reparsed and re-analyzed. Symptoms of stale metadata are as listed
    // below. Expand the list in future as new cases arise.
    switch (exec_errcode) {

      // TABLET_NOT_FOUND when the tserver fails to execute the YBSqlOp because the tablet is not
      // found (ENG-945).
      case ErrorCode::TABLET_NOT_FOUND: FALLTHROUGH_INTENDED;
      // WRONG_METADATA_VERSION when the schema version the tablet holds is different from the one
      // used by the semantic analyzer.
      case ErrorCode::WRONG_METADATA_VERSION: FALLTHROUGH_INTENDED;
      // INVALID_TABLE_DEFINITION when a referenced user-defined type is not found.
      case ErrorCode::INVALID_TABLE_DEFINITION: FALLTHROUGH_INTENDED;
      // INVALID_ARGUMENTS when the column datatype is inconsistent with the supplied value in an
      // INSERT or UPDATE statement.
      case ErrorCode::INVALID_ARGUMENTS: {

        // Clear the metadata cache before invoking callback so that the statement can be reprepared
        // with new metadata.
        parse_tree->ClearAnalyzedTableCache(sql_env_.get());
        parse_tree->ClearAnalyzedUDTypeCache(sql_env_.get());

        cb.Run(STATUS(SqlError, ErrorText(ErrorCode::STALE_PREPARED_STATEMENT), Slice(),
                      static_cast<int64_t>(ErrorCode::STALE_PREPARED_STATEMENT)), result);
        return;
      }

      default:
        break;
    }
  }

  cb.Run(s, result);
}

// ReRunAsync callback added to keep the parse tree in-scope while it is being run asynchronously.
// When called, just forward the status and result to the actual callback cb.
void ReRunAsyncDone(
    ParseTree *parse_tree, StatementExecutedCallback cb, const Status& s,
    const ExecutedResult::SharedPtr& result) {
  cb.Run(s, result);
}

// RunAsync callback added to keep the parse tree in-scope while it is being run asynchronously.
// When called, just forward the status and result to the actual callback cb.
void SqlProcessor::RunAsyncDone(
    const string& sql_stmt, const StatementParameters* params, ParseTree *parse_tree,
    StatementExecutedCallback cb, const Status& s, const ExecutedResult::SharedPtr& result) {
  if (s.IsSqlError() && GetErrorCode(s) == ErrorCode::STALE_PREPARED_STATEMENT) {
    ParseTree::UniPtr new_parse_tree;
    bool reparse = false;
    CB_RETURN_NOT_OK(cb, Parse(sql_stmt, &new_parse_tree, nullptr /* mem_tracker */));
    CB_RETURN_NOT_OK(cb, Analyze(sql_stmt, &new_parse_tree, &reparse));
    const ParseTree* ptree = new_parse_tree.get();  // copy the pointer before releasing below.
    ExecuteAsync(sql_stmt, *ptree, *params,
                 Bind(&ReRunAsyncDone, Owned(new_parse_tree.release()), std::move(cb)),
                 true /* reparsed */);
    return;
  }
  cb.Run(s, result);
}

void SqlProcessor::RunAsync(
    const string& sql_stmt, const StatementParameters& params, StatementExecutedCallback cb) {
  sql_env_->Reset();
  ParseTree::UniPtr parse_tree;
  bool reparse = false;
  CB_RETURN_NOT_OK(cb, Parse(sql_stmt, &parse_tree, nullptr /* mem_tracker */));
  CB_RETURN_NOT_OK(cb, Analyze(sql_stmt, &parse_tree, &reparse));
  if (reparse) {
    CB_RETURN_NOT_OK(cb, Parse(sql_stmt, &parse_tree, nullptr /* mem_tracker */));
    CB_RETURN_NOT_OK(cb, Analyze(sql_stmt, &parse_tree));
  }
  const ParseTree* ptree = parse_tree.get();  // copy the pointer before releasing below.
  ExecuteAsync(sql_stmt, *ptree, params,
               Bind(&SqlProcessor::RunAsyncDone, Unretained(this), sql_stmt, Unretained(&params),
                    Owned(parse_tree.release()), std::move(cb)));
}

void SqlProcessor::SetCurrentCall(rpc::InboundCallPtr call) {
  sql_env_->SetCurrentCall(std::move(call));
}

}  // namespace sql
}  // namespace yb
