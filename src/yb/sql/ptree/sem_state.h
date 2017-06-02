//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// The SemState module defines the states of semantic process for expressions. Semantic states are
// different from semantic context.
// - The states consists of attributes that are used to process a tree node.
// - The context consists of attributes that are used for the entire compilation.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PTREE_SEM_STATE_H_
#define YB_SQL_PTREE_SEM_STATE_H_

#include "yb/sql/util/sql_env.h"
#include "yb/sql/ptree/process_context.h"
#include "yb/sql/ptree/column_desc.h"

namespace yb {
namespace sql {

class WhereExprState;

//--------------------------------------------------------------------------------------------------
// This class represents the state variables for the analyzing process of one tree node. This
// is just a stack varible that is constructed when a treenode is being processed and destructed
// when that process is done.
//
// Example:
// - Suppose user type the following statements
//     CREATE TABLE tab(id INT PRIMARY KEY);
//     INSERT INTO tab(id) values(expr);
// - When analyzing INSERT, we would do the following.
//   {
//     // Create a new state for sem_context.
//     SemState new_expr_state(sem_context, DataType::INT);
//
//     // Run express analyzer knowing that its expected type INT (=== targeted column type).
//     expr->Analyze(sem_context);
//
//     // When exiting this scope, sem_state are auto-swiched back to the previous state.
//   }
//
class SemState {
 public:
  // Constructor: Create a new sem_state to use and save the existing state to previous_state_.
  // TODO(mihnea) Remove the overload after working with YQLType for collection.
  SemState(SemContext *sem_context,
           DataType expected_yql_type_id = DataType::UNKNOWN_DATA,
           InternalType expected_internal_type = InternalType::VALUE_NOT_SET,
           const MCSharedPtr<MCString>& bindvar_name = nullptr);

  SemState(SemContext *sem_context,
           const std::shared_ptr<YQLType>& expected_yql_type,
           InternalType expected_internal_type = InternalType::VALUE_NOT_SET,
           const MCSharedPtr<MCString>& bindvar_name = nullptr);

  // Destructor: Reset sem_context back to previous_state_.
  virtual ~SemState();

  // Reset the sem_context back to its previous state.
  void ResetContextState();

  // Update state variable for where clause.
  void SetWhereState(WhereExprState *where_state) {
    where_state_ = where_state;
  }

  // Update the expr states.
  // TODO(mihnea) Remove the overload after working with YQLType for collection.
  void SetExprState(DataType yql_type_id,
                    InternalType internal_type,
                    const MCSharedPtr<MCString>& bindvar_name = nullptr,
                    const ColumnDesc *updating_counter = nullptr);
  void SetExprState(const std::shared_ptr<YQLType>& yql_type,
                    InternalType internal_type,
                    const MCSharedPtr<MCString>& bindvar_name = nullptr,
                    const ColumnDesc *updating_counter = nullptr);

  // Set the current state using previous state's values.
  void CopyPreviousStates();

  // Set the current state using previous state's values.
  void CopyPreviousWhereState();

  // Access function for expression states.
  const std::shared_ptr<YQLType>& expected_yql_type() const { return expected_yql_type_; }
  InternalType expected_internal_type() const { return expected_internal_type_; }
  WhereExprState *where_state() const { return where_state_; }
  const MCSharedPtr<MCString>& bindvar_name() const { return bindvar_name_; }
  const ColumnDesc *updating_counter() const { return updating_counter_; }

  bool processing_if_clause() const { return processing_if_clause_; }
  void set_processing_if_clause(bool value) { processing_if_clause_ = value; }

  void set_processing_column_definition(bool val) {
    processing_column_definition_ = val;
  }

  bool processing_column_definition() const {
    return processing_column_definition_;
  }

  void set_bindvar_name(string bindvar_name);

 private:
  // Context that owns this SemState.
  SemContext *sem_context_;

  // Save the previous state to reset when done.
  SemState *previous_state_;
  bool was_reset;

  // States to process an expression node.
  std::shared_ptr<YQLType> expected_yql_type_; // The expected sql type of an expression.
  InternalType expected_internal_type_;        // The expected internal type of an expression.

  MCSharedPtr<MCString> bindvar_name_;

  // State variables for where expression.
  WhereExprState *where_state_;

  // Predicate for processing a column definition in a table.
  bool processing_column_definition_ = false;

  // State variables for counters.
  const ColumnDesc *updating_counter_;

  // State variables for if clause.
  bool processing_if_clause_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_PTREE_SEM_STATE_H_
