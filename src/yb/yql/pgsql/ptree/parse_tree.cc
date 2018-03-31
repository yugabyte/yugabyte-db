//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// Parse Tree Declaration.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/parse_tree.h"
#include "yb/yql/pgsql/ptree/tree_node.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// Parse Tree
//--------------------------------------------------------------------------------------------------

ParseTree::ParseTree()
    : ptree_mem_(buffer_allocator_ ? buffer_allocator_.get() : HeapBufferAllocator::Get()),
      psem_mem_(buffer_allocator_ ? buffer_allocator_.get() : HeapBufferAllocator::Get()) {
}

ParseTree::~ParseTree() {
  // Make sure we delete the tree first before deleting the memory pools.
  root_ = nullptr;
}

CHECKED_STATUS ParseTree::Analyze(PgCompileContext *compile_context) {
  if (root_ == nullptr) {
    LOG(INFO) << "Parse tree is NULL";
    return Status::OK();
  }

  // Restrict statement list to single statement only and hoist the statement to the root node.
  if (root_->opcode() == TreeNodeOpcode::kPTListNode) {
    const auto lnode = std::static_pointer_cast<PTListNode>(root_);
    switch (lnode->size()) {
      case 0:
        root_ = nullptr;
        return Status::OK();
      case 1:
        root_ = lnode->node_list().front();
        break;
      default:
        return compile_context->Error(root_, "Multi-statement list not supported yet",
                                  ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  return root_->Analyze(compile_context);
}

}  // namespace pgsql
}  // namespace yb
