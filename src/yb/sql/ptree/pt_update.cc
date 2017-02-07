//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode implementation for UPDATE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_update.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTAssign::PTAssign(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   const PTQualifiedName::SharedPtr& lhs,
                   const PTExpr::SharedPtr& rhs)
    : TreeNode(memctx, loc),
      lhs_(lhs),
      rhs_(rhs),
      col_desc_(nullptr) {
}

PTAssign::~PTAssign() {
}

CHECKED_STATUS PTAssign::Analyze(SemContext *sem_context) {
  // Analyze left value (column name).
  RETURN_NOT_OK(lhs_->Analyze(sem_context));

  col_desc_ = sem_context->GetColumnDesc(lhs_->last_name());
  if (col_desc_ == nullptr) {
    return sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  // Analyze right value (constant).
  if (rhs_->expr_op() != ExprOperator::kConst) {
    return sem_context->Error(rhs_->loc(), "Only literal values are allowed in this context",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  if (!sem_context->IsConvertible(col_desc_->sql_type(), rhs_->sql_type())) {
    return sem_context->Error(loc(), ErrorCode::DATATYPE_MISMATCH);
  }

  return Status::OK();
}

void PTAssign::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTUpdateStmt::PTUpdateStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTTableRef::SharedPtr relation,
                           PTAssignListNode::SharedPtr set_clause,
                           PTExpr::SharedPtr where_clause,
                           PTExpr::SharedPtr if_clause,
                           PTConstInt::SharedPtr ttl_seconds)
    : PTDmlStmt(memctx, loc, true, ttl_seconds),
      relation_(relation),
      set_clause_(set_clause),
      where_clause_(where_clause),
      if_clause_(if_clause) {
}

PTUpdateStmt::~PTUpdateStmt() {
}

CHECKED_STATUS PTUpdateStmt::Analyze(SemContext *sem_context) {
  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  // Process set clause.
  column_args_.resize(num_columns());
  TreeNodePtrOperator<SemContext, PTAssign> analyze = std::bind(&PTUpdateStmt::AnalyzeSetExpr,
                                                                this,
                                                                std::placeholders::_1,
                                                                std::placeholders::_2);
  RETURN_NOT_OK(set_clause_->Analyze(sem_context, analyze));

  // Set clause can't have primary keys.
  int num_keys = num_key_columns();
  for (int idx = 0; idx < num_keys; idx++) {
    if (column_args_[idx].IsInitialized()) {
      return sem_context->Error(set_clause_->loc(), ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context, if_clause_));

  // Run error checking on USING clause.
  RETURN_NOT_OK(AnalyzeUsingClause(sem_context));

  return Status::OK();
}

CHECKED_STATUS PTUpdateStmt::AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context) {
  // Analyze the expression.
  RETURN_NOT_OK(assign_expr->Analyze(sem_context));

  // Form the column args for protobuf.
  const ColumnDesc *col_desc = assign_expr->col_desc();
  column_args_[col_desc->index()].Init(col_desc, assign_expr->rhs());

  return Status::OK();
}

void PTUpdateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
