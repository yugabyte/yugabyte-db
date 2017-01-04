//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Structure definitions for column descriptor of a table.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_COLUMN_ARG_H_
#define YB_SQL_PTREE_COLUMN_ARG_H_

#include "yb/client/client.h"
#include "yb/common/types.h"
#include "yb/sql/util/base_types.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/common/ysql_value.h"

namespace yb {
namespace sql {

// This class represents an argument in expressions, but it is not part of the parse tree. It is
// used during semantic and execution phase.
class ColumnArg {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ColumnArg> SharedPtr;
  typedef std::shared_ptr<const ColumnArg> SharedPtrConst;

  ColumnArg(const ColumnDesc *desc = nullptr,
            const PTExpr::SharedPtr& expr = nullptr)
      : desc_(desc), expr_(expr) {
  }
  ColumnArg(const ColumnArg &arg) : ColumnArg(arg.desc_, arg.expr_) {
  }
  virtual ~ColumnArg() {
  }

  void Init(const ColumnDesc *desc, const PTExpr::SharedPtr& expr) {
    desc_ = desc;
    expr_ = expr;
  }

  bool IsInitialized() const {
    return desc_ != nullptr;
  }

  const ColumnDesc *desc() const {
    return desc_;
  }

  const PTExpr::SharedPtr& expr() const {
    return expr_;
  }

 protected:
  const ColumnDesc *desc_;
  PTExpr::SharedPtr expr_;
};

// This class represents an operation on a column.
class ColumnOp : public ColumnArg {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ColumnOp> SharedPtr;
  typedef std::shared_ptr<const ColumnOp> SharedPtrConst;

  ColumnOp(const ColumnDesc *desc = nullptr,
           const PTExpr::SharedPtr& expr = nullptr,
           ExprOperator expr_op = ExprOperator::kNoOp,
           yb::YSQLOperator yb_op = yb::YSQLOperator::YSQL_OP_NOOP)
      : ColumnArg(desc, expr), expr_op_(expr_op), yb_op_(yb_op) {
  }
  ColumnOp(const ColumnOp &column_op)
      : ColumnOp(column_op.desc_, column_op.expr_, column_op.expr_op_, column_op.yb_op_) {
  }
  virtual ~ColumnOp() {
  }

  void Init(const ColumnDesc *desc, const PTExpr::SharedPtr& expr,
            ExprOperator expr_op, yb::YSQLOperator yb_op) {
    desc_ = desc;
    expr_ = expr;
    expr_op_ = expr_op;
    yb_op_ = yb_op;
  }

  ExprOperator expr_op() const {
    return expr_op_;
  }

  yb::YSQLOperator yb_op() const {
    return yb_op_;
  }

 private:
  ExprOperator expr_op_;
  yb::YSQLOperator yb_op_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_COLUMN_ARG_H_
