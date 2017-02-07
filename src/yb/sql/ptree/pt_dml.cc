//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Treenode implementation for DML including SELECT statements.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ptree/sem_context.h"
#include "yb/sql/ptree/pt_dml.h"

#include "yb/client/schema-internal.h"
#include "yb/common/ttl_constants.h"

namespace yb {
namespace sql {

using client::YBSchema;
using client::YBTable;
using client::YBTableType;
using client::YBColumnSchema;

PTDmlStmt::PTDmlStmt(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     bool write_only,
                     PTConstInt::SharedPtr ttl_msec)
  : PTCollection(memctx, loc),
    table_columns_(memctx),
    num_key_columns_(0),
    num_hash_key_columns_(0),
    key_where_ops_(memctx),
    where_ops_(memctx),
    write_only_(write_only),
    ttl_msec_(ttl_msec),
    column_args_(memctx) {
}

PTDmlStmt::~PTDmlStmt() {
}

CHECKED_STATUS PTDmlStmt::LookupTable(SemContext *sem_context) {
  const char *name = table_name();
  VLOG(3) << "Loading table descriptor for " << name;
  table_ = sem_context->GetTableDesc(name);
  if (table_ == nullptr) {
    return sem_context->Error(table_loc(), ErrorCode::TABLE_NOT_FOUND);
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
    RETURN_NOT_OK(sem_context->MapSymbol(col_name, &table_columns_[idx]));
  }

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeWhereClause(SemContext *sem_context,
                                             const PTExpr::SharedPtr& where_clause) {
  if (where_clause == nullptr) {
    return sem_context->Error(loc(), "Missing partition key",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  MCVector<WhereSemanticStats> col_stats(sem_context->PTempMem());
  col_stats.resize(num_columns());

  // Analyze where expression.
  const int key_count = write_only_ ? num_key_columns_ : num_hash_key_columns_;
  key_where_ops_.resize(key_count);
  RETURN_NOT_OK(AnalyzeWhereExpr(sem_context, where_clause.get(), &col_stats));

  // Make sure that all hash entries are referenced in where expression.
  for (int idx = 0; idx < key_count; idx++) {
    if (!col_stats[idx].has_eq_) {
      return sem_context->Error(where_clause->loc(),
                                "Missing condition on key columns in WHERE clause",
                                ErrorCode::CQL_STATEMENT_INVALID);
    }
  }

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeWhereExpr(SemContext *sem_context,
                                           PTExpr *expr,
                                           MCVector<WhereSemanticStats> *col_stats) {

  if (expr == nullptr) {
    return Status::OK();
  }

  PTPredicate2 *bool_expr = nullptr;
  const ColumnDesc *col_desc = nullptr;
  PTExpr::SharedPtr value;
  switch (expr->expr_op()) {
    case ExprOperator::kAND:
      bool_expr = static_cast<PTPredicate2*>(expr);
      RETURN_NOT_OK(AnalyzeWhereExpr(sem_context, bool_expr->op1().get(), col_stats));
      RETURN_NOT_OK(AnalyzeWhereExpr(sem_context, bool_expr->op2().get(), col_stats));
      break;

    case ExprOperator::kEQ: {
      RETURN_NOT_OK(AnalyzeCompareExpr(sem_context, expr, &col_desc, &value));

      if ((*col_stats)[col_desc->index()].has_eq_ ||
          (*col_stats)[col_desc->index()].has_lt_ ||
          (*col_stats)[col_desc->index()].has_gt_) {
        // A column in CQL WHERE shouldn't have "==" operator toghether in combination with others.
        // Invalid conditions: (col = x && col = y), (col = x && col < y)
        return sem_context->Error(expr->loc(), "Illogical condition for where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      (*col_stats)[col_desc->index()].has_eq_ = true;

      // The condition to "where" operator list.
      if (col_desc->is_hash()) {
        key_where_ops_[col_desc->index()].Init(
          col_desc, value, ExprOperator::kEQ, YQLOperator::YQL_OP_EQUAL);
      } else if (col_desc->is_primary()) {
        if (write_only_) {
          key_where_ops_[col_desc->index()].Init(
            col_desc, value, ExprOperator::kEQ, YQLOperator::YQL_OP_EQUAL);
        } else {
          ColumnOp col_op(col_desc, value, ExprOperator::kEQ, YQLOperator::YQL_OP_EQUAL);
          where_ops_.push_back(col_op);
        }
      } else {
        return sem_context->Error(expr->loc(), "Non primary key cannot be used in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      break;
    }

    case ExprOperator::kLT: {
      RETURN_NOT_OK(AnalyzeCompareExpr(sem_context, expr, &col_desc, &value));

      if (col_desc->is_hash()) {
        return sem_context->Error(expr->loc(), "Partition column cannot be used in this expression",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      } else if (col_desc->is_primary()) {
        if (write_only_) {
          return sem_context->Error(expr->loc(), "Range expression is not yet supported",
                                    ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
        } else if ((*col_stats)[col_desc->index()].has_eq_ ||
                   (*col_stats)[col_desc->index()].has_lt_) {
          return sem_context->Error(expr->loc(), "Illogical range condition",
                                    ErrorCode::CQL_STATEMENT_INVALID);
        }
      } else {
        return sem_context->Error(expr->loc(), "Non primary key cannot be used in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      (*col_stats)[col_desc->index()].has_lt_ = true;

      // The condition to "where" operator list.
      ColumnOp col_op(col_desc, value, ExprOperator::kLT, YQLOperator::YQL_OP_LESS_THAN);
      where_ops_.push_back(col_op);
      break;
    }

    case ExprOperator::kGT: {
      RETURN_NOT_OK(AnalyzeCompareExpr(sem_context, expr, &col_desc, &value));

      if (col_desc->is_hash()) {
        return sem_context->Error(expr->loc(), "Partition column cannot be used in this expression",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      } else if (col_desc->is_primary()) {
        if (write_only_) {
          return sem_context->Error(expr->loc(), "Range expression is not yet supported",
                                    ErrorCode::FEATURE_NOT_YET_IMPLEMENTED);
        } else if ((*col_stats)[col_desc->index()].has_eq_ ||
                   (*col_stats)[col_desc->index()].has_gt_) {
          return sem_context->Error(expr->loc(), "Illogical range condition",
                                    ErrorCode::CQL_STATEMENT_INVALID);
        }
      } else {
        return sem_context->Error(expr->loc(), "Non primary key cannot be used in where clause",
                                  ErrorCode::CQL_STATEMENT_INVALID);
      }
      (*col_stats)[col_desc->index()].has_gt_ = true;

      // The condition to "where" operator list.
      ColumnOp col_op(col_desc, value, ExprOperator::kGT, YQLOperator::YQL_OP_GREATER_THAN);
      where_ops_.push_back(col_op);
      break;
    }

    default:
      LOG(FATAL) << "Illegal op = " << int(expr->expr_op());
      break;
  }

  // Check that if where clause is present, it must follow CQL rules.
  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeIfExpr(SemContext *sem_context,
                                        PTExpr *expr) {

  if (expr == nullptr) {
    return Status::OK();
  }

  switch (expr->expr_op()) {
    case ExprOperator::kAND: FALLTHROUGH_INTENDED;
    case ExprOperator::kOR: {
      PTPredicate2 *bool_expr = static_cast<PTPredicate2*>(expr);
      RETURN_NOT_OK(AnalyzeIfExpr(sem_context, bool_expr->op1().get()));
      RETURN_NOT_OK(AnalyzeIfExpr(sem_context, bool_expr->op2().get()));
      break;
    }
    case ExprOperator::kNot: {
      PTPredicate1 *bool_expr = static_cast<PTPredicate1*>(expr);
      RETURN_NOT_OK(AnalyzeIfExpr(sem_context, bool_expr->op1().get()));
      break;
    }

    case ExprOperator::kEQ: FALLTHROUGH_INTENDED;
    case ExprOperator::kLT: FALLTHROUGH_INTENDED;
    case ExprOperator::kGT: FALLTHROUGH_INTENDED;
    case ExprOperator::kLE: FALLTHROUGH_INTENDED;
    case ExprOperator::kGE: FALLTHROUGH_INTENDED;
    case ExprOperator::kNE: {
      RETURN_NOT_OK(AnalyzeCompareExpr(sem_context, expr));
      break;
    }

    case ExprOperator::kBetween: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotBetween: {
      RETURN_NOT_OK(AnalyzeBetweenExpr(sem_context, expr));
      break;
    }

    case ExprOperator::kIsNull:    FALLTHROUGH_INTENDED;
    case ExprOperator::kIsNotNull: FALLTHROUGH_INTENDED;
    case ExprOperator::kIsTrue:    FALLTHROUGH_INTENDED;
    case ExprOperator::kIsFalse: {
      RETURN_NOT_OK(AnalyzeColumnExpr(sem_context, expr));
      break;
    }

    case ExprOperator::kExists: FALLTHROUGH_INTENDED;
    case ExprOperator::kNotExists:
      break;

    case ExprOperator::kIn:      FALLTHROUGH_INTENDED;
    case ExprOperator::kNotIn:   FALLTHROUGH_INTENDED;
    case ExprOperator::kLike:    FALLTHROUGH_INTENDED;
    case ExprOperator::kNotLike:
      return sem_context->Error(expr->loc(), "Operator not supported yet",
                                ErrorCode::CQL_STATEMENT_INVALID);

    default:
      LOG(FATAL) << "Illegal op = " << int(expr->expr_op());
      break;
  }

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeIfClause(SemContext *sem_context,
                                          const PTExpr::SharedPtr& if_clause) {
  if (if_clause != nullptr) {
    RETURN_NOT_OK(AnalyzeIfExpr(sem_context, if_clause.get()));
  }
  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeCompareExpr(SemContext *sem_context,
                                             PTExpr *expr,
                                             const ColumnDesc **col_desc,
                                             PTExpr::SharedPtr *value) {
  PTPredicate2 *bool_expr = static_cast<PTPredicate2*>(expr);
  expr = bool_expr->op1().get();
  if (expr->expr_op() != ExprOperator::kRef) {
    return sem_context->Error(expr->loc(), "Only column reference is allowed here",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  RETURN_NOT_OK(expr->Analyze(sem_context));

  if (col_desc != nullptr) {
    *col_desc = static_cast<PTRef*>(expr)->desc();
  }
  expr = bool_expr->op2().get();
  if (expr->expr_op() != ExprOperator::kConst) {
    return sem_context->Error(expr->loc(), "Only literal value is allowed here",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  if (value != nullptr) {
    *value = bool_expr->op2();
  }

  if (!sem_context->IsComparable(bool_expr->op1()->sql_type(), bool_expr->op2()->sql_type())) {
    return sem_context->Error(bool_expr->loc(), "Cannot compare values of these datatypes",
                              ErrorCode::INCOMPARABLE_DATATYPES);
  }

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeBetweenExpr(SemContext *sem_context,
                                             PTExpr *expr) {
  PTPredicate3 *bool_expr = static_cast<PTPredicate3*>(expr);
  expr = bool_expr->op1().get();
  if (expr->expr_op() != ExprOperator::kRef) {
    return sem_context->Error(expr->loc(), "Only column reference is allowed here",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  RETURN_NOT_OK(expr->Analyze(sem_context));

  expr = bool_expr->op2().get();
  if (expr->expr_op() != ExprOperator::kConst) {
    return sem_context->Error(expr->loc(), "Only literal value is allowed here",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  expr = bool_expr->op3().get();
  if (expr->expr_op() != ExprOperator::kConst) {
    return sem_context->Error(expr->loc(), "Only literal value is allowed here",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }

  if (!sem_context->IsComparable(bool_expr->op1()->sql_type(), bool_expr->op2()->sql_type()) ||
      !sem_context->IsComparable(bool_expr->op1()->sql_type(), bool_expr->op3()->sql_type())) {
    return sem_context->Error(bool_expr->loc(), "Cannot compare values of these datatypes",
                              ErrorCode::INCOMPARABLE_DATATYPES);
  }

  return Status::OK();
}

CHECKED_STATUS PTDmlStmt::AnalyzeColumnExpr(SemContext *sem_context,
                                            PTExpr *expr) {
  PTPredicate1 *bool_expr = static_cast<PTPredicate1*>(expr);
  expr = bool_expr->op1().get();
  if (expr->expr_op() != ExprOperator::kRef) {
    return sem_context->Error(expr->loc(), "Only column reference is allowed here",
                              ErrorCode::CQL_STATEMENT_INVALID);
  }
  return expr->Analyze(sem_context);
}

CHECKED_STATUS PTDmlStmt::AnalyzeUsingClause(SemContext *sem_context) {
  if (ttl_msec_ == nullptr) {
    return Status::OK();
  }

  if (!yb::common::isValidTTLMsec(ttl_msec_->Eval())) {
    return sem_context->Error(ttl_msec_->loc(),
                              strings::Substitute("Valid ttl range : [$0, $1]",
                                                  yb::common::kMinTtlMsec,
                                                  yb::common::kMaxTtlMsec).c_str(),
                              ErrorCode::INVALID_ARGUMENTS);
  }
  return Status::OK();
}

} // namespace sql
} // namespace yb
