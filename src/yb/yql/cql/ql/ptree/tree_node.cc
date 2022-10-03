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

#include "yb/yql/cql/ql/ptree/tree_node.h"

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// TreeNode base class.
//--------------------------------------------------------------------------------------------------

TreeNode::TreeNode(MemoryContext *memctx, YBLocationPtr loc)
    : MCBase(memctx), loc_(loc) {
}

TreeNode::~TreeNode() {
}

// Run semantics analysis on this node.
Status TreeNode::Analyze(SemContext *sem_context) {
  // Raise unsupported error when a treenode does not implement this method.
  return sem_context->Error(this, ErrorCode::FEATURE_NOT_SUPPORTED);
}

}  // namespace ql
}  // namespace yb
