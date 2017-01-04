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

ErrorCode PTAssign::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Analyze left value (column name).
  err = lhs_->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }
  col_desc_ = sem_context->GetColumnDesc(lhs_->last_name());
  if (col_desc_ == nullptr) {
    sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
    return ErrorCode::UNDEFINED_COLUMN;
  }

  // Analyze right value (constant).
  if (rhs_->expr_op() != ExprOperator::kConst) {
    err = ErrorCode::CQL_STATEMENT_INVALID;
    sem_context->Error(loc(), "Only literal values are allowed in this context", err);
    sem_context->Error(rhs_->loc(), err);
  }
  if (!sem_context->IsCompatible(col_desc_->sql_type(), rhs_->sql_type())) {
    err = ErrorCode::DATATYPE_MISMATCH;
    sem_context->Error(loc(), err);
    return err;
  }

  return err;
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
                           PTOptionExist option_exists)
    : PTDmlStmt(memctx, loc, option_exists),
      relation_(relation),
      set_clause_(set_clause),
      where_clause_(where_clause),
      column_args_(memctx) {
}

PTUpdateStmt::~PTUpdateStmt() {
}

ErrorCode PTUpdateStmt::Analyze(SemContext *sem_context) {
  LOG(INFO) << kErrorFontStart << "UPDATE - start";
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Collect table's schema for semantic analysis.
  LookupTable(sem_context);

  // Process set clause.
  column_args_.resize(num_columns());
  TreeNodePtrOperator<SemContext, PTAssign> analyze = std::bind(&PTUpdateStmt::AnalyzeSetExpr,
                                                                this,
                                                                std::placeholders::_1,
                                                                std::placeholders::_2);
  set_clause_->Analyze(sem_context, analyze);
  int num_keys = num_key_columns();
  for (int idx = 0; idx < num_keys; idx++) {
    if (!column_args_[idx].IsInitialized()) {
      err = ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY;
      sem_context->Error(set_clause_->loc(), err);
      return err;
    }
  }

  // Run error checking on the WHERE conditions.
  err = AnalyzeWhereClause(sem_context, where_clause_);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  LOG(INFO) << "UPDATE - end" << kErrorFontEnd;
  return err;
}

ErrorCode PTUpdateStmt::AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Analyze the expression.
  err = assign_expr->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  // Form the column args for protobuf.
  const ColumnDesc *col_desc = assign_expr->col_desc();
  column_args_[col_desc->index()].Init(col_desc, assign_expr->rhs());

  return err;
}

void PTUpdateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
