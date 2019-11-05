//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/common/ql_expr.h"

#include "yb/common/ql_bfunc.h"
#include "yb/common/ql_value.h"
#include "yb/common/jsonb.h"

namespace yb {

bfql::TSOpcode QLExprExecutor::GetTSWriteInstruction(const QLExpressionPB& ql_expr) const {
  // "kSubDocInsert" instructs the tablet server to insert a new value or replace an existing value.
  if (ql_expr.has_tscall()) {
    return static_cast<bfql::TSOpcode>(ql_expr.tscall().opcode());
  }
  return bfql::TSOpcode::kScalarInsert;
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalExpr(const QLExpressionPB& ql_expr,
                                        const QLTableRow& table_row,
                                        QLValue *result,
                                        const Schema *schema,
                                        const QLValuePB** result_ptr) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:
      if (result_ptr) {
        *result_ptr = &ql_expr.value();
      } else {
        *result = ql_expr.value();
      }
      break;

    case QLExpressionPB::ExprCase::kColumnId:
      if (result_ptr) {
        *result_ptr = table_row.GetColumn(ql_expr.column_id());
        if (!*result_ptr) {
          result->SetNull();
        }
      } else {
        RETURN_NOT_OK(table_row.ReadColumn(ql_expr.column_id(), result));
      }
      break;

    case QLExpressionPB::ExprCase::kJsonColumn: {
      QLValue ql_value;
      const QLJsonColumnOperationsPB& json_ops = ql_expr.json_column();
      RETURN_NOT_OK(table_row.ReadColumn(json_ops.column_id(), &ql_value));
      common::Jsonb jsonb(std::move(ql_value.jsonb_value()));
      RETURN_NOT_OK(jsonb.ApplyJsonbOperators(json_ops, result));
      break;
    }

    case QLExpressionPB::ExprCase::kSubscriptedCol:
      if (table_row.IsEmpty()) {
        result->SetNull();
      } else {
        QLValue index_arg;
        const QLSubscriptedColPB& subcol = ql_expr.subscripted_col();
        RETURN_NOT_OK(EvalExpr(subcol.subscript_args(0), table_row, &index_arg));
        RETURN_NOT_OK(table_row.ReadSubscriptedColumn(subcol, index_arg, result));
      }
      break;

    case QLExpressionPB::ExprCase::kBfcall:
      return EvalBFCall(ql_expr.bfcall(), table_row, result);

    case QLExpressionPB::ExprCase::kTscall:
      return EvalTSCall(ql_expr.tscall(), table_row, result, schema);

    case QLExpressionPB::ExprCase::kCondition:
      return EvalCondition(ql_expr.condition(), table_row, result);

    case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      result->SetNull();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalExpr(QLExpressionPB* ql_expr,
                                        const QLTableRow& table_row,
                                        const Schema *schema) {
  if (!ql_expr->has_value()) {
    QLValue temp;
    RETURN_NOT_OK(EvalExpr(*ql_expr, table_row, &temp, schema));
    ql_expr->mutable_value()->Swap(temp.mutable_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::ReadExprValue(const QLExpressionPB& ql_expr,
                                             const QLTableRow& table_row,
                                             QLValue *result) {
  if (ql_expr.expr_case() == QLExpressionPB::ExprCase::kTscall) {
    return ReadTSCallValue(ql_expr.tscall(), table_row, result);
  } else {
    return EvalExpr(ql_expr, table_row, result);
  }
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalBFCall(const QLBCallPB& bfcall,
                                          const QLTableRow& table_row,
                                          QLValue *result) {
  // TODO(neil)
  // - Use TSOpode for collection expression if only TabletServer can execute.
  // OR
  // - Introduce BuiltinOperator in addition to builtin function. Use builtin operators for all
  //   special operations including collection operations. That way, we don't need special cases.

  // Special cases: for collection operations of the form "cref = cref +/- <value>" we avoid
  // reading column cref and instead tell doc writer to modify it in-place.
  //   "AddMapMap"
  //   "AddSetSet"
  //   "SubMapSet"
  //   "SubSetSet"
  //   "AddListList"
  //   "SubListList"

  const bfql::BFOpcode bf_opcode = static_cast<bfql::BFOpcode>(bfcall.opcode());
  // First evaluate the arguments.
  vector<QLValue> args(bfcall.operands().size());
  int arg_index = 0;
  for (auto operand : bfcall.operands()) {
    QLValue* arg = &args[arg_index++];
    RETURN_NOT_OK(EvalExpr(operand, table_row, arg));
  }

  // Execute the builtin call associated with the given opcode.
  return QLBfunc::Exec(bf_opcode, &args, result);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalTSCall(const QLBCallPB& ql_expr,
                                          const QLTableRow& table_row,
                                          QLValue *result,
                                          const Schema *schema) {
  result->SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

CHECKED_STATUS QLExprExecutor::ReadTSCallValue(const QLBCallPB& ql_expr,
                                               const QLTableRow& table_row,
                                               QLValue *result) {
  result->SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalCondition(const QLConditionPB& condition,
                                             const QLTableRow& table_row,
                                             bool* result) {
  QLValue result_pb;
  RETURN_NOT_OK(EvalCondition(condition, table_row, &result_pb));
  *result = result_pb.bool_value();
  return Status::OK();
}

CHECKED_STATUS QLExprExecutor::EvalCondition(const QLConditionPB& condition,
                                             const QLTableRow& table_row,
                                             QLValue *result) {
#define QL_EVALUATE_RELATIONAL_OP(op)                                                              \
  do {                                                                                             \
    CHECK_EQ(operands.size(), 2);                                                                  \
    QLValue left, right;                                                                           \
    RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &left));                                    \
    RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &right));                                   \
    if (!left.Comparable(right))                                                                   \
      return STATUS(RuntimeError, "values not comparable");                                        \
    result->set_bool_value(left.value() op right.value());                                         \
    return Status::OK();                                                                           \
  } while (false)

#define QL_EVALUATE_BETWEEN(op1, op2, rel_op)                                                      \
  do {                                                                                             \
      CHECK_EQ(operands.size(), 3);                                                                \
      QLValue lower, upper;                                                                        \
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));                                  \
      RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &lower));                                 \
      RETURN_NOT_OK(EvalExpr(operands.Get(2), table_row, &upper));                                 \
      if (!temp.Comparable(lower) || !temp.Comparable(upper)) {                                    \
        return STATUS(RuntimeError, "values not comparable");                                      \
      }                                                                                            \
      result->set_bool_value(temp.value() op1 lower.value() rel_op temp.value() op2 upper.value());\
      return Status::OK();                                                                         \
  } while (false)

  QLValue temp;
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case QL_OP_NOT:
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), QLExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvalCondition(operands.Get(0).condition(), table_row, &temp));
      result->set_bool_value(!temp.bool_value());
      return Status::OK();

    case QL_OP_IS_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      result->set_bool_value(temp.IsNull());
      return Status::OK();

    case QL_OP_IS_NOT_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      result->set_bool_value(!temp.IsNull());
      return Status::OK();

    case QL_OP_IS_TRUE:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      if (temp.type() != InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      result->set_bool_value(!temp.IsNull() && temp.bool_value());
      return Status::OK();

    case QL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      if (temp.type() != InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      result->set_bool_value(!temp.IsNull() && !temp.bool_value());
      return Status::OK();
    }

    case QL_OP_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(==);

    case QL_OP_LESS_THAN:
      QL_EVALUATE_RELATIONAL_OP(<);                                                      // NOLINT

    case QL_OP_LESS_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(<=);

    case QL_OP_GREATER_THAN:
      QL_EVALUATE_RELATIONAL_OP(>);                                                      // NOLINT

    case QL_OP_GREATER_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(>=);

    case QL_OP_NOT_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(!=);

    case QL_OP_AND:
      CHECK_GT(operands.size(), 0);
      for (const auto &operand : operands) {
        CHECK_EQ(operand.expr_case(), QLExpressionPB::ExprCase::kCondition);
        RETURN_NOT_OK(EvalCondition(operand.condition(), table_row, result));
        if (!result->bool_value()) {
          break;
        }
      }
      return Status::OK();

    case QL_OP_OR:
      CHECK_GT(operands.size(), 0);
      for (const auto &operand : operands) {
        CHECK_EQ(operand.expr_case(), QLExpressionPB::ExprCase::kCondition);
        RETURN_NOT_OK(EvalCondition(operand.condition(), table_row, result));
        if (result->bool_value()) {
          break;
        }
      }
      return Status::OK();

    case QL_OP_BETWEEN:
      QL_EVALUATE_BETWEEN(>=, <=, &&);

    case QL_OP_NOT_BETWEEN:
      QL_EVALUATE_BETWEEN(<, >, ||);

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case QL_OP_EXISTS:
      result->set_bool_value(!table_row.IsEmpty());
      return Status::OK();

    case QL_OP_NOT_EXISTS:
      result->set_bool_value(table_row.IsEmpty());
      return Status::OK();

    case QL_OP_IN: {
      CHECK_EQ(operands.size(), 2);
      QLValue left, right;
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &left));
      RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &right));

      result->set_bool_value(false);
      for (const QLValuePB& elem : right.list_value().elems()) {
        if (!Comparable(elem, left)) {
           return STATUS(RuntimeError, "values not comparable");
        }
        if (elem == left) {
          result->set_bool_value(true);
          break;
        }
      }
      return Status::OK();
    }

    case QL_OP_NOT_IN: {
      CHECK_EQ(operands.size(), 2);
      QLValue left, right;
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &left));
      RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &right));

      result->set_bool_value(true);
      for (const QLValuePB& elem : right.list_value().elems()) {
        if (!Comparable(elem, left)) {
          return STATUS(RuntimeError, "values not comparable");
        }
        if (elem == left) {
          result->set_bool_value(false);
          break;
        }
      }
      return Status::OK();
    }

    case QL_OP_LIKE: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE:
      LOG(ERROR) << "Internal error: illegal or unknown operator " << condition.op();
      break;

    case QL_OP_NOOP:
      break;
  }

  result->SetNull();
  return STATUS(RuntimeError, "Internal error: illegal or unknown operator");

#undef QL_EVALUATE_RELATIONAL_OP
#undef QL_EVALUATE_BETWEEN
}

//--------------------------------------------------------------------------------------------------

bfpg::TSOpcode QLExprExecutor::GetTSWriteInstruction(const PgsqlExpressionPB& ql_expr) const {
  // "kSubDocInsert" instructs the tablet server to insert a new value or replace an existing value.
  if (ql_expr.has_tscall()) {
    return static_cast<bfpg::TSOpcode>(ql_expr.tscall().opcode());
  }
  return bfpg::TSOpcode::kScalarInsert;
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalExpr(const PgsqlExpressionPB& ql_expr,
                                        const QLTableRow::SharedPtrConst& table_row,
                                        QLValue *result) {
  switch (ql_expr.expr_case()) {
    case PgsqlExpressionPB::ExprCase::kValue:
      *result = ql_expr.value();
      break;

    case PgsqlExpressionPB::ExprCase::kColumnId:
      return EvalColumnRef(ql_expr.column_id(), table_row, result);

    case PgsqlExpressionPB::ExprCase::kBfcall:
      return EvalBFCall(ql_expr.bfcall(), table_row, result);

    case PgsqlExpressionPB::ExprCase::kTscall:
      return EvalTSCall(ql_expr.tscall(), table_row, result);

    case PgsqlExpressionPB::ExprCase::kCondition:
      return EvalCondition(ql_expr.condition(), table_row, result);

    case PgsqlExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::kAliasId: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::EXPR_NOT_SET:
      result->SetNull();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::ReadExprValue(const PgsqlExpressionPB& ql_expr,
                                             const QLTableRow::SharedPtrConst& table_row,
                                             QLValue *result) {
  if (ql_expr.expr_case() == PgsqlExpressionPB::ExprCase::kTscall) {
    return ReadTSCallValue(ql_expr.tscall(), table_row, result);
  } else {
    return EvalExpr(ql_expr, table_row, result);
  }
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalColumnRef(ColumnIdRep col_id,
                                             const QLTableRow::SharedPtrConst& table_row,
                                             QLValue *result) {
  if (table_row == nullptr) {
    result->SetNull();
  } else {
    RETURN_NOT_OK(table_row->ReadColumn(col_id, result));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalBFCall(const PgsqlBCallPB& bfcall,
                                          const QLTableRow::SharedPtrConst& table_row,
                                          QLValue *result) {
  // TODO(neil)
  // - Use TSOpode for collection expression if only TabletServer can execute.
  // OR
  // - Introduce BuiltinOperator in addition to builtin function. Use builtin operators for all
  //   special operations including collection operations. That way, we don't need special cases.

  // First, evaluate the arguments.
  vector<QLValue> args(bfcall.operands().size());
  int arg_index = 0;
  for (auto operand : bfcall.operands()) {
    RETURN_NOT_OK(EvalExpr(operand, table_row, &args[arg_index]));
    arg_index++;
  }

  // Now, execute the builtin call associated with the given opcode.
  return PgsqlBfunc::Exec(static_cast<bfpg::BFOpcode>(bfcall.opcode()), &args, result);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalTSCall(const PgsqlBCallPB& ql_expr,
                                          const QLTableRow::SharedPtrConst& table_row,
                                          QLValue *result) {
  result->SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

CHECKED_STATUS QLExprExecutor::ReadTSCallValue(const PgsqlBCallPB& ql_expr,
                                               const QLTableRow::SharedPtrConst& table_row,
                                               QLValue *result) {
  result->SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalCondition(const PgsqlConditionPB& condition,
                                             const QLTableRow::SharedPtrConst& table_row,
                                             bool* result) {
  QLValue result_pb;
  RETURN_NOT_OK(EvalCondition(condition, table_row, &result_pb));
  *result = result_pb.bool_value();
  return Status::OK();
}

CHECKED_STATUS QLExprExecutor::EvalCondition(const PgsqlConditionPB& condition,
                                             const QLTableRow::SharedPtrConst& table_row,
                                             QLValue *result) {
#define QL_EVALUATE_RELATIONAL_OP(op)                                                              \
  do {                                                                                             \
    CHECK_EQ(operands.size(), 2);                                                                  \
    QLValue left, right;                                                                           \
    RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &left));                                    \
    RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &right));                                   \
    if (!left.Comparable(right))                                                                   \
      return STATUS(RuntimeError, "values not comparable");                                        \
    result->set_bool_value(left.value() op right.value());                                         \
    return Status::OK();                                                                           \
  } while (false)

#define QL_EVALUATE_BETWEEN(op1, op2, rel_op)                                                      \
  do {                                                                                             \
      CHECK_EQ(operands.size(), 3);                                                                \
      QLValue lower, upper;                                                                        \
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));                                  \
      RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &lower));                                 \
      RETURN_NOT_OK(EvalExpr(operands.Get(2), table_row, &upper));                                 \
      if (!temp.Comparable(lower) || !temp.Comparable(upper)) {                                    \
        return STATUS(RuntimeError, "values not comparable");                                      \
      }                                                                                            \
      result->set_bool_value(temp.value() op1 lower.value() rel_op temp.value() op2 upper.value());\
      return Status::OK();                                                                         \
  } while (false)

  QLValue temp;
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case QL_OP_NOT:
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), PgsqlExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvalCondition(operands.Get(0).condition(), table_row, &temp));
      result->set_bool_value(!temp.bool_value());
      return Status::OK();

    case QL_OP_IS_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      result->set_bool_value(temp.IsNull());
      return Status::OK();

    case QL_OP_IS_NOT_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      result->set_bool_value(!temp.IsNull());
      return Status::OK();

    case QL_OP_IS_TRUE:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      if (temp.type() != InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      result->set_bool_value(!temp.IsNull() && temp.bool_value());
      return Status::OK();

    case QL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &temp));
      if (temp.type() != InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      result->set_bool_value(!temp.IsNull() && !temp.bool_value());
      return Status::OK();
    }

    case QL_OP_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(==);

    case QL_OP_LESS_THAN:
      QL_EVALUATE_RELATIONAL_OP(<);                                                      // NOLINT

    case QL_OP_LESS_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(<=);

    case QL_OP_GREATER_THAN:
      QL_EVALUATE_RELATIONAL_OP(>);                                                      // NOLINT

    case QL_OP_GREATER_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(>=);

    case QL_OP_NOT_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(!=);

    case QL_OP_AND:
      CHECK_GT(operands.size(), 0);
      for (const auto &operand : operands) {
        CHECK_EQ(operand.expr_case(), PgsqlExpressionPB::ExprCase::kCondition);
        RETURN_NOT_OK(EvalCondition(operand.condition(), table_row, result));
        if (!result->bool_value()) {
          break;
        }
      }
      return Status::OK();

    case QL_OP_OR:
      CHECK_GT(operands.size(), 0);
      for (const auto &operand : operands) {
        CHECK_EQ(operand.expr_case(), PgsqlExpressionPB::ExprCase::kCondition);
        RETURN_NOT_OK(EvalCondition(operand.condition(), table_row, result));
        if (result->bool_value()) {
          break;
        }
      }
      return Status::OK();

    case QL_OP_BETWEEN:
      QL_EVALUATE_BETWEEN(>=, <=, &&);

    case QL_OP_NOT_BETWEEN:
      QL_EVALUATE_BETWEEN(<, >, ||);

      // When a row exists, the primary key columns are always populated in the row (value-map) by
      // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
      // the row (value-map) is not empty.
    case QL_OP_EXISTS:
      result->set_bool_value(!table_row->IsEmpty());
      return Status::OK();

    case QL_OP_NOT_EXISTS:
      result->set_bool_value(table_row->IsEmpty());
      return Status::OK();

    case QL_OP_IN: {
      CHECK_EQ(operands.size(), 2);
      QLValue left, right;
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &left));
      RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &right));

      result->set_bool_value(false);
      for (const QLValuePB& elem : right.list_value().elems()) {
        if (!Comparable(elem, left)) {
          return STATUS(RuntimeError, "values not comparable");
        }
        if (elem == left) {
          result->set_bool_value(true);
          break;
        }
      }
      return Status::OK();
    }

    case QL_OP_NOT_IN: {
      CHECK_EQ(operands.size(), 2);
      QLValue left, right;
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, &left));
      RETURN_NOT_OK(EvalExpr(operands.Get(1), table_row, &right));

      result->set_bool_value(true);
      for (const QLValuePB& elem : right.list_value().elems()) {
        if (!Comparable(elem, left)) {
          return STATUS(RuntimeError, "values not comparable");
        }
        if (elem == left) {
          result->set_bool_value(false);
          break;
        }
      }
      return Status::OK();
    }

    case QL_OP_LIKE: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE:
      LOG(ERROR) << "Internal error: illegal or unknown operator " << condition.op();
      break;

    case QL_OP_NOOP:
      break;
  }

  result->SetNull();
  return STATUS(RuntimeError, "Internal error: illegal or unknown operator");

#undef QL_EVALUATE_RELATIONAL_OP
#undef QL_EVALUATE_BETWEEN
}

//--------------------------------------------------------------------------------------------------

const QLValuePB* QLTableRow::GetColumn(ColumnIdRep col_id) const {
  const auto& col_iter = col_map_.find(col_id);
  if (col_iter == col_map_.end()) {
    return nullptr;
  }

  return &col_iter->second.value;
}

CHECKED_STATUS QLTableRow::ReadColumn(ColumnIdRep col_id, QLValue *col_value) const {
  auto value = GetColumn(col_id);
  if (value == nullptr) {
    col_value->SetNull();
    return Status::OK();
  }

  *col_value = *value;
  return Status::OK();
}

CHECKED_STATUS QLTableRow::ReadSubscriptedColumn(const QLSubscriptedColPB& subcol,
                                                 const QLValue& index_arg,
                                                 QLValue *col_value) const {
  col_value->SetNull();

  const auto& col_iter = col_map_.find(subcol.column_id());
  if (col_iter == col_map_.end()) {
    // Not exists.
    return Status::OK();
  } else if (col_iter->second.value.has_map_value()) {
    // map['key']
    auto& map = col_iter->second.value.map_value();
    for (int i = 0; i < map.keys_size(); i++) {
      if (map.keys(i) == index_arg.value()) {
          *col_value = map.values(i);
      }
    }
  } else if (col_iter->second.value.has_list_value()) {
    // list[index]
    auto& list = col_iter->second.value.list_value();
    if (index_arg.value().has_int32_value()) {
      int list_index = index_arg.int32_value();
      if (list_index >= 0 && list_index < list.elems_size()) {
        *col_value = list.elems(list_index);
      }
    }
  }
  return Status::OK();
}

CHECKED_STATUS QLTableRow::GetTTL(ColumnIdRep col_id, int64_t *ttl_seconds) const {
  const auto& col_iter = col_map_.find(col_id);
  if (col_iter == col_map_.end()) {
    // Not exists.
    return STATUS(InternalError, "Column unexpectedly not found in cache");
  }
  *ttl_seconds = col_iter->second.ttl_seconds;
  return Status::OK();
}

CHECKED_STATUS QLTableRow::GetWriteTime(ColumnIdRep col_id, int64_t *write_time) const {
  const auto& col_iter = col_map_.find(col_id);
  if (col_iter == col_map_.end()) {
    // Not exists.
    return STATUS(InternalError, "Column unexpectedly not found in cache");
  }
  DCHECK_NE(QLTableColumn::kUninitializedWriteTime, col_iter->second.write_time);
  *write_time = col_iter->second.write_time;
  return Status::OK();
}

CHECKED_STATUS QLTableRow::GetValue(ColumnIdRep col_id, QLValue *column) const {
  const auto& col_iter = col_map_.find(col_id);
  if (col_iter == col_map_.end()) {
    // Not exists.
    return STATUS(InternalError, "Column unexpectedly not found in cache");
  }
  *column = std::move(col_iter->second.value);
  return Status::OK();
}

boost::optional<const QLValuePB&> QLTableRow::GetValue(ColumnIdRep col_id) const {
  const auto& col_iter = col_map_.find(col_id);
  if (col_iter == col_map_.end()) {
    return boost::none;
  }
  return col_iter->second.value;
}

bool QLTableRow::IsColumnSpecified(ColumnIdRep col_id) const {
  return col_map_.find(col_id) != col_map_.end();
}

void QLTableRow::ClearValue(ColumnIdRep col_id) {
  col_map_[col_id].value.Clear();
}

bool QLTableRow::MatchColumn(ColumnIdRep col_id, const QLTableRow& source) const {
  auto this_iter = col_map_.find(col_id);
  auto source_iter = source.col_map_.find(col_id);
  if (this_iter != col_map_.end() && source_iter != source.col_map_.end()) {
    return this_iter->second.value == source_iter->second.value;
  }
  if (this_iter != col_map_.end() || source_iter != source.col_map_.end()) {
    return false;
  }
  return true;
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id) {
  return col_map_[col_id];
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id, const QLValue& ql_value) {
  col_map_[col_id].value = ql_value.value();
  return col_map_[col_id];
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id, const QLValuePB& ql_value) {
  col_map_[col_id].value = ql_value;
  return col_map_[col_id];
}

CHECKED_STATUS QLTableRow::CopyColumn(ColumnIdRep col_id,
                                      const QLTableRow& source) {
  auto col_iter = source.col_map_.find(col_id);
  if (col_iter != source.col_map_.end()) {
    col_map_[col_id] = col_iter->second;
  }
  return Status::OK();
}

std::string QLTableRow::ToString(const Schema& schema) const {
  std::string ret;
  ret.append("{ ");

  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    auto it = col_map_.find(schema.column_id(col_idx));
    if (it != col_map_.end() && it->second.value.value_case() != QLValuePB::VALUE_NOT_SET) {
      ret += it->second.value.ShortDebugString();
    } else {
      ret += "null";
    }
    ret += ' ';
  }
  ret.append("}");
  return ret;
}

} // namespace yb
