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
#include "yb/yql/cql/ql/util/statement_params.h"
#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/common/common.pb.h"
#include "yb/client/client.h"
#include "yb/client/session.h"

namespace yb {
namespace ql {

class TnodeContext {
 public:
  explicit TnodeContext(const TreeNode* tnode);

  // Returns the tree node of the statement being executed.
  const TreeNode* tnode() const {
    return tnode_;
  }

  // Access function for start_time and end_time.
  const MonoTime& start_time() const {
    return start_time_;
  }
  const MonoTime& end_time() const {
    return end_time_;
  }
  void set_end_time(const MonoTime& end_time) {
    end_time_ = end_time;
  }
  MonoDelta execution_time() const {
    return end_time_ - start_time_;
  }

  // Access function for op.
  std::vector<client::YBqlOpPtr>& ops() {
    return ops_;
  }
  const std::vector<client::YBqlOpPtr>& ops() const {
    return ops_;
  }

  // Add an operation.
  void AddOperation(const client::YBqlOpPtr& op) {
    ops_.push_back(op);
  }

  // Does this statement have pending operations?
  bool HasPendingOperations() const;

  // Access function for rows result.
  RowsResult::SharedPtr& rows_result() {
    return rows_result_;
  }

  // Append rows result.
  CHECKED_STATUS AppendRowsResult(RowsResult::SharedPtr&& rows_result);

  // Access functions for row_count.
  size_t row_count() const {
    return row_count_;
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

  std::vector<std::vector<QLExpressionPB>>& hash_values_options() {
    if (!hash_values_options_) {
      hash_values_options_.emplace();
    }
    return *hash_values_options_;
  }

  uint64_t current_partition_index() const {
    return current_partition_index_;
  }

  void set_partitions_count(const uint64_t count) {
    partitions_count_ = count;
  }

  // Access functions for child tnode context.
  TnodeContext* AddChildTnode(const TreeNode* tnode) {
    DCHECK(!child_context_);
    child_context_ = std::make_unique<TnodeContext>(tnode);
    return child_context_.get();
  }
  TnodeContext* child_context() {
    return child_context_.get();
  }

  // Access functions for uncovered select op template and primary keys.
  const client::YBqlReadOpPtr& uncovered_select_op() const {
    return uncovered_select_op_;
  }
  QLRowBlock* keys() {
    return keys_.get();
  }

  void SetUncoveredSelectOp(const client::YBqlReadOpPtr& select_op);

 private:
  // Tree node of the statement being executed.
  const TreeNode* tnode_ = nullptr;

  // Execution start and end time.
  const MonoTime start_time_;
  MonoTime end_time_;

  // Read/write operations to execute.
  std::vector<client::YBqlOpPtr> ops_;

  // Accumulated number of rows fetched by the statement.
  size_t row_count_ = 0;

  // For multi-partition selects (e.g. selects with 'IN' condition on hash cols) we hold the options
  // for each hash column (starting from first 'IN') as we iteratively query each partition.
  // e.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6".
  //  hash_values_options_ = [[2, 3], [4, 5], [6]]
  //  partitions_count_ = 4 (i.e. [2,4,6], [2,5,6], [3,4,6], [3,5,6]).
  //  current_partition_index_ starts from 0 unless set in the paging state.
  boost::optional<std::vector<std::vector<QLExpressionPB>>> hash_values_options_;
  uint64_t partitions_count_ = 0;
  uint64_t current_partition_index_ = 0;

  // Rows result of this statement tnode for DML statements.
  RowsResult::SharedPtr rows_result_;

  // Child context for nested statement.
  std::unique_ptr<TnodeContext> child_context_;

  // Select op template and primary keys for fetching from indexed table in an uncovered query.
  client::YBqlReadOpPtr uncovered_select_op_;
  std::unique_ptr<QLRowBlock> keys_;
};

// Processing could take a while, we are rescheduling it to our thread pool, if not yet
// running in it.
class Rescheduler {
 public:
  virtual bool NeedReschedule() = 0;
  virtual void Reschedule(rpc::ThreadPoolTask* task) = 0;
  virtual CoarseTimePoint GetDeadline() const = 0;

 protected:
  ~Rescheduler() {}
};

// The context for execution of a statement. Inside the statement parse tree, there may be one or
// more statement tnodes to be executed.
class ExecContext : public ProcessContextBase {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<ExecContext> UniPtr;
  typedef std::unique_ptr<const ExecContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.

  // Constructs an execution context to execute a statement. The context saves references to the
  // parse tree and parameters.
  ExecContext(const ParseTree& parse_tree, const StatementParameters& params);
  virtual ~ExecContext();

  // Returns the statement string being executed.
  const std::string& stmt() const override {
    return parse_tree_.stmt();
  }

  // Access function for parse_tree and params.
  const ParseTree& parse_tree() const {
    return parse_tree_;
  }
  const StatementParameters& params() const {
    return params_;
  }

  // Add a statement tree node to be executed.
  TnodeContext* AddTnode(const TreeNode *tnode);

  // Return the tnode contexts being executed.
  std::list<TnodeContext>& tnode_contexts() {
    return tnode_contexts_;
  }

  //------------------------------------------------------------------------------------------------
  // Start a distributed transaction.
  CHECKED_STATUS StartTransaction(
      IsolationLevel isolation_level, QLEnv* ql_env, Rescheduler* rescheduler);

  // Is a transaction currently in progress?
  bool HasTransaction() const {
    return transaction_ != nullptr;
  }

  // Returns the start time of the transaction.
  const MonoTime& transaction_start_time() const {
    return transaction_start_time_;
  }

  // Prepare a child distributed transaction.
  CHECKED_STATUS PrepareChildTransaction(CoarseTimePoint deadline, ChildTransactionDataPB* data);

  // Apply the result of a child distributed transaction.
  CHECKED_STATUS ApplyChildTransactionResult(const ChildTransactionResultPB& result);

  // Commit the current distributed transaction.
  void CommitTransaction(CoarseTimePoint deadline, client::CommitCallback callback);

  // Abort the current distributed transaction.
  void AbortTransaction();

  // Return the transactional session of the statement.
  client::YBSessionPtr transactional_session() {
    DCHECK(transaction_ && transactional_session_) << "transaction missing in this statement";
    return transactional_session_;
  }

  // Does this statement have pending operations?
  bool HasPendingOperations() const;

  //------------------------------------------------------------------------------------------------
  client::Restart restart() const {
    return restart_;
  }

  int64_t num_retries() const {
    return num_retries_;
  }

  // Reset this ExecContext.
  void Reset(client::Restart restart, Rescheduler* rescheduler);

 private:
  // Statement parse tree to execute and parameters to execute with.
  const ParseTree& parse_tree_;
  const StatementParameters& params_;

  // Should this statement be restarted?
  client::Restart restart_ = client::Restart::kFalse;

  // Contexts to execute statement tnodes.
  std::list<TnodeContext> tnode_contexts_;

  // Transaction and session to apply transactional write operations in and the start time.
  client::YBTransactionPtr transaction_;
  client::YBSessionPtr transactional_session_;
  MonoTime transaction_start_time_;

  // The number of times this statement has been retried.
  int64_t num_retries_ = 0;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_EXEC_EXEC_CONTEXT_H_
