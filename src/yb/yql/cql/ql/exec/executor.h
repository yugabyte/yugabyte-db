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

#include "yb/client/yb_op.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_rowblock.h"
#include "yb/common/common.pb.h"
#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/ptree/pt_create_keyspace.h"
#include "yb/yql/cql/ql/ptree/pt_use_keyspace.h"
#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_alter_table.h"
#include "yb/yql/cql/ql/ptree/pt_create_type.h"
#include "yb/yql/cql/ql/ptree/pt_create_index.h"
#include "yb/yql/cql/ql/ptree/pt_create_role.h"
#include "yb/yql/cql/ql/ptree/pt_drop.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/pt_insert.h"
#include "yb/yql/cql/ql/ptree/pt_grant.h"
#include "yb/yql/cql/ql/ptree/pt_delete.h"
#include "yb/yql/cql/ql/ptree/pt_update.h"
#include "yb/yql/cql/ql/ptree/pt_transaction.h"
#include "yb/yql/cql/ql/ptree/pt_truncate.h"
#include "yb/yql/cql/ql/util/statement_params.h"
#include "yb/yql/cql/ql/util/statement_result.h"

namespace yb {
namespace ql {

class QLMetrics;

class Executor : public QLExprExecutor {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Executor> UniPtr;
  typedef std::unique_ptr<const Executor> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Executor(QLEnv *ql_env, const QLMetrics* ql_metrics);
  virtual ~Executor();

  // Execute the given statement (parse tree).
  void ExecuteAsync(const std::string &ql_stmt, const ParseTree &parse_tree,
                    const StatementParameters* params, StatementExecutedCallback cb);

  // Batch execution of statements. StatementExecutedCallback will be invoked when the batch is
  // applied and execution is complete, or when an error occurs.
  void BeginBatch(StatementExecutedCallback cb);
  void ExecuteBatch(const std::string &ql_stmt, const ParseTree &parse_tree,
                    const StatementParameters* params);
  void ApplyBatch();
  void AbortBatch();

  // Invoke statement executed callback.
  void StatementExecuted(const Status& s);

 private:
  //------------------------------------------------------------------------------------------------
  // Currently, we don't yet have code generator into byte code, so the following ExecTNode()
  // functions are operating directly on the parse tree.

  // Execute a parse tree.
  CHECKED_STATUS Execute(const std::string &ql_stmt, const ParseTree &parse_tree,
                         const StatementParameters* params);

  // Execute any TreeNode. This function determines how to execute a node.
  CHECKED_STATUS ExecTreeNode(const TreeNode *tnode);

  // Execute a list of statements.
  CHECKED_STATUS ExecPTNode(const PTListNode *tnode);

  // Create a table (including index table for CREATE INDEX).
  CHECKED_STATUS ExecPTNode(const PTCreateTable *tnode);

  // Alter a table.
  CHECKED_STATUS ExecPTNode(const PTAlterTable *tnode);

  // Drop a table.
  CHECKED_STATUS ExecPTNode(const PTDropStmt *tnode);

  // Create a user-defined type.
  CHECKED_STATUS ExecPTNode(const PTCreateType *tnode);

  // Creates a role.
  CHECKED_STATUS ExecPTNode(const PTCreateRole *tnode);

  // Grants a role to another role.
  CHECKED_STATUS ExecPTNode(const PTGrantRole *tnode);

  // Select statement.
  CHECKED_STATUS ExecPTNode(const PTSelectStmt *tnode);

  // Select statement.
  CHECKED_STATUS ExecPTNode(const PTGrantPermission *tnode);

  // Insert statement.
  CHECKED_STATUS ExecPTNode(const PTInsertStmt *tnode);

  // Delete statement.
  CHECKED_STATUS ExecPTNode(const PTDeleteStmt *tnode);

  // Update statement.
  CHECKED_STATUS ExecPTNode(const PTUpdateStmt *tnode);

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

  // Re-execute the current statement.
  void Reexecute();

  //------------------------------------------------------------------------------------------------
  // Result processing.

  // Flush operations that have been applied. If there is none, finish the statement execution.
  void FlushAsync();

  // Callback for FlushAsync.
  void FlushAsyncDone(const Status& s, bool rescheduled_call);

  // Callback for Commit.
  void CommitDone(const Status& s);

  // Process the status of executing a statement.
  CHECKED_STATUS ProcessStatementStatus(const ParseTree& parse_tree, const Status& s);

  // Process the read/write op response.
  CHECKED_STATUS ProcessOpResponse(client::YBqlOp* op,
                                   const TreeNode* tnode,
                                   ExecContext* exec_context);

  // Process result of FlushAsyncDone.
  CHECKED_STATUS ProcessAsyncResults(const Status& s);

  // Append execution result.
  CHECKED_STATUS AppendResult(const RowsResult::SharedPtr& result);

  // Continue a multi-partition select (e.g. table scan or query with 'IN' condition on hash cols).
  Result<bool> FetchMoreRowsIfNeeded(const PTSelectStmt* tnode,
                                     const std::shared_ptr<client::YBqlReadOp>& op,
                                     ExecContext* exec_context,
                                     TnodeContext* tnode_context);

  // Aggregate all result sets from all tablet servers to form the requested resultset.
  CHECKED_STATUS AggregateResultSets(const PTSelectStmt* pt_select);
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

  // Reset execution state.
  void Reset();

  //------------------------------------------------------------------------------------------------
  // Expression evaluation.

  // CHECKED_STATUS EvalTimeUuidExpr(const PTExpr::SharedPtr& expr, EvalTimeUuidValue *result);
  // CHECKED_STATUS ConvertFromTimeUuid(EvalValue *result, const EvalTimeUuidValue& uuid_value);
  CHECKED_STATUS PTExprToPB(const PTExpr::SharedPtr& expr, QLExpressionPB *expr_pb);

  // Constant expressions.
  CHECKED_STATUS PTConstToPB(const PTExpr::SharedPtr& const_pt, QLValuePB *const_pb,
                             bool negate = false);
  CHECKED_STATUS PTExprToPB(const PTConstVarInt *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstDecimal *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstInt *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstDouble *const_pt, QLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstText *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstBool *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstUuid *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstBinary *const_pt, QLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTCollectionExpr *const_pt, QLValuePB *const_pb);

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
  CHECKED_STATUS PTJsonOperatorToPB(const PTJsonOperator::SharedPtr& json_pt,
                                    QLJsonOperationPB *op_pb);

  // Builtin calls.
  // Even though BFCall and TSCall are processed similarly in executor at this point because they
  // have similar protobuf, it is best not to merge the two functions "BFCallToPB" and "TSCallToPB"
  // into one. That way, coding changes to one case doesn't affect the other in the future.
  CHECKED_STATUS PTExprToPB(const PTBcall *bcall_pt, QLExpressionPB *bcall_pb);
  CHECKED_STATUS BFCallToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb);
  CHECKED_STATUS TSCallToPB(const PTBcall *bcall_pt, QLExpressionPB *expr_pb);

  // Logic expressions.
  CHECKED_STATUS PTExprToPB(const PTLogic1 *logic_pt, QLExpressionPB *logic_pb);
  CHECKED_STATUS PTExprToPB(const PTLogic2 *logic_pt, QLExpressionPB *logic_pb);

  // Relation expressions.
  CHECKED_STATUS PTExprToPB(const PTRelation0 *relation_pt, QLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation1 *relation_pt, QLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation2 *relation_pt, QLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation3 *relation_pt, QLExpressionPB *relation_pb);

  //------------------------------------------------------------------------------------------------

  // Set the time to live for the values affected by the current write request
  CHECKED_STATUS TtlToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req);

  // Set the timestamp for the values affected by the current write request
  CHECKED_STATUS TimestampToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req);

  // Convert PTExpr to appropriate QLExpressionPB with appropriate validation
  CHECKED_STATUS PTExprToPBValidated(const PTExpr::SharedPtr& expr, QLExpressionPB *expr_pb);

  //------------------------------------------------------------------------------------------------
  // Column evaluation.

  // Convert column references to protobuf.
  CHECKED_STATUS ColumnRefsToPB(const PTDmlStmt *tnode, QLReferencedColumnsPB *columns_pb);

  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToPB(const PTDmlStmt *tnode, QLWriteRequestPB *req);

  //------------------------------------------------------------------------------------------------
  // Where clause evaluation.

  // Convert where clause to protobuf for read request.
  CHECKED_STATUS WhereClauseToPB(QLReadRequestPB *req,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops,
                                 const MCList<JsonColumnOp>& jsoncol_where_ops,
                                 const MCList<PartitionKeyOp>& partition_key_ops,
                                 const MCList<FuncOp>& func_ops,
                                 bool *no_results);

  // Convert where clause to protobuf for write request.
  CHECKED_STATUS WhereClauseToPB(QLWriteRequestPB *req,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops);

  // Convert an expression op in where clause to protobuf.
  CHECKED_STATUS WhereOpToPB(QLConditionPB *condition, const ColumnOp& col_op);
  CHECKED_STATUS WhereSubColOpToPB(QLConditionPB *condition, const SubscriptedColumnOp& subcol_op);
  CHECKED_STATUS WhereJsonColOpToPB(QLConditionPB *condition, const JsonColumnOp& jsoncol_op);
  CHECKED_STATUS FuncOpToPB(QLConditionPB *condition, const FuncOp& func_op);

  //------------------------------------------------------------------------------------------------
  bool DeferOperation(const PTDmlStmt *tnode, const client::YBqlWriteOpPtr& op);
  CHECKED_STATUS ApplyOperation(const PTDmlStmt *tnode, const client::YBqlWriteOpPtr& op);

  //------------------------------------------------------------------------------------------------
  CHECKED_STATUS UpdateIndexes(const PTDmlStmt *tnode, QLWriteRequestPB *req);
  CHECKED_STATUS ApplyIndexWriteOps(const PTDmlStmt *tnode, const QLWriteRequestPB& req);

  //------------------------------------------------------------------------------------------------
  ExecContext& exec_context();

  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) for executing statements.
  QLEnv *ql_env_;

  // Execution contexts of the statements being executed. They are created and destroyed for each
  // execution.
  std::list<ExecContext> exec_contexts_;

  // Set of write operations that have been applied (separated by their primary keys).
  std::unordered_set<client::YBqlWriteOpPtr,
                     client::YBqlWriteOp::PrimaryKeyComparator,
                     client::YBqlWriteOp::PrimaryKeyComparator> batched_writes_by_primary_key_;

  // Set of write operations referencing a static column that have been applied (separated by their
  // hash keys).
  std::unordered_set<client::YBqlWriteOpPtr,
                     client::YBqlWriteOp::HashKeyComparator,
                     client::YBqlWriteOp::HashKeyComparator> batched_writes_by_hash_key_;

  // Execution result.
  ExecutedResult::SharedPtr result_;

  // Statement executed callback.
  StatementExecutedCallback cb_;

  // QLMetrics to keep track of node parsing etc.
  const QLMetrics* ql_metrics_;

  // Rescheduled Execute callback.
  Callback<void(void)> rescheduled_execute_cb_;

  // Rescheduled FlushAsync callback.
  Callback<void(void)> rescheduled_flush_async_cb_;

  // FlushAsync callback.
  Callback<void(const Status&, bool)> flush_async_cb_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_EXEC_EXECUTOR_H_
