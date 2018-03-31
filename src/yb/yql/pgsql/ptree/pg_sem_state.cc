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

#include "yb/yql/pgsql/ptree/pg_sem_state.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

using std::shared_ptr;
using client::YBTable;

//--------------------------------------------------------------------------------------------------

PgSemState::PgSemState(PgCompileContext *compile_context,
                   const std::shared_ptr<QLType>& expected_ql_type,
                   InternalType expected_internal_type,
                   const ColumnDesc *lhs_col)
    : compile_context_(compile_context),
      expected_ql_type_(expected_ql_type),
      expected_internal_type_(expected_internal_type),
      lhs_col_(lhs_col) {
  // Passing down state variables that stay the same until they are set or reset.
  if (compile_context->sem_state() != nullptr) {
    processing_set_clause_ = compile_context_->processing_set_clause();
    processing_assignee_ = compile_context_->processing_assignee();
  }

  // Use this new state for semantic analysis.
  compile_context_->set_sem_state(this, &previous_state_);
}

PgSemState::~PgSemState() {
  // Reset the state.
  ResetContextState();
}

void PgSemState::ResetContextState() {
  // Reset state if it has not been reset.
  if (!was_reset) {
    compile_context_->reset_sem_state(previous_state_);
    was_reset = true;
  }
}

void PgSemState::SetExprState(const std::shared_ptr<QLType>& ql_type,
                            InternalType internal_type,
                            const ColumnDesc *lhs_col) {
  expected_ql_type_ = ql_type;
  expected_internal_type_ = internal_type;
  lhs_col_ = lhs_col;
}

void PgSemState::CopyPreviousStates() {
  if (previous_state_ != nullptr) {
    expected_ql_type_ = previous_state_->expected_ql_type_;
    expected_internal_type_ = previous_state_->expected_internal_type_;
  }
}

}  // namespace pgsql}  // namespace pgsql
}  // namespace yb
