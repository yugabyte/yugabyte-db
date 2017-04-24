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

#include "yb/common/yql_value.h"
#include "yb/util/bfyql/bfyql.h"

namespace yb {
namespace sql {

class PTRef;
class PTBindVar;

//--------------------------------------------------------------------------------------------------

enum class ExprOperator : int {
  kNoOp = 0,

  // Reference to constants, columns, and variables.
  kConst,
  kAlias,
  kRef,
  kBindVar,
  kBfunc,

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
  kCollection,
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
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType yql_type_id = DataType::UNKNOWN_DATA)
      : TreeNode(memctx, loc),
        op_(op),
        internal_type_(internal_type),
        yql_type_id_(yql_type_id) {
  }
  virtual ~PTExpr() {
  }

  // Expression return type in DocDB format.
  virtual InternalType internal_type() const {
    return internal_type_;
  }

  bool has_valid_internal_type() {
    // internal_type_ is not set in case of PTNull.
    return yql_type_id_ == DataType::NULL_VALUE_TYPE ||
           internal_type_ != InternalType::VALUE_NOT_SET;
  }

  // Expression return type in Cassandra format.
  virtual DataType yql_type_id() const {
    return yql_type_id_;
  }

  bool has_valid_yql_type_id() {
    return yql_type_id_ != DataType::UNKNOWN_DATA;
  }

  virtual void set_yql_type_id(DataType t) {
    yql_type_id_ = t;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTExpr;
  }

  // Returns the expression operator.
  virtual ExprOperator expr_op() const {
    return op_;
  }

  // Predicate for null.
  virtual bool is_null() {
    return yql_type_id_ == DataType::NULL_VALUE_TYPE;
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
  //   yql_type_id_ and internal_type_ for expressions.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override = 0;

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
  virtual CHECKED_STATUS AnalyzeLeftRightOperands(SemContext *sem_context,
                                                  PTExpr::SharedPtr lhs,
                                                  PTExpr::SharedPtr rhs);

  // Analyze LHS expression.
  virtual CHECKED_STATUS CheckLhsExpr(SemContext *sem_context);

  // Analyze RHS expression.
  virtual CHECKED_STATUS CheckRhsExpr(SemContext *sem_context);

  // Experimental: Convert current treenode to YQLValue.
  virtual std::shared_ptr<YQLValue> ToYqlValue(yb::bfyql::BFOpcode cast_opcode);

 protected:
  // Set the name of unnamed bind marker to the column it is associated with.
  static void SetVariableName(MemoryContext *memctx, const PTRef *ref, PTBindVar *var);

  ExprOperator op_;
  InternalType internal_type_;
  DataType yql_type_id_;
};

using PTExprListNode = TreeListNode<PTExpr>;

//--------------------------------------------------------------------------------------------------
// Template for expression with no operand (0 input).
template<InternalType itype, DataType ytype>
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
      : PTExpr(memctx, loc, op, itype, ytype) {
  }
  virtual ~PTExpr0() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr0::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr0>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Analyze this node operator and setup its yql_type_id_ and internal_type_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context));

    // Make sure that it has valid data type.
    CHECK(has_valid_internal_type() && has_valid_yql_type_id());
    return Status::OK();
  }
};

//--------------------------------------------------------------------------------------------------
// Template for expression with one operand (1 input).
template<InternalType itype, DataType ytype>
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
      : PTExpr(memctx, loc, op, itype, ytype),
        op1_(op1) {
  }
  virtual ~PTExpr1() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr1::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr1>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTExpr::SharedPtr op1() const override {
    return op1_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Run semantic analysis on child nodes.
    RETURN_NOT_OK(op1_->Analyze(sem_context));

    // Analyze this node operator and setup its yql_type_id_ and internal_type_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context, op1_));

    // Make sure that it has valid data type.
    CHECK(has_valid_internal_type() && has_valid_yql_type_id());
    return Status::OK();
  }

 private:
  //------------------------------------------------------------------------------------------------
  // Operand.
  PTExpr::SharedPtr op1_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (2 inputs).
template<InternalType itype, DataType ytype>
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
      : PTExpr(memctx, loc, op, itype, ytype),
        op1_(op1),
        op2_(op2) {
    // Set the name of unnamed bind marker for "... WHERE <column> <op> ? ..."
    if (op1_ != nullptr && op1_->expr_op() == ExprOperator::kRef &&
        op2_ != nullptr && op2_->expr_op() == ExprOperator::kBindVar) {
      SetVariableName(memctx,
                      static_cast<const PTRef*>(op1_.get()),
                      static_cast<PTBindVar*>(op2_.get()));
    }
  }
  virtual ~PTExpr2() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr2::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr2>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTExpr::SharedPtr op1() const override {
    return op1_;
  }

  const PTExpr::SharedPtr op2() const override {
    return op2_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Run semantic analysis on child nodes.
    RETURN_NOT_OK(op1_->Analyze(sem_context));
    RETURN_NOT_OK(op2_->Analyze(sem_context));

    // Analyze this node operator and setup its yql_type_id_ and internal_type_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context, op1_, op2_));

    // Make sure that it has valid data type.
    CHECK(has_valid_internal_type() && has_valid_yql_type_id());
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
template<InternalType itype, DataType ytype>
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
      : PTExpr(memctx, loc, op, itype, ytype),
        op1_(op1),
        op2_(op2),
        op3_(op3) {
    // Set the name of unnamed bind marker for "... WHERE <column> <op> ? ..."
    if (op1_ != nullptr && op1_->expr_op() == ExprOperator::kRef) {
      const PTRef *ref = static_cast<const PTRef*>(op1_.get());
      if (op2_ != nullptr && op2_->expr_op() == ExprOperator::kBindVar) {
        SetVariableName(memctx, ref, static_cast<PTBindVar*>(op2_.get()));
      }
      if (op3_ != nullptr && op3_->expr_op() == ExprOperator::kBindVar) {
        SetVariableName(memctx, ref, static_cast<PTBindVar*>(op3_.get()));
      }
    }
  }
  virtual ~PTExpr3() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr3::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr3>(memctx, std::forward<TypeArgs>(args)...);
  }

  const PTExpr::SharedPtr op1() const override {
    return op1_;
  }

  const PTExpr::SharedPtr op2() const override {
    return op2_;
  }

  const PTExpr::SharedPtr op3() const override {
    return op3_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Run semantic analysis on child nodes.
    RETURN_NOT_OK(op1_->Analyze(sem_context));
    RETURN_NOT_OK(op2_->Analyze(sem_context));
    RETURN_NOT_OK(op3_->Analyze(sem_context));

    // Analyze this node operator and setup its yql_type_id_ and internal_type_.
    RETURN_NOT_OK(AnalyzeOperator(sem_context, op1_, op2_, op3_));

    // Make sure that it has valid data type.
    CHECK(has_valid_internal_type() && has_valid_yql_type_id());
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
//--------------------------------------------------------------------------------------------------
// Template for constant expressions.
template<InternalType itype,
    DataType ytype,
    typename ReturnType>
class PTExprConst : public PTExpr0<itype, ytype> {
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
      : PTExpr0<itype, ytype>(memctx, loc, ExprOperator::kConst),
        value_(value) {
  }
  virtual ~PTExprConst() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExprConst::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExprConst>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context) override {
    // Nothing to do: constant expressions should be initialized with valid data type already
    return Status::OK();
  };


  // Evaluate this expression and its operand.
  virtual ReturnType Eval() const {
    return value_;
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Constant value.
  ReturnType value_;
};

using PTConstArg = PTExprConst<InternalType::VALUE_NOT_SET,
                               DataType::UNKNOWN_DATA,
                               void*>;

using PTNull = PTExprConst<InternalType::VALUE_NOT_SET,
                           DataType::NULL_VALUE_TYPE,
                           void*>;

using PTConstInt = PTExprConst<InternalType::kInt64Value,
                               DataType::INT64,
                               int64_t>;

using PTConstDouble = PTExprConst<InternalType::kDoubleValue,
                                  DataType::DOUBLE,
                                  long double>;

using PTConstText = PTExprConst<InternalType::kStringValue,
                                DataType::STRING,
                                MCString::SharedPtr>;

using PTConstVarInt = PTExprConst<InternalType::kVarintStringValue,
                                  DataType::VARINT,
                                  MCString::SharedPtr>;

using PTConstDecimal = PTExprConst<InternalType::kDecimalValue,
                                   DataType::DECIMAL,
                                   MCString::SharedPtr>;

using PTConstUuid = PTExprConst<InternalType::kUuidValue,
                                DataType::UUID,
                                MCString::SharedPtr>;

using PTConstBool = PTExprConst<InternalType::kBoolValue,
                                DataType::BOOL,
                                bool>;

using PTConstBinary = PTExprConst<InternalType::kBinaryValue,
                                  DataType::BINARY,
                                  MCString::SharedPtr>;

//--------------------------------------------------------------------------------------------------
// Tree Nodes for Collections -- treated as expressions with flexible arity
//--------------------------------------------------------------------------------------------------

class PTMapExpr : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTMapExpr> SharedPtr;
  typedef MCSharedPtr<const PTMapExpr> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTMapExpr(MemoryContext *memctx, YBLocation::SharedPtr loc)
      : PTExpr(memctx, loc, ExprOperator::kCollection, InternalType::kMapValue, DataType::MAP),
        keys_(memctx), values_(memctx) { }
  virtual ~PTMapExpr() { }

  void Insert(PTExpr::SharedPtr key, PTExpr::SharedPtr value) {
    keys_.emplace_back(key);
    values_.emplace_back(value);
  }

  int size() const {
    DCHECK_EQ(keys_.size(), values_.size());
    return static_cast<int>(keys_.size());
  }

  const MCList<PTExpr::SharedPtr> keys() const {
    return keys_;
  }

  const MCList<PTExpr::SharedPtr> values() const {
    return values_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Run semantic analysis on collection elements.
    for (auto& key : keys_) {
      RETURN_NOT_OK(key->Analyze(sem_context));
    }
    for (auto& value : values_) {
      RETURN_NOT_OK(value->Analyze(sem_context));
    }
    // We don't know the expected type parameters yet so we check convertability during execution

    return Status::OK();
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTMapExpr::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTMapExpr>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  // this is intentionally not MCMap because we don't have the right equality relation for keys at
  // this point which can lead to subtle bugs later
  MCList<PTExpr::SharedPtr> keys_;
  MCList<PTExpr::SharedPtr> values_;

};

class PTSetExpr : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTSetExpr> SharedPtr;
  typedef MCSharedPtr<const PTSetExpr> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTSetExpr(MemoryContext *memctx,
             YBLocation::SharedPtr loc)
      : PTExpr(memctx, loc, ExprOperator::kCollection, InternalType::kSetValue, DataType::SET),
        value_(memctx) { }

  virtual ~PTSetExpr() { }

  void Insert(PTExpr::SharedPtr elem) {
    value_.emplace_back(elem);
  }

  const MCList<PTExpr::SharedPtr> elems() const {
    return value_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Run semantic analysis on collection elements.
    for (auto& elem : value_) {
      RETURN_NOT_OK(elem->Analyze(sem_context));
    }
    // We don't know the expected type parameter yet so we check convertability during execution

    return Status::OK();
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTSetExpr::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTSetExpr>(memctx, std::forward<TypeArgs>(args)...);
  }
 private:
  // this is intentionally not MCSet because we don't have the right equality relation at this point
  // e.g. 0 and '1970-1-1' are different expressions but would represent same value as Timestamps
  MCList<PTExpr::SharedPtr> value_;
};


class PTListExpr : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTListExpr> SharedPtr;
  typedef MCSharedPtr<const PTListExpr> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTListExpr(MemoryContext *memctx, YBLocation::SharedPtr loc)
      : PTExpr(memctx, loc, ExprOperator::kCollection, InternalType::kListValue, DataType::LIST),
        value_(memctx) { }
  virtual ~PTListExpr() { }

  void Append(PTExpr::SharedPtr elem) {
    value_.emplace_back(elem);
  }

  const MCList<PTExpr::SharedPtr> elems() const {
    return value_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    // Run semantic analysis on collection elements.
    for (auto& elem : value_) {
      RETURN_NOT_OK(elem->Analyze(sem_context));
    }
    // We don't know the expected type parameter yet so we check convertability during execution

    return Status::OK();
  }

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTListExpr::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTListExpr>(memctx, std::forward<TypeArgs>(args)...);
  }
 private:
  MCList<PTExpr::SharedPtr> value_;
};

// Tree node for comparisons.
using PTPredicate0 = PTExpr0<InternalType::kBoolValue, DataType::BOOL>;
using PTPredicate1 = PTExpr1<InternalType::kBoolValue, DataType::BOOL>;
using PTPredicate2 = PTExpr2<InternalType::kBoolValue, DataType::BOOL>;
using PTPredicate3 = PTExpr3<InternalType::kBoolValue, DataType::BOOL>;

// Operators: '+', '-', '*', '/', etc.
// The datatypes for these operators cannot be determined at parsing time.
using PTOperator0 = PTExpr0<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA>;
using PTOperator1 = PTExpr1<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA>;
using PTOperator2 = PTExpr2<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA>;
using PTOperator3 = PTExpr3<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA>;

//--------------------------------------------------------------------------------------------------
// Column Reference. The datatype of this expression would need to be resolved by the analyzer.
class PTRef : public PTOperator0 {
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
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context) override;

  // Access function for name.
  const PTQualifiedName::SharedPtr& name() const {
    return name_;
  }

  // Access function for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTRef;
  }

 private:
  PTQualifiedName::SharedPtr name_;

  // Fields that should be resolved by semantic analysis.
  const ColumnDesc *desc_;
};

//--------------------------------------------------------------------------------------------------
// Expression alias - Name of an expression including reference to column.
class PTExprAlias : public PTOperator1 {
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

  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) override;

 private:
  MCString::SharedPtr alias_;
};

//--------------------------------------------------------------------------------------------------
// Bind variable. The datatype of this expression would need to be resolved by the analyzer.
class PTBindVar : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTBindVar> SharedPtr;
  typedef MCSharedPtr<const PTBindVar> SharedPtrConst;

  // Unset bind position.
  static constexpr int64_t kUnsetPosition = INT64_MIN;

  // Compare 2 bind variable positions in a statement.
  struct SetCmp {
    bool operator() (const PTBindVar* v1, const PTBindVar* v2) const {
      const YBLocation& l1 = v1->loc();
      const YBLocation& l2 = v2->loc();
      if (l1.BeginLine() < l2.BeginLine()) {
        return true;
      } else if (l1.BeginLine() == l2.BeginLine()) {
        return l1.BeginColumn() < l2.BeginColumn();
      } else {
        return false;
      }
    }
  };

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTBindVar(MemoryContext *memctx,
            YBLocation::SharedPtr loc,
            const MCString::SharedPtr& name = nullptr);
  PTBindVar(MemoryContext *memctx,
            YBLocation::SharedPtr loc,
            int64_t pos);
  virtual ~PTBindVar();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTBindVar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTBindVar>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  // Access functions for position.
  int64_t pos() const {
    return pos_;
  }
  void set_pos(const int64_t pos) {
    pos_ = pos;
  }
  bool is_unset_pos() const {
    return pos_ == kUnsetPosition;
  }

  // Access functions for name.
  MCString::SharedPtr name() const {
    return name_;
  }
  void set_name(MemoryContext *memctx, const MCString& name) {
    name_ = MCString::MakeShared(memctx, name.c_str());
  }

  // Access functions for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }
  void set_desc(const ColumnDesc * desc) {
    desc_ = desc;
    yql_type_id_ = desc->yql_type().main();
  }

  // Expression return type in DocDB format.
  virtual InternalType internal_type() const override {
    DCHECK(desc_ != nullptr);
    return desc_->internal_type();
  }

  // Expression return type in Cassandra format.
  YQLType yql_type() const {
    DCHECK(desc_ != nullptr);
    return desc_->yql_type();
  }

  virtual DataType yql_type_id() const override {
    DCHECK(desc_ != nullptr);
    return desc_->yql_type().main();
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTBindVar;
  }

  // Access to op_.
  virtual ExprOperator expr_op() const override {
    return ExprOperator::kBindVar;
  }

  // Reset to clear and release previous semantics analysis results.
  virtual void Reset() override;

 private:
  // 0-based position.
  int64_t pos_;
  // Variable name.
  MCString::SharedPtr name_;

  // Fields that should be resolved by semantic analysis.
  const ColumnDesc *desc_;
};

//--------------------------------------------------------------------------------------------------
class PTBfunc : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTBfunc> SharedPtr;
  typedef MCSharedPtr<const PTBfunc> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTBfunc(MemoryContext *memctx,
          YBLocation::SharedPtr loc,
          const MCString::SharedPtr& name,
          PTExprListNode::SharedPtr args);
  virtual ~PTBfunc();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTBfunc::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTBfunc>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  // Experimental: Convert current treenode to YQLValue.
  virtual std::shared_ptr<YQLValue> ToYqlValue(yb::bfyql::BFOpcode cast_opcode) override;

  // Return the result of evaluation.
  PTExpr::SharedPtr result() const {
    return result_;
  }

 private:
  // Find opcode to convert actual to formal yql_type_id.
  Status FindCastOpcode(DataType source, DataType target, yb::bfyql::BFOpcode *opcode);

  // Builtin function name.
  MCString::SharedPtr name_;

  // Arguments to builtin call.
  PTExprListNode::SharedPtr args_;

  // Builtin opcode.
  bfyql::BFOpcode opcode_;

  // Casting arguments to correct datatype before calling the builtin-function.
  MCVector<yb::bfyql::BFOpcode> cast_ops_;

  // Initialize result to null value.
  PTExpr::SharedPtr result_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_EXPR_H_
