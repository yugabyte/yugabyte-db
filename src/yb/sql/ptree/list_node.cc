//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// List Node Definitions.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/list_node.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// TreeNode base class.
//--------------------------------------------------------------------------------------------------

PTListNode::PTListNode(MemoryContext *memory_context, YBLocation::SharedPtr loc)
    : TreeNode(memory_context, loc),
      node_list_(memory_context) {
}

PTListNode::~PTListNode() {
}

void PTListNode::Append(const TreeNode::SharedPtr& tnode) {
  if (tnode != nullptr) {
    node_list_.push_back(tnode);
  }
}

void PTListNode::Prepend(const TreeNode::SharedPtr& tnode) {
  if (tnode != nullptr) {
    node_list_.push_front(tnode);
  }
}

ErrorCode PTListNode::Analyze(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  for (auto tnode : node_list_) {
    err = tnode->Analyze(sem_context);
    if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
      return err;
    }
  }
  return err;
}

}  // namespace sql
}  // namespace yb
