//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode implementation for DELETE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_delete.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTDeleteStmt::PTDeleteStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           TreeNode::SharedPtr selections,
                           PTTableRef::SharedPtr relation,
                           TreeNode::SharedPtr using_clause,
                           PTExpr::SharedPtr where_expr)
    : TreeNode(memctx, loc),
      relation_(relation),
      where_expr_(where_expr) {
}

PTDeleteStmt::~PTDeleteStmt() {
}

ErrorCode PTDeleteStmt::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  return err;
}

void PTDeleteStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
