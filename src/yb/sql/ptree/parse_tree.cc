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

ParseTree::ParseTree(std::shared_ptr<MemTracker> mem_tracker)
    : root_(nullptr),
      ptree_mem_(new MemoryContext(mem_tracker)),
      psem_mem_(new MemoryContext(mem_tracker)) {
}

ParseTree::~ParseTree() {
  // Make sure we delete the tree first before deleting the memory pools.
  root_ = nullptr;
  psem_mem_ = nullptr;
  ptree_mem_ = nullptr;
}

CHECKED_STATUS ParseTree::Analyze(SemContext *sem_context) {
  if (root_ == nullptr) {
    LOG(INFO) << "Parse tree is NULL";
    return Status::OK();
  }

  // Reset and release previous semantic analysis results and free the associated memory.
  root_->Reset();
  psem_mem_->Reset();

  return root_->Analyze(sem_context);
}

}  // namespace sql
}  // namespace yb
