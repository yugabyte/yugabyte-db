//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode definitions for CREATE TABLE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_create_table.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTCreateTable::PTCreateTable(MemoryContext *memctx,
                             const PTQualifiedName::SharedPtr& name,
                             const PTListNode::SharedPtr& elements)
    : relation_(name),
      elements_(elements) {
}

PTCreateTable::~PTCreateTable() {
}

//--------------------------------------------------------------------------------------------------

PTColumnDefinition::PTColumnDefinition(MemoryContext *memctx,
                                       const MCString::SharedPtr& name,
                                       const PTBaseType::SharedPtr& datatype,
                                       const PTListNode::SharedPtr& constraints)
    : name_(name),
      datatype_(datatype),
      constraints_(constraints) {
}

PTColumnDefinition::~PTColumnDefinition() {
}

//--------------------------------------------------------------------------------------------------

PTPrimaryKey::PTPrimaryKey(MemoryContext *memctx,
                           const PTListNode::SharedPtr& columns)
    : columns_(columns) {
}

PTPrimaryKey::~PTPrimaryKey() {
}

}  // namespace sql
}  // namespace yb
