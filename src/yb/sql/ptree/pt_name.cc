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
  const SymbolEntry *entry = sem_context->SeekSymbol(*name_);
  if (entry == nullptr) {
    LOG(INFO) << "Column \"" << *name_ << "\" doesn't exist";
    return ErrorCode::UNDEFINED_COLUMN;
  }

  PTColumnDefinition *column = entry->column_;
  column->set_is_primary_key(true);

  return ErrorCode::SUCCESSFUL_COMPLETION;
}

ErrorCode PTName::SetupHashAndPrimaryKey(SemContext *sem_context) {
  const SymbolEntry *entry = sem_context->SeekSymbol(*name_);
  if (entry == nullptr) {
    sem_context->Error(loc(), "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
    return ErrorCode::UNDEFINED_COLUMN;
  }

  PTColumnDefinition *column = entry->column_;
  column->set_is_hash_key(true);
  column->set_is_primary_key(true);

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
      ptnames_(memctx) {
  Append(ptname);
}

PTQualifiedName::PTQualifiedName(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const MCString::SharedPtr& name)
    : PTName(memctx, loc),
      ptnames_(memctx) {
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
  if (ptnames_.size() > 1) {
    return ErrorCode::FEATURE_NOT_SUPPORTED;
  }
  return ErrorCode::SUCCESSFUL_COMPLETION;
}

}  // namespace sql
}  // namespace yb
