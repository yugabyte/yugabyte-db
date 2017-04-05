//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Entry point for the execution process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_EXEC_EXECUTOR_H_
#define YB_SQL_EXEC_EXECUTOR_H_

#include "yb/common/partial_row.h"
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
      const std::string &sql_stmt, const ParseTree &ptree, const StatementParameters &params,
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

  // Select statement.
  void ExecPTNodeAsync(const PTSelectStmt *tnode, StatementExecutedCallback cb);

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
  CHECKED_STATUS EvalExpr(const PTExpr::SharedPtr& expr, EvalValue *result);
  CHECKED_STATUS EvalIntExpr(const PTExpr::SharedPtr& expr, EvalIntValue *result);
  CHECKED_STATUS EvalVarIntExpr(const PTExpr::SharedPtr& expr, EvalVarIntStringValue *result);
  CHECKED_STATUS EvalDoubleExpr(const PTExpr::SharedPtr& expr, EvalDoubleValue *result);
  CHECKED_STATUS EvalStringExpr(const PTExpr::SharedPtr& expr, EvalStringValue *result);
  CHECKED_STATUS EvalBoolExpr(const PTExpr::SharedPtr& expr, EvalBoolValue *result);
  CHECKED_STATUS EvalTimestampExpr(const PTExpr::SharedPtr& expr, EvalTimestampValue *result);
  CHECKED_STATUS EvalInetaddressExpr(const PTExpr::SharedPtr& expr, EvalInetaddressValue *result);
  CHECKED_STATUS EvalUuidExpr(const PTExpr::SharedPtr& expr, EvalUuidValue *result);
  CHECKED_STATUS EvalDecimalExpr(const PTExpr::SharedPtr& expr, EvalDecimalValue *result);

  CHECKED_STATUS ConvertFromInt(EvalValue *result, const EvalIntValue& int_value);
  CHECKED_STATUS ConvertFromVarInt(EvalValue *result, const EvalVarIntStringValue& varint_value);
  CHECKED_STATUS ConvertFromDouble(EvalValue *result, const EvalDoubleValue& double_value);
  CHECKED_STATUS ConvertFromString(EvalValue *result, const EvalStringValue& string_value);
  CHECKED_STATUS ConvertFromDecimal(EvalValue *result, const EvalDecimalValue& decimal_value);
  CHECKED_STATUS ConvertFromBool(EvalValue *result, const EvalBoolValue& bool_value);

 private:
  //------------------------------------------------------------------------------------------------
  // Convert expression to protobuf.
  template<typename PBType>
  CHECKED_STATUS ExprToPB(const PTExpr::SharedPtr& expr,
                          YQLType col_type,
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

  // Convert an expression op in where clause to protobuf.
  CHECKED_STATUS WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op);

  // Convert a bool expression to protobuf.
  CHECKED_STATUS BoolExprToPB(YQLConditionPB *condition, const PTExpr* expr);

  // Convert a relational op to protobuf.
  CHECKED_STATUS RelationalOpToPB(YQLConditionPB *condition,
                                  YQLOperator opr,
                                  const PTExpr *relation);

  // Convert a column condition to protobuf.
  CHECKED_STATUS ColumnConditionToPB(YQLConditionPB *condition,
                                     YQLOperator opr,
                                     const PTExpr *cond);

  // Convert a between (not) to protobuf.
  CHECKED_STATUS BetweenToPB(YQLConditionPB *condition,
                             YQLOperator opr,
                             const PTExpr *between);

  // Get a bind variable.
  CHECKED_STATUS GetBindVariable(const PTBindVar* var, YQLValue *value) const;

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
