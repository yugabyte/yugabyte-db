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

#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"

namespace yb {
namespace ql {

using std::string;

//--------------------------------------------------------------------------------------------------
// Parse Tree
//--------------------------------------------------------------------------------------------------

ParseTree::ParseTree(const string& stmt, const bool reparsed, const MemTrackerPtr& mem_tracker,
                     const bool internal)
    : stmt_(stmt),
      reparsed_(reparsed),
      buffer_allocator_(mem_tracker ?
                        std::make_shared<MemoryTrackingBufferAllocator>(HeapBufferAllocator::Get(),
                                                                        mem_tracker) :
                        nullptr),
      ptree_mem_(buffer_allocator_ ? buffer_allocator_.get() : HeapBufferAllocator::Get()),
      psem_mem_(buffer_allocator_ ? buffer_allocator_.get() : HeapBufferAllocator::Get()),
      internal_(internal) {
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

  // Each analysis process needs to have state variables.
  // Setup a base sem_state variable before traversing the statement tree.
  SemState sem_state(sem_context);

  DCHECK_EQ(root_->opcode(), TreeNodeOpcode::kPTListNode) << "statement list expected";
  const auto lnode = std::static_pointer_cast<PTListNode>(root_);
  switch (lnode->size()) {
    case 0:
      return sem_context->Error(lnode, "Unexpected empty statement list",
                                ErrorCode::SQL_STATEMENT_INVALID);
    case 1: {
      const TreeNode::SharedPtr tnode = lnode->node_list().front();
      if (internal_) {
        tnode->set_internal();
      }
      RETURN_NOT_OK(tnode->Analyze(sem_context));
      // Hoist the statement to the root node.
      root_ = tnode;
      return Status::OK();
    }
    default:
      return lnode->AnalyzeStatementBlock(sem_context);
  }
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
