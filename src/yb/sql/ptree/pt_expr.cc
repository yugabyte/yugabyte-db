//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for expressions.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_expr.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTRef::PTRef(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         const PTQualifiedName::SharedPtr& name)
    : PTExpr(memctx, loc),
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
    : PTExpr(memctx, loc),
      expr_(expr),
      alias_(alias) {
}

PTExprAlias::~PTExprAlias() {
}

}  // namespace sql
}  // namespace yb
