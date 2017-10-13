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

#include "yb/ql/ptree/pt_select.h"

#include <functional>

#include "yb/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

using std::make_shared;

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
                           PTExprListNode::SharedPtr selected_exprs,
                           PTTableRefListNode::SharedPtr from_clause,
                           PTExpr::SharedPtr where_clause,
                           PTListNode::SharedPtr group_by_clause,
                           PTListNode::SharedPtr having_clause,
                           PTListNode::SharedPtr order_by_clause,
                           PTExpr::SharedPtr limit_clause)
    : PTDmlStmt(memctx, loc, false, where_clause),
      distinct_(distinct),
      selected_exprs_(selected_exprs),
      from_clause_(from_clause),
      group_by_clause_(group_by_clause),
      having_clause_(having_clause),
      order_by_clause_(order_by_clause),
      limit_clause_(limit_clause) {
}

PTSelectStmt::~PTSelectStmt() {
}

CHECKED_STATUS PTSelectStmt::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

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

  // Analyze clauses in select statements and check that references to columns in selected_exprs
  // are valid and used appropriately.
  SemState sem_state(sem_context);
  RETURN_NOT_OK(selected_exprs_->Analyze(sem_context));
  if (distinct_) {
    RETURN_NOT_OK(AnalyzeDistinctClause(sem_context));
  }

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  // Run error checking on the LIMIT clause.
  RETURN_NOT_OK(AnalyzeLimitClause(sem_context));

  // Constructing the schema of the result set.
  RETURN_NOT_OK(ConstructSelectedSchema());

  return Status::OK();
}

void PTSelectStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTSelectStmt::AnalyzeDistinctClause(SemContext *sem_context) {
  // Only partition and static columns are allowed to be used with distinct clause.
  int key_count = 0;
  for (const ColumnDesc& desc : table_columns_) {
    if (desc.is_hash()) {
      if (column_refs_.find(desc.id()) != column_refs_.end()) {
        key_count++;
      }
    } else if (!desc.is_static()) {
      if (column_refs_.find(desc.id()) != column_refs_.end()) {
        return sem_context->Error(
            selected_exprs_,
            "Selecting distinct must request only partition keys and static columns",
            ErrorCode::CQL_STATEMENT_INVALID);
      }
    }
  }

  if (key_count != 0 && key_count != num_hash_key_columns_) {
    return sem_context->Error(selected_exprs_,
                              "Selecting distinct must request all or none of partition keys",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTSelectStmt::AnalyzeLimitClause(SemContext *sem_context) {
  if (limit_clause_ == nullptr) {
    return Status::OK();
  }

  RETURN_NOT_OK(limit_clause_->CheckRhsExpr(sem_context));

  SemState sem_state(sem_context, QLType::Create(INT32), InternalType::kInt32Value);
  sem_state.set_bindvar_name(PTBindVar::limit_bindvar_name());
  RETURN_NOT_OK(limit_clause_->Analyze(sem_context));

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS PTSelectStmt::ConstructSelectedSchema() {
  const MCList<PTExpr::SharedPtr>& exprs = selected_exprs();
  selected_schemas_ = make_shared<vector<ColumnSchema>>();

  selected_schemas_->reserve(exprs.size());
  for (auto expr : exprs) {
    if (expr->opcode() == TreeNodeOpcode::kPTAllColumns) {
      const PTAllColumns *ref = static_cast<const PTAllColumns*>(expr.get());
      for (const auto& col_desc : ref->table_columns()) {
        selected_schemas_->emplace_back(col_desc.name(), col_desc.ql_type());
      }
    } else {
      selected_schemas_->emplace_back(expr->QLName(), expr->ql_type());
    }
  }
  return Status::OK();
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
  return name_->AnalyzeName(sem_context, OBJECT_TABLE);
}

//--------------------------------------------------------------------------------------------------

}  // namespace ql
}  // namespace yb
