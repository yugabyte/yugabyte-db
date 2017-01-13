//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/parse_tree.h"
#include "yb/sql/ptree/tree_node.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

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

CHECKED_STATUS ParseTree::Analyze(SemContext *sem_context) {
  if (root_ == nullptr) {
    LOG(INFO) << "Parse tree is NULL";
    return Status::OK();
  }
  return root_->Analyze(sem_context);
}

}  // namespace sql
}  // namespace yb
