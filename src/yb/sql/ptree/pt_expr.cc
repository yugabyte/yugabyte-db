//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------
void PTExpr::SetVariableName(MemoryContext* memctx, const PTRef *ref, PTBindVar *var) {
  if (var->name() == nullptr) {
    var->set_name(memctx, ref->name()->last_name());
  }
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context) {
  switch (op_) {
    case ExprOperator::kNoOp:
      break;
    case ExprOperator::kExists:
      break;
    case ExprOperator::kNotExists:
      break;

    default:
      LOG(FATAL) << "Invalid operator";
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1) {
  switch (op_) {
    case ExprOperator::kUMinus:
      // "op1" must have been analyzed before we get here.
      // Check to make sure that it is allowed in this context.
      if (op1->op_ != ExprOperator::kConst) {
        return sem_context->Error(loc(), "Only numeric constant is allowed in this context",
                                  ErrorCode::FEATURE_NOT_SUPPORTED);
      }
      if (!YQLType::IsNumeric(op1->yql_type_id())) {
        return sem_context->Error(loc(), "Only numeric data type is allowed in this context",
                                  ErrorCode::INVALID_DATATYPE);
      }

      // Type resolution: (-x) should have the same datatype as (x).
      yql_type_id_ = op1->yql_type_id();
      internal_type_ = op1->internal_type();
      break;
    case ExprOperator::kNot:
      if (op1->yql_type_id_ != BOOL) {
        return sem_context->Error(loc(), "Only boolean data type is allowed in this context",
            ErrorCode::INVALID_DATATYPE);
      }
      yql_type_id_ = yb::DataType::BOOL;
      internal_type_ = yb::InternalType::kBoolValue;
      break;
    case ExprOperator::kIsNull: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsNotNull: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsTrue: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsFalse:
      return sem_context->Error(loc(), "Operator not supported yet",
          ErrorCode::CQL_STATEMENT_INVALID);
    default:
      LOG(FATAL) << "Invalid operator" << int(op_);
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2) {
  // "op1" and "op2" must have been analyzed before getting here
  switch (op_) {
    case ExprOperator::kEQ: FALLTHROUGH_INTENDED;
    case ExprOperator::kLT: FALLTHROUGH_INTENDED;
    case ExprOperator::kGT: FALLTHROUGH_INTENDED;
    case ExprOperator::kLE: FALLTHROUGH_INTENDED;
    case ExprOperator::kGE: FALLTHROUGH_INTENDED;
    case ExprOperator::kNE:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(AnalyzeLeftRightOperands(sem_context, op1, op2));
      if (!sem_context->IsComparable(op1->yql_type_id(), op2->yql_type_id())) {
        return sem_context->Error(loc(), "Cannot compare values of these datatypes",
            ErrorCode::INCOMPARABLE_DATATYPES);
      }
      yql_type_id_ = yb::DataType::BOOL;
      internal_type_ = yb::InternalType::kBoolValue;
      break;
    case ExprOperator::kAND: FALLTHROUGH_INTENDED;
    case ExprOperator::kOR:
      if (op1->yql_type_id_ != BOOL || op2->yql_type_id_ != BOOL) {
        return sem_context->Error(loc(), "Only boolean data type is allowed in this context",
            ErrorCode::INVALID_DATATYPE);
      }
      yql_type_id_ = yb::DataType::BOOL;
      internal_type_ = yb::InternalType::kBoolValue;
      break;
    case ExprOperator::kLike: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotLike: FALLTHROUGH_INTENDED;
    case ExprOperator::kIn: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotIn:
      return sem_context->Error(loc(), "Operator not supported yet",
          ErrorCode::CQL_STATEMENT_INVALID);
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2,
                                       PTExpr::SharedPtr op3) {
  // "op1", "op2", and "op3" must have been analyzed before getting here
  switch (op_) {
    case ExprOperator::kBetween: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotBetween:
      RETURN_NOT_OK(op1->CheckLhsExpr(sem_context));
      RETURN_NOT_OK(op2->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(op3->CheckRhsExpr(sem_context));
      RETURN_NOT_OK(AnalyzeLeftRightOperands(sem_context, op1, op2));
      RETURN_NOT_OK(AnalyzeLeftRightOperands(sem_context, op1, op3));
      yql_type_id_ = yb::DataType::BOOL;
      internal_type_ = yb::InternalType::kBoolValue;
      break;
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeLeftRightOperands(SemContext *sem_context,
                                                PTExpr::SharedPtr lhs,
                                                PTExpr::SharedPtr rhs) {
  if (lhs->op_ == ExprOperator::kRef && rhs->op_ == ExprOperator::kBindVar) {
    // For "<column> <op> <bindvar>", set up the bind var column description.
    const PTRef *ref = static_cast<const PTRef*>(lhs.get());
    PTBindVar *var = static_cast<PTBindVar*>(rhs.get());
    var->set_desc(ref->desc());
    return Status::OK();
  }
  if (!sem_context->IsComparable(lhs->yql_type_id(), rhs->yql_type_id())) {
    return sem_context->Error(loc(), "Cannot compare values of these datatypes",
                              ErrorCode::INCOMPARABLE_DATATYPES);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTExpr::CheckLhsExpr(SemContext *sem_context) {
  if (op_ != ExprOperator::kRef) {
    return sem_context->Error(loc(), "Only column reference is allowed for left hand value",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

CHECKED_STATUS PTExpr::CheckRhsExpr(SemContext *sem_context) {
  // Check for limitation in YQL (Not all expressions are acceptable).
  switch (op_) {
    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kCollection: FALLTHROUGH_INTENDED;
    case ExprOperator::kUMinus: FALLTHROUGH_INTENDED;
    case ExprOperator::kBindVar:
      break;
    default:
      return sem_context->Error(loc(), "Only literal value and bind marker are allowed for "
                                "right hand value", ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTRef::PTRef(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTQualifiedName::SharedPtr& name)
    : PTOperator0(memctx, loc, ExprOperator::kRef),
      name_(name),
      desc_(nullptr) {
}

PTRef::~PTRef() {
}

CHECKED_STATUS PTRef::AnalyzeOperator(SemContext *sem_context) {

  // Check if this refers to the whole table (SELECT *).
  if (name_ == nullptr) {
    return sem_context->Error(loc(), "Cannot do type resolution for wildcard reference (SELECT *)");
  }

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  desc_ = sem_context->GetColumnDesc(name_->last_name());
  if (desc_ == nullptr) {
    return sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  internal_type_ = desc_->internal_type();
  yql_type_id_ = desc_->yql_type().main();

  return Status::OK();
}

void PTRef::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTExprAlias::PTExprAlias(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         const PTExpr::SharedPtr& expr,
                         const MCString::SharedPtr& alias)
    : PTOperator1(memctx, loc, ExprOperator::kAlias, expr),
      alias_(alias) {
}

PTExprAlias::~PTExprAlias() {
}

CHECKED_STATUS PTExprAlias::AnalyzeOperator(SemContext *sem_context, PTExpr::SharedPtr op1) {
  // Type resolution: Alias of (x) should have the same datatype as (x).
  yql_type_id_ = op1->yql_type_id();
  internal_type_ = op1->internal_type();

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTBindVar::PTBindVar(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const MCString::SharedPtr& name)
    : PTExpr(memctx, loc, ExprOperator::kBindVar),
      pos_(kUnsetPosition),
      name_(name),
      desc_(nullptr) {
}

PTBindVar::PTBindVar(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     int64_t pos)
    : PTExpr(memctx, loc, ExprOperator::kBindVar),
      pos_(pos),
      name_(nullptr),
      desc_(nullptr) {
}

PTBindVar::~PTBindVar() {
}

CHECKED_STATUS PTBindVar::Analyze(SemContext *sem_context) {
  // Initialize the bind variable's yql_type to "NULL". The actual yql_type will be resolved to
  // the column it is associated with afterward (e.g. in "... WHERE <column> < <bindvar>",
  // "UPDATE table SET <column> = <bindvar> ..." or "INSERT INTO table (<column>, ...) VALUES
  // (<bindvar>, ...)").
  yql_type_id_ = DataType::NULL_VALUE_TYPE;
  return Status::OK();
}

void PTBindVar::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

void PTBindVar::Reset() {
  desc_ = nullptr;
}

}  // namespace sql
}  // namespace yb
