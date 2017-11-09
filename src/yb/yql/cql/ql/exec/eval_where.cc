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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/util/yb_partition.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS Executor::WhereClauseToPB(QLWriteRequestPB *req,
                                         const MCVector<ColumnOp>& key_where_ops,
                                         const MCList<ColumnOp>& where_ops,
                                         const MCList<SubscriptedColumnOp>& subcol_where_ops) {
  // Setup the key columns.
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    QLExpressionPB *col_expr_pb;
    if (col_desc->is_hash()) {
      col_expr_pb = req->add_hashed_column_values();
    } else if (col_desc->is_primary()) {
      col_expr_pb = req->add_range_column_values();
    } else {
      LOG(FATAL) << "Unexpected non primary key column in this context";
    }
    RETURN_NOT_OK(PTExprToPB(op.expr(), col_expr_pb));
  }

  // Setup the rest of the columns.
  CHECK(where_ops.empty() || req->type() == QLWriteRequestPB::QL_STMT_DELETE)
      << "Server only supports range operations in write requests for deletes";

  CHECK(subcol_where_ops.empty())
      << "Server doesn't support sub-column conditions in where clause for write requests";

  // Setup the where clause -- only allowed for deletes, should be checked before getting here.
  if (!where_ops.empty()) {
    QLConditionPB *where_pb = req->mutable_where_expr()->mutable_condition();
    where_pb->set_op(QL_OP_AND);
    for (const auto &col_op : where_ops) {
          RETURN_NOT_OK(WhereOpToPB(where_pb->add_operands()->mutable_condition(), col_op));
    }
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereClauseToPB(QLReadRequestPB *req,
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
    QLExpressionPB expr_pb;
    RETURN_NOT_OK(PTExprToPB(op.expr(), &expr_pb));
    QLValue result;
    RETURN_NOT_OK(EvalExpr(expr_pb, nullptr, &result));
    DCHECK(result.value().has_int64_value())
      << "Partition key operations are expected to return BIGINT";
    uint16_t hash_code = YBPartition::CqlToYBHashCode(result.int64_value());

    // We always use inclusive intervals [start, end] for hash_code
    switch (op.yb_op()) {
      case QL_OP_GREATER_THAN:
        if (hash_code < YBPartition::kMaxHashCode) {
          req->set_hash_code(hash_code + 1);
        } else {
          // Token hash greater than max implies no results.
          *no_results = true;
          return Status::OK();
        }
        break;
      case QL_OP_GREATER_THAN_EQUAL:
        req->set_hash_code(hash_code);
        break;
      case QL_OP_LESS_THAN:
        if (hash_code > YBPartition::kMinHashCode) {
          req->set_max_hash_code(hash_code - 1);
        } else {
          // Token hash smaller than min implies no results.
          *no_results = true;
          return Status::OK();
        }
        break;
      case QL_OP_LESS_THAN_EQUAL:
        req->set_max_hash_code(hash_code);
        break;
      case QL_OP_EQUAL:
        req->set_hash_code(hash_code);
        req->set_max_hash_code(hash_code);
        break;

      default:
        LOG(FATAL) << "Unsupported operator for token-based partition key condition";
    }
  }

  // Try to set up key_where_ops as the requests' hash key columns.
  // For selects with 'IN' conditions on the hash keys we may need to read several partitions.
  // If we find an 'IN', we add subsequent hash column values options to the execution context.
  // Then, the executor will use them to produce the partitions that need to be read.
  bool is_multi_partition = false;
  uint64_t partitions_count = 0;
  for (const auto& op : key_where_ops) {
    const ColumnDesc *col_desc = op.desc();
    CHECK(col_desc->is_hash()) << "Unexpected non partition column in this context";

    VLOG(3) << "READ request, column id = " << col_desc->id();

    switch (op.yb_op()) {
      case QL_OP_EQUAL: {
        if (!is_multi_partition) {
          QLExpressionPB *col_pb = req->add_hashed_column_values();
          col_pb->set_column_id(col_desc->id());
          Status s = PTExprToPB(op.expr(), col_pb);
          if (PREDICT_FALSE(!s.ok())) {
            return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
          }
        } else {
          QLExpressionPB col_pb;
          col_pb.set_column_id(col_desc->id());
          Status s = PTExprToPB(op.expr(), &col_pb);
          if (PREDICT_FALSE(!s.ok())) {
            return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
          }
          exec_context_->hash_values_options()->push_back({col_pb});
        }
        break;
      }

      case QL_OP_IN: {
        if (!is_multi_partition) {
          is_multi_partition = true;
          partitions_count = 1;
        }
        auto *in_expr = static_cast<const PTCollectionExpr *>(op.expr().get());
        if (in_expr->size() == 0) {
          *no_results = true;
          return Status::OK();
        } else {
          // De-duplicating and ordering values from the 'IN' expression.
          QLExpressionPB col_pb;
          Status s = PTExprToPB(op.expr(), &col_pb);
          if (PREDICT_FALSE(!s.ok())) {
            return exec_context_->Error(s, ErrorCode::INVALID_ARGUMENTS);
          }

          std::set<QLValuePB> set_values;
          for (const QLValuePB &value_pb : col_pb.value().list_value().elems()) {
            set_values.insert(value_pb);
          }

          // Adding partition options information to the execution context.
          partitions_count *= set_values.size();
          exec_context_->hash_values_options()->emplace_back();
          auto& options = exec_context_->hash_values_options()->back();
          for (const QLValuePB &value_pb : set_values) {
            options.emplace_back();
            options.back().set_column_id(col_desc->id());
            options.back().mutable_value()->CopyFrom(value_pb);
          }
        }
        break;
      }

      default:
        // This should be caught by the analyzer before getting here.
        LOG(FATAL) << "Only '=' and 'IN' operators allowed on hash keys";
    }
  }

  // Set the partitions count in the execution context, will be 0 if not IN conditions found.
  exec_context_->set_partitions_count(partitions_count);

  // Skip generation of query condition if where clause is empty.
  if (where_ops.empty() && subcol_where_ops.empty() && func_ops.empty()) {
    return Status::OK();
  }

  // Setup the where clause.
  QLConditionPB *where_pb = req->mutable_where_expr()->mutable_condition();
  where_pb->set_op(QL_OP_AND);
  for (const auto& col_op : where_ops) {
    RETURN_NOT_OK(WhereOpToPB(where_pb->add_operands()->mutable_condition(), col_op));
  }
  for (const auto& col_op : subcol_where_ops) {
    RETURN_NOT_OK(WhereSubColOpToPB(where_pb->add_operands()->mutable_condition(), col_op));
  }
  for (const auto& func_op : func_ops) {
    RETURN_NOT_OK(FuncOpToPB(where_pb->add_operands()->mutable_condition(), func_op));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::WhereOpToPB(QLConditionPB *condition, const ColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  QLExpressionPB *expr_pb = condition->add_operands();
  VLOG(3) << "WHERE condition, column id = " << col_desc->id();
  expr_pb->set_column_id(col_desc->id());

  // Operand 2: The expression.
  expr_pb = condition->add_operands();
  return PTExprToPB(col_op.expr(), expr_pb);
}

CHECKED_STATUS Executor::WhereSubColOpToPB(QLConditionPB *condition,
                                           const SubscriptedColumnOp& col_op) {
  // Set the operator.
  condition->set_op(col_op.yb_op());

  // Operand 1: The column.
  const ColumnDesc *col_desc = col_op.desc();
  QLExpressionPB *expr_pb = condition->add_operands();
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

CHECKED_STATUS Executor::FuncOpToPB(QLConditionPB *condition, const FuncOp& func_op) {
  // Set the operator.
  condition->set_op(func_op.yb_op());

  // Operand 1: The function call.
  PTBcall::SharedPtr ptr = func_op.func_expr();
  QLExpressionPB *expr_pb = condition->add_operands();
  RETURN_NOT_OK(PTExprToPB(static_cast<const PTBcall*>(ptr.get()), expr_pb));

  // Operand 2: The expression.
  expr_pb = condition->add_operands();
  return PTExprToPB(func_op.value_expr(), expr_pb);
}

}  // namespace ql
}  // namespace yb
