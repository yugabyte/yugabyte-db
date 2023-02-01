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

#include "yb/common/common.pb.h"

#include "yb/gutil/casts.h"

#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_dml_using_clause.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/sem_state.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

using std::max;

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTDeleteStmt::PTDeleteStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTExprListNode::SharedPtr target,
                           PTTableRef::SharedPtr relation,
                           PTDmlUsingClausePtr using_clause,
                           PTExprPtr where_clause,
                           PTExprPtr if_clause,
                           const bool else_error,
                           const bool returns_status)
    : PTDmlStmt(memctx, loc, where_clause, if_clause, else_error, using_clause, returns_status),
      target_(target),
      relation_(relation) {
}

PTDeleteStmt::~PTDeleteStmt() {
}

Status PTDeleteStmt::Analyze(SemContext *sem_context) {
  // If use_cassandra_authentication is set, permissions are checked in PTDmlStmt::Analyze.
  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  RETURN_NOT_OK(relation_->Analyze(sem_context));

  // Collect table's schema for semantic analysis.
  RETURN_NOT_OK(LookupTable(sem_context));

  // Analyze the target columns.
  if (target_) {
    column_args_->resize(num_columns());
    TreeNodePtrOperator<SemContext> analyze =
        std::bind(&PTDeleteStmt::AnalyzeTarget, this, std::placeholders::_1, std::placeholders::_2);
    RETURN_NOT_OK(target_->Analyze(sem_context, analyze));
  }

  // Analyze column args to set if primary and/or static row is modified.
  RETURN_NOT_OK(AnalyzeColumnArgs(sem_context));

  // Run error checking on the WHERE conditions.
  RETURN_NOT_OK(AnalyzeWhereClause(sem_context));
  bool range_key_missing = key_where_ops_.size() < num_key_columns();

  // If target columns are given, range key can be omitted only if all columns targeted for
  // deletions are static. Then we must also check there are no extra conditions on the range
  // columns (e.g. inequality conditions).
  // Otherwise, (if no target columns are given) range key can omitted (implying a range delete)
  // only if there is no 'IF' clause (not allowed for range deletes).
  if (target_) {
    if (range_key_missing) {
      if (!StaticColumnArgsOnly()) {
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
  } else if (range_key_missing) {
    if (if_clause_ != nullptr) {
      return sem_context->Error(this,
          "DELETE statement must specify the entire primary key to use an IF clause",
          ErrorCode::CQL_STATEMENT_INVALID);
    }
    // This is a range delete, affecting an entire hash key.
    modifies_multiple_rows_ = true;
  }

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context));

  // Run error checking on USING clause.
  RETURN_NOT_OK(AnalyzeUsingClause(sem_context));

  // Analyze indexes for write operations.
  RETURN_NOT_OK(AnalyzeIndexesForWrites(sem_context));

  if (using_clause_ != nullptr && using_clause_->has_ttl_seconds()) {
    // Delete only supports TIMESTAMP as part of the using clause.
    return sem_context->Error(this, "DELETE statement cannot have TTL",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  // If returning a status we always return back the whole row.
  if (returns_status_) {
    AddRefForAllColumns();
  }

  return Status::OK();
}

Status PTDeleteStmt::AnalyzeTarget(TreeNode *target, SemContext *sem_context) {
  // Walking through the target expressions and collect all columns
  switch (target->opcode()) {
    case TreeNodeOpcode::kPTRef: {
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

        // Set rhs expr to nullptr, since it is delete.
        column_args_->at(col_desc->index()).Init(col_desc, nullptr);
      }

      break;
    }
    case TreeNodeOpcode::kPTSubscript: {
      PTSubscriptedColumn *subscriptedRef = static_cast<PTSubscriptedColumn *>(target);
      RETURN_NOT_OK(subscriptedRef->Analyze(sem_context));

      // Set the subscripted_col_args to nullptr, since it is a delete operation.
      const ColumnDesc *col_desc = subscriptedRef->desc();
      subscripted_col_args_->emplace_back(col_desc, subscriptedRef->args(), nullptr);
      break;
    }
    default:
      return sem_context->Error(
          target, "Deleting expression is not allowed in CQL", ErrorCode::CQL_STATEMENT_INVALID);
  }

  return Status::OK();
}

void PTDeleteStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

ExplainPlanPB PTDeleteStmt::AnalysisResultToPB() {
  ExplainPlanPB explain_plan;
  DeletePlanPB *delete_plan = explain_plan.mutable_delete_plan();
  delete_plan->set_delete_type("Delete on " + table_name().ToString());
  if (modifies_multiple_rows_) {
    delete_plan->set_scan_type("  ->  Range Scan on " + table_name().ToString());
  } else {
    delete_plan->set_scan_type("  ->  Primary Key Lookup on " + table_name().ToString());
  }
  std::string key_conditions = "        Key Conditions: " + ConditionsToString(key_where_ops());
  delete_plan->set_key_conditions(key_conditions);
  if (!where_ops().empty()) {
    std::string filter = "        Filter: " + ConditionsToString(where_ops());
    delete_plan->set_filter(filter);
  }
  delete_plan->set_output_width(narrow_cast<int32_t>(max({
    delete_plan->delete_type().length(),
    delete_plan->scan_type().length(),
    delete_plan->key_conditions().length(),
    delete_plan->filter().length()
  })));
  return explain_plan;
}

}  // namespace ql
}  // namespace yb
