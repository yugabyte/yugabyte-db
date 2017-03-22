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
  // Set the name of unnamed bind marker for "UPDATE tab SET <column> = ? ..."
  if (lhs_ != nullptr && rhs_ != nullptr && rhs_->expr_op() == ExprOperator::kBindVar) {
    PTBindVar *var = static_cast<PTBindVar*>(rhs_.get());
    if (var->name() == nullptr) {
      var->set_name(memctx, lhs_->last_name());
    }
  }
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

  // Analyze right value.
  RETURN_NOT_OK(rhs_->Analyze(sem_context));
  RETURN_NOT_OK(rhs_->CheckRhsExpr(sem_context));

  if (rhs_->expr_op() == ExprOperator::kBindVar) {
    // For "<column> = <bindvar>", set up the bind var column description.
    PTBindVar *var = static_cast<PTBindVar*>(rhs_.get());
    var->set_desc(col_desc_);
  } else if (!sem_context->IsConvertible(col_desc_->sql_type(), rhs_->sql_type())) {
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

  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  // Process set clause.
  column_args_->resize(num_columns());
  TreeNodePtrOperator<SemContext, PTAssign> analyze = std::bind(&PTUpdateStmt::AnalyzeSetExpr,
                                                                this,
                                                                std::placeholders::_1,
                                                                std::placeholders::_2);
  RETURN_NOT_OK(set_clause_->Analyze(sem_context, analyze));

  // Set clause can't have primary keys.
  int num_keys = num_key_columns();
  for (int idx = 0; idx < num_keys; idx++) {
    if (column_args_->at(idx).IsInitialized()) {
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
  column_args_->at(col_desc->index()).Init(col_desc, assign_expr->rhs());

  return Status::OK();
}

void PTUpdateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
