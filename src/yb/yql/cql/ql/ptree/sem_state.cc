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
                   const ColumnDesc *lhs_col)
    : sem_context_(sem_context),
      expected_ql_type_(expected_ql_type),
      expected_internal_type_(expected_internal_type),
      bindvar_name_(bindvar_name),
      lhs_col_(lhs_col) {
  // Passing down state variables that stay the same until they are set or reset.
  if (sem_context->sem_state() != nullptr) {
    selecting_from_index_ = sem_context_->selecting_from_index();
    processing_if_clause_ = sem_context_->processing_if_clause();
    processing_set_clause_ = sem_context_->processing_set_clause();
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
                            const ColumnDesc *lhs_col) {
  expected_ql_type_ = ql_type;
  expected_internal_type_ = internal_type;
  bindvar_name_ = bindvar_name;
  lhs_col_ = lhs_col;
}

void SemState::CopyPreviousStates() {
  if (previous_state_ != nullptr) {
    expected_ql_type_ = previous_state_->expected_ql_type_;
    expected_internal_type_ = previous_state_->expected_internal_type_;
    bindvar_name_ = previous_state_->bindvar_name_;
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

void SemState::set_bindvar_name(string name) {
  bindvar_name_ = MCMakeShared<MCString>(sem_context_->PSemMem(), name.data(), name.size());
}

void SemState::add_index_column_ref(int32_t col_id) {
  if (index_column_) {
    index_column_->AddIndexedRef(col_id);
  }
}

bool SemState::is_uncovered_index_select() const {
  return DCHECK_NOTNULL(sem_context_)->IsUncoveredIndexSelect();
}

}  // namespace ql}  // namespace ql
}  // namespace yb
