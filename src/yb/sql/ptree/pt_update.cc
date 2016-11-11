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
      rhs_(rhs) {
}

PTAssign::~PTAssign() {
}

ErrorCode PTAssign::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
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
                           PTExpr::SharedPtr where_expr)
    : TreeNode(memctx, loc),
      relation_(relation),
      set_clause_(set_clause),
      where_expr_(where_expr) {
}

PTUpdateStmt::~PTUpdateStmt() {
}

ErrorCode PTUpdateStmt::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  return err;
}

void PTUpdateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
