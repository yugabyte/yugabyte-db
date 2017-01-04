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
                           PTOptionExist option_exists)
    : PTDmlStmt(memctx, loc, option_exists),
      relation_(relation),
      columns_(columns),
      value_clause_(value_clause),
      column_args_(memctx) {
}

PTInsertStmt::~PTInsertStmt() {
}

ErrorCode PTInsertStmt::Analyze(SemContext *sem_context) {
  // Clear column_args_ as this call might be a reentrance due to metadata mismatch.
  column_args_.clear();

  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  // Get table descriptor.
  err = relation_->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  LookupTable(sem_context);
  const int num_cols = num_columns();

  // Check the selected columns. Cassandra only supports inserting one tuple / row at a time.
  PTValues *value_clause = static_cast<PTValues *>(value_clause_.get());
  if (value_clause->TupleCount() == 0) {
    err = ErrorCode::TOO_FEW_ARGUMENTS;
    sem_context->Error(value_clause_->loc(), err);
    return err;
  }
  const MCList<PTExpr::SharedPtr>& exprs = value_clause->Tuple(0)->node_list();
  for (const auto& const_expr : exprs) {
    if (const_expr->expr_op() != ExprOperator::kConst) {
      err = ErrorCode::CQL_STATEMENT_INVALID;
      sem_context->Error(loc(), "Only literal values are allowed in this context", err);
      sem_context->Error(const_expr->loc(), err);
    }
  }

  int idx = 0;
  column_args_.resize(num_cols);
  if (columns_) {
    // Mismatch between column names and their values.
    const MCList<PTQualifiedName::SharedPtr>& names = columns_->node_list();
    if (names.size() != exprs.size()) {
      if (names.size() > exprs.size()) {
        err = ErrorCode::TOO_FEW_ARGUMENTS;
      } else {
        err = ErrorCode::TOO_MANY_ARGUMENTS;
      }
      sem_context->Error(value_clause_->loc(), err);
      return err;
    }

    // Mismatch between arguments and columns.
    MCList<PTExpr::SharedPtr>::const_iterator iter = exprs.begin();
    for (PTQualifiedName::SharedPtr name : names) {
      const ColumnDesc *col_desc = sem_context->GetColumnDesc(name->last_name());

      // Check that the column exists.
      if (col_desc == nullptr) {
        err = ErrorCode::UNDEFINED_COLUMN;
        sem_context->Error(name->loc(), err);
        return err;
      }

      // Check that the datatypes are compatible.
      if (!sem_context->IsCompatible(col_desc->sql_type(), (*iter)->sql_type())) {
        err = ErrorCode::DATATYPE_MISMATCH;
        sem_context->Error((*iter)->loc(), err);
        return err;
      }

      // Check that the given column is not a duplicate and initialize the argument entry.
      idx = col_desc->index();
      if (column_args_[idx].IsInitialized()) {
        err = ErrorCode::DUPLICATE_COLUMN;
        sem_context->Error((*iter)->loc(), err);
        return err;
      }
      column_args_[idx].Init(col_desc, *iter);
      iter++;
    }
  } else {
    // Check number of arguments.
    if (exprs.size() != num_cols) {
      if (exprs.size() > num_cols) {
        err = ErrorCode::TOO_MANY_ARGUMENTS;
      } else {
        err = ErrorCode::TOO_FEW_ARGUMENTS;
      }
      sem_context->Error(value_clause_->loc(), err);
      return err;
    }

    // Check that the argument datatypes are compatible with all columns.
    idx = 0;
    for (const auto& expr : exprs) {
      ColumnDesc *col_desc = &table_columns_[idx];
      if (!sem_context->IsCompatible(col_desc->sql_type(), expr->sql_type())) {
        err = ErrorCode::DATATYPE_MISMATCH;
        sem_context->Error(expr->loc(), err);
        return err;
      }

      // Initialize the argument entry.
      column_args_[idx].Init(col_desc, expr);
      idx++;
    }
  }

  // Now check that each column in primary key is associated with an argument.
  // NOTE: we assumed that primary_indexes and arguments are sorted by column_index.
  int num_keys = num_key_columns();
  for (idx = 0; idx < num_keys; idx++) {
    if (!column_args_[idx].IsInitialized()) {
      err = ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY;
      sem_context->Error(value_clause_->loc(), err);
      return err;
    }
  }

  return err;
}

void PTInsertStmt::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):";
  for (const ColumnArg& arg : column_args_) {
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
