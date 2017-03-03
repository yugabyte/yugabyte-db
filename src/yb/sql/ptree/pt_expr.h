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

  // Operators that take no operand.
  kExists,
  kNotExists,

  // Operators that take one operand.
  kUMinus,

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

  // Operators that take three operands.
  kBetween,
  kNotBetween,

  // Operators that take unspecified number of operands.
  kIn,
  kNotIn,
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
  explicit PTExpr(
      MemoryContext *memctx,
      YBLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::DataType type_id = yb::DataType::UNKNOWN_DATA,
      client::YBColumnSchema::DataType sql_type = client::YBColumnSchema::MAX_TYPE_INDEX)
      : TreeNode(memctx, loc),
        op_(op),
        type_id_(type_id),
        sql_type_(sql_type) {
  }
  virtual ~PTExpr() {
  }

  // Expression return type in Cassandra format.
  virtual yb::DataType type_id() const {
    return type_id_;
  }

  bool has_valid_type_id() {
    return type_id_ != yb::DataType::UNKNOWN_DATA;
  }

  // Expression return type in DocDB format.
  virtual client::YBColumnSchema::DataType sql_type() const {
    return sql_type_;
  }

  bool has_valid_sql_type() {
    return sql_type_ != client::YBColumnSchema::MAX_TYPE_INDEX;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTExpr;
  }

  // Returns the expression operator.
  virtual ExprOperator expr_op() const {
    return op_;
  }

  // Predicate for null.
  virtual bool is_null() {
    return sql_type_ == client::YBColumnSchema::NULL_VALUE_TYPE;
  }

  // Returns the operands of an expression.
  virtual const PTExpr::SharedPtr op1() const {
    return nullptr;
  }

  virtual const PTExpr::SharedPtr op2() const {
    return nullptr;
  }

  virtual const PTExpr::SharedPtr op3() const {
    return nullptr;
  }

  // All expressions must define this Analyze() function, which does the following steps.
  // - Call Analyze() on child treenodes to run semantic analysis on the child nodes. The child
  //   nodes will then call their own child nodes and so forth. In short, we traverse the expression
  //   parse tree to run semantic analysis on them.
  // - Run semantic analysis on this node.
  // - The main job of semantics analysis is to run type resolution to find the correct values for
  //   sql_type_ and type_id_ for expressions.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE = 0;

  // These functions are called by analyze to run type resolution on this expression.
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context);
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1);
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1,
                                         PTExpr::SharedPtr op2);
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1,
                                         PTExpr::SharedPtr op2,
                                         PTExpr::SharedPtr op3);

  // Analyze LHS expression.
  virtual CHECKED_STATUS AnalyzeLhsExpr(SemContext *sem_context);

  // Analyze RHS expression.
  virtual CHECKED_STATUS AnalyzeRhsExpr(SemContext *sem_context);

 protected:
  ExprOperator op_;
  yb::DataType type_id_;
  client::YBColumnSchema::DataType sql_type_;
};

using PTExprListNode = TreeListNode<PTExpr>;

//--------------------------------------------------------------------------------------------------
// Template for expression with no operand (0 input).
template<yb::DataType itype,
         client::YBColumnSchema::DataType stype,
         typename ReturnType>
class PTExprConst : public PTExpr {
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
      : PTExpr(memctx, loc, ExprOperator::kConst, itype, stype),
        value_(value) {
  }
  virtual ~PTExprConst() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExprConst::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExprConst>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE {
    // Analyze this node operator and setup its sql_type_ and type_id_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context));

    // Make sure that it has valid data type.
    CHECK(has_valid_type_id() && has_valid_sql_type());
    return Status::OK();
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
template<yb::DataType itype, client::YBColumnSchema::DataType stype>
class PTExpr0 : public PTExpr {
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
      : PTExpr(memctx, loc, op, itype, stype) {
  }
  virtual ~PTExpr0() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr0::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr0>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE {
    // Analyze this node operator and setup its sql_type_ and type_id_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context));

    // Make sure that it has valid data type.
    CHECK(has_valid_type_id() && has_valid_sql_type());
    return Status::OK();
  }
};

//--------------------------------------------------------------------------------------------------
// Template for expression with one operand (1 input).
template<yb::DataType itype, client::YBColumnSchema::DataType stype>
class PTExpr1 : public PTExpr {
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
      : PTExpr(memctx, loc, op, itype, stype),
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

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE {
    // Run semantic analysis on child nodes.
    RETURN_NOT_OK(op1_->Analyze(sem_context));

    // Analyze this node operator and setup its sql_type_ and type_id_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context, op1_));

    // Make sure that it has valid data type.
    CHECK(has_valid_type_id() && has_valid_sql_type());
    return Status::OK();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Operand.
  PTExpr::SharedPtr op1_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (2 inputs).
template<yb::DataType itype, client::YBColumnSchema::DataType stype>
class PTExpr2 : public PTExpr {
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
      : PTExpr(memctx, loc, op, itype, stype),
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

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE {
    // Run semantic analysis on child nodes.
    RETURN_NOT_OK(op1_->Analyze(sem_context));
    RETURN_NOT_OK(op2_->Analyze(sem_context));

    // Analyze this node operator and setup its sql_type_ and type_id_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context, op1_, op2_));

    // Make sure that it has valid data type.
    CHECK(has_valid_type_id() && has_valid_sql_type());
    return Status::OK();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Operand 1 and 2.
  PTExpr::SharedPtr op1_;
  PTExpr::SharedPtr op2_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (3 inputs).
template<yb::DataType itype, client::YBColumnSchema::DataType stype>
class PTExpr3 : public PTExpr {
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
      : PTExpr(memctx, loc, op, itype, stype),
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

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE {
    // Run semantic analysis on child nodes.
    RETURN_NOT_OK(op1_->Analyze(sem_context));
    RETURN_NOT_OK(op2_->Analyze(sem_context));
    RETURN_NOT_OK(op3_->Analyze(sem_context));

    // Analyze this node operator and setup its sql_type_ and type_id_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context, op1_, op2_, op3_));

    // Make sure that it has valid data type.
    CHECK(has_valid_type_id() && has_valid_sql_type());
    return Status::OK();
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
using PTNull = PTExprConst<yb::DataType::UNKNOWN_DATA,
                           client::YBColumnSchema::NULL_VALUE_TYPE,
                           void*>;

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
using PTPredicate0 = PTExpr0<yb::DataType::BOOL, client::YBColumnSchema::BOOL>;
using PTPredicate1 = PTExpr1<yb::DataType::BOOL, client::YBColumnSchema::BOOL>;
using PTPredicate2 = PTExpr2<yb::DataType::BOOL, client::YBColumnSchema::BOOL>;
using PTPredicate3 = PTExpr3<yb::DataType::BOOL, client::YBColumnSchema::BOOL>;

// Operators: '+', '-', '*', '/', etc.
// The datatypes for these operators cannot be determined at parsing time.
using PTOperator0 = PTExpr0<yb::DataType::UNKNOWN_DATA, client::YBColumnSchema::MAX_TYPE_INDEX>;
using PTOperator1 = PTExpr1<yb::DataType::UNKNOWN_DATA, client::YBColumnSchema::MAX_TYPE_INDEX>;
using PTOperator2 = PTExpr2<yb::DataType::UNKNOWN_DATA, client::YBColumnSchema::MAX_TYPE_INDEX>;
using PTOperator3 = PTExpr3<yb::DataType::UNKNOWN_DATA, client::YBColumnSchema::MAX_TYPE_INDEX>;

//--------------------------------------------------------------------------------------------------
// Column Reference. The datatype of this expression would need to be resolved by the analyzer.
// TODO(mihnea) This class should be a subclass of PTOperator0. I'm not sure about this TODO, but
// please investigate if it makes sense.
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
  void PrintSemanticAnalysisResult(SemContext *sem_context);
  // TODO(mihnea) This Analyze() function should be changed to AnalyzeOperator(), which will be
  // called by Analyze() automatically by the tree traversal.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;

  // Access function for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const OVERRIDE {
    return TreeNodeOpcode::kPTRef;
  }

 private:
  PTQualifiedName::SharedPtr name_;

  // Fields that should be resolved by semantic analysis.
  const ColumnDesc *desc_;
};

//--------------------------------------------------------------------------------------------------
// Expression alias - Name of an expression including reference to column.
// TODO(mihnea) This class should be a subclass of PTOperator1. I'm not sure about this TODO, but
// please investigate if it makes sense.
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

  // TODO(mihnea) This Analyze() function should be changed to AnalyzeOperator(ctx, op1), which will
  // be called by Analyze() automatically by the tree traversal.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) OVERRIDE;

 private:
  // TODO(mihnea) expr_ should be removed, and op1_ should be in its place.
  PTExpr::SharedPtr expr_;
  MCString::SharedPtr alias_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_EXPR_H_
