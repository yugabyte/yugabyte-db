//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//--------------------------------------------------------------------------------------------------

#include <stdio.h>

#include "yb/sql/ptree/parse_tree.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------
// TreeNode base class.
//--------------------------------------------------------------------------------------------------

TreeNode::TreeNode(MemoryContext *memctx) {
}

TreeNode::~TreeNode() {
}

//--------------------------------------------------------------------------------------------------
// TreeNode base class.
//--------------------------------------------------------------------------------------------------

PTListNode::PTListNode(MemoryContext *memory_context)
    : node_list_(memory_context) {
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

//--------------------------------------------------------------------------------------------------
// Parse Tree
//--------------------------------------------------------------------------------------------------

ParseTree::ParseTree()
    : root_(nullptr),
      ptree_mem_(new MemoryContext()) {
}

ParseTree::~ParseTree() {
  // Make sure we delete the tree first before deleting the tree memory pool.
  root_ = nullptr;
  ptree_mem_ = nullptr;
}

}  // namespace sql
}  // namespace yb
