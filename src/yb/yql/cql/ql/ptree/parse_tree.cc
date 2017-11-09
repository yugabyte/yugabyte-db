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

#include "yb/yql/cql/ql/ptree/parse_tree.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Parse Tree
//--------------------------------------------------------------------------------------------------

namespace {

std::shared_ptr<BufferAllocator> MakeAllocator(std::shared_ptr<MemTracker> mem_tracker) {
  if (mem_tracker) {
    return std::make_shared<MemoryTrackingBufferAllocator>(HeapBufferAllocator::Get(),
                                                           std::move(mem_tracker));
  } else {
    return nullptr;
  }
}

} // namespace

ParseTree::ParseTree(const bool reparsed, std::shared_ptr<MemTracker> mem_tracker)
    : buffer_allocator_(MakeAllocator(std::move(mem_tracker))),
      ptree_mem_(buffer_allocator_ ? buffer_allocator_.get() : HeapBufferAllocator::Get()),
      psem_mem_(buffer_allocator_ ? buffer_allocator_.get() : HeapBufferAllocator::Get()),
      reparsed_(reparsed) {
}

ParseTree::~ParseTree() {
  // Make sure we delete the tree first before deleting the memory pools.
  root_ = nullptr;
}

CHECKED_STATUS ParseTree::Analyze(SemContext *sem_context) {
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
        return sem_context->Error(root_, "Multi-statement list not supported yet",
                                  ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  return root_->Analyze(sem_context);
}

void ParseTree::AddAnalyzedTable(const client::YBTableName& table_name) {
  analyzed_tables_.insert(table_name);
}

void ParseTree::ClearAnalyzedTableCache(QLEnv* ql_env) const {
  for (const auto& table_name : analyzed_tables_) {
    ql_env->RemoveCachedTableDesc(table_name);
  }
}

void ParseTree::AddAnalyzedUDType(const std::string& keyspace_name, const std::string& type_name) {
  analyzed_types_.insert(std::make_pair(keyspace_name, type_name));
}

void ParseTree::ClearAnalyzedUDTypeCache(QLEnv *ql_env) const {
  for (const auto& type_name : analyzed_types_) {
    ql_env->RemoveCachedUDType(type_name.first, type_name.second);
  }
}

}  // namespace ql
}  // namespace yb
