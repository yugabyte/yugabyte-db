//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
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

SqlProcessor::SqlProcessor(std::weak_ptr<rpc::Messenger> messenger, shared_ptr<YBClient> client,
                           shared_ptr<YBMetaDataCache> cache, SqlMetrics* sql_metrics,
                           cqlserver::CQLRpcServerEnv* cql_rpcserver_env)
    : sql_env_(messenger, client, cache, cql_rpcserver_env),
      analyzer_(&sql_env_),
      executor_(&sql_env_, sql_metrics),
      sql_metrics_(sql_metrics) {
}

SqlProcessor::~SqlProcessor() {
}

Status SqlProcessor::Parse(const string& sql_stmt, ParseTree::UniPtr* parse_tree,
                           const bool reparsed, shared_ptr<MemTracker> mem_tracker) {
  // Parse the statement and get the generated parse tree.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  RETURN_NOT_OK(parser_.Parse(sql_stmt, reparsed, mem_tracker));
  const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    sql_metrics_->time_to_parse_sql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  *parse_tree = parser_.Done();
  DCHECK(parse_tree->get() != nullptr) << "Parse tree is null";
  return Status::OK();
}

Status SqlProcessor::Analyze(const string& sql_stmt, ParseTree::UniPtr* parse_tree) {
  // Semantic analysis - traverse, error-check, and decorate the parse tree nodes with datatypes.
  const MonoTime begin_time = MonoTime::Now(MonoTime::FINE);
  const Status s = analyzer_.Analyze(sql_stmt, std::move(*parse_tree));
  const MonoTime end_time = MonoTime::Now(MonoTime::FINE);
  if (sql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    sql_metrics_->time_to_analyze_sql_query_->Increment(elapsed_time.ToMicroseconds());
    sql_metrics_->num_rounds_to_analyze_sql_->Increment(1);
  }
  *parse_tree = analyzer_.Done();
  CHECK(parse_tree->get() != nullptr) << "Parse tree is null";
  return s;
}

Status SqlProcessor::Prepare(const string& sql_stmt, ParseTree::UniPtr* parse_tree,
                             const bool reparsed, shared_ptr<MemTracker> mem_tracker) {
  RETURN_NOT_OK(Parse(sql_stmt, parse_tree, reparsed, mem_tracker));
  const Status s = Analyze(sql_stmt, parse_tree);
  if (s.IsSqlError() && GetErrorCode(s) == ErrorCode::STALE_METADATA && !reparsed) {
    *parse_tree = nullptr;
    RETURN_NOT_OK(Parse(sql_stmt, parse_tree, true /* reparsed */, mem_tracker));
    return Analyze(sql_stmt, parse_tree);
  }
  return s;
}

void SqlProcessor::ExecuteAsync(const string& sql_stmt, const ParseTree& parse_tree,
                                const StatementParameters& params, StatementExecutedCallback cb) {
  executor_.ExecuteAsync(sql_stmt, parse_tree, &params, std::move(cb));
}

void SqlProcessor::RunAsync(const string& sql_stmt, const StatementParameters& params,
                            StatementExecutedCallback cb, const bool reparsed) {
  ParseTree::UniPtr parse_tree;
  const Status s = Prepare(sql_stmt, &parse_tree, reparsed);
  if (PREDICT_FALSE(!s.ok())) {
    return cb.Run(s, nullptr /* result */);
  }
  const ParseTree* ptree = parse_tree.release();
  ExecuteAsync(sql_stmt, *ptree, params,
               Bind(&SqlProcessor::RunAsyncDone, Unretained(this), sql_stmt, Unretained(&params),
                    Owned(ptree), cb));
}

// RunAsync callback added to keep the parse tree in-scope while it is being run asynchronously.
// When called, just forward the status and result to the actual callback cb.
void SqlProcessor::RunAsyncDone(const string& sql_stmt, const StatementParameters* params,
                                const ParseTree *parse_tree, StatementExecutedCallback cb,
                                const Status& s, const ExecutedResult::SharedPtr& result) {
  if (s.IsSqlError() && GetErrorCode(s) == ErrorCode::STALE_METADATA && !parse_tree->reparsed()) {
    return RunAsync(sql_stmt, *params, cb, true /* reparsed */);
  }
  cb.Run(s, result);
}

void SqlProcessor::BeginBatch(StatementExecutedCallback cb) {
  executor_.BeginBatch(std::move(cb));
}

void SqlProcessor::ExecuteBatch(const std::string& sql_stmt, const ParseTree& parse_tree,
                                const StatementParameters& params) {
  executor_.ExecuteBatch(sql_stmt, parse_tree, &params);
}

void SqlProcessor::RunBatch(const std::string& sql_stmt, const StatementParameters& params,
                            ParseTree::UniPtr* parse_tree, bool reparsed) {
  const Status s = Prepare(sql_stmt, parse_tree, reparsed);
  if (PREDICT_FALSE(!s.ok())) {
    return executor_.StatementExecuted(s);
  }
  ExecuteBatch(sql_stmt, **parse_tree, params);
}

void SqlProcessor::ApplyBatch() {
  executor_.ApplyBatch();
}

void SqlProcessor::AbortBatch() {
  executor_.AbortBatch();
}

void SqlProcessor::SetCurrentCall(rpc::InboundCallPtr call) {
  sql_env_.SetCurrentCall(std::move(call));
}

}  // namespace sql
}  // namespace yb
