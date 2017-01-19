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
                             const PTListNode::SharedPtr& elements,
                             bool create_if_not_exists)
    : TreeNode(memctx, loc),
      relation_(name),
      elements_(elements),
      columns_(memctx),
      primary_columns_(memctx),
      hash_columns_(memctx),
      create_if_not_exists_(create_if_not_exists),
      table_already_exists_(false) {
}

PTCreateTable::~PTCreateTable() {
}

CHECKED_STATUS PTCreateTable::Analyze(SemContext *sem_context) {
  // DDL statement is not allowed to be retry.
  if (sem_context->retry_count() > 0) {
    return sem_context->Error(loc(), ErrorCode::DDL_EXECUTION_RERUN_NOT_ALLOWED);
  }

  // Processing table name.
  RETURN_NOT_OK(relation_->Analyze(sem_context));

  // Save context state, and set "this" as current column in the context.
  SymbolEntry cached_entry = *sem_context->current_processing_id();
  sem_context->set_current_table(this);

  // Processing table elements.
  RETURN_NOT_OK(elements_->Analyze(sem_context));

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
      AppendHashColumn(primary_columns_.front());
      primary_columns_.pop_front();
    } else {
      // First column must be a partition key.
      AppendHashColumn(columns_.front());
      columns_.pop_front();
    }
  }

  // Restore the context value as we are done with this table.
  sem_context->set_current_processing_id(cached_entry);
  if (VLOG_IS_ON(3)) {
    PrintSemanticAnalysisResult(sem_context);
  }

  std::shared_ptr<client::YBTable> table =
      sem_context->GetTableDesc(relation_->last_name().c_str(), true);
  if (table != nullptr) {
    if (!create_if_not_exists_) {
      return sem_context->Error(relation_->loc(), "Cannot create a table that already exists",
                                ErrorCode::DUPLICATE_TABLE);
    } else {
      table_already_exists_ = true;
      return Status::OK();
    }
  }

  return Status::OK();
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
    sem_output += to_string(column->sql_type()).c_str();
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
  table->AppendColumn(this);

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
    table->AppendHashColumn(column);

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
