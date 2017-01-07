//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for all name nodes.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_name.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTName::PTName(MemoryContext *memctx,
               YBLocation::SharedPtr loc,
               const MCString::SharedPtr& name)
    : TreeNode(memctx, loc),
      name_(name) {
}

PTName::~PTName() {
}

ErrorCode PTName::SetupPrimaryKey(SemContext *sem_context) {
  PTColumnDefinition *column = sem_context->GetColumnDefinition(*name_);
  if (column == nullptr) {
    LOG(INFO) << "Column \"" << *name_ << "\" doesn't exist";
    sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
    return ErrorCode::UNDEFINED_COLUMN;
  }
  column->set_is_primary_key();

  // Add the analyzed column to table.
  PTCreateTable *table = sem_context->current_table();
  table->AppendPrimaryColumn(column);

  return ErrorCode::SUCCESSFUL_COMPLETION;
}

ErrorCode PTName::SetupHashAndPrimaryKey(SemContext *sem_context) {
  PTColumnDefinition *column = sem_context->GetColumnDefinition(*name_);
  if (column == nullptr) {
    LOG(INFO) << "Column \"" << *name_ << "\" doesn't exist";
    sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
    return ErrorCode::UNDEFINED_COLUMN;
  }
  column->set_is_hash_key();

  // Add the analyzed column to table.
  PTCreateTable *table = sem_context->current_table();
  table->AppendHashColumn(column);

  return ErrorCode::SUCCESSFUL_COMPLETION;
}

//--------------------------------------------------------------------------------------------------

PTNameAll::PTNameAll(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : PTName(memctx, loc, MCString::MakeShared(memctx, "*")) {
}

PTNameAll::~PTNameAll() {
}

//--------------------------------------------------------------------------------------------------

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const PTName::SharedPtr& ptname)
    : PTName(memctx, loc),
      ptnames_(memctx),
      is_system_(false) {
  Append(ptname);
}

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const MCString::SharedPtr& name)
    : PTName(memctx, loc),
      ptnames_(memctx),
      is_system_(false) {
  Append(PTName::MakeShared(memctx, loc, name));
}

PTQualifiedName::~PTQualifiedName() {
}

void PTQualifiedName::Append(const PTName::SharedPtr& ptname) {
  ptnames_.push_back(ptname);
}

void PTQualifiedName::Prepend(const PTName::SharedPtr& ptname) {
  ptnames_.push_front(ptname);
}

ErrorCode PTQualifiedName::Analyze(SemContext *sem_context) {
  // We don't support qualified name yet except for "system" namespace.
  if (ptnames_.size() > 1) {
    const MCString& first_name = ptnames_.front()->name();
    if (first_name == "system") {
      is_system_ = true;
    } else {
      sem_context->Error(loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
      return ErrorCode::FEATURE_NOT_SUPPORTED;
    }
  }
  return ErrorCode::SUCCESSFUL_COMPLETION;
}

}  // namespace sql
}  // namespace yb
