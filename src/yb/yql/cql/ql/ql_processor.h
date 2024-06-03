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
//
// Entry to SQL module. It takes SQL statements and uses the given YBClient to execute them. Each
// QLProcessor runs on one and only one thread.
// Notably, this does NOT apply to Reschedule implementation methods, which are called from
// different ExecContexts, so non-thread-safe fields should not be referenced there.
//--------------------------------------------------------------------------------------------------
#pragma once

#include "yb/ash/wait_state.h"

#include "yb/client/client_fwd.h"

#include "yb/server/server_fwd.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/metrics_fwd.h"
#include "yb/util/object_pool.h"

#include "yb/yql/cql/ql/ql_fwd.h"
#include "yb/yql/cql/ql/audit/audit_logger.h"
#include "yb/yql/cql/ql/sem/analyzer.h"
#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/exec/rescheduler.h"
#include "yb/yql/cql/ql/parser/parser_fwd.h"
#include "yb/yql/cql/ql/util/ql_env.h"
#include "yb/yql/cql/ql/util/statement_result.h"

namespace yb {
namespace ql {

class QLMetrics {
 public:
  explicit QLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity);
  ~QLMetrics();

  scoped_refptr<yb::Histogram> time_to_parse_ql_query_;
  scoped_refptr<yb::Histogram> time_to_analyze_ql_query_;
  scoped_refptr<yb::Histogram> time_to_execute_ql_query_;
  scoped_refptr<yb::Histogram> num_rounds_to_analyze_ql_;
  scoped_refptr<yb::Histogram> num_retries_to_execute_ql_;
  scoped_refptr<yb::Histogram> num_flushes_to_execute_ql_;

  scoped_refptr<yb::Histogram> ql_select_;
  scoped_refptr<yb::Histogram> ql_insert_;
  scoped_refptr<yb::Histogram> ql_update_;
  scoped_refptr<yb::Histogram> ql_delete_;
  scoped_refptr<yb::Histogram> ql_use_;
  scoped_refptr<yb::Histogram> ql_others_;
  scoped_refptr<yb::Histogram> ql_transaction_;

  scoped_refptr<yb::Histogram> ql_response_size_bytes_;
};

class QLProcessor : public Rescheduler {
 public:
  // Public types.
  typedef std::unique_ptr<QLProcessor> UniPtr;
  typedef std::unique_ptr<const QLProcessor> UniPtrConst;

  // Constructors.
  QLProcessor(client::YBClient* client,
              std::shared_ptr<client::YBMetaDataCache> cache,
              QLMetrics* ql_metrics,
              ThreadSafeObjectPool<Parser>* parser_pool,
              const server::ClockPtr& clock,
              TransactionPoolProvider transaction_pool_provider);
  virtual ~QLProcessor();

  // Prepare a SQL statement (parse and analyze). A reference to the statement string is saved in
  // the parse tree.
  Status Prepare(const std::string& stmt, ParseTreePtr* parse_tree,
                 bool reparsed = false, const MemTrackerPtr& mem_tracker = nullptr,
                 const bool internal = false);

  // Check whether the current user has the required permissions to execute the statment.
  bool CheckPermissions(const ParseTree& parse_tree, StatementExecutedCallback cb);

  // Execute a prepared statement (parse tree) or batch. The parse trees and the parameters must not
  // be destroyed until the statements have been executed.
  void ExecuteAsync(const ParseTree& parse_tree, const StatementParameters& params,
                    StatementExecutedCallback cb);
  void ExecuteAsync(const StatementBatch& batch, StatementExecutedCallback cb);

  // Run (parse, analyze and execute) a SQL statement. The statement string and the parameters must
  // not be destroyed until the statement has been executed.
  TreeNodeOpcode RunAsync(const std::string& stmt, const StatementParameters& params,
                          StatementExecutedCallback cb, bool reparsed = false);

 protected:
  void SetCurrentSession(const QLSessionPtr& ql_session) {
    ql_env_.set_ql_session(ql_session);
  }

  bool NeedReschedule() override { return true; }
  void Reschedule(rpc::ThreadPoolTask* task) override;
  CoarseTimePoint GetDeadline() const override;

  // Check whether the current user has the required permissions for the parser tree node.
  Status CheckNodePermissions(const TreeNode* tnode);

  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) that processor uses to execute statement.
  QLEnv ql_env_;

  // Used for logging audit records.
  audit::AuditLogger audit_logger_;

  // Semantic analysis processor.
  Analyzer analyzer_;

  // Tree executor.
  Executor executor_;

  // SQL metrics.
  QLMetrics* const ql_metrics_;

  ThreadSafeObjectPool<Parser>* parser_pool_;

  // Whether the execution was rescheduled.
  std::atomic<IsRescheduled> is_rescheduled_{IsRescheduled::kFalse};

 private:
  friend class QLTestBase;
  friend class TestQLProcessor;

  // Parse a SQL statement and generate a parse tree.
  Status Parse(const std::string& stmt, ParseTreePtr* parse_tree,
               bool reparsed = false, const MemTrackerPtr& mem_tracker = nullptr,
               const bool internal = false);

  // Semantically analyze a parse tree.
  Status Analyze(ParseTreePtr* parse_tree);

  void RunAsyncDone(const std::string& stmt, const StatementParameters& params,
                    const ParseTree* parse_tree, StatementExecutedCallback cb,
                    const Status& s, const ExecutedResult::SharedPtr& result);

  class RunAsyncTask : public rpc::ThreadPoolTask {
   public:
    RunAsyncTask& Bind(QLProcessor* processor, const std::string& stmt,
                       const StatementParameters& params, StatementExecutedCallback cb) {
      processor_ = processor;
      stmt_ = &stmt;
      params_ = &params;
      cb_ = std::move(cb);
      wait_state_ = ash::WaitStateInfo::CurrentWaitState();
      return *this;
    }

    virtual ~RunAsyncTask() {}

   private:
    void Run() override {
      auto processor = processor_;
      processor_ = nullptr;
      ADOPT_WAIT_STATE(wait_state_);
      wait_state_ = nullptr;
      processor->RunAsync(*stmt_, *params_, std::move(cb_), true /* reparsed */);
    }

    void Done(const Status& status) override {}

    QLProcessor* processor_ = nullptr;
    const std::string* stmt_ = nullptr;
    const StatementParameters* params_ = nullptr;
    ash::WaitStateInfoPtr wait_state_ = nullptr;
    StatementExecutedCallback cb_;
  };

  RunAsyncTask run_async_task_;

  friend class RunAsyncTask;
};

}  // namespace ql
}  // namespace yb
