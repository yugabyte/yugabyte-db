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
// Treenode definitions for TRUNCATE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/ql/ptree/pt_truncate.h"
#include "yb/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

PTTruncateStmt::PTTruncateStmt(MemoryContext *memctx,
                               YBLocation::SharedPtr loc,
                               PTQualifiedNameListNode::SharedPtr names)
    : TreeNode(memctx, loc), names_(names) {
}

PTTruncateStmt::~PTTruncateStmt() {
}

Status PTTruncateStmt::Analyze(SemContext *sem_context) {
  if (names_->size() > 1) {
    return sem_context->Error(names_, "Only one table name is allowed in a truncate statement",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Processing table name.
  bool is_system_ignored;
  RETURN_NOT_OK(name()->AnalyzeName(sem_context, OBJECT_TABLE));
  return sem_context->LookupTable(yb_table_name(), name()->loc(), true /* write_table */,
                                  &table_, &is_system_ignored);
}

void PTTruncateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n TRUNCATE "
          << yb_table_name().ToString();
}

}  // namespace ql
}  // namespace yb
