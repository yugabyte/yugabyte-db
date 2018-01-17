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
//
// Entry to SQL module. It takes SQL statements and uses the given YBClient to execute them. Each
// QLProcessor runs on one and only one thread, so all function in SQL modules don't need to be
// thread-safe.
//--------------------------------------------------------------------------------------------------
#ifndef YB_YQL_CQL_QL_QL_PROCESSOR_H_
#define YB_YQL_CQL_QL_QL_PROCESSOR_H_

#include "yb/client/callbacks.h"

#include "yb/yql/cql/cqlserver/cql_rpcserver_env.h"

#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/parser/parser.h"
#include "yb/yql/cql/ql/sem/analyzer.h"
#include "yb/yql/cql/ql/util/ql_env.h"

#include "yb/util/metrics.h"

namespace yb {
namespace ql {

class QLMetrics {
 public:
  explicit QLMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity);

  scoped_refptr<yb::Histogram> time_to_parse_ql_query_;
  scoped_refptr<yb::Histogram> time_to_analyze_ql_query_;
  scoped_refptr<yb::Histogram> time_to_execute_ql_query_;
  scoped_refptr<yb::Histogram> num_rounds_to_analyze_ql_;

  scoped_refptr<yb::Histogram> ql_select_;
  scoped_refptr<yb::Histogram> ql_insert_;
  scoped_refptr<yb::Histogram> ql_update_;
  scoped_refptr<yb::Histogram> ql_delete_;
  scoped_refptr<yb::Histogram> ql_others_;
  scoped_refptr<yb::Histogram> ql_transaction_;

  scoped_refptr<yb::Histogram> ql_response_size_bytes_;
};

class QLProcessor {
 public:
  // Public types.
  typedef std::unique_ptr<QLProcessor> UniPtr;
  typedef std::unique_ptr<const QLProcessor> UniPtrConst;

  // Constructors.
  explicit QLProcessor(std::weak_ptr<rpc::Messenger> messenger,
                        std::shared_ptr<client::YBClient> client,
                        std::shared_ptr<client::YBMetaDataCache> cache, QLMetrics* ql_metrics,
                        cqlserver::CQLRpcServerEnv* cql_rpcserver_env = nullptr);
  virtual ~QLProcessor();

  // Prepare a SQL statement (parse and analyze).
  CHECKED_STATUS Prepare(const string& ql_stmt, ParseTree::UniPtr* parse_tree,
                         bool reparsed = false, std::shared_ptr<MemTracker> mem_tracker = nullptr);

  // Execute a prepared statement (parse tree).
  void ExecuteAsync(const std::string& ql_stmt, const ParseTree& parse_tree,
                    const StatementParameters& params, StatementExecutedCallback cb);

  // Run (parse, analyze and execute) a SQL statement.
  void RunAsync(const std::string& ql_stmt, const StatementParameters& params,
                StatementExecutedCallback cb, bool reparsed = false);

  // Batch execution of statements. StatementExecutedCallback will be invoked when the batch is
  // applied and execution is complete, or when an error occurs.
  void BeginBatch(StatementExecutedCallback cb);
  void ExecuteBatch(const std::string& ql_stmt, const ParseTree& parse_tree,
                    const StatementParameters& params);
  void RunBatch(const std::string& ql_stmt, const StatementParameters& params,
                ParseTree::UniPtr* parse_tree, bool reparsed = false);
  void ApplyBatch();
  void AbortBatch();

 protected:
  void SetCurrentCall(rpc::InboundCallPtr call);
  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) that processor uses to execute statement.
  QLEnv ql_env_;

  // Parsing processor.
  Parser parser_;

  // Semantic analysis processor.
  Analyzer analyzer_;

  // Tree executor.
  Executor executor_;

  // SQL metrics.
  QLMetrics* const ql_metrics_;

 private:
  friend class QLTestBase;

  // Parse a SQL statement and generate a parse tree.
  CHECKED_STATUS Parse(const string& ql_stmt, ParseTree::UniPtr* parse_tree, bool reparsed = false,
                       std::shared_ptr<MemTracker> mem_tracker = nullptr);

  // Semantically analyze a parse tree.
  CHECKED_STATUS Analyze(const string& ql_stmt, ParseTree::UniPtr* parse_tree);

  void RunAsyncDone(const std::string& ql_stmt, const StatementParameters* params,
                    const ParseTree *parse_tree, StatementExecutedCallback cb,
                    const Status& s, const ExecutedResult::SharedPtr& result);
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_QL_PROCESSOR_H_
