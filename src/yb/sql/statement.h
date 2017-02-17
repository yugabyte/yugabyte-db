//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This class represents a SQL statement (to be adde).
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_STATEMENT_H_
#define YB_SQL_STATEMENT_H_

#include <string>

namespace yb {
namespace sql {

// This class represents the parameters for executing a SQL statement.
class StatementParameters {
 public:
  StatementParameters(int64_t rows_limit = INT64_MAX,
                      const std::string& next_read_key = "");
  StatementParameters(StatementParameters&& other);

  // Accessors
  int64_t rows_limit() const { return rows_limit_; }

  const std::string& next_read_key() const { return next_read_key_; }

 private:
  // Limit of the number of rows to return.
  const int64_t rows_limit_;
  // Last rows result state to continue the execution of the statement from.
  const std::string next_read_key_;
};

} // namespace sql
} // namespace yb

#endif  // YB_SQL_STATEMENT_H_
