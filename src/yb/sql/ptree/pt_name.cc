//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for all name nodes.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_name.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTName::PTName(MemoryContext *memctx, const MCString::SharedPtr& name) : name_(name) {
}

PTName::~PTName() {
}

PTNameAll::PTNameAll(MemoryContext *memctx) : PTName(memctx, MCString::MakeShared(memctx, "*")) {
}

PTNameAll::~PTNameAll() {
}

//--------------------------------------------------------------------------------------------------

PTQualifiedName::PTQualifiedName(MemoryContext *memctx, const PTName::SharedPtr& ptname)
    : ptnames_(memctx) {
  Append(ptname);
}

PTQualifiedName::PTQualifiedName(MemoryContext *memctx, const MCString::SharedPtr& name)
    : ptnames_(memctx) {
  Append(PTName::MakeShared(memctx, name));
}

PTQualifiedName::~PTQualifiedName() {
}

void PTQualifiedName::Append(const PTName::SharedPtr& ptname) {
  ptnames_.push_back(ptname);
}

void PTQualifiedName::Prepend(const PTName::SharedPtr& ptname) {
  ptnames_.push_front(ptname);
}

}  // namespace sql
}  // namespace yb
