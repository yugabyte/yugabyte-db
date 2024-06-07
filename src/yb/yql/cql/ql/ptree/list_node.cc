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
// Implementation of a list of tree nodes.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/ptree/list_node.h"

#include "yb/client/schema.h"
#include "yb/client/table.h"
#include "yb/common/schema.h"
#include "yb/yql/cql/ql/ptree/pt_dml.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

Status PTListNode::AnalyzeStatementBlock(SemContext *sem_context) {
  // The only statement block we support currently is transaction block as either
  //   BEGIN TRANSACTION
  //     dml;
  //     ...
  //   END TRANSACTION;
  // or
  //   START TRANSACTION;
  //     dml;
  //     ...
  //   COMMIT;

  if (node_list().empty()) {
    return Status::OK();
  }

  const TreeNode::SharedPtr front = node_list().front();
  if (front->opcode() != TreeNodeOpcode::kPTStartTransaction) {
    return sem_context->Error(front,
                              "A transaction must be started at the beginning of a statement batch",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  const TreeNode::SharedPtr back = node_list().back();
  if (back->opcode() != TreeNodeOpcode::kPTCommit) {
    return sem_context->Error(back,
                              "A transaction must be committed at the end of a statement batch",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  for (TreeNode::SharedPtr tnode : node_list()) {
    const PTDmlStmt *dml = nullptr;
    if (tnode != front && tnode != back) {
      const auto opcode = tnode->opcode();
      if (opcode != TreeNodeOpcode::kPTInsertStmt &&
          opcode != TreeNodeOpcode::kPTUpdateStmt &&
          opcode != TreeNodeOpcode::kPTDeleteStmt) {
        return sem_context->Error(tnode,
                                  "Only insert, update, and delete statements are allowed in a "
                                  "statement batch",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      dml = static_cast<const PTDmlStmt*>(tnode.get());
    }
    RETURN_NOT_OK(tnode->Analyze(sem_context));
    if (dml != nullptr) {
      if (!dml->table()->schema().table_properties().is_transactional()) {
        return sem_context->Error(dml->table_loc(),
                                  "Transactions are not enabled in the table",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      if (dml->if_clause() != nullptr && !dml->else_error()) {
        return sem_context->Error(dml,
                                  "Execution of conditional DML statement in transaction block "
                                  "without ELSE ERROR is not supported yet",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      if (dml->returns_status()) {
        return sem_context->Error(dml,
                                  "Execution of statement in transaction block "
                                  "with RETURNS STATUS AS ROW is not supported yet",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
    }
  }
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
