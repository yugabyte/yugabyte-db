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

#include "yb/yql/cql/ql/ptree/pt_delete.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTDeleteStmt::PTDeleteStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTExprListNode::SharedPtr target,
                           PTTableRef::SharedPtr relation,
                           PTDmlUsingClause::SharedPtr using_clause,
                           PTExpr::SharedPtr where_clause,
                           PTExpr::SharedPtr if_clause)
    : PTDmlStmt(memctx, loc, where_clause, if_clause, using_clause),
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

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));
  bool range_key_missing = key_where_ops_.size() < num_key_columns_;

  column_args_->resize(num_columns());

  // If target columns are given, range key can be omitted only if all columns targeted for
  // deletions are static. Then we must also check there are no extra conditions on the range
  // columns (e.g. inequality conditions).
  // Otherwise, (if no target columns are given) range key can omitted (implying a range delete)
  // only if there is no 'IF' clause (not allowed for range deletes).
  if (target_) {
    deleting_only_static_cols_ = true;
    TreeNodePtrOperator<SemContext> analyze =
        std::bind(&PTDeleteStmt::AnalyzeTarget, this, std::placeholders::_1, std::placeholders::_2);
        RETURN_NOT_OK(target_->Analyze(sem_context, analyze));

    if (range_key_missing) {
      if (!deleting_only_static_cols_) {
        return sem_context->Error(this,
            "DELETE statement must give the entire primary key if specifying non-static columns",
            ErrorCode::CQL_STATEMENT_INVALID);
      }
      if (!where_ops_.empty()) {
        return sem_context->Error(this,
            "DELETE statement cannot specify both target columns and range condition",
            ErrorCode::CQL_STATEMENT_INVALID);
      }
    }
  } else if (range_key_missing && if_clause_ != nullptr) {
    return sem_context->Error(this,
        "DELETE statement must specify the entire primary key to use an IF clause",
        ErrorCode::CQL_STATEMENT_INVALID);
  }

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context, if_clause_));

  // Run error checking on USING clause.
  RETURN_NOT_OK(AnalyzeUsingClause(sem_context));

  // Analyze indexes for write operations.
  RETURN_NOT_OK(AnalyzeIndexesForWrites(sem_context));

  // Analyze for inter-statement dependency.
  RETURN_NOT_OK(AnalyzeInterDependency(sem_context));

  if (using_clause_ != nullptr && using_clause_->has_ttl_seconds()) {
    // Delete only supports TIMESTAMP as part of the using clause.
    return sem_context->Error(this, "DELETE statement cannot have TTL",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  return Status::OK();
}

CHECKED_STATUS PTDeleteStmt::AnalyzeTarget(TreeNode *target, SemContext *sem_context) {
  // Walking through the target expressions and collect all columns. Currently, CQL doesn't allow
  // any expression except for references to table column.
  if (target->opcode() != TreeNodeOpcode::kPTRef) {
    return sem_context->Error(target, "Deleting expression is not allowed in CQL",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  PTRef *ref = static_cast<PTRef *>(target);

  if (ref->name() == nullptr) { // This ref is pointing to the whole table (DELETE *)
    return sem_context->Error(target, "Deleting '*' is not allowed in this context",
                              ErrorCode::CQL_STATEMENT_INVALID);
  } else { // Add the column descriptor to column_args.
    SemState sem_state(sem_context);
    RETURN_NOT_OK(ref->Analyze(sem_context));
    const ColumnDesc *col_desc = ref->desc();
    if (col_desc->is_primary()) {
      return sem_context->Error(target, "Delete target cannot be part of primary key",
                                ErrorCode::INVALID_ARGUMENTS);
    }
    if (!col_desc->is_static()) {
      deleting_only_static_cols_ = false;
    }

    // Set rhs expr to nullptr, since it is delete.
    column_args_->at(col_desc->index()).Init(col_desc, nullptr);
  }
  return Status::OK();
}

void PTDeleteStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace ql
}  // namespace yb
