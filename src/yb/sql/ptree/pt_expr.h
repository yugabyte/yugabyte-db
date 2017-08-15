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
#include "yb/sql/ptree/sem_state.h"

#include "yb/common/yql_value.h"
#include "yb/util/bfyql/bfyql.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// The order of the following enum values are not important.
enum class ExprOperator : int {
  kNoOp = 0,

  // Reference to constants, columns, and variables.
  kConst = 1,
  kAlias = 2,
  kRef = 3,
  kSubColRef = 4,
  kBindVar = 5,
  kBcall = 6,

  // Operators that take one operand.
  kUMinus = 7,

  // Logical operators with one operand.
  kLogic1 = 8,

  // Logical operators with two operands.
  kLogic2 = 9,

  // Relation operators that take no operand.
  kRelation0 = 10,

  // Relation operators that take one operand.
  kRelation1 = 11,

  // Relation operators that take two operands.
  kRelation2 = 12,

  // Relation operators that take three operands.
  kRelation3 = 13,

  // Relation operators that take unspecified number of operands.
  kCollection = 14,
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
      yb::YQLOperator yql_op = yb::YQLOperator::YQL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType yql_type_id = DataType::UNKNOWN_DATA)
      : TreeNode(memctx, loc),
        op_(op),
        yql_op_(yql_op),
        internal_type_(internal_type),
        yql_type_(YQLType::Create(yql_type_id)),
        expected_internal_type_(InternalType::VALUE_NOT_SET) {
  }
  virtual ~PTExpr() {
  }

  // Expression return type in DocDB format.
  virtual InternalType internal_type() const {
    return internal_type_;
  }

  bool has_valid_internal_type() {
    // internal_type_ is not set in case of PTNull.
    return yql_type_->main() == DataType::NULL_VALUE_TYPE ||
           internal_type_ != InternalType::VALUE_NOT_SET;
  }

  virtual InternalType expected_internal_type() const {
    return expected_internal_type_;
  }

  // Expression return type in YQL format.
  virtual const std::shared_ptr<YQLType>& yql_type() const {
    return yql_type_;
  }

  virtual void set_yql_type(const std::shared_ptr<YQLType>& yql_type) {
    yql_type_ = yql_type;
  }

  virtual void set_yql_type(DataType type_id) {
    yql_type_ = YQLType::Create(type_id);
  }

  // TODO(neil or mihnea) Remove or replace all yql_type_id API & comments with YQLType.
  virtual DataType yql_type_id() const {
    return yql_type_->main();
  }

  virtual void set_yql_type_id(DataType type_id) {
    yql_type_ = YQLType::Create(type_id);
  }

  bool has_valid_yql_type_id() {
    return yql_type_->main() != DataType::UNKNOWN_DATA;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTExpr;
  }

  // Returns the expression operator.
  virtual ExprOperator expr_op() const {
    return op_;
  }

  yb::YQLOperator yql_op() const {
    return yql_op_;
  }

  // Predicate for null.
  virtual bool is_null() const {
    return yql_type_->main() == DataType::NULL_VALUE_TYPE;
  }

  // Returns the operands of an expression.
  virtual PTExpr::SharedPtr op1() const {
    return nullptr;
  }

  virtual PTExpr::SharedPtr op2() const {
    return nullptr;
  }

  virtual PTExpr::SharedPtr op3() const {
    return nullptr;
  }

  // Predicate for constant tree node.
  bool is_constant() const {
    return ((expr_op() == ExprOperator::kConst) ||
            (expr_op() == ExprOperator::kUMinus && op1()->expr_op() == ExprOperator::kConst));
  }

  // Predicate for updating counter.  Only '+' and '-' expression support counter update.
  virtual CHECKED_STATUS CheckCounterUpdateSupport(SemContext *sem_context) const;

  // All expressions must define this Analyze() function, which does the following steps.
  // - Call Analyze() on child treenodes to run semantic analysis on the child nodes. The child
  //   nodes will then call their own child nodes and so forth. In short, we traverse the expression
  //   parse tree to run semantic analysis on them.
  // - Run semantic analysis on this node.
  // - The main job of semantics analysis is to run type resolution to find the correct values for
  //   yql_type and internal_type_ for expressions.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override = 0;

  // Check if an operator is allowed in the current context before analyzing it.
  virtual CHECKED_STATUS CheckOperator(SemContext *sem_context);

  // The following functions are called to setup the states before analyzing an operand.
  virtual CHECKED_STATUS SetupSemStateForOp1(SemState *sem_state);
  virtual CHECKED_STATUS SetupSemStateForOp2(SemState *sem_state);
  virtual CHECKED_STATUS SetupSemStateForOp3(SemState *sem_state);

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
  virtual CHECKED_STATUS CheckLhsExpr(SemContext *sem_context);

  // Analyze RHS expression.
  virtual CHECKED_STATUS CheckRhsExpr(SemContext *sem_context);

  // Check if left and right values are compatible.
  virtual CHECKED_STATUS CheckInequalityOperands(SemContext *sem_context,
                                                 PTExpr::SharedPtr lhs,
                                                 PTExpr::SharedPtr rhs);
  // Check if left and right values are compatible.
  virtual CHECKED_STATUS CheckEqualityOperands(SemContext *sem_context,
                                               PTExpr::SharedPtr lhs,
                                               PTExpr::SharedPtr rhs);

  // Compare this node datatype with the expected type from the parent treenode.
  virtual CHECKED_STATUS CheckExpectedTypeCompatibility(SemContext *sem_context);

 protected:
  ExprOperator op_;
  yb::YQLOperator yql_op_;
  InternalType internal_type_;
  std::shared_ptr<YQLType> yql_type_;
  InternalType expected_internal_type_;
};

using PTExprListNode = TreeListNode<PTExpr>;

//--------------------------------------------------------------------------------------------------
// Tree Nodes for Collections -- treated as expressions with flexible arity
//--------------------------------------------------------------------------------------------------

class PTCollectionExpr : public PTExpr {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTCollectionExpr> SharedPtr;
  typedef MCSharedPtr<const PTCollectionExpr> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTCollectionExpr(MemoryContext *memctx, YBLocation::SharedPtr loc, DataType literal_type)
      : PTExpr(memctx, loc, ExprOperator::kCollection, yb::YQLOperator::YQL_OP_NOOP,
      client::YBColumnSchema::ToInternalDataType(YQLType::Create(literal_type))),
        keys_(memctx), values_(memctx), udtype_field_values_(memctx) {
    yql_type_ = YQLType::Create(literal_type);
  }
  virtual ~PTCollectionExpr() { }

  void AddKeyValuePair(PTExpr::SharedPtr key, PTExpr::SharedPtr value) {
    keys_.emplace_back(key);
    values_.emplace_back(value);
  }

  void AddElement(PTExpr::SharedPtr value) {
    values_.emplace_back(value);
  }

  int size() const {
    return static_cast<int>(values_.size());
  }

  const MCList<PTExpr::SharedPtr>& keys() const {
    return keys_;
  }

  const MCList<PTExpr::SharedPtr>& values() const {
    return values_;
  }

  const MCVector<PTExpr::SharedPtr>& udtype_field_values() const {
    return udtype_field_values_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTCollectionExpr::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTCollectionExpr>(memctx, std::forward<TypeArgs>(args)...);
  }

 private:
  MCList<PTExpr::SharedPtr> keys_;
  MCList<PTExpr::SharedPtr> values_;

  // This field will be decorated during analysis if this collection represents a user-defined type
  // It contains the field values in the order specified by the type (or nullptr for missing values)
  MCVector<PTExpr::SharedPtr> udtype_field_values_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with no operand (0 input).
template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr0 : public expr_class {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTExpr0> SharedPtr;
  typedef MCSharedPtr<const PTExpr0> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTExpr0(MemoryContext *memctx,
          YBLocation::SharedPtr loc,
          ExprOperator op,
          yb::YQLOperator yql_op)
      : expr_class(memctx, loc, op, yql_op, itype, ytype) {
  }
  virtual ~PTExpr0() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr0::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr0>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    RETURN_NOT_OK(this->CheckOperator(sem_context));

    // Analyze this node operator and setup its yql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(sem_context));

    // Make sure that this expression has valid data type.
    return this->CheckExpectedTypeCompatibility(sem_context);
  }
};

//--------------------------------------------------------------------------------------------------
// Template for expression with one operand (1 input).
template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr1 : public expr_class {
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
          yb::YQLOperator yql_op,
          PTExpr::SharedPtr op1)
      : expr_class(memctx, loc, op, yql_op, itype, ytype),
        op1_(op1) {
  }
  virtual ~PTExpr1() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExpr1::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExpr1>(memctx, std::forward<TypeArgs>(args)...);
  }

  PTExpr::SharedPtr op1() const override {
    return op1_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    RETURN_NOT_OK(this->CheckOperator(sem_context));

    // Run semantic analysis on child nodes.
    SemState sem_state(sem_context);
    RETURN_NOT_OK(this->SetupSemStateForOp1(&sem_state));
    RETURN_NOT_OK(op1_->Analyze(sem_context));
    sem_state.ResetContextState();

    // Analyze this node operator and setup its yql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(sem_context, op1_));

    // Make sure that it has valid data type.
    return this->CheckExpectedTypeCompatibility(sem_context);
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Operand.
  PTExpr::SharedPtr op1_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (2 inputs).
template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr2 : public expr_class {
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
          yb::YQLOperator yql_op,
          const PTExpr::SharedPtr& op1,
          const PTExpr::SharedPtr& op2)
      : expr_class(memctx, loc, op, yql_op, itype, ytype),
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

  PTExpr::SharedPtr op1() const override {
    return op1_;
  }

  PTExpr::SharedPtr op2() const override {
    return op2_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    RETURN_NOT_OK(this->CheckOperator(sem_context));

    // Run semantic analysis on child nodes.
    SemState sem_state(sem_context);
    RETURN_NOT_OK(this->SetupSemStateForOp1(&sem_state));
    RETURN_NOT_OK(op1_->Analyze(sem_context));

    RETURN_NOT_OK(this->SetupSemStateForOp2(&sem_state));
    RETURN_NOT_OK(op2_->Analyze(sem_context));
    sem_state.ResetContextState();

    // Analyze this node operator and setup its yql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(sem_context, op1_, op2_));

    // Make sure that it has valid data type.
    return this->CheckExpectedTypeCompatibility(sem_context);
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Operand 1 and 2.
  PTExpr::SharedPtr op1_;
  PTExpr::SharedPtr op2_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (3 inputs).
template<InternalType itype, DataType ytype, class expr_class = PTExpr>
class PTExpr3 : public expr_class {
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
          yb::YQLOperator yql_op,
          const PTExpr::SharedPtr& op1,
          const PTExpr::SharedPtr& op2,
          const PTExpr::SharedPtr& op3)
      : expr_class(memctx, loc, op, yql_op, itype, ytype),
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

  PTExpr::SharedPtr op1() const override {
    return op1_;
  }

  PTExpr::SharedPtr op2() const override {
    return op2_;
  }

  PTExpr::SharedPtr op3() const override {
    return op3_;
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override {
    RETURN_NOT_OK(this->CheckOperator(sem_context));

    // Run semantic analysis on child nodes.
    SemState sem_state(sem_context);
    RETURN_NOT_OK(this->SetupSemStateForOp1(&sem_state));
    RETURN_NOT_OK(op1_->Analyze(sem_context));

    RETURN_NOT_OK(this->SetupSemStateForOp2(&sem_state));
    RETURN_NOT_OK(op2_->Analyze(sem_context));

    RETURN_NOT_OK(this->SetupSemStateForOp3(&sem_state));
    RETURN_NOT_OK(op3_->Analyze(sem_context));
    sem_state.ResetContextState();

    // Analyze this node operator and setup its yql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(sem_context, op1_, op2_, op3_));

    // Make sure that it has valid data type.
    return this->CheckExpectedTypeCompatibility(sem_context);
  }

 protected:
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
template<typename ReturnType>
class PTLiteral {
 public:
  explicit PTLiteral(ReturnType value) : value_(value) { }

  virtual ~PTLiteral() { }

  virtual ReturnType Eval() const { return value_; }

  virtual ReturnType value() const { return value_; }

 protected:
  ReturnType value_;
};

template<InternalType itype, DataType ytype,
         typename ReturnType, typename LiteralType = PTLiteral<ReturnType>>
class PTExprConst : public PTExpr0<itype, ytype>,
                    public LiteralType {
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
      : PTExpr0<itype, ytype>(memctx, loc, ExprOperator::kConst, yb::YQLOperator::YQL_OP_NOOP),
        LiteralType(value) {
  }
  virtual ~PTExprConst() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PTExprConst::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExprConst>(memctx, std::forward<TypeArgs>(args)...);
  }

  using PTExpr0<itype, ytype>::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context) override {
    // Nothing to do: constant expressions should be initialized with valid data type already
    return Status::OK();
  };
};

// NULL constant.
using PTConstArg = PTExprConst<InternalType::VALUE_NOT_SET,
                               DataType::UNKNOWN_DATA,
                               void*>;

using PTNull = PTExprConst<InternalType::VALUE_NOT_SET,
                           DataType::NULL_VALUE_TYPE,
                           void*>;

// String base classes for constant expression.
class PTLiteralString : public PTLiteral<MCSharedPtr<MCString>> {
 public:
  explicit PTLiteralString(MCSharedPtr<MCString> value);
  virtual ~PTLiteralString();

  CHECKED_STATUS ToInt64(int64_t *value, bool negate) const;
  CHECKED_STATUS ToDouble(long double *value, bool negate) const;
  CHECKED_STATUS ToDecimal(util::Decimal *value, bool negate) const;
  CHECKED_STATUS ToDecimal(std::string *value, bool negate) const;

  CHECKED_STATUS ToString(std::string *value) const;
  CHECKED_STATUS ToTimestamp(int64_t *value) const;

  CHECKED_STATUS ToInetaddress(InetAddress *value) const;
};
using PTConstText = PTExprConst<InternalType::kStringValue,
                                DataType::STRING,
                                MCSharedPtr<MCString>,
                                PTLiteralString>;
using PTConstVarInt = PTExprConst<InternalType::kStringValue,
                                  DataType::VARINT,
                                  MCSharedPtr<MCString>,
                                  PTLiteralString>;
using PTConstDecimal = PTExprConst<InternalType::kStringValue,
                                   DataType::DECIMAL,
                                   MCSharedPtr<MCString>,
                                   PTLiteralString>;
using PTConstUuid = PTExprConst<InternalType::kUuidValue,
                                DataType::UUID,
                                MCSharedPtr<MCString>,
                                PTLiteralString>;
using PTConstBinary = PTExprConst<InternalType::kBinaryValue,
                                  DataType::BINARY,
                                  MCSharedPtr<MCString>,
                                  PTLiteralString>;

// Boolean constant.
using PTConstBool = PTExprConst<InternalType::kBoolValue,
                                DataType::BOOL,
                                bool>;

// Obsolete numeric constant classes.
using PTConstInt = PTExprConst<InternalType::kInt64Value,
                               DataType::INT64,
                               int64_t>;

using PTConstDouble = PTExprConst<InternalType::kDoubleValue,
                                  DataType::DOUBLE,
                                  long double>;

//--------------------------------------------------------------------------------------------------
// Tree node for logical expressions (AND, OR, NOT, ...).
//--------------------------------------------------------------------------------------------------
class PTLogicExpr : public PTExpr {
 public:
  explicit PTLogicExpr(
      MemoryContext *memctx,
      YBLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::YQLOperator yql_op = yb::YQLOperator::YQL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType yql_type_id = DataType::BOOL)
      : PTExpr(memctx, loc, op, yql_op, internal_type, yql_type_id) {
  }

  // Setup states before analyzing operand.
  virtual CHECKED_STATUS SetupSemStateForOp1(SemState *sem_state) override;
  virtual CHECKED_STATUS SetupSemStateForOp2(SemState *sem_state) override;

  // Analyze this operator.
  using PTExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1) override;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1,
                                         PTExpr::SharedPtr op2) override;
};
using PTLogic1 = PTExpr1<InternalType::kBoolValue, DataType::BOOL, PTLogicExpr>;
using PTLogic2 = PTExpr2<InternalType::kBoolValue, DataType::BOOL, PTLogicExpr>;

//--------------------------------------------------------------------------------------------------
// Tree node for relational expressions (=, !=, >, ...).
//--------------------------------------------------------------------------------------------------
class PTRelationExpr : public PTExpr {
 public:
  explicit PTRelationExpr(
      MemoryContext *memctx,
      YBLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::YQLOperator yql_op = yb::YQLOperator::YQL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType yql_type_id = DataType::BOOL)
      : PTExpr(memctx, loc, op, yql_op, internal_type, yql_type_id) {
  }

  // Setup states before analyzing operands.
  virtual CHECKED_STATUS SetupSemStateForOp1(SemState *sem_state) override;
  virtual CHECKED_STATUS SetupSemStateForOp2(SemState *sem_state) override;
  virtual CHECKED_STATUS SetupSemStateForOp3(SemState *sem_state) override;

  // Analyze this operator after all operands were analyzed.
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context) override;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1) override;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1,
                                         PTExpr::SharedPtr op2) override;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context,
                                         PTExpr::SharedPtr op1,
                                         PTExpr::SharedPtr op2,
                                         PTExpr::SharedPtr op3) override;
};
using PTRelation0 = PTExpr0<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;
using PTRelation1 = PTExpr1<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;
using PTRelation2 = PTExpr2<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;
using PTRelation3 = PTExpr3<InternalType::kBoolValue, DataType::BOOL, PTRelationExpr>;

//--------------------------------------------------------------------------------------------------
// Tree node for operators.
// - Currently, we only have unary '-'.
// - Generally, we only need PTOperator for performance purposes. All operations can be supported
//   by adding it to builtin library, but that method is less efficient and PTOperator.
//--------------------------------------------------------------------------------------------------
class PTOperatorExpr : public PTExpr {
 public:
  explicit PTOperatorExpr(
      MemoryContext *memctx,
      YBLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::YQLOperator yql_op = yb::YQLOperator::YQL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType yql_type_id = DataType::UNKNOWN_DATA)
      : PTExpr(memctx, loc, op, yql_op, internal_type, yql_type_id) {
  }

  // Setup states before analyzing operands.
  virtual CHECKED_STATUS SetupSemStateForOp1(SemState *sem_state) override;

  // Analyze this operator after all operands were analyzed.
  using PTExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) override;
};

using PTOperator0 = PTExpr0<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;
using PTOperator1 = PTExpr1<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;
using PTOperator2 = PTExpr2<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;
using PTOperator3 = PTExpr3<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PTOperatorExpr>;

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

  using PTOperatorExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context) override;

  // Access function for name.
  const PTQualifiedName::SharedPtr& name() const {
    return name_;
  }

  // Construct bind variable name from the name of this column.
  const MCSharedPtr<MCString>& bindvar_name() const {
    return name_->bindvar_name();
  }

  // Access function for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTRef;
  }

  // Analyze LHS expression.
  virtual CHECKED_STATUS CheckLhsExpr(SemContext *sem_context) override;

 private:
  PTQualifiedName::SharedPtr name_;

  // Fields that should be resolved by semantic analysis.
  const ColumnDesc *desc_;
};

// SubColumn Reference. The datatype of this expression would need to be resolved by the analyzer.
class PTSubscriptedColumn : public PTOperator0 {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTSubscriptedColumn> SharedPtr;
  typedef MCSharedPtr<const PTSubscriptedColumn> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTSubscriptedColumn(MemoryContext *memctx,
        YBLocation::SharedPtr loc,
        const PTQualifiedName::SharedPtr& name,
        const PTExprListNode::SharedPtr& args);
  virtual ~PTSubscriptedColumn();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTSubscriptedColumn::SharedPtr MakeShared(MemoryContext *memctx,
                                                          TypeArgs&&... args) {
    return MCMakeShared<PTSubscriptedColumn>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  using PTOperatorExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context) override;

  // Access function for name.
  const PTQualifiedName::SharedPtr& name() const {
    return name_;
  }

  // Access function for name.
  const PTExprListNode::SharedPtr& args() const {
    return args_;
  }

  // Construct bind variable name from the name of this column.
  const MCSharedPtr<MCString>& bindvar_name() const {
    return name_->bindvar_name();
  }

  // Access function for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTSubscript;
  }

  // Analyze LHS expression.
  virtual CHECKED_STATUS CheckLhsExpr(SemContext *sem_context) override;

 private:
  PTQualifiedName::SharedPtr name_;
  PTExprListNode::SharedPtr args_;

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
              const MCSharedPtr<MCString>& alias);
  virtual ~PTExprAlias();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PTExprAlias::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTExprAlias>(memctx, std::forward<TypeArgs>(args)...);
  }

  using PTOperatorExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) override;

 private:
  MCSharedPtr<MCString> alias_;
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

  // Compare 2 bind variables for their hash column ids.
  struct HashColCmp {
    bool operator() (const PTBindVar* v1, const PTBindVar* v2) const {
      DCHECK(v1->hash_col() != nullptr) << "bindvar pos " << v1->pos() << " is not a hash column";
      DCHECK(v2->hash_col() != nullptr) << "bindvar pos " << v2->pos() << " is not a hash column";
      return v1->hash_col()->id() < v2->hash_col()->id();
    }
  };

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTBindVar(MemoryContext *memctx,
            YBLocation::SharedPtr loc,
            const MCSharedPtr<MCString>& name = nullptr);
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
  const MCSharedPtr<MCString>& name() const {
    return name_;
  }

  // Access function for hash column if available.
  const ColumnDesc *hash_col() const {
    return hash_col_;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTBindVar;
  }

  // Access to op_.
  virtual ExprOperator expr_op() const override {
    return ExprOperator::kBindVar;
  }

  // The name Cassandra uses for the virtual column when binding LIMIT clause
  static const string& limit_bindvar_name() {
    static string limit_bindvar_name = "[limit]";
    return limit_bindvar_name;
  }

  // The name Cassandra uses for the virtual column when binding USING TTL clause
  static const string& ttl_bindvar_name() {
    static string ttl_bindvar_name = "[ttl]";
    return ttl_bindvar_name;
  }

  // The name Cassandra uses for the virtual column when binding the partition key (i.e. with token)
  static const string& token_bindvar_name() {
    static string token_bindvar_name = "partition key token";
    return token_bindvar_name;
  }

 private:
  // 0-based position.
  int64_t pos_;
  // Variable name.
  MCSharedPtr<MCString> name_;
  // Hash column descriptor.
  const ColumnDesc *hash_col_ = nullptr;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_PT_EXPR_H_
