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
// Treenode implementation for DELETE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/pt_delete.h"
#include "yb/sql/ptree/sem_context.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

PTDeleteStmt::PTDeleteStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTListNode::SharedPtr target,
                           PTTableRef::SharedPtr relation,
                           TreeNode::SharedPtr using_clause,
                           PTExpr::SharedPtr where_clause,
                           PTExpr::SharedPtr if_clause)
    : PTDmlStmt(memctx, loc, true, where_clause, if_clause),
      target_(target),
      relation_(relation) {
}

PTDeleteStmt::~PTDeleteStmt() {
}

CHECKED_STATUS PTDeleteStmt::Analyze(SemContext *sem_context) {

  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  RETURN_NOT_OK(relation_->Analyze(sem_context));

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  column_args_->resize(num_columns());

  if (target_) {
    TreeNodePtrOperator<SemContext> analyze = std::bind(&PTDeleteStmt::AnalyzeTarget, this,
                                                        std::placeholders::_1,
                                                        std::placeholders::_2);
      RETURN_NOT_OK(target_->Analyze(sem_context, analyze));
  }
  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context, if_clause_));

  return Status::OK();
}


CHECKED_STATUS PTDeleteStmt::AnalyzeTarget(TreeNode *target, SemContext *sem_context) {
  // Walking through the target expressions and collect all columns. Currently, CQL doesn't allow
  // any expression except for references to table column.
  if (target->opcode() != TreeNodeOpcode::kPTRef) {
    return sem_context->Error(target->loc(), "Deleting expression is not allowed in CQL",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  PTRef *ref = static_cast<PTRef *>(target);

  if (ref->name() == nullptr) { // This ref is pointing to the whole table (DELETE *)
    return sem_context->Error(target->loc(), "Deleting '*' is not allowed in this context",
                              ErrorCode::CQL_STATEMENT_INVALID);
  } else { // Add the column descriptor to column_args.
    SemState sem_state(sem_context);
    RETURN_NOT_OK(ref->Analyze(sem_context));
    const ColumnDesc *col_desc = ref->desc();
    if (col_desc->is_primary()) {
      return sem_context->Error(target->loc(), "Delete target cannot be part of primary key",
                                ErrorCode::INVALID_ARGUMENTS);
  }
    // Set rhs expr to nullptr, since it is delete.
    column_args_->at(col_desc->index()).Init(col_desc, nullptr);
  }
  return Status::OK();
}


void PTDeleteStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace sql
}  // namespace yb
