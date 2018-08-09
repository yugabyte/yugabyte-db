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
#include "yb/util/thread_restrictions.h"

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
    server, handler_latency_yb_cqlserver_SQLProcessor_NumFlushesToExecute,
    "Number of flushes to successfully execute a SQL query", yb::MetricUnit::kOperations,
    "Number of flushes to successfully execute a SQL query", 60000000LU, 2);
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
using client::YBMetaDataCache;

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
  num_flushes_to_execute_ql_ =
      METRIC_handler_latency_yb_cqlserver_SQLProcessor_NumFlushesToExecute.Instantiate(
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

QLProcessor::QLProcessor(shared_ptr<YBClient> client,
                         shared_ptr<YBMetaDataCache> cache, QLMetrics* ql_metrics,
                         const server::ClockPtr& clock,
                         TransactionManagerProvider transaction_manager_provider)
    : ql_env_(client, cache, clock, std::move(transaction_manager_provider)),
      analyzer_(&ql_env_),
      executor_(&ql_env_,
                [this](std::function<void()> resume_from) { RescheduleCurrentCall(resume_from); },
                ql_metrics),
      ql_metrics_(ql_metrics) {
}

QLProcessor::~QLProcessor() {
}

Status QLProcessor::Parse(const string& stmt, ParseTree::UniPtr* parse_tree,
                          const bool reparsed, const MemTrackerPtr& mem_tracker) {
  // Parse the statement and get the generated parse tree.
  const MonoTime begin_time = MonoTime::Now();
  RETURN_NOT_OK(parser_.Parse(stmt, reparsed, mem_tracker));
  const MonoTime end_time = MonoTime::Now();
  if (ql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    ql_metrics_->time_to_parse_ql_query_->Increment(elapsed_time.ToMicroseconds());
  }
  *parse_tree = parser_.Done();
  DCHECK(*parse_tree) << "Parse tree is null";
  return Status::OK();
}

Status QLProcessor::Analyze(ParseTree::UniPtr* parse_tree) {
  // Semantic analysis - traverse, error-check, and decorate the parse tree nodes with datatypes.
  const MonoTime begin_time = MonoTime::Now();
  const Status s = analyzer_.Analyze(std::move(*parse_tree));
  const MonoTime end_time = MonoTime::Now();
  if (ql_metrics_ != nullptr) {
    const MonoDelta elapsed_time = end_time.GetDeltaSince(begin_time);
    ql_metrics_->time_to_analyze_ql_query_->Increment(elapsed_time.ToMicroseconds());
    ql_metrics_->num_rounds_to_analyze_ql_->Increment(1);
  }
  *parse_tree = analyzer_.Done();
  DCHECK(*parse_tree) << "Parse tree is null";
  return s;
}

Status QLProcessor::Prepare(const string& stmt, ParseTree::UniPtr* parse_tree,
                            const bool reparsed, const MemTrackerPtr& mem_tracker) {
  RETURN_NOT_OK(Parse(stmt, parse_tree, reparsed, mem_tracker));
  const Status s = Analyze(parse_tree);
  if (s.IsQLError() && GetErrorCode(s) == ErrorCode::STALE_METADATA && !reparsed) {
    *parse_tree = nullptr;
    RETURN_NOT_OK(Parse(stmt, parse_tree, true /* reparsed */, mem_tracker));
    return Analyze(parse_tree);
  }
  return s;
}

void QLProcessor::ExecuteAsync(const ParseTree& parse_tree, const StatementParameters& params,
                               StatementExecutedCallback cb) {
  executor_.ExecuteAsync(parse_tree, params, std::move(cb));
}

void QLProcessor::ExecuteAsync(const StatementBatch& batch, StatementExecutedCallback cb) {
  executor_.ExecuteAsync(batch, std::move(cb));
}

void QLProcessor::RunAsync(const string& stmt, const StatementParameters& params,
                           StatementExecutedCallback cb, const bool reparsed) {
  ParseTree::UniPtr parse_tree;
  const Status s = Prepare(stmt, &parse_tree, reparsed);
  if (PREDICT_FALSE(!s.ok())) {
    return cb.Run(s, nullptr /* result */);
  }
  const ParseTree* ptree = parse_tree.release();
  // Do not make a copy of stmt and params when binding to the RunAsyncDone callback because when
  // error occurs due to stale matadata, the statement needs to be reexecuted. We should pass the
  // original references which are guaranteed to still be alive when the statement is reexecuted.
  ExecuteAsync(*ptree, params, Bind(&QLProcessor::RunAsyncDone, Unretained(this), ConstRef(stmt),
                                    ConstRef(params), Owned(ptree), cb));
}

void QLProcessor::RunAsyncDone(const string& stmt, const StatementParameters& params,
                               const ParseTree* parse_tree, StatementExecutedCallback cb,
                               const Status& s, const ExecutedResult::SharedPtr& result) {
  // If execution fails due to stale metadata and the statement has not been reparsed, rerun this
  // statement with stale metadata flushed. The rerun needs to be rescheduled in because this
  // callback may not be executed in the RPC worker thread. Also, rescheduling gives other calls a
  // chance to execute first before we do.
  if (s.IsQLError() && GetErrorCode(s) == ErrorCode::STALE_METADATA && !parse_tree->reparsed()) {
    return RescheduleCurrentCall([this, &stmt, &params, cb]() {
        RunAsync(stmt, params, cb, true /* reparsed */);
      });
  }
  cb.Run(s, result);
}

void QLProcessor::RescheduleCurrentCall(std::function<void()> resume_from) {
  // Some unit tests are not executed in CQL proxy. In those cases, just execute the callback
  // directly while disabling thread restrictions.
  const bool allowed = ThreadRestrictions::SetWaitAllowed(true);
  resume_from();
  ThreadRestrictions::SetWaitAllowed(allowed);
}

}  // namespace ql
}  // namespace yb
