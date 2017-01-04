//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// TreeNode base class.
//--------------------------------------------------------------------------------------------------

TreeNode::TreeNode(MemoryContext *memctx, YBLocation::SharedPtr loc)
    : MCBase(memctx), loc_(loc) {
}

TreeNode::~TreeNode() {
}

// Run semantics analysis on this node.
ErrorCode TreeNode::Analyze(SemContext *sem_context) {
  // Raise unsupported error when a treenode does not implement this method.
  sem_context->Error(loc(), ErrorCode::FEATURE_NOT_SUPPORTED);
  return ErrorCode::FEATURE_NOT_SUPPORTED;
}

}  // namespace sql
}  // namespace yb
