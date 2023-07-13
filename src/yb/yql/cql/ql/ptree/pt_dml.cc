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
// Treenode implementation for DML including SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_dml.h"

#include <unordered_map>

#include "yb/client/schema.h"
#include "yb/client/table.h"

#include "yb/common/common.pb.h"
#include "yb/qlexpr/index.h"
#include "yb/qlexpr/index_column.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_dml_using_clause.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/ycql_predtest.h"

using std::string;

DECLARE_bool(allow_index_table_read_write);
DECLARE_bool(use_cassandra_authentication);

namespace yb {
namespace ql {

using strings::Substitute;

PTDmlStmt::PTDmlStmt(MemoryContext *memctx,
                     YBLocationPtr loc,
                     PTExpr::SharedPtr where_clause,
                     PTExpr::SharedPtr if_clause,
                     const bool else_error,
                     PTDmlUsingClausePtr using_clause,
                     const bool returns_status)
    : PTCollection(memctx, loc),
      where_clause_(where_clause),
      if_clause_(if_clause),
      else_error_(else_error),
      using_clause_(using_clause),
      returns_status_(returns_status),
      bind_variables_(memctx),
      column_map_(memctx),
      func_ops_(memctx),
      key_where_ops_(memctx),
      where_ops_(memctx),
      multi_col_where_ops_(memctx),
      subscripted_col_where_ops_(memctx),
      json_col_where_ops_(memctx),
      partition_key_ops_(memctx),
      hash_col_bindvars_(memctx),
      column_refs_(memctx),
      static_column_refs_(memctx),
      column_ref_cnts_(memctx),
      pk_only_indexes_(memctx),
      non_pk_only_indexes_(memctx) {
}

// Clone a DML tnode for re-analysis. Only the syntactic information populated by the parser should
// be cloned here. Semantic information should be left in the initial state to be populated when
// this tnode is analyzed.
PTDmlStmt::PTDmlStmt(MemoryContext *memctx, const PTDmlStmt& other, bool copy_if_clause)
    : PTCollection(memctx, other.loc_ptr()),
      where_clause_(other.where_clause_),
      if_clause_(copy_if_clause ? other.if_clause_ : nullptr),
      else_error_(other.else_error_),
      using_clause_(other.using_clause_),
      returns_status_(other.returns_status_),
      bind_variables_(other.bind_variables_, memctx),
      column_map_(memctx),
      func_ops_(memctx),
      key_where_ops_(memctx),
      where_ops_(memctx),
      multi_col_where_ops_(memctx),
      subscripted_col_where_ops_(memctx),
      json_col_where_ops_(memctx),
      partition_key_ops_(memctx),
      hash_col_bindvars_(memctx),
      column_refs_(memctx),
      static_column_refs_(memctx),
      column_ref_cnts_(memctx),
      pk_only_indexes_(memctx),
      non_pk_only_indexes_(memctx) {
}

PTDmlStmt::~PTDmlStmt() {
}

size_t PTDmlStmt::num_columns() const {
  return table_->schema().num_columns();
}

size_t PTDmlStmt::num_key_columns() const {
  return table_->schema().num_key_columns();
}

size_t PTDmlStmt::num_hash_key_columns() const {
  return table_->schema().num_hash_key_columns();
}

string PTDmlStmt::hash_key_columns() const {
  std::stringstream s;
  auto &schema = table_->schema();
  for (size_t i = 0; i < schema.num_hash_key_columns(); ++i) {
    if (i != 0) s << ", ";
    s << schema.Column(i).name();
  }
  return s.str();
}

Status PTDmlStmt::LookupTable(SemContext *sem_context) {
  if (FLAGS_use_cassandra_authentication) {
    switch (opcode()) {
      case TreeNodeOpcode::kPTSelectStmt: {
        if (!internal_) {
          if (down_cast<PTSelectStmt *>(this)->IsReadableByAllSystemTable()) {
            break;
          }
          RETURN_NOT_OK(sem_context->CheckHasTablePermission(loc(),
              PermissionType::SELECT_PERMISSION, this->table_name()));
        }
        break;
      }
      case TreeNodeOpcode::kPTUpdateStmt: FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTInsertStmt: FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTDeleteStmt: {
        RETURN_NOT_OK(sem_context->CheckHasTablePermission(loc(),
            PermissionType::MODIFY_PERMISSION, this->table_name()));
        break;
      }
      default:
        DFATAL_OR_RETURN_NOT_OK(STATUS_FORMAT(InternalError, "Unexpected operation $0", opcode()));
    }
  }
  is_system_ = table_name().is_system();
  if (is_system_ && IsWriteOp() && client::FLAGS_yb_system_namespace_readonly) {
    return sem_context->Error(table_loc(), ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  VLOG(3) << "Loading table descriptor for " << table_name().ToString();
  table_ = sem_context->GetTableDesc(table_name());
  if (!table_ || (table_->IsIndex() && !FLAGS_allow_index_table_read_write) ||
      // Only looking for CQL tables.
      (table_->table_type() != client::YBTableType::YQL_TABLE_TYPE)) {
    return sem_context->Error(table_loc(), ErrorCode::OBJECT_NOT_FOUND);
  }
  LoadSchema(sem_context, table_, &column_map_, false /* is_index */);
  return Status::OK();
}

void PTDmlStmt::LoadSchema(SemContext *sem_context,
                           const client::YBTablePtr& table,
                           MCColumnMap* column_map,
                           bool is_index) {
  column_map->clear();
  const client::YBSchema& schema = table->schema();
  for (size_t idx = 0; idx < schema.num_columns(); idx++) {
    const client::YBColumnSchema col = schema.Column(idx);
    string colname = col.name();
    if (is_index && !schema.table_properties().use_mangled_column_name()) {
      // This is an OLD INDEX. We need to mangled its column name to work with new implementation.
      colname = qlexpr::YcqlName::MangleColumnName(colname);
    }
    column_map->emplace(MCString(colname.c_str(), sem_context->PSemMem()),
                        ColumnDesc(idx,
                                   schema.ColumnId(idx),
                                   col.name(),
                                   idx < schema.num_hash_key_columns(),
                                   idx < schema.num_key_columns(),
                                   col.is_static(),
                                   col.is_counter(),
                                   col.type(),
                                   client::YBColumnSchema::ToInternalDataType(col.type()),
                                   is_index));
  }
}

// Node semantics analysis.
Status PTDmlStmt::Analyze(SemContext *sem_context) {
  sem_context->set_current_dml_stmt(this);
  MemoryContext *psem_mem = sem_context->PSemMem();
  column_args_ = MCMakeShared<MCVector<ColumnArg>>(psem_mem);
  subscripted_col_args_ = MCMakeShared<MCVector<SubscriptedColumnArg>>(psem_mem);
  json_col_args_ = MCMakeShared<MCVector<JsonColumnArg>>(psem_mem);
  return Status::OK();
}

const ColumnDesc* PTDmlStmt::GetColumnDesc(const SemContext *sem_context,
                                           const MCString& col_name) {
  const auto iter = column_map_.find(col_name);
  if (iter == column_map_.end()) {
    return nullptr;
  }

  const ColumnDesc* column_desc = &iter->second;

  // To indicate that DocDB must read a columm value to execute an expression, the column is added
  // to the column_refs list.
  bool reading_column = false;

  switch (opcode()) {
    case TreeNodeOpcode::kPTSelectStmt:
      reading_column = true;
      break;
    case TreeNodeOpcode::kPTUpdateStmt:
      if (sem_context->sem_state() != nullptr &&
          sem_context->processing_set_clause() &&
          !sem_context->processing_assignee()) {
        reading_column = true;
        break;
      }
      FALLTHROUGH_INTENDED;
    case TreeNodeOpcode::kPTInsertStmt:
    case TreeNodeOpcode::kPTDeleteStmt:
      if (sem_context->sem_state() != nullptr &&
          sem_context->processing_if_clause()) {
        reading_column = true;
        break;
      }
      break;
    default:
      break;
  }

  if (reading_column) {
    // TODO(neil) Currently AddColumnRef() relies on MCSet datatype to guarantee that we have a
    // unique list of IDs, but we should take advantage to "symbol table" when collecting data
    // for execution. Symbol table and "column_read_count_" need to be corrected so that we can
    // use MCList instead.

    // Indicate that this column must be read for the statement execution.
    AddColumnRef(*column_desc);
  }

  return column_desc;
}

Status PTDmlStmt::AnalyzeWhereClause(SemContext *sem_context) {
  if (!where_clause_) {
    if (IsWriteOp()) {
      return sem_context->Error(this, "Missing partition key", ErrorCode::CQL_STATEMENT_INVALID);
    }
    return Status::OK();
  }

  // Analyze where expression.
  if (IsWriteOp()) {
    key_where_ops_.resize(num_key_columns());
  } else {
    key_where_ops_.resize(num_hash_key_columns());
  }
  RETURN_NOT_OK(AnalyzeWhereExpr(sem_context, where_clause_.get()));
  return Status::OK();
}

Status PTDmlStmt::AnalyzeWhereExpr(SemContext *sem_context, PTExpr *expr) {
  // Construct the state variables and analyze the expression.
  MCVector<ColumnOpCounter> op_counters(sem_context->PTempMem());
  op_counters.resize(num_columns());
  ColumnOpCounter partition_key_counter;
  WhereExprState where_state(&where_ops_, &key_where_ops_, &subscripted_col_where_ops_,
                             &json_col_where_ops_, &partition_key_ops_, &op_counters,
                             &partition_key_counter, opcode(), &func_ops_, &multi_col_where_ops_);

  SemState sem_state(sem_context, QLType::Create(DataType::BOOL), InternalType::kBoolValue);
  sem_state.SetWhereState(&where_state);
  RETURN_NOT_OK(expr->Analyze(sem_context));

  if (IsWriteOp()) {
    // Make sure that all hash entries are referenced in where expression.
    for (size_t idx = 0; idx < num_hash_key_columns(); idx++) {
      if (op_counters[idx].eq_count() == 0) {
        return sem_context->Error(expr, "Missing condition on key columns in WHERE clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
    }

    // If writing static columns only, check that either all range key entries are referenced in the
    // where expression or none is referenced. Else, check that all range key are referenced.
    size_t range_keys = 0;
    for (auto idx = num_hash_key_columns(); idx < num_key_columns(); idx++) {
      if (op_counters[idx].eq_count() != 0) {
        range_keys++;
      }
    }
    if (StaticColumnArgsOnly()) {
      if (range_keys != num_key_columns() - num_hash_key_columns() && range_keys != 0)
        return sem_context->Error(expr, "Missing condition on key columns in WHERE clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      if (range_keys == 0) {
        key_where_ops_.resize(num_hash_key_columns());
      }
    } else {
      if (range_keys != num_key_columns() - num_hash_key_columns()) {
        if (opcode() == TreeNodeOpcode::kPTDeleteStmt) {
          // Range expression in write requests are allowed for deletes only.
          for (auto idx = num_hash_key_columns(); idx < num_key_columns(); idx++) {
            if (op_counters[idx].eq_count() != 0) {
              where_ops_.push_front(key_where_ops_[idx]);
            }
          }
          key_where_ops_.resize(num_hash_key_columns());
        } else {
          return sem_context->Error(expr, "Missing condition on key columns in WHERE clause",
                                    ErrorCode::CQL_STATEMENT_INVALID);
        }
      }
    }
  } else { // ReadOp
    // Add the hash to the where clause if the list is incomplete. Clear key_where_ops_ to do
    // whole-table scan.
    for (size_t idx = 0; idx < num_hash_key_columns(); idx++) {
      if (!key_where_ops_[idx].IsInitialized()) {
        has_incomplete_hash_ = true;
        break;
      }
    }
    if (has_incomplete_hash_) {
      for (auto idx = num_hash_key_columns(); idx > 0;) {
        --idx;
        if (key_where_ops_[idx].IsInitialized()) {
          where_ops_.push_front(key_where_ops_[idx]);
        }
      }
      key_where_ops_.clear();
    } else {
      select_has_primary_keys_set_ = true;
      // Unset if there is a range key without a condition.
      for (auto idx = num_hash_key_columns(); idx < num_key_columns(); idx++) {
        if (op_counters[idx].IsEmpty()) {
          select_has_primary_keys_set_ = false;
          break;
        }
      }
    }
  }

  // Analyze bind variables for hash columns in the WHERE clause.
  RETURN_NOT_OK(AnalyzeHashColumnBindVars(sem_context));

  return Status::OK();
}

Status PTDmlStmt::AnalyzeIfClause(SemContext *sem_context) {
  if (if_clause_) {
    SemState sem_state(sem_context, QLType::Create(DataType::BOOL), InternalType::kBoolValue);
    sem_state.set_processing_if_clause(true);
    return if_clause_->Analyze(sem_context);
  }
  return Status::OK();
}

Status PTDmlStmt::AnalyzeUsingClause(SemContext *sem_context) {
  if (using_clause_ == nullptr) {
    return Status::OK();
  }

  RETURN_NOT_OK(using_clause_->Analyze(sem_context));
  return Status::OK();
}

Status PTDmlStmt::AnalyzeIndexesForWrites(SemContext *sem_context) {
  const Schema& indexed_schema = table_->InternalSchema();
  for (const auto& itr : table_->index_map()) {
    const TableId& index_id = itr.first;
    const auto& index = itr.second;

    bool primary_key_cols_only = index.PrimaryKeyColumnsOnly(indexed_schema);

    std::shared_ptr<const IndexInfoPB::WherePredicateSpecPB>& where_predicate_spec_pb =
      index.where_predicate_spec();

    // If the index has primary key columns only and doesn't reference non-pk columns in
    // predicate, index updates can be issued from the CQL proxy side without reading the current
    // row as long as the DML does not delete the column (including setting the value to null).
    // Otherwise, the updates needed can only be determined from the tserver side after the current
    // values are read.

    // TODO (Piyush) - Right now we are not distinguishing between -
    //   1. primary columns only in index where clause (if it is present) and
    //   2. non-primary columns in index where clause.
    // This distinction can help in optimizing the index write path as per discussion
    // in the Partial Indexes design doc.
    if (primary_key_cols_only && !where_predicate_spec_pb) {
      std::shared_ptr<client::YBTable> index_table = sem_context->GetTableDesc(index_id);
      if (index_table == nullptr) {
        return sem_context->Error(this, Substitute("Index table $0 not found", index_id).c_str(),
                                  ErrorCode::OBJECT_NOT_FOUND);
      }
      pk_only_indexes_.insert(index_table);
    } else {
      non_pk_only_indexes_.insert(index_id);
      for (const auto& column : index.columns()) {
        const ColumnId indexed_column_id = column.indexed_column_id;
        if (!indexed_schema.is_key_column(indexed_column_id)) {
          column_refs_.insert(indexed_column_id);
        }
      }

      // In case non-pk columns are present in the index predicate (valid only for a partial index),
      // add those references as well.
      if (where_predicate_spec_pb) {
        for (auto column_id : where_predicate_spec_pb->column_ids())
          column_refs_.insert(column_id);
      }
    }
  }
  return Status::OK();
}

bool PTDmlStmt::RequiresTransaction() const {
  return IsWriteOp() && !DCHECK_NOTNULL(table_.get())->index_map().empty() &&
      table_->InternalSchema().table_properties().is_transactional();
}

Status PTDmlStmt::AnalyzeHashColumnBindVars(SemContext *sem_context) {
  // If not all hash columns are bound, clear hash_col_bindvars_ because the client driver will not
  // be able to compute the full hash key unless it parses the SQL statement and extracts the
  // literals also, which is not currently supported and unlikely to be.
  if (hash_col_bindvars_.size() != num_hash_key_columns()) {
    hash_col_bindvars_.clear();
  }

  return Status::OK();
}

Status PTDmlStmt::AnalyzeColumnArgs(SemContext *sem_context) {

  // If we have no args, this must be a delete modifying primary key only.
  if (column_args_->empty() && subscripted_col_args_->empty() && json_col_args_->empty()) {
    modifies_primary_row_ = true;
    return Status::OK();
  }

  // If we have range keys we modify the primary row.
  for (auto idx = num_hash_key_columns(); idx < num_key_columns(); idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      modifies_primary_row_ = true;
      break;
    }
  }

  // If we have column args:
  //  - Writing to static columns => modify static row.
  //  - Writing to non-static columns -> modify primary row.

  // Check plain column args.
  for (auto idx = num_key_columns(); idx < column_args_->size(); idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      if (column_args_->at(idx).desc()->is_static()) {
        modifies_static_row_ = true;
      } else {
        modifies_primary_row_ = true;
      }
      if (modifies_static_row_ && modifies_primary_row_) {
        return Status::OK();
      }
    }
  }

  // Check subscripted column args (e.g. map['k'] or list[1])
  for (auto& arg : *subscripted_col_args_) {
    if (arg.desc()->is_static()) {
      modifies_static_row_ = true;
    } else {
      modifies_primary_row_ = true;
    }
    if (modifies_static_row_ && modifies_primary_row_) {
      return Status::OK();
    }
  }

  // Check json column args (e.g. json->'key' or json->1)
  for (auto& arg : *json_col_args_) {
    if (arg.desc()->is_static()) {
      modifies_static_row_ = true;
    } else {
      modifies_primary_row_ = true;
    }
    if (modifies_static_row_ && modifies_primary_row_) {
      return Status::OK();
    }
  }

  return Status::OK();
}

// Are we writing to static columns only, i.e. no range columns or non-static columns.
bool PTDmlStmt::StaticColumnArgsOnly() const {
  return modifies_static_row_ && !modifies_primary_row_;
}

//--------------------------------------------------------------------------------------------------

Status AnalyzeStepState::AnalyzePartitionKeyOp(SemContext *sem_context,
                                               const PTRelationExpr *expr,
                                               PTExprPtr value) {
  partition_key_ops_->emplace_back(expr->ql_op(), value);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status WhereExprState::AnalyzeMultiColumnOp(
    SemContext* sem_context,
    const PTRelationExpr* expr,
    const std::vector<const ColumnDesc*>
        col_descs,
    PTExprPtr value) {
  for (const auto& col_desc : col_descs) {
    if (col_desc == nullptr) {
      return STATUS(InternalError, "Column does not exist");
    }
  }

  if (col_descs.empty()) {
    return sem_context->Error(expr, "No range columns specified", ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (statement_type_ != TreeNodeOpcode::kPTSelectStmt) {
    return sem_context->Error(
        expr, "Operator not supported for write operations",
        ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
  }

  if (expr->ql_op() != QL_OP_IN) {
    return sem_context->Error(
        expr, "Multi-column relation not supported for the operator",
        ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (!value->has_no_column_ref()) {
    return sem_context->Error(
        value,
        "Argument of this opreator cannot reference a column",
        ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (value->expr_op() != ExprOperator::kCollection && value->expr_op() != ExprOperator::kBindVar) {
    return sem_context->Error(
        value, "Invalid operand for IN clause", ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (value->expr_op() == ExprOperator::kCollection) {
    const auto options = static_cast<const PTCollectionExpr*>(value.get());

    for (const auto& option : options->values()) {
      if (option->expr_op() != ExprOperator::kCollection &&
          option->expr_op() != ExprOperator::kBindVar) {
        return sem_context->Error(
            option, "Invalid operand for IN clause", ErrorCode::CQL_STATEMENT_INVALID);
      }
    }
  }

  int idx = -1;
  for (const auto& col_desc : col_descs) {
    if (sem_context->void_primary_key_condition() && col_desc->is_primary()) {
      // Drop the key condition from where clause as instructed.
      return Status::OK();
    }
    if (!col_desc->is_primary() || col_desc->is_hash()) {
      return sem_context->Error(
          expr,
          Format(
              "Multi-column relations can only be applied to clustering columns but was applied "
              "to: $0",
              col_desc->name()),
          ErrorCode::CQL_STATEMENT_INVALID);
    }
    if (idx != -1 && (idx + 1) != static_cast<int>(col_desc->index())) {
      if (idx == static_cast<int>(col_desc->index())) {
        // repeating column
        return sem_context->Error(
            expr,
            Format("Column \"$0\" appeared twice in a relation", col_desc->name()),
            ErrorCode::CQL_STATEMENT_INVALID);
      }
      return sem_context->Error(
          expr, "Clustering columns must appear in the PRIMARY KEY order in multi-column relations",
          ErrorCode::CQL_STATEMENT_INVALID);
    }
    idx = static_cast<int>(col_desc->index());
    ColumnOpCounter& counter = op_counters_->at(col_desc->index());
    counter.increase_in();
    if (!counter.is_valid()) {
      return sem_context->Error(
          expr, "Illogical condition for where clause", ErrorCode::CQL_STATEMENT_INVALID);
    }
  }
  multi_colum_ops_->emplace_back(col_descs, value, expr->ql_op());

  return Status::OK();
}

Status WhereExprState::AnalyzeColumnOp(SemContext *sem_context,
                                       const PTRelationExpr *expr,
                                       const ColumnDesc *col_desc,
                                       PTExpr::SharedPtr value,
                                       PTExprListNode::SharedPtr col_args) {
  // If this is a nested select from an uncovered index/partial index,
  // ignore column that is uncovered/only in partial index predicate and not in index cols.
  if (col_desc == nullptr) {
    if (sem_context->IsUncoveredIndexSelect() || sem_context->IsPartialIndexSelect())
      return Status::OK();

    return STATUS(InternalError, "Column does not exist");
  }

  // If a SELECT involves a partial index scan, we can safely ignore some sub-clauses of WHERE
  // clause if they are taken care of by the index predicate.
  //
  // Preserve the sub-clause in case of below exceptions for child SELECT:
  //    For col = val type of sub-clauses on a prefix of the index's primary key, it is better
  //    to again take note of the sub-clauses because it will help decide -
  //      i) the tserver to land on (if all hash cols are specified)
  //      ii) the range to scan (if range col is specified and is part of a prefix found in WHERE)

  if (statement_type_ == TreeNodeOpcode::kPTSelectStmt) {
    PTSelectStmt* select_stmt = static_cast<PTSelectStmt*>(sem_context->current_dml_stmt());
    if (select_stmt->child_select()) {
      // Parent SELECT (of a nested select).
      std::shared_ptr<client::YBTable> table = select_stmt->table();
      auto it = table->index_map().find(select_stmt->child_select()->index_id());

      RSTATUS_DCHECK(it != table->index_map().end(), InternalError, "Index should be present");
      const auto& idx_info = it->second;

      if (idx_info.where_predicate_spec()) {
        // It is a partial index.
        ColumnOp op(col_desc, value, expr->ql_op());
        if (VERIFY_RESULT(OpInExpr(idx_info.where_predicate_spec()->where_expr(), op))) {
          return Status::OK();
        }
      }
    } else if (!select_stmt->index_id().empty()) {
      // Child SELECT.
      std::shared_ptr<client::YBTable> table = select_stmt->table();
      const auto& idx_info = table->index_info();

      if (idx_info.where_predicate_spec()) {
        // First attempt to preserve the sub-clause if it might be useful.
        bool preserve_col_op = false;
        auto prefix_len = sem_context->index_select_prefix_length();
        // Only in case all hash cols are set, we can even attempt to preserve the sub-clause.
        bool all_hash_cols_set = prefix_len >= idx_info.hash_column_count();
        if (all_hash_cols_set) {
          if (col_desc->index() < prefix_len && expr->ql_op() == QL_OP_EQUAL)
            preserve_col_op = true;
          else if (col_desc->index() == prefix_len + 1 &&
                   col_desc->index() < idx_info.range_column_count() &&
                   (expr->ql_op() == QL_OP_LESS_THAN ||
                    expr->ql_op() == QL_OP_LESS_THAN_EQUAL ||
                    expr->ql_op() == QL_OP_GREATER_THAN ||
                    expr->ql_op() == QL_OP_GREATER_THAN_EQUAL)
                  )
            preserve_col_op = true;
        }

        if (!preserve_col_op) {
          const auto& idx_col = idx_info.column(col_desc->index());
          // Change to id in indexed table because we are have those ids in the index predicate
          // as well.
          ColumnDesc translated_col_desc(*col_desc);
          translated_col_desc.set_id(idx_col.indexed_column_id);

          ColumnOp op(&translated_col_desc, value, expr->ql_op());
          if (VERIFY_RESULT(OpInExpr(idx_info.where_predicate_spec()->where_expr(), op))) {
            return Status::OK();
          }
        }
      }
    }
  }

  if (sem_context->void_primary_key_condition() && col_desc->is_primary()) {
    // Drop the key condition from where clause as instructed.
    return Status::OK();
  }

  ColumnOpCounter& counter = op_counters_->at(col_desc->index());
  switch (expr->ql_op()) {
    case QL_OP_EQUAL: {
      counter.increase_eq(col_args != nullptr);
      if (!counter.is_valid()) {
        return sem_context->Error(expr, "Illogical condition for where clause",
            ErrorCode::CQL_STATEMENT_INVALID);
      }

      // Check that the column is being used correctly.
      switch (statement_type_) {
        case TreeNodeOpcode::kPTInsertStmt:
        case TreeNodeOpcode::kPTUpdateStmt:
        case TreeNodeOpcode::kPTDeleteStmt: {
          if (!col_desc->is_primary()) {
            return sem_context->Error(expr,
                "Non primary key cannot be used in where clause for write requests",
                ErrorCode::CQL_STATEMENT_INVALID);
          }
          (*key_ops_)[col_desc->index()].Init(col_desc, value, QLOperator::QL_OP_EQUAL);
          break;
        }
        case TreeNodeOpcode::kPTSelectStmt: {
          if (col_desc->is_hash()) {
            (*key_ops_)[col_desc->index()].Init(col_desc, value, QLOperator::QL_OP_EQUAL);
          } else if (col_args != nullptr) {
            if (col_desc->ql_type()->IsJson()) {
              json_col_ops_->emplace_back(col_desc, col_args, value, expr->ql_op());
            } else {
              subscripted_col_ops_->emplace_back(col_desc, col_args, value, expr->ql_op());
            }
          } else {
            ops_->emplace_back(col_desc, value, QLOperator::QL_OP_EQUAL);
          }
          break;
        }
        default:
          return sem_context->Error(expr, "Statement type cannot have where condition",
              ErrorCode::CQL_STATEMENT_INVALID);
      }
      break;
    }

    case QL_OP_LESS_THAN: FALLTHROUGH_INTENDED;
    case QL_OP_LESS_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_GREATER_THAN: {
      // Inequality conditions on hash columns are not allowed.
      // - Ignore the error if key condition is not needed.
      if (col_desc->is_hash()) {
        return sem_context->Error(expr, "Partition column cannot be used in this expression",
            ErrorCode::CQL_STATEMENT_INVALID);
      }

      // Check for illogical conditions.
      if (col_args == nullptr) { // subcolumn conditions don't affect the condition counter.
        if (expr->ql_op() == QL_OP_LESS_THAN || expr->ql_op() == QL_OP_LESS_THAN_EQUAL) {
          counter.increase_lt(col_args != nullptr);
        } else {
          counter.increase_gt(col_args != nullptr);
        }

        if (!counter.is_valid()) {
          return sem_context->Error(expr, "Illogical condition for where clause",
              ErrorCode::CQL_STATEMENT_INVALID);
        }
      }

      // Check that the column is being used correctly.
      switch (statement_type_) {
        case TreeNodeOpcode::kPTInsertStmt:
        case TreeNodeOpcode::kPTUpdateStmt:
          if (col_desc->is_primary()) {
            return sem_context->Error(expr,
                "Range expressions are not supported for inserts and updates",
                ErrorCode::CQL_STATEMENT_INVALID);
          }
          FALLTHROUGH_INTENDED;
        case TreeNodeOpcode::kPTDeleteStmt: {
          if (!col_desc->is_primary()) {
            return sem_context->Error(expr,
                "Non primary key cannot be used in where clause for write requests",
                ErrorCode::CQL_STATEMENT_INVALID);
          }
          ops_->emplace_back(col_desc, value, expr->ql_op());
          break;
        }
        case TreeNodeOpcode::kPTSelectStmt: {
          // Cache the column operator for execution.
          if (col_args != nullptr) {
            if (col_desc->ql_type()->IsJson()) {
              json_col_ops_->emplace_back(col_desc, col_args, value, expr->ql_op());
            } else {
              subscripted_col_ops_->emplace_back(col_desc, col_args, value, expr->ql_op());
            }
          } else {
            ops_->emplace_back(col_desc, value, expr->ql_op());
          }
          break;
        }
        default:
          return sem_context->Error(expr, "Statement type cannot have where condition",
              ErrorCode::CQL_STATEMENT_INVALID);
      }
      break;
    }

    case QL_OP_CONTAINS_KEY: {
      if (col_args != nullptr) {
        return sem_context->Error(
            expr, "Operator not supported for subscripted column",
            ErrorCode::CQL_STATEMENT_INVALID);
      } else {
        counter.increase_contains_key();
        if (!counter.is_valid()) {
          return sem_context->Error(
              expr, "Illogical condition for where clause", ErrorCode::CQL_STATEMENT_INVALID);
        }
        ops_->emplace_back(col_desc, value, expr->ql_op());
      }
      break;
    }

    case QL_OP_CONTAINS: {
      if (col_args != nullptr) {
        return sem_context->Error(
            expr, "Operator not supported for subscripted column",
            ErrorCode::CQL_STATEMENT_INVALID);
      } else {
        counter.increase_contains();
        if (!counter.is_valid()) {
          return sem_context->Error(
              expr, "Illogical condition for where clause", ErrorCode::CQL_STATEMENT_INVALID);
        }
        ops_->emplace_back(col_desc, value, expr->ql_op());
      }
      break;
    }

    case QL_OP_NOT_EQUAL: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN: FALLTHROUGH_INTENDED;
    case QL_OP_IN: {
      if (statement_type_ != TreeNodeOpcode::kPTSelectStmt) {
        return sem_context->Error(expr, "Operator not supported for write operations",
                                  ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
      }

      if (col_args != nullptr) {
        return sem_context->Error(expr, "Operator not supported for subscripted column",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }

      if(!value->has_no_column_ref()) {
        return sem_context->Error(expr,
            "Argument of this opreator cannot reference a column",
            ErrorCode::CQL_STATEMENT_INVALID);
      }

      counter.increase_in(col_args != nullptr);
      if (!counter.is_valid()) {
        return sem_context->Error(expr, "Illogical condition for where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }

      if (expr->ql_op() == QL_OP_IN && col_desc->is_hash()) {
        (*key_ops_)[col_desc->index()].Init(col_desc, value, QLOperator::QL_OP_IN);
      } else {
        ops_->emplace_back(col_desc, value, expr->ql_op());
      }
      break;
    }

    default:
      return sem_context->Error(expr, "Operator is not supported in where clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }

  return Status::OK();
}

Status WhereExprState::AnalyzeColumnFunction(SemContext *sem_context,
                                             const PTRelationExpr *expr,
                                             PTExpr::SharedPtr value,
                                             PTBcallPtr call) {
  switch (expr->ql_op()) {
    case QL_OP_LESS_THAN:
    case QL_OP_LESS_THAN_EQUAL:
    case QL_OP_EQUAL:
    case QL_OP_GREATER_THAN_EQUAL:
    case QL_OP_IN:
    case QL_OP_NOT_IN:
    case QL_OP_GREATER_THAN: {
      func_ops_->emplace_back(value, call, expr->ql_op());
      break;
    }

    default:
      return sem_context->Error(expr, "Operator is not supported in where clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Check that if where clause is present, it must follow CQL rules.
  return Status::OK();
}

Status WhereExprState::AnalyzePartitionKeyOp(SemContext *sem_context,
                                             const PTRelationExpr *expr,
                                             PTExpr::SharedPtr value) {
  switch (expr->ql_op()) {
    case QL_OP_LESS_THAN: {
      partition_key_counter_->increase_lt();
      break;
    }
    case QL_OP_LESS_THAN_EQUAL: {
      partition_key_counter_->increase_lt();
      break;
    }
    case QL_OP_EQUAL: {
      partition_key_counter_->increase_eq();
      break;
    }
    case QL_OP_GREATER_THAN_EQUAL: {
      partition_key_counter_->increase_gt();
      break;
    }
    case QL_OP_GREATER_THAN: {
      partition_key_counter_->increase_gt();
      break;
    }

    default:
      return sem_context->Error(expr, "Operator is not supported for token in where clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (!partition_key_counter_->is_valid()) {
    return sem_context->Error(expr, "Illogical where condition for token in where clause",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  return AnalyzeStepState::AnalyzePartitionKeyOp(sem_context, expr, value);
}

std::vector<int64_t> PTDmlStmt::hash_col_indices() const {
  std::vector<int64_t> indices;
  indices.reserve(hash_col_bindvars_.size());
  for (const PTBindVar* bindvar : hash_col_bindvars_) {
    indices.emplace_back(bindvar->pos());
  }
  return indices;
}

void PTDmlStmt::AddColumnRef(const ColumnDesc& col_desc) {
  if (col_desc.is_static()) {
    static_column_refs_.insert(col_desc.id());
  } else {
    column_refs_.insert(col_desc.id());
  }

  if (column_ref_cnts_.find(col_desc.id()) == column_ref_cnts_.end())
    column_ref_cnts_[col_desc.id()] = 0;
  column_ref_cnts_[col_desc.id()]++;
}

std::string PTDmlStmt::PartitionKeyToString(const MCList<PartitionKeyOp>& conds) {
  std::string str;
  for (auto col_op = conds.begin(); col_op != conds.end(); ++col_op) {
    std::stringstream s;
    if (col_op != conds.begin()) {
      s << " AND ";
    }
    // Partition_hash is stored as INT32, token is stored as INT64, unless you specify the
    // rhs expression e.g partition_hash(h1, h2) >= 3 in which case it's stored as an VARINT.
    // So setting the default to the yql partition_hash in that case seems reasonable.
    string label = (col_op->expr()->expected_internal_type() == InternalType::kInt64Value) ?
        "token" : "partition_hash";
    s << "(" << label << "(" << hash_key_columns() <<  ") " << QLOperatorAsString(col_op->yb_op())
      << " " << col_op->expr()->QLName() << ")";
    str += s.str();
  }
  return str;
}

PTExprPtr PTDmlStmt::ttl_seconds() const {
  return using_clause_ ? using_clause_->ttl_seconds() : nullptr;
}

PTExprPtr PTDmlStmt::user_timestamp_usec() const {
  return using_clause_ ? using_clause_->user_timestamp_usec() : nullptr;
}

void PTDmlStmt::AddRefForAllColumns() {
  for (const auto& pair : column_map_) {
    AddColumnRef(pair.second);
  }
}

void PTDmlStmt::AddHashColumnBindVar(PTBindVar* bindvar) {
  hash_col_bindvars_.insert(bindvar);
}

bool PTDmlStmt::HashColCmp::operator()(const PTBindVar* v1, const PTBindVar* v2) const {
  DCHECK(v1->hash_col() != nullptr) << "bindvar pos " << v1->pos() << " is not a hash column";
  DCHECK(v2->hash_col() != nullptr) << "bindvar pos " << v2->pos() << " is not a hash column";
  return v1->hash_col()->id() < v2->hash_col()->id();
}

}  // namespace ql
}  // namespace yb
