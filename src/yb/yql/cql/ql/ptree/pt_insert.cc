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

#include "yb/yql/cql/ql/ptree/pt_insert.h"

#include "yb/client/table.h"

#include "yb/common/common.pb.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/yql/cql/ql/ptree/column_arg.h"
#include "yb/yql/cql/ql/ptree/column_desc.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/pt_insert_json_clause.h"
#include "yb/yql/cql/ql/ptree/pt_option.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTInsertStmt::PTInsertStmt(MemoryContext *memctx,
                           YBLocationPtr loc,
                           PTQualifiedName::SharedPtr relation,
                           PTQualifiedNameListNode::SharedPtr columns,
                           const PTCollection::SharedPtr& inserting_value,
                           PTExprPtr if_clause,
                           const bool else_error,
                           PTDmlUsingClausePtr using_clause,
                           const bool returns_status)
    : PTDmlStmt(memctx, loc, nullptr /* where_clause */, if_clause, else_error, using_clause,
                returns_status),
      relation_(relation),
      columns_(columns),
      inserting_value_(inserting_value) {
}

PTInsertStmt::~PTInsertStmt() = default;

Status PTInsertStmt::Analyze(SemContext *sem_context) {
  // If use_cassandra_authentication is set, permissions are checked in PTDmlStmt::Analyze.
  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  // Get table descriptor.
  RETURN_NOT_OK(relation_->AnalyzeName(sem_context, ObjectType::TABLE));
  RETURN_NOT_OK(LookupTable(sem_context));
  if (table_->schema().table_properties().contain_counters()) {
    return sem_context->Error(relation_, ErrorCode::INSERT_TABLE_OF_COUNTERS);
  }

  // Check the selected columns. Cassandra only supports inserting one tuple / row at a time.
  column_args_->resize(num_columns());

  RETURN_NOT_OK(AnalyzeInsertingValue(inserting_value_.get(), sem_context));

  // Analyze bind variables for hash columns in the INSERT list.
  RETURN_NOT_OK(AnalyzeHashColumnBindVars(sem_context));

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context));

  // Run error checking on USING clause.
  RETURN_NOT_OK(AnalyzeUsingClause(sem_context));

  // Analyze indexes for write operations.
  RETURN_NOT_OK(AnalyzeIndexesForWrites(sem_context));

  return Status::OK();
}

Status PTInsertStmt::AnalyzeInsertingValue(PTCollection* inserting_value,
                                           SemContext* sem_context) {
  RETURN_NOT_OK(inserting_value->Analyze(sem_context));
  if (auto values_clause = dynamic_cast<PTInsertValuesClause*>(inserting_value)) {
    return AnanlyzeValuesClause(values_clause, sem_context);
  } else if (auto json_clause = dynamic_cast<PTInsertJsonClause*>(inserting_value)) {
    return AnanlyzeJsonClause(json_clause, sem_context);
  } else {
    return STATUS(InternalError, "Clause can be either VALUES or JSON");
  }
}

Status PTInsertStmt::AnanlyzeValuesClause(PTInsertValuesClause* values_clause,
                                          SemContext* sem_context) {
  if (values_clause->TupleCount() == 0) {
    return sem_context->Error(values_clause, ErrorCode::TOO_FEW_ARGUMENTS);
  }
  const auto& value_exprs = values_clause->Tuple(0)->node_list();

  if (columns_) {
    // Processing insert statement that has column list.
    //   INSERT INTO <table>(names) VALUES(exprs).
    const MCList<PTQualifiedName::SharedPtr>& names = columns_->node_list();

    // Mismatch between number column names and their associated values.
    if (names.size() > value_exprs.size()) {
      return sem_context->Error(inserting_value_.get(), ErrorCode::TOO_FEW_ARGUMENTS);
    } else if (names.size() < value_exprs.size()) {
      return sem_context->Error(inserting_value_.get(), ErrorCode::TOO_MANY_ARGUMENTS);
    }

    // Mismatch between arguments and columns.
    auto value_exprs_iter = value_exprs.begin();
    for (const PTQualifiedName::SharedPtr& name : names) {
      if (!name->IsSimpleName()) {
        return sem_context->Error(name, "Qualified name not allowed for column reference",
                                  ErrorCode::SQL_STATEMENT_INVALID);
      }
      const ColumnDesc* col_desc = GetColumnDesc(sem_context, name->last_name());

      // Check that the column exists.
      if (col_desc == nullptr) {
        return sem_context->Error(name, ErrorCode::UNDEFINED_COLUMN);
      }

      // Process values arguments.
      const PTExprPtr& value_expr = *value_exprs_iter;
      RETURN_NOT_OK(ProcessColumn(name->bindvar_name(), col_desc, value_expr, sem_context));

      value_exprs_iter++;
    }

    RETURN_NOT_OK(InitRemainingColumns(false, sem_context));
  } else {
    // This case is not yet supported as it's not CQL syntax.
    // Processing insert statement that doesn't has column list.
    //   INSERT INTO <table> VALUES(exprs);

    // Wrong number of arguments.
    if (value_exprs.size() > num_columns()) {
      return sem_context->Error(inserting_value_.get(), ErrorCode::TOO_MANY_ARGUMENTS);
    } else if (value_exprs.size() < num_columns()) {
      return sem_context->Error(inserting_value_.get(), ErrorCode::TOO_FEW_ARGUMENTS);
    }

    // If any of the arguments is a bind variable, set up its column description. Else check that
    // the argument datatypes are convertible with all columns.
    MCList<PTQualifiedName::SharedPtr>::const_iterator col_iter =
        columns_->node_list().cbegin();
    MCColumnMap::const_iterator col_desc_iter =
        column_map_.cbegin();
    for (const auto& value_expr : value_exprs) {
      const ColumnDesc *col_desc = &col_desc_iter->second;
      RETURN_NOT_OK(ProcessColumn((*col_iter)->bindvar_name(), col_desc, value_expr, sem_context));
      col_iter++;
      col_desc_iter++;
    }
  }

  // If returning a status we always return back the whole row.
  if (returns_status_) {
    AddRefForAllColumns();
  }

  // Now check that each column in the hash key is associated with an argument.
  // NOTE: we assumed that primary_indexes and arguments are sorted by column_index.
  for (size_t idx = 0; idx < num_hash_key_columns(); idx++) {
    if (!(*column_args_)[idx].IsInitialized()) {
      return sem_context->Error(inserting_value_.get(),
                                ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }
  // If inserting static columns only, check that either each column in the range key is associated
  // with an argument or no range key has an argument. Else, check that all range columns
  // have arguments.
  size_t range_keys = 0;
  for (auto idx = num_hash_key_columns(); idx < num_key_columns(); idx++) {
    if ((*column_args_)[idx].IsInitialized()) {
      range_keys++;
    }
  }

  // Analyze column args to set if primary and/or static row is modified.
  RETURN_NOT_OK(AnalyzeColumnArgs(sem_context));

  if (StaticColumnArgsOnly()) {
    if (range_keys != num_key_columns() - num_hash_key_columns() && range_keys != 0)
      return sem_context->Error(inserting_value_.get(),
                                ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
  } else {
    if (range_keys != num_key_columns() - num_hash_key_columns())
      return sem_context->Error(inserting_value_.get(),
                                ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
  }

  // Primary key cannot be null.
  for (size_t idx = 0; idx < num_key_columns(); idx++) {
    if ((*column_args_)[idx].IsInitialized() && (*column_args_)[idx].expr()->is_null()) {
      return sem_context->Error(inserting_value_.get(), ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }

  return Status::OK();
}

Status PTInsertStmt::AnanlyzeJsonClause(PTInsertJsonClause* json_clause,
                                        SemContext* sem_context) {
  // Since JSON could be a PTBindVar, at this stage we don't have a clue about a JSON we've got
  // other than its type is a string.
  // However, INSERT JSON should initialize all non-mentioned columns to NULLs
  // (prevented by appending DEFAULT UNSET)
  RETURN_NOT_OK(InitRemainingColumns(json_clause->IsDefaultNull(), sem_context));
  RETURN_NOT_OK(json_clause->Analyze(sem_context));
  AddRefForAllColumns();
  return Status::OK();
}

Status PTInsertStmt::ProcessColumn(const MCSharedPtr<MCString>& mc_col_name,
                                   const ColumnDesc* col_desc,
                                   const PTExpr::SharedPtr& value_expr,
                                   SemContext* sem_context) {
  SemState sem_state(sem_context, col_desc->ql_type(), col_desc->internal_type(),
                     mc_col_name, col_desc);

  RETURN_NOT_OK(value_expr->Analyze(sem_context));
  RETURN_NOT_OK(value_expr->CheckRhsExpr(sem_context));

  ColumnArg& col = (*column_args_)[col_desc->index()];

  // Check that the given column is not a duplicate and initialize the argument entry.
  // (Note that INSERT JSON allows duplicate column names, latter value taking priority -
  // but we're not processing INSERT JSON here)
  if (col.IsInitialized()) {
    return sem_context->Error(value_expr, ErrorCode::DUPLICATE_COLUMN);
  }

  col.Init(col_desc, value_expr);
  return Status::OK();
}


// For INSERT VALUES, default behaviour is to not modify missing columns
// For INSERT JSON,   default behaviour is to replace missing columns with nulls - that is, unless
// DEFAULT UNSET is specified
Status PTInsertStmt::InitRemainingColumns(bool init_to_null,
                                          SemContext* sem_context) {
  if (!init_to_null) {
    // Not much we can do here
    return Status::OK();
  }
  // TODO: Use PB and avoid modifying parse tree?
  const PTExpr::SharedPtr null_expr = PTNull::MakeShared(sem_context->PTreeMem(), loc_, nullptr);

  for (auto& col_iter : column_map_) {
    ColumnDesc* col_desc = &col_iter.second;
    ColumnArg& col = (*column_args_)[col_desc->index()];
    if (!col.IsInitialized()) {
      const MCSharedPtr<MCString>& mc_col_name =
          MCMakeShared<MCString>(sem_context->PTempMem(), col_iter.first.c_str());
      RETURN_NOT_OK(ProcessColumn(mc_col_name, col_desc, null_expr, sem_context));
    }
  }
  return Status::OK();
}

void PTInsertStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):";
  for (const ColumnArg& arg : *column_args_) {
    if (arg.IsInitialized()) {
      const ColumnDesc *col_desc = arg.desc();
      VLOG(3) << "ARG: " << col_desc->id()
              << ", Hash: " << col_desc->is_hash()
              << ", Primary: " << col_desc->is_primary()
              << ", Expected Type: " << col_desc->ql_type()->ToString()
              << ", Expr Type: " << arg.expr()->ql_type_id();
    }
  }
}

ExplainPlanPB PTInsertStmt::AnalysisResultToPB() {
  ExplainPlanPB explain_plan;
  InsertPlanPB *insert_plan = explain_plan.mutable_insert_plan();
  insert_plan->set_insert_type("Insert on " + table_name().ToString());
  insert_plan->set_output_width(narrow_cast<int32_t>(insert_plan->insert_type().length()));
  return explain_plan;
}

}  // namespace ql
}  // namespace yb
