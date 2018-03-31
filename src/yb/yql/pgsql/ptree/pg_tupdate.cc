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
// Treenode implementation for UPDATE statements.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pgsql/ptree/pg_tupdate.h"
#include "yb/yql/pgsql/ptree/pg_compile_context.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------

PgTAssign::PgTAssign(MemoryContext *memctx,
                   PgTLocation::SharedPtr loc,
                   const PgTQualifiedName::SharedPtr& lhs,
                   const PgTExpr::SharedPtr& rhs)
    : TreeNode(memctx, loc),
      lhs_(lhs),
      rhs_(rhs),
      col_desc_(nullptr) {
}

PgTAssign::~PgTAssign() {
}

CHECKED_STATUS PgTAssign::Analyze(PgCompileContext *compile_context) {
  PgSemState sem_state(compile_context);

  // Analyze left value (column name).
  sem_state.set_processing_assignee(true);
  lhs_->set_object_type(OBJECT_COLUMN);
  RETURN_NOT_OK(lhs_->Analyze(compile_context));
  col_desc_ = compile_context->GetColumnDesc(lhs_->last_name());
  if (col_desc_ == nullptr) {
    return compile_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }
  sem_state.set_processing_assignee(false);

  // Setup the expected datatypes, and analyze the rhs value.
  std::shared_ptr<QLType> curr_ytype = col_desc_->ql_type();
  InternalType curr_itype = col_desc_->internal_type();
  sem_state.SetExprState(curr_ytype, curr_itype, col_desc_);
  RETURN_NOT_OK(rhs_->Analyze(compile_context));
  RETURN_NOT_OK(rhs_->CheckRhsExpr(compile_context));

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgTUpdateStmt::PgTUpdateStmt(MemoryContext *memctx,
                             PgTLocation::SharedPtr loc,
                             PgTTableRef::SharedPtr relation,
                             PgTAssignListNode::SharedPtr set_clause,
                             PgTExpr::SharedPtr where_clause)
    : PgTDmlStmt(memctx, loc, where_clause),
      relation_(relation),
      set_clause_(set_clause) {
}

PgTUpdateStmt::~PgTUpdateStmt() {
}

CHECKED_STATUS PgTUpdateStmt::Analyze(PgCompileContext *compile_context) {
  RETURN_NOT_OK(PgTDmlStmt::Analyze(compile_context));

  RETURN_NOT_OK(relation_->Analyze(compile_context));

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(compile_context));

  PgSemState sem_state(compile_context);

  // Process set clause.
  sem_state.set_processing_set_clause(true);
  column_args_->resize(num_columns());
  TreeNodePtrOperator<PgCompileContext, PgTAssign> analyze =
    std::bind(&PgTUpdateStmt::AnalyzeSetExpr,
              this,
              std::placeholders::_1,
              std::placeholders::_2);
  RETURN_NOT_OK(set_clause_->Analyze(compile_context, analyze));
  sem_state.ResetContextState();

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(compile_context, where_clause_));

  return Status::OK();
}

CHECKED_STATUS PgTUpdateStmt::AnalyzeSetExpr(PgTAssign *assign_expr,
                                             PgCompileContext *compile_context) {
  // Analyze the expression.
  RETURN_NOT_OK(assign_expr->Analyze(compile_context));
  if (!require_column_read_ && assign_expr->require_column_read()) {
    require_column_read_ = true;
  }

  // Form the column args for protobuf.
  const ColumnDesc *col_desc = assign_expr->col_desc();
  column_args_->at(col_desc->index()).Init(col_desc, assign_expr->rhs());
  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
