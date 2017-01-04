//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_select.h"

#include <functional>

#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

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
    : PTDmlStmt(memctx, loc),
      target_(target),
      from_clause_(from_clause),
      where_clause_(where_clause),
      group_by_clause_(group_by_clause),
      having_clause_(having_clause),
      order_by_clause_(order_by_clause),
      limit_clause_(limit_clause),
      selected_columns_(memctx) {
}

PTSelectStmt::~PTSelectStmt() {
}

ErrorCode PTSelectStmt::Analyze(SemContext *sem_context) {
  LOG(INFO) << kErrorFontStart;
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Get the table descriptor.
  if (from_clause_->size() > 1) {
    err = ErrorCode::CQL_STATEMENT_INVALID;
    sem_context->Error(from_clause_->loc(), "Only one selected table is allowed", err);
  }
  from_clause_->Analyze(sem_context);
  if (is_system()) {
    return err;
  }

  // Collect table's schema for semantic analysis.
  LookupTable(sem_context);

  // Run error checking on the select list 'target_'.
  // Check that all targets are valid references to table columns.
  selected_columns_.reserve(num_columns());
  TreeNodePtrOperator<SemContext> analyze = std::bind(&PTSelectStmt::AnalyzeTarget,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2);
  err = target_->Analyze(sem_context, analyze);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  // Run error checking on the WHERE conditions.
  err = AnalyzeWhereClause(sem_context, where_clause_);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  LOG(INFO) << kErrorFontEnd;
  return err;
}

ErrorCode PTSelectStmt::AnalyzeTarget(TreeNode *target, SemContext *sem_context) {
  // Walking through the target expressions and collect all columns. Currently, CQL doesn't allow
  // any expression except for references to table column.
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  if (target->opcode() != TreeNodeOpcode::kPTRef) {
    err = ErrorCode::CQL_STATEMENT_INVALID;
    sem_context->Error(target->loc(), "Selecting expression is not allowed in CQL", err);
    return err;
  }

  PTRef *ref = static_cast<PTRef *>(target);
  err = ref->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  // Add the column descriptor to select_list_.
  const ColumnDesc *col_desc = ref->desc();
  if (col_desc == nullptr) {
    // This ref is pointing to the whole table (SELECT *).
    if (target_->size() != 1) {
      err = ErrorCode::CQL_STATEMENT_INVALID;
      sem_context->Error(target->loc(), "Selecting '*' is not allowed in this context", err);
      return err;
    }
    int num_cols = num_columns();
    for (int idx = 0; idx < num_cols; idx++) {
      selected_columns_.push_back(&table_columns_[idx]);
    }
  } else {
    selected_columns_.push_back(col_desc);
  }

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

ErrorCode PTTableRef::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  if (alias_ != nullptr) {
    err = ErrorCode::CQL_STATEMENT_INVALID;
    sem_context->Error(loc(), "Alias is not allowed", err);
  }
  return name_->Analyze(sem_context);
}

//--------------------------------------------------------------------------------------------------

}  // namespace sql
}  // namespace yb
