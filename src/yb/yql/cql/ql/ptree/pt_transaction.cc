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
// Treenode implementation for TRANSACTION statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_transaction.h"

#include "yb/util/status.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTStartTransaction::PTStartTransaction(MemoryContext *memctx, YBLocationPtr loc)
    : TreeNode(memctx, loc) {
}

PTStartTransaction::~PTStartTransaction() {
}

Status PTStartTransaction::Analyze(SemContext *sem_context) {
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PTCommit::PTCommit(MemoryContext *memctx, YBLocationPtr loc)
    : TreeNode(memctx, loc) {
}

PTCommit::~PTCommit() {
}

Status PTCommit::Analyze(SemContext *sem_context) {
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
