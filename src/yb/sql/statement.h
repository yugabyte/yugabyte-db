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
// This class represents a SQL statement.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_STATEMENT_H_
#define YB_SQL_STATEMENT_H_

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
      const WARN_UNUSED_RESULT;

 protected:
  // The keyspace this statement is parsed in.
  const std::string keyspace_;

  // The text of the SQL statement.
  const std::string text_;

 private:
  // The parse tree.
  ParseTree::UniPtr parse_tree_;

  // Mutex that protects the generation of the parse tree.
  std::mutex parse_tree_mutex_;

  // Atomic bool to indicate if the statement has been prepared.
  std::atomic<bool> prepared_ = {false};
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_STATEMENT_H_
