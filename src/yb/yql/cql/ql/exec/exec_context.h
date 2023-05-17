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

#pragma once

#include <string>

#include <rapidjson/document.h>

#include "yb/client/session.h"

#include "yb/common/ql_protocol.pb.h"

#include "yb/util/status_fwd.h"

#include "yb/yql/cql/ql/exec/exec_fwd.h"
#include "yb/yql/cql/ql/ptree/process_context.h"
#include "yb/yql/cql/ql/util/statement_result.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// In addition to actual data, a CQL result contains a paging state for the CURSOR position.
//
// QueryPagingState represents the processing status in both CQL and DocDB. This class is used
// for those purposes.
//
// 1. Load paging state from users and docdb.
//    - When users send request, they sent StatementParams that contain user data and a paging
//      state that indicates the status the statement execution.
//    - When CQL gets the request, it loads user-provided paging state to ql::QueryPagingState.
//
// 2. Compose paging_state when sending replies to users.
//    - When responding to user, in addition to row-data, CQL construct paging-state of its
//      processing status, and CQL uses info in ql::SelectPagingState to construct the response.
//    - In subsequent requests, users will send this info back to CQL together with user-data.
//
// 3. Load paging state from docdb.
//    - In addition to actual row data, when DocDB replies to CQL, it sends back its paging state
//      to indicate the status of DocDB's processing.
//    - CQL will load DocDB's paging state into ql::QueryPagingState.
//    - In subsequent READ requests, CQL sends back paging state to DocDB.
//
// 4. Compose paging_state when sending request to DocDB.
//    - When sending requests to DocDB, CQL sends a paging state in addition to user data.
//    - CQL uses info in QueryPagingState to construct DocDB's paging state.
class QueryPagingState {
 public:
  typedef std::unique_ptr<QueryPagingState> UniPtr;

  // Constructing paging_state from given user_params.
  QueryPagingState(const StatementParameters& user_params, bool is_top_level_select);

  // Clear paging state.
  // NOTE:
  // - Clear only query_pb_.
  // - The counter_pb_ are used only in this class and should not be cleared.
  // - DocDB as well as users do not know of this counter.
  void ClearPagingState();

  // Load the paging state in user request.
  void LoadPagingStateFromUser(const StatementParameters& params,
                                              bool is_top_level_read_node);

  // Compose paging state to send to users.
  Status ComposePagingStateForUser();
  Status ComposePagingStateForUser(const QLPagingStatePB& child_state);
  Status ComposePagingStateForUser(const QLPagingStatePB& child_state,
                                   uint32_t overridden_schema_version);

  // Load the paging state in DocDB responses.
  Status LoadPagingStateFromDocdb(const RowsResult::SharedPtr& rows_result,
                                  int64_t number_of_new_rows,
                                  bool has_nested_query);

  // Access functions to query_pb_.
  const std::string& table_id() const {
    return query_pb_.table_id();
  }

  void set_table_id(const std::string& val) {
    query_pb_.set_table_id(val);
  }

  const std::string& next_partition_key() const {
    return query_pb_.next_partition_key();
  }

  void set_next_partition_key(const std::string& val) {
    query_pb_.set_next_partition_key(val);
  }

  const std::string& next_row_key() const {
    return query_pb_.next_row_key();
  }

  void set_next_row_key(const std::string& val) {
    query_pb_.set_next_row_key(val);
  }

  int64_t total_num_rows_read() const {
    return query_pb_.total_num_rows_read();
  }

  void set_total_num_rows_read(int64_t val) {
    query_pb_.set_total_num_rows_read(val);
  }

  int64_t total_rows_skipped() const {
    return query_pb_.total_rows_skipped();
  }

  void set_total_rows_skipped(int64_t val) {
    query_pb_.set_total_rows_skipped(val);
  }

  int64_t next_partition_index() const {
    return query_pb_.next_partition_index();
  }

  void set_next_partition_index(int64_t val) {
    query_pb_.set_next_partition_index(val);
  }

  // It appears the folliwng fields are not used.
  void set_original_request_id(int64_t val) {
    query_pb_.set_original_request_id(val);
  }

  // Access function to counter_pb_ - Predicate for (LIMIT, OFFSET).
  bool has_select_limit() const {
    return counter_pb_.has_select_limit();
  }

  bool has_select_offset() const {
    return counter_pb_.has_select_offset();
  }

  // row-read counters.
  void set_read_count(int64_t val) {
    counter_pb_.set_read_count(val);
  }

  int64_t read_count() const {
    return counter_pb_.read_count();
  }

  // row-skip counter.
  void set_skip_count(int64_t val) {
    counter_pb_.set_skip_count(val);
  }

  int64_t skip_count() const {
    return counter_pb_.skip_count();
  }

  // row limit counter processing.
  void set_select_limit(int64_t val) {
    counter_pb_.set_select_limit(val);
  }

  int64_t select_limit() const {
    return counter_pb_.select_limit();
  }

  bool reached_select_limit() const {
    return counter_pb_.has_select_limit() && read_count() >= select_limit();
  }

  // row offset counter processing.
  void set_select_offset(int64_t val) {
    counter_pb_.set_select_offset(val);
  }

  int64_t select_offset() const {
    return counter_pb_.select_offset();
  }

  bool reached_select_offset() const {
    return !has_select_offset() || skip_count() >= select_offset();
  }

  // Debug logging.
  std::string DebugString() const {
    return (std::string("\nQueryPB = {\n") + query_pb_.DebugString() + std::string ("\n};") +
            std::string("\nCounterPB = {\n") + counter_pb_.DebugString() + std::string("\n};"));
  }

  // Access to internal protobuf.
  const QLPagingStatePB& query_pb() const {
    return query_pb_;
  }

  const QLSelectRowCounterPB& counter_pb() const {
    return counter_pb_;
  }

  int64_t max_fetch_size() const {
    return max_fetch_size_;
  }

  // Users can indicate how many rows can be read at one time. If a SELECT statement has its own
  // LIMIT, the users' setting will be adjusted to the SELECT's LIMIT.
  void AdjustMaxFetchSizeToSelectLimit();

 private:
  // Query paging state.
  // - Paging state to be exchanged with DocDB and User.
  // - When loading data from users and DocDB, all information in the query_pb_ will be overwritten.
  //   All information are are needed for CQL processing must be kept separately from this variable.
  QLPagingStatePB query_pb_;

  // Row counter.
  // - Processed by CQL and should not be overwritten when loading status from users or docdb.
  // - Only top-level SELECT uses RowCounter.
  //   Example:
  //     SELECT * FROM <table> WHERE keys IN (SELECT keys FROM <index>)
  //   From user's point of view, only number of rows being read from <table> is meaningful, so
  //   clauses like LIMIT and OFFSET should be applied to top-level select row-count.
  // - NOTE:
  //   For fully-covered index query, the nested index query is promoted to top-level read node.
  //   In this scenarios, the INDEX becomes the primary table, and the counter for nested query
  //   is sent back to user.
  QLSelectRowCounterPB counter_pb_;

  // The maximum number of rows that user can receive at one time.
  //   max row number = MIN (<limit>, <page-size>)
  // If it is (-1), we fetch all of them.
  int64_t max_fetch_size_ = -1;
};

class TnodeContext {
 public:
  explicit TnodeContext(const TreeNode* tnode);

  ~TnodeContext();

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

  // Append rows result that was sent back by DocDB to this node.
  Status AppendRowsResult(RowsResult::SharedPtr&& rows_result);

  // Create CQL paging state based on user's information.
  // When calling YugaByte, users provide all info in StatementParameters including paging state.
  QueryPagingState *CreateQueryState(const StatementParameters& user_params,
                                     bool is_top_level_select);

  // Clear paging state when the query reaches the end of scan.
  Status ClearQueryState();

  QueryPagingState *query_state() {
    return query_state_.get();
  }

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
  void InitializePartition(QLReadRequestPB *req, bool continue_user_request);

  // Predicate for the completion of a partition read.
  bool FinishedReadingPartition();

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
  TnodeContext* AddChildTnode(const TreeNode* tnode);

  TnodeContext* child_context() {
    return child_context_.get();
  }
  const TnodeContext* child_context() const {
    return child_context_.get();
  }

  // Allocate and prepare parent node for reading keys from nested query.
  void SetUncoveredSelectOp(const client::YBqlReadOpPtr& select_op);

  // Access functions for uncovered select op template and primary keys.
  const client::YBqlReadOpPtr& uncovered_select_op() const {
    return uncovered_select_op_;
  }

  qlexpr::QLRowBlock* keys() {
    return keys_.get();
  }

  // Compose the final result (rows_result_) which will be sent to users.
  // - Data content: Already in rows_result_
  // - Read-response paging state: query_state_::query_pb_
  // - Row counters: query_state_::counter_pb_
  //
  // NOTE:
  // 1. For primary-indexed or sequential scan SELECT.
  //    - Data is read from primary table.
  //    - User paging state = { QueryPagingState::query_pb_ }
  //    - DocDB paging state = { QueryPagingState::query_pb_ }
  //
  // 2. Full-covered secondary-indexed SELECT
  //    - Data is read from secondary table.
  //    - Users paging state = { Nested QueryPagingState::query_pb_ }
  //    - DocDB paging state = { Nested QueryPagingState::query_pb_ }
  //
  // 3. Partial-covered secondary-indexed SELECT
  //    - Primary key is read from INDEX table (nested query).
  //    - Data is read from PRIMARY table (top level query).
  //    - When construct user paging state, we compose two paging states into one.
  //      The read state = nested query read state.
  //      The counter state = top-level query counter state.
  //      User paging state = { Nested QueryPagingState::query_pb_,
  //                            Top-Level QueryPagingState::counter_pb_ }
  Status ComposeRowsResultForUser(const TreeNode* child_select_node, bool for_new_batches);

  const boost::optional<uint32_t>& hash_code_from_partition_key_ops() {
    return hash_code_from_partition_key_ops_;
  }

  const boost::optional<uint32_t>& max_hash_code_from_partition_key_ops() {
    return max_hash_code_from_partition_key_ops_;
  }

  void set_hash_code_from_partition_key_ops(uint32_t hash_code) {
    hash_code_from_partition_key_ops_ = hash_code;
  }

  void set_max_hash_code_from_partition_key_ops(uint32_t max_hash_code) {
    max_hash_code_from_partition_key_ops_ = max_hash_code;
  }

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

  // Read paging state for each query including nested query.
  // - Only SELECT statement has a query_state_.
  // - The rest of commands has a NULL query_state_.
  QueryPagingState::UniPtr query_state_;

  // Child context for nested statement.
  std::unique_ptr<TnodeContext> child_context_;

  // Select op template and primary keys for fetching from indexed table in an uncovered query.
  client::YBqlReadOpPtr uncovered_select_op_;
  std::unique_ptr<qlexpr::QLRowBlock> keys_;

  boost::optional<uint32_t> hash_code_from_partition_key_ops_;
  boost::optional<uint32_t> max_hash_code_from_partition_key_ops_;
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
  const std::string& stmt() const override;

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
  Status StartTransaction(
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
  Status PrepareChildTransaction(CoarseTimePoint deadline, ChildTransactionDataPB* data);

  // Apply the result of a child distributed transaction.
  Status ApplyChildTransactionResult(const ChildTransactionResultPB& result);

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
