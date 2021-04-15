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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/client/callbacks.h"
#include "yb/client/table.h"
#include "yb/client/yb_op.h"
#include "yb/rpc/thread_pool.h"
#include "yb/util/trace.h"

namespace yb {
namespace ql {

using client::CommitCallback;
using client::Restart;
using client::YBqlReadOpPtr;
using client::YBSessionPtr;
using client::YBTransactionPtr;

ExecContext::ExecContext(const ParseTree& parse_tree, const StatementParameters& params)
    : parse_tree_(parse_tree), params_(params) {
}

ExecContext::~ExecContext() {
  // Reset to abort transaction explicitly instead of letting it expire.
  // Should be ok not to take a rescheduler here since the `ExecContext` clean up should happen
  // only when we return a response to the CQL client, which is now guaranteed to happen in
  // CQL proxy's handler thread.
  Reset(client::Restart::kFalse, nullptr);
}

TnodeContext* ExecContext::AddTnode(const TreeNode *tnode) {
  restart_ = client::Restart::kFalse;
  tnode_contexts_.emplace_back(tnode);
  return &tnode_contexts_.back();
}

//--------------------------------------------------------------------------------------------------
Status ExecContext::StartTransaction(
    const IsolationLevel isolation_level, QLEnv* ql_env, Rescheduler* rescheduler) {
  TRACE("Start Transaction");
  transaction_start_time_ = MonoTime::Now();
  if (!transaction_) {
    transaction_ = VERIFY_RESULT(ql_env->NewTransaction(transaction_, isolation_level));
  } else if (transaction_->IsRestartRequired()) {
    transaction_ = VERIFY_RESULT(transaction_->CreateRestartedTransaction());
  } else {
    // If there is no need to start or restart transaction, just return. This can happen to DMLs on
    // a table with secondary index inside a "BEGIN TRANSACTION ... END TRANSACTION" block. Each DML
    // will try to start a transaction "on-demand" and we will use the shared transaction already
    // started by "BEGIN TRANSACTION".
    return Status::OK();
  }

  if (!transactional_session_) {
    transactional_session_ = ql_env->NewSession();
    transactional_session_->SetReadPoint(client::Restart::kFalse);
  }
  transactional_session_->SetDeadline(rescheduler->GetDeadline());
  transactional_session_->SetTransaction(transaction_);

  return Status::OK();
}

Status ExecContext::PrepareChildTransaction(
    CoarseTimePoint deadline, ChildTransactionDataPB* data) {
  ChildTransactionDataPB result =
      VERIFY_RESULT(DCHECK_NOTNULL(transaction_.get())->PrepareChildFuture(
           client::ForceConsistentRead::kTrue,
           deadline).get());
  *data = std::move(result);
  return Status::OK();
}

Status ExecContext::ApplyChildTransactionResult(const ChildTransactionResultPB& result) {
  return DCHECK_NOTNULL(transaction_.get())->ApplyChildResult(result);
}

void ExecContext::CommitTransaction(CoarseTimePoint deadline, CommitCallback callback) {
  if (!transaction_) {
    LOG(DFATAL) << "No transaction to commit";
    return;
  }

  // Clear the transaction from the session before committing the transaction. SetTransaction()
  // must be called before the Commit() call instead of after because when the commit callback is
  // invoked, it will finish the current transaction, return the response and make the CQLProcessor
  // available for the next statement and its operations would be aborted by SetTransaction().
  transactional_session_->SetTransaction(nullptr);
  transactional_session_ = nullptr;

  YBTransactionPtr transaction = std::move(transaction_);
  TRACE("Commit Transaction");
  transaction->Commit(deadline, std::move(callback));
}

void ExecContext::AbortTransaction() {
  if (!transaction_) {
    LOG(DFATAL) << "No transaction to abort";
    return;
  }

  // Abort the session and clear the transaction from the session before aborting the transaction.
  transactional_session_->Abort();
  transactional_session_->SetTransaction(nullptr);
  transactional_session_ = nullptr;

  YBTransactionPtr transaction = std::move(transaction_);
  TRACE("Abort Transaction");
  transaction->Abort();
}

bool ExecContext::HasPendingOperations() const {
  for (const auto& tnode_context : tnode_contexts_) {
    if (tnode_context.HasPendingOperations()) {
      return true;
    }
  }
  return false;
}

class AbortTransactionTask : public rpc::ThreadPoolTask {
 public:
  explicit AbortTransactionTask(YBTransactionPtr transaction)
      : transaction_(std::move(transaction)) {}

  void Run() override {
    transaction_->Abort();
    transaction_ = nullptr;
  }

  void Done(const Status& status) override {
    delete this;
  }

  virtual ~AbortTransactionTask() {
  }

 private:
  YBTransactionPtr transaction_;
};

//--------------------------------------------------------------------------------------------------
void ExecContext::Reset(const Restart restart, Rescheduler* rescheduler) {
  if (transactional_session_) {
    transactional_session_->Abort();
    transactional_session_->SetTransaction(nullptr);
  }
  if (transaction_ && !(transaction_->IsRestartRequired() && restart)) {
    YBTransactionPtr transaction = std::move(transaction_);
    TRACE("Abort Transaction");
    if (rescheduler && rescheduler->NeedReschedule()) {
      rescheduler->Reschedule(new AbortTransactionTask(std::move(transaction)));
    } else {
      transaction->Abort();
    }
  }
  restart_ = restart;
  tnode_contexts_.clear();
  if (restart) {
    num_retries_++;
  }
}

//--------------------------------------------------------------------------------------------------
TnodeContext::TnodeContext(const TreeNode* tnode) : tnode_(tnode), start_time_(MonoTime::Now()) {
}

Status TnodeContext::AppendRowsResult(RowsResult::SharedPtr&& rows_result) {
  if (!rows_result) {
    return Status::OK();
  }
  row_count_ += VERIFY_RESULT(QLRowBlock::GetRowCount(YQL_CLIENT_CQL, rows_result->rows_data()));
  if (rows_result_ == nullptr) {
    rows_result_ = std::move(rows_result);
    return Status::OK();
  }
  return rows_result_->Append(std::move(*rows_result));
}

void TnodeContext::InitializePartition(QLReadRequestPB *req, uint64_t start_partition) {
  current_partition_index_ = start_partition;
  // Hash values before the first 'IN' condition will be already set.
  // hash_values_options_ vector starts from the first column with an 'IN' restriction.
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6":
  // hashed_column_values() will be [1] and hash_values_options_ will be [[2,3],[4,5],[6]].
  int set_cols_size = req->hashed_column_values().size();
  int unset_cols_size = hash_values_options_->size();

  // Initialize the missing columns with default values (e.g. h2, h3, h4 in example above).
  req->mutable_hashed_column_values()->Reserve(set_cols_size + unset_cols_size);
  for (int i = 0; i < unset_cols_size; i++) {
    req->add_hashed_column_values();
  }

  // Set the right values for the missing/unset columns by converting partition index into positions
  // for each hash column and using the corresponding values from the hash values options vector.
  // E.g. In example above, with start_partition = 0:
  //    h4 = 6 since pos is "0 % 1 = 0", (start_position becomes 0 / 1 = 0).
  //    h3 = 4 since pos is "0 % 2 = 0", (start_position becomes 0 / 2 = 0).
  //    h2 = 2 since pos is "0 % 2 = 0", (start_position becomes 0 / 2 = 0).
  for (int i = unset_cols_size - 1; i >= 0; i--) {
    const auto& options = (*hash_values_options_)[i];
    int pos = start_partition % options.size();
    *req->mutable_hashed_column_values(i + set_cols_size) = options[pos];
    start_partition /= options.size();
  }
}

void TnodeContext::AdvanceToNextPartition(QLReadRequestPB *req) {
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6" partition index 2:
  // this will do, index: 2 -> 3 and hashed_column_values(): [1, 3, 4, 6] -> [1, 3, 5, 6].
  current_partition_index_++;
  uint64_t partition_counter = current_partition_index_;
  // Hash_values_options_ vector starts from the first column with an 'IN' restriction.
  const int hash_key_size = req->hashed_column_values().size();
  const int fixed_cols_size = hash_key_size - hash_values_options_->size();

  // Set the right values for the missing/unset columns by converting partition index into positions
  // for each hash column and using the corresponding values from the hash values options vector.
  // E.g. In example above, with start_partition = 3:
  //    h4 = 6 since pos is "3 % 1 = 0", new partition counter is "3 / 1 = 3".
  //    h3 = 5 since pos is "3 % 2 = 1", pos is non-zero which guarantees previous cols don't need
  //    to be changed (i.e. are the same as for previous partition index) so we break.
  for (int i = hash_key_size - 1; i >= fixed_cols_size; i--) {
    const auto& options = (*hash_values_options_)[i - fixed_cols_size];
    int pos = partition_counter % options.size();
    *req->mutable_hashed_column_values(i) = options[pos];
    if (pos != 0) break; // The previous position hash values must be unchanged.
    partition_counter /= options.size();
  }

  req->clear_hash_code();
  req->clear_max_hash_code();
}

bool TnodeContext::HasPendingOperations() const {
  for (const auto& op : ops_) {
    if (!op->response().has_status()) {
      return true;
    }
  }
  if (child_context_) {
    return child_context_->HasPendingOperations();
  }
  return false;
}

void TnodeContext::SetUncoveredSelectOp(const YBqlReadOpPtr& select_op) {
  uncovered_select_op_ = select_op;
  const Schema& schema = static_cast<const PTSelectStmt*>(tnode_)->table()->InternalSchema();
  std::vector<ColumnId> key_column_ids;
  key_column_ids.reserve(schema.num_key_columns());
  for (size_t idx = 0; idx < schema.num_key_columns(); idx++) {
    key_column_ids.emplace_back(schema.column_id(idx));
  }
  keys_ = std::make_unique<QLRowBlock>(schema, key_column_ids);
}

}  // namespace ql
}  // namespace yb
