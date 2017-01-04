//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode implementation for DML including SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/sem_context.h"
#include "yb/sql/ptree/pt_dml.h"

#include "yb/client/schema-internal.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

PTDmlStmt::PTDmlStmt(MemoryContext *memctx, YBLocation::SharedPtr loc, PTOptionExist option_exists)
  : PTCollection(memctx, loc),
    option_exists_(option_exists),
    table_columns_(memctx),
    num_key_columns_(0),
    num_hash_key_columns_(0),
    hash_where_ops_(memctx),
    where_ops_(memctx) {
}

PTDmlStmt::~PTDmlStmt() {
}

ErrorCode PTDmlStmt::LookupTable(SemContext *sem_context) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  const char *name = table_name();
  VLOG(3) << "Loading table descriptor for " << name;
  table_ = sem_context->GetTableDesc(name);
  if (table_ == nullptr) {
    err = ErrorCode::FDW_TABLE_NOT_FOUND;
    sem_context->Error(table_loc(), err);
    return err;
  }

  const YBSchema& schema = table_->schema();
  const int num_columns = schema.num_columns();
  num_key_columns_ = schema.num_key_columns();
  num_hash_key_columns_ = schema.num_hash_key_columns();

  table_columns_.resize(num_columns);
  for (int idx = 0; idx < num_columns; idx++) {
    // Find the column descriptor.
    const YBColumnSchema col = schema.Column(idx);
    table_columns_[idx].Init(idx,
                             schema.ColumnId(idx),
                             idx < num_hash_key_columns_,
                             idx < num_key_columns_,
                             col.type(),
                             ToInternalDataType(col.type()));

    // Insert the column descriptor to symbol table.
    MCString col_name(sem_context->PTreeMem(), col.name().c_str(), col.name().size());
    sem_context->MapSymbol(col_name, &table_columns_[idx]);
  }

  return err;
}

ErrorCode PTDmlStmt::AnalyzeWhereClause(SemContext *sem_context,
                                        const PTExpr::SharedPtr& where_clause) {

  MCVector<WhereSemanticStats> col_stats(sem_context->PTempMem());
  col_stats.resize(num_columns());

  // Analyze where expression.
  hash_where_ops_.resize(num_hash_key_columns_);
  ErrorCode err = AnalyzeWhereExpr(sem_context, where_clause.get(), &col_stats);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }

  // Make sure that all hash entries are referenced in where expression.
  for (int idx = 0; idx < num_hash_key_columns_; idx++) {
    if (!col_stats[idx].has_eq_) {
      err = ErrorCode::CQL_STATEMENT_INVALID;
      sem_context->Error(where_clause->loc(), "Missing partition key", err);
      break;
    }
  }

  return err;
}

ErrorCode PTDmlStmt::AnalyzeWhereExpr(SemContext *sem_context,
                                      PTExpr *expr,
                                      MCVector<WhereSemanticStats> *col_stats) {

  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;
  if (expr == nullptr) {
    return err;
  }

  PTPredicate2 *bool_expr = nullptr;
  const ColumnDesc *col_desc = nullptr;
  PTExpr::SharedPtr value;
  switch (expr->expr_op()) {
    case ExprOperator::kAND:
      bool_expr = static_cast<PTPredicate2*>(expr);
      err = AnalyzeWhereExpr(sem_context, bool_expr->op1().get(), col_stats);
      err = AnalyzeWhereExpr(sem_context, bool_expr->op2().get(), col_stats);
      break;

    case ExprOperator::kEQ: {
      err = AnalyzeWhereCompareExpr(sem_context, expr, &col_desc, &value);
      if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
        break;
      }
      if ((*col_stats)[col_desc->index()].has_eq_ ||
          (*col_stats)[col_desc->index()].has_lt_ ||
          (*col_stats)[col_desc->index()].has_gt_) {
        err = ErrorCode::CQL_STATEMENT_INVALID;
        sem_context->Error(expr->loc(), "Partition key is specified more than one time", err);
        break;
      }
      (*col_stats)[col_desc->index()].has_eq_ = true;

      // The condition to "where" operator list.
      if (col_desc->is_hash()) {
        hash_where_ops_[col_desc->index()].Init(
          col_desc, value, ExprOperator::kEQ, YSQLOperator::YSQL_OP_EQUAL);
      } else {
        ColumnOp col_op(col_desc, value, ExprOperator::kEQ, YSQLOperator::YSQL_OP_EQUAL);
        where_ops_.push_back(col_op);
      }
      break;
    }

    case ExprOperator::kLT: {
      err = AnalyzeWhereCompareExpr(sem_context, expr, &col_desc, &value);
      if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
        break;
      }
      if (col_desc->is_hash()) {
        err = ErrorCode::CQL_STATEMENT_INVALID;
        sem_context->Error(expr->loc(), "Partition column cannot be used in this context", err);
        break;
      }
      if ((*col_stats)[col_desc->index()].has_eq_ || (*col_stats)[col_desc->index()].has_lt_) {
        err = ErrorCode::CQL_STATEMENT_INVALID;
        sem_context->Error(expr->loc(), "Illogical range condition", err);
        break;
      }
      (*col_stats)[col_desc->index()].has_lt_ = true;

      // The condition to "where" operator list.
      ColumnOp col_op(col_desc, value, ExprOperator::kLT, YSQLOperator::YSQL_OP_LESS_THAN);
      where_ops_.push_back(col_op);
      break;
    }

    case ExprOperator::kGT: {
      err = AnalyzeWhereCompareExpr(sem_context, expr, &col_desc, &value);
      if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
        break;
      }
      if (col_desc->is_hash()) {
        err = ErrorCode::CQL_STATEMENT_INVALID;
        sem_context->Error(expr->loc(), "Partition column cannot be used in this context", err);
        break;
      }
      if ((*col_stats)[col_desc->index()].has_eq_ || (*col_stats)[col_desc->index()].has_gt_) {
        err = ErrorCode::CQL_STATEMENT_INVALID;
        sem_context->Error(expr->loc(), "Illogical range condition", err);
        break;
      }
      (*col_stats)[col_desc->index()].has_gt_ = true;

      // The condition to "where" operator list.
      ColumnOp col_op(col_desc, value, ExprOperator::kGT, YSQLOperator::YSQL_OP_GREATER_THAN);
      where_ops_.push_back(col_op);
      break;
    }

    default:
      LOG(FATAL) << "Illegal op = " << int(expr->expr_op());
      break;
  }

  // Check that if where clause is present, it must follow CQL rules.
  return err;
}

ErrorCode PTDmlStmt::AnalyzeWhereCompareExpr(SemContext *sem_context,
                                             PTExpr *expr,
                                             const ColumnDesc **col_desc,
                                             PTExpr::SharedPtr *value) {
  ErrorCode err = ErrorCode::SUCCESSFUL_COMPLETION;

  PTPredicate2 *bool_expr = static_cast<PTPredicate2*>(expr);
  expr = bool_expr->op1().get();
  if (expr->expr_op() != ExprOperator::kRef) {
    err = ErrorCode::CQL_STATEMENT_INVALID;
    sem_context->Error(expr->loc(), "Only column reference is allowed here", err);
    return err;
  }
  err = expr->Analyze(sem_context);
  if (err != ErrorCode::SUCCESSFUL_COMPLETION) {
    return err;
  }
  *col_desc = static_cast<PTRef*>(expr)->desc();

  expr = bool_expr->op2().get();
  if (expr->expr_op() != ExprOperator::kConst) {
    err = ErrorCode::CQL_STATEMENT_INVALID;
    sem_context->Error(expr->loc(), "Only literal value is allowed here", err);
    return err;
  }
  *value = bool_expr->op2();

  return err;
}

} // namespace sql
} // namespace yb
