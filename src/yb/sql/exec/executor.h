//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the execution process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EXECUTOR_H_
#define YB_SQL_EXEC_EXECUTOR_H_

#include "yb/sql/exec/exec_context.h"
#include "yb/sql/exec/eval_expr.h"
#include "yb/sql/ptree/pt_create_table.h"
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
  ErrorCode Execute(const std::string& sql_stmt,
                    ParseTree::UniPtr ptree,
                    SqlEnv *sql_env);

  // Complete execution and release the parse tree from the process.
  ParseTree::UniPtr Done();

  //------------------------------------------------------------------------------------------------
  // Currently, we don't yet have code generator into byte code, so the following ExecTNode()
  // functions are operating directly on the parse tree.
  ErrorCode ExecPTree(const ParseTree *ptree);

  // Execute any TreeNode. This function determines how to execute a node.
  ErrorCode ExecTreeNode(const TreeNode *tnode);

  // Returns unsupported error for generic tree node execution. We only get to this overloaded
  // function if the execution of a specific treenode is not yet supported or defined.
  ErrorCode ExecPTNode(const TreeNode *tnode);

  // Runs the execution on all of the entries in the list node.
  ErrorCode ExecPTNode(const PTListNode *tnode);

  // Creates table.
  ErrorCode ExecPTNode(const PTCreateTable *tnode);

  // Select statement.
  ErrorCode ExecPTNode(const PTSelectStmt *tnode);

  // Insert statement.
  ErrorCode ExecPTNode(const PTInsertStmt *tnode);

  // Delete statement.
  ErrorCode ExecPTNode(const PTDeleteStmt *tnode);

  // Update statement.
  ErrorCode ExecPTNode(const PTUpdateStmt *tnode);

  //------------------------------------------------------------------------------------------------
  // Expression evaluation.
  Status EvalExpr(const PTExpr::SharedPtr& expr, EvalValue *result);
  Status EvalIntExpr(const PTExpr::SharedPtr& expr, EvalIntValue *result);
  Status EvalDoubleExpr(const PTExpr::SharedPtr& expr, EvalDoubleValue *result);
  Status EvalStringExpr(const PTExpr::SharedPtr& expr, EvalStringValue *result);
  Status EvalBoolExpr(const PTExpr::SharedPtr& expr, EvalBoolValue *result);

  Status ConvertFromInt(EvalValue *result, const EvalIntValue& int_value);
  Status ConvertFromDouble(EvalValue *result, const EvalDoubleValue& double_value);
  Status ConvertFromString(EvalValue *result, const EvalStringValue& string_value);
  Status ConvertFromBool(EvalValue *result, const EvalBoolValue& bool_value);

 private:
  //------------------------------------------------------------------------------------------------
  // Convert expression to protobuf.
  template<typename PBType>
  Status ExprToPB(const PTExpr::SharedPtr& expr,
                  yb::DataType col_type,
                  PBType* col_pb,
                  YBPartialRow *row = nullptr,
                  int col_index = -1);

  // Convert column arguments to protobuf.
  Status ColumnArgsToWriteRequestPB(const std::shared_ptr<client::YBTable>& table,
                                    const MCVector<ColumnArg>& column_args,
                                    YSQLWriteRequestPB *req,
                                    YBPartialRow *row);

  // Convert where clause to protobuf for read request.
  Status WhereClauseToPB(YSQLReadRequestPB *req,
                         YBPartialRow *row,
                         const MCVector<ColumnOp>& hash_where_ops,
                         const MCList<ColumnOp>& where_ops);

  // Convert where clause to protobuf for write request.
  Status WhereClauseToPB(YSQLWriteRequestPB *req,
                         YBPartialRow *row,
                         const MCVector<ColumnOp>& hash_where_ops,
                         const MCList<ColumnOp>& where_ops);

  // Convert an expression op in where claluse to protobuf.
  Status WhereOpToPB(YSQLConditionPB *condition, const ColumnOp& col_op);

  //------------------------------------------------------------------------------------------------
  // Execution context which are created and destroyed for each execution.
  ExecContext::UniPtr exec_context_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_EXEC_EXECUTOR_H_
