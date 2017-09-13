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

#ifndef YB_SQL_EXEC_EXECUTOR_H_
#define YB_SQL_EXEC_EXECUTOR_H_

#include "yb/common/partial_row.h"
#include "yb/sql/exec/exec_context.h"
#include "yb/sql/ptree/pt_create_keyspace.h"
#include "yb/sql/ptree/pt_use_keyspace.h"
#include "yb/sql/ptree/pt_create_table.h"
#include "yb/sql/ptree/pt_alter_table.h"
#include "yb/sql/ptree/pt_create_type.h"
#include "yb/sql/ptree/pt_drop.h"
#include "yb/sql/ptree/pt_select.h"
#include "yb/sql/ptree/pt_insert.h"
#include "yb/sql/ptree/pt_delete.h"
#include "yb/sql/ptree/pt_update.h"
#include "yb/sql/util/statement_params.h"
#include "yb/sql/util/statement_result.h"

namespace yb {
namespace sql {

class SqlMetrics;

class Executor {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Executor> UniPtr;
  typedef std::unique_ptr<const Executor> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Executor(SqlEnv *sql_env, const SqlMetrics* sql_metrics);
  virtual ~Executor();

  // Execute the given statement (parse tree).
  void ExecuteAsync(const std::string &sql_stmt, const ParseTree &parse_tree,
                    const StatementParameters* params, StatementExecutedCallback cb);

  // Batch execution of statements. StatementExecutedCallback will be invoked when the batch is
  // applied and execution is complete, or when an error occurs.
  void BeginBatch(StatementExecutedCallback cb);
  void ExecuteBatch(const std::string &sql_stmt, const ParseTree &parse_tree,
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
  CHECKED_STATUS Execute(const std::string &sql_stmt, const ParseTree &parse_tree,
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
  CHECKED_STATUS PTExprToPB(const PTExpr::SharedPtr& expr, YQLExpressionPB *expr_pb);

  // Constant expressions.
  CHECKED_STATUS PTConstToPB(const PTExpr::SharedPtr& const_pt, YQLValuePB *const_pb,
                             bool negate = false);
  CHECKED_STATUS PTExprToPB(const PTConstVarInt *const_pt, YQLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstDecimal *const_pt, YQLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstInt *const_pt, YQLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstDouble *const_pt, YQLValuePB *const_pb, bool negate);
  CHECKED_STATUS PTExprToPB(const PTConstText *const_pt, YQLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstBool *const_pt, YQLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstUuid *const_pt, YQLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTConstBinary *const_pt, YQLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTCollectionExpr *const_pt, YQLValuePB *const_pb);

  // Bind variable.
  CHECKED_STATUS PTExprToPB(const PTBindVar *bind_pt, YQLExpressionPB *bind_pb);

  // Column types.
  CHECKED_STATUS PTExprToPB(const PTRef *ref_pt, YQLExpressionPB *ref_pb);
  CHECKED_STATUS PTExprToPB(const PTSubscriptedColumn *ref_pt, YQLExpressionPB *ref_pb);

  // Operators.
  // There's only one, so call it PTUMinus for now.
  CHECKED_STATUS PTUMinusToPB(const PTOperator1 *op_pt, YQLExpressionPB *op_pb);
  CHECKED_STATUS PTUMinusToPB(const PTOperator1 *op_pt, YQLValuePB *const_pb);

  // Builtins.
  CHECKED_STATUS PTExprToPB(const PTBcall *bcall_pt, YQLExpressionPB *bcall_pb);

  // Logic expressions.
  CHECKED_STATUS PTExprToPB(const PTLogic1 *logic_pt, YQLExpressionPB *logic_pb);
  CHECKED_STATUS PTExprToPB(const PTLogic2 *logic_pt, YQLExpressionPB *logic_pb);

  // Relation expressions.
  CHECKED_STATUS PTExprToPB(const PTRelation0 *relation_pt, YQLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation1 *relation_pt, YQLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation2 *relation_pt, YQLExpressionPB *relation_pb);
  CHECKED_STATUS PTExprToPB(const PTRelation3 *relation_pt, YQLExpressionPB *relation_pb);

  //------------------------------------------------------------------------------------------------

  // Set the time to live for the values affected by the current write request
  CHECKED_STATUS TtlToPB(const PTDmlStmt *tnode, YQLWriteRequestPB *req);

  //------------------------------------------------------------------------------------------------
  // Column evaluation.

  // Convert column references to protobuf.
  CHECKED_STATUS ColumnRefsToPB(const PTDmlStmt *tnode, YQLReferencedColumnsPB *columns_pb);

  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToPB(const std::shared_ptr<client::YBTable>& table,
                                const PTDmlStmt *tnode,
                                YQLWriteRequestPB *req,
                                YBPartialRow *row);

  // Set up partial row for computing hash value.
  CHECKED_STATUS SetupPartialRow(const ColumnDesc *col_desc,
                                 const YQLExpressionPB *col_expr,
                                 YBPartialRow *row);

  //------------------------------------------------------------------------------------------------
  // Where clause evaluation.

  // Convert where clause to protobuf for read request.
  CHECKED_STATUS WhereClauseToPB(YQLReadRequestPB *req,
                                 YBPartialRow *row,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops,
                                 const MCList<PartitionKeyOp>& partition_key_ops,
                                 const MCList<FuncOp>& func_ops,
                                 bool *no_results);

  // Convert where clause to protobuf for write request.
  CHECKED_STATUS WhereClauseToPB(YQLWriteRequestPB *req,
                                 YBPartialRow *row,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<SubscriptedColumnOp>& subcol_where_ops);

  // Convert an expression op in where clause to protobuf.
  CHECKED_STATUS WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op);
  CHECKED_STATUS WhereSubColOpToPB(YQLConditionPB *condition, const SubscriptedColumnOp& subcol_op);
  CHECKED_STATUS FuncOpToPB(YQLConditionPB *condition, const FuncOp& func_op);

  //------------------------------------------------------------------------------------------------
  // Environment (YBClient) for executing statements.
  SqlEnv *sql_env_;

  // Execution contexts of the statements being executed. They are created and destroyed for each
  // execution.
  std::list<ExecContext> exec_contexts_;

  // Execution context of the last statement being executed.
  ExecContext* exec_context_;

  // Execution result.
  ExecutedResult::SharedPtr result_;

  // Statement executed callback.
  StatementExecutedCallback cb_;

  // SqlMetrics to keep track of node parsing etc.
  const SqlMetrics* sql_metrics_;

  // FlushAsync callback.
  Callback<void(const Status&)> flush_async_cb_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXECUTOR_H_
