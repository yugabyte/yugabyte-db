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

#include "yb/yql/cql/ql/ptree/pt_update.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTAssign::PTAssign(MemoryContext *memctx,
                   YBLocation::SharedPtr loc,
                   const PTQualifiedName::SharedPtr& lhs,
                   const PTExpr::SharedPtr& rhs,
                   const PTExprListNode::SharedPtr& subscript_args)
    : TreeNode(memctx, loc),
      lhs_(lhs),
      rhs_(rhs),
      subscript_args_(subscript_args),
      col_desc_(nullptr) {
}

PTAssign::~PTAssign() {
}

CHECKED_STATUS PTAssign::Analyze(SemContext *sem_context) {
  SemState sem_state(sem_context);

  sem_state.set_processing_assignee(true);

  // Analyze left value (column name).
  RETURN_NOT_OK(lhs_->Analyze(sem_context));
  if (!lhs_->IsSimpleName()) {
    return sem_context->Error(lhs_, "Qualified name not allowed for column reference",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }
  col_desc_ = sem_context->GetColumnDesc(lhs_->last_name());
  if (col_desc_ == nullptr) {
    return sem_context->Error(this, "Column doesn't exist", ErrorCode::UNDEFINED_COLUMN);
  }

  std::shared_ptr<QLType> curr_ytype = col_desc_->ql_type();
  InternalType curr_itype = col_desc_->internal_type();

  if (has_subscripted_column()) {
    for (const auto &arg : subscript_args_->node_list()) {
      if (curr_ytype->keys_type() == nullptr) {
        return sem_context->Error(this, "Columns with elementary types cannot take arguments",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }

      sem_state.SetExprState(curr_ytype->keys_type(),
                             client::YBColumnSchema::ToInternalDataType(curr_ytype->keys_type()));
      RETURN_NOT_OK(arg->Analyze(sem_context));

      curr_ytype = curr_ytype->values_type();
      curr_itype = client::YBColumnSchema::ToInternalDataType(curr_ytype);
    }
    // For "UPDATE ... SET list[x] = ...", the list needs to be read first in order to set an
    // element in it.
    if (col_desc_->ql_type()->main() == DataType::LIST) {
      sem_context->current_dml_stmt()->AddColumnRef(*col_desc_);
    }
  }

  sem_state.set_processing_assignee(false);

  // Setup the expected datatypes, and analyze the rhs value.
  sem_state.SetExprState(curr_ytype, curr_itype, lhs_->bindvar_name(), col_desc_);
  RETURN_NOT_OK(rhs_->Analyze(sem_context));
  RETURN_NOT_OK(rhs_->CheckRhsExpr(sem_context));

  return Status::OK();
}

void PTAssign::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

//--------------------------------------------------------------------------------------------------

PTUpdateStmt::PTUpdateStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTTableRef::SharedPtr relation,
                           PTAssignListNode::SharedPtr set_clause,
                           PTExpr::SharedPtr where_clause,
                           PTExpr::SharedPtr if_clause,
                           PTDmlUsingClause::SharedPtr using_clause)
    : PTDmlStmt(memctx, loc, where_clause, if_clause, using_clause),
      relation_(relation),
      set_clause_(set_clause) {
}

PTUpdateStmt::~PTUpdateStmt() {
}

CHECKED_STATUS PTUpdateStmt::Analyze(SemContext *sem_context) {
  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  RETURN_NOT_OK(relation_->Analyze(sem_context));

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  SemState sem_state(sem_context);
  // Run error checking on USING clause. Need to run this before analyzing the SET clause, so the
  // user supplied timestamp is filled in.
  RETURN_NOT_OK(AnalyzeUsingClause(sem_context));

  // Process set clause.
  column_args_->resize(num_columns());
  TreeNodePtrOperator<SemContext, PTAssign> analyze = std::bind(&PTUpdateStmt::AnalyzeSetExpr,
                                                                this,
                                                                std::placeholders::_1,
                                                                std::placeholders::_2);

  sem_state.set_processing_set_clause(true);
  RETURN_NOT_OK(set_clause_->Analyze(sem_context, analyze));
  sem_state.ResetContextState();

  // Set clause can't have primary keys.
  int num_keys = num_key_columns();
  for (int idx = 0; idx < num_keys; idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      return sem_context->Error(set_clause_, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context, where_clause_));

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context, if_clause_));

  return Status::OK();
}

CHECKED_STATUS PTUpdateStmt::AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context) {
  // Analyze the expression.
  RETURN_NOT_OK(assign_expr->Analyze(sem_context));

  if (assign_expr->col_desc()->ql_type()->IsCollection() &&
      using_clause_ != nullptr && using_clause_->has_user_timestamp_usec()) {
    return sem_context->Error(assign_expr, "UPDATE statement with collection and USING TIMESTAMP "
        "is not supported", ErrorCode::INVALID_ARGUMENTS);
  }

  if (!require_column_read_ && assign_expr->require_column_read()) {
    require_column_read_ = true;
  }

  // Form the column args for protobuf.
  const ColumnDesc *col_desc = assign_expr->col_desc();
  if (assign_expr->has_subscripted_column()) {
    subscripted_col_args_->emplace_back(col_desc,
                                        assign_expr->subscript_args(),
                                        assign_expr->rhs());
  } else {
    column_args_->at(col_desc->index()).Init(col_desc, assign_expr->rhs());
  }
  return Status::OK();
}

void PTUpdateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

}  // namespace ql
}  // namespace yb
