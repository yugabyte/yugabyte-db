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

#include "yb/yql/cql/ql/ptree/pt_create_table.h"

#include "yb/client/schema.h"
#include "yb/common/schema.h"

#include "yb/util/flags.h"

#include "yb/yql/cql/ql/ptree/pt_column_definition.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/pt_table_property.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"
#include "yb/yql/cql/ql/util/errcodes.h"

DECLARE_bool(use_cassandra_authentication);

DEFINE_UNKNOWN_bool(cql_table_is_transactional_by_default, false,
            "When the 'transactions' property is not specified at CREATE TABLE time "
            "for a YCQL table, this flag determines the default setting for whether "
            "the table is transactional or not.");
TAG_FLAG(cql_table_is_transactional_by_default, advanced);

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTCreateTable::PTCreateTable(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             const PTQualifiedName::SharedPtr& name,
                             const PTListNodePtr& elements,
                             bool create_if_not_exists,
                             const PTTablePropertyListNode::SharedPtr& table_properties)
    : TreeNode(memctx, loc),
      relation_(name),
      elements_(elements),
      columns_(memctx),
      primary_columns_(memctx),
      hash_columns_(memctx),
      create_if_not_exists_(create_if_not_exists),
      contain_counters_(false),
      table_properties_(table_properties) {
}

PTCreateTable::~PTCreateTable() {
}

client::YBTableName PTCreateTable::yb_table_name() const {
  return relation_->ToTableName();
}

Status PTCreateTable::Analyze(SemContext *sem_context) {
  SemState sem_state(sem_context);

  // Processing table name.
  RETURN_NOT_OK(relation_->AnalyzeName(sem_context, ObjectType::TABLE));

  // For creating an index operation, SemContext::LookupTable should have already checked that the
  // current role has the ALTER permission on the table. We don't need to check for any additional
  // permissions for this operation.
  if (FLAGS_use_cassandra_authentication && opcode() != TreeNodeOpcode::kPTCreateIndex) {
    RETURN_NOT_OK(sem_context->CheckHasKeyspacePermission(loc(), PermissionType::CREATE_PERMISSION,
                                                          yb_table_name().namespace_name()));
  }

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_create_table_stmt(this);

  // Processing table elements.
  // - First, process all column definitions to collect all symbols.
  // - Process all other elements afterward.
  sem_state.set_processing_column_definition(true);
  RETURN_NOT_OK(elements_->Analyze(sem_context));

  sem_state.set_processing_column_definition(false);
  RETURN_NOT_OK(elements_->Analyze(sem_context));

  // Move the all partition and primary key columns from columns list to appropriate list.
  int32_t order = 0;
  MCList<PTColumnDefinition *>::iterator iter = columns_.begin();
  while (iter != columns_.end()) {
    PTColumnDefinition *coldef = *iter;
    coldef->set_order(order++);

    // Remove a column from regular column list if it's already in hash or primary list.
    if (coldef->is_hash_key()) {
      if (coldef->is_static()) {
        return sem_context->Error(coldef, "Hash column cannot be static",
                                  ErrorCode::INVALID_TABLE_DEFINITION);
      }
      iter = columns_.erase(iter);
    } else if (coldef->is_primary_key()) {
      if (coldef->is_static()) {
        return sem_context->Error(coldef, "Primary key column cannot be static",
                                  ErrorCode::INVALID_TABLE_DEFINITION);
      }
      iter = columns_.erase(iter);
    } else {
      if (contain_counters_ != coldef->is_counter()) {
        return sem_context->Error(coldef,
                                  "Table cannot contain both counter and non-counter columns",
                                  ErrorCode::INVALID_TABLE_DEFINITION);
      }
      iter++;
    }
  }

  // When partition key is not defined, the first column is assumed to be the partition key.
  if (hash_columns_.empty()) {
    if (primary_columns_.empty()) {
      return sem_context->Error(elements_, ErrorCode::MISSING_PRIMARY_KEY);
    }
    RETURN_NOT_OK(AppendHashColumn(sem_context, primary_columns_.front()));
    primary_columns_.pop_front();
  }

  // After primary key columns are fully analyzed, return error if the table contains no range
  // (primary) column but a static column. With no range column, every non-key column is static
  // by nature and Cassandra raises error in such a case.
  if (primary_columns_.empty()) {
    for (const auto& column : columns_) {
      if (column->is_static()) {
        return sem_context->Error(column,
                                  "Static column not allowed in a table without a range column",
                                  ErrorCode::INVALID_TABLE_DEFINITION);
      }
    }
  }

  if (table_properties_ != nullptr) {
    // Process table properties.
    RETURN_NOT_OK(table_properties_->Analyze(sem_context));
  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

bool PTCreateTable::ColumnExists(const MCList<PTColumnDefinition *>& columns,
                                 const PTColumnDefinition* column) {
  return std::find(columns.begin(), columns.end(), column) != columns.end();
}

Status PTCreateTable::AppendColumn(SemContext *sem_context,
                                   PTColumnDefinition *column,
                                   const bool check_duplicate) {
  if (check_duplicate && ColumnExists(columns_, column)) {
    return sem_context->Error(column, ErrorCode::DUPLICATE_COLUMN);
  }
  columns_.push_back(column);

  if (column->is_counter()) {
    contain_counters_ = true;
  }
  return Status::OK();
}

Status PTCreateTable::AppendPrimaryColumn(SemContext *sem_context,
                                          PTColumnDefinition *column,
                                          const bool check_duplicate) {
  // The column and its datatype should already have been analyzed at this point.
  // Check if the column can be used as primary column.
  RETURN_NOT_OK(CheckPrimaryType(sem_context, column));
  if (check_duplicate && ColumnExists(primary_columns_, column)) {
    return sem_context->Error(column, ErrorCode::DUPLICATE_COLUMN);
  }
  column->set_is_primary_key();
  primary_columns_.push_back(column);
  return Status::OK();
}

Status PTCreateTable::AppendHashColumn(SemContext *sem_context,
                                       PTColumnDefinition *column,
                                       const bool check_duplicate) {
  // The column and its datatype should already have been analyzed at this point.
  // Check if the column can be used as hash column.
  RETURN_NOT_OK(CheckPrimaryType(sem_context, column));
  if (check_duplicate && ColumnExists(hash_columns_, column)) {
    return sem_context->Error(column, ErrorCode::DUPLICATE_COLUMN);
  }
  column->set_is_hash_key();
  hash_columns_.push_back(column);
  return Status::OK();
}

Status PTCreateTable::CheckPrimaryType(SemContext *sem_context,
                                       const PTColumnDefinition *column) const {
  // Column must have been analyzed. Check if its datatype is allowed for primary column.
  if (!QLType::IsValidPrimaryType(column->ql_type()->main())) {
    return sem_context->Error(column, ErrorCode::INVALID_PRIMARY_COLUMN_TYPE);
  }
  return Status::OK();
}

void PTCreateTable::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tTable ", sem_context->PTempMem());
  sem_output += yb_table_name().ToString().c_str();
  sem_output += "(";

  MCList<PTColumnDefinition *> columns(sem_context->PTempMem());
  for (auto column : hash_columns_) {
    columns.push_back(column);
  }
  for (auto column : primary_columns_) {
    columns.push_back(column);
  }
  for (auto column : columns_) {
    columns.push_back(column);
  }

  bool is_first = true;
  for (auto column : columns) {
    if (is_first) {
      is_first = false;
    } else {
      sem_output += ", ";
    }
    sem_output += column->yb_name();
    if (column->is_hash_key()) {
      sem_output += " <Hash key, Type = ";
    } else if (column->is_primary_key()) {
      sem_output += " <Primary key, ";
      switch (column->sorting_type()) {
        case SortingType::kNotSpecified: sem_output += "None"; break;
        case SortingType::kAscending: sem_output += "Asc"; break;
        case SortingType::kDescending: sem_output += "Desc"; break;
        case SortingType::kAscendingNullsLast: sem_output += "Asc nulls last"; break;
        case SortingType::kDescendingNullsLast: sem_output += "Desc nulls last"; break;
      }
      sem_output += ", Type = ";
    } else {
      sem_output += " <Type = ";
    }
    sem_output += column->ql_type()->ToString().c_str();
    sem_output += ">";
  }

  sem_output += ")";
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

Status PTCreateTable::ToTableProperties(TableProperties *table_properties) const {
  // Some external tools need to create indexes for a regular table.
  // For such tools any new table can be created as transactional by default.
  if (PREDICT_FALSE(FLAGS_cql_table_is_transactional_by_default)) {
    // Note: the table property can be overrided below by the user specified value.
    table_properties->SetTransactional(true);
  }

  if (table_properties_ != nullptr) {
    for (PTTableProperty::SharedPtr table_property : table_properties_->node_list()) {
      RETURN_NOT_OK(table_property->SetTableProperty(table_properties));
    }
  }

  table_properties->SetContainCounters(contain_counters_);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTPrimaryKey::PTPrimaryKey(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           const PTListNodePtr& columns)
    : PTConstraint(memctx, loc),
      columns_(columns) {
}

PTPrimaryKey::~PTPrimaryKey() {
}

namespace {

Status SetupKeyNodeFunc(PTIndexColumn *node, SemContext *sem_context) {
  return node->SetupPrimaryKey(sem_context);
}

Status SetupNestedKeyNodeFunc(PTIndexColumn *node, SemContext *sem_context) {
  return node->SetupHashKey(sem_context);
}

} // namespace

Status PTPrimaryKey::Analyze(SemContext *sem_context) {
  if (sem_context->processing_column_definition() != is_column_element()) {
    return Status::OK();
  }

  // Check if primary key is defined more than one time.
  PTCreateTable *table = sem_context->current_create_table_stmt();
  if (table->primary_columns().size() > 0 || table->hash_columns().size() > 0) {
    return sem_context->Error(this, "Too many primary key", ErrorCode::INVALID_TABLE_DEFINITION);
  }

  if (columns_ == nullptr) {
    // Decorate the current processing name node as this is a column constraint.
    PTColumnDefinition *column = sem_context->current_column();
    RETURN_NOT_OK(table->AppendHashColumn(sem_context, column));
  } else {
    // Decorate all name node of this key as this is a table constraint.
    TreeNodePtrOperator<SemContext, PTIndexColumn> func = &SetupKeyNodeFunc;
    TreeNodePtrOperator<SemContext, PTIndexColumn> nested_func = &SetupNestedKeyNodeFunc;
    RETURN_NOT_OK(
        (columns_->Apply<SemContext, PTIndexColumn>(sem_context, func, 1, 1, nested_func)));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status PTStatic::Analyze(SemContext *sem_context) {
  // Decorate the current column as static.
  PTColumnDefinition *column = sem_context->current_column();
  column->set_is_static();
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
