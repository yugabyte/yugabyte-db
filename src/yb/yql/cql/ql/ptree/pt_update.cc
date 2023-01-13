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

#include "yb/common/common.pb.h"
#include "yb/common/ql_type.h"

#include "yb/gutil/casts.h"

#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_dml_using_clause.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

using std::string;
using std::max;

DEFINE_RUNTIME_bool(
    ycql_bind_collection_assignment_using_column_name, false,
    "Enable using column name for binding the value of subscripted collection column");

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTAssign::PTAssign(MemoryContext *memctx,
                   YBLocationPtr loc,
                   const PTQualifiedName::SharedPtr& lhs,
                   const PTExprPtr& rhs,
                   const PTExprListNode::SharedPtr& subscript_args,
                   const PTExprListNode::SharedPtr& json_ops)
    : TreeNode(memctx, loc),
      lhs_(lhs),
      rhs_(rhs),
      subscript_args_(subscript_args),
      json_ops_(json_ops),
      col_desc_(nullptr) {
}

PTAssign::~PTAssign() {
}

Status PTAssign::Analyze(SemContext *sem_context) {
  SemState sem_state(sem_context);

  sem_state.set_processing_assignee(true);

  // Analyze left value (column name).
  RETURN_NOT_OK(lhs_->Analyze(sem_context));
  if (!lhs_->IsSimpleName()) {
    return sem_context->Error(lhs_, "Qualified name not allowed for column reference",
                              ErrorCode::SQL_STATEMENT_INVALID);
  }
  col_desc_ = sem_context->current_dml_stmt()->GetColumnDesc(sem_context, lhs_->last_name());
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
      string subscripted_column_bindvar_name;
      switch (col_desc_->ql_type()->main()) {
        case DataType::MAP:
          subscripted_column_bindvar_name = PTBindVar::coll_map_key_bindvar_name(col_desc_->name());
          break;
        case DataType::LIST:
          subscripted_column_bindvar_name =
              PTBindVar::coll_list_index_bindvar_name(col_desc_->name());
          break;
        default:
          subscripted_column_bindvar_name = PTBindVar::default_bindvar_name();
      }
      sem_state.set_bindvar_name(subscripted_column_bindvar_name);
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

  if (has_json_ops()) {
    if (!col_desc_->ql_type()->IsJson()) {
      return sem_context->Error(this, "Json ops only supported for json columns",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
    RETURN_NOT_OK(json_ops_->Analyze(sem_context));

    // We need to perform a read-modify-write for json updates.
    sem_context->current_dml_stmt()->AddColumnRef(*col_desc_);
  }

  sem_state.set_processing_assignee(false);

  auto rhs_bindvar_name = lhs_->bindvar_name();
  if (has_subscripted_column()) {
    // For "UPDATE ... SET map[x] = ? ..." or "UPDATE ... SET list[x] = ? ..." when GFlag
    // ycql_bind_collection_assignment_using_column_name is enabled where x is an integer:
    // 1. allow both "column_name" and "value(column_name)" as the bindvar name.
    // 2. "column_name" is the primary bindvar name since PreparedStatements can only support a
    // single name.
    //
    // Note that nested maps and lists are only allowed if they are frozen. A frozen collection
    // can't be updated, hence only checking the first node in the node_list is sufficient here.
    auto subscripted_bindvar_name = PTBindVar::coll_value_bindvar_name(col_desc_->name());
    if (subscript_args_->node_list().front()->expr_op() == ExprOperator::kConst &&
        FLAGS_ycql_bind_collection_assignment_using_column_name) {
      sem_state.add_alternate_bindvar_name(subscripted_bindvar_name);
    } else {
      rhs_bindvar_name = MCMakeShared<MCString>(
          sem_context->PSemMem(), subscripted_bindvar_name.data(), subscripted_bindvar_name.size());
    }
  }

  // Setup the expected datatypes, and analyze the rhs value.
  sem_state.SetExprState(curr_ytype, curr_itype, rhs_bindvar_name, col_desc_);
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
                           const bool else_error,
                           PTDmlUsingClause::SharedPtr using_clause,
                           const bool return_status,
                           PTDmlWritePropertyListNode::SharedPtr update_properties)
    : PTDmlStmt(memctx, loc, where_clause, if_clause, else_error, using_clause, return_status),
      relation_(relation),
      set_clause_(set_clause),
      update_properties_(update_properties) {
}

PTUpdateStmt::~PTUpdateStmt() {
}

Status PTUpdateStmt::Analyze(SemContext *sem_context) {
  // If use_cassandra_authentication is set, permissions are checked in PTDmlStmt::Analyze.
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
  sem_state.set_allowing_column_refs(true);
  RETURN_NOT_OK(set_clause_->Analyze(sem_context, analyze));
  sem_state.ResetContextState();

  // Set clause can't have primary keys.
  auto num_keys = num_key_columns();
  for (size_t idx = 0; idx < num_keys; idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      return sem_context->Error(set_clause_, ErrorCode::INVALID_ARGUMENTS);
    }
  }

  // Analyze column args to set if primary and/or static row is modified.
  RETURN_NOT_OK(AnalyzeColumnArgs(sem_context));

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context));

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context));

  // Analyze indexes for write operations.
  RETURN_NOT_OK(AnalyzeIndexesForWrites(sem_context));

  if (update_properties_ != nullptr) {
    RETURN_NOT_OK(update_properties_->Analyze(sem_context));
  }
  // If returning a status we always return back the whole row.
  if (returns_status_) {
    AddRefForAllColumns();
  }

  return Status::OK();
}

namespace {

Status MultipleColumnSetError(const ColumnDesc* const col_desc,
                              const PTAssign* const assign_expr,
                              SemContext* sem_context) {
  return sem_context->Error(
      assign_expr,
      strings::Substitute("Multiple incompatible setting of column $0.",
                          col_desc->name()).c_str(),
      ErrorCode::INVALID_ARGUMENTS);
}

} // anonymous namespace

Status PTUpdateStmt::AnalyzeSetExpr(PTAssign *assign_expr, SemContext *sem_context) {
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
    // Setting the same column twice, once with a subscripted arg and once as a regular set for the
    // entire column is not allowed.
    if (column_args_->at(col_desc->index()).IsInitialized()) {
      return MultipleColumnSetError(col_desc, assign_expr, sem_context);
    }
    subscripted_col_args_->emplace_back(col_desc,
                                        assign_expr->subscript_args(),
                                        assign_expr->rhs());
  } else if (assign_expr->has_json_ops()) {
    // Setting the same column twice, once with a json arg and once as a regular set for the
    // entire column is not allowed.
    if (column_args_->at(col_desc->index()).IsInitialized()) {
      return MultipleColumnSetError(col_desc, assign_expr, sem_context);
    }
    json_col_args_->emplace_back(col_desc,
                                 assign_expr->json_ops(),
                                 assign_expr->rhs());
  } else {
    // Setting the same column twice is not allowed.
    for (const auto& json_col_arg : *json_col_args_) {
      if (json_col_arg.desc()->id() == col_desc->id()) {
        return MultipleColumnSetError(col_desc, assign_expr, sem_context);
      }
    }
    for (const auto& subscripted_col_arg : *subscripted_col_args_) {
      if (subscripted_col_arg.desc()->id() == col_desc->id()) {
        return MultipleColumnSetError(col_desc, assign_expr, sem_context);
      }
    }
    if (column_args_->at(col_desc->index()).IsInitialized()) {
      return MultipleColumnSetError(col_desc, assign_expr, sem_context);
    }
    column_args_->at(col_desc->index()).Init(col_desc, assign_expr->rhs());
  }
  return Status::OK();
}

void PTUpdateStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

ExplainPlanPB PTUpdateStmt::AnalysisResultToPB() {
  ExplainPlanPB explain_plan;
  UpdatePlanPB *update_plan = explain_plan.mutable_update_plan();
  update_plan->set_update_type("Update on " + table_name().ToString());
  update_plan->set_scan_type("  ->  Primary Key Lookup on " + table_name().ToString());
  string key_conditions = "        Key Conditions: " +
      ConditionsToString<MCVector<ColumnOp>>(key_where_ops());
  update_plan->set_key_conditions(key_conditions);
  update_plan->set_output_width(narrow_cast<int32_t>(max({
    update_plan->update_type().length(),
    update_plan->scan_type().length(),
    update_plan->key_conditions().length()
  })));
  return explain_plan;
}

}  // namespace ql
}  // namespace yb
