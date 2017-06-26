//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for ALTER TABLE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_alter_table.h"
#include "yb/sql/ptree/pt_create_table.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBColumnSchema;
using client::YBTableName;

//--------------------------------------------------------------------------------------------------

PTAlterTable::PTAlterTable(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTQualifiedName::SharedPtr name,
                           const PTListNode::SharedPtr &commands)
  : TreeNode(memctx, loc),
    name_(name),
    commands_(commands),
    table_columns_(memctx),
    mod_columns_(memctx),
    with_props_(memctx) {
}

PTAlterTable::~PTAlterTable() {
}

CHECKED_STATUS PTAlterTable::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(name_->Analyze(sem_context));

  // Populate internal table_ variable.
  YBTableName table_name = name_->ToTableName();
  RETURN_NOT_OK(sem_context->LookupTable(table_name, &table_, &table_columns_, &num_key_columns_,
                                         &num_hash_key_columns_, &is_system_,
                                         /* write only = */true, name_->loc()));

  // Save context state, and set "this" as current table being altered.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_alter_table(this);

  // Process alter commands.
  RETURN_NOT_OK(commands_->Analyze(sem_context));

  // Restore saved context state.
  sem_context->set_current_processing_id(cached_entry);

  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }
  return Status::OK();
}

void PTAlterTable::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output("\tAltering Table ", sem_context->PTempMem());
  sem_output += table_.get()->name().ToString().c_str();
  sem_output += "(";

  sem_output += ")";
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
}

CHECKED_STATUS PTAlterTable::AppendModColumn(SemContext *sem_context,
                                             PTAlterColumnDefinition *column) {

  if (column->mod_type() != ALTER_ADD) {
    // Check if column already exists

    const ColumnDesc* desc = sem_context->GetColumnDesc(column->name()->last_name(),
                             /* reading column = */ false);
    if (desc == nullptr) {
      return sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
    }
  }

  if (column->datatype() != nullptr) {
    RETURN_NOT_OK(PTCreateTable::CheckType(sem_context, column->datatype()));
  }

  switch (column->mod_type()) {
    case ALTER_ADD:
      // TODO: check for dup name etc
      break;
    case ALTER_DROP:
      // TODO: check if key column
      break;
    case ALTER_RENAME:
      // TODO: check for dup name etc
      break;
    case ALTER_TYPE:
      break;
  }

  mod_columns_.push_back(column);
  return Status::OK();
}

CHECKED_STATUS PTAlterTable::AppendAlterProperty(SemContext *sem_context, PTAlterProperty *prop) {
  const auto property_name = string(prop->property_name()->c_str());
  bool found_match = false;
  for (auto supported_property : supported_properties) {
    found_match |= (supported_property == property_name);
  }
  if (!found_match) {
    return sem_context->Error(prop->loc(), ErrorCode::INVALID_TABLE_PROPERTY);
  }
  with_props_.push_back(prop);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTAlterColumnDefinition::PTAlterColumnDefinition(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         PTQualifiedName::SharedPtr name,
                         const MCSharedPtr<MCString>& new_name,
                         const PTBaseType::SharedPtr& datatype,
                         AlterColumnType type)
  : TreeNode(memctx, loc),
    name_(name),
    new_name_(new_name),
    datatype_(datatype),
    type_(type) {
}

PTAlterColumnDefinition::~PTAlterColumnDefinition() {
}

CHECKED_STATUS PTAlterColumnDefinition::Analyze(SemContext *sem_context) {
  if (name_ != nullptr) {
    RETURN_NOT_OK(name_->Analyze(sem_context));
  }

  if (new_name_ != nullptr) {
    RETURN_NOT_OK(sem_context->MapSymbol(*new_name_, this));
  }

  PTAlterTable *table = sem_context->current_alter_table();
  RETURN_NOT_OK(table->AppendModColumn(sem_context, this));

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTAlterProperty::PTAlterProperty(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const MCSharedPtr<MCString>& lhs,
                                 const MCSharedPtr<MCString>& rhs)
  : TreeNode(memctx, loc),
    lhs_(lhs),
    rhs_(rhs) {
}

PTAlterProperty::~PTAlterProperty() {
}

CHECKED_STATUS PTAlterProperty::Analyze(SemContext *sem_context) {
  VLOG(3) << "Appending " << lhs_.get() << rhs_ << "\n";

  PTAlterTable *table = sem_context->current_alter_table();
  RETURN_NOT_OK(table->AppendAlterProperty(sem_context, this));

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
