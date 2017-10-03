//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/common/ql_expr.h"
#include "yb/common/ql_bfunc.h"

namespace yb {

CHECKED_STATUS QLExprExecutor::EvalExpr(const QLExpressionPB& ql_expr,
                                        const QLTableRow& column_map,
                                        QLValueWithPB *result) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:
      result->Assign(ql_expr.value());
      break;

    case QLExpressionPB::ExprCase::kColumnId: {
      auto iter = column_map.find(ColumnId(ql_expr.column_id()));
      if (iter != column_map.end()) {
        result->Assign(iter->second.value);
      } else {
        result->SetNull();
      }
      break;
    }

    case QLExpressionPB::ExprCase::kSubscriptedCol:
      return EvalSubscriptedColumn(ql_expr.subscripted_col(), column_map, result);

    case QLExpressionPB::ExprCase::kBfcall:
      return EvalBFCall(ql_expr.bfcall(), column_map, result);

    case QLExpressionPB::ExprCase::kTscall:
      return EvalTSCall(ql_expr.tscall(), column_map, result);

    case QLExpressionPB::ExprCase::kCondition:
      return EvalCondition(ql_expr.condition(), column_map, result);

    case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      result->SetNull();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalBFCall(const QLBCallPB& bfcall,
                                          const QLTableRow& column_map,
                                          QLValueWithPB *result) {
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

  // Default case: First evaluate the arguments.
  vector<QLValueWithPB> args(bfcall.operands().size());
  int arg_index = 0;
  for (auto operand : bfcall.operands()) {
    RETURN_NOT_OK(EvalExpr(operand, column_map, &args[arg_index]));
    arg_index++;
  }

  // Execute the builtin call associated with the given opcode.
  return QLBfunc::Exec(static_cast<bfql::BFOpcode>(bfcall.opcode()), &args, result);
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalTSCall(const QLBCallPB& ql_expr,
                                          const QLTableRow& column_map,
                                          QLValueWithPB *result) {
  result->SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalSubscriptedColumn(const QLSubscriptedColPB& subcol,
                                                     const QLTableRow& column_map,
                                                     QLValueWithPB *result) {
  // TODO(neil) I cut & paste this function from Mihnea's code, but this needs to be tested.
  // This will be done in a follow-up diff.
  LOG(FATAL) << "Not yet activated";

  // Default result to null.
  result->SetNull();

  // Seeking result.
  const auto column_id = ColumnId(subcol.column_id());
  const auto it = column_map.find(column_id);
  if (it != column_map.end()) {
    if (it->second.value.has_map_value()) { // map['key']
      auto& map = it->second.value.map_value();
      QLValueWithPB key;
      RETURN_NOT_OK(EvalExpr(subcol.subscript_args(0), column_map, &key));
      for (int i = 0; i < map.keys_size(); i++) {
        if (map.keys(i) == key.value()) {
          result->Assign(map.values(i));
        }
      }
    } else if (it->second.value.has_list_value()) { // list[index]
      auto& list = it->second.value.list_value();
      QLValueWithPB idx;

      RETURN_NOT_OK(EvalExpr(subcol.subscript_args(0), column_map, &idx));
      if (idx.has_int32_value()) {
        int index = idx.int32_value();
        // QL list index starts from 1 not 0
        if (index > 0 && index <= list.elems_size()) {
          result->Assign(list.elems(index - 1));
        }
      }
    }
  }

  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLExprExecutor::EvalCondition(const QLConditionPB& condition,
                                             const QLTableRow& column_map,
                                             bool* result) {
  // TODO(neil) I cut & paste this function from Robert's code, but this needs to be tested.
  // This will be done in a follow-up diff.
  LOG(FATAL) << "Not yet activated";

  QLValueWithPB result_pb;
  RETURN_NOT_OK(EvalCondition(condition, column_map, &result_pb));
  *result = result_pb.bool_value();
  return Status::OK();
}

CHECKED_STATUS QLExprExecutor::EvalCondition(const QLConditionPB& condition,
                                             const QLTableRow& column_map,
                                             QLValueWithPB *result) {
  // TODO(neil) I cut & paste this function from Robert's code with some modifications. Test it
  // before using. This will be done in a follow-up diff.
  LOG(FATAL) << "Not yet activated";

#define QL_EVALUATE_RELATIONAL_OP(op)                                      \
  do {                                                                      \
    CHECK_EQ(operands.size(), 2);                                           \
    QLValueWithPB left, right;                                             \
    RETURN_NOT_OK(EvalExpr(operands.Get(0), column_map, &left));            \
    RETURN_NOT_OK(EvalExpr(operands.Get(1), column_map, &right));           \
    if (!left.Comparable(right))                                            \
          return STATUS(RuntimeError, "values not comparable");             \
    result->set_bool_value(left.value() op right.value());                  \
    return Status::OK();                                                    \
  } while (false)

// Evaluate a logical AND/OR operation. To see if we can short-circuit, we do
// "(left op true) ^ (left op false)" that applies the "left" result with both
// "true" and "false" and only if the answers are different (i.e. exclusive or ^)
// that we should evaluate the "right" result also.
#define QL_EVALUATE_LOGICAL_OP(op)                                                            \
  do {                                                                                         \
    CHECK_EQ(operands.size(), 2);                                                              \
    CHECK_EQ(operands.Get(0).expr_case(), QLExpressionPB::ExprCase::kCondition);              \
    CHECK_EQ(operands.Get(1).expr_case(), QLExpressionPB::ExprCase::kCondition);              \
    QLValueWithPB left, right;                                                                \
    RETURN_NOT_OK(EvalCondition(operands.Get(0).condition(), column_map, &left));              \
    if ((left.bool_value() op true) ^ (left.bool_value() op false)) {                          \
      RETURN_NOT_OK(EvalCondition(operands.Get(1).condition(), column_map, &right));           \
    }                                                                                          \
    result->set_bool_value(left.bool_value() op right.bool_value());                           \
    return Status::OK();                                                                       \
  } while (false)

#define QL_EVALUATE_BETWEEN(op1, op2)                                                        \
  do {                                                                                        \
      CHECK_EQ(operands.size(), 3);                                                           \
      QLValueWithPB lower, upper;                                                            \
      RETURN_NOT_OK(EvalExpr(operands.Get(0), column_map, &temp));                            \
      RETURN_NOT_OK(EvalExpr(operands.Get(1), column_map, &lower));                           \
      RETURN_NOT_OK(EvalExpr(operands.Get(2), column_map, &upper));                           \
      if (!temp.Comparable(lower) || !temp.Comparable(upper)) {                               \
        return STATUS(RuntimeError, "values not comparable");                                 \
      }                                                                                       \
      result->set_bool_value(temp.value() >= lower.value() && temp.value() <= upper.value()); \
      result->set_bool_value(temp.value() < lower.value() || temp.value() > upper.value());   \
      return Status::OK();                                                                    \
  } while (false)

  QLValueWithPB temp;
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case QL_OP_NOT:
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), QLExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvalCondition(operands.Get(0).condition(), column_map, &temp));
      result->set_bool_value(!temp.bool_value());
      return Status::OK();

    case QL_OP_IS_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), column_map, &temp));
      result->set_bool_value(temp.IsNull());
      return Status::OK();

    case QL_OP_IS_NOT_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), column_map, &temp));
      result->set_bool_value(!temp.IsNull());
      return Status::OK();

    case QL_OP_IS_TRUE:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), column_map, &temp));
      if (temp.type() != QLValue::InternalType::kBoolValue)
        return STATUS(RuntimeError, "not a bool value");
      result->set_bool_value(!temp.IsNull()&& temp.bool_value());
      return Status::OK();

    case QL_OP_IS_FALSE: {
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), column_map, &temp));
      if (temp.type() != QLValue::InternalType::kBoolValue)
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
      QL_EVALUATE_LOGICAL_OP(&&);

    case QL_OP_OR:
      QL_EVALUATE_LOGICAL_OP(||);

    case QL_OP_BETWEEN:
      QL_EVALUATE_BETWEEN(>=, <=);

    case QL_OP_NOT_BETWEEN:
      QL_EVALUATE_BETWEEN(<, >);

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case QL_OP_EXISTS:
      result->set_bool_value(!column_map.empty());
      return Status::OK();

    case QL_OP_NOT_EXISTS:
      result->set_bool_value(column_map.empty());
      return Status::OK();

    case QL_OP_LIKE: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE: FALLTHROUGH_INTENDED;
    case QL_OP_IN: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_IN:
      LOG(ERROR) << "Internal error: illegal or unknown operator " << condition.op();
      break;

    case QL_OP_NOOP:
      break;
  }

  result->SetNull();
  return STATUS(RuntimeError, "Internal error: illegal or unknown operator");

#undef QL_EVALUATE_RELATIONAL_OP
#undef QL_EVALUATE_LOGICAL_OP
#undef QL_EVALUATE_BETWEEN
}

} // namespace yb
