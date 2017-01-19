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
                           PTExpr::SharedPtr where_clause,
                           PTOptionExist option_exists)
    : PTDmlStmt(memctx, loc, true, option_exists),
      relation_(relation),
      where_clause_(where_clause) {
}

PTDeleteStmt::~PTDeleteStmt() {
}

CHECKED_STATUS PTDeleteStmt::Analyze(SemContext *sem_context) {
  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  return Status::OK();
}

void PTDeleteStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
