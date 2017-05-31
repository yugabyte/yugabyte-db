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

#include "yb/util/monotime.h"

namespace yb {
namespace sql {

class SqlProcessor;

class Statement {
 public:
  // Public types.
  typedef std::unique_ptr<Statement> UniPtr;
  typedef std::unique_ptr<const Statement> UniPtrConst;

  // Constructors.
  Statement(const std::string& keyspace, const std::string& text);
  virtual ~Statement();

  // Returns the keyspace and statement text.
  const std::string& keyspace() const { return keyspace_; }
  const std::string& text() const { return text_; }

  // Prepare the statement for execution. Optionally return prepared result if requested.
  CHECKED_STATUS Prepare(
      SqlProcessor *processor, std::shared_ptr<MemTracker> mem_tracker = nullptr,
      PreparedResult::UniPtr *result = nullptr);

  // Execute the prepared statement. Returns false if the statement has not been prepared
  // successfully.
  bool ExecuteAsync(
      SqlProcessor* processor, const StatementParameters& params, StatementExecutedCallback cb)
      WARN_UNUSED_RESULT;

 protected:
  // The keyspace this statement is parsed in.
  const std::string keyspace_;

  // The text of the SQL statement.
  const std::string text_;

 private:
  // The parse tree.
  ParseTree::UniPtr parse_tree_;

  // Shared/exclusive lock on the parse tree and parse time.
  boost::shared_mutex lock_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_STATEMENT_H_
