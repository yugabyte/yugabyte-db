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
      name_(name) {
}

PTRef::~PTRef() {
}

ErrorCode PTRef::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  return err;
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
