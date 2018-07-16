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

#include "yb/yql/cql/ql/ql_processor.h"

#include <memory>

#include "yb/yql/cql/ql/statement.h"

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
    server, handler_latency_yb_cqlserver_SQLProcessor_NumRetriesToExecute,
    "Number of retries to successfully execute a SQL query", yb::MetricUnit::kOperations,
    "Number of retries to successfully execute a SQL query", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_SelectStmt,
    "Time spent processing a SELECT statement", yb::MetricUnit::kMicroseconds,
    "Time spent processing a SELECT statement", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_InsertStmt,
    "Time spent processing an INSERT statement", yb::MetricUnit::kMicroseconds,
    "Time spent processing an INSERT statement", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_UpdateStmt,
    "Time spent processing an UPDATE statement", yb::MetricUnit::kMicroseconds,
    "Time spent processing an UPDATE statement", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_DeleteStmt,
    "Time spent processing a DELETE statement", yb::MetricUnit::kMicroseconds,
    "Time spent processing a DELETE statement", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_OtherStmts,
    "Time spent processing any statement other than SELECT/INSERT/UPDATE/DELETE",
    yb::MetricUnit::kMicroseconds,
    "Time spent processing any statement other than SELECT/INSERT/UPDATE/DELETE", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_Transaction,
    "Time spent processing a transaction", yb::MetricUnit::kMicroseconds,
    "Time spent processing a transaction", 60000000LU, 2);
METRIC_DEFINE_histogram(
    server, handler_latency_yb_cqlserver_SQLProcessor_ResponseSize,
    "Size of the returned response blob (in bytes)", yb::MetricUnit::kBytes,
    "Size of the returned response blob (in bytes)", 60000000LU, 2);

namespace yb {
namespace ql {

using std::shared_ptr;
using std::string;
using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;

// Runs the StatementExecutedCallback cb and returns if the status s is not OK.
#define CB_RETURN_NOT_OK(cb, s)                 \
  do {                                          \
    ::yb::Status _s = (s);                      \
    if (PREDICT_FALSE(!_s.ok())) {              \
      (cb).Run(_s, nullptr);                    \
      return;                                   \
    }                                           \
  } while (0)

QLMetrics::QLMetrics(const scoped_refptr<yb::MetricEntity> &metric_entity) {
  time_to_parse_ql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ParseRequest.Instantiate(metric_entity);
  time_to_analyze_ql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_AnalyzeRequest.Instantiate(metric_entity);
  time_to_execute_ql_query_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ExecuteRequest.Instantiate(metric_entity);
  num_rounds_to_analyze_ql_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_NumRoundsToAnalyze.Instantiate(
          metric_entity);
  num_retries_to_execute_ql_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_NumRetriesToExecute.Instantiate(
          metric_entity);

  ql_select_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_SelectStmt.Instantiate(metric_entity);
  ql_insert_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_InsertStmt.Instantiate(metric_entity);
  ql_update_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_UpdateStmt.Instantiate(metric_entity);
  ql_delete_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_DeleteStmt.Instantiate(metric_entity);
  ql_others_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_OtherStmts.Instantiate(metric_entity);
  ql_transaction_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_Transaction.Instantiate(metric_entity);

  ql_response_size_bytes_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_ResponseSize.Instantiate(metric_entity);
}

QLProcessor::QLProcessor(std::weak_ptr<rpc::Messenger> messenger, shared_ptr<YBClient> client,
                         shared_ptr<YBMetaDataCache> cache, QLMetrics* ql_metrics,
                         const server::ClockPtr& clock,
                         TransactionManagerProvider transaction_manager_provider,
                         cqlserver::CQLRpcServerEnv* cql_rpcserver_env)
    : ql_env_(messenger, client, cache, clock, std::move(transaction_manager_provider),
              cql_rpcserver_env),
      analyzer_(&ql_env_),
      executor_(&ql_env_, ql_metrics),
      ql_metrics_(ql_metrics) {
}

QLProcessor::~QLProcessor() {
}

Status QLProcessor::Parse(const string& ql_stmt, ParseTree::UniPtr* parse_tree,
                          const bool reparsed, shared_ptr<MemTracker> mem_tracker) {
  // Parse the statement and get the generated parse tree.
  const MonoTime begin_time = MonoTime::Now();
  RETURN_NOT_OK(parser_.Parse(ql_stmt, reparsed, mem_tracker));
  const MonoTime end_time = MonoTime::Now();
  if (ql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    ql_metrics_->time_to_parse_ql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  *parse_tree = parser_.Done();
  DCHECK(parse_tree->get() != nullptr) << "Parse tree is null";
  return Status::OK();
}

Status QLProcessor::Analyze(const string& ql_stmt, ParseTree::UniPtr* parse_tree) {
  // Semantic analysis - traverse, error-check, and decorate the parse tree nodes with datatypes.
  const MonoTime begin_time = MonoTime::Now();
  const Status s = analyzer_.Analyze(ql_stmt, std::move(*parse_tree));
  const MonoTime end_time = MonoTime::Now();
  if (ql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    ql_metrics_->time_to_analyze_ql_query_->Increment(elapsed_time.ToMicroseconds());
    ql_metrics_->num_rounds_to_analyze_ql_->Increment(1);
  }
  *parse_tree = analyzer_.Done();
  CHECK(parse_tree->get() != nullptr) << "Parse tree is null";
  return s;
}

Status QLProcessor::Prepare(const string& ql_stmt, ParseTree::UniPtr* parse_tree,
                            const bool reparsed, shared_ptr<MemTracker> mem_tracker) {
  RETURN_NOT_OK(Parse(ql_stmt, parse_tree, reparsed, mem_tracker));
  const Status s = Analyze(ql_stmt, parse_tree);
  if (s.IsQLError() && GetErrorCode(s) == ErrorCode::STALE_METADATA && !reparsed) {
    *parse_tree = nullptr;
    RETURN_NOT_OK(Parse(ql_stmt, parse_tree, true /* reparsed */, mem_tracker));
    return Analyze(ql_stmt, parse_tree);
  }
  return s;
}

void QLProcessor::ExecuteAsync(const string& ql_stmt, const ParseTree& parse_tree,
                               const StatementParameters& params, StatementExecutedCallback cb) {
  executor_.ExecuteAsync(ql_stmt, parse_tree, &params, std::move(cb));
}

void QLProcessor::RunAsync(const string& ql_stmt, const StatementParameters& params,
                           StatementExecutedCallback cb, const bool reparsed) {
  ParseTree::UniPtr parse_tree;
  const Status s = Prepare(ql_stmt, &parse_tree, reparsed);
  if (PREDICT_FALSE(!s.ok())) {
    return cb.Run(s, nullptr /* result */);
  }
  const ParseTree* ptree = parse_tree.release();
  // Do not make a copy of ql_stmt when binding to the RunAsyncDone callback because when error
  // occurs due to stale matadata, the statement needs to be reexecuted. We should pass the original
  // ql_stmt reference which is guaranteed to still be alive after the statement is reexecuted.
  ExecuteAsync(ql_stmt, *ptree, params,
               Bind(&QLProcessor::RunAsyncDone, Unretained(this), ConstRef(ql_stmt),
                    Unretained(&params), Owned(ptree), cb));
}

// RunAsync callback added to keep the parse tree in-scope while it is being run asynchronously.
// When called, just forward the status and result to the actual callback cb.
void QLProcessor::RunAsyncDone(const string& ql_stmt, const StatementParameters* params,
                               const ParseTree *parse_tree, StatementExecutedCallback cb,
                               const Status& s, const ExecutedResult::SharedPtr& result) {
  if (s.IsQLError() && GetErrorCode(s) == ErrorCode::STALE_METADATA && !parse_tree->reparsed()) {
    return RunAsync(ql_stmt, *params, cb, true /* reparsed */);
  }
  cb.Run(s, result);
}

void QLProcessor::BeginBatch(StatementExecutedCallback cb) {
  executor_.BeginBatch(std::move(cb));
}

void QLProcessor::ExecuteBatch(const std::string& ql_stmt, const ParseTree& parse_tree,
                               const StatementParameters& params) {
  executor_.ExecuteBatch(ql_stmt, parse_tree, &params);
}

void QLProcessor::RunBatch(const std::string& ql_stmt, const StatementParameters& params,
                           ParseTree::UniPtr* parse_tree, bool reparsed) {
  const Status s = Prepare(ql_stmt, parse_tree, reparsed);
  if (PREDICT_FALSE(!s.ok())) {
    return executor_.StatementExecuted(s);
  }
  ExecuteBatch(ql_stmt, **parse_tree, params);
}

void QLProcessor::ApplyBatch() {
  executor_.ApplyBatch();
}

void QLProcessor::AbortBatch() {
  executor_.AbortBatch();
}

void QLProcessor::SetCurrentCall(rpc::InboundCallPtr call) {
  ql_env_.SetCurrentCall(std::move(call));
}

}  // namespace ql
}  // namespace yb
