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
// Structure definitions for column descriptor of a table.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_COLUMN_ARG_H_
#define YB_YQL_CQL_QL_PTREE_COLUMN_ARG_H_

#include "yb/common/types.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_bcall.h"
#include "yb/util/memory/mc_types.h"

namespace yb {
namespace ql {

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
           yb::QLOperator yb_op = yb::QLOperator::QL_OP_NOOP)
      : ColumnArg(desc, expr), yb_op_(yb_op) {
  }
  ColumnOp(const ColumnOp &column_op)
      : ColumnOp(column_op.desc_, column_op.expr_, column_op.yb_op_) {
  }
  virtual ~ColumnOp() {
  }

  void Init(const ColumnDesc *desc, const PTExpr::SharedPtr& expr, yb::QLOperator yb_op) {
    desc_ = desc;
    expr_ = expr;
    yb_op_ = yb_op;
  }

  yb::QLOperator yb_op() const {
    return yb_op_;
  }

 private:
  yb::QLOperator yb_op_;
};

class FuncOp {
 public:
  typedef std::shared_ptr<FuncOp> SharedPtr;
  typedef std::shared_ptr<const FuncOp> SharedPtrConst;

  FuncOp(const PTExpr::SharedPtr& value_expr,
         const PTBcall::SharedPtr& func_expr,
         yb::QLOperator yb_op = yb::QLOperator::QL_OP_NOOP)
    : value_expr_(value_expr), func_expr_(func_expr), yb_op_(yb_op) {
  }

  void Init(const PTExpr::SharedPtr& value_expr,
            const PTExpr::SharedPtr& func_expr,
            yb::QLOperator yb_op) {
    value_expr_ = value_expr;
    func_expr_ = std::dynamic_pointer_cast<PTBcall>(func_expr);
    yb_op_ = yb_op;
  }

  yb::QLOperator yb_op() const {
    return yb_op_;
  }

  PTExpr::SharedPtr value_expr() const {
    return value_expr_;
  }

  PTBcall::SharedPtr func_expr() const {
    return func_expr_;
  }

 private:
  PTExpr::SharedPtr value_expr_;
  PTBcall::SharedPtr func_expr_;
  yb::QLOperator yb_op_;
};

// This class represents a json column argument (e.g. "WHERE c1->'a'->'b'->>'c' = 'b'")
class JsonColumnArg : public ColumnArg {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<JsonColumnArg> SharedPtr;
  typedef std::shared_ptr<const JsonColumnArg> SharedPtrConst;

  JsonColumnArg(const ColumnDesc *desc = nullptr,
                const PTExprListNode::SharedPtr& args = nullptr,
                const PTExpr::SharedPtr& expr = nullptr)
      : ColumnArg(desc, expr), args_(args) {
  }

  JsonColumnArg(const JsonColumnArg &sval)
      : JsonColumnArg(sval.desc_, sval.args_, sval.expr_) { }

  virtual ~JsonColumnArg() {
  }

  const PTExprListNode::SharedPtr& args() const {
    return args_;
  }

 protected:
  PTExprListNode::SharedPtr args_;
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

// This class represents an operation on a json column (e.g. "WHERE c1->'a'->'b'->>'c' = 'b'").
class JsonColumnOp : public JsonColumnArg {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<JsonColumnOp> SharedPtr;
  typedef std::shared_ptr<const JsonColumnOp> SharedPtrConst;

  JsonColumnOp(const ColumnDesc *desc = nullptr,
                      const PTExprListNode::SharedPtr& args = nullptr,
                      const PTExpr::SharedPtr& expr = nullptr,
                      yb::QLOperator yb_op = yb::QLOperator::QL_OP_NOOP)
      : JsonColumnArg(desc, args, expr), yb_op_(yb_op) {
  }
  JsonColumnOp(const JsonColumnOp &subcol_op)
      : JsonColumnOp(subcol_op.desc_, subcol_op.args_, subcol_op.expr_, subcol_op.yb_op_) {
  }
  virtual ~JsonColumnOp() {
  }

  void Init(const ColumnDesc *desc, const PTExprListNode::SharedPtr& args,
            const PTExpr::SharedPtr& expr, yb::QLOperator yb_op) {
    desc_ = desc;
    args_ = args;
    expr_ = expr;
    yb_op_ = yb_op;
  }

  yb::QLOperator yb_op() const {
    return yb_op_;
  }

  // Name of a Catalog::IndexTable::ExprColumn is created by mangling original name from users.
  string IndexExprToColumnName() const {
    string index_column_name = desc_->MangledName();
    for (const PTExpr::SharedPtr &arg : args_->node_list()) {
      index_column_name += arg->MangledName();
    }
    return index_column_name;
  }

 private:
  yb::QLOperator yb_op_;
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
                      yb::QLOperator yb_op = yb::QLOperator::QL_OP_NOOP)
      : SubscriptedColumnArg(desc, args, expr), yb_op_(yb_op) {
  }
  SubscriptedColumnOp(const SubscriptedColumnOp &subcol_op)
      : SubscriptedColumnOp(subcol_op.desc_, subcol_op.args_, subcol_op.expr_, subcol_op.yb_op_) {
  }
  virtual ~SubscriptedColumnOp() {
  }

  void Init(const ColumnDesc *desc, const PTExprListNode::SharedPtr& args,
            const PTExpr::SharedPtr& expr, yb::QLOperator yb_op) {
    desc_ = desc;
    args_ = args;
    expr_ = expr;
    yb_op_ = yb_op;
  }

  yb::QLOperator yb_op() const {
    return yb_op_;
  }

  string IndexExprToColumnName() const {
    LOG(FATAL) << "Mangling name for subscript operator is not yet supported";
    return "expr";
  }

 private:
  yb::QLOperator yb_op_;
};

class PartitionKeyOp {
 public:
  PartitionKeyOp(yb::QLOperator yb_op, PTExpr::SharedPtr expr)
      : yb_op_(yb_op), expr_(expr) {}

  QLOperator yb_op() const {
    return yb_op_;
  }

  PTExpr::SharedPtr expr() const {
    return expr_;
  }

 private:
  QLOperator yb_op_;
  PTExpr::SharedPtr expr_;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_COLUMN_ARG_H_
