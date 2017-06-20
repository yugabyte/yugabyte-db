//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry to SQL module. It takes SQL statements and uses the given YBClient to execute them. Each
// SqlProcessor runs on one and only one thread, so all function in SQL modules don't need to be
// thread-safe.
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_SQL_PROCESSOR_H_
#define YB_SQL_SQL_PROCESSOR_H_

#include "yb/client/callbacks.h"
#include "yb/rpc/cql_rpcserver_env.h"
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
  explicit SqlProcessor(
      std::weak_ptr<rpc::Messenger> messenger, std::shared_ptr<client::YBClient> client,
      std::shared_ptr<client::YBTableCache> cache, SqlMetrics* sql_metrics,
      rpc::CQLRpcServerEnv* cql_rpcserver_env = nullptr);
  virtual ~SqlProcessor();

  // Parse a SQL statement and generate a parse tree.
  CHECKED_STATUS Parse(
      const string& sql_stmt, ParseTree::UniPtr* parse_tree,
      std::shared_ptr<MemTracker> mem_tracker);

  // Semantically analyze a parse tree. Return "reparse" if an error was encountered that may be
  // attributed to stale metadata cache. If that happens, the metadata cache will be purged and the
  // statement should be reparsed and re-analyzed. When the statement is parsed for the first time,
  // caller should check if the statement needs to be "reparsed".
  CHECKED_STATUS Analyze(
      const string& sql_stmt, ParseTree::UniPtr* parse_tree, bool* reparse = nullptr);

  // Execute a parse tree.
  void ExecuteAsync(
      const string& sql_stmt, const ParseTree& parse_tree, const StatementParameters& params,
      StatementExecutedCallback cb, bool reparsed = false);

  // Execute a SQL statement.
  void RunAsync(
      const std::string& sql_stmt, const StatementParameters& params,
      StatementExecutedCallback cb);

 protected:
  void SetCurrentCall(rpc::InboundCallPtr call);
  //------------------------------------------------------------------------------------------------
  // Parsing processor.
  Parser::UniPtr parser_;

  // Semantic analysis processor.
  Analyzer::UniPtr analyzer_;

  // Tree executor.
  Executor::UniPtr executor_;

  // Environment (YBClient) that processor uses to execute statement.
  SqlEnv::UniPtr sql_env_;

  // SQL metrics.
  SqlMetrics* const sql_metrics_;

 private:
  void ExecuteAsyncDone(
      const MonoTime& begin_time, const ParseTree *parse_tree, StatementExecutedCallback cb,
      bool reparsed, const Status& s, const ExecutedResult::SharedPtr& result);
  void RunAsyncDone(
      const std::string& sql_stmt, const StatementParameters* params, ParseTree *parse_tree,
      StatementExecutedCallback cb, const Status& s, const ExecutedResult::SharedPtr& result);
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_SQL_PROCESSOR_H_
