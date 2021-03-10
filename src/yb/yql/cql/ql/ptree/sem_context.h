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
// Entry point for the semantic analytical process.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_SEM_CONTEXT_H_
#define YB_YQL_CQL_QL_PTREE_SEM_CONTEXT_H_

#include "yb/yql/cql/ql/util/ql_env.h"
#include "yb/yql/cql/ql/ptree/process_context.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_alter_table.h"
#include "yb/yql/cql/ql/ptree/pt_create_type.h"
#include "yb/yql/cql/ql/ptree/pt_create_index.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

struct SymbolEntry {
  // Parse tree node for column. It's used for table creation.
  PTColumnDefinition *column_ = nullptr;

  // Parse tree node for column alterations.
  PTAlterColumnDefinition *alter_column_ = nullptr;

  // Parse tree node for table. It's used for table creation.
  PTCreateTable *create_table_ = nullptr;
  PTAlterTable *alter_table_ = nullptr;

  // Parser tree node for user-defined type. It's used for creating types.
  PTCreateType *create_type_ = nullptr;
  PTTypeField *type_field_ = nullptr;

  // Column description. It's used for DML statements including select.
  // Not part of a parse tree, but it is allocated within the parse tree pool because it is
  // persistent metadata. It represents a column during semantic and execution phases.
  //
  // TODO(neil) Add "column_read_count_" and potentially "column_write_count_" and use them for
  // error check wherever needed. Symbol tables and entries are destroyed after compilation, so
  // data that are used during compilation but not execution should be declared here.
  ColumnDesc *column_desc_ = nullptr;
};

//--------------------------------------------------------------------------------------------------

class SemContext : public ProcessContext {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::unique_ptr<SemContext> UniPtr;
  typedef std::unique_ptr<const SemContext> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  SemContext(ParseTree::UniPtr parse_tree, QLEnv *ql_env);
  virtual ~SemContext();

  // Memory pool for semantic analysis of the parse tree of a statement.
  MemoryContext *PSemMem() const {
    return parse_tree_->PSemMem();
  }

  //------------------------------------------------------------------------------------------------
  // Symbol table support.
  CHECKED_STATUS MapSymbol(const MCString& name, PTColumnDefinition *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, PTAlterColumnDefinition *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, PTCreateTable *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, ColumnDesc *entry);
  CHECKED_STATUS MapSymbol(const MCString& name, PTTypeField *entry);

  // Access functions to current processing symbol.
  SymbolEntry *current_processing_id() {
    return &current_processing_id_;
  }
  void set_current_processing_id(const SymbolEntry& new_id) {
    current_processing_id_ = new_id;
  }

  //------------------------------------------------------------------------------------------------
  // Load table schema into symbol table.
  CHECKED_STATUS LookupTable(const client::YBTableName& name,
                             const YBLocation& loc,
                             bool write_table,
                             const PermissionType permission_type,
                             std::shared_ptr<client::YBTable>* table,
                             bool* is_system,
                             MCVector<ColumnDesc>* col_descs = nullptr);

  //------------------------------------------------------------------------------------------------
  // Access functions to current processing table and column.
  PTColumnDefinition *current_column() {
    return current_processing_id_.column_;
  }
  void set_current_column(PTColumnDefinition *column) {
    current_processing_id_.column_ = column;
  }

  PTCreateTable *current_create_table_stmt() {
    return current_processing_id_.create_table_;
  }

  void set_current_create_table_stmt(PTCreateTable *table) {
    current_processing_id_.create_table_ = table;
  }

  PTCreateIndex *current_create_index_stmt() {
    PTCreateTable* const table = current_create_table_stmt();
    return (table != nullptr && table->opcode() == TreeNodeOpcode::kPTCreateIndex)
        ? static_cast<PTCreateIndex*>(table) : nullptr;
  }

  void set_current_create_index_stmt(PTCreateIndex *index) {
    set_current_create_table_stmt(index);
  }

  PTAlterTable *current_alter_table() {
    return current_processing_id_.alter_table_;
  }

  void set_current_alter_table(PTAlterTable *table) {
    current_processing_id_.alter_table_ = table;
  }

  PTCreateType *current_create_type_stmt() {
    return current_processing_id_.create_type_;
  }

  void set_current_create_type_stmt(PTCreateType *type) {
    current_processing_id_.create_type_ = type;
  }

  PTTypeField *current_type_field() {
    return current_processing_id_.type_field_;
  }

  void set_current_type_field(PTTypeField *field) {
    current_processing_id_.type_field_ = field;
  }

  // Find table descriptor from metadata server.
  std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name);

  // Find table descriptor from metadata server.
  std::shared_ptr<client::YBTable> GetTableDesc(const TableId& table_id);

  // Get (user-defined) type from metadata server.
  std::shared_ptr<QLType> GetUDType(const string &keyspace_name, const string &type_name);

  // Find column descriptor from symbol table.
  PTColumnDefinition *GetColumnDefinition(const MCString& col_name);

  // Find column descriptor from symbol table. From the context, the column value will be marked to
  // be read if necessary when executing the QL statement.
  const ColumnDesc *GetColumnDesc(const MCString& col_name) const;

  // Check if the lhs_type is convertible to rhs_type.
  bool IsConvertible(const std::shared_ptr<QLType>& lhs_type,
                     const std::shared_ptr<QLType>& rhs_type) const;

  // Check if two types are comparable -- parametric types are never comparable so we only take
  // DataType not QLType as arguments
  bool IsComparable(DataType lhs_type, DataType rhs_type) const;

  std::string CurrentKeyspace() const {
    return ql_env_->CurrentKeyspace();
  }

  std::string CurrentRoleName() const {
    return ql_env_->CurrentRoleName();
  }

  // Access function to cache_used.
  bool cache_used() const { return cache_used_; }

  // Acess functions for semantic states.
  SemState *sem_state() const {
    return sem_state_;
  }

  const std::shared_ptr<QLType>& expr_expected_ql_type() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->expected_ql_type();
  }

  InternalType expr_expected_internal_type() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->expected_internal_type();
  }

  SelectScanInfo *scan_state() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->scan_state();
  }

  bool void_primary_key_condition() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->void_primary_key_condition();
  }

  WhereExprState *where_state() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->where_state();
  }

  IfExprState *if_state() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->if_state();
  }

  bool validate_orderby_expr() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->validate_orderby_expr();
  }

  bool selecting_from_index() const {
    DCHECK(sem_state_) << "State variable is not set";
    return sem_state_->selecting_from_index();
  }

  bool processing_column_definition() const {
    DCHECK(sem_state_) << "State variable is not set";
    return sem_state_->processing_column_definition();
  }

  const MCSharedPtr<MCString>& bindvar_name() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->bindvar_name();
  }

  const ColumnDesc *hash_col() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->hash_col();
  }

  const ColumnDesc *lhs_col() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->lhs_col();
  }

  bool processing_set_clause() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->processing_set_clause();
  }

  bool processing_assignee() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->processing_assignee();
  }

  bool processing_if_clause() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->processing_if_clause();
  }

  bool allowing_aggregate() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->allowing_aggregate();
  }

  bool allowing_column_refs() const {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    return sem_state_->allowing_column_refs();
  }

  void set_sem_state(SemState *new_state, SemState **existing_state_holder) {
    *existing_state_holder = sem_state_;
    sem_state_ = new_state;
  }

  void reset_sem_state(SemState *previous_state) {
    sem_state_ = previous_state;
  }

  PTDmlStmt *current_dml_stmt() const {
    return sem_state_->current_dml_stmt();
  }

  void set_current_dml_stmt(PTDmlStmt *stmt) {
    sem_state_->set_current_dml_stmt(stmt);
  }

  void set_void_primary_key_condition(bool val) {
    DCHECK(sem_state_) << "State variable is not set for the expression";
    sem_state_->set_void_primary_key_condition(val);
  }

  CHECKED_STATUS HasKeyspacePermission(const PermissionType permission,
                                       const NamespaceName& keyspace_name);

  // Check whether the current role has the specified permission on the keyspace. Returns an
  // UNAUTHORIZED error message if not found.
  CHECKED_STATUS CheckHasKeyspacePermission(const YBLocation& loc,
                                            const PermissionType permission,
                                            const NamespaceName& keyspace_name);

  // Check whether the current role has the specified permission on the keyspace or table. Returns
  // an UNAUTHORIZED error message if not found.
  CHECKED_STATUS CheckHasTablePermission(const YBLocation& loc,
                                         const PermissionType permission,
                                         const NamespaceName& keyspace_name,
                                         const TableName& table_name);

  // Convenience method.
  CHECKED_STATUS CheckHasTablePermission(const YBLocation& loc,
                                         const PermissionType permission,
                                         client::YBTableName table_name);

  // Check whether the current role has the specified permission on the role. Returns an
  // UNAUTHORIZED error message if not found.
  CHECKED_STATUS CheckHasRolePermission(const YBLocation& loc,
                                        const PermissionType permission,
                                        const RoleName& role_name);

  // Check whether the current role has the specified permission on 'ALL KEYSPACES'.
  CHECKED_STATUS CheckHasAllKeyspacesPermission(const YBLocation& loc,
                                                const PermissionType permission);

  // Check whether the current role has the specified permission on 'ALL ROLES'.
  CHECKED_STATUS CheckHasAllRolesPermission(const YBLocation& loc,
                                            const PermissionType permission);

  bool IsUncoveredIndexSelect() const {
    return sem_state_->is_uncovered_index_select();
  }

 private:
  CHECKED_STATUS LoadSchema(const std::shared_ptr<client::YBTable>& table,
                            MCVector<ColumnDesc>* col_descs = nullptr);

  // Find symbol.
  const SymbolEntry *SeekSymbol(const MCString& name) const;

  // Symbol table.
  MCMap<MCString, SymbolEntry> symtab_;

  // Current processing symbol.
  SymbolEntry current_processing_id_;

  // Session.
  QLEnv *ql_env_;

  // Is metadata cache used?
  bool cache_used_ = false;

  // sem_state_ consists of state variables that are used to process one tree node. It is generally
  // set and reset at the beginning and end of the semantic analysis of one treenode.
  SemState *sem_state_ = nullptr;
};

}  // namespace ql
}  // namespace yb
#endif  // YB_YQL_CQL_QL_PTREE_SEM_CONTEXT_H_
