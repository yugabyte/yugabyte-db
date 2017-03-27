//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class represents a SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_STATEMENT_H_
#define YB_SQL_STATEMENT_H_

#include <boost/thread/shared_mutex.hpp>

#include "yb/sql/ptree/parse_tree.h"
#include "yb/sql/util/statement_params.h"
#include "yb/sql/util/statement_result.h"

#include "yb/common/yql_value.h"

namespace yb {
namespace sql {

class SqlProcessor;

class Statement {
 public:
  // Public types.
  typedef std::unique_ptr<Statement> UniPtr;
  typedef std::unique_ptr<const Statement> UniPtrConst;

  // No last-prepare time.
  static const MonoTime kNoLastPrepareTime;

  // Constructors.
  explicit Statement(const std::string& keyspace, const std::string& text);
  virtual ~Statement();

  // Returns statement text.
  const std::string& text() const { return text_; }

  // Prepare the statement for execution. Reprepare it if it hasn't been since last_prepare_time.
  // Use kNoLastPrepareTime if it doesn't need to be reprepared. Optionally return prepared result
  // if requested.
  CHECKED_STATUS Prepare(SqlProcessor *processor,
                         const MonoTime& last_prepare_time = kNoLastPrepareTime,
                         bool refresh_cache = false,
                         std::shared_ptr<MemTracker> mem_tracker = nullptr,
                         PreparedResult::UniPtr *result = nullptr);

  // Execute the prepared statement.
  void ExecuteAsync(
      SqlProcessor* processor, const StatementParameters& params, StatementExecutedCallback cb);

  // Run the statement (i.e. prepare and execute).
  void RunAsync(
      SqlProcessor* processor, const StatementParameters& params, StatementExecutedCallback cb);

 protected:
  // The keyspace this statement is parsed in.
  const std::string keyspace_;

  // The text of the SQL statement.
  const std::string text_;

 private:
  struct Request {
    Request(SqlProcessor* processor, const StatementParameters* params)
        : processor(processor), params(params) {}
    SqlProcessor* const processor;
    const StatementParameters* const params;
  };

  // Execute the prepared statement. Don't reprepare.
  void DoExecuteAsync(SqlProcessor* processor,
                      const StatementParameters& params,
                      const MonoTime &last_prepare_time,
                      Callback<void(const MonoTime &last_prepare_time,
                                    bool new_analysis_needed,
                                    const Status &s,
                                    ExecutedResult::SharedPtr result)> cb);

  void ExecuteAsyncDone(
      Request req, StatementExecutedCallback cb, const MonoTime &updated_last_prepare_time,
      bool new_analysis_needed, const Status &s, ExecutedResult::SharedPtr result);

  // The prepare time.
  MonoTime prepare_time_;

  // The parse tree.
  ParseTree::UniPtr parse_tree_;

  // Shared/exclusive lock on the parse tree and parse time.
  boost::shared_mutex lock_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_STATEMENT_H_
