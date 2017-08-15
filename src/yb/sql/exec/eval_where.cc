//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"

#include "yb/util/yb_partition.h"

namespace yb {
namespace sql {

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::WhereClauseToPB(YQLWriteRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops,
                                         const MCList<SubscriptedColumnOp>& subcol_where_ops) {
  // Setup the key columns.
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      LOG(FATAL) << "Unexpected non primary key column in this context";
    }
    VLOG(3) << "WRITE request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
    if (col_desc->is_hash()) {
      RETURN_NOT_OK(SetupPartialRow(col_desc, col_pb->mutable_expr(), row));
    }
  }

  // Setup the rest of the columns.
  CHECK(where_ops.empty()) << "Server doesn't support range operation yet";
  for (const auto& op : where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
    if (col_desc->is_primary()) {
      col_pb = req->add_range_column_values();
    } else {
      col_pb = req->add_column_values();
    }
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
  }

  for (const auto& op : subcol_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
    col_pb = req->add_column_values();
    col_pb->set_column_id(col_desc->id());
    for (auto& arg : op.args()->node_list()) {
      RETURN_NOT_OK(PTExprToPB(arg, col_pb->add_subscript_args()));
    }
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereClauseToPB(YQLReadRequestPB *req,
                                         YBPartialRow *row,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops,
                                         const MCList<SubscriptedColumnOp>& subcol_where_ops,
                                         const MCList<PartitionKeyOp>& partition_key_ops,
                                         const MCList<FuncOp>& func_ops,
                                         bool *no_results) {
  // If where clause restrictions guarantee no results can be found this will be set to true below.
  *no_results = false;

  // Setup the lower/upper bounds on the partition key -- if any
  for (const auto& op : partition_key_ops) {
    YQLExpressionPB hash_code_pb;
    RETURN_NOT_OK(PTExprToPB(op.expr(), &hash_code_pb));
    DCHECK(hash_code_pb.has_value()) << "Integer constant expected";

    uint16_t hash_code = YBPartition::CqlToYBHashCode(hash_code_pb.value().int64_value());

    // internally we use [start, end) intervals -- start-inclusive, end-exclusive
    switch (op.yb_op()) {
      case YQL_OP_GREATER_THAN:
        if (hash_code != YBPartition::kMaxHashCode) {
          req->set_hash_code(hash_code + 1);
        } else {
          // Token hash greater than max implies no results.
          *no_results = true;
          return Status::OK();
        }
        break;
      case YQL_OP_GREATER_THAN_EQUAL:
        req->set_hash_code(hash_code);
        break;
      case YQL_OP_LESS_THAN:
        req->set_max_hash_code(hash_code);
        break;
      case YQL_OP_LESS_THAN_EQUAL:
        if (hash_code != YBPartition::kMaxHashCode) {
          req->set_max_hash_code(hash_code + 1);
        } // Token hash less or equal than max adds no real restriction.
        break;
      case YQL_OP_EQUAL:
        req->set_hash_code(hash_code);
        if (hash_code != YBPartition::kMaxHashCode) {
          req->set_max_hash_code(hash_code + 1);
        }  // Token hash equality restriction with max value needs no upper bound.
        break;

      default:
        LOG(FATAL) << "Unsupported operator for token-based partition key condition";
    }
  }

  // Try to set up key_where_ops as the requests hash key columns. This may be empty
  bool key_ops_are_set = true;
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    YQLColumnValuePB *col_pb;
    if (col_desc->is_hash()) {
      col_pb = req->add_hashed_column_values();
    } else {
      LOG(FATAL) << "Unexpected non partition column in this context";
    }
    VLOG(3) << "READ request, column id = " << col_desc->id();
    col_pb->set_column_id(col_desc->id());
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_pb->mutable_expr()));
    if (op.yb_op() == YQL_OP_IN) {
      int in_size = col_pb->expr().value().list_value().elems_size();
      if (in_size == 0) {
        // Empty 'IN' condition guarantees no results.
        *no_results = true;
        return Status::OK();
      } else if (in_size == 1) {
        // 'IN' condition with one element is treated as equality for efficiency.
        YQLValuePB* value_pb =
            col_pb->mutable_expr()->mutable_value()->mutable_list_value()->mutable_elems(0);
        col_pb->mutable_expr()->mutable_value()->Swap(value_pb);
      } else {
        // For now doing filtering in this case TODO(Mihnea) optimize this later.
        key_ops_are_set = false;
        req->clear_hashed_column_values();
        break;
      }
    }
    RETURN_NOT_OK(SetupPartialRow(col_desc, col_pb->mutable_expr(), row));
  }

  // Skip generation of query condition if where clause is empty.
  if (key_ops_are_set && where_ops.empty() && subcol_where_ops.empty() && func_ops.empty()) {
    return Status::OK();
  }

  // Setup the where clause.
  YQLConditionPB *next_cond = req->mutable_where_expr()->mutable_condition();

  if (!key_ops_are_set) {
    for (const auto& col_op : key_where_ops) {
      YQLConditionPB *curr_cond = next_cond; // default for last op
      if (&col_op != &key_where_ops.back() || !where_ops.empty() ||
          !subcol_where_ops.empty() || !func_ops.empty()) {
        // If this is not last op then ConditionPB is AND of this op and the next one.
        next_cond->set_op(YQL_OP_AND);

        // Create new ConditionPBs for current and next operands.
        curr_cond = next_cond->add_operands()->mutable_condition();
        next_cond = next_cond->add_operands()->mutable_condition();
      }
      RETURN_NOT_OK(WhereOpToPB(curr_cond, col_op));
      // Empty 'IN' condition guarantees no results. We check this again here since we may break
      // early in the previous loop and skip subsequent empty `IN` conditions.
      if (curr_cond->op() == YQL_OP_IN && curr_cond->operands().empty()) {
        *no_results = true;
        return Status::OK();
      }
    }
  }

  for (const auto& col_op : where_ops) {
    YQLConditionPB *curr_cond = next_cond; // default for last op
    if (&col_op != &where_ops.back() || !subcol_where_ops.empty() || !func_ops.empty()) {
      // If this is not last op then ConditionPB is AND of this op and the next one.
      next_cond->set_op(YQL_OP_AND);

      // Create new ConditionPBs for current and next operands.
      curr_cond = next_cond->add_operands()->mutable_condition();
      next_cond = next_cond->add_operands()->mutable_condition();
    }
    RETURN_NOT_OK(WhereOpToPB(curr_cond, col_op));
    // Empty 'IN' condition guarantees no results.
    if (curr_cond->op() == YQL_OP_IN && curr_cond->operands().empty()) {
      *no_results = true;
      return Status::OK();
    }
  }

  for (const auto& col_op : subcol_where_ops) {
    if (&col_op == &subcol_where_ops.back() && func_ops.empty()) {
      // This is the last operator. Use the current ConditionPB.
      RETURN_NOT_OK(WhereSubColOpToPB(next_cond, col_op));
    } else {
      // Current ConditionPB would be AND of this op and the next one.
      next_cond->set_op(YQL_OP_AND);
      YQLExpressionPB *op = next_cond->add_operands();
      RETURN_NOT_OK(WhereSubColOpToPB(op->mutable_condition(), col_op));
      // Create a new the ConditionPB for the next operand.
      next_cond = next_cond->add_operands()->mutable_condition();
    }
  }

  for (const auto& func_op : func_ops) {
    if (&func_op == &func_ops.back()) {
      // This is the last operator. Use the current ConditionPB.
      RETURN_NOT_OK(FuncOpToPB(next_cond, func_op));
    } else {
      // Current ConditionPB would be AND of this op and the next one.
      next_cond->set_op(YQL_OP_AND);
      YQLExpressionPB *op = next_cond->add_operands();
      RETURN_NOT_OK(FuncOpToPB(op->mutable_condition(), func_op));

      // Create a new the ConditionPB for the next operand.
      next_cond = next_cond->add_operands()->mutable_condition();
    }
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereOpToPB(YQLConditionPB *condition, const ColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  YQLExpressionPB *expr_pb = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  expr_pb->set_column_id(col_desc->id());

  // Operand 2: The expression.
  expr_pb = condition->add_operands();
  return PTExprToPB(col_op.expr(), expr_pb);
}

CHECKED_STATUS Executor::WhereSubColOpToPB(YQLConditionPB *condition,
                                           const SubscriptedColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  YQLExpressionPB *expr_pb = condition->add_operands();
  VLOG(3) << "WHERE condition, sub-column with id = " << col_desc->id();
  auto col_pb = expr_pb->mutable_subscripted_col();
  col_pb->set_column_id(col_desc->id());
  for (auto& arg : col_op.args()->node_list()) {
    RETURN_NOT_OK(PTExprToPB(arg, col_pb->add_subscript_args()));
  }
  // Operand 2: The expression.
  expr_pb = condition->add_operands();
  return PTExprToPB(col_op.expr(), expr_pb);
}

CHECKED_STATUS Executor::FuncOpToPB(YQLConditionPB *condition, const FuncOp& func_op) {
  // Set the operator.
  condition->set_op(func_op.yb_op());

  // Operand 1: The function call.
  PTBcall::SharedPtr ptr = func_op.func_expr();
  YQLExpressionPB *expr_pb = condition->add_operands();
  RETURN_NOT_OK(PTExprToPB(static_cast<const PTBcall*>(ptr.get()), expr_pb));

  // Operand 2: The expression.
  expr_pb = condition->add_operands();
  return PTExprToPB(func_op.value_expr(), expr_pb);
}

}  // namespace sql
}  // namespace yb
