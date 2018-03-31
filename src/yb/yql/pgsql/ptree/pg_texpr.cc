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
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_texpr.h"
#include "yb/yql/pgsql/ptree/pg_tbcall.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"
#include "yb/util/decimal.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/stol_utils.h"

namespace yb {
namespace pgsql {

using client::YBColumnSchema;
using std::shared_ptr;

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgTExpr::CheckOperator(PgCompileContext *compile_context) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::AnalyzeOperator(PgCompileContext *compile_context) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::AnalyzeOperator(PgCompileContext *compile_context,
                                       PgTExpr::SharedPtr op1) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::AnalyzeOperator(PgCompileContext *compile_context,
                                       PgTExpr::SharedPtr op1,
                                       PgTExpr::SharedPtr op2) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::AnalyzeOperator(PgCompileContext *compile_context,
                                       PgTExpr::SharedPtr op1,
                                       PgTExpr::SharedPtr op2,
                                       PgTExpr::SharedPtr op3) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::SetupPgSemStateForOp1(PgSemState *sem_state) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::SetupPgSemStateForOp2(PgSemState *sem_state) {
  // Passing down where clause state variables.
  return Status::OK();
}

CHECKED_STATUS PgTExpr::SetupPgSemStateForOp3(PgSemState *sem_state) {
  return Status::OK();
}

CHECKED_STATUS PgTExpr::CheckExpectedTypeCompatibility(PgCompileContext *compile_context) {
  CHECK(has_valid_internal_type() && has_valid_ql_type_id());

  // Check if RHS is convertible to LHS.
  if (!compile_context->expr_expected_ql_type()->IsUnknown()) {
    if (!compile_context->IsConvertible(compile_context->expr_expected_ql_type(), ql_type_)) {
      return compile_context->Error(this, ErrorCode::DATATYPE_MISMATCH);
    }
  }

  // Resolve internal type.
  const InternalType expected_itype = compile_context->expr_expected_internal_type();
  if (expected_itype == InternalType::VALUE_NOT_SET) {
    expected_internal_type_ = internal_type_;
  } else {
    expected_internal_type_ = expected_itype;
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
CHECKED_STATUS PgTExpr::CheckInequalityOperands(PgCompileContext *compile_context,
                                               PgTExpr::SharedPtr lhs,
                                               PgTExpr::SharedPtr rhs) {
  if (!compile_context->IsComparable(lhs->ql_type_id(), rhs->ql_type_id())) {
    return compile_context->Error(this, "Cannot compare values of these datatypes",
                              ErrorCode::INCOMPARABLE_DATATYPES);
  }
  return Status::OK();
}

CHECKED_STATUS PgTExpr::CheckEqualityOperands(PgCompileContext *compile_context,
                                             PgTExpr::SharedPtr lhs,
                                             PgTExpr::SharedPtr rhs) {
  if (QLType::IsNull(lhs->ql_type_id()) || QLType::IsNull(rhs->ql_type_id())) {
    return Status::OK();
  } else {
    return CheckInequalityOperands(compile_context, lhs, rhs);
  }
}


CHECKED_STATUS PgTExpr::CheckLhsExpr(PgCompileContext *compile_context) {
  if (op_ != ExprOperator::kRef && op_ != ExprOperator::kBcall) {
    return compile_context->Error(this,
                              "Only column refs and builtin calls are allowed for left hand value",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

CHECKED_STATUS PgTExpr::CheckRhsExpr(PgCompileContext *compile_context) {
  // Check for limitation in QL (Not all expressions are acceptable).
  switch (op_) {
    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kUMinus: FALLTHROUGH_INTENDED;
    case ExprOperator::kRef: FALLTHROUGH_INTENDED;
    case ExprOperator::kBcall:
      break;
    default:
      return compile_context->Error(this, "Operator not allowed as right hand value",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgTLiteralString::PgTLiteralString(MCSharedPtr<MCString> value)
    : PgTLiteral<MCSharedPtr<MCString>>(value) {
}

PgTLiteralString::~PgTLiteralString() {
}

CHECKED_STATUS PgTLiteralString::ToInt64(int64_t *value, bool negate) const {
  auto temp = negate ? util::CheckedStoll(string("-") + value_->c_str())
                     : util::CheckedStoll(*value_);
  RETURN_NOT_OK(temp);
  *value = *temp;
  return Status::OK();
}

CHECKED_STATUS PgTLiteralString::ToDouble(long double *value, bool negate) const {
  auto temp = util::CheckedStold(*value_);
  RETURN_NOT_OK(temp);
  *value = negate ? -*temp : *temp;
  return Status::OK();
}

CHECKED_STATUS PgTLiteralString::ToDecimal(util::Decimal *value, bool negate) const {
  if (negate) {
    return value->FromString(string("-") + value_->c_str());
  } else {
    return value->FromString(value_->c_str());
  }
}

CHECKED_STATUS PgTLiteralString::ToDecimal(string *value, bool negate) const {
  util::Decimal d;
  if (negate) {
    RETURN_NOT_OK(d.FromString(string("-") + value_->c_str()));
  } else {
    RETURN_NOT_OK(d.FromString(value_->c_str()));
  }
  *value = d.EncodeToComparable();
  return Status::OK();
}

CHECKED_STATUS PgTLiteralString::ToVarInt(string *value, bool negate) const {
  util::VarInt v;
  if (negate) {
    RETURN_NOT_OK(v.FromString(string("-") + value_->c_str()));
  } else {
    RETURN_NOT_OK(v.FromString(value_->c_str()));
  }
  *value = v.EncodeToComparable();
  return Status::OK();
}

CHECKED_STATUS PgTLiteralString::ToString(string *value) const {
  *value = value_->c_str();
  return Status::OK();
}

CHECKED_STATUS PgTLiteralString::ToTimestamp(int64_t *value) const {
  auto ts = DateTime::TimestampFromString(value_->c_str());
  RETURN_NOT_OK(ts);
  *value = ts->ToInt64();
  return Status::OK();
}

CHECKED_STATUS PgTLiteralString::ToInetaddress(InetAddress *value) const {
  RETURN_NOT_OK(value->FromString(value_->c_str()));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PgTOperatorExpr::SetupPgSemStateForOp1(PgSemState *sem_state) {
  switch (op_) {
    case ExprOperator::kUMinus:
    case ExprOperator::kAlias:
      sem_state->CopyPreviousStates();
      break;

    default:
      LOG(FATAL) << "Invalid operator " << int(op_);
  }

  return Status::OK();
}

CHECKED_STATUS PgTOperatorExpr::AnalyzeOperator(PgCompileContext *compile_context,
                                                PgTExpr::SharedPtr op1) {
  switch (op_) {
    case ExprOperator::kUMinus:
      // "op1" must have been analyzed before we get here.
      // Check to make sure that it is allowed in this context.
      if (op1->expr_op() != ExprOperator::kConst) {
        return compile_context->Error(this, "Only numeric constant is allowed in this context",
                                      ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      if (!QLType::IsNumeric(op1->ql_type_id())) {
        return compile_context->Error(this, "Only numeric data type is allowed in this context",
                                      ErrorCode::INVALID_DATATYPE);
      }

      // Type resolution: (-x) should have the same datatype as (x).
      ql_type_ = op1->ql_type();
      internal_type_ = op1->internal_type();
      break;

    default:
      LOG(FATAL) << "Invalid operator" << int(op_);
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgTRef::PgTRef(MemoryContext *memctx,
             PgTLocation::SharedPtr loc,
             const PgTQualifiedName::SharedPtr& name)
    : PgTOperator0(memctx, loc, ExprOperator::kRef, yb::QLOperator::QL_OP_NOOP),
      name_(name),
      desc_(nullptr) {
}

PgTRef::~PgTRef() {
}

CHECKED_STATUS PgTRef::AnalyzeOperator(PgCompileContext *compile_context) {
  DCHECK(name_ != nullptr) << "Reference column is not specified";

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(compile_context));
  if (!name_->IsSimpleName()) {
    return compile_context->Error(this, "Qualified name not allowed for column reference",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }
  desc_ = compile_context->GetColumnDesc(name_->last_name());
  if (desc_ == nullptr) {
    return compile_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  internal_type_ = desc_->internal_type();
  ql_type_ = desc_->ql_type();

  return Status::OK();
}

CHECKED_STATUS PgTRef::CheckLhsExpr(PgCompileContext *compile_context) {
  return Status::OK();
}

void PgTRef::PrintSemanticAnalysisResult(PgCompileContext *compile_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PgTAllColumns::PgTAllColumns(MemoryContext *memctx,
                           PgTLocation::SharedPtr loc)
    : PgTOperator0(memctx, loc, ExprOperator::kRef, yb::QLOperator::QL_OP_NOOP),
      stmt_(nullptr) {
}

PgTAllColumns::~PgTAllColumns() {
}

CHECKED_STATUS PgTAllColumns::AnalyzeOperator(PgCompileContext *compile_context) {
  // Make sure '*' is used only in 'SELECT *' statement.
  PgTDmlStmt *stmt = compile_context->current_dml_stmt();
  if (stmt == nullptr ||
      stmt->opcode() != TreeNodeOpcode::kPgTSelectStmt ||
      static_cast<PgTSelectStmt*>(stmt)->selected_exprs().size() > 1) {
    return compile_context->Error(loc(), "Cannot use '*' expression in this context",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  stmt_ = static_cast<PgTSelectStmt*>(stmt);

  // Note to server that all column are referenced by this statement.
  compile_context->current_dml_stmt()->AddRefForAllColumns();

  // TODO(Mihnea) See if TUPLE datatype can be used here.
  // '*' should be of TUPLE type, but we use the following workaround for now.
  ql_type_ = QLType::Create(DataType::NULL_VALUE_TYPE);
  internal_type_ = InternalType::kListValue;
  return Status::OK();
}

const MCVector<ColumnDesc>& PgTAllColumns::table_columns() const {
  return stmt_->table_columns();
}

//--------------------------------------------------------------------------------------------------

PgTExprAlias::PgTExprAlias(MemoryContext *memctx,
                         PgTLocation::SharedPtr loc,
                         const PgTExpr::SharedPtr& expr,
                         const MCSharedPtr<MCString>& alias)
    : PgTOperator1(memctx, loc, ExprOperator::kAlias, yb::QLOperator::QL_OP_NOOP, expr),
      alias_(alias) {
}

PgTExprAlias::~PgTExprAlias() {
}

CHECKED_STATUS PgTExprAlias::SetupPgSemStateForOp1(PgSemState *sem_state) {
  sem_state->set_allowing_aggregate(sem_state->previous_state()->allowing_aggregate());
  return Status::OK();
}

CHECKED_STATUS PgTExprAlias::AnalyzeOperator(PgCompileContext *compile_context,
                                             PgTExpr::SharedPtr op1) {
  // Type resolution: Alias of (x) should have the same datatype as (x).
  ql_type_ = op1->ql_type();
  internal_type_ = op1->internal_type();
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
