//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for CREATE TABLE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_create_table.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using std::to_string;
using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

PTCreateTable::PTCreateTable(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             const PTQualifiedName::SharedPtr& name,
                             const PTListNode::SharedPtr& elements,
                             bool create_if_not_exists,
                             const PTTablePropertyListNode::SharedPtr& table_properties)
    : TreeNode(memctx, loc),
      relation_(name),
      elements_(elements),
      columns_(memctx),
      primary_columns_(memctx),
      hash_columns_(memctx),
      create_if_not_exists_(create_if_not_exists),
      table_properties_(table_properties) {
}

PTCreateTable::~PTCreateTable() {
}

CHECKED_STATUS PTCreateTable::Analyze(SemContext *sem_context) {
  // Processing table name.
  RETURN_NOT_OK(relation_->Analyze(sem_context));

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_table(this);

  // Processing table elements.
  RETURN_NOT_OK(elements_->Analyze(sem_context));

  if (table_properties_ != nullptr) {
    // Process table properties.
    RETURN_NOT_OK(table_properties_->Analyze(sem_context));
  }

  // Move the all partition and primary columns from columns list to appropriate list.
  int32_t order = 0;
  MCList<PTColumnDefinition *>::iterator iter = columns_.begin();
  while (iter != columns_.end()) {
    PTColumnDefinition *coldef = *iter;
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

  // When partition or primary keys are not defined, the first column must be partition key.
  if (hash_columns_.empty()) {
    if (!primary_columns_.empty()) {
      // First primary column must be a partition key.
      RETURN_NOT_OK(AppendHashColumn(sem_context, primary_columns_.front()));
      primary_columns_.pop_front();
    } else {
      // ERROR: Primary key is not defined.
      return sem_context->Error(elements_->loc(), ErrorCode::MISSING_PRIMARY_KEY);
    }
  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return Status::OK();
}

CHECKED_STATUS PTCreateTable::AppendColumn(SemContext *sem_context, PTColumnDefinition *column) {
  columns_.push_back(column);
  return Status::OK();
}

CHECKED_STATUS PTCreateTable::AppendPrimaryColumn(SemContext *sem_context,
                                                  PTColumnDefinition *column) {
  RETURN_NOT_OK(CheckPrimaryType(sem_context, column->datatype()));
  primary_columns_.push_back(column);
  return Status::OK();
}

CHECKED_STATUS PTCreateTable::AppendHashColumn(SemContext *sem_context,
                                               PTColumnDefinition *column) {
  RETURN_NOT_OK(CheckPrimaryType(sem_context, column->datatype()));
  hash_columns_.push_back(column);
  return Status::OK();
}

CHECKED_STATUS PTCreateTable::CheckPrimaryType(SemContext *sem_context,
                                               const PTBaseType::SharedPtr& datatype) {
  switch (datatype->sql_type()) {
  case DataType::DOUBLE: FALLTHROUGH_INTENDED;
  case DataType::FLOAT: FALLTHROUGH_INTENDED;
  case DataType::BOOL:
    return sem_context->Error(datatype->loc(), ErrorCode::INVALID_PRIMARY_COLUMN_TYPE);

  default:
    // Let all other types go. Because we already process column datatype before getting here, just
    // assume that they are all valid types.
    return Status::OK();
  }
}

void PTCreateTable::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output(sem_context->PTempMem(), "\tTable ");
  sem_output += yb_table_name().ToString().c_str();
  sem_output += "(";

  bool is_first = true;
  for (auto column : columns_) {
    if (is_first) {
      is_first = false;
    } else {
      sem_output += ", ";
    }
    sem_output += column->yb_name();
    if (column->is_hash_key()) {
      sem_output += " <Shard-hash key, Type = ";
    } else if (column->is_primary_key()) {
      sem_output += " <Primary key, Type = ";
    } else {
      sem_output += " <Type = ";
    }
    sem_output += to_string(column->sql_type()).c_str();
    sem_output += ">";
  }

  sem_output += ")";
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

CHECKED_STATUS PTCreateTable::ToTableProperties(TableProperties *table_properties) const {
  if (table_properties_ != nullptr) {
    for (PTTableProperty::SharedPtr table_property : table_properties_->node_list()) {
      RETURN_NOT_OK(table_property->SetTableProperty(table_properties));
    }
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTColumnDefinition::PTColumnDefinition(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc,
                                       const MCString::SharedPtr& name,
                                       const PTBaseType::SharedPtr& datatype,
                                       const PTListNode::SharedPtr& constraints)
    : TreeNode(memctx, loc),
      name_(name),
      datatype_(datatype),
      constraints_(constraints),
      is_primary_key_(false),
      is_hash_key_(false),
      order_(-1) {
}

PTColumnDefinition::~PTColumnDefinition() {
}

CHECKED_STATUS PTColumnDefinition::Analyze(SemContext *sem_context) {
  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_column(this);

  // Analyze column constraint.
  RETURN_NOT_OK(sem_context->MapSymbol(*name_, this));
  if (constraints_ != nullptr) {
    RETURN_NOT_OK(constraints_->Analyze(sem_context));
  }

  // Add the analyzed column to table.
  PTCreateTable *table = sem_context->current_table();
  RETURN_NOT_OK(table->AppendColumn(sem_context, this));

  // Restore the context value as we are done with this colummn.
  sem_context->set_current_processing_id(cached_entry);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTPrimaryKey::PTPrimaryKey(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           const PTListNode::SharedPtr& columns)
    : PTConstraint(memctx, loc),
      columns_(columns) {
}

PTPrimaryKey::~PTPrimaryKey() {
}

CHECKED_STATUS PTPrimaryKey::Analyze(SemContext *sem_context) {
  // Check if primary key is defined more than one time.
  PTCreateTable *table = sem_context->current_table();
  if (table->primary_columns().size() > 0 || table->hash_columns().size() > 0) {
    return sem_context->Error(loc(), "Too many primary key", ErrorCode::INVALID_TABLE_DEFINITION);
  }

  if (columns_ == nullptr) {
    // Decorate the current processing name node as this is a column constraint.
    PTColumnDefinition *column = sem_context->current_column();
    column->set_is_hash_key();
    RETURN_NOT_OK(table->AppendHashColumn(sem_context, column));

  } else {
    // Decorate all name node of this key as this is a table constraint.
    TreeNodeOperator<SemContext, PTName> func = &PTName::SetupPrimaryKey;
    TreeNodeOperator<SemContext, PTName> nested_func = &PTName::SetupHashAndPrimaryKey;
    RETURN_NOT_OK((columns_->Apply<SemContext, PTName>(sem_context, func, 1, 1, nested_func)));
  }
  return Status::OK();
}

}  // namespace sql
}  // namespace yb
