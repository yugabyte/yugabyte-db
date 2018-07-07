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

#ifndef YB_YQL_CQL_QL_EXEC_EXEC_CONTEXT_H_
#define YB_YQL_CQL_QL_EXEC_EXEC_CONTEXT_H_

#include "yb/yql/cql/ql/ptree/process_context.h"
#include "yb/yql/cql/ql/util/ql_env.h"
#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/common/common.pb.h"

namespace yb {
namespace ql {

class TnodeContext {
 public:
  explicit TnodeContext(const TreeNode* tnode);

  // Returns the tree node of the statement being executed.
  const TreeNode* tnode() const {
    return tnode_;
  }

  // Access function for start_time.
  const MonoTime& start_time() const {
    return start_time_;
  }

  // Access function for op.
  const std::vector<std::shared_ptr<client::YBqlOp>>& ops() const {
    return ops_;
  }

  // Apply one YBClient read/write operation.
  CHECKED_STATUS Apply(std::shared_ptr<client::YBqlOp> op, QLEnv* ql_env, bool defer = false) {
    ops_ = {op};
    deferred_ = defer;
    return defer ? Status::OK() : ql_env->Apply(op);
  }

  // Add an operation -- need to call Apply below to apply all added ops.
  void AddOperation(std::shared_ptr<client::YBqlOp> op) {
    ops_.push_back(op);
  }

  // Apply all operations.
  CHECKED_STATUS Apply(QLEnv* ql_env) {
    deferred_ = false;
    for (auto& op : ops_) {
      RETURN_NOT_OK(ql_env->Apply(op));
    }
    return Status::OK();
  }

  // Is the operation deferred?
  bool IsDeferred() const {
    return !ops_.empty() && deferred_;
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

 private:
  // Tree node of the statement being executed.
  const TreeNode* tnode_ = nullptr;

  // Execution start time.
  const MonoTime start_time_;

  // Read/write operations to execute.
  std::vector<std::shared_ptr<client::YBqlOp>> ops_;

  // Is the operation deferred?
  bool deferred_ = false;

  // For multi-partition selects (e.g. selects with 'IN' condition on hash cols) we hold the options
  // for each hash column (starting from first 'IN') as we iteratively query each partition.
  // e.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6".
  //  hash_values_options_ = [[2, 3], [4, 5], [6]]
  //  partitions_count_ = 4 (i.e. [2,4,6], [2,5,6], [3,4,6], [4,5,6]).
  //  current_partition_index_ starts from 0 unless set in the paging state.
  std::unique_ptr<std::vector<std::vector<QLExpressionPB>>> hash_values_options_;
  uint64_t partitions_count_ = 0;
  uint64_t current_partition_index_ = 0;
};

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

  // Add a statement tree node for execution.
  void AddTnode(const TreeNode *tnode);

  // Returns the context for the current tnode being executed.
  TnodeContext* tnode_context() {
    return &tnode_contexts_.back();
  }

  // Access function for parse_tree.
  const ParseTree* parse_tree() const {
    return parse_tree_;
  }

  // Access function for params.
  const StatementParameters* params() const {
    return params_;
  }

  const std::list<TnodeContext>& tnode_contexts() const {
    return tnode_contexts_;
  }
  std::list<TnodeContext>* tnode_contexts() {
    return &tnode_contexts_;
  }

  int64_t num_retries() const {
    return num_retries_;
  }

  int64_t num_flushes() const {
    return num_flushes_;
  }

  void inc_num_flushes() {
    num_flushes_ += 1;
  }

  // Apply YBClient read/write operation.
  CHECKED_STATUS Apply(std::shared_ptr<client::YBqlOp> op, const bool defer = false) {
    return tnode_context()->Apply(op, ql_env_, defer);
  }

  // Apply YBClient read/write operation.
  CHECKED_STATUS ApplyAll() {
    return tnode_context()->Apply(ql_env_);
  }

  // Reset this ExecContext to re-execute the statement.
  void Reset();

 private:
  // Statement parse tree to execute.
  const ParseTree *parse_tree_;

  // Statement parameters to execute with.
  const StatementParameters *params_;

  // SQL environment.
  QLEnv *ql_env_;

  std::list<TnodeContext> tnode_contexts_;

  int64_t num_retries_ = 0;

  // The number of times we called flush for this exec context.
  int64_t num_flushes_ = 0;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_EXEC_EXEC_CONTEXT_H_
