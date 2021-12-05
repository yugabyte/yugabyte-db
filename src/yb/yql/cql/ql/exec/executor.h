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
// Entry point for the execution process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_EXEC_EXECUTOR_H_
#define YB_YQL_CQL_QL_EXEC_EXECUTOR_H_

#include <mutex>
#include <vector>

#include <rapidjson/document.h>

#include "yb/client/yb_op.h"

#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_type.h"

#include "yb/gutil/callback.h"

#include "yb/rpc/thread_pool.h"

#include "yb/util/memory/mc_types.h"

#include "yb/yql/cql/ql/exec/exec_fwd.h"
#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/ptree/pt_expr_types.h"
#include "yb/yql/cql/ql/util/util_fwd.h"
#include "yb/yql/cql/ql/util/statement_result.h"

namespace yb {

namespace client {
class YBColumnSpec;
} // namespace client

namespace ql {

namespace audit {

class AuditLogger;

}

class QLMetrics;

class Executor : public QLExprExecutor {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Executor> UniPtr;
  typedef std::unique_ptr<const Executor> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Executor(QLEnv *ql_env, audit::AuditLogger* audit_logger, Rescheduler* rescheduler,
           const QLMetrics* ql_metrics);
  virtual ~Executor();

  // Execute the given statement (parse tree) or batch. The parse trees and the parameters must not
  // be destroyed until the statements have been executed.
  void ExecuteAsync(const ParseTree& parse_tree, const StatementParameters& params,
                    StatementExecutedCallback cb);
  void ExecuteAsync(const StatementBatch& batch, StatementExecutedCallback cb);

  void Shutdown();

  static constexpr int64_t kAsyncCallsIdle = -1;

 private:
  class ResetAsyncCalls {
   public:
    explicit ResetAsyncCalls(std::atomic<int64_t>* num_async_calls);

    ResetAsyncCalls(const ResetAsyncCalls&) = delete;
    void operator=(const ResetAsyncCalls&) = delete;

    ResetAsyncCalls(ResetAsyncCalls&& rhs);
    void operator=(ResetAsyncCalls&& rhs);

    bool empty() const {
      return num_async_calls_ == nullptr;
    }

    void Cancel();
    void Perform();

    ~ResetAsyncCalls();

   private:
    std::atomic<int64_t>* num_async_calls_;
  };

  ResetAsyncCalls PrepareExecuteAsync();

  bool HasAsyncCalls();

  //------------------------------------------------------------------------------------------------
  // Currently, we don't yet have code generator into byte code, so the following ExecTNode()
  // functions are operating directly on the parse tree.
  // Execute a parse tree.
  CHECKED_STATUS Execute(const ParseTree& parse_tree, const StatementParameters& params);

  // Run runtime analysis and prepare for execution within the execution context.
  // Serves for processing things unavailable for initial semantic analysis.
  CHECKED_STATUS PreExecTreeNode(TreeNode *tnode);

  CHECKED_STATUS PreExecTreeNode(PTInsertStmt *tnode);

  CHECKED_STATUS PreExecTreeNode(PTInsertJsonClause *tnode);

  // Convert JSON value to an expression acording to its given expected type
  Result<PTExprPtr> ConvertJsonToExpr(const rapidjson::Value& json_value,
                                      const QLType::SharedPtr& type,
                                      const YBLocationPtr& loc);

  Result<PTExprPtr> ConvertJsonToExprInner(const rapidjson::Value& json_value,
                                           const QLType::SharedPtr& type,
                                           const YBLocationPtr& loc);

  // Execute any TreeNode. This function determines how to execute a node.
  CHECKED_STATUS ExecTreeNode(const TreeNode *tnode);

  // Execute a list of statements.
  CHECKED_STATUS ExecPTNode(const PTListNode *tnode);

  CHECKED_STATUS GetOffsetOrLimit(
      const PTSelectStmt* tnode,
      const std::function<PTExprPtr(const PTSelectStmt* tnode)>& get_val,
      const string& clause_type,
      int32_t* value);

  // Create a table (including index table for CREATE INDEX).
  CHECKED_STATUS ExecPTNode(const PTCreateTable *tnode);
  CHECKED_STATUS AddColumnToIndexInfo(IndexInfoPB *index_info, const PTColumnDefinition *column);

  // Alter a table.
  CHECKED_STATUS ExecPTNode(const PTAlterTable *tnode);

  // Drop a table.
  CHECKED_STATUS ExecPTNode(const PTDropStmt *tnode);

  // Create a user-defined type.
  CHECKED_STATUS ExecPTNode(const PTCreateType *tnode);

  // Creates a role.
  CHECKED_STATUS ExecPTNode(const PTCreateRole *tnode);

  // Alter an existing role.
  CHECKED_STATUS ExecPTNode(const PTAlterRole *tnode);

  // Grants or revokes a role to another role.
  CHECKED_STATUS ExecPTNode(const PTGrantRevokeRole* tnode);

  // Grants or revokes permissions to resources (roles/tables/keyspaces).
  CHECKED_STATUS ExecPTNode(const PTGrantRevokePermission* tnode);

  // Select statement.
  CHECKED_STATUS ExecPTNode(const PTSelectStmt *tnode, TnodeContext* tnode_context);

  // Insert statement.
  CHECKED_STATUS ExecPTNode(const PTInsertStmt *tnode, TnodeContext* tnode_context);

  // Delete statement.
  CHECKED_STATUS ExecPTNode(const PTDeleteStmt *tnode, TnodeContext* tnode_context);

  // Update statement.
  CHECKED_STATUS ExecPTNode(const PTUpdateStmt *tnode, TnodeContext* tnode_context);

  // Explain statement.
  CHECKED_STATUS ExecPTNode(const PTExplainStmt *tnode);

  // Truncate statement.
  CHECKED_STATUS ExecPTNode(const PTTruncateStmt *tnode);

  // Start a transaction.
  CHECKED_STATUS ExecPTNode(const PTStartTransaction *tnode);

  // Commit a transaction.
  CHECKED_STATUS ExecPTNode(const PTCommit *tnode);

  // Create a keyspace.
  CHECKED_STATUS ExecPTNode(const PTCreateKeyspace *tnode);

  // Use a keyspace.
  CHECKED_STATUS ExecPTNode(const PTUseKeyspace *tnode);

  // Alter a keyspace.
  CHECKED_STATUS ExecPTNode(const PTAlterKeyspace *tnode);

  //------------------------------------------------------------------------------------------------
  // Result processing.

  // Returns the YBSession for the statement in execution.
  client::YBSessionPtr GetSession(ExecContext* exec_context);

  // Flush operations that have been applied and commit. If there is none, finish the statement
  // execution.
  void FlushAsync(ResetAsyncCalls* reset_async_calls);

  // Callback for FlushAsync.
  void FlushAsyncDone(client::FlushStatus* s, ExecContext* exec_context = nullptr);

  // Callback for Commit.
  void CommitDone(Status s, ExecContext* exec_context);

  // Process async results from FlushAsync and Commit.
  void ProcessAsyncResults(bool rescheduled, ResetAsyncCalls* reset_async_calls);

  // Process async results from FlushAsync and Commit for a tnode. Returns true if there are new ops
  // being buffered to be flushed.
  Result<bool> ProcessTnodeResults(TnodeContext* tnode_context);

  // Process the status of executing a statement.
  CHECKED_STATUS ProcessStatementStatus(const ParseTree& parse_tree, const Status& s);

  // Process the read/write op status.
  CHECKED_STATUS ProcessOpStatus(const PTDmlStmt* stmt,
                                 const client::YBqlOpPtr& op,
                                 ExecContext* exec_context);

  std::shared_ptr<client::YBTable> GetTableFromStatement(const TreeNode *tnode) const;

  // Process status of FlushAsyncDone.
  using OpErrors = std::unordered_map<const client::YBqlOp*, Status>;
  CHECKED_STATUS ProcessAsyncStatus(const OpErrors& op_errors, ExecContext* exec_context);

  // Append rows result.
  CHECKED_STATUS AppendRowsResult(RowsResult::SharedPtr&& rows_result);

  // Read paging state from user's StatementParams.
  Result<QueryPagingState*> LoadPagingStateFromUser(const PTSelectStmt* tnode,
                                                    TnodeContext* tnode_context);

  // When request does not need to be executed, create and return empty result (0 row) to users.
  CHECKED_STATUS GenerateEmptyResult(const PTSelectStmt* tnode);

  // Continue a multi-partition select (e.g. table scan or query with 'IN' condition on hash cols).
  Result<bool> FetchMoreRows(const PTSelectStmt* tnode,
                             const client::YBqlReadOpPtr& op,
                             TnodeContext* tnode_context,
                             ExecContext* exec_context);

  // Fetch rows for a select statement using primary keys selected from an uncovered index.
  Result<bool> FetchRowsByKeys(const PTSelectStmt* tnode,
                               const client::YBqlReadOpPtr& select_op,
                               const QLRowBlock& keys,
                               TnodeContext* tnode_context);

  // Aggregate all result sets from all tablet servers to form the requested resultset.
  CHECKED_STATUS AggregateResultSets(const PTSelectStmt* pt_select, TnodeContext* tnode_context);
  CHECKED_STATUS EvalCount(const std::shared_ptr<QLRowBlock>& row_block,
                           int column_index,
                           QLValue *ql_value);
  CHECKED_STATUS EvalMax(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         QLValue *ql_value);
  CHECKED_STATUS EvalMin(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         QLValue *ql_value);
  CHECKED_STATUS EvalSum(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         DataType data_type,
                         QLValue *ql_value);
  CHECKED_STATUS EvalAvg(const std::shared_ptr<QLRowBlock>& row_block,
                         int column_index,
                         DataType data_type,
                         QLValue *ql_value);

  // Invoke statement executed callback.
  void StatementExecuted(const Status& s, ResetAsyncCalls* reset_async_calls);

  // Reset execution state.
  void Reset(ResetAsyncCalls* reset_async_calls);

  //------------------------------------------------------------------------------------------------
  // Expression evaluation.

  // CHECKED_STATUS EvalTimeUuidExpr(const PTExpr::SharedPtr& expr, EvalTimeUuidValue *result);
  // CHECKED_STATUS ConvertFromTimeUuid(EvalValue *result, const EvalTimeUuidValue& uuid_value);
  CHECKED_STATUS PTExprToPB(const PTExprPtr& expr, QLExpressionPB *expr_pb);

  // Constant expressions.
  CHECKED_STATUS PTConstToPB(const PTExprPtr& const_pt, QLValuePB *const_pb,
                             bool negate = false);
  CHECKED_STATUS PTExprToPB(const PTConstVarInt *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstDecimal *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstInt *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstDouble *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstText *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstBool *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstUuid *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstBinary *const_pt, QLValuePB *const_pb);

  // Bind variable.
  CHECKED_STATUS PTExprToPB(const PTBindVar *bind_pt, QLExpressionPB *bind_pb);

  // Column types.
  CHECKED_STATUS PTExprToPB(const PTRef *ref_pt, QLExpressionPB *ref_pb);
  CHECKED_STATUS PTExprToPB(const PTSubscriptedColumn *ref_pt, QLExpressionPB *ref_pb);
  CHECKED_STATUS PTExprToPB(const PTJsonColumnWithOperators *ref_pt, QLExpressionPB *ref_pb);
  CHECKED_STATUS PTExprToPB(const PTAllColumns *ref_all, QLReadRequestPB *req);

  // Operators.
  // There's only one, so call it PTUMinus for now.
  CHECKED_STATUS PTUMinusToPB(const PTOperator1 *op_pt, QLExpressionPB *op_pb);
  CHECKED_STATUS PTUMinusToPB(const PTOperator1 *op_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTJsonOperatorToPB(const PTJsonOperatorPtr& json_pt,
                                    QLJsonOperationPB *op_pb);

  // Builtin calls.
  // Even though BFCall and TSCall are processed similarly in executor at this point because they
  // have similar protobuf, it is best not to merge the two functions "BFCallToPB" and "TSCallToPB"
  // into one. That way, coding changes to one case doesn't affect the other in the future.
  CHECKED_STATUS PTExprToPB(const PTBcall *bcall_pt, QLExpressionPB *bcall_pb);
  CHECKED_STATUS BFCallToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb);
  CHECKED_STATUS TSCallToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb);

  // Constructors for collection and UDT.
  CHECKED_STATUS PTExprToPB(const PTCollectionExpr *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTCollectionExpr *expr_pt, QLExpressionPB *expr_pb);

  // Logic expressions.
  CHECKED_STATUS PTExprToPB(const PTLogic1 *logic_pt, QLExpressionPB *logic_pb);
  CHECKED_STATUS PTExprToPB(const PTLogic2 *logic_pt, QLExpressionPB *logic_pb);

  // Relation expressions.
  CHECKED_STATUS PTExprToPB(const PTRelation0 *relation_pt, QLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation1 *relation_pt, QLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation2 *relation_pt, QLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation3 *relation_pt, QLExpressionPB *relation_pb);

  //------------------------------------------------------------------------------------------------

  // Set the time to live for the values affected by the current write request.
  CHECKED_STATUS TtlToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req);

  // Set the timestamp for the values affected by the current write request.
  CHECKED_STATUS TimestampToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req);

  // Convert PTExpr to appropriate QLExpressionPB with appropriate validation.
  CHECKED_STATUS PTExprToPBValidated(const PTExprPtr& expr, QLExpressionPB *expr_pb);

  //------------------------------------------------------------------------------------------------
  // Column evaluation.

  // Convert column references to protobuf.
  CHECKED_STATUS ColumnRefsToPB(const PTDmlStmt *tnode, QLReferencedColumnsPB *columns_pb);

  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req);

  // Convert INSERT JSON clause to protobuf.
  CHECKED_STATUS InsertJsonClauseToPB(const PTInsertStmt *insert_stmt,
                                      const PTInsertJsonClause *json_clause,
                                      QLWriteRequestPB *req);

  //------------------------------------------------------------------------------------------------
  // Where clause evaluation.

  // Convert where clause to protobuf for read request.
  Result<uint64_t> WhereClauseToPB(QLReadRequestPB *req,
                                   const MCVector<ColumnOp>& key_where_ops,
                                   const MCList<ColumnOp>& where_ops,
                                   const MCList<SubscriptedColumnOp>& subcol_where_ops,
                                   const MCList<JsonColumnOp>& jsoncol_where_ops,
                                   const MCList<PartitionKeyOp>& partition_key_ops,
                                   const MCList<FuncOp>& func_ops,
                                   TnodeContext* tnode_context);

  // Convert where clause to protobuf for write request.
  CHECKED_STATUS WhereClauseToPB(QLWriteRequestPB *req,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops);

  // Set a primary key in a read request.
  CHECKED_STATUS WhereKeyToPB(QLReadRequestPB *req, const Schema& schema, const QLRow& key);

  // Convert an expression op in where clause to protobuf.
  CHECKED_STATUS WhereOpToPB(QLConditionPB *condition, const ColumnOp& col_op);
  CHECKED_STATUS WhereSubColOpToPB(QLConditionPB *condition, const SubscriptedColumnOp& subcol_op);
  CHECKED_STATUS WhereJsonColOpToPB(QLConditionPB *condition, const JsonColumnOp& jsoncol_op);
  CHECKED_STATUS FuncOpToPB(QLConditionPB *condition, const FuncOp& func_op);

  //------------------------------------------------------------------------------------------------
  // Add a read/write operation for the current statement and apply it. For write operation, check
  // for inter-dependency before applying. If it is a write operation to a table with secondary
  // indexes, update them as needed.
  void AddOperation(const client::YBqlReadOpPtr& op, TnodeContext *tnode_context);
  CHECKED_STATUS AddOperation(const client::YBqlWriteOpPtr& op, TnodeContext *tnode_context);

  // Is this a batch returning status?
  bool IsReturnsStatusBatch() const {
    return returns_status_batch_opt_ && *returns_status_batch_opt_;
  }

  //------------------------------------------------------------------------------------------------
  CHECKED_STATUS UpdateIndexes(const PTDmlStmt *tnode,
                               QLWriteRequestPB *req,
                               TnodeContext* tnode_context);
  CHECKED_STATUS AddIndexWriteOps(const PTDmlStmt *tnode,
                                  const QLWriteRequestPB& req,
                                  TnodeContext* tnode_context);

  int64_t num_async_calls() const {
    return num_async_calls_.load(std::memory_order_acquire);
  }

  //------------------------------------------------------------------------------------------------
  // Helper class to separate inter-dependent write operations.
  class WriteBatch {
   public:
    // Add a write operation. Returns true if it does not depend on another operation in the batch.
    // Returns false if it does and is not added. In that case, the operation needs to be deferred
    // until the dependent operation has been applied.
    bool Add(const client::YBqlWriteOpPtr& op,
             const TnodeContext* tnode_context,
             ExecContext* exec_context);

    // Clear the batch.
    void Clear();

    // Check if the batch is empty.
    bool Empty() const;

   private:
    // Sets of write operations separated by their primary and keys.
    std::unordered_set<client::YBqlWriteOpPtr,
                       client::YBqlWritePrimaryKeyComparator,
                       client::YBqlWritePrimaryKeyComparator> ops_by_primary_key_;
    std::unordered_set<client::YBqlWriteOpPtr,
                       client::YBqlWriteHashKeyComparator,
                       client::YBqlWriteHashKeyComparator> ops_by_hash_key_;
  };

  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) for executing statements.
  QLEnv *ql_env_;

  // Used for logging audit records.
  audit::AuditLogger& audit_logger_;

  // A rescheduler to reschedule the current call.
  Rescheduler* const rescheduler_;

  // Execution context of the statement currently being executed, and the contexts for all
  // statements in execution. The contexts are created and destroyed for each execution.
  ExecContext* exec_context_ = nullptr;
  std::list<ExecContext> exec_contexts_;

  // Batch of outstanding write operations that are being applied.
  WriteBatch write_batch_;

  // Session to apply non-transactional read/write operations. Transactional read/write operations
  // are applied using the corresponding transactional session in ExecContext.
  const client::YBSessionPtr session_;

  // The number of outstanding async calls pending. 0 means that we are processing result of all
  // calls, -1 (kAsyncCallsIdle) means that this executor is idle.
  std::atomic<int64_t> num_async_calls_ = {kAsyncCallsIdle};

  // The async error status and the mutex to protect its update.
  std::mutex status_mutex_;
  Status async_status_;

  // The number of FlushAsync called to execute the statements.
  int64_t num_flushes_ = 0;

  // Execution result.
  ExecutedResult::SharedPtr result_;

  // Statement executed callback.
  StatementExecutedCallback cb_;

  // QLMetrics to keep track of node parsing etc.
  const QLMetrics* ql_metrics_;

  // Whether this is a batch with statements that returns status.
  boost::optional<bool> returns_status_batch_opt_;

  class ExecutorTask : public rpc::ThreadPoolTask {
   public:
    ExecutorTask& Bind(Executor* executor, ResetAsyncCalls* reset_async_calls);

    virtual ~ExecutorTask() = default;

   private:
    void Run() override;
    void Done(const Status& status) override;
    virtual void DoRun(Executor* executor, ResetAsyncCalls* reset_async_calls) = 0;

    Executor* executor_ = nullptr;
    ResetAsyncCalls reset_async_calls_{nullptr};
  };

  class ProcessAsyncResultsTask : public ExecutorTask {
   public:
    void DoRun(Executor* executor, ResetAsyncCalls* reset_async_calls) override {
      executor->ProcessAsyncResults(true /* rescheduled */, reset_async_calls);
    }
  };

  friend class ProcessAsyncResultsTask;

  ProcessAsyncResultsTask process_async_results_task_;

  class FlushAsyncTask : public ExecutorTask {
   private:
    void DoRun(Executor* executor, ResetAsyncCalls* reset_async_calls) override {
      executor->FlushAsync(reset_async_calls);
    }
  };

  friend class FlushAsyncTask;

  FlushAsyncTask flush_async_task_;
};

// Normalize the JSON object key according to CQL rules:
// Key is made lowercase unless it's double-quoted - in which case double quotes are removed
std::string NormalizeJsonKey(const std::string& key);

// Create an appropriate QLExpressionPB depending on a column description
QLExpressionPB* CreateQLExpression(QLWriteRequestPB *req, const ColumnDesc& col_desc);

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_EXEC_EXECUTOR_H_
