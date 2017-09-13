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
// SqlProcessor runs on one and only one thread, so all function in SQL modules don't need to be
// thread-safe.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_SQL_PROCESSOR_H_
#define YB_SQL_SQL_PROCESSOR_H_

#include "yb/client/callbacks.h"

#include "yb/cqlserver/cql_rpcserver_env.h"

#include "yb/sql/exec/executor.h"
#include "yb/sql/parser/parser.h"
#include "yb/sql/sem/analyzer.h"
#include "yb/sql/util/sql_env.h"

#include "yb/util/metrics.h"

namespace yb {
namespace sql {

class SqlMetrics {
 public:
  explicit SqlMetrics(const scoped_refptr<yb::MetricEntity>& metric_entity);

  scoped_refptr<yb::Histogram> time_to_parse_sql_query_;
  scoped_refptr<yb::Histogram> time_to_analyze_sql_query_;
  scoped_refptr<yb::Histogram> time_to_execute_sql_query_;
  scoped_refptr<yb::Histogram> num_rounds_to_analyze_sql_;

  scoped_refptr<yb::Histogram> sql_select_;
  scoped_refptr<yb::Histogram> sql_insert_;
  scoped_refptr<yb::Histogram> sql_update_;
  scoped_refptr<yb::Histogram> sql_delete_;
  scoped_refptr<yb::Histogram> sql_others_;

  scoped_refptr<yb::Histogram> sql_response_size_bytes_;
};

class SqlProcessor {
 public:
  // Public types.
  typedef std::unique_ptr<SqlProcessor> UniPtr;
  typedef std::unique_ptr<const SqlProcessor> UniPtrConst;

  // Constructors.
  explicit SqlProcessor(std::weak_ptr<rpc::Messenger> messenger,
                        std::shared_ptr<client::YBClient> client,
                        std::shared_ptr<client::YBMetaDataCache> cache, SqlMetrics* sql_metrics,
                        cqlserver::CQLRpcServerEnv* cql_rpcserver_env = nullptr);
  virtual ~SqlProcessor();

  // Prepare a SQL statement (parse and analyze).
  CHECKED_STATUS Prepare(const string& sql_stmt, ParseTree::UniPtr* parse_tree,
                         bool reparsed = false, std::shared_ptr<MemTracker> mem_tracker = nullptr);

  // Execute a prepared statement (parse tree).
  void ExecuteAsync(const std::string& sql_stmt, const ParseTree& parse_tree,
                    const StatementParameters& params, StatementExecutedCallback cb);

  // Run (parse, analyze and execute) a SQL statement.
  void RunAsync(const std::string& sql_stmt, const StatementParameters& params,
                StatementExecutedCallback cb, bool reparsed = false);

  // Batch execution of statements. StatementExecutedCallback will be invoked when the batch is
  // applied and execution is complete, or when an error occurs.
  void BeginBatch(StatementExecutedCallback cb);
  void ExecuteBatch(const std::string& sql_stmt, const ParseTree& parse_tree,
                    const StatementParameters& params);
  void RunBatch(const std::string& sql_stmt, const StatementParameters& params,
                ParseTree::UniPtr* parse_tree, bool reparsed = false);
  void ApplyBatch();
  void AbortBatch();

 protected:
  void SetCurrentCall(rpc::InboundCallPtr call);
  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) that processor uses to execute statement.
  SqlEnv sql_env_;

  // Parsing processor.
  Parser parser_;

  // Semantic analysis processor.
  Analyzer analyzer_;

  // Tree executor.
  Executor executor_;

  // SQL metrics.
  SqlMetrics* const sql_metrics_;

 private:
  friend class YbSqlTestBase;

  // Parse a SQL statement and generate a parse tree.
  CHECKED_STATUS Parse(const string& sql_stmt, ParseTree::UniPtr* parse_tree, bool reparsed = false,
                       std::shared_ptr<MemTracker> mem_tracker = nullptr);

  // Semantically analyze a parse tree.
  CHECKED_STATUS Analyze(const string& sql_stmt, ParseTree::UniPtr* parse_tree);

  void RunAsyncDone(const std::string& sql_stmt, const StatementParameters* params,
                    const ParseTree *parse_tree, StatementExecutedCallback cb,
                    const Status& s, const ExecutedResult::SharedPtr& result);
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_SQL_PROCESSOR_H_
