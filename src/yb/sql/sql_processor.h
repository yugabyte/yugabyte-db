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
  scoped_refptr<yb::Histogram> time_to_analyse_sql_query_;
  scoped_refptr<yb::Histogram> time_to_execute_sql_query_;
  scoped_refptr<yb::Histogram> num_rounds_to_analyse_sql_;

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
      std::shared_ptr<client::YBTableCache> cache, SqlMetrics* sql_metrics);
  virtual ~SqlProcessor();

  // Set the SQL session to use to process SQL statements.
  void SetSqlSession(SqlSession::SharedPtr sql_session) {
    sql_env_->set_sql_session(sql_session);
  }

  // Parse a SQL statement and generate a parse tree.
  CHECKED_STATUS Parse(const string& sql_stmt,
                       ParseTree::UniPtr* parse_tree,
                       std::shared_ptr<MemTracker> mem_tracker);

  // Semantically analyze a parse tree.
  CHECKED_STATUS Analyze(const string& sql_stmt,
                         ParseTree::UniPtr* parse_tree,
                         bool refresh_cache);

  // Execute a parse tree.
  void ExecuteAsync(
      const string& sql_stmt, const ParseTree& parse_tree, const StatementParameters& params,
      Callback<void(
          bool new_analysis_needed, const Status& s, ExecutedResult::SharedPtr result)> cb);

  // Execute a SQL statement.
  void RunAsync(
      const std::string& sql_stmt, const StatementParameters& params,
      StatementExecutedCallback cb);

  // Claim this processor for a request.
  void used() {
    start_time_ = MonoTime::Now(MonoTime::FINE);
    is_used_ = true;
  }
  // Unclaim this processor.
  void unused() {
    SetCurrentCall(nullptr);
    is_used_ = false;
  }
  // Check if the processor is currently working on a statement.
  bool is_used() const {
    return is_used_;
  }

 protected:
  void SetCurrentCall(rpc::CQLInboundCall* cql_call);
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

  // Processing state.
  bool is_used_;
  MonoTime start_time_;

 private:
  struct Request {
    Request(
        const std::string& sql_stmt, const ParseTree& parse_tree, const StatementParameters& params)
        : sql_stmt(sql_stmt), parse_tree(parse_tree), params(params) {}

    const std::string& sql_stmt;
    const ParseTree& parse_tree;
    const StatementParameters& params;
  };

  void ProcessExecuteResponse(
      const MonoTime& begin_time,
      Callback<void(bool, const Status& s, ExecutedResult::SharedPtr result)> cb,
      const Status& s, ExecutedResult::SharedPtr result);
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_SQL_PROCESSOR_H_
