//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the execution process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EXECUTOR_H_
#define YB_SQL_EXEC_EXECUTOR_H_

#include "yb/sql/statement.h"
#include "yb/sql/exec/exec_context.h"
#include "yb/sql/exec/eval_expr.h"
#include "yb/sql/ptree/pt_create_keyspace.h"
#include "yb/sql/ptree/pt_use_keyspace.h"
#include "yb/sql/ptree/pt_create_table.h"
#include "yb/sql/ptree/pt_drop.h"
#include "yb/sql/ptree/pt_select.h"
#include "yb/sql/ptree/pt_insert.h"
#include "yb/sql/ptree/pt_delete.h"
#include "yb/sql/ptree/pt_update.h"

#include "yb/common/partial_row.h"

namespace yb {
namespace sql {

class Executor {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<Executor> UniPtr;
  typedef std::unique_ptr<const Executor> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  Executor();
  virtual ~Executor();

  // Execute the given parse tree.
  CHECKED_STATUS Execute(const std::string& sql_stmt,
                         const ParseTree& ptree,
                         const StatementParameters& params,
                         SqlEnv *sql_env);

  // Complete execution.
  void Done();

  // Access to error code.
  ErrorCode error_code() const {
    return exec_context_->error_code();
  }

  //------------------------------------------------------------------------------------------------
  // Currently, we don't yet have code generator into byte code, so the following ExecTNode()
  // functions are operating directly on the parse tree.
  CHECKED_STATUS ExecPTree(const ParseTree& ptree);

  // Execute any TreeNode. This function determines how to execute a node.
  CHECKED_STATUS ExecTreeNode(const TreeNode *tnode);

  // Returns unsupported error for generic tree node execution. We only get to this overloaded
  // function if the execution of a specific treenode is not yet supported or defined.
  CHECKED_STATUS ExecPTNode(const TreeNode *tnode);

  // Runs the execution on all of the entries in the list node.
  CHECKED_STATUS ExecPTNode(const PTListNode *tnode);

  // Creates table.
  CHECKED_STATUS ExecPTNode(const PTCreateTable *tnode);

  CHECKED_STATUS ExecPTNode(const PTDropStmt *tnode);

  // Select statement.
  CHECKED_STATUS ExecPTNode(const PTSelectStmt *tnode);

  // Insert statement.
  CHECKED_STATUS ExecPTNode(const PTInsertStmt *tnode);

  // Delete statement.
  CHECKED_STATUS ExecPTNode(const PTDeleteStmt *tnode);

  // Update statement.
  CHECKED_STATUS ExecPTNode(const PTUpdateStmt *tnode);

  // Create keyspace.
  CHECKED_STATUS ExecPTNode(const PTCreateKeyspace *tnode);

  // Use keyspace.
  CHECKED_STATUS ExecPTNode(const PTUseKeyspace *tnode);

  //------------------------------------------------------------------------------------------------
  // Expression evaluation.
  CHECKED_STATUS EvalExpr(const PTExpr::SharedPtr& expr, EvalValue *result);
  CHECKED_STATUS EvalIntExpr(const PTExpr::SharedPtr& expr, EvalIntValue *result);
  CHECKED_STATUS EvalDoubleExpr(const PTExpr::SharedPtr& expr, EvalDoubleValue *result);
  CHECKED_STATUS EvalStringExpr(const PTExpr::SharedPtr& expr, EvalStringValue *result);
  CHECKED_STATUS EvalBoolExpr(const PTExpr::SharedPtr& expr, EvalBoolValue *result);

  CHECKED_STATUS ConvertFromInt(EvalValue *result, const EvalIntValue& int_value);
  CHECKED_STATUS ConvertFromDouble(EvalValue *result, const EvalDoubleValue& double_value);
  CHECKED_STATUS ConvertFromString(EvalValue *result, const EvalStringValue& string_value);
  CHECKED_STATUS ConvertFromBool(EvalValue *result, const EvalBoolValue& bool_value);

 private:
  //------------------------------------------------------------------------------------------------
  // Convert expression to protobuf.
  template<typename PBType>
  CHECKED_STATUS ExprToPB(const PTExpr::SharedPtr& expr,
                          InternalType col_type,
                          PBType* col_pb,
                          YBPartialRow *row = nullptr,
                          int col_index = -1);

  // Convert column arguments to protobuf.
  CHECKED_STATUS ColumnArgsToWriteRequestPB(const std::shared_ptr<client::YBTable>& table,
                                            const PTDmlStmt *tnode,
                                            YQLWriteRequestPB *req,
                                            YBPartialRow *row);

  // Convert where clause to protobuf for read request.
  CHECKED_STATUS WhereClauseToPB(YQLReadRequestPB *req,
                                 YBPartialRow *row,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops);

  // Convert where clause to protobuf for write request.
  CHECKED_STATUS WhereClauseToPB(YQLWriteRequestPB *req,
                                 YBPartialRow *row,
                                 const MCVector<ColumnOp>& key_where_ops,
                                 const MCList<ColumnOp>& where_ops);

  // Convert an expression op in where claluse to protobuf.
  CHECKED_STATUS WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op);

  // Convert a bool expression to protobuf.
  CHECKED_STATUS BoolExprToPB(YQLConditionPB *condition, const PTExpr* expr);

  // Convert a relational op to protobuf
  CHECKED_STATUS RelationalOpToPB(YQLConditionPB *condition,
                                  YQLOperator opr,
                                  const PTExpr *relation);

  // Convert a column condition to protobuf
  CHECKED_STATUS ColumnConditionToPB(YQLConditionPB *condition,
                                     YQLOperator opr,
                                     const PTExpr *cond);

  // Convert a between (not) to protobuf
  CHECKED_STATUS BetweenToPB(YQLConditionPB *condition,
                             YQLOperator opr,
                             const PTExpr *between);

  //------------------------------------------------------------------------------------------------
  // Execution context which are created and destroyed for each execution.
  ExecContext::UniPtr exec_context_;

  // Parameters to execute the statement with.
  const StatementParameters *params_ = nullptr;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXECUTOR_H_
