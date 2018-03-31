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

#include "yb/yql/pgsql/ptree/pg_compile_context.h"
#include "yb/yql/pgsql/ptree/pg_tinsert.h"

#include "yb/client/client.h"
#include "yb/client/schema-internal.h"

namespace yb {
namespace pgsql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

PgTInsertStmt::PgTInsertStmt(MemoryContext *memctx,
                             PgTLocation::SharedPtr loc,
                             PgTQualifiedName::SharedPtr relation,
                             PgTQualifiedNameListNode::SharedPtr columns,
                             PgTCollection::SharedPtr value_clause)
    : PgTDmlStmt(memctx, loc, nullptr /* where_clause */),
      relation_(relation),
      columns_(columns),
      value_clause_(value_clause) {
}

PgTInsertStmt::~PgTInsertStmt() {
}

CHECKED_STATUS PgTInsertStmt::Analyze(PgCompileContext *compile_context) {
  RETURN_NOT_OK(PgTDmlStmt::Analyze(compile_context));

  // Get table descriptor.
  relation_->set_object_type(OBJECT_TABLE);
  RETURN_NOT_OK(relation_->Analyze(compile_context));
  RETURN_NOT_OK(LookupTable(compile_context));

  // Check the selected columns. Cassandra only supports inserting one tuple / row at a time.
  const int num_cols = num_columns();
  PgTValues *value_clause = static_cast<PgTValues *>(value_clause_.get());
  if (value_clause->TupleCount() == 0) {
    return compile_context->Error(value_clause_, ErrorCode::TOO_FEW_ARGUMENTS);
  }

  // Process arguments.
  int idx = 0;
  column_args_->resize(num_cols);

  // Insert value 0 to system-defined column "OID".
  const ColumnDesc *col_desc = compile_context->GetColumnDesc(oid_name_);
  idx = col_desc->index();
  column_args_->at(idx).Init(col_desc, oid_arg_);

  // Insert now() to system-defined column "CTID".
  {
    col_desc = compile_context->GetColumnDesc(ctid_name_);
    PgSemState ctid_state(compile_context,
                          col_desc->ql_type(),
                          col_desc->internal_type(),
                          col_desc);
    idx = col_desc->index();
    RETURN_NOT_OK(ctid_arg_->Analyze(compile_context));
    column_args_->at(idx).Init(col_desc, ctid_arg_);
  }

  // Now, setup values for the user-defined column.
  const MCList<PgTExpr::SharedPtr>& exprs = value_clause->Tuple(0)->node_list();
  if (columns_) {
    // Processing insert statement that has column list.
    //   INSERT INTO <table>(names) VALUES(exprs).
    const MCList<PgTQualifiedName::SharedPtr>& names = columns_->node_list();
    if (names.size() != exprs.size()) {
      // Mismatch between number column names and their associated values.
      if (names.size() > exprs.size()) {
        return compile_context->Error(value_clause_, ErrorCode::TOO_FEW_ARGUMENTS);
      } else {
        return compile_context->Error(value_clause_, ErrorCode::TOO_MANY_ARGUMENTS);
      }
    }

    // Check mismatch between arguments and columns.
    MCList<PgTExpr::SharedPtr>::const_iterator iter = exprs.begin();
    for (PgTQualifiedName::SharedPtr name : names) {
      // Check that the column of given name exists.
      name->set_object_type(OBJECT_COLUMN);
      RETURN_NOT_OK(name->Analyze(compile_context));
      col_desc = compile_context->GetColumnDesc(name->last_name());
      if (col_desc == nullptr) {
        return compile_context->Error(name, ErrorCode::UNDEFINED_COLUMN);
      }

      // Process column arguments.
      const PgTExpr::SharedPtr& expr = *iter;
      PgSemState sem_state(compile_context,
                           col_desc->ql_type(),
                           col_desc->internal_type(),
                           col_desc);
      RETURN_NOT_OK(expr->Analyze(compile_context));
      RETURN_NOT_OK(expr->CheckRhsExpr(compile_context));

      // Check that the given column is not a duplicate and initialize the argument entry.
      idx = col_desc->index();
      if (column_args_->at(idx).IsInitialized()) {
        return compile_context->Error(*iter, ErrorCode::DUPLICATE_COLUMN);
      }

      // Init the column, and advance to next argument.
      column_args_->at(idx).Init(col_desc, *iter);
      iter++;
    }

    for (idx = 1; idx < num_key_columns_; idx++) {
      if (!column_args_->at(idx).IsInitialized()) {
        return compile_context->Error(value_clause_, ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
      }
    }
  } else {
    // Processing insert statement that doesn't has column list.
    //   INSERT INTO <table> VALUES(exprs);
    if (exprs.size() != (num_cols - ColumnDesc::kHiddenColumnCount)) {
      // Wrong number of arguments.
      if (exprs.size() > num_cols - ColumnDesc::kHiddenColumnCount) {
        return compile_context->Error(value_clause_, ErrorCode::TOO_MANY_ARGUMENTS);
      } else {
        return compile_context->Error(value_clause_, ErrorCode::TOO_FEW_ARGUMENTS);
      }
    }

    // Skip the first two columns, which is saved for column "OID" and "CTID".
    int expr_order = ColumnDesc::kHiddenColumnCount;
    for (const auto& expr : exprs) {
      ColumnDesc *col_desc = nullptr;
      for (idx = 0; idx < num_cols; idx++) {
        col_desc = &table_columns_[idx];
        if (col_desc->order() == expr_order)
          break;
      }
      CHECK_LT(idx, num_cols) << "Column at position " << expr_order << "not found";

      // Process values arguments.
      PgSemState sem_state(compile_context,
                           col_desc->ql_type(),
                           col_desc->internal_type(),
                           col_desc);
      RETURN_NOT_OK(expr->Analyze(compile_context));
      RETURN_NOT_OK(expr->CheckRhsExpr(compile_context));

      // Initialize the argument entry.
      CHECK_EQ(idx, col_desc->index()) << "Master indexing column out of order";
      if (column_args_->at(idx).IsInitialized()) {
        return compile_context->Error(expr, ErrorCode::DUPLICATE_COLUMN);
      }
      column_args_->at(idx).Init(col_desc, expr);

      // Next expression.
      expr_order++;
    }
  }

  // Primary key cannot be null.
  for (idx = 0; idx < num_key_columns_; idx++) {
    if (column_args_->at(idx).IsInitialized() && column_args_->at(idx).expr()->is_null()) {
      return compile_context->Error(value_clause_, ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }

  return Status::OK();
}

}  // namespace pgsql
}  // namespace yb
