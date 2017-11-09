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

#include "yb/yql/cql/ql/ptree/pt_select.h"

#include <functional>

#include "yb/yql/cql/ql/ptree/sem_context.h"

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
                           PTOrderByListNode::SharedPtr order_by_clause,
                           PTExpr::SharedPtr limit_clause)
    : PTDmlStmt(memctx, loc, where_clause),
      distinct_(distinct),
      is_forward_scan_(true),
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
  sem_state.set_allowing_aggregate(true);
  RETURN_NOT_OK(selected_exprs_->Analyze(sem_context));
  sem_state.set_allowing_aggregate(false);
  if (distinct_) {
    RETURN_NOT_OK(AnalyzeDistinctClause(sem_context));
  }

  // Check if this is an aggregate read.
  bool has_aggregate_expr = false;
  bool has_singular_expr = false;
  for (auto expr_node : selected_exprs_->node_list()) {
    if (expr_node->IsAggregateCall()) {
      has_aggregate_expr = true;
    } else {
      has_singular_expr = true;
    }
  }
  if (has_aggregate_expr && has_singular_expr) {
    return sem_context->Error(
        selected_exprs_,
        "Selecting aggregate together with rows of non-aggregate values is not allowed",
        ErrorCode::CQL_STATEMENT_INVALID);
  }
  is_aggregate_ = has_aggregate_expr;

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  RETURN_NOT_OK(AnalyzeOrderByClause(sem_context));

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

namespace {

PTOrderBy::Direction directionFromSortingType(ColumnSchema::SortingType sorting_type) {
  return sorting_type == ColumnSchema::SortingType::kDescending ?
      PTOrderBy::Direction::kDESC : PTOrderBy::Direction::kASC;
}

} // namespace


CHECKED_STATUS PTSelectStmt::AnalyzeOrderByClause(SemContext *sem_context) {
  if (order_by_clause_ != nullptr) {
    if (key_where_ops_.empty()) {
      return sem_context->Error(
          order_by_clause_,
          "All hash columns must be set if order by clause is present.",
          ErrorCode::INVALID_ARGUMENTS);
    }

    unordered_map<string, PTOrderBy::Direction> order_by_map;
    for (auto& order_by : order_by_clause_->node_list()) {
      RETURN_NOT_OK(order_by->Analyze(sem_context));
      order_by_map[order_by->name()->QLName()] = order_by->direction();
    }
    const auto& schema = table_->schema();
    vector<bool> is_column_forward;
    is_column_forward.reserve(schema.num_range_key_columns());
    bool last_column_order_specified = true;
    for (size_t i = schema.num_hash_key_columns(); i < schema.num_key_columns(); i++) {
      const auto& column = schema.Column(i);
      if (order_by_map.find(column.name()) != order_by_map.end()) {
        if (!last_column_order_specified) {
          return sem_context->Error(
              order_by_clause_,
              "Order by currently only support the ordering of columns following their declared"
                  " order in the PRIMARY KEY", ErrorCode::INVALID_ARGUMENTS);
        }
        is_column_forward.push_back(
            directionFromSortingType(column.sorting_type()) == order_by_map[column.name()]);
        order_by_map.erase(column.name());
      } else {
        last_column_order_specified = false;
        is_column_forward.push_back(is_column_forward.empty() || is_column_forward.back());
      }
    }
    if (!order_by_map.empty()) {
      return sem_context->Error(
          order_by_clause_,
          ("Order by is should only contain clustering columns, got " + order_by_map.begin()->first)
              .c_str(), ErrorCode::INVALID_ARGUMENTS);
    }
    is_forward_scan_ = is_column_forward[0];
    for (auto&& b : is_column_forward) {
      if (b != is_forward_scan_) {
        return sem_context->Error(
            order_by_clause_,
            "Unsupported order by relation", ErrorCode::INVALID_ARGUMENTS);
      }
    }
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

Status PTOrderBy::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(name_->Analyze(sem_context));
  if (name_->expr_op() != ExprOperator::kRef) {
    return sem_context->Error(
        this,
        "Order By clause contains invalid expression",
        ErrorCode::INVALID_ARGUMENTS);
  }
  return Status::OK();
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
