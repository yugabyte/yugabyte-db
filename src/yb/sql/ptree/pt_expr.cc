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

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context) {
  switch (op_) {
    case ExprOperator::kNoOp:
      break;
    case ExprOperator::kConst:
      break;
    case ExprOperator::kAlias:
      break;
    case ExprOperator::kRef:
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
      if (!YBColumnSchema::IsNumeric(op1->sql_type())) {
        return sem_context->Error(loc(), "Only numeric data type is allowed in this context",
                                  ErrorCode::INVALID_DATATYPE);
      }

      // Type resolution: (-x) should have the same datatype as (x).
      sql_type_ = op1->sql_type();
      type_id_ = op1->type_id();
      break;
    case ExprOperator::kNot:
      break;
    case ExprOperator::kIsNull:
      break;
    case ExprOperator::kIsNotNull:
      break;
    case ExprOperator::kIsTrue:
      break;
    case ExprOperator::kIsFalse:
      break;
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2) {
  switch (op_) {
    case ExprOperator::kEQ: // Starting op with 2 operands.
      break;
    case ExprOperator::kLT:
      break;
    case ExprOperator::kGT:
      break;
    case ExprOperator::kLE:
      break;
    case ExprOperator::kGE:
      break;
    case ExprOperator::kNE:
      break;
    case ExprOperator::kAND:
      break;
    case ExprOperator::kOR:
      break;
    case ExprOperator::kLike:
      break;
    case ExprOperator::kNotLike:
      break;
    case ExprOperator::kIn:
      break;
    case ExprOperator::kNotIn:
      break;
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

CHECKED_STATUS PTExpr::AnalyzeOperator(SemContext *sem_context,
                                       PTExpr::SharedPtr op1,
                                       PTExpr::SharedPtr op2,
                                       PTExpr::SharedPtr op3) {
  switch (op_) {
    case ExprOperator::kBetween: // Starting op with 3 operands.
      break;
    case ExprOperator::kNotBetween: break;
    default:
      LOG(FATAL) << "Invalid operator";
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

// TODO(mihnea) This function should be called from AnalyzeOperator() from certain kOp.
CHECKED_STATUS PTExpr::AnalyzeLhsExpr(SemContext *sem_context) {
  if (op_ != ExprOperator::kRef) {
    return sem_context->Error(loc(), "Only column reference is allowed for left hand value",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // TODO(mihnea) This call should be removed as it's already called by the exp tree traversal.
  // Analyze variable.
  return Analyze(sem_context);
}

// TODO(mihnea) This function should be called from AnalyzeOperator() from certain kOp.
CHECKED_STATUS PTExpr::AnalyzeRhsExpr(SemContext *sem_context) {
  // Check for limitation in YQL (Not all expressions are acceptable).
  switch (op_) {
    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kUMinus:
      break;
    default:
      return sem_context->Error(loc(), "Only literal value is allowed for right hand value",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }

  // TODO(mihnea) This call should be removed as it's already called by the exp tree traversal.
  // Analyze the argument.
  return Analyze(sem_context);
}

//--------------------------------------------------------------------------------------------------

PTRef::PTRef(MemoryContext *memctx,
             YBLocation::SharedPtr loc,
             const PTQualifiedName::SharedPtr& name)
    : PTExpr(memctx, loc, ExprOperator::kRef),
      name_(name),
      desc_(nullptr) {
}

PTRef::~PTRef() {
}

CHECKED_STATUS PTRef::Analyze(SemContext *sem_context) {
  // Check if this refers to the whole table (SELECT *).
  if (name_ == nullptr) {
    return Status::OK();
  }

  // Look for a column descriptor from symbol table.
  RETURN_NOT_OK(name_->Analyze(sem_context));
  desc_ = sem_context->GetColumnDesc(name_->last_name());
  if (desc_ == nullptr) {
    return sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Type resolution: Ref(x) should have the same datatype as (x).
  type_id_ = desc_->type_id();
  sql_type_ = desc_->sql_type();
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
    : PTExpr(memctx, loc, ExprOperator::kAlias),
      expr_(expr),
      alias_(alias) {
}

PTExprAlias::~PTExprAlias() {
}

CHECKED_STATUS PTExprAlias::Analyze(SemContext *sem_context) {
  // Analyze the expression.
  RETURN_NOT_OK(expr_->Analyze(sem_context));

  // Type resolution: Alias of (x) should have the same datatype as (x).
  sql_type_ = expr_->sql_type();
  type_id_ = expr_->type_id();

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
