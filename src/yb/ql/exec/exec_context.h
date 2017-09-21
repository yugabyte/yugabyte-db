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
// This class represents the context to execute a single statment. It contains the statement code
// (parse tree) and the environment (parameters and session context) with which the code is to be
// executed.
//--------------------------------------------------------------------------------------------------

#ifndef YB_QL_EXEC_EXEC_CONTEXT_H_
#define YB_QL_EXEC_EXEC_CONTEXT_H_

#include "yb/ql/ptree/process_context.h"
#include "yb/ql/util/ql_env.h"
#include "yb/ql/util/statement_result.h"

namespace yb {
namespace ql {

class ExecContext : public ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ExecContext> UniPtr;
  typedef std::unique_ptr<const ExecContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  ExecContext(const char *ql_stmt,
              size_t stmt_len,
              const ParseTree *parse_tree,
              const StatementParameters *params,
              QLEnv *ql_env);
  virtual ~ExecContext();

  // Get a table creator from YB client.
  client::YBTableCreator* NewTableCreator() {
    return ql_env_->NewTableCreator();
  }

  // Get a table alterer from YB client.
  client::YBTableAlterer* NewTableAlterer(const client::YBTableName& table_name) {
    return ql_env_->NewTableAlterer(table_name);
  }

  CHECKED_STATUS DeleteTable(const client::YBTableName& name) {
    return ql_env_->DeleteTable(name);
  }

  // Keyspace related methods.

  // Create a new keyspace with the given name.
  CHECKED_STATUS CreateKeyspace(const std::string& keyspace_name) {
    return ql_env_->CreateKeyspace(keyspace_name);
  }

  // Delete keyspace with the given name.
  CHECKED_STATUS DeleteKeyspace(const std::string& keyspace_name) {
    return ql_env_->DeleteKeyspace(keyspace_name);
  }

  // Use keyspace with the given name.
  CHECKED_STATUS UseKeyspace(const std::string& keyspace_name) {
    return ql_env_->UseKeyspace(keyspace_name);
  }

  std::string CurrentKeyspace() const {
    return ql_env_->CurrentKeyspace();
  }

  // (User-defined) Type related methods.

  // Create (user-defined) type with the given arguments
  CHECKED_STATUS CreateUDType(const std::string &keyspace_name,
                              const std::string &type_name,
                              const std::vector<std::string> &field_names,
                              const std::vector<std::shared_ptr<QLType>> &field_types) {
    return ql_env_->CreateUDType(keyspace_name, type_name, field_names, field_types);
  }

  // Delete a (user-defined) type by name.
  CHECKED_STATUS DeleteUDType(const std::string &keyspace_name, const std::string &type_name) {
    return ql_env_->DeleteUDType(keyspace_name, type_name);
  }

  // Access function for ql_env.
  QLEnv* ql_env() const {
    return ql_env_;
  }

  // Access function for parse_tree.
  const ParseTree* parse_tree() const {
    return parse_tree_;
  }

  // Returns the tree node of the statement being executed.
  const TreeNode* tnode() const {
    return parse_tree_->root().get();
  }

  // Access function for params.
  const StatementParameters* params() const {
    return params_;
  }

  // Access function for op.
  const std::shared_ptr<client::YBqlOp>& op() const {
    return op_;
  }

  // Used for multi-partition selects (i.e. with 'IN' conditions on hash columns).
  // Called from Executor::FetchMoreRowsIfNeeded to check if request is finished.
  uint64_t UnreadPartitionsRemaining() const {
    return partitions_count_ - current_partition_index_;
  }

  // Used for multi-partition selects (i.e. with 'IN' conditions on hash columns).
  // Initializes the current partition index and sets the corresponding hashed column values in the
  // request object so that it references the appropriate partition.
  // Called from Executor::ExecPTNode for PTSelectStmt.
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6" start_position 0:
  // this will set req->hashed_column_values() to [1, 2, 4, 6].
  void InitializePartition(QLReadRequestPB *req, uint64_t start_partition);

  // Used for multi-partition selects (i.e. with 'IN' conditions on hash columns).
  // Increments the current partition index and updates the corresponding hashed column values in
  // passed request object so that it references the appropriate partition.
  // Called from Executor::FetchMoreRowsIfNeeded.
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6" partition index 2:
  // this will do, index: 2 -> 3 and hashed_column_values: [1, 3, 4, 6] -> [1, 3, 5, 6].
  void AdvanceToNextPartition(QLReadRequestPB *req);

  std::unique_ptr<std::vector<std::vector<QLExpressionPB>>>& hash_values_options() {
    if (hash_values_options_ == nullptr) {
      hash_values_options_ = std::make_unique<std::vector<std::vector<QLExpressionPB>>>();
    }
    return hash_values_options_;
  }

  uint64_t current_partition_index() const {
    return current_partition_index_;
  }

  void set_partitions_count(uint64_t count) {
    partitions_count_ = count;
  }

  // Access function for start_time.
  const MonoTime& start_time() const {
    return start_time_;
  }

  // Apply YBClient read/write operation.
  CHECKED_STATUS ApplyWrite(std::shared_ptr<client::YBqlWriteOp> op) {
    op_ = op;
    return ql_env_->ApplyWrite(op);
  }

  CHECKED_STATUS ApplyRead(std::shared_ptr<client::YBqlReadOp> op) {
    op_ = op;
    return ql_env_->ApplyRead(op);
  }

  // Variants of ProcessContextBase::Error() that report location of statement tnode as the error
  // location.
  using ProcessContextBase::Error;
  CHECKED_STATUS Error(ErrorCode error_code);
  CHECKED_STATUS Error(const char *m, ErrorCode error_code);
  CHECKED_STATUS Error(const Status& s, ErrorCode error_code);

 private:
  // Statement parse tree to execute.
  const ParseTree *parse_tree_;

  // Statement parameters to execute with.
  const StatementParameters *params_;

  // Read/write operation to execute.
  std::shared_ptr<client::YBqlOp> op_;

  // Execution start time.
  const MonoTime start_time_;

  // SQL environment.
  QLEnv *ql_env_;

  // For multi-partition selects (e.g. selects with 'IN' condition on hash cols) we hold the options
  // for each hash column (starting from first 'IN') as we iteratively query each partition.
  // e.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6".
  //  hash_values_options_ = [[2, 3], [4, 5], [6]]
  //  partitions_count_ = 4 (i.e. [2,4,6], [2,5,6], [3,4,6], [4,5,6]).
  //  current_partition_index_ starts from 0 unless set in the paging state.
  std::unique_ptr<std::vector<std::vector<QLExpressionPB>>> hash_values_options_;
  uint64_t partitions_count_;
  uint64_t current_partition_index_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_QL_EXEC_EXEC_CONTEXT_H_
