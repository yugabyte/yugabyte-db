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
// Treenode definitions for CREATE TABLE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_tcreate_table.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

using std::shared_ptr;
using std::to_string;
using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

PgTCreateTable::PgTCreateTable(MemoryContext *memctx,
                               PgTLocation::SharedPtr loc,
                               const PgTQualifiedName::SharedPtr& name,
                               const PTListNode::SharedPtr& elements,
                               bool create_if_not_exists)
    : TreeNode(memctx, loc),
      relation_(name),
      elements_(elements),
      columns_(memctx),
      primary_columns_(memctx),
      hash_columns_(memctx),
      create_if_not_exists_(create_if_not_exists) {
  // For each table, Postgresql creates a few system columns. We'll implement two of those columns
  // here at the compilation level.
  // 1. OID column
  //    PostgreSQL defines OID column as the logical identifier of a row. At this moment, in our
  //    system, this column will have following specifications.
  //    - Use BIGINT datatype.
  //    - Define it as PARTITION KEY.
  //    - For now, insert '0' to "OID" for all rows, so all PosgreSQL data is in the same tablet.
  //
  // 2. CTID column
  //    PostgreSQL defines CTID column as the physical identifier or location of a row. In our
  //    system it is the primary key for table that don't have primary key.
  //    - Use TIMEUUID datatype.
  //    - It is defined as PRIMARY KEY, which is used to locate a row in DocDB.
  MCSharedPtr<MCString> colid = MCMakeShared<MCString>(memctx, "oid");
  PgTBaseType::SharedPtr coltype = PgTBigInt::MakeShared(memctx, loc);
  col_oid_ = PgTColumnDefinition::MakeShared(memctx, loc, colid, coltype, nullptr);

  colid = MCMakeShared<MCString>(memctx, "ctid");
  coltype = PgTTimeUuid::MakeShared(memctx, loc);
  col_ctid_ = PgTColumnDefinition::MakeShared(memctx, loc, colid, coltype, nullptr);
}

PgTCreateTable::~PgTCreateTable() {
}

CHECKED_STATUS PgTCreateTable::Analyze(PgCompileContext *compile_context) {
  PgSemState sem_state(compile_context);

  // Processing table name.
  relation_->set_object_type(OBJECT_TABLE);
  RETURN_NOT_OK(relation_->Analyze(compile_context));

  // Save context state, and set "this" as current column in the context.
  PgSymbolEntry cached_entry = *compile_context->current_processing_symbol();
  compile_context->set_current_create_table_stmt(this);

  // Processing table elements.
  // - First, process all column definitions to collect all symbols.
  // - Process all other elements afterward.
  sem_state.set_processing_column_definition(true);
  RETURN_NOT_OK(elements_->Analyze(compile_context));
  sem_state.set_processing_column_definition(false);
  RETURN_NOT_OK(elements_->Analyze(compile_context));

  // Postgresql does not have partition column, but it does have ROWID.
  // Define rowid as hash column. This is necessary when we do scaning across all servers.
  DCHECK(hash_columns_.empty()) << "Postgresql does not have partition column";

  // Column OID: Add "OID" directly to the hash/partition column list without processing.
  //
  // TODO(neil) Need to discuss about hashing and rowid type before it is implemented. Currently,
  // builtin column "OID" is used as a hack. We'll always insert value 0 to "OID" for all rows, so
  // its value doesn't affect the actual primary key.
  int32_t order = 0;
  col_oid_->set_is_hash_key();
  col_oid_->set_order(order++);
  hash_columns_.push_back(col_oid_.get());

  // Column CTID: Set its order before other columns.
  col_ctid_->set_order(order++);

  // Move the all partition and primary key columns from columns list to appropriate list.
  // - Set order number for builtin-columns. Currently, there's only col_oid.
  // - Remove primary columns from regular column list.
  MCList<PgTColumnDefinition *>::iterator iter = columns_.begin();
  while (iter != columns_.end()) {
    PgTColumnDefinition *coldef = *iter;
    coldef->set_order(order++);

    // Remove a column from regular column list if it's already in hash or primary list.
    if (coldef->is_hash_key()) {
      iter = columns_.erase(iter);
    } else if (coldef->is_primary_key()) {
      iter = columns_.erase(iter);
    } else {
      iter++;
    }
  }

  // Column CTID: Use "ctid" as primary columns if primary_columns_ is empty.
  if (primary_columns_.empty()) {
    col_ctid_->set_is_primary_key();
    primary_columns_.push_back(col_ctid_.get());
  } else {
    columns_.push_front(col_ctid_.get());
  }

  // Restore the context value as we are done with this table.
  compile_context->set_current_processing_symbol(cached_entry);
  return Status::OK();
}

CHECKED_STATUS PgTCreateTable::AppendColumn(PgCompileContext *compile_context,
                                            PgTColumnDefinition *column) {
  RETURN_NOT_OK(CheckType(compile_context, column->datatype()));
  columns_.push_back(column);
  return Status::OK();
}

CHECKED_STATUS PgTCreateTable::AppendPrimaryColumn(PgCompileContext *compile_context,
                                                   PgTColumnDefinition *column) {
  RETURN_NOT_OK(CheckPrimaryType(compile_context, column->datatype()));
  column->set_is_primary_key();
  primary_columns_.push_back(column);
  return Status::OK();
}

CHECKED_STATUS PgTCreateTable::CheckPrimaryType(PgCompileContext *compile_context,
                                               const PgTBaseType::SharedPtr& datatype) {
  RETURN_NOT_OK(CheckType(compile_context, datatype));
  if (!QLType::IsValidPrimaryType(datatype->ql_type()->main())) {
    return compile_context->Error(datatype, ErrorCode::INVALID_PRIMARY_COLUMN_TYPE);
  }
  return Status::OK();
}

CHECKED_STATUS PgTCreateTable::CheckType(PgCompileContext *compile_context,
                                         const PgTBaseType::SharedPtr& datatype) {
  // Although simple datatypes don't need further checking, complex datatypes such as collections,
  // tuples, and user-defined datatypes need to be analyzed because they have members.
  RETURN_NOT_OK(datatype->Analyze(compile_context));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgTColumnDefinition::PgTColumnDefinition(MemoryContext *memctx,
                                         PgTLocation::SharedPtr loc,
                                         const MCSharedPtr<MCString>& name,
                                         const PgTBaseType::SharedPtr& datatype,
                                         const PTListNode::SharedPtr& qualifiers)
    : TreeNode(memctx, loc),
      name_(name),
      datatype_(datatype),
      qualifiers_(qualifiers),
      is_primary_key_(false),
      is_hash_key_(false),
      order_(-1),
      sorting_type_(ColumnSchema::SortingType::kNotSpecified) {
}

PgTColumnDefinition::~PgTColumnDefinition() {
}

CHECKED_STATUS PgTColumnDefinition::Analyze(PgCompileContext *compile_context) {
  if (!compile_context->processing_column_definition()) {
    return Status::OK();
  }

  // Save context state, and set "this" as current column in the context.
  PgSymbolEntry cached_entry = *compile_context->current_processing_symbol();
  compile_context->set_current_column(this);

  // Analyze column qualifiers.
  if (!PgTName::IsLegalName(name_->c_str())) {
    return compile_context->Error(this, ErrorCode::ILLEGAL_NAME);
  }
  RETURN_NOT_OK(compile_context->MapSymbol(*name_, this));
  if (qualifiers_ != nullptr) {
    RETURN_NOT_OK(qualifiers_->Analyze(compile_context));
  }

  // Add the analyzed column to table.
  PgTCreateTable *table = compile_context->current_create_table_stmt();
  RETURN_NOT_OK(table->AppendColumn(compile_context, this));

  // Restore the context value as we are done with this colummn.
  compile_context->set_current_processing_symbol(cached_entry);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgTPrimaryKey::PgTPrimaryKey(MemoryContext *memctx,
                           PgTLocation::SharedPtr loc,
                           const PTListNode::SharedPtr& columns)
    : PgTConstraint(memctx, loc), columns_(columns) {
}

PgTPrimaryKey::~PgTPrimaryKey() {
}

CHECKED_STATUS PgTPrimaryKey::Analyze(PgCompileContext *compile_context) {
  if (compile_context->processing_column_definition() != is_column_element()) {
    return Status::OK();
  }

  // Check if primary key is defined more than one time.
  PgTCreateTable *table = compile_context->current_create_table_stmt();
  if (table->primary_columns().size() > 0) {
     return compile_context->Error(this, "Too many primary key",
                                  ErrorCode::INVALID_TABLE_DEFINITION);
  }

  if (is_column_element()) {
    // Decorate the current processing name node as this is a column constraint.
    PgTColumnDefinition *column = compile_context->current_column();
    return table->AppendPrimaryColumn(compile_context, column);

  }

  // Decorate all name node of this key as this is a table constraint.
  TreeNodeOperator<PgCompileContext, PgTName> func = &PgTName::SetupPrimaryKey;
  TreeNodeOperator<PgCompileContext, PgTName> nested_func = &PgTName::SetupPrimaryKey;
  return columns_->Apply<PgCompileContext, PgTName>(compile_context, func, 1, 1, nested_func);
}

}  // namespace pgsql
}  // namespace yb
