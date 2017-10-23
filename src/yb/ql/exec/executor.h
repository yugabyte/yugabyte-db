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

#ifndef YB_QL_EXEC_EXECUTOR_H_
#define YB_QL_EXEC_EXECUTOR_H_

#include "yb/common/partial_row.h"
#include "yb/ql/exec/exec_context.h"
#include "yb/ql/ptree/pt_create_keyspace.h"
#include "yb/ql/ptree/pt_use_keyspace.h"
#include "yb/ql/ptree/pt_create_table.h"
#include "yb/ql/ptree/pt_alter_table.h"
#include "yb/ql/ptree/pt_create_type.h"
#include "yb/ql/ptree/pt_drop.h"
#include "yb/ql/ptree/pt_select.h"
#include "yb/ql/ptree/pt_insert.h"
#include "yb/ql/ptree/pt_delete.h"
#include "yb/ql/ptree/pt_update.h"
#include "yb/ql/util/statement_params.h"
#include "yb/ql/util/statement_result.h"

namespace yb {
namespace ql {

class QLMetrics;

class Executor {
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

  // Creates a table.
  CHECKED_STATUS ExecPTNode(const PTCreateTable *tnode);

  // Alters a table.
  CHECKED_STATUS ExecPTNode(const PTAlterTable *tnode);

  // Drops a table.
  CHECKED_STATUS ExecPTNode(const PTDropStmt *tnode);

  // Creates a user-defined type;
  CHECKED_STATUS ExecPTNode(const PTCreateType *tnode);

  // Select statement.
  CHECKED_STATUS ExecPTNode(const PTSelectStmt *tnode);

  // Insert statement.
  CHECKED_STATUS ExecPTNode(const PTInsertStmt *tnode);

  // Delete statement.
  CHECKED_STATUS ExecPTNode(const PTDeleteStmt *tnode);

  // Update statement.
  CHECKED_STATUS ExecPTNode(const PTUpdateStmt *tnode);

  // Creates a keyspace.
  CHECKED_STATUS ExecPTNode(const PTCreateKeyspace *tnode);

  // Uses a keyspace.
  CHECKED_STATUS ExecPTNode(const PTUseKeyspace *tnode);

  //------------------------------------------------------------------------------------------------
  // Result processing.

  // Callback for FlushAsync.
  void FlushAsyncDone(const Status& s);

  // Process the status of executing a statement.
  CHECKED_STATUS ProcessStatementStatus(const ParseTree& parse_tree, const Status& s);

  // Process the read/write op response.
  CHECKED_STATUS ProcessOpResponse(client::YBqlOp* op, ExecContext* exec_context);

  // Process result of FlushAsyncDone.
  CHECKED_STATUS ProcessAsyncResults();

  // Append execution result.
  CHECKED_STATUS AppendResult(const ExecutedResult::SharedPtr& result);

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
  CHECKED_STATUS PTExprToPB(const PTAllColumns *ref_all, QLReadRequestPB *req);

  // Operators.
  // There's only one, so call it PTUMinus for now.
  CHECKED_STATUS PTUMinusToPB(const PTOperator1 *op_pt, QLExpressionPB *op_pb);
  CHECKED_STATUS PTUMinusToPB(const PTOperator1 *op_pt, QLValuePB *const_pb);

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

  //------------------------------------------------------------------------------------------------
  // Column evaluation.

  // Convert column references to protobuf.
  CHECKED_STATUS ColumnRefsToPB(const PTDmlStmt *tnode, QLReferencedColumnsPB *columns_pb);

  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToPB(const std::shared_ptr<client::YBTable>& table,
                                const PTDmlStmt *tnode,
                                QLWriteRequestPB *req);

  //------------------------------------------------------------------------------------------------
  // Where clause evaluation.

  // Convert where clause to protobuf for read request.
  CHECKED_STATUS WhereClauseToPB(QLReadRequestPB *req,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops,
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
  CHECKED_STATUS FuncOpToPB(QLConditionPB *condition, const FuncOp& func_op);

  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) for executing statements.
  QLEnv *ql_env_;

  // Execution contexts of the statements being executed. They are created and destroyed for each
  // execution.
  std::list<ExecContext> exec_contexts_;

  // Execution context of the last statement being executed.
  ExecContext* exec_context_;

  // Execution result.
  ExecutedResult::SharedPtr result_;

  // Statement executed callback.
  StatementExecutedCallback cb_;

  // QLMetrics to keep track of node parsing etc.
  const QLMetrics* ql_metrics_;

  // FlushAsync callback.
  Callback<void(const Status&)> flush_async_cb_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_QL_EXEC_EXECUTOR_H_
