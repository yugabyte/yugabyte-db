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
// Treenode implementation for INSERT statements.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/ptree/pt_explain.h"

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTExplainStmt::PTExplainStmt(MemoryContext *memctx,
                             YBLocation::SharedPtr loc,
                             TreeNode::SharedPtr stmt)
    : stmt_(stmt) {
}

PTExplainStmt::~PTExplainStmt() {
}

Status PTExplainStmt::Analyze(SemContext *sem_context) {
  if (!stmt_->IsDml()) {
    return sem_context->Error(this, "Only DML statements can be explained",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Analyze explainable sub statement.
  return stmt_->Analyze(sem_context);
}

void PTExplainStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace ql
}  // namespace yb
