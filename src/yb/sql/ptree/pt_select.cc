//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
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

CHECKED_STATUS PTValues::Analyze(SemContext *sem_context) {
  return Status::OK();
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
                           const bool distinct,
                           PTListNode::SharedPtr target,
                           PTTableRefListNode::SharedPtr from_clause,
                           PTExpr::SharedPtr where_clause,
                           PTListNode::SharedPtr group_by_clause,
                           PTListNode::SharedPtr having_clause,
                           PTListNode::SharedPtr order_by_clause,
                           PTExpr::SharedPtr limit_clause)
    : PTDmlStmt(memctx, loc, false, where_clause),
      distinct_(distinct),
      target_(target),
      from_clause_(from_clause),
      group_by_clause_(group_by_clause),
      having_clause_(having_clause),
      order_by_clause_(order_by_clause),
      limit_clause_(limit_clause),
      selected_columns_(nullptr) {
}

PTSelectStmt::~PTSelectStmt() {
}

CHECKED_STATUS PTSelectStmt::Analyze(SemContext *sem_context) {

  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  MemoryContext *psem_mem = sem_context->PSemMem();
  selected_columns_ = MCMakeShared<MCVector<const ColumnDesc*>>(psem_mem);

  // Get the table descriptor.
  if (from_clause_->size() > 1) {
    return sem_context->Error(from_clause_, "Only one selected table is allowed",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  RETURN_NOT_OK(from_clause_->Analyze(sem_context));

  // Collect table's schema for semantic analysis.
  Status s = LookupTable(sem_context);
  if (PREDICT_FALSE(!s.ok())) {
    // If it is a system table and it does not exist, do not analyze further. We will return
    // void result when the SELECT statement is executed.
    return is_system() ? Status::OK() : s;
  }

  // Run error checking on the select list 'target_'.
  // Check that all targets are valid references to table columns.
  selected_columns_->reserve(num_columns());
  TreeNodePtrOperator<SemContext> analyze = std::bind(&PTSelectStmt::AnalyzeTarget,
                                                      this,
                                                      std::placeholders::_1,
                                                      std::placeholders::_2);
  RETURN_NOT_OK(target_->Analyze(sem_context, analyze));

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  // Run error checking on the LIMIT clause.
  RETURN_NOT_OK(AnalyzeLimitClause(sem_context));

  return Status::OK();
}

CHECKED_STATUS PTSelectStmt::AnalyzeLimitClause(SemContext *sem_context) {
  if (limit_clause_ == nullptr) {
    return Status::OK();
  }

  RETURN_NOT_OK(limit_clause_->CheckRhsExpr(sem_context));

  SemState sem_state(sem_context, YQLType::Create(INT64), InternalType::kInt64Value);
  sem_state.set_bindvar_name(PTBindVar::limit_bindvar_name());
  RETURN_NOT_OK(limit_clause_->Analyze(sem_context));

  return Status::OK();
}

namespace {

CHECKED_STATUS AnalyzeDistinctColumn(TreeNode *target,
                                     const ColumnDesc *col_desc,
                                     SemContext *sem_context) {
  if (col_desc->is_primary() && !col_desc->is_hash()) {
    return sem_context->Error(target, "Selecting distinct range column is not yet supported",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  if (!col_desc->is_primary() && !col_desc->is_static()) {
    return sem_context->Error(target, "Selecting distinct non-static column is not yet supported",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

} // namespace

CHECKED_STATUS PTSelectStmt::AnalyzeTarget(TreeNode *target, SemContext *sem_context) {
  // Walking through the target expressions and collect all columns. Currently, CQL doesn't allow
  // any expression except for references to table column.
  if (target->opcode() != TreeNodeOpcode::kPTRef) {
    return sem_context->Error(target, "Selecting expression is not allowed in CQL",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  PTRef *ref = static_cast<PTRef *>(target);

  if (ref->name() == nullptr) { // This ref is pointing to the whole table (SELECT *)
    if (target_->size() != 1) {
      return sem_context->Error(target, "Selecting '*' is not allowed in this context",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
    int num_cols = num_columns();
    for (int idx = 0; idx < num_cols; idx++) {
      const ColumnDesc *col_desc = &table_columns_[idx];
      if (distinct_) {
        RETURN_NOT_OK(AnalyzeDistinctColumn(target, col_desc, sem_context));
      }
      selected_columns_->push_back(col_desc);

      // Inform the executor that we need to read this column.
      AddColumnRef(*col_desc);
    }
  } else { // Add the column descriptor to selected_columns_.
    // Set expected type to UNKNOWN as we don't expected any datatype.
    SemState sem_state(sem_context);
    RETURN_NOT_OK(ref->Analyze(sem_context));
    const ColumnDesc *col_desc = ref->desc();
    if (distinct_) {
      RETURN_NOT_OK(AnalyzeDistinctColumn(target, col_desc, sem_context));
    }
    selected_columns_->push_back(col_desc);
  }

  return Status::OK();
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
                       MCSharedPtr<MCString> alias)
    : TreeNode(memctx, loc),
      name_(name),
      alias_(alias) {
}

PTTableRef::~PTTableRef() {
}

CHECKED_STATUS PTTableRef::Analyze(SemContext *sem_context) {
  if (alias_ != nullptr) {
    return sem_context->Error(this, "Alias is not allowed", ErrorCode::CQL_STATEMENT_INVALID);
  }
  return name_->Analyze(sem_context);
}

//--------------------------------------------------------------------------------------------------

}  // namespace sql
}  // namespace yb
