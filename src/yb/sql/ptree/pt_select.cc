//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_select.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTValues::PTValues(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   PTExprListNode::SharedPtr tuple)
    : PTCollection(memctx, loc),
      tuples_(memctx, loc) {
  Append(tuple);
}

PTValues::~PTValues() {
}

void PTValues::Append(const PTExprListNode::SharedPtr& tuple) {
  tuples_.Append(tuple);
}

void PTValues::Prepend(const PTExprListNode::SharedPtr& tuple) {
  tuples_.Prepend(tuple);
}

ErrorCode PTValues::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  return err;
}

void PTValues::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

PTExprListNode::SharedPtr PTValues::Tuple(int index) const {
  DCHECK_GE(index, 0);
  return tuples_.element(index);
}

//--------------------------------------------------------------------------------------------------

PTSelectStmt::PTSelectStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTListNode::SharedPtr target,
                           PTTableRefListNode::SharedPtr from_clause,
                           PTExpr::SharedPtr where_clause,
                           PTListNode::SharedPtr group_by_clause,
                           PTListNode::SharedPtr having_clause,
                           PTListNode::SharedPtr order_by_clause,
                           PTExpr::SharedPtr limit_clause)
    : PTCollection(memctx, loc),
      target_(target),
      from_clause_(from_clause),
      where_clause_(where_clause),
      group_by_clause_(group_by_clause),
      having_clause_(having_clause),
      order_by_clause_(order_by_clause),
      limit_clause_(limit_clause) {
}

PTSelectStmt::~PTSelectStmt() {
}

ErrorCode PTSelectStmt::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  return err;
}

void PTSelectStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTOrderBy::PTOrderBy(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const PTExpr::SharedPtr& name,
                     const Direction direction,
                     const NullPlacement null_placement)
  : TreeNode(memctx, loc),
    name_(name),
    direction_(direction),
    null_placement_(null_placement) {
}

PTOrderBy::~PTOrderBy() {
}

//--------------------------------------------------------------------------------------------------

PTTableRef::PTTableRef(MemoryContext *memctx,
                       YBLocation::SharedPtr loc,
                       const PTQualifiedName::SharedPtr& name,
                       MCString::SharedPtr alias)
    : TreeNode(memctx, loc),
      name_(name),
      alias_(alias) {
}

PTTableRef::~PTTableRef() {
}

//--------------------------------------------------------------------------------------------------

}  // namespace sql
}  // namespace yb
