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
// Column Definition Tree node implementation.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_column_definition.h"

#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_create_index.h"
#include "yb/yql/cql/ql/ptree/pt_create_table.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/util/flags.h"

using std::string;

DEFINE_UNKNOWN_bool(cql_allow_static_column_index, false,
            "Raise unsupported error when creating an index on static columns");

namespace yb {
namespace ql {

PTColumnDefinition::PTColumnDefinition(MemoryContext *memctx,
                                       YBLocationPtr loc,
                                       const MCSharedPtr<MCString>& name,
                                       const PTBaseType::SharedPtr& datatype,
                                       const PTListNode::SharedPtr& qualifiers)
    : TreeNode(memctx, loc),
      name_(name),
      datatype_(datatype),
      qualifiers_(qualifiers),
      is_primary_key_(false),
      is_hash_key_(false),
      is_static_(false),
      order_(-1),
      sorting_type_(SortingType::kNotSpecified),
      coldef_name_(*name) {
}

PTColumnDefinition::~PTColumnDefinition() {
}

Status PTColumnDefinition::Analyze(SemContext *sem_context) {
  // When creating INDEX, this node is not yet defined and processed.
  if (!sem_context->processing_column_definition()) {
    return Status::OK();
  }

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_column(this);

  // Analyze column datatype.
  RETURN_NOT_OK(datatype_->Analyze(sem_context));

  // Analyze column qualifiers (not null, primary key, ...).
  RETURN_NOT_OK(sem_context->MapSymbol(*name_, this));
  if (qualifiers_ != nullptr) {
    RETURN_NOT_OK(qualifiers_->Analyze(sem_context));
  }

  // Add the analyzed column to table.
  PTCreateTable *table = sem_context->current_create_table_stmt();
  RETURN_NOT_OK(table->AppendColumn(sem_context, this));

  // Restore the context value as we are done with this colummn.
  sem_context->set_current_processing_id(cached_entry);

  return Status::OK();
}

void PTColumnDefinition::AddIndexedRef(int32_t col_id) {
  DCHECK(indexed_ref_ == -1 || indexed_ref_ == col_id)
    << "Indexed expression cannot reference more than one column";
  indexed_ref_ = col_id;
}

//--------------------------------------------------------------------------------------------------

PTIndexColumn::PTIndexColumn(MemoryContext *memctx,
                             YBLocationPtr loc,
                             const MCSharedPtr<MCString>& name,
                             const PTExpr::SharedPtr& colexpr)
  : PTColumnDefinition(memctx, loc, name, nullptr, nullptr), colexpr_(colexpr) {
  const string colname = colexpr->MangledName();
  coldef_name_ = colname.c_str();
}

PTIndexColumn::~PTIndexColumn() {
}

Status PTIndexColumn::Analyze(SemContext *sem_context) {
  // Seek the table that is being created currently.
  const PTCreateTable* table = sem_context->current_create_table_stmt();

  // Look for column definition of the given name.
  coldef_ = sem_context->GetColumnDefinition(*name_);

  if (table->opcode() == TreeNodeOpcode::kPTCreateTable) {
    // CREATE TABLE: this object is a column in PRIMARY KEY( index_columns ) clause.
    if (!coldef_) {
      return sem_context->Error(this, "Column does not exist", ErrorCode::UNDEFINED_COLUMN);
    }

    // Check if this column has already been declared as PRIMARY previously.
    if (coldef_->is_primary_key()) {
      return sem_context->Error(this, ErrorCode::DUPLICATE_COLUMN);
    }

    // Only allow column-ref to be used as PRIMARY KEY for a table.
    if (colexpr_->opcode() != TreeNodeOpcode::kPTRef) {
      return sem_context->Error(colexpr_, "PRIMARY KEY element must be a column",
                                ErrorCode::SQL_STATEMENT_INVALID);
    }

    return Status::OK();
  }

  // CREATE INDEX statement processing.
  if (coldef_) {
    // Error: Column is already included in the index.
    return sem_context->Error(this, ErrorCode::DUPLICATE_COLUMN);
  }

  // Analyze new column definition for expression such as column-ref or JSON attribute-ref.
  // Example for column ref.
  //   TABLE (a, b, c)
  //   INDEX (c)
  //   This column in the index-table is "defined" the same as the column in the data-table.
  // Example for JSON index
  //   TABLE (a, b, j)
  //   INDEX (j->>'b') -> INDEX is a table whose column 'j->>b' is referencing to TABLE(j)
  SemState sem_state(sem_context);
  sem_state.set_processing_index_column(this);
  RETURN_NOT_OK(colexpr_->Analyze(sem_context));
  sem_state.set_processing_index_column(nullptr);

  // Check if indexing expression is supported.
  if (colexpr_->opcode() == TreeNodeOpcode::kPTRef) {
    // Transfer information from indexed_table::column_desc to this index::column_def.
    const ColumnDesc *coldesc = sem_context->GetColumnDesc(*name_);
    is_static_ = coldesc->is_static();
    if(!FLAGS_cql_allow_static_column_index && is_static_)
      return sem_context->Error(this, "Static columns cannot be indexed.",
                                ErrorCode::SQL_STATEMENT_INVALID);

  } else if (colexpr_->opcode() != TreeNodeOpcode::kPTJsonOp) {
    // Currently only JSon refereence is allowed for indexing.
    return sem_context->Error(this, "Only columns and JSONB attributes can be used for indexing",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }

  // For CREATE INDEX, column is not defined in this statement, so this node is used as definition.
  coldef_ = this;
  RETURN_NOT_OK(sem_context->MapSymbol(*name_, this));

  return Status::OK();
}

Status PTIndexColumn::SetupPrimaryKey(SemContext *sem_context) {
  RETURN_NOT_OK(Analyze(sem_context));
  PTCreateTable* table = sem_context->current_create_table_stmt();
  return table->AppendPrimaryColumn(sem_context, coldef_);
}

Status PTIndexColumn::SetupHashKey(SemContext *sem_context) {
  RETURN_NOT_OK(Analyze(sem_context));
  PTCreateTable* table = sem_context->current_create_table_stmt();
  return table->AppendHashColumn(sem_context, coldef_);
}

std::shared_ptr<QLType> PTIndexColumn::ql_type() const {
  return colexpr_->ql_type();
}

Status PTIndexColumn::SetupCoveringIndexColumn(SemContext *sem_context) {
  coldef_ = sem_context->GetColumnDefinition(*name_);
  if (coldef_ && coldef_->colexpr()->opcode() == TreeNodeOpcode::kPTRef) {
    // Ignore as column is already defined as a part of INDEX.
    return Status::OK();
  }

  // Analyze column as it has not been included in the INDEX.
  RETURN_NOT_OK(Analyze(sem_context));

  // Not allow static columns.
  if (coldef_->is_static()) {
    return sem_context->Error(coldef_, "Static column not supported as a covering index column",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }

  // Not allow expressions in INCLUDE.
  if (colexpr_->opcode() != TreeNodeOpcode::kPTRef) {
    return sem_context->Error(coldef_, "Only columns can be used for COVERING clause",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }

  if (!QLType::IsValidPrimaryType(ql_type()->main()) || ql_type()->main() == DataType::FROZEN) {
    return sem_context->Error(coldef_, "Unsupported index datatype",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }

  // Add the analyzed covering index column to table. Need to check for proper datatype and set
  // column location because column definition is loaded from the indexed table definition actually.
  DCHECK(sem_context->current_create_table_stmt()->opcode() == TreeNodeOpcode::kPTCreateIndex);
  PTCreateIndex* tab = static_cast<PTCreateIndex*>(sem_context->current_create_table_stmt());
  return tab->AppendIndexColumn(sem_context, coldef_);
}

}  // namespace ql
}  // namespace yb
