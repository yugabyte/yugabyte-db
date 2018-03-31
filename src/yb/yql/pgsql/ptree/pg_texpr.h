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
// Tree node definitions for expression.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_TEXPR_H_
#define YB_YQL_PGSQL_PTREE_PG_TEXPR_H_

#include "yb/yql/pgsql/ptree/column_desc.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_ttype.h"
#include "yb/yql/pgsql/ptree/pg_tname.h"
#include "yb/yql/pgsql/ptree/pg_sem_state.h"

#include "yb/common/ql_value.h"
#include "yb/util/bfpg/bfpg.h"

namespace yb {
namespace pgsql {

// Because statements own expressions and their headers include expression headers, we forward
// declare statement classes here.
class PgTSelectStmt;

//--------------------------------------------------------------------------------------------------
// The order of the following enum values are not important.
enum class ExprOperator : int {
  kNoOp = 0,

  // Reference to constants, columns, and variables.
  kConst = 1,
  kAlias = 2,
  kRef = 3,
  kBindVar = 4,
  kBcall = 5,

  // Operators that take one operand.
  kUMinus = 6,
};

//--------------------------------------------------------------------------------------------------
// Base class for all expressions.
class PgTExpr : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExpr> SharedPtr;
  typedef MCSharedPtr<const PgTExpr> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PgTExpr(
      MemoryContext *memctx,
      PgTLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::QLOperator ql_op = yb::QLOperator::QL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType ql_type_id = DataType::UNKNOWN_DATA)
      : TreeNode(memctx, loc),
        op_(op),
        ql_op_(ql_op),
        internal_type_(internal_type),
        ql_type_(QLType::Create(ql_type_id)),
        expected_internal_type_(InternalType::VALUE_NOT_SET) {
  }
  virtual ~PgTExpr() {
  }

  // Expression return type in DocDB format.
  virtual InternalType internal_type() const {
    return internal_type_;
  }

  bool has_valid_internal_type() {
    // internal_type_ is not set in case of PgTNull.
    return ql_type_->main() == DataType::NULL_VALUE_TYPE ||
           internal_type_ != InternalType::VALUE_NOT_SET;
  }

  virtual InternalType expected_internal_type() const {
    return expected_internal_type_;
  }
  void set_expected_internal_type(InternalType val) {
    expected_internal_type_ = val;
  }

  // Expression return type in QL format.
  virtual const std::shared_ptr<QLType>& ql_type() const {
    return ql_type_;
  }

  virtual void set_ql_type(const std::shared_ptr<QLType>& ql_type) {
    ql_type_ = ql_type;
  }

  virtual void set_ql_type(DataType type_id) {
    ql_type_ = QLType::Create(type_id);
  }

  // TODO(neil or mihnea) Remove or replace all ql_type_id API & comments with QLType.
  virtual DataType ql_type_id() const {
    return ql_type_->main();
  }

  virtual void set_ql_type_id(DataType type_id) {
    ql_type_ = QLType::Create(type_id);
  }

  bool has_valid_ql_type_id() {
    return ql_type_->main() != DataType::UNKNOWN_DATA;
  }

  // Return selected name of expression. In SELECT statement, each selected expression is assigned a
  // name, and this method is to form the name of an expression. For example, when selected expr is
  // a column of a table, QLName() would be the name of that column.
  virtual string QLName() const {
    LOG(INFO) << "Missing QLName for expression("
              << static_cast<int>(expr_op())
              << ") that is being selected";
    return "expr";
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTExpr;
  }

  // Returns the expression operator.
  virtual ExprOperator expr_op() const {
    return op_;
  }

  yb::QLOperator ql_op() const {
    return ql_op_;
  }

  // Predicate for null.
  virtual bool is_null() const {
    return ql_type_->main() == DataType::NULL_VALUE_TYPE;
  }

  // Returns the operands of an expression.
  virtual PgTExpr::SharedPtr op1() const {
    return nullptr;
  }

  virtual PgTExpr::SharedPtr op2() const {
    return nullptr;
  }

  virtual PgTExpr::SharedPtr op3() const {
    return nullptr;
  }

  // Predicate for constant tree node.
  bool is_constant() const {
    return ((expr_op() == ExprOperator::kConst) ||
            (expr_op() == ExprOperator::kUMinus && op1()->expr_op() == ExprOperator::kConst));
  }
  virtual bool IsDummyStar() const {
    return false;
  }

  // Predicate for calls to aggregate functions.
  virtual bool IsAggregateCall() const {
    return false;
  }
  virtual yb::bfpg::TSOpcode aggregate_opcode() const {
    return yb::bfpg::TSOpcode::kNoOp;
  }

  // All expressions must define this Analyze() function, which does the following steps.
  // - Call Analyze() on child treenodes to run semantic analysis on the child nodes. The child
  //   nodes will then call their own child nodes and so forth. In short, we traverse the expression
  //   parse tree to run semantic analysis on them.
  // - Run semantic analysis on this node.
  // - The main job of semantics analysis is to run type resolution to find the correct values for
  //   ql_type and internal_type_ for expressions.
  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override = 0;

  // Check if an operator is allowed in the current context before analyzing it.
  virtual CHECKED_STATUS CheckOperator(PgCompileContext *compile_context);

  // The following functions are called to setup the states before analyzing an operand.
  virtual CHECKED_STATUS SetupPgSemStateForOp1(PgSemState *sem_state);
  virtual CHECKED_STATUS SetupPgSemStateForOp2(PgSemState *sem_state);
  virtual CHECKED_STATUS SetupPgSemStateForOp3(PgSemState *sem_state);

  // These functions are called by analyze to run type resolution on this expression.
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context);
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1);
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1,
                                         PgTExpr::SharedPtr op2);
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1,
                                         PgTExpr::SharedPtr op2,
                                         PgTExpr::SharedPtr op3);

  // Analyze LHS expression.
  virtual CHECKED_STATUS CheckLhsExpr(PgCompileContext *compile_context);

  // Analyze RHS expression.
  virtual CHECKED_STATUS CheckRhsExpr(PgCompileContext *compile_context);

  // Check if left and right values are compatible.
  virtual CHECKED_STATUS CheckInequalityOperands(PgCompileContext *compile_context,
                                                 PgTExpr::SharedPtr lhs,
                                                 PgTExpr::SharedPtr rhs);
  // Check if left and right values are compatible.
  virtual CHECKED_STATUS CheckEqualityOperands(PgCompileContext *compile_context,
                                               PgTExpr::SharedPtr lhs,
                                               PgTExpr::SharedPtr rhs);

  // Compare this node datatype with the expected type from the parent treenode.
  virtual CHECKED_STATUS CheckExpectedTypeCompatibility(PgCompileContext *compile_context);

 protected:
  ExprOperator op_;
  yb::QLOperator ql_op_;
  InternalType internal_type_;
  std::shared_ptr<QLType> ql_type_;
  InternalType expected_internal_type_;
};

using PgTExprListNode = TreeListNode<PgTExpr>;

//--------------------------------------------------------------------------------------------------
// Template for expression with no operand (0 input).
template<InternalType itype, DataType ytype, class expr_class = PgTExpr>
class PgTExpr0 : public expr_class {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExpr0> SharedPtr;
  typedef MCSharedPtr<const PgTExpr0> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTExpr0(MemoryContext *memctx,
          PgTLocation::SharedPtr loc,
          ExprOperator op,
          yb::QLOperator ql_op)
      : expr_class(memctx, loc, op, ql_op, itype, ytype) {
  }
  virtual ~PgTExpr0() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PgTExpr0::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTExpr0>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override {
    RETURN_NOT_OK(this->CheckOperator(compile_context));

    // Analyze this node operator and setup its ql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(compile_context));

    // Make sure that this expression has valid data type.
    return this->CheckExpectedTypeCompatibility(compile_context);
  }
};

//--------------------------------------------------------------------------------------------------
// Template for expression with one operand (1 input).
template<InternalType itype, DataType ytype, class expr_class = PgTExpr>
class PgTExpr1 : public expr_class {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExpr1> SharedPtr;
  typedef MCSharedPtr<const PgTExpr1> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTExpr1(MemoryContext *memctx,
          PgTLocation::SharedPtr loc,
          ExprOperator op,
          yb::QLOperator ql_op,
          PgTExpr::SharedPtr op1)
      : expr_class(memctx, loc, op, ql_op, itype, ytype),
        op1_(op1) {
  }
  virtual ~PgTExpr1() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PgTExpr1::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTExpr1>(memctx, std::forward<TypeArgs>(args)...);
  }

  PgTExpr::SharedPtr op1() const override {
    return op1_;
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override {
    RETURN_NOT_OK(this->CheckOperator(compile_context));

    // Run semantic analysis on child nodes.
    PgSemState sem_state(compile_context);
    RETURN_NOT_OK(this->SetupPgSemStateForOp1(&sem_state));
    RETURN_NOT_OK(op1_->Analyze(compile_context));
    sem_state.ResetContextState();

    // Analyze this node operator and setup its ql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(compile_context, op1_));

    // Make sure that it has valid data type.
    return this->CheckExpectedTypeCompatibility(compile_context);
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Operand.
  PgTExpr::SharedPtr op1_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (2 inputs).
template<InternalType itype, DataType ytype, class expr_class = PgTExpr>
class PgTExpr2 : public expr_class {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExpr2> SharedPtr;
  typedef MCSharedPtr<const PgTExpr2> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTExpr2(MemoryContext *memctx,
          PgTLocation::SharedPtr loc,
          ExprOperator op,
          yb::QLOperator ql_op,
          const PgTExpr::SharedPtr& op1,
          const PgTExpr::SharedPtr& op2)
      : expr_class(memctx, loc, op, ql_op, itype, ytype),
        op1_(op1),
        op2_(op2) {
  }
  virtual ~PgTExpr2() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PgTExpr2::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTExpr2>(memctx, std::forward<TypeArgs>(args)...);
  }

  PgTExpr::SharedPtr op1() const override {
    return op1_;
  }

  PgTExpr::SharedPtr op2() const override {
    return op2_;
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override {
    RETURN_NOT_OK(this->CheckOperator(compile_context));

    // Run semantic analysis on child nodes.
    PgSemState sem_state(compile_context);
    RETURN_NOT_OK(this->SetupPgSemStateForOp1(&sem_state));
    RETURN_NOT_OK(op1_->Analyze(compile_context));

    RETURN_NOT_OK(this->SetupPgSemStateForOp2(&sem_state));
    RETURN_NOT_OK(op2_->Analyze(compile_context));
    sem_state.ResetContextState();

    // Analyze this node operator and setup its ql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(compile_context, op1_, op2_));

    // Make sure that it has valid data type.
    return this->CheckExpectedTypeCompatibility(compile_context);
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Operand 1 and 2.
  PgTExpr::SharedPtr op1_;
  PgTExpr::SharedPtr op2_;
};

//--------------------------------------------------------------------------------------------------
// Template for expression with two operands (3 inputs).
template<InternalType itype, DataType ytype, class expr_class = PgTExpr>
class PgTExpr3 : public expr_class {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExpr3> SharedPtr;
  typedef MCSharedPtr<const PgTExpr3> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTExpr3(MemoryContext *memctx,
          PgTLocation::SharedPtr loc,
          ExprOperator op,
          yb::QLOperator ql_op,
          const PgTExpr::SharedPtr& op1,
          const PgTExpr::SharedPtr& op2,
          const PgTExpr::SharedPtr& op3)
      : expr_class(memctx, loc, op, ql_op, itype, ytype),
        op1_(op1),
        op2_(op2),
        op3_(op3) {
  }
  virtual ~PgTExpr3() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PgTExpr3::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTExpr3>(memctx, std::forward<TypeArgs>(args)...);
  }

  PgTExpr::SharedPtr op1() const override {
    return op1_;
  }

  PgTExpr::SharedPtr op2() const override {
    return op2_;
  }

  PgTExpr::SharedPtr op3() const override {
    return op3_;
  }

  virtual CHECKED_STATUS Analyze(PgCompileContext *compile_context) override {
    RETURN_NOT_OK(this->CheckOperator(compile_context));

    // Run semantic analysis on child nodes.
    PgSemState sem_state(compile_context);
    RETURN_NOT_OK(this->SetupPgSemStateForOp1(&sem_state));
    RETURN_NOT_OK(op1_->Analyze(compile_context));

    RETURN_NOT_OK(this->SetupPgSemStateForOp2(&sem_state));
    RETURN_NOT_OK(op2_->Analyze(compile_context));

    RETURN_NOT_OK(this->SetupPgSemStateForOp3(&sem_state));
    RETURN_NOT_OK(op3_->Analyze(compile_context));
    sem_state.ResetContextState();

    // Analyze this node operator and setup its ql_type and internal_type_.
    RETURN_NOT_OK(this->AnalyzeOperator(compile_context, op1_, op2_, op3_));

    // Make sure that it has valid data type.
    return this->CheckExpectedTypeCompatibility(compile_context);
  }

 protected:
  //------------------------------------------------------------------------------------------------
  // Operand 1 and 2.
  PgTExpr::SharedPtr op1_;
  PgTExpr::SharedPtr op2_;
  PgTExpr::SharedPtr op3_;
};

//--------------------------------------------------------------------------------------------------
// Tree node for constants
//--------------------------------------------------------------------------------------------------
// Template for constant expressions.
template<typename ReturnType>
class PgTLiteral {
 public:
  explicit PgTLiteral(ReturnType value) : value_(value) { }

  virtual ~PgTLiteral() { }

  virtual ReturnType Eval() const { return value_; }

  virtual ReturnType value() const { return value_; }

  virtual string ToQLName(int64_t value) const {
    return std::to_string(value);
  }

  virtual string ToQLName(long double value) const {
    return std::to_string(value);
  }

  virtual string ToQLName(const string& value) const {
    return value;
  }

  virtual string ToQLName(bool value) const {
    return std::to_string(value);
  }

  virtual string ToQLName(void *ptr) const {
    return "NULL";
  }

  virtual string ToQLName(const MCSharedPtr<MCString>& value) const {
    return value->c_str();
  }

 protected:
  ReturnType value_;
};

template<InternalType itype, DataType ytype,
         typename ReturnType, typename LiteralType = PgTLiteral<ReturnType>>
class PgTExprConst : public PgTExpr0<itype, ytype>,
                     public LiteralType {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExprConst> SharedPtr;
  typedef MCSharedPtr<const PgTExprConst> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTExprConst(MemoryContext *memctx,
              PgTLocation::SharedPtr loc,
              ReturnType value)
      : PgTExpr0<itype, ytype>(memctx, loc, ExprOperator::kConst, yb::QLOperator::QL_OP_NOOP),
        LiteralType(value) {
  }
  virtual ~PgTExprConst() {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PgTExprConst::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTExprConst>(memctx, std::forward<TypeArgs>(args)...);
  }

  using PgTExpr0<itype, ytype>::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context) override {
    // Nothing to do: constant expressions should be initialized with valid data type already
    return Status::OK();
  };

  virtual string QLName() const override {
    return LiteralType::ToQLName(LiteralType::value());
  }
};

// NULL constant.
using PgTConstArg = PgTExprConst<InternalType::VALUE_NOT_SET,
                                 DataType::UNKNOWN_DATA,
                                 void*>;

using PgTNull = PgTExprConst<InternalType::VALUE_NOT_SET,
                             DataType::NULL_VALUE_TYPE,
                             void*>;

// This class is used only for the dummy (meaningless) '*' such as in COUNT(*).
class PgTStar : public PgTNull {
 public:
  // Public types.
  typedef MCSharedPtr<PgTStar> SharedPtr;
  typedef MCSharedPtr<const PgTStar> SharedPtrConst;

  // Constructor and destructor.
  PgTStar(MemoryContext *memctx, PgTLocation::SharedPtr loc)
      : PgTNull(memctx, loc, nullptr) {
  }

  // Shared pointer support.
  template<typename... TypeArgs>
  inline static PgTStar::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTStar>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual string QLName() const override {
    return "*";
  }
  virtual bool IsDummyStar() const override {
    return true;
  }
};

// String base classes for constant expression.
class PgTLiteralString : public PgTLiteral<MCSharedPtr<MCString>> {
 public:
  explicit PgTLiteralString(MCSharedPtr<MCString> value);
  virtual ~PgTLiteralString();

  CHECKED_STATUS ToInt64(int64_t *value, bool negate) const;
  CHECKED_STATUS ToDouble(long double *value, bool negate) const;
  CHECKED_STATUS ToDecimal(util::Decimal *value, bool negate) const;
  CHECKED_STATUS ToDecimal(std::string *value, bool negate) const;
  CHECKED_STATUS ToVarInt(std::string *value, bool negate) const;

  CHECKED_STATUS ToString(std::string *value) const;
  CHECKED_STATUS ToTimestamp(int64_t *value) const;

  CHECKED_STATUS ToInetaddress(InetAddress *value) const;
};
using PgTConstText = PgTExprConst<InternalType::kStringValue,
                                  DataType::STRING,
                                  MCSharedPtr<MCString>,
                                  PgTLiteralString>;
using PgTConstVarInt = PgTExprConst<InternalType::kVarintValue,
                                    DataType::VARINT,
                                    MCSharedPtr<MCString>,
                                    PgTLiteralString>;
using PgTConstDecimal = PgTExprConst<InternalType::kDecimalValue,
                                     DataType::DECIMAL,
                                     MCSharedPtr<MCString>,
                                     PgTLiteralString>;
using PgTConstUuid = PgTExprConst<InternalType::kUuidValue,
                                  DataType::UUID,
                                  MCSharedPtr<MCString>,
                                  PgTLiteralString>;
using PgTConstBinary = PgTExprConst<InternalType::kBinaryValue,
                                    DataType::BINARY,
                                    MCSharedPtr<MCString>,
                                    PgTLiteralString>;

// Boolean constant.
using PgTConstBool = PgTExprConst<InternalType::kBoolValue,
                                  DataType::BOOL,
                                  bool>;

// Obsolete numeric constant classes.
using PgTConstInt = PgTExprConst<InternalType::kInt64Value,
                                 DataType::INT64,
                                 int64_t>;

using PgTConstDouble = PgTExprConst<InternalType::kDoubleValue,
                                    DataType::DOUBLE,
                                    long double>;

//--------------------------------------------------------------------------------------------------
// Tree node for relational expressions (=, !=, >, ...).
//--------------------------------------------------------------------------------------------------
class PgTRelationExpr : public PgTExpr {
 public:
  explicit PgTRelationExpr(
      MemoryContext *memctx,
      PgTLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::QLOperator ql_op = yb::QLOperator::QL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType ql_type_id = DataType::BOOL)
      : PgTExpr(memctx, loc, op, ql_op, internal_type, ql_type_id) {
  }

  // Setup states before analyzing operands.
  virtual CHECKED_STATUS SetupPgSemStateForOp1(PgSemState *sem_state) override;
  virtual CHECKED_STATUS SetupPgSemStateForOp2(PgSemState *sem_state) override;
  virtual CHECKED_STATUS SetupPgSemStateForOp3(PgSemState *sem_state) override;

  // Analyze this operator after all operands were analyzed.
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context) override;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1) override;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1,
                                         PgTExpr::SharedPtr op2) override;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1,
                                         PgTExpr::SharedPtr op2,
                                         PgTExpr::SharedPtr op3) override;
};
using PTRelation0 = PgTExpr0<InternalType::kBoolValue, DataType::BOOL, PgTRelationExpr>;
using PTRelation1 = PgTExpr1<InternalType::kBoolValue, DataType::BOOL, PgTRelationExpr>;
using PTRelation2 = PgTExpr2<InternalType::kBoolValue, DataType::BOOL, PgTRelationExpr>;
using PTRelation3 = PgTExpr3<InternalType::kBoolValue, DataType::BOOL, PgTRelationExpr>;

//--------------------------------------------------------------------------------------------------
// Tree node for operators.
// - Currently, we only have unary '-'.
// - Generally, we only need PgTOperator for performance purposes. All operations can be supported
//   by adding it to builtin library, but that method is less efficient and PgTOperator.
//--------------------------------------------------------------------------------------------------
class PgTOperatorExpr : public PgTExpr {
 public:
  explicit PgTOperatorExpr(
      MemoryContext *memctx,
      PgTLocation::SharedPtr loc,
      ExprOperator op = ExprOperator::kNoOp,
      yb::QLOperator ql_op = yb::QLOperator::QL_OP_NOOP,
      InternalType internal_type = InternalType::VALUE_NOT_SET,
      DataType ql_type_id = DataType::UNKNOWN_DATA)
      : PgTExpr(memctx, loc, op, ql_op, internal_type, ql_type_id) {
  }

  // Setup states before analyzing operands.
  virtual CHECKED_STATUS SetupPgSemStateForOp1(PgSemState *sem_state) override;

  // Analyze this operator after all operands were analyzed.
  using PgTExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1) override;
};

using PgTOperator0 = PgTExpr0<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PgTOperatorExpr>;
using PgTOperator1 = PgTExpr1<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PgTOperatorExpr>;
using PgTOperator2 = PgTExpr2<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PgTOperatorExpr>;
using PgTOperator3 = PgTExpr3<InternalType::VALUE_NOT_SET, DataType::UNKNOWN_DATA, PgTOperatorExpr>;

//--------------------------------------------------------------------------------------------------
// Column Reference. The datatype of this expression would need to be resolved by the analyzer.
class PgTRef : public PgTOperator0 {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTRef> SharedPtr;
  typedef MCSharedPtr<const PgTRef> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTRef(MemoryContext *memctx,
        PgTLocation::SharedPtr loc,
        const PgTQualifiedName::SharedPtr& name);
  virtual ~PgTRef();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PgTRef::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTRef>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  void PrintSemanticAnalysisResult(PgCompileContext *compile_context);

  using PgTOperatorExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context) override;

  // Selected name.
  virtual string QLName() const override {
    return name_->QLName();
  }

  // Access function for name.
  const PgTQualifiedName::SharedPtr& name() const {
    return name_;
  }

  // Access function for descriptor.
  const ColumnDesc *desc() const {
    return desc_;
  }

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTRef;
  }

  // Analyze LHS expression.
  virtual CHECKED_STATUS CheckLhsExpr(PgCompileContext *compile_context) override;

 private:
  PgTQualifiedName::SharedPtr name_;

  // Fields that should be resolved by semantic analysis.
  const ColumnDesc *desc_;
};

//--------------------------------------------------------------------------------------------------
// Reference to all columns of all selected tables.
class PgTAllColumns : public PgTOperator0 {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTAllColumns> SharedPtr;
  typedef MCSharedPtr<const PgTAllColumns> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTAllColumns(MemoryContext *memctx, PgTLocation::SharedPtr loc);
  virtual ~PgTAllColumns();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PgTAllColumns::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTAllColumns>(memctx, std::forward<TypeArgs>(args)...);
  }

  using PgTOperatorExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPgTAllColumns;
  }

  virtual string QLName() const override {
    // We should not get here as '*' should have been converted into a list of column name before
    // the selected tuple is constructed and described.
    LOG(FATAL) << "Calling QLName for '*' is not expected";
    return "*";
  }

  const MCVector<ColumnDesc>& table_columns() const;

 private:
  // Fields that should be resolved by semantic analysis.
  PgTSelectStmt *stmt_;
};

//--------------------------------------------------------------------------------------------------
// Expression alias - Name of an expression including reference to column.
class PgTExprAlias : public PgTOperator1 {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PgTExprAlias> SharedPtr;
  typedef MCSharedPtr<const PgTExprAlias> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PgTExprAlias(MemoryContext *memctx,
              PgTLocation::SharedPtr loc,
              const PgTExpr::SharedPtr& expr,
              const MCSharedPtr<MCString>& alias);
  virtual ~PgTExprAlias();

  // Support for shared_ptr.
  template<typename... TypeArgs>
  inline static PgTExprAlias::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PgTExprAlias>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS SetupPgSemStateForOp1(PgSemState *sem_state) override;

  using PgTOperatorExpr::AnalyzeOperator;
  virtual CHECKED_STATUS AnalyzeOperator(PgCompileContext *compile_context,
                                         PgTExpr::SharedPtr op1) override;

  virtual string QLName() const override {
    return alias_->c_str();
  }

  // Predicate for calls to aggregate functions.
  virtual bool IsAggregateCall() const override {
    return (op1_ != nullptr && op1_->IsAggregateCall());
  }
  virtual yb::bfpg::TSOpcode aggregate_opcode() const override {
    DCHECK(op1_ != nullptr) << "Reading aggregate opcode from a NULL operator";
    return op1_->aggregate_opcode();
  }

 private:
  MCSharedPtr<MCString> alias_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_TEXPR_H_
