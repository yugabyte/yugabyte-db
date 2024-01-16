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
// The SemState module defines the states of semantic process for expressions. Semantic states are
// different from semantic context.
// - The states consists of attributes that are used to process a tree node.
// - The context consists of attributes that are used for the entire compilation.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/util/logging.h"
#include "yb/common/common_fwd.h"
#include "yb/common/ql_datatype.h"

#include "yb/util/memory/mc_types.h"
#include "yb/util/strongly_typed_bool.h"

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"

namespace yb {
namespace ql {

class SelectScanInfo;
class WhereExprState;
class IfExprState;
class IdxPredicateState;
class PTColumnDefinition;
class PTDmlStmt;

//--------------------------------------------------------------------------------------------------
// This class represents the state variables for the analyzing process of one tree node. This
// is just a stack variable that is constructed when a treenode is being processed and destructed
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
//     // When exiting this scope, sem_state are auto-switched back to the previous state.
//   }
//
class SemState {
 public:
  // Constructor: Create a new sem_state to use and save the existing state to previous_state_.
  explicit SemState(SemContext *sem_context,
                    const QLTypePtr& expected_ql_type = DefaultQLType(),
                    InternalType expected_internal_type = InternalType::VALUE_NOT_SET,
                    const MCSharedPtr<MCString>& bindvar_name = nullptr,
                    const ColumnDesc *lhs_col = nullptr,
                    NullIsAllowed allow_null = NullIsAllowed::kTrue);

  // Destructor: Reset sem_context back to previous_state_.
  virtual ~SemState();

  // Read previous state.
  const SemState *previous_state() const {
    return previous_state_;
  }

  // Reset the sem_context back to its previous state.
  void ResetContextState();

  // State variable for index analysis.
  void SetScanState(SelectScanInfo *scan_state) {
    scan_state_ = scan_state;
  }
  SelectScanInfo *scan_state() {
    return scan_state_;
  }

  // State variable to checking key condition.
  bool void_primary_key_condition() const {
    return void_primary_key_condition_;
  }

  void set_void_primary_key_condition(bool val) {
    void_primary_key_condition_ = val;
  }

  // Update state variable for where clause.
  void SetWhereState(WhereExprState *where_state) {
    where_state_ = where_state;
  }
  WhereExprState *where_state() const { return where_state_; }

  // Update state variable for if clause.
  void SetIfState(IfExprState *if_state) {
    if_state_ = if_state;
  }
  IfExprState *if_state() const { return if_state_; }

  // Update state variable for orderby clause.
  void set_validate_orderby_expr(bool validate_orderby_expr) {
    validate_orderby_expr_ = validate_orderby_expr;
  }
  bool validate_orderby_expr() const {
    return validate_orderby_expr_;
  }

  // Update the expr states.
  void SetExprState(const std::shared_ptr<QLType>& ql_type,
                    InternalType internal_type,
                    const MCSharedPtr<MCString>& bindvar_name = nullptr,
                    const ColumnDesc *lhs_col = nullptr,
                    NullIsAllowed allow_null = NullIsAllowed::kTrue);

  // Update state variable for index predicate clause i.e, where clause.
  void SetIdxPredicateState(IdxPredicateState *idx_predicate_state) {
    idx_predicate_state_ = idx_predicate_state;
  }
  IdxPredicateState *idx_predicate_state() const { return idx_predicate_state_; }

  // Set the current state using previous state's values.
  void CopyPreviousStates();

  // Set the current state using previous state's values.
  void CopyPreviousWhereState();

  // Set the current state using previous state's values.
  void CopyPreviousIfState();

  // Access function for expression states.
  const std::shared_ptr<QLType>& expected_ql_type() const { return expected_ql_type_; }
  NullIsAllowed expected_ql_type_accepts_null() const { return allow_null_ql_type_; }
  InternalType expected_internal_type() const { return expected_internal_type_; }

  // Return the hash column descriptor on LHS if available.
  const ColumnDesc *lhs_col() const { return lhs_col_; }
  const ColumnDesc *hash_col() const;

  void set_bindvar_name(std::string bindvar_name);
  void add_alternate_bindvar_name(std::string bindvar_name);
  const MCSharedPtr<MCString>& bindvar_name() const { return bindvar_name_; }
  const MCSharedPtr<MCVector<MCSharedPtr<MCString>>> &alternative_bindvar_names() const {
    return alternative_bindvar_names_;
  }

  PTDmlStmt *current_dml_stmt() const {
    return current_dml_stmt_;
  }

  void set_current_dml_stmt(PTDmlStmt *stmt) {
    current_dml_stmt_ = stmt;
  }

  bool processing_set_clause() const { return processing_set_clause_; }
  void set_processing_set_clause(bool value) { processing_set_clause_ = value; }

  bool processing_assignee() const { return processing_assignee_; }
  void set_processing_assignee(bool value) { processing_assignee_ = value; }

  void set_index_select_prefix_length(size_t val) { index_select_prefix_length_ = val; }
  size_t index_select_prefix_length() const { return index_select_prefix_length_; }

  void set_selecting_from_index(bool val) { selecting_from_index_ = val; }
  bool selecting_from_index() const { return selecting_from_index_; }

  void set_processing_column_definition(bool val) { processing_column_definition_ = val; }
  bool processing_column_definition() const { return processing_column_definition_; }

  bool processing_if_clause() const { return processing_if_clause_; }
  void set_processing_if_clause(bool value) { processing_if_clause_ = value; }

  bool allowing_aggregate() const {
    return allowing_aggregate_;
  }
  void set_allowing_aggregate(bool val) {
    allowing_aggregate_ = val;
  }

  bool allowing_column_refs() const {
    return allowing_column_refs_;
  }
  void set_allowing_column_refs(bool val) {
    allowing_column_refs_ = val;
  }

  void set_processing_index_column(PTColumnDefinition *index_column) {
    index_column_ = index_column;
  }
  bool is_processing_index_column() const {
    return (index_column_ != nullptr);
  }
  void add_index_column_ref(int32_t col_id);

  bool is_uncovered_index_select() const;

  bool is_partial_index_select() const;

 private:
  static const QLTypePtr& DefaultQLType();

  // Context that owns this SemState.
  SemContext *sem_context_;

  // The current dml statement being processed.
  PTDmlStmt *current_dml_stmt_ = nullptr;

  // Save the previous state to reset when done.
  SemState *previous_state_ = nullptr;
  bool was_reset = false;

  // States to process an expression node.
  std::shared_ptr<QLType> expected_ql_type_; // The expected sql type of an expression.
  NullIsAllowed allow_null_ql_type_;         // Does the expected type accept NULL?
  InternalType expected_internal_type_;      // The expected internal type of an expression.

  // For reasons such as backwards compatibility, we might want to allow multiple bindvar names for
  // certain cases. The bindvar names present in alternative_bindvar_names_ can also be used when
  // directly binding by name in the query. Note that the alternate names can only be supported for
  // directing bindings since drivers have validations in place which prevents supporting multiple
  // names for PreparedStatements.
  MCSharedPtr<MCString> bindvar_name_ = nullptr;
  MCSharedPtr<MCVector<MCSharedPtr<MCString>>> alternative_bindvar_names_;

  // State variables for index analysis.
  SelectScanInfo *scan_state_ = nullptr;

  // State variable for analyzing key condition.
  // - If select-scan is using nested INDEX key, primary key condition should be dropped off, and
  //   this flag should be set to true. Error on primary key condition should be ignored because
  //   the secondary index key is used in place of primary key.
  // - Currently, this flag is only used for query that has nested INDEX query. YugaByte does not
  //   have any other kinds of nested query
  bool void_primary_key_condition_ = false;

  // State variables for where expression.
  WhereExprState *where_state_ = nullptr;

  // State variables for if expression.
  IfExprState *if_state_ = nullptr;

  // State variables for orderby expression.
  bool validate_orderby_expr_ = false;

  // State variables for index predicate expression i.e., where clause in case of partial indexes.
  IdxPredicateState *idx_predicate_state_ = nullptr;

  // Predicate for selecting data from an index instead of a user table.
  bool selecting_from_index_ = false;

  // Length of prefix of cols in index table that have a sub-clause in WHERE with =/IN operator.
  size_t index_select_prefix_length_ = 0;

  // Predicate for processing a column definition in a table.
  bool processing_column_definition_ = false;

  // Descriptor for the LHS column.
  const ColumnDesc *lhs_col_ = nullptr;

  // State variables for if clause.
  bool processing_if_clause_ = false;

  // State variable for set clause.
  bool processing_set_clause_ = false;

  // State variable for assignee.
  bool processing_assignee_ = false;

  // State variable for aggregate function.
  bool allowing_aggregate_ = false;

  // State variable for allowing column references.
  bool allowing_column_refs_ = false;

  // State variable for processing index column.
  PTColumnDefinition *index_column_ = nullptr;
};

}  // namespace ql
}  // namespace yb
