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

//--------------------------------------------------------------------------------------------------

PTCreateTable::PTCreateTable(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             const PTQualifiedName::SharedPtr& name,
                             const PTListNode::SharedPtr& elements)
    : TreeNode(memctx, loc),
      relation_(name),
      elements_(elements),
      columns_(memctx),
      primary_columns_(memctx),
      hash_columns_(memctx) {
}

PTCreateTable::~PTCreateTable() {
}

ErrorCode PTCreateTable::Analyze(SemContext *sem_context) {
  ErrorCode err;

  err = relation_->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_table(this);

  err = elements_->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  // Set first column as primary key if the create statement doesn't define primary key.
  if (hash_columns_.empty() && primary_columns_.empty()) {
    primary_columns_.push_back(columns_.front());
    columns_.pop_front();
  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  return err;
}

void PTCreateTable::PrintSemanticAnalysisResult(SemContext *sem_context) {
  MCString sem_output(sem_context->PTempMem(), "\tTable ");
  sem_output += yb_table_name();
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
    sem_output += to_string(column->yb_data_type()).c_str();
    sem_output += ">";
  }

  sem_output += ")";
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << sem_output;
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
      is_hash_key_(false) {
}

PTColumnDefinition::~PTColumnDefinition() {
}

ErrorCode PTColumnDefinition::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_column(this);

  // Analyze column constraint.
  sem_context->MapSymbol(*name_, this);
  if (constraints_ != nullptr) {
    constraints_->Analyze(sem_context);
  }

  // Add the analyzed column to table.
  PTCreateTable *table = sem_context->current_table();
  if (is_hash_key_) {
    table->AppendHashColumn(this);
  } else if (is_primary_key_) {
    table->AppendPrimaryColumn(this);
  } else {
    table->AppendColumn(this);
  }

  // Restore the context value as we are done with this colummn.
  sem_context->set_current_processing_id(cached_entry);
  return err;
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

ErrorCode PTPrimaryKey::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  if (columns_ == nullptr) {
    // Decorate the current processing name node as this is a column constraint.
    PTColumnDefinition *column = sem_context->current_column();
    column->set_is_primary_key(true);
  } else {
    // Decorate all name node of this key as this is a table constraint.
    TreeNodeOperator<SemContext, PTName> func = &PTName::SetupPrimaryKey;
    TreeNodeOperator<SemContext, PTName> nested_func = &PTName::SetupHashAndPrimaryKey;
    columns_->Apply<SemContext, PTName>(sem_context, func, 1, 1, nested_func);
  }
  return err;
}

}  // namespace sql
}  // namespace yb
