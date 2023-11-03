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

#pragma once

#include "yb/client/client_fwd.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/ql_datatype.h"

#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/ptree/process_context.h"

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
  SemContext(ParseTreePtr parse_tree, QLEnv *ql_env);
  virtual ~SemContext();

  // Memory pool for semantic analysis of the parse tree of a statement.
  MemoryContext *PSemMem() const;

  //------------------------------------------------------------------------------------------------
  // Symbol table support.
  Status MapSymbol(const MCString& name, PTColumnDefinition *entry);
  Status MapSymbol(const MCString& name, PTAlterColumnDefinition *entry);
  Status MapSymbol(const MCString& name, PTCreateTable *entry);
  Status MapSymbol(const MCString& name, ColumnDesc *entry);
  Status MapSymbol(const MCString& name, PTTypeField *entry);

  // Access functions to current processing symbol.
  SymbolEntry *current_processing_id() {
    return &current_processing_id_;
  }
  void set_current_processing_id(const SymbolEntry& new_id) {
    current_processing_id_ = new_id;
  }

  //------------------------------------------------------------------------------------------------
  // Load table schema into symbol table.
  Status LookupTable(const client::YBTableName& name,
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

  PTCreateIndex *current_create_index_stmt();

  void set_current_create_index_stmt(PTCreateIndex *index);

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
  std::shared_ptr<QLType> GetUDType(const std::string &keyspace_name, const std::string &type_name);

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

  std::string CurrentKeyspace() const;

  std::string CurrentRoleName() const;

  // Access function to cache_used.
  bool cache_used() const { return cache_used_; }

  // Acess functions for semantic states.
  SemState *sem_state() const {
    return sem_state_;
  }

  const std::shared_ptr<QLType>& expr_expected_ql_type() const;

  NullIsAllowed expected_ql_type_accepts_null() const;

  InternalType expr_expected_internal_type() const;

  SelectScanInfo *scan_state() const;

  bool void_primary_key_condition() const;

  WhereExprState *where_state() const;

  IfExprState *if_state() const;

  bool validate_orderby_expr() const;

  IdxPredicateState *idx_predicate_state() const;

  bool selecting_from_index() const;

  size_t index_select_prefix_length() const;

  bool processing_column_definition() const;

  const MCSharedPtr<MCString>& bindvar_name() const;

  const MCSharedPtr<MCVector<MCSharedPtr<MCString>>> &alternative_bindvar_names() const;

  const ColumnDesc *hash_col() const;

  const ColumnDesc *lhs_col() const;

  bool processing_set_clause() const;

  bool processing_assignee() const;

  bool processing_if_clause() const;

  bool allowing_aggregate() const;

  bool allowing_column_refs() const;

  void set_sem_state(SemState *new_state, SemState **existing_state_holder) {
    *existing_state_holder = sem_state_;
    sem_state_ = new_state;
  }

  void reset_sem_state(SemState *previous_state) {
    sem_state_ = previous_state;
  }

  PTDmlStmt *current_dml_stmt() const;

  void set_current_dml_stmt(PTDmlStmt *stmt);

  void set_void_primary_key_condition(bool val);

  Status HasKeyspacePermission(const PermissionType permission,
                               const NamespaceName& keyspace_name);

  // Check whether the current role has the specified permission on the keyspace. Returns an
  // UNAUTHORIZED error message if not found.
  Status CheckHasKeyspacePermission(const YBLocation& loc,
                                    const PermissionType permission,
                                    const NamespaceName& keyspace_name);

  // Check whether the current role has the specified permission on the keyspace or table. Returns
  // an UNAUTHORIZED error message if not found.
  Status CheckHasTablePermission(const YBLocation& loc,
                                 const PermissionType permission,
                                 const NamespaceName& keyspace_name,
                                 const TableName& table_name);

  // Convenience method.
  Status CheckHasTablePermission(const YBLocation& loc,
                                 const PermissionType permission,
                                 client::YBTableName table_name);

  // Check whether the current role has the specified permission on the role. Returns an
  // UNAUTHORIZED error message if not found.
  Status CheckHasRolePermission(const YBLocation& loc,
                                const PermissionType permission,
                                const RoleName& role_name);

  // Check whether the current role has the specified permission on 'ALL KEYSPACES'.
  Status CheckHasAllKeyspacesPermission(const YBLocation& loc,
                                        const PermissionType permission);

  // Check whether the current role has the specified permission on 'ALL ROLES'.
  Status CheckHasAllRolesPermission(const YBLocation& loc,
                                    const PermissionType permission);

  bool IsUncoveredIndexSelect() const;

  bool IsPartialIndexSelect() const;

 private:
  Status LoadSchema(const std::shared_ptr<client::YBTable>& table,
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
