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
//--------------------------------------------------------------------------------------------------

#include "yb/client/table.h"
#include "yb/qlexpr/index.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

using std::shared_ptr;
using client::YBTable;

//--------------------------------------------------------------------------------------------------

SemState::SemState(SemContext *sem_context,
                   const std::shared_ptr<QLType>& expected_ql_type,
                   InternalType expected_internal_type,
                   const MCSharedPtr<MCString>& bindvar_name,
                   const ColumnDesc *lhs_col,
                   NullIsAllowed allow_null)
    : sem_context_(sem_context),
      expected_ql_type_(expected_ql_type),
      allow_null_ql_type_(allow_null),
      expected_internal_type_(expected_internal_type),
      bindvar_name_(bindvar_name),
      alternative_bindvar_names_(
          MCMakeShared<MCVector<MCSharedPtr<MCString>>>(sem_context->PSemMem())),
      lhs_col_(lhs_col) {
  // Passing down state variables that stay the same until they are set or reset.
  if (sem_context->sem_state() != nullptr) {
    // Must forward "current_dml_stmt_", so we always know what tree is being analyzed.
    //
    // TODO(neil) Change this name to root_node_ and use it for all statements including DML.
    // Currently, the statement roots are set in SemContext and should have been moved to SemState
    // to support "nested / index" statement.
    current_dml_stmt_ = sem_context->current_dml_stmt();

    // Index analysis states.
    scan_state_ = sem_context->scan_state();
    selecting_from_index_ = sem_context_->selecting_from_index();
    index_select_prefix_length_ = sem_context_->index_select_prefix_length();
    void_primary_key_condition_ = sem_context_->void_primary_key_condition();

    // Clause analysis states.
    // TODO(neil) Need to find reason why WhereExprState and IfExprState are not passed forward
    // and add comments on that here.
    processing_if_clause_ = sem_context_->processing_if_clause();
    validate_orderby_expr_ = sem_context->validate_orderby_expr();
    processing_set_clause_ = sem_context_->processing_set_clause();
    idx_predicate_state_ = sem_context->idx_predicate_state();

    // Operator states.
    processing_assignee_ = sem_context_->processing_assignee();
    allowing_column_refs_ = sem_context_->allowing_column_refs();
  }

  // Use this new state for semantic analysis.
  sem_context_->set_sem_state(this, &previous_state_);
}

SemState::~SemState() {
  // Reset the state.
  ResetContextState();
}

void SemState::ResetContextState() {
  // Reset state if it has not been reset.
  if (!was_reset) {
    sem_context_->reset_sem_state(previous_state_);
    was_reset = true;
  }
}

void SemState::SetExprState(const std::shared_ptr<QLType>& ql_type,
                            InternalType internal_type,
                            const MCSharedPtr<MCString>& bindvar_name,
                            const ColumnDesc *lhs_col,
                            NullIsAllowed allow_null) {
  expected_ql_type_ = ql_type;
  allow_null_ql_type_ = allow_null;
  expected_internal_type_ = internal_type;
  bindvar_name_ = bindvar_name;
  lhs_col_ = lhs_col;
}

void SemState::CopyPreviousStates() {
  if (previous_state_ != nullptr) {
    expected_ql_type_ = previous_state_->expected_ql_type_;
    allow_null_ql_type_ = previous_state_->allow_null_ql_type_;
    expected_internal_type_ = previous_state_->expected_internal_type_;
    bindvar_name_ = previous_state_->bindvar_name_;
    alternative_bindvar_names_ = previous_state_->alternative_bindvar_names_;
    where_state_ = previous_state_->where_state_;
  }
}

void SemState::CopyPreviousWhereState() {
  if (previous_state_ != nullptr) {
    where_state_ = previous_state_->where_state_;
  }
}

void SemState::CopyPreviousIfState() {
  if (previous_state_ != nullptr) {
    if_state_ = previous_state_->if_state_;
  }
}

void SemState::set_bindvar_name(std::string name) {
  bindvar_name_ = MCMakeShared<MCString>(sem_context_->PSemMem(), name.data(), name.size());
}

void SemState::add_alternate_bindvar_name(std::string name) {
  alternative_bindvar_names_->push_back(
      MCMakeShared<MCString>(sem_context_->PSemMem(), name.data(), name.size()));
}

void SemState::add_index_column_ref(int32_t col_id) {
  index_column_->AddIndexedRef(col_id);
}

bool SemState::is_uncovered_index_select() const {
  if (current_dml_stmt_ == nullptr ||
      current_dml_stmt_->opcode() != TreeNodeOpcode::kPTSelectStmt) {
    return false;
  }
  // Applicable to SELECT statement only.
  const auto* select_stmt = static_cast<const PTSelectStmt*>(current_dml_stmt_);
  return !select_stmt->index_id().empty() && !select_stmt->covers_fully();
}

bool SemState::is_partial_index_select() const {
  if (current_dml_stmt_ == nullptr ||
      current_dml_stmt_->opcode() != TreeNodeOpcode::kPTSelectStmt) {
    return false;
  }
  // Applicable to SELECT statement only.
  const auto* select_stmt = static_cast<const PTSelectStmt*>(current_dml_stmt_);
  if (select_stmt->index_id().empty()) return false;

  std::shared_ptr<client::YBTable> table = select_stmt->table();
  const auto& idx_info = table->index_info();
  return idx_info.where_predicate_spec() != nullptr;
}

const ColumnDesc *SemState::hash_col() const {
  return lhs_col_ != nullptr && lhs_col_->is_hash() ? lhs_col_ : nullptr;
}

const QLTypePtr& SemState::DefaultQLType() {
  static const auto result = QLType::Create(DataType::UNKNOWN_DATA);
  return result;
}

}  // namespace ql}  // namespace ql
}  // namespace yb
