//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/sem_state.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using client::YBTable;

//--------------------------------------------------------------------------------------------------

SemState::SemState(SemContext *sem_context,
                   DataType expected_yql_type_id,
                   InternalType expected_internal_type,
                   const MCSharedPtr<MCString>& bindvar_name,
                   const ColumnDesc *lhs_col)
    : sem_context_(sem_context),
      previous_state_(nullptr),
      was_reset(false),
      expected_yql_type_(YQLType::Create(expected_yql_type_id)),
      expected_internal_type_(expected_internal_type),
      bindvar_name_(bindvar_name),
      where_state_(nullptr),
      lhs_col_(lhs_col),
      processing_if_clause_(false) {
  // Passing down state variables that stay the same until they are set or reset.
  if (sem_context->sem_state() != nullptr) {
    processing_if_clause_ = sem_context_->processing_if_clause();
  }

  // Use this new state for semantic analysis.
  sem_context_->set_sem_state(this, &previous_state_);
}

SemState::SemState(SemContext *sem_context,
                   const std::shared_ptr<YQLType>& expected_yql_type,
                   InternalType expected_internal_type,
                   const MCSharedPtr<MCString>& bindvar_name,
                   const ColumnDesc *lhs_col)
    : sem_context_(sem_context),
      previous_state_(nullptr),
      was_reset(false),
      expected_yql_type_(expected_yql_type),
      expected_internal_type_(expected_internal_type),
      bindvar_name_(bindvar_name),
      where_state_(nullptr),
      lhs_col_(lhs_col),
      processing_if_clause_(false) {
  // Passing down state variables that stay the same until they are set or reset.
  if (sem_context->sem_state() != nullptr) {
    processing_if_clause_ = sem_context_->processing_if_clause();
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

void SemState::SetExprState(DataType yql_type_id,
                            InternalType internal_type,
                            const MCSharedPtr<MCString>& bindvar_name,
                            const ColumnDesc *lhs_col) {
  expected_yql_type_ = YQLType::Create(yql_type_id);
  expected_internal_type_ = internal_type;
  bindvar_name_ = bindvar_name;
  lhs_col_ = lhs_col;
}

void SemState::SetExprState(const std::shared_ptr<YQLType>& yql_type,
                            InternalType internal_type,
                            const MCSharedPtr<MCString>& bindvar_name,
                            const ColumnDesc *lhs_col) {
  expected_yql_type_ = yql_type;
  expected_internal_type_ = internal_type;
  bindvar_name_ = bindvar_name;
  lhs_col_ = lhs_col;
}

void SemState::CopyPreviousStates() {
  if (previous_state_ != nullptr) {
    expected_yql_type_ = previous_state_->expected_yql_type_;
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

void SemState::set_bindvar_name(string name) {
  bindvar_name_ = MCMakeShared<MCString>(sem_context_->PSemMem(), name.data(), name.size());
}

}  // namespace sql}  // namespace sql
}  // namespace yb
