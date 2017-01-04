//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Tree node definitions for expression.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_PT_EXPR_H_
#define YB_SQL_PTREE_PT_EXPR_H_

#include "yb/sql/ptree/column_desc.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/pt_type.h"
#include "yb/sql/ptree/pt_name.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

enum class ExprOperator : int {
  kNoOp = 0,

  // Reference to constants, columns, and variables.
  kConst,
  kAlias,
  kRef,

  // Operators that take one operand.
  kNot,
  kIsNull,
  kIsNotNull,
  kIsTrue,
  kIsFalse,

  // Operators that take two operands.
  kEQ,
  kLT,
  kGT,
  kLE,
  kGE,
  kNE,
  kAND,
  kOR,
  kLike,
  kNotLike,
  kIn,
  kNotIn,

  // Operators that take three operands.
  kBetween,
  kNotBetween,
};

//--------------------------------------------------------------------------------------------------

// Base class for all expressions.
class PTExpr : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExpr> SharedPtr;
  typedef MCSharedPtr<const PTExpr> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTExpr(MemoryContext *memctx = nullptr, YBLocation::SharedPtr loc = nullptr)
      : TreeNode(memctx, loc) {
  }
  virtual ~PTExpr() {
  }

  // Expression return type in Cassandra format.
  virtual yb::DataType type_id() const = 0;

  // Expression return type in DocDB format.
  virtual client::YBColumnSchema::DataType sql_type() const = 0;

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTExpr;
  }

  // Returns the expression operator.
  virtual ExprOperator expr_op() const = 0;
};

using PTExprListNode = TreeListNode<PTExpr>;

//--------------------------------------------------------------------------------------------------

// Template for all expressions.
template<yb::DataType type_id_,
         client::YBColumnSchema::DataType sql_type_,
         typename ReturnType>
class PTExprOperator : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExprOperator> SharedPtr;
  typedef MCSharedPtr<const PTExprOperator> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTExprOperator(MemoryContext *memctx = nullptr,
                          YBLocation::SharedPtr loc = nullptr,
                          ExprOperator op = ExprOperator::kNoOp)
      : PTExpr(memctx, loc),
        op_(op) {
  }
  virtual ~PTExprOperator() {
  }

  // Expresion return type in Cassandra format.
  virtual yb::DataType type_id() const OVERRIDE {
    return type_id_;
  }

  // Expresion return type in DocDB format.
  virtual client::YBColumnSchema::DataType sql_type() const OVERRIDE {
    return sql_type_;
  }

  // Access to op_.
  ExprOperator expr_op() const {
    return op_;
  }

 protected:
  ExprOperator op_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with no operand (0 input).
template<yb::DataType type_id_,
         client::YBColumnSchema::DataType sql_type_,
         typename ReturnType>
class PTExprConst : public PTExprOperator<type_id_, sql_type_, ReturnType> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExprConst> SharedPtr;
  typedef MCSharedPtr<const PTExprConst> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExprConst(MemoryContext *memctx,
              YBLocation::SharedPtr loc,
              ReturnType value)
      : PTExprOperator<type_id_, sql_type_, ReturnType>(memctx, loc, ExprOperator::kConst),
        value_(value) {
  }
  virtual ~PTExprConst() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExprConst::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExprConst>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Nothing to do.
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE {
    return ErrorCode::SUCCESSFUL_COMPLETION;
  }

  // Evaluate this expression and its operand.
  virtual ReturnType Eval() const {
    return value_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Constant value.
  ReturnType value_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with no operand (0 input).
template<yb::DataType type_id_,
         client::YBColumnSchema::DataType sql_type_,
         typename ReturnType>
class PTExpr0 : public PTExprOperator<type_id_, sql_type_, ReturnType> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExpr0> SharedPtr;
  typedef MCSharedPtr<const PTExpr0> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExpr0(MemoryContext *memctx,
          YBLocation::SharedPtr loc,
          ExprOperator op)
      : PTExprOperator<type_id_, sql_type_, ReturnType>(memctx, loc, op) {
  }
  virtual ~PTExpr0() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr0::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr0>(memctx, std::forward<TypeArgs>(args)...);
  }
};

//--------------------------------------------------------------------------------------------------
// Template for expression with one operand (1 input).
template<yb::DataType type_id_,
         client::YBColumnSchema::DataType sql_type_,
         typename ReturnType>
class PTExpr1 : public PTExprOperator<type_id_, sql_type_, ReturnType> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExpr1> SharedPtr;
  typedef MCSharedPtr<const PTExpr1> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExpr1(MemoryContext *memctx,
          YBLocation::SharedPtr loc,
          ExprOperator op,
          PTExpr::SharedPtr op1)
      : PTExprOperator<type_id_, sql_type_, ReturnType>(memctx, loc, op),
        op1_(op1) {
  }
  virtual ~PTExpr1() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr1::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr1>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTExpr::SharedPtr op1() const {
    return op1_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Operand.
  PTExpr::SharedPtr op1_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (2 inputs).
template<yb::DataType type_id_,
         client::YBColumnSchema::DataType sql_type_,
         typename ReturnType>
class PTExpr2 : public PTExprOperator<type_id_, sql_type_, ReturnType> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExpr2> SharedPtr;
  typedef MCSharedPtr<const PTExpr2> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExpr2(MemoryContext *memctx,
          YBLocation::SharedPtr loc,
          ExprOperator op,
          const PTExpr::SharedPtr& op1,
          const PTExpr::SharedPtr& op2)
      : PTExprOperator<type_id_, sql_type_, ReturnType>(memctx, loc, op),
        op1_(op1),
        op2_(op2) {
  }
  virtual ~PTExpr2() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr2::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr2>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTExpr::SharedPtr op1() const {
    return op1_;
  }

  const PTExpr::SharedPtr op2() const {
    return op2_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Operand 1 and 2.
  PTExpr::SharedPtr op1_;
  PTExpr::SharedPtr op2_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (3 inputs).
template<yb::DataType type_id_,
         client::YBColumnSchema::DataType sql_type_,
         typename ReturnType>
class PTExpr3 : public PTExprOperator<type_id_, sql_type_, ReturnType> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExpr3> SharedPtr;
  typedef MCSharedPtr<const PTExpr3> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExpr3(MemoryContext *memctx,
          YBLocation::SharedPtr loc,
          ExprOperator op,
          const PTExpr::SharedPtr& op1,
          const PTExpr::SharedPtr& op2,
          const PTExpr::SharedPtr& op3)
      : PTExprOperator<type_id_, sql_type_, ReturnType>(memctx, loc, op),
        op1_(op1),
        op2_(op2),
        op3_(op3) {
  }
  virtual ~PTExpr3() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr3::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr3>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTExpr::SharedPtr op1() const {
    return op1_;
  }

  const PTExpr::SharedPtr op2() const {
    return op2_;
  }

  const PTExpr::SharedPtr op3() const {
    return op3_;
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Operand 1 and 2.
  PTExpr::SharedPtr op1_;
  PTExpr::SharedPtr op2_;
  PTExpr::SharedPtr op3_;
};

//--------------------------------------------------------------------------------------------------
// Tree node for constants
using PTConstInt = PTExprConst<yb::DataType::INT64,
                               client::YBColumnSchema::INT64,
                               int64_t>;

using PTConstDouble = PTExprConst<yb::DataType::DOUBLE,
                                  client::YBColumnSchema::DOUBLE,
                                  long double>;

using PTConstText = PTExprConst<yb::DataType::STRING,
                                client::YBColumnSchema::STRING,
                                MCString::SharedPtr>;

using PTConstBool = PTExprConst<yb::DataType::BOOL,
                                client::YBColumnSchema::BOOL,
                                bool>;

// Tree node for comparisons.
using PTPredicate1 = PTExpr1<yb::DataType::BOOL, client::YBColumnSchema::BOOL, bool>;
using PTPredicate2 = PTExpr2<yb::DataType::BOOL, client::YBColumnSchema::BOOL, bool>;
using PTPredicate3 = PTExpr3<yb::DataType::BOOL, client::YBColumnSchema::BOOL, bool>;

//--------------------------------------------------------------------------------------------------
// Column Reference. The datatype of this expression would need to be resolved by the analyzer.
class PTRef : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTRef> SharedPtr;
  typedef MCSharedPtr<const PTRef> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTRef(MemoryContext *memctx,
        YBLocation::SharedPtr loc,
        const PTQualifiedName::SharedPtr& name);
  virtual ~PTRef();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTRef::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTRef>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual ErrorCode Analyze(SemContext *sem_context) OVERRIDE;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Access function for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }

  // Expression return type in Cassandra format.
  virtual yb::DataType type_id() const OVERRIDE {
    DCHECK(desc_ != nullptr);
    return desc_->type_id();
  }

  // Expression return type in DocDB format.
  virtual client::YBColumnSchema::DataType sql_type() const OVERRIDE {
    DCHECK(desc_ != nullptr);
    return desc_->sql_type();
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTRef;
  }

  // Access to op_.
  virtual ExprOperator expr_op() const OVERRIDE {
    return ExprOperator::kRef;
  }

 private:
  PTQualifiedName::SharedPtr name_;

  // Fields that should be resolved by semantic analysis.
  const ColumnDesc *desc_;
};

//--------------------------------------------------------------------------------------------------
// Expression alias - Name of an expression including reference to column.
class PTExprAlias : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExprAlias> SharedPtr;
  typedef MCSharedPtr<const PTExprAlias> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExprAlias(MemoryContext *memctx,
              YBLocation::SharedPtr loc,
              const PTExpr::SharedPtr& expr,
              const MCString::SharedPtr& alias);
  virtual ~PTExprAlias();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTExprAlias::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExprAlias>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Expresion return type in Cassandra format.
  virtual yb::DataType type_id() const {
    return expr_->type_id();
  }

  // Expresion return type in DocDB format.
  virtual client::YBColumnSchema::DataType sql_type() const {
    return expr_->sql_type();
  }

  // Access to op_.
  virtual ExprOperator expr_op() const OVERRIDE {
    return ExprOperator::kAlias;
  }

 private:
  PTExpr::SharedPtr expr_;
  MCString::SharedPtr alias_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_EXPR_H_
