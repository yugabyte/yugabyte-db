//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Structure definitions for column descriptor of a table.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_COLUMN_ARG_H_
#define YB_SQL_PTREE_COLUMN_ARG_H_

#include "yb/client/client.h"
#include "yb/common/yql_value.h"
#include "yb/common/types.h"
#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/pt_bcall.h"
#include "yb/util/memory/mc_types.h"

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
           yb::YQLOperator yb_op = yb::YQLOperator::YQL_OP_NOOP)
      : ColumnArg(desc, expr), yb_op_(yb_op) {
  }
  ColumnOp(const ColumnOp &column_op)
      : ColumnOp(column_op.desc_, column_op.expr_, column_op.yb_op_) {
  }
  virtual ~ColumnOp() {
  }

  void Init(const ColumnDesc *desc, const PTExpr::SharedPtr& expr, yb::YQLOperator yb_op) {
    desc_ = desc;
    expr_ = expr;
    yb_op_ = yb_op;
  }

  yb::YQLOperator yb_op() const {
    return yb_op_;
  }

 private:
  yb::YQLOperator yb_op_;
};

// This class represents a sub-column argument (e.g. "SET l[1] = 'b'")
class SubscriptedColumnArg : public ColumnArg {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<SubscriptedColumnArg> SharedPtr;
  typedef std::shared_ptr<const SubscriptedColumnArg> SharedPtrConst;

  SubscriptedColumnArg(const ColumnDesc *desc = nullptr,
                 const PTExprListNode::SharedPtr& args = nullptr,
                 const PTExpr::SharedPtr& expr = nullptr)
      : ColumnArg(desc, expr), args_(args) {
  }

  SubscriptedColumnArg(const SubscriptedColumnArg &sval)
      : SubscriptedColumnArg(sval.desc_, sval.args_, sval.expr_) { }

  virtual ~SubscriptedColumnArg() {
  }

  const PTExprListNode::SharedPtr& args() const {
    return args_;
  }

 protected:
  PTExprListNode::SharedPtr args_;
};

// This class represents an operation on a sub-column (e.g. "WHERE l[1] = 'b'").
class SubscriptedColumnOp : public SubscriptedColumnArg {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<SubscriptedColumnOp> SharedPtr;
  typedef std::shared_ptr<const SubscriptedColumnOp> SharedPtrConst;

  SubscriptedColumnOp(const ColumnDesc *desc = nullptr,
              const PTExprListNode::SharedPtr& args = nullptr,
              const PTExpr::SharedPtr& expr = nullptr,
              yb::YQLOperator yb_op = yb::YQLOperator::YQL_OP_NOOP)
      : SubscriptedColumnArg(desc, args, expr), yb_op_(yb_op) {
  }
  SubscriptedColumnOp(const SubscriptedColumnOp &subcol_op)
      : SubscriptedColumnOp(subcol_op.desc_, subcol_op.args_, subcol_op.expr_, subcol_op.yb_op_) {
  }
  virtual ~SubscriptedColumnOp() {
  }

  void Init(const ColumnDesc *desc, const PTExprListNode::SharedPtr& args,
            const PTExpr::SharedPtr& expr, yb::YQLOperator yb_op) {
    desc_ = desc;
    args_ = args;
    expr_ = expr;
    yb_op_ = yb_op;
  }

  yb::YQLOperator yb_op() const {
    return yb_op_;
  }

 private:
  yb::YQLOperator yb_op_;
};

class PartitionKeyOp {
 public:
  PartitionKeyOp(yb::YQLOperator yb_op, PTExpr::SharedPtr expr)
      : yb_op_(yb_op), expr_(expr) {}

  YQLOperator yb_op() const {
    return yb_op_;
  }

  PTExpr::SharedPtr expr() const {
    return expr_;
  }

 private:
  YQLOperator yb_op_;
  PTExpr::SharedPtr expr_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_COLUMN_ARG_H_
