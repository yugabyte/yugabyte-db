//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
  explicit Executor(const SqlMetrics* sql_metrics);
  virtual ~Executor();

  // Execute the given parse tree.
  void ExecuteAsync(
      const std::string &sql_stmt, const ParseTree &ptree, const StatementParameters& params,
      SqlEnv *sql_env, StatementExecutedCallback cb);

  // Complete execution.
  void Done();

  // Access to error code.
  ErrorCode error_code() const {
    return exec_context_->error_code();
  }

  //------------------------------------------------------------------------------------------------
  // Currently, we don't yet have code generator into byte code, so the following ExecTNode()
  // functions are operating directly on the parse tree.
  void ExecPTreeAsync(const ParseTree &ptree, StatementExecutedCallback cb);

  // Execute any TreeNode. This function determines how to execute a node.
  void ExecTreeNodeAsync(const TreeNode *tnode, StatementExecutedCallback cb);

  // Returns unsupported error for generic tree node execution. We only get to this overloaded
  // function if the execution of a specific treenode is not yet supported or defined.
  void ExecPTNodeAsync(const TreeNode *tnode, StatementExecutedCallback cb);

  // Runs the execution on all of the entries in the list node.
  void ExecPTNodeAsync(const PTListNode *tnode, StatementExecutedCallback cb, int idx = 0);

  // Creates table.
  void ExecPTNodeAsync(const PTCreateTable *tnode, StatementExecutedCallback cb);

  void ExecPTNodeAsync(const PTDropStmt *tnode, StatementExecutedCallback cb);

  // Select statement: current_result contains accummulated rows if any that are buffered locally
  // to be returned.
  void ExecPTNodeAsync(
      const PTSelectStmt *tnode, StatementExecutedCallback cb,
      RowsResult::SharedPtr current_result = nullptr);

  // Insert statement.
  void ExecPTNodeAsync(const PTInsertStmt *tnode, StatementExecutedCallback cb);

  // Delete statement.
  void ExecPTNodeAsync(const PTDeleteStmt *tnode, StatementExecutedCallback cb);

  // Update statement.
  void ExecPTNodeAsync(const PTUpdateStmt *tnode, StatementExecutedCallback cb);

  // Create keyspace.
  void ExecPTNodeAsync(const PTCreateKeyspace *tnode, StatementExecutedCallback cb);

  // Use keyspace.
  void ExecPTNodeAsync(const PTUseKeyspace *tnode, StatementExecutedCallback cb);

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
  CHECKED_STATUS PTExprToPB(const PTMapExpr *const_pt, YQLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTSetExpr *const_pt, YQLValuePB *const_pb);
  CHECKED_STATUS PTExprToPB(const PTListExpr *const_pt, YQLValuePB *const_pb);

  // Bind variable.
  CHECKED_STATUS PTExprToPB(const PTBindVar *bind_pt, YQLExpressionPB *bind_pb);
  // Get a bind variable.
  CHECKED_STATUS GetBindVariable(const PTBindVar* var, YQLValue *value) const;

  // Column types.
  CHECKED_STATUS PTExprToPB(const PTRef *ref_pt, YQLExpressionPB *ref_pb);

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

 private:
  //------------------------------------------------------------------------------------------------
  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToWriteRequestPB(const std::shared_ptr<client::YBTable>& table,
                                            const PTDmlStmt *tnode,
                                            YQLWriteRequestPB *req,
                                            YBPartialRow *row);

  // Set up partial row for computing hash value.
  CHECKED_STATUS SetupPartialRow(const ColumnDesc *col_desc,
                                 const YQLExpressionPB *col_expr,
                                 YBPartialRow *row);

  // Convert where clause to protobuf for read request.
  CHECKED_STATUS WhereClauseToPB(YQLReadRequestPB *req,
                                 YBPartialRow *row,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops,
                                 const MCList<PartitionKeyOp>& partition_key_ops);

  // Convert where clause to protobuf for write request.
  CHECKED_STATUS WhereClauseToPB(YQLWriteRequestPB *req,
                                 YBPartialRow *row,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops);

  // Convert an expression op in where clause to protobuf.
  CHECKED_STATUS WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op);

  void SelectAsyncDone(
      const PTSelectStmt *tnode, StatementExecutedCallback cb, RowsResult::SharedPtr current_result,
      const Status &s, ExecutedResult::SharedPtr new_result);

  void PTNodeAsyncDone(
      const PTListNode *lnode, int index, StatementExecutedCallback cb, const Status &s,
      ExecutedResult::SharedPtr result);

  void ExecuteDone(
      const ParseTree *ptree, MonoTime start, StatementExecutedCallback cb, const Status &s,
      ExecutedResult::SharedPtr result);

  //------------------------------------------------------------------------------------------------
  // Execution context which are created and destroyed for each execution.
  ExecContext::UniPtr exec_context_;

  // Parameters to execute the statement with.
  const StatementParameters *params_ = nullptr;

  // SqlMetrics to keep track of node parsing etc.
  const SqlMetrics* sql_metrics_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXECUTOR_H_
