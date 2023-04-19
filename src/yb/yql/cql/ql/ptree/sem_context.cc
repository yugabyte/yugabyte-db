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
#include "yb/yql/cql/ql/ptree/sem_context.h"

#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/common/roles_permissions.h"
#include "yb/common/schema.h"
#include "yb/util/flags.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/pt_alter_table.h"
#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/pt_create_index.h"
#include "yb/yql/cql/ql/ptree/pt_create_type.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/ql_env.h"

DECLARE_bool(use_cassandra_authentication);

DEFINE_UNKNOWN_bool(allow_index_table_read_write, false,
    "Allow direct read and write of index tables");
TAG_FLAG(allow_index_table_read_write, hidden);

namespace yb {
namespace ql {

using std::shared_ptr;
using std::string;
using client::YBTable;
using client::YBTableName;
using client::YBColumnSchema;
using client::YBSchema;

//--------------------------------------------------------------------------------------------------

SemContext::SemContext(ParseTreePtr parse_tree, QLEnv *ql_env)
    : ProcessContext(std::move(parse_tree)),
      symtab_(PTempMem()),
      ql_env_(ql_env) {
}

SemContext::~SemContext() {
}

//--------------------------------------------------------------------------------------------------

MemoryContext *SemContext::PSemMem() const {
  return parse_tree_->PSemMem();
}

PTCreateIndex *SemContext::current_create_index_stmt() {
  PTCreateTable* const table = current_create_table_stmt();
  return (table != nullptr && table->opcode() == TreeNodeOpcode::kPTCreateIndex)
      ? static_cast<PTCreateIndex*>(table) : nullptr;
}

void SemContext::set_current_create_index_stmt(PTCreateIndex *index) {
  set_current_create_table_stmt(index);
}

std::string SemContext::CurrentKeyspace() const {
  return ql_env_->CurrentKeyspace();
}

std::string SemContext::CurrentRoleName() const {
  return ql_env_->CurrentRoleName();
}

Status SemContext::LoadSchema(const shared_ptr<YBTable>& table,
                              MCVector<ColumnDesc>* col_descs) {
  const YBSchema& schema = table->schema();
  const auto num_columns = schema.num_columns();
  const auto num_key_columns = schema.num_key_columns();
  const auto num_hash_key_columns = schema.num_hash_key_columns();

  if (col_descs != nullptr) {
    col_descs->reserve(num_columns);
    for (size_t idx = 0; idx < num_columns; idx++) {
      // Find the column descriptor.
      const YBColumnSchema col = schema.Column(idx);
      col_descs->emplace_back(idx,
                              schema.ColumnId(idx),
                              col.name(),
                              idx < num_hash_key_columns,
                              idx < num_key_columns,
                              col.is_static(),
                              col.is_counter(),
                              col.type(),
                              YBColumnSchema::ToInternalDataType(col.type()),
                              schema.table_properties().use_mangled_column_name());

      // Insert the column descriptor, and column definition if requested, to symbol table.
      MCSharedPtr<MCString> col_name = MCMakeShared<MCString>(PSemMem(), col.name().c_str());
      RETURN_NOT_OK(MapSymbol(*col_name, &(*col_descs)[idx]));
    }
  }

  return Status::OK();
}

Status SemContext::LookupTable(const YBTableName& name,
                               const YBLocation& loc,
                               const bool write_table,
                               const PermissionType permission,
                               shared_ptr<YBTable>* table,
                               bool* is_system,
                               MCVector<ColumnDesc>* col_descs) {
  if (FLAGS_use_cassandra_authentication) {
    RETURN_NOT_OK(CheckHasTablePermission(loc, permission, name.namespace_name(),
                                          name.table_name()));
  }
  *is_system = name.is_system();
  if (*is_system && write_table && client::FLAGS_yb_system_namespace_readonly) {
    return Error(loc, ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  VLOG(3) << "Loading table descriptor for " << name.ToString();
  *table = GetTableDesc(name);
  if (*table == nullptr || ((*table)->IsIndex() && !FLAGS_allow_index_table_read_write) ||
      // Only looking for CQL tables.
      (*table)->table_type() != client::YBTableType::YQL_TABLE_TYPE) {
    return Error(loc, ErrorCode::OBJECT_NOT_FOUND);
  }

  return LoadSchema(*table, col_descs);
}

Status SemContext::MapSymbol(const MCString& name, PTColumnDefinition *entry) {
  if (symtab_[name].column_ != nullptr) {
    return Error(entry, ErrorCode::DUPLICATE_COLUMN);
  }
  symtab_[name].column_ = entry;
  return Status::OK();
}

Status SemContext::MapSymbol(const MCString& name, PTAlterColumnDefinition *entry) {
  if (symtab_[name].alter_column_ != nullptr) {
    return Error(entry, ErrorCode::DUPLICATE_COLUMN);
  }
  symtab_[name].alter_column_ = entry;
  return Status::OK();
}

Status SemContext::MapSymbol(const MCString& name, PTCreateTable *entry) {
  if (symtab_[name].create_table_ != nullptr) {
    return Error(entry, ErrorCode::DUPLICATE_OBJECT);
  }
  symtab_[name].create_table_ = entry;
  return Status::OK();
}

Status SemContext::MapSymbol(const MCString& name, ColumnDesc *entry) {
  if (symtab_[name].column_desc_ != nullptr) {
    LOG(FATAL) << "Entries of the same symbol are inserted"
               << ", Existing entry = " << symtab_[name].column_desc_
               << ", New entry = " << entry;
  }
  symtab_[name].column_desc_ = entry;
  return Status::OK();
}

Status SemContext::MapSymbol(const MCString& name, PTTypeField *entry) {
  if (symtab_[name].type_field_ != nullptr) {
    return Error(entry, ErrorCode::DUPLICATE_TYPE_FIELD);
  }
  symtab_[name].type_field_ = entry;
  return Status::OK();
}

shared_ptr<YBTable> SemContext::GetTableDesc(const client::YBTableName& table_name) {
  bool cache_used = false;
  shared_ptr<YBTable> table = ql_env_->GetTableDesc(table_name, &cache_used);
  if (table != nullptr) {
    parse_tree_->AddAnalyzedTable(table_name);
    if (cache_used) {
      // Remember cache was used.
      cache_used_ = true;
    }
  }
  return table;
}

shared_ptr<YBTable> SemContext::GetTableDesc(const TableId& table_id) {
  bool cache_used = false;
  shared_ptr<YBTable> table = ql_env_->GetTableDesc(table_id, &cache_used);
  if (table != nullptr) {
    parse_tree_->AddAnalyzedTable(table->name());
    if (cache_used) {
      // Remember cache was used.
      cache_used_ = true;
    }
  }
  return table;
}

std::shared_ptr<QLType> SemContext::GetUDType(const string &keyspace_name,
                                              const string &type_name) {
  bool cache_used = false;
  shared_ptr<QLType> type = ql_env_->GetUDType(keyspace_name, type_name, &cache_used);

  if (type != nullptr) {
    parse_tree_->AddAnalyzedUDType(keyspace_name, type_name);
    if (cache_used) {
      // Remember cache was used.
      cache_used_ = true;
    }
  }

  return type;
}

const SymbolEntry *SemContext::SeekSymbol(const MCString& name) const {
  MCMap<MCString, SymbolEntry>::const_iterator iter = symtab_.find(name);
  if (iter != symtab_.end()) {
    return &iter->second;
  }
  return nullptr;
}

PTColumnDefinition *SemContext::GetColumnDefinition(const MCString& col_name) {
  const SymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry->column_;
}

const ColumnDesc *SemContext::GetColumnDesc(const MCString& col_name) const {
  const SymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }

  PTDmlStmt *dml_stmt = current_dml_stmt();
  if (dml_stmt != nullptr) {
    // To indicate that DocDB must read a columm value to execute an expression, the column is added
    // to the column_refs list.
    bool reading_column = false;

    switch (dml_stmt->opcode()) {
      case TreeNodeOpcode::kPTSelectStmt:
        reading_column = true;
        break;
      case TreeNodeOpcode::kPTUpdateStmt:
        if (sem_state() != nullptr && processing_set_clause() && !processing_assignee()) {
          reading_column = true;
          break;
        }
        FALLTHROUGH_INTENDED;
      case TreeNodeOpcode::kPTInsertStmt:
      case TreeNodeOpcode::kPTDeleteStmt:
        if (sem_state() != nullptr && processing_if_clause()) {
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
      dml_stmt->AddColumnRef(*entry->column_desc_);
    }
  }

  // Setup the column to which the INDEX column is referencing.
  if (sem_state_ && sem_state_->is_processing_index_column()) {
    sem_state_->add_index_column_ref(entry->column_desc_->id());
  }

  if (sem_state_ && sem_state_->idx_predicate_state()) {
    // We are in CREATE INDEX path of a partial index. Save column ids referenced in the predicate.
    sem_state_->idx_predicate_state()->column_refs()->insert(entry->column_desc_->id());
  }

  return entry->column_desc_;
}

Status SemContext::HasKeyspacePermission(PermissionType permission,
                                         const NamespaceName& keyspace_name) {

  DFATAL_OR_RETURN_ERROR_IF(keyspace_name.empty(),
                            STATUS(InvalidArgument, "Invalid empty keyspace"));
  return ql_env_->HasResourcePermission(get_canonical_keyspace(keyspace_name),
                                        ObjectType::SCHEMA, permission, keyspace_name);
}

Status SemContext::CheckHasKeyspacePermission(const YBLocation& loc,
                                              const PermissionType permission,
                                              const NamespaceName& keyspace_name) {
  auto s = HasKeyspacePermission(permission, keyspace_name);
  if (!s.ok()) {
    return Error(loc, s.message().ToBuffer().c_str(), ErrorCode::UNAUTHORIZED);
  }
  return Status::OK();
}

Status SemContext::CheckHasTablePermission(const YBLocation &loc,
                                           PermissionType permission,
                                           const NamespaceName& keyspace_name,
                                           const TableName& table_name) {
  DFATAL_OR_RETURN_ERROR_IF(keyspace_name.empty(),
                            STATUS_SUBSTITUTE(InvalidArgument, "Empty keyspace for table $0",
                                              table_name));
  DFATAL_OR_RETURN_ERROR_IF(table_name.empty(),
                            STATUS(InvalidArgument, "Table name cannot be empty"));

  auto s = ql_env_->HasTablePermission(keyspace_name, table_name, permission);
  if (!s.ok()) {
    return Error(loc, s.message().ToBuffer().c_str(), ErrorCode::UNAUTHORIZED);
  }
  return Status::OK();
}

Status SemContext::CheckHasTablePermission(const YBLocation& loc,
                                           const PermissionType permission,
                                           client::YBTableName table_name) {
  return CheckHasTablePermission(loc, permission, table_name.namespace_name(),
                                 table_name.table_name());
}

Status SemContext::CheckHasRolePermission(const YBLocation& loc,
                                          PermissionType permission,
                                          const RoleName& role_name) {

  auto s = ql_env_->HasRolePermission(role_name, permission);
  if (!s.ok()) {
    return Error(loc, s.message().ToBuffer().c_str(), ErrorCode::UNAUTHORIZED);
  }
  return Status::OK();
}

Status SemContext::CheckHasAllKeyspacesPermission(const YBLocation& loc,
                                                  PermissionType permission) {

  auto s = ql_env_->HasResourcePermission(kRolesDataResource, ObjectType::SCHEMA,
                                          permission);
  if (!s.ok()) {
    return Error(loc, s.message().ToBuffer().c_str(), ErrorCode::UNAUTHORIZED);
  }
  return Status::OK();
}

Status SemContext::CheckHasAllRolesPermission(const YBLocation& loc,
                                              PermissionType permission) {

  auto s = ql_env_->HasResourcePermission(kRolesRoleResource, ObjectType::ROLE, permission);
  if (!s.ok()) {
    return Error(loc, s.message().ToBuffer().c_str(), ErrorCode::UNAUTHORIZED);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

bool SemContext::IsConvertible(const std::shared_ptr<QLType>& lhs_type,
                               const std::shared_ptr<QLType>& rhs_type) const {
  return QLType::IsImplicitlyConvertible(lhs_type, rhs_type);
}

bool SemContext::IsComparable(DataType lhs_type, DataType rhs_type) const {
  return QLType::IsComparable(lhs_type, rhs_type);
}

const std::shared_ptr<QLType>& SemContext::expr_expected_ql_type() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->expected_ql_type();
}

NullIsAllowed SemContext::expected_ql_type_accepts_null() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->expected_ql_type_accepts_null();
}

InternalType SemContext::expr_expected_internal_type() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->expected_internal_type();
}

SelectScanInfo *SemContext::scan_state() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->scan_state();
}

bool SemContext::void_primary_key_condition() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->void_primary_key_condition();
}

WhereExprState *SemContext::where_state() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->where_state();
}

IfExprState *SemContext::if_state() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->if_state();
}

bool SemContext::validate_orderby_expr() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->validate_orderby_expr();
}

IdxPredicateState *SemContext::idx_predicate_state() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->idx_predicate_state();
}

bool SemContext::selecting_from_index() const {
  DCHECK(sem_state_) << "State variable is not set";
  return sem_state_->selecting_from_index();
}

size_t SemContext::index_select_prefix_length() const {
  DCHECK(sem_state_) << "State variable is not set";
  return sem_state_->index_select_prefix_length();
}

bool SemContext::processing_column_definition() const {
  DCHECK(sem_state_) << "State variable is not set";
  return sem_state_->processing_column_definition();
}

const MCSharedPtr<MCString>& SemContext::bindvar_name() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->bindvar_name();
}

const MCSharedPtr<MCVector<MCSharedPtr<MCString>>>& SemContext::alternative_bindvar_names() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->alternative_bindvar_names();
}

const ColumnDesc *SemContext::hash_col() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->hash_col();
}

const ColumnDesc *SemContext::lhs_col() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->lhs_col();
}

bool SemContext::processing_set_clause() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->processing_set_clause();
}

bool SemContext::processing_assignee() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->processing_assignee();
}

bool SemContext::processing_if_clause() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->processing_if_clause();
}

bool SemContext::allowing_aggregate() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->allowing_aggregate();
}

bool SemContext::allowing_column_refs() const {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  return sem_state_->allowing_column_refs();
}

PTDmlStmt *SemContext::current_dml_stmt() const {
  return sem_state_->current_dml_stmt();
}

void SemContext::set_current_dml_stmt(PTDmlStmt *stmt) {
  sem_state_->set_current_dml_stmt(stmt);
}

void SemContext::set_void_primary_key_condition(bool val) {
  DCHECK(sem_state_) << "State variable is not set for the expression";
  sem_state_->set_void_primary_key_condition(val);
}

bool SemContext::IsUncoveredIndexSelect() const {
  return sem_state_->is_uncovered_index_select();
}

bool SemContext::IsPartialIndexSelect() const {
  return sem_state_->is_partial_index_select();
}

}  // namespace ql
}  // namespace yb
