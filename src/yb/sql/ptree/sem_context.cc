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

#include "yb/sql/util/sql_env.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using std::shared_ptr;
using client::YBTable;
using client::YBTableName;
using client::YBColumnSchema;
using client::YBSchema;

//--------------------------------------------------------------------------------------------------

SemContext::SemContext(const char *sql_stmt,
                       size_t stmt_len,
                       ParseTree::UniPtr parse_tree,
                       SqlEnv *sql_env)
    : ProcessContext(sql_stmt, stmt_len, std::move(parse_tree)),
      symtab_(PTempMem()),
      sql_env_(sql_env),
      cache_used_(false),
      current_table_(nullptr),
      sem_state_(nullptr) {
}

SemContext::~SemContext() {
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS SemContext::LookupTable(YBTableName name, shared_ptr<YBTable>* table,
                                       MCVector<ColumnDesc>* table_columns,
                                       int* num_key_columns, int* num_hash_key_columns,
                                       bool* is_system, bool write_only, const YBLocation& loc) {
  if (!name.has_namespace()) {
    if (CurrentKeyspace().empty()) {
      return Error(loc, ErrorCode::NO_NAMESPACE_USED);
    }

    name.set_namespace_name(CurrentKeyspace());
  }

  *is_system = name.is_system();
  if (*is_system && write_only && client::FLAGS_yb_system_namespace_readonly) {
    return Error(loc, ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }

  VLOG(3) << "Loading table descriptor for " << name.ToString();
  *table = GetTableDesc(name);
  if (*table == nullptr) {
    return Error(loc, ErrorCode::TABLE_NOT_FOUND);
  }
  set_current_table(*table);

  const YBSchema& schema = (*table)->schema();
  const int num_columns = schema.num_columns();
  *num_key_columns = schema.num_key_columns();
  *num_hash_key_columns = schema.num_hash_key_columns();

  table_columns->resize(num_columns);
  for (int idx = 0; idx < num_columns; idx++) {
    // Find the column descriptor.
    const YBColumnSchema col = schema.Column(idx);
    (*table_columns)[idx].Init(idx,
                             schema.ColumnId(idx),
                             idx < *num_hash_key_columns,
                             idx < *num_key_columns,
                             col.is_static(),
                             col.is_counter(),
                             col.type(),
                             YBColumnSchema::ToInternalDataType(col.type()));

    // Insert the column descriptor to symbol table.
    MCString col_name(col.name().c_str(), col.name().size(), PTreeMem());
    RETURN_NOT_OK(MapSymbol(col_name, &(*table_columns)[idx]));
  }

  return Status::OK();

}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, PTColumnDefinition *entry) {
  if (symtab_[name].column_ != nullptr) {
    RETURN_NOT_OK(Error(entry, ErrorCode::DUPLICATE_COLUMN));
  }
  symtab_[name].column_ = entry;
  return Status::OK();
}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, PTAlterColumnDefinition *entry) {
  if (symtab_[name].alter_column_ != nullptr) {
    RETURN_NOT_OK(Error(entry, ErrorCode::DUPLICATE_COLUMN));
  }
  symtab_[name].alter_column_ = entry;
  return Status::OK();
}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, PTCreateTable *entry) {
  if (symtab_[name].create_table_ != nullptr) {
    RETURN_NOT_OK(Error(entry, ErrorCode::DUPLICATE_TABLE));
  }
  symtab_[name].create_table_ = entry;
  return Status::OK();
}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, ColumnDesc *entry) {
  if (symtab_[name].column_desc_ != nullptr) {
    LOG(FATAL) << "Entries of the same symbol are inserted"
               << ", Existing entry = " << symtab_[name].column_desc_
               << ", New entry = " << entry;
  }
  symtab_[name].column_desc_ = entry;
  return Status::OK();
}

CHECKED_STATUS SemContext::MapSymbol(const MCString& name, PTTypeField *entry) {
  if (symtab_[name].type_field_ != nullptr) {
    RETURN_NOT_OK(Error(entry, ErrorCode::DUPLICATE_TYPE_FIELD));
  }
  symtab_[name].type_field_ = entry;
  return Status::OK();
}

shared_ptr<YBTable> SemContext::GetTableDesc(const client::YBTableName& table_name) {
  bool cache_used = false;
  shared_ptr<YBTable> table = sql_env_->GetTableDesc(table_name, &cache_used);
  if (table != nullptr) {
    parse_tree_->AddAnalyzedTable(table_name);
    if (cache_used) {
      // Remember cache was used.
      cache_used_ = true;
    }
  }
  return table;
}

std::shared_ptr<YQLType> SemContext::GetUDType(const string &keyspace_name,
                                               const string &type_name) {
  bool cache_used = false;
  shared_ptr<YQLType> type = sql_env_->GetUDType(keyspace_name, type_name, &cache_used);

  if (type != nullptr) {
    parse_tree_->AddAnalyzedUDType(keyspace_name, type_name);
    if (cache_used) {
      // Remember cache was used.
      cache_used_ = true;
    }
  }

  return type;
}

SymbolEntry *SemContext::SeekSymbol(const MCString& name) {
  MCMap<MCString, SymbolEntry>::iterator iter = symtab_.find(name);
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

const ColumnDesc *SemContext::GetColumnDesc(const MCString& col_name, bool reading_column) {
  SymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }

  // To indicate that DocDB must read a columm value to execute an expression, the column is added
  // to the column_refs list.
  if (reading_column) {
    // TODO(neil) Currently AddColumnRef() relies on MCSet datatype to guarantee that we have a
    // unique list of IDs, but we should take advantage to "symbol table" when collecting data
    // for execution. Symbol table and "column_read_count_" need to be corrected so that we can
    // use MCList instead.

    // Indicate that this column must be read for the statement execution.
    current_dml_stmt_->AddColumnRef(*entry->column_desc_);
  }

  return entry->column_desc_;
}

//--------------------------------------------------------------------------------------------------

bool SemContext::IsConvertible(const std::shared_ptr<YQLType>& lhs_type,
                               const std::shared_ptr<YQLType>& rhs_type) const {
  return YQLType::IsImplicitlyConvertible(lhs_type, rhs_type);
}

bool SemContext::IsComparable(DataType lhs_type, DataType rhs_type) const {
  return YQLType::IsComparable(lhs_type, rhs_type);
}

}  // namespace sql
}  // namespace yb
