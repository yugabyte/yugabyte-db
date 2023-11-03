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

#include <boost/function.hpp>

#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/client/transaction.h"
#include "yb/client/yb_op.h"

#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/cql/ql/exec/rescheduler.h"
#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/util/statement_params.h"
#include "yb/util/flags.h"

DEFINE_UNKNOWN_int32(cql_prepare_child_threshold_ms, 2000 * yb::kTimeMultiplier,
             "Timeout if preparing for child transaction takes longer"
             "than the prescribed threshold.");

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
    transaction_ = VERIFY_RESULT(ql_env->NewTransaction(
        transaction_, isolation_level, rescheduler->GetDeadline()));
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
    transactional_session_ = ql_env->NewSession(rescheduler->GetDeadline());
    transactional_session_->RestartNonTxnReadPoint(client::Restart::kFalse);
  }
  transactional_session_->SetTransaction(transaction_);

  return Status::OK();
}

Status ExecContext::PrepareChildTransaction(
    CoarseTimePoint deadline, ChildTransactionDataPB* data) {
  auto future = DCHECK_NOTNULL(transaction_.get())->PrepareChildFuture(
      client::ForceConsistentRead::kTrue, deadline);

  // Set the deadline to be the earlier of the input deadline and the current timestamp
  // plus the waiting time for the prepare child
  auto future_deadline = std::min(
      deadline,
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cql_prepare_child_threshold_ms));

  auto future_status = future.wait_until(future_deadline);

  if (future_status == std::future_status::ready) {
    *data = VERIFY_RESULT(std::move(future).get());
    return Status::OK();
  }

  auto message = Format("Timed out waiting for prepare child status, left to deadline: $0",
                        MonoDelta(deadline - CoarseMonoClock::now()));
  LOG(INFO) << message;
  return STATUS(TimedOut, message);
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

TnodeContext::~TnodeContext() = default;

TnodeContext* TnodeContext::AddChildTnode(const TreeNode* tnode) {
  DCHECK(!child_context_);
  child_context_ = std::make_unique<TnodeContext>(tnode);
  return child_context_.get();
}

Status TnodeContext::AppendRowsResult(RowsResult::SharedPtr&& rows_result) {
  // Append data arriving from DocDB.
  // (1) SELECT without nested query.
  //  - SELECT <select_list> FROM <table or index>
  //      WHERE <filter_cond>
  //      LIMIT <limit> OFFSET <offset>
  //  - New rows are appended at the end.
  //
  // (2) SELECT with nested query.
  //  - SELECT <select_list> FROM <table>
  //      WHERE
  //        primary_key IN (SELECT primary_key FROM <index> WHERE <index_cond>)
  //        AND
  //        <filter_cond>
  //      LIMIT <limit> OFFSET <offset>
  //  - When nested INDEX query fully-covers the SELECT command, data coming from the nested node
  //    is appended without being filtered or rejected.
  //  - When nested INDEX query does NOT fully-cover the SELECT command, data is rejected if OFFSET
  //    is not yet reached. Otherwise, data is appended ONE row at a time.
  if (!rows_result) {
    return Status::OK();
  }

  int64_t number_of_new_rows =
    VERIFY_RESULT(qlexpr::QLRowBlock::GetRowCount(YQL_CLIENT_CQL, rows_result->rows_data()));

  if (query_state_) {
    RSTATUS_DCHECK(tnode_->opcode() == TreeNodeOpcode::kPTSelectStmt,
                   Corruption, "QueryPagingState is setup for non-select statement");
    const auto* select_stmt = static_cast<const PTSelectStmt *>(tnode_);

    // Save the last offset status before loading new status from DocDB.
    const bool reached_offset = query_state_->reached_select_offset();
    const bool has_nested_query = select_stmt->child_select() != nullptr;
    RETURN_NOT_OK(
      query_state_->LoadPagingStateFromDocdb(rows_result, number_of_new_rows, has_nested_query));

    if (!reached_offset && has_nested_query) {
      // Parent query needs to discard the new row when (row_count < SELECT::OFFSET).
      if (!rows_result_) {
        rows_result_ = std::make_shared<RowsResult>(select_stmt);
      }
      rows_result_->SetPagingState(std::move(*rows_result));
      return Status::OK();
    }
  }

  // Append the new rows to result.
  row_count_ += number_of_new_rows;
  if (rows_result_ == nullptr) {
    rows_result_ = std::move(rows_result);
    return Status::OK();
  }
  return rows_result_->Append(std::move(*rows_result));
}

void TnodeContext::InitializePartition(QLReadRequestPB *req, bool continue_user_request) {
  uint64_t start_partition = continue_user_request ? query_state_->next_partition_index() : 0;

  current_partition_index_ = start_partition;
  // Hash values before the first 'IN' condition will be already set.
  // hash_values_options_ vector starts from the first column with an 'IN' restriction.
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6":
  // hashed_column_values() will be [1] and hash_values_options_ will be [[2,3],[4,5],[6]].
  int set_cols_size = req->hashed_column_values().size();
  auto unset_cols_size = hash_values_options_->size();

  // Initialize the missing columns with default values (e.g. h2, h3, h4 in example above).
  req->mutable_hashed_column_values()->Reserve(narrow_cast<int>(set_cols_size + unset_cols_size));
  for (size_t i = 0; i < unset_cols_size; i++) {
    req->add_hashed_column_values();
  }

  // Set the right values for the missing/unset columns by converting partition index into positions
  // for each hash column and using the corresponding values from the hash values options vector.
  // E.g. In example above, with start_partition = 0:
  //    h4 = 6 since pos is "0 % 1 = 0", (start_position becomes 0 / 1 = 0).
  //    h3 = 4 since pos is "0 % 2 = 0", (start_position becomes 0 / 2 = 0).
  //    h2 = 2 since pos is "0 % 2 = 0", (start_position becomes 0 / 2 = 0).
  for (auto i = unset_cols_size; i > 0;) {
    --i;
    const auto& options = (*hash_values_options_)[i];
    auto pos = start_partition % options.size();
    *req->mutable_hashed_column_values(narrow_cast<int>(i + set_cols_size)) = options[pos];
    start_partition /= options.size();
  }
}

bool TnodeContext::FinishedReadingPartition() {
  return rows_result_->paging_state().empty() ||
      (query_state_->next_partition_key().empty() && query_state_->next_row_key().empty());
}

void TnodeContext::AdvanceToNextPartition(QLReadRequestPB *req) {
  // E.g. for a query "h1 = 1 and h2 in (2,3) and h3 in (4,5) and h4 = 6" partition index 2:
  // this will do, index: 2 -> 3 and hashed_column_values(): [1, 3, 4, 6] -> [1, 3, 5, 6].
  current_partition_index_++;
  uint64_t partition_counter = current_partition_index_;
  // Hash_values_options_ vector starts from the first column with an 'IN' restriction.
  const int hash_key_size = req->hashed_column_values().size();
  const auto fixed_cols_size = hash_key_size - hash_values_options_->size();

  // Set the right values for the missing/unset columns by converting partition index into positions
  // for each hash column and using the corresponding values from the hash values options vector.
  // E.g. In example above, with start_partition = 3:
  //    h4 = 6 since pos is "3 % 1 = 0", new partition counter is "3 / 1 = 3".
  //    h3 = 5 since pos is "3 % 2 = 1", pos is non-zero which guarantees previous cols don't need
  //    to be changed (i.e. are the same as for previous partition index) so we break.
  for (size_t i = hash_key_size; i > fixed_cols_size;) {
    --i;
    const auto& options = (*hash_values_options_)[i - fixed_cols_size];
    auto pos = partition_counter % options.size();
    *req->mutable_hashed_column_values(narrow_cast<int>(i)) = options[pos];
    if (pos != 0) break; // The previous position hash values must be unchanged.
    partition_counter /= options.size();
  }

  req->clear_hash_code();
  req->clear_max_hash_code();

  if (hash_code_from_partition_key_ops_.is_initialized())
    req->set_hash_code(*hash_code_from_partition_key_ops_);
  if (max_hash_code_from_partition_key_ops_.is_initialized())
    req->set_max_hash_code(*max_hash_code_from_partition_key_ops_);
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
  keys_ = std::make_unique<qlexpr::QLRowBlock>(schema, key_column_ids);
}

QueryPagingState *TnodeContext::CreateQueryState(const StatementParameters& user_params,
                                                 bool is_top_level_select) {
  query_state_ = std::make_unique<QueryPagingState>(user_params, is_top_level_select);
  return query_state_.get();
}

Status TnodeContext::ClearQueryState() {
  RSTATUS_DCHECK(query_state_, Corruption, "Query state should not be null for SELECT");
  rows_result_->ClearPagingState();
  query_state_->ClearPagingState();

  return Status::OK();
}

Status TnodeContext::ComposeRowsResultForUser(const TreeNode* child_select_node,
                                              bool for_new_batches) {
  RSTATUS_DCHECK_EQ(tnode_->opcode(), TreeNodeOpcode::kPTSelectStmt,
                    Corruption, "Only SELECT node can have nested query");
  const auto* select_stmt = static_cast<const PTSelectStmt *>(tnode_);

  // Case 1:
  //   SELECT * FROM <table>;
  if (!child_select_node) {
    if (rows_result_->has_paging_state() || for_new_batches) {
      // Paging state must be provided for two cases. Otherwise, we've reached end of result set.
      // - Docdb sent back rows_result with paging state.
      // - Seting up paging_state for user's next batches.
      RETURN_NOT_OK(query_state_->ComposePagingStateForUser());
      rows_result_->SetPagingState(query_state_->query_pb());
    }
    return Status::OK();
  }

  // Check for nested condition.
  RSTATUS_DCHECK(child_context_ && child_select_node->opcode() == TreeNodeOpcode::kPTSelectStmt,
                 Corruption, "Expecting nested context with a SELECT node");

  // Case 2:
  //   SELECT <fully_covered_columns> FROM <index>;
  // Move result from index query (child) to the table query (this parent node). This result would
  // also contain the paging state to be sent in the response. The paging state would contain the
  // schema version of the Index which should be overriden to be the schema version of the indexed
  // table. This is done so that the schema version check while fetching the next page can succeed
  // as it expects the schema version of the indexed table. This is OK to do because the schema
  // version of the indexed table changes on every ADD/DROP index operation.
  const auto* child_select = static_cast<const PTSelectStmt*>(child_select_node);
  if (child_select->covers_fully()) {
    auto child_rows_result = std::move(child_context_->rows_result());
    if (child_rows_result->has_paging_state()) {
      child_rows_result->OverrideSchemaVersionInPagingState(
          select_stmt->table()->schema().version());
    }
    return AppendRowsResult(std::move(child_rows_result));
  }

  // Case 3:
  //   SELECT <any columns> FROM <table> WHERE primary_keys IN (SELECT primary_keys FROM <index>);
  // Compose result of the following fields.
  // - The rows_result should be from this node (rows_result_).
  // - The counter_state should be from this node (query_state_::counter_pb_).
  // - The read paging_state should be from the CHILD node (query_state_::query_pb_) except the
  // schema version which should be from the indexed table.
  if (!rows_result_) {
    // Allocate an empty rows_result that will be filled with paging state.
    rows_result_ = std::make_shared<RowsResult>(select_stmt);
  }

  if (child_context_->rows_result()->has_paging_state() && !query_state_->reached_select_limit()) {
    // If child node has paging state and LIMIT is not yet reached, provide paging state to users
    // to continue reading.
    // Set the schema version from the indexed table in the paging state. This is done so that the
    // schema version check while fetching the next page can succeed as it expects the schema
    // version of the indexed table. This is OK to do because the schema version of the indexed
    // table changes on every ADD/DROP index operation.
    RETURN_NOT_OK(query_state_->ComposePagingStateForUser(
        child_context_->query_state()->query_pb(), select_stmt->table()->schema().version()));
    rows_result_->SetPagingState(query_state_->query_pb());
  } else {
    // Clear paging state once all requested rows were retrieved.
    rows_result_->ClearPagingState();
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

QueryPagingState::QueryPagingState(const StatementParameters& user_params,
                                   bool is_top_level_read_node)
    : max_fetch_size_(user_params.page_size()) {
  LoadPagingStateFromUser(user_params, is_top_level_read_node);

  // Just default it to max_int.
  if (max_fetch_size_ <= 0) {
    max_fetch_size_ = INT64_MAX;
  }
}

void QueryPagingState::AdjustMaxFetchSizeToSelectLimit() {
  int64_t limit = select_limit();
  if (limit < 0) {
    return;
  }

  int64_t count = read_count();
  if (count < limit) {
    int64_t wanted = limit - count;
    if (wanted < max_fetch_size_) {
      max_fetch_size_ = wanted;
    }
  } else {
    max_fetch_size_ = 0;
  }
}

void QueryPagingState::ClearPagingState() {
  // Clear only the paging state.
  // Keep the counter so that we knows how many rows have been processed.
  query_pb_.Clear();
}

void QueryPagingState::LoadPagingStateFromUser(const StatementParameters& user_params,
                                               bool is_top_level_read_node) {
  user_params.WritePagingState(&query_pb_);

  // Calculate "skip_count" and "read_count".
  // (1) Top level read node.
  //  - Either top-level SELECT or fully-covering INDEX query.
  //  - User "params::couter_pb_" should have the valid "counter_pb_"
  //
  // (2) Nested read node.
  //  - Zero out its counters. We don't use "counter_pb_" for this node because LIMIT and OFFSET
  //    restrictions are not applied to nested nodes.
  //  - Because nested node might be running different READ operators (with different hash values)
  //    for different calls from users, the counters from users' message are discarded here.
  if (is_top_level_read_node) {
    counter_pb_.CopyFrom(query_pb_.row_counter());
  } else {
    // These values are not used, set them to zero.
    set_skip_count(0);
    set_read_count(0);
  }
}

Status QueryPagingState::ComposePagingStateForUser() {
  // Write the counters into the paging_state.
  query_pb_.mutable_row_counter()->CopyFrom(counter_pb_);
  return Status::OK();
}

Status QueryPagingState::ComposePagingStateForUser(const QLPagingStatePB& child_state) {
  // Write child_state.
  query_pb_.CopyFrom(child_state);

  // Write the counters into the paging_state.
  query_pb_.mutable_row_counter()->CopyFrom(counter_pb_);

  return Status::OK();
}

Status QueryPagingState::ComposePagingStateForUser(const QLPagingStatePB& child_state,
                                                   uint32_t overridden_schema_version) {
  RETURN_NOT_OK(ComposePagingStateForUser(child_state));
  query_pb_.set_schema_version(overridden_schema_version);
  return Status::OK();
}

Status QueryPagingState::LoadPagingStateFromDocdb(const RowsResult::SharedPtr& rows_result,
                                                  int64_t number_of_new_rows,
                                                  bool has_nested_query) {
  // Load "query_pb_" with the latest result from DocDB.
  query_pb_.ParseFromString(rows_result->paging_state());

  // If DocDB processed the skipping rows, record it here.
  if (total_rows_skipped() > 0) {
    set_skip_count(total_rows_skipped());
  }

  if (!has_nested_query) {
    // SELECT <select_list> FROM <table or index>
    //   WHERE <filter_cond>
    //   LIMIT <limit> OFFSET <offset>
    // - DocDB processed the <limit> and <offset> restrictions.
    //   Either "reached_select_offset() == TRUE" OR number_of_new_rows == 0.
    // - Two DocDB::counters are used to compute here.
    //   . QLPagingStatePB::total_rows_skipped - Skip count in DocDB.
    //   . number_of_new_rows - Rows of data from DocDB after skipping.
    set_read_count(read_count() + number_of_new_rows);

  } else {
    // SELECT <select_list> FROM <table>
    //   WHERE
    //     primary_key IN (SELECT primary_key FROM <index> WHERE <index_cond>)
    //     AND
    //     <filter_cond>
    //   LIMIT <limit> OFFSET <offset>
    //
    // NOTE:
    // 1. Case INDEX query fully-covers the SELECT command.
    //    - DocDB counters are transfered from nested node to this node.
    //    - "reached_select_offset() == TRUE" OR number_of_new_rows == 0.
    //
    // 2. Case INDEX query does NOT fully-cover the SELECT command.
    //    - Values of <limit> and <offset> are NOT sent together with proto request to DocDB. They
    //      are computed and processed here in CQL layer.
    //    - For this case, "number_of_new_rows" is either 1 or 0.
    //      CQL assumes that outer SELECT reads at most one row at a time as it uses values of
    //      PRIMARY KEY (always unique) to read the rest of the columns of the <table>.
    if (!reached_select_offset()) {
      // Since OFFSET is processed here, this must be case 2.
      RSTATUS_DCHECK_LE(number_of_new_rows, 1, Corruption, "Incorrect counter calculation");
      set_skip_count(skip_count() + number_of_new_rows);
    } else {
      set_read_count(read_count() + number_of_new_rows);
    }
  }

  return Status::OK();
}

const std::string& ExecContext::stmt() const {
  return parse_tree_.stmt();
}

}  // namespace ql
}  // namespace yb
