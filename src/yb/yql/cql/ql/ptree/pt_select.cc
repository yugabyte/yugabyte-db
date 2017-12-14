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

#include "yb/client/client.h"
#include "yb/common/index.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

using std::make_shared;

//--------------------------------------------------------------------------------------------------

namespace {

// Class to compare selectivity of an index for a SELECT statement.
class Selectivity {
 public:
  // Selectivity of the indexed table.
  Selectivity(MemoryContext *memctx, const PTSelectStmt& tnode);
  // Selectivity of an index.
  Selectivity(MemoryContext *memctx, const PTSelectStmt& tnode, const IndexInfo& index_info);

  int prefix_length() const { return prefix_length_; }
  bool ends_with_range() const { return ends_with_range_; }
  bool is_index() const { return is_index_; }
  bool is_local() const { return is_local_; }
  bool covers_fully() const { return covers_fully_; }
  TableId index_id() const { return index_id_; }

  bool operator>(const Selectivity& other) const;

  std::string ToString() const;

 private:
  // Find selectivity, currently defined as length of longest fully specified prefix and whether
  // there is a range operation immediately after the prefix.
  void FindSelectivity(MemoryContext *memctx,
                       const PTSelectStmt& tnode,
                       const MCUnorderedMap<int, size_t>& id_to_idx,
                       size_t num_hash_key_columns);

  TableId index_id_; // Index table id (null for indexed table).
  bool is_index_ = false; // Whether this is an index or the indexed table.
  bool is_local_ = false; // Whether the index is local (true for indexed table).
  size_t hash_length_ = 0; // Length of hash key in index or indexed table.
  size_t prefix_length_ = 0; // Length of fully specified prefix in index or indexed table.
  bool ends_with_range_ = false; // Whether there is a range clause after prefix.
  bool covers_fully_ = false; // Whether the index covers the read fully (true for indexed table).
};

// Properly update ops (for means of prefix and ends with range computations), for operation op at
// index (in ops) idx.
void AnalyzeOperatorSelectivity(const size_t idx,
                                const yb::QLOperator& op,
                                MCVector<QLOperator> *ops) {
  switch (op) {
    case QL_OP_GREATER_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL:
      (*ops)[idx] = QL_OP_GREATER_THAN;
      break;
    case QL_OP_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_IN:
      (*ops)[idx] = QL_OP_EQUAL;
      break;
    case QL_OP_NOOP: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN:
      break;
    default:
      // Should have been caught beforehand (as this is called in pt_select after analyzing the
      // where clause).
      break;
  }
}

// Return whether the index covers the read fully.
bool CoversFully(const IndexInfo& index_info, const MCSet<int32>& column_refs) {
  for (const int32 table_col_id : column_refs) {
    if (!index_info.IsColumnCovered(ColumnId(table_col_id))) {
      return false;
    }
  }
  return true;
}

Selectivity::Selectivity(MemoryContext *memctx, const PTSelectStmt& tnode)
    : is_index_(false),
      is_local_(true),
      hash_length_(tnode.table()->schema().num_hash_key_columns()),
      covers_fully_(true) {
  const client::YBSchema& schema = tnode.table()->schema();
  MCUnorderedMap<int, size_t> id_to_idx(memctx);
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    id_to_idx[schema.ColumnId(i)] = i;
  }
  FindSelectivity(memctx, tnode, id_to_idx, schema.num_hash_key_columns());
  VLOG(3) << ToString();
}

Selectivity::Selectivity(MemoryContext *memctx,
                         const PTSelectStmt& tnode,
                         const IndexInfo& index_info)
    : index_id_(index_info.table_id()),
      is_index_(true),
      is_local_(index_info.is_local()),
      hash_length_(index_info.hash_column_count()) {
  MCUnorderedMap<int, size_t> id_to_idx(memctx);
  for (size_t i = 0; i < index_info.key_column_count(); i++) {
    id_to_idx[index_info.column(i).indexed_column_id] = i;
  }
  FindSelectivity(memctx, tnode, id_to_idx, index_info.hash_column_count());

  // If prefix length is 0, the table is going to be better than the index anyway (because prefix
  // length 0 means do full index scan), so don't even check whether the index covers the read
  // fully.
  if (prefix_length_ > 0) {
    covers_fully_ = CoversFully(index_info, tnode.column_refs());
  }
  VLOG(3) << ToString();
}

void Selectivity::FindSelectivity(MemoryContext *memctx,
                                  const PTSelectStmt& tnode,
                                  const MCUnorderedMap<int, size_t>& id_to_idx,
                                  const size_t num_hash_key_columns) {
  // The operation on each column, in the order of the columns in the table or index we analyze.
  MCVector<QLOperator> ops(id_to_idx.size(), QL_OP_NOOP, memctx);
  for (const ColumnOp& col_op : tnode.key_where_ops()) {
    const auto iter = id_to_idx.find(col_op.desc()->id());
    if (iter != id_to_idx.end()) {
      AnalyzeOperatorSelectivity(iter->second, col_op.yb_op(), &ops);
    }
  }
  for (const ColumnOp& col_op : tnode.where_ops()) {
    const auto iter = id_to_idx.find(col_op.desc()->id());
    if (iter != id_to_idx.end()) {
      AnalyzeOperatorSelectivity(iter->second, col_op.yb_op(), &ops);
    }
  }

  // Now find the prefix length.
  while (prefix_length_ < ops.size() && ops[prefix_length_] == QL_OP_EQUAL) {
    prefix_length_++;
  }

  // If hash key not fully specified, set prefix length to 0, as we will have to do a full table
  // scan anyway.
  if (prefix_length_ < num_hash_key_columns) {
    prefix_length_ = 0;
    return;
  }

  // Now find out if it ends with a range.
  ends_with_range_ = (prefix_length_ < ops.size()) && ops[prefix_length_] == QL_OP_GREATER_THAN;
}

bool Selectivity::operator>(const Selectivity& other) const {
  // If different prefix lengths, the longer one is better.
  if (prefix_length_ != other.prefix_length_) {
    return prefix_length_ > other.prefix_length_;
  }

  // If same prefix lengths, the longer the hash length the better.
  if (hash_length_ != other.hash_length_) {
    return hash_length_ > other.hash_length_;
  }

  // If same prefix and hash lengths, but one ends with a range query and the other does not, the
  // one that does is better.
  if (ends_with_range_ != other.ends_with_range_) {
    return ends_with_range_ > other.ends_with_range_;
  }

  // If both previous values are the same, the indexed table is better than the index.
  if (is_index_ != other.is_index_) {
    return is_index_ < other.is_index_;
  }

  // An index that covers the read fully is better than one that does not.
  if (covers_fully_ != other.covers_fully_) {
    return covers_fully_ > other.covers_fully_;
  }

  // If all previous values are the same, a local index is better than a non-local index.
  return is_local_ > other.is_local_;
}


string Selectivity::ToString() const {
  return strings::Substitute("Selectivity: index_id $0 is_index $1 is_local $2 hash_length $3 "
                             "prefix_length $4 ends_with_range $5 covers_fully $6", index_id_,
                             is_index_, is_local_, hash_length_, prefix_length_, ends_with_range_,
                             covers_fully_);
}

} // namespace

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

  if (use_index_) {

    // Reset previous analysis results pertaining to the use of indexed table done below before
    // re-analyze using the index.
    func_ops_.clear();
    key_where_ops_.clear();
    where_ops_.clear();
    subscripted_col_where_ops_.clear();
    partition_key_ops_.clear();
    hash_col_bindvars_.clear();
    column_refs_.clear();
    static_column_refs_.clear();

    RETURN_NOT_OK(sem_context->LookupIndex(index_id_, loc(), &table_, &table_columns_,
                                           &num_key_columns_, &num_hash_key_columns_));
  } else {
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
  }

  // Analyze clauses in select statements and check that references to columns in selected_exprs
  // are valid and used appropriately.
  SemState sem_state(sem_context);
  sem_state.set_allowing_aggregate(true);
  sem_state.set_allowing_column_refs(true);
  RETURN_NOT_OK(selected_exprs_->Analyze(sem_context));
  sem_state.set_allowing_aggregate(false);
  sem_state.set_allowing_column_refs(false);

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

  // Check whether we should use an index.
  if (!use_index_) {
    RETURN_NOT_OK(AnalyzeIndexes(sem_context));
    // If AnalyzeIndexes() decides to use index, just return since a full re-analysis has been done.
    if (use_index_) {
      return Status::OK();
    }
  }

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

// Check whether we can use a local index.
// Updates use_index_, index_id_ and read_just_index_ fields properly.
CHECKED_STATUS PTSelectStmt::AnalyzeIndexes(SemContext *sem_context) {
  VLOG(3) << "AnalyzeIndexes: " << sem_context->stmt();
  if (table_->index_map().empty()) {
    return Status::OK();
  }

  // We can now find the best local index for this query. We are going for the most selective
  // index, where most selective is defined as having the largest fully specified prefix. In case
  // of a tie, choose the one that also specifies some range for the column right after the prefix.
  // If we still have a tie, we choose the indexed table over an index, then a local index over an
  // index, and if nothing else worked, an index that covers the select fully as opposed to the one
  // that has to go back to the indexed table.
  MCVector<Selectivity> selectivities(sem_context->PTempMem());
  selectivities.reserve(table_->index_map().size() + 1);
  selectivities.emplace_back(sem_context->PTempMem(), *this);
  for (const std::pair<TableId, IndexInfo>& index : table_->index_map()) {
    selectivities.emplace_back(sem_context->PTempMem(), *this, index.second);
  }
  std::sort(selectivities.begin(), selectivities.end(), std::greater<Selectivity>());

  // Find the best selectivity. For now, we will use an index only if it covers the read fully.
  for (const Selectivity& selectivity : selectivities) {
    if (selectivity.covers_fully()) {
      VLOG(3) << "Selected = " << selectivity.ToString();
      if (selectivity.is_index()) {
        use_index_ = true;
        read_just_index_ = true;
        index_id_ = selectivity.index_id();

        // If index is to be used, re-analyze using the index.
        sem_context->Reset();
        return Analyze(sem_context);
      }
      break;
    }
  }

  return Status::OK();
}

// -------------------------------------------------------------------------------------------------

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
