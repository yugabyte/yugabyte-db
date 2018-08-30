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

#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/pt_insert.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

PTInsertStmt::PTInsertStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTQualifiedName::SharedPtr relation,
                           PTQualifiedNameListNode::SharedPtr columns,
                           PTCollection::SharedPtr value_clause,
                           PTExpr::SharedPtr if_clause,
                           const bool else_error,
                           PTDmlUsingClause::SharedPtr using_clause,
                           const bool returns_status)
    : PTDmlStmt(memctx, loc, nullptr /* where_clause */, if_clause, else_error, using_clause,
                returns_status),
      relation_(relation),
      columns_(columns),
      value_clause_(value_clause) {
}

PTInsertStmt::~PTInsertStmt() {
}

CHECKED_STATUS PTInsertStmt::Analyze(SemContext *sem_context) {

  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  // Get table descriptor.
  RETURN_NOT_OK(relation_->AnalyzeName(sem_context, OBJECT_TABLE));
  RETURN_NOT_OK(LookupTable(sem_context));
  if (table_->schema().table_properties().contain_counters()) {
    return sem_context->Error(relation_, ErrorCode::INSERT_TABLE_OF_COUNTERS);
  }

  // Check the selected columns. Cassandra only supports inserting one tuple / row at a time.
  const int num_cols = num_columns();
  PTValues *value_clause = static_cast<PTValues *>(value_clause_.get());
  if (value_clause->TupleCount() == 0) {
    return sem_context->Error(value_clause_, ErrorCode::TOO_FEW_ARGUMENTS);
  }

  int idx = 0;
  const MCList<PTExpr::SharedPtr>& exprs = value_clause->Tuple(0)->node_list();
  column_args_->resize(num_cols);
  if (columns_) {
    // Processing insert statement that has column list.
    //   INSERT INTO <table>(names) VALUES(exprs).
    const MCList<PTQualifiedName::SharedPtr>& names = columns_->node_list();
    if (names.size() != exprs.size()) {
      // Mismatch between number column names and their associated values.
      if (names.size() > exprs.size()) {
        return sem_context->Error(value_clause_, ErrorCode::TOO_FEW_ARGUMENTS);
      } else {
        return sem_context->Error(value_clause_, ErrorCode::TOO_MANY_ARGUMENTS);
      }
    }

    // Mismatch between arguments and columns.
    MCList<PTExpr::SharedPtr>::const_iterator iter = exprs.begin();
    for (PTQualifiedName::SharedPtr name : names) {
      if (!name->IsSimpleName()) {
        return sem_context->Error(name, "Qualified name not allowed for column reference",
                                  ErrorCode::SQL_STATEMENT_INVALID);
      }
      const ColumnDesc *col_desc = sem_context->GetColumnDesc(name->last_name());

      // Check that the column exists.
      if (col_desc == nullptr) {
        return sem_context->Error(name, ErrorCode::UNDEFINED_COLUMN);
      }

      // Process values arguments.
      const PTExpr::SharedPtr& expr = *iter;
      SemState sem_state(sem_context, col_desc->ql_type(), col_desc->internal_type(),
                         name->bindvar_name(), col_desc);
      RETURN_NOT_OK(expr->Analyze(sem_context));
      RETURN_NOT_OK(expr->CheckRhsExpr(sem_context));

      // Check that the given column is not a duplicate and initialize the argument entry.
      idx = col_desc->index();
      if (column_args_->at(idx).IsInitialized()) {
        return sem_context->Error(*iter, ErrorCode::DUPLICATE_COLUMN);
      }
      column_args_->at(idx).Init(col_desc, *iter);
      iter++;

      // If returning a status we always return back the whole row.
      if (returns_status_) {
        AddRefForAllColumns();
      }

    }
  } else {
    // This case is not yet supported as it's not CQL syntax.
    // Processing insert statement that doesn't has column list.
    //   INSERT INTO <table> VALUES(exprs);
    if (exprs.size() != num_cols) {
      // Wrong number of arguments.
      if (exprs.size() > num_cols) {
        return sem_context->Error(value_clause_, ErrorCode::TOO_MANY_ARGUMENTS);
      } else {
        return sem_context->Error(value_clause_, ErrorCode::TOO_FEW_ARGUMENTS);
      }
    }

    // If any of the arguments is a bind variable, set up its column description. Else check that
    // the argument datatypes are convertible with all columns.
    idx = 0;
    MCList<PTQualifiedName::SharedPtr>::const_iterator iter = columns_->node_list().cbegin();
    for (const auto& expr : exprs) {
      ColumnDesc *col_desc = &table_columns_[idx];

      // Process values arguments.
      SemState sem_state(sem_context, col_desc->ql_type(), col_desc->internal_type(),
                         (*iter)->bindvar_name(), col_desc);
      RETURN_NOT_OK(expr->Analyze(sem_context));
      RETURN_NOT_OK(expr->CheckRhsExpr(sem_context));

      // Initialize the argument entry.
      column_args_->at(idx).Init(col_desc, expr);
      idx++;
      iter++;
    }
  }

  // Now check that each column in the hash key is associated with an argument.
  // NOTE: we assumed that primary_indexes and arguments are sorted by column_index.
  for (idx = 0; idx < num_hash_key_columns(); idx++) {
    if (!column_args_->at(idx).IsInitialized()) {
      return sem_context->Error(value_clause_, ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }
  // If inserting static columns only, check that either each column in the range key is associated
  // with an argument or no range key has an argument. Else, check that all range columns
  // have arguments.
  int range_keys = 0;
  for (idx = num_hash_key_columns(); idx < num_key_columns(); idx++) {
    if (column_args_->at(idx).IsInitialized()) {
      range_keys++;
    }
  }

  // Analyze column args to set if primary and/or static row is modified.
  RETURN_NOT_OK(AnalyzeColumnArgs(sem_context));

  if (StaticColumnArgsOnly()) {
    if (range_keys != num_key_columns() - num_hash_key_columns() && range_keys != 0)
      return sem_context->Error(value_clause_, ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
  } else {
    if (range_keys != num_key_columns() - num_hash_key_columns())
      return sem_context->Error(value_clause_, ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
  }

  // Primary key cannot be null.
  for (idx = 0; idx < num_key_columns(); idx++) {
    if (column_args_->at(idx).IsInitialized() && column_args_->at(idx).expr()->is_null()) {
      return sem_context->Error(value_clause_, ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }

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

}  // namespace ql
}  // namespace yb
