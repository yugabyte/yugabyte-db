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

#pragma once

#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/util/statement_params.h"
#include "yb/yql/cql/ql/util/statement_result.h"

#include "yb/util/monotime.h"

namespace yb {
namespace ql {

class QLProcessor;

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
  Status Prepare(QLProcessor *processor, const MemTrackerPtr& mem_tracker = nullptr,
                 const bool internal = false, PreparedResult::UniPtr *result = nullptr);

  // Execute the prepared statement.
  Status ExecuteAsync(QLProcessor* processor, const StatementParameters& params,
                      StatementExecutedCallback cb) const;

  // Validate and return the parse tree.
  Result<const ParseTree&> GetParseTree() const;

  // Is this statement unprepared?
  bool unprepared() const {
    return !prepared_.load(std::memory_order_acquire);
  }

  // Is this statement stale?
  bool stale() const {
    return parse_tree_->stale();
  }

  // Clear the reparsed status.
  void clear_reparsed() const {
    parse_tree_->clear_reparsed();
  }

  size_t DynamicMemoryUsage() const {
    return sizeof(*this) + keyspace_.size() + text_.size();
  }

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

}  // namespace ql
}  // namespace yb
