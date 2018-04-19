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

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"
#include "yb/common/table_properties_constants.h"

namespace yb {
namespace ql {

using strings::Substitute;

const PTExpr::SharedPtr PTDmlStmt::kNullPointerRef = nullptr;

PTDmlStmt::PTDmlStmt(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     PTExpr::SharedPtr where_clause,
                     PTExpr::SharedPtr if_clause,
                     PTDmlUsingClause::SharedPtr using_clause)
  : PTCollection(memctx, loc),
    where_clause_(where_clause),
    if_clause_(if_clause),
    using_clause_(using_clause),
    bind_variables_(memctx),
    table_columns_(memctx),
    func_ops_(memctx),
    key_where_ops_(memctx),
    where_ops_(memctx),
    subscripted_col_where_ops_(memctx),
    json_col_where_ops_(memctx),
    partition_key_ops_(memctx),
    hash_col_bindvars_(memctx),
    column_refs_(memctx),
    static_column_refs_(memctx),
    pk_only_indexes_(memctx),
    non_pk_only_indexes_(memctx) {
}

PTDmlStmt::~PTDmlStmt() {
}

CHECKED_STATUS PTDmlStmt::LookupTable(SemContext *sem_context) {
  return sem_context->LookupTable(table_name(), table_loc(), IsWriteOp(), &table_, &is_system_,
                                  &table_columns_, &num_key_columns_, &num_hash_key_columns_);
}

// Node semantics analysis.
CHECKED_STATUS PTDmlStmt::Analyze(SemContext *sem_context) {
  sem_context->set_current_dml_stmt(this);
  MemoryContext *psem_mem = sem_context->PSemMem();
  column_args_ = MCMakeShared<MCVector<ColumnArg>>(psem_mem);
  subscripted_col_args_ = MCMakeShared<MCVector<SubscriptedColumnArg>>(psem_mem);
  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeWhereClause(SemContext *sem_context,
                                             const PTExpr::SharedPtr& where_clause) {
  if (where_clause == nullptr) {
    if (IsWriteOp()) {
      return sem_context->Error(this, "Missing partition key", ErrorCode::CQL_STATEMENT_INVALID);
    }
    return Status::OK();
  }

  // Analyze where expression.
  if (IsWriteOp()) {
    key_where_ops_.resize(num_key_columns_);
  } else {
    key_where_ops_.resize(num_hash_key_columns_);
  }
  RETURN_NOT_OK(AnalyzeWhereExpr(sem_context, where_clause.get()));
  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeWhereExpr(SemContext *sem_context, PTExpr *expr) {
  // Construct the state variables and analyze the expression.
  MCVector<ColumnOpCounter> op_counters(sem_context->PTempMem());
  op_counters.resize(num_columns());
  ColumnOpCounter partition_key_counter;
  WhereExprState where_state(&where_ops_, &key_where_ops_, &subscripted_col_where_ops_,
                             &json_col_where_ops_, &partition_key_ops_, &op_counters,
                             &partition_key_counter, opcode(), &func_ops_);

  SemState sem_state(sem_context, QLType::Create(BOOL), InternalType::kBoolValue);
  sem_state.SetWhereState(&where_state);
  RETURN_NOT_OK(expr->Analyze(sem_context));

  if (IsWriteOp()) {
    // Make sure that all hash entries are referenced in where expression.
    for (int idx = 0; idx < num_hash_key_columns_; idx++) {
      if (op_counters[idx].eq_count() == 0) {
        return sem_context->Error(expr, "Missing condition on key columns in WHERE clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
    }

    // If writing static columns only, check that either all range key entries are referenced in the
    // where expression or none is referenced. Else, check that all range key are referenced.
    int range_keys = 0;
    for (int idx = num_hash_key_columns_; idx < num_key_columns_; idx++) {
      if (op_counters[idx].eq_count() != 0) {
        range_keys++;
      }
    }
    if (StaticColumnArgsOnly()) {
      if (range_keys != num_key_columns_ - num_hash_key_columns_ && range_keys != 0)
        return sem_context->Error(expr, "Missing condition on key columns in WHERE clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      if (range_keys == 0) {
        key_where_ops_.resize(num_hash_key_columns_);
      }
    } else {
      if (range_keys != num_key_columns_ - num_hash_key_columns_) {
        if (opcode() == TreeNodeOpcode::kPTDeleteStmt) {
          // Range expression in write requests are allowed for deletes only.
          key_where_ops_.resize(num_hash_key_columns_);
        } else {
          return sem_context->Error(expr, "Missing condition on key columns in WHERE clause",
                                    ErrorCode::CQL_STATEMENT_INVALID);
        }
      }
    }
  } else {
    // Add the hash to the where clause if the list is incomplete. Clear key_where_ops_ to do
    // whole-table scan.
    bool has_incomplete_hash = false;
    for (int idx = 0; idx < num_hash_key_columns_; idx++) {
      if (!key_where_ops_[idx].IsInitialized()) {
        has_incomplete_hash = true;
        break;
      }
    }
    if (has_incomplete_hash) {
      for (int idx = num_hash_key_columns_ - 1; idx >= 0; idx--) {
        if (key_where_ops_[idx].IsInitialized()) {
          where_ops_.push_front(key_where_ops_[idx]);
        }
      }
      key_where_ops_.clear();
    }
  }

  // Analyze bind variables for hash columns in the WHERE clause.
  RETURN_NOT_OK(AnalyzeHashColumnBindVars(sem_context));

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeIfClause(SemContext *sem_context,
                                          const PTExpr::SharedPtr& if_clause) {
  if (if_clause != nullptr) {
    SemState sem_state(sem_context, QLType::Create(BOOL), InternalType::kBoolValue);
    sem_state.set_processing_if_clause(true);
    return if_clause->Analyze(sem_context);
  }
  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeUsingClause(SemContext *sem_context) {
  if (using_clause_ == nullptr) {
    return Status::OK();
  }

  RETURN_NOT_OK(using_clause_->Analyze(sem_context));
  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeIndexesForWrites(SemContext *sem_context) {
  const Schema& indexed_schema = table_->InternalSchema();
  for (const auto& itr : table_->index_map()) {
    const TableId& index_id = itr.first;
    const IndexInfo& index = itr.second;
    // If the index indexes the primary key columns only, index updates can be issued from the CQL
    // proxy side without reading the current row so long as the DML does not delete the column
    // (including setting the value to null). Otherwise, the updates needed can only be determined
    // from the tserver side after the current values are read.
    if (index.PrimaryKeyColumnsOnly(indexed_schema)) {
      std::shared_ptr<client::YBTable> index_table = sem_context->GetTableDesc(index_id);
      if (index_table == nullptr) {
        return sem_context->Error(this, Substitute("Index table $0 not found", index_id).c_str(),
                                  ErrorCode::TABLE_NOT_FOUND);
      }
      pk_only_indexes_[index_id] = index_table;
    } else {
      non_pk_only_indexes_.insert(index_id);
      for (const IndexInfo::IndexColumn& column : index.columns()) {
        const ColumnId indexed_column_id = column.indexed_column_id;
        if (!indexed_schema.is_key_column(indexed_column_id)) {
          column_refs_.insert(indexed_column_id);
        }
      }
    }
  }
  return Status::OK();
}

bool PTDmlStmt::RequireTransaction() const {
  return IsWriteOp() && !DCHECK_NOTNULL(table_.get())->index_map().empty();
}

CHECKED_STATUS PTDmlStmt::AnalyzeHashColumnBindVars(SemContext *sem_context) {
  // If not all hash columns are bound, clear hash_col_bindvars_ because the client driver will not
  // be able to compute the full hash key unless it parses the SQL statement and extracts the
  // literals also, which is not currently supported and unlikely to be.
  if (hash_col_bindvars_.size() != num_hash_key_columns_) {
    hash_col_bindvars_.clear();
  }

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeInterDependency(SemContext *sem_context) {
  // A DML modifies the hash key if it modifies a static column. It will potentially affect another
  // DML reading from the static column.
  if (column_args_ != nullptr) {
    for (const auto& arg : *column_args_) {
      if (arg.IsInitialized() && arg.desc()->is_static()) {
        modifies_hash_key_ = true;
        break;
      }
    }
  }
  // A DML modifies the primary key if the key contains a non-hash column, i.e. not something like
  // INSERT INTO t (hash_col, static_col) VALUES (...);
  for (const auto& op : key_where_ops_) {
    if (!op.desc()->is_hash()) {
      modifies_primary_key_ = true;
      break;
    }
  }
  return Status::OK();
}

// Are we writing to static columns only, i.e. no range columns or non-static columns.
bool PTDmlStmt::StaticColumnArgsOnly() const {
  if (column_args_->empty() && subscripted_col_args_->empty()) {
    return false;
  }
  bool write_range_columns = false;
  for (int idx = num_hash_key_columns_; idx < num_key_columns_; idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      write_range_columns = true;
      break;
    }
  }
  bool write_static_columns = false;
  bool write_non_static_columns = false;
  for (int idx = num_key_columns_; idx < column_args_->size(); idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      if (column_args_->at(idx).desc()->is_static()) {
        write_static_columns = true;
      } else {
        write_non_static_columns = true;
      }
      if (write_static_columns && write_non_static_columns) {
        break;
      }
    }
  }
  for (auto& arg : *subscripted_col_args_) {
    if (write_static_columns && write_non_static_columns) {
      break;
    }
    if (arg.desc()->is_static()) {
      write_static_columns = true;
    } else {
      write_non_static_columns = true;
    }
  }
  return write_static_columns && !write_range_columns && !write_non_static_columns;
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS WhereExprState::AnalyzeColumnOp(SemContext *sem_context,
                                               const PTRelationExpr *expr,
                                               const ColumnDesc *col_desc,
                                               PTExpr::SharedPtr value,
                                               PTExprListNode::SharedPtr col_args) {
  ColumnOpCounter& counter = op_counters_->at(col_desc->index());
  switch (expr->ql_op()) {
    case QL_OP_EQUAL: {
      counter.increase_eq();
      if (!counter.isValid()) {
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
      if (col_desc->is_hash()) {
        return sem_context->Error(expr, "Partition column cannot be used in this expression",
            ErrorCode::CQL_STATEMENT_INVALID);
      }

      // Check for illogical conditions.
      if (col_args == nullptr) { // subcolumn conditions don't affect the condition counter.
        if (expr->ql_op() == QL_OP_LESS_THAN || expr->ql_op() == QL_OP_LESS_THAN_EQUAL) {
          counter.increase_lt();
        } else {
          counter.increase_gt();
        }

        if (!counter.isValid()) {
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

    case QL_OP_NOT_IN: FALLTHROUGH_INTENDED;
    case QL_OP_IN: {
      if (statement_type_ != TreeNodeOpcode::kPTSelectStmt) {
        return sem_context->Error(expr, "IN expression not supported for write operations",
                                  ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
      }

      if (col_args != nullptr) {
        return sem_context->Error(expr, "IN expression not supported for subscripted column",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }

      if(!value->has_no_column_ref()) {
        return sem_context->Error(expr,
            "Expressions are not allowed as argument of IN condition.",
            ErrorCode::CQL_STATEMENT_INVALID);
      }

      counter.increase_in();
      if (!counter.isValid()) {
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

CHECKED_STATUS WhereExprState::AnalyzeColumnFunction(SemContext *sem_context,
                                                     const PTRelationExpr *expr,
                                                     PTExpr::SharedPtr value,
                                                     PTBcall::SharedPtr call) {
  switch (expr->ql_op()) {
    case QL_OP_LESS_THAN:
    case QL_OP_LESS_THAN_EQUAL:
    case QL_OP_EQUAL:
    case QL_OP_GREATER_THAN_EQUAL:
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


CHECKED_STATUS WhereExprState::AnalyzePartitionKeyOp(SemContext *sem_context,
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

  if (!partition_key_counter_->isValid()) {
    return sem_context->Error(expr, "Illogical where condition for token in where clause",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  partition_key_ops_->emplace_back(expr->ql_op(), value);
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
