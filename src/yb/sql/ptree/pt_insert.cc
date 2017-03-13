//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode implementation for INSERT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/sem_context.h"
#include "yb/sql/ptree/pt_insert.h"
#include "yb/client/schema-internal.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

//--------------------------------------------------------------------------------------------------

PTInsertStmt::PTInsertStmt(MemoryContext *memctx,
                           YBLocation::SharedPtr loc,
                           PTQualifiedName::SharedPtr relation,
                           PTQualifiedNameListNode::SharedPtr columns,
                           PTCollection::SharedPtr value_clause,
                           PTExpr::SharedPtr if_clause,
                           PTConstInt::SharedPtr ttl_seconds)
    : PTDmlStmt(memctx, loc, false, ttl_seconds),
      relation_(relation),
      columns_(columns),
      value_clause_(value_clause),
      if_clause_(if_clause) {
}

PTInsertStmt::~PTInsertStmt() {
}

// TODO(Mihnea) Some where in this function, we must call expr->Analyze() even if it is a const.
CHECKED_STATUS PTInsertStmt::Analyze(SemContext *sem_context) {

  RETURN_NOT_OK(PTDmlStmt::Analyze(sem_context));

  // Get table descriptor.
  RETURN_NOT_OK(relation_->Analyze(sem_context));

  RETURN_NOT_OK(LookupTable(sem_context));
  const int num_cols = num_columns();

  // Check the selected columns. Cassandra only supports inserting one tuple / row at a time.
  PTValues *value_clause = static_cast<PTValues *>(value_clause_.get());
  if (value_clause->TupleCount() == 0) {
    return sem_context->Error(value_clause_->loc(), ErrorCode::TOO_FEW_ARGUMENTS);
  }
  const MCList<PTExpr::SharedPtr>& exprs = value_clause->Tuple(0)->node_list();
  for (const auto& const_expr : exprs) {
    RETURN_NOT_OK(const_expr->AnalyzeRhsExpr(sem_context));
  }

  int idx = 0;
  column_args_->resize(num_cols);
  if (columns_) {
    // Mismatch between column names and their values.
    const MCList<PTQualifiedName::SharedPtr>& names = columns_->node_list();
    if (names.size() != exprs.size()) {
      if (names.size() > exprs.size()) {
        return sem_context->Error(value_clause_->loc(), ErrorCode::TOO_FEW_ARGUMENTS);
      } else {
        return sem_context->Error(value_clause_->loc(), ErrorCode::TOO_MANY_ARGUMENTS);
      }
    }

    // Mismatch between arguments and columns.
    MCList<PTExpr::SharedPtr>::const_iterator iter = exprs.begin();
    for (PTQualifiedName::SharedPtr name : names) {
      const ColumnDesc *col_desc = sem_context->GetColumnDesc(name->last_name());

      // Check that the column exists.
      if (col_desc == nullptr) {
        return sem_context->Error(name->loc(), ErrorCode::UNDEFINED_COLUMN);
      }

      // Check that the datatypes are convertible.
      if (!sem_context->IsConvertible(col_desc->sql_type(), (*iter)->sql_type())) {
        return sem_context->Error((*iter)->loc(), ErrorCode::DATATYPE_MISMATCH);
      }

      // Check that the given column is not a duplicate and initialize the argument entry.
      idx = col_desc->index();
      if (column_args_->at(idx).IsInitialized()) {
        return sem_context->Error((*iter)->loc(), ErrorCode::DUPLICATE_COLUMN);
      }
      column_args_->at(idx).Init(col_desc, *iter);
      iter++;
    }
  } else {
    // Check number of arguments.
    if (exprs.size() != num_cols) {
      if (exprs.size() > num_cols) {
        return sem_context->Error(value_clause_->loc(), ErrorCode::TOO_MANY_ARGUMENTS);
      } else {
        return sem_context->Error(value_clause_->loc(), ErrorCode::TOO_FEW_ARGUMENTS);
      }
    }

    // Check that the argument datatypes are convertible with all columns.
    idx = 0;
    for (const auto& expr : exprs) {
      ColumnDesc *col_desc = &table_columns_[idx];
      if (!sem_context->IsConvertible(col_desc->sql_type(), expr->sql_type())) {
        return sem_context->Error(expr->loc(), ErrorCode::DATATYPE_MISMATCH);
      }

      // Initialize the argument entry.
      column_args_->at(idx).Init(col_desc, expr);
      idx++;
    }
  }

  // Now check that each column in primary key is associated with an argument.
  // NOTE: we assumed that primary_indexes and arguments are sorted by column_index.
  int num_keys = num_key_columns();
  for (idx = 0; idx < num_keys; idx++) {
    if (!column_args_->at(idx).IsInitialized()) {
      return sem_context->Error(value_clause_->loc(), ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY);
    }
    if (column_args_->at(idx).expr()->is_null()) {
      return sem_context->Error(value_clause_->loc(), ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY);
    }
  }

  // Run error checking on the IF conditions.
  RETURN_NOT_OK(AnalyzeIfClause(sem_context, if_clause_));

  // Run error checking on USING clause.
  RETURN_NOT_OK(AnalyzeUsingClause(sem_context));

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
              << ", Expected Type: " << col_desc->type_id()
              << ", Expr Type: " << arg.expr()->sql_type();
    }
  }
}

}  // namespace sql
}  // namespace yb
