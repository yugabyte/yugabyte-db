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

#include "yb/yql/pgsql/ptree/pg_compile_context.h"

#include <iostream>

#include "yb/client/client.h"
#include "yb/yql/pgsql/util/pg_env.h"

namespace yb {
namespace pgsql {

using std::endl;
using std::istream;
using std::min;
using std::shared_ptr;
using std::string;

using client::YBTable;
using client::YBTableName;
using client::YBTableType;
using client::YBColumnSchema;
using client::YBSchema;

//--------------------------------------------------------------------------------------------------

PgCompileContext::PgCompileContext(const PgEnv::SharedPtr& pg_env,
                                   const PgSession::SharedPtr& pg_session,
                                   const string& stmt)
    : PgProcessContext(stmt.c_str(), stmt.size()),
      pg_env_(pg_env),
      pg_session_(pg_session),
      parse_tree_(std::make_shared<ParseTree>()),
      symtab_(PTempMem()) {
  // The MAC version of FLEX requires empty or valid input stream. It does not allow input file to
  // be nullptr.
  input_file_ = std::unique_ptr<istream>(new istream(nullptr));

  if (VLOG_IS_ON(3)) {
    trace_scanning_ = true;
    trace_parsing_ = true;
  }
}

PgCompileContext::~PgCompileContext() {
}

//--------------------------------------------------------------------------------------------------
// API for parsing phase.
//--------------------------------------------------------------------------------------------------

void PgCompileContext::SaveGeneratedParseTree(TreeNode::SharedPtr generated_parse_tree) {
  CHECK(parse_tree_.get() != nullptr) << "Context is not associated with a parse tree";
  parse_tree_->set_root(generated_parse_tree);
}

size_t PgCompileContext::Read(char* buf, size_t max_size) {
  const size_t copy_size = min<size_t>(stmt_len_ - stmt_offset_, max_size);
  if (copy_size > 0) {
    memcpy(buf, stmt_ + stmt_offset_, copy_size);
    stmt_offset_ += copy_size;
    return copy_size;
  }
  return 0;
}

//--------------------------------------------------------------------------------------------------
// API for semantic phase.
//--------------------------------------------------------------------------------------------------

Status PgCompileContext::LookupTable(
    const YBTableName& name,
    const PgTLocation& loc,
    const bool write_table,
    shared_ptr<YBTable>* table,
    bool* is_system,
    MCVector<ColumnDesc>* col_descs,
    int* num_key_columns,
    int* num_partition_columns) {

  shared_ptr<YBTable> pg_table;
  *table = nullptr;
  *is_system = name.is_system();
  if (*is_system && write_table && client::FLAGS_yb_system_namespace_readonly) {
    return Error(loc, ErrorCode::SYSTEM_NAMESPACE_READONLY);
  }
  LOG(INFO) << "Loading table descriptor for " << name.ToString();

  VLOG(3) << "Loading table descriptor for " << name.ToString();
  pg_table = pg_env_->GetTableDesc(name);
  if (pg_table == nullptr) {
    return Error(loc, ErrorCode::TABLE_NOT_FOUND);
  }
  if (pg_table->table_type() != YBTableType::PGSQL_TABLE_TYPE) {
    return Error(loc, "Cannot access non-postgres table", ErrorCode::INVALID_TABLE_TYPE);
  }
  set_current_table(pg_table);

  const YBSchema& schema = pg_table->schema();
  const int num_columns = schema.num_columns();
  if (num_key_columns != nullptr) {
    *num_key_columns = schema.num_key_columns();
  }
  if (num_partition_columns != nullptr) {
    *num_partition_columns = schema.num_hash_key_columns();
  }

  if (col_descs != nullptr) {
    col_descs->resize(num_columns);
    for (int idx = 0; idx < num_columns; idx++) {
      // Find the column descriptor.
      const YBColumnSchema col = schema.Column(idx);
      (*col_descs)[idx].Init(idx,
                             schema.ColumnId(idx),
                             col.name(),
                             idx < *num_partition_columns,
                             idx < *num_key_columns,
                             col.order(),
                             col.type(),
                             YBColumnSchema::ToInternalDataType(col.type()));

      // Insert the column descriptor, and column definition if requested, to symbol table.
      MCSharedPtr<MCString> col_name = MCMakeShared<MCString>(PSemMem(), col.name().c_str());
      RETURN_NOT_OK(MapSymbol(*col_name, &(*col_descs)[idx]));
    }
  }

  *table = pg_table;
  return Status::OK();
}

Status PgCompileContext::MapSymbol(const MCString& name, PgTColumnDefinition *entry) {
  if (symtab_[name].column_ != nullptr) {
    return Error(entry, ErrorCode::DUPLICATE_COLUMN);
  }
  symtab_[name].column_ = entry;
  return Status::OK();
}

Status PgCompileContext::MapSymbol(const MCString& name, PgTCreateTable *entry) {
  if (symtab_[name].create_table_ != nullptr) {
    return Error(entry, ErrorCode::DUPLICATE_TABLE);
  }
  symtab_[name].create_table_ = entry;
  return Status::OK();
}

Status PgCompileContext::MapSymbol(const MCString& name, ColumnDesc *entry) {
  if (symtab_[name].column_desc_ != nullptr) {
    LOG(FATAL) << "Entries of the same symbol are inserted"
               << ", Existing entry = " << symtab_[name].column_desc_
               << ", New entry = " << entry;
  }
  symtab_[name].column_desc_ = entry;
  return Status::OK();
}

PgSymbolEntry *PgCompileContext::SeekSymbol(const MCString& name) {
  MCMap<MCString, PgSymbolEntry>::iterator iter = symtab_.find(name);
  if (iter != symtab_.end()) {
    return &iter->second;
  }
  return nullptr;
}

PgTColumnDefinition *PgCompileContext::GetColumnDefinition(const MCString& col_name) {
  const PgSymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }
  return entry->column_;
}

const ColumnDesc *PgCompileContext::GetColumnDesc(const MCString& col_name) {
  PgSymbolEntry * entry = SeekSymbol(col_name);
  if (entry == nullptr) {
    return nullptr;
  }

  if (current_dml_stmt_ != nullptr) {
    // To indicate that DocDB must read a columm value to execute an expression, the column is added
    // to the column_refs list.
    bool reading_column = false;

    switch (current_dml_stmt_->opcode()) {
      case TreeNodeOpcode::kPgTSelectStmt:
        reading_column = true;
        break;
      case TreeNodeOpcode::kPgTUpdateStmt:
        if (sem_state() != nullptr && processing_set_clause() && !processing_assignee()) {
          reading_column = true;
          break;
        }
        break;
      case TreeNodeOpcode::kPgTInsertStmt:
        break;
      case TreeNodeOpcode::kPgTDeleteStmt:
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
      current_dml_stmt_->AddColumnRef(*entry->column_desc_);
    }
  }

  return entry->column_desc_;
}

//--------------------------------------------------------------------------------------------------

bool PgCompileContext::IsConvertible(const std::shared_ptr<QLType>& lhs_type,
                                     const std::shared_ptr<QLType>& rhs_type) const {
  return QLType::IsImplicitlyConvertible(lhs_type, rhs_type);
}

bool PgCompileContext::IsComparable(DataType lhs_type, DataType rhs_type) const {
  return QLType::IsComparable(lhs_type, rhs_type);
}

}  // namespace pgsql
}  // namespace yb
