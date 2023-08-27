//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/qlexpr/ql_expr.h"

#include "yb/common/jsonb.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_datatype.h"
#include "yb/common/value.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/dockv/pg_row.h"

#include "yb/qlexpr/ql_bfunc.h"

#include "yb/util/result.h"

namespace yb::qlexpr {

namespace {

constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();

}

template <>
QLExprResultWriter::ExprResultWriter(QLExprResult* result) : result_(result) {
  result_->existing_value_ = nullptr;
}

template <>
void QLExprResultWriter::SetNull() {
  yb::SetNull(&result_->value_);
}

template <>
void QLExprResultWriter::SetExisting(const QLValuePB* existing_value) {
  result_->existing_value_ = existing_value;
}

template <>
QLValuePB& QLExprResultWriter::NewValue() {
  return result_->value_;
}

template <>
LWExprResultWriter::ExprResultWriter(LWExprResult* result) : result_(result) {
  result_->value_ = nullptr;
}

template <>
void LWExprResultWriter::SetExisting(const LWQLValuePB* existing_value) {
  if (&existing_value->arena() == result_->arena_) {
    result_->value_ = const_cast<LWQLValuePB*>(existing_value);
  } else {
    result_->value_ = result_->arena_->NewObject<LWQLValuePB>(result_->arena_, *existing_value);
  }
}

template <>
LWQLValuePB& LWExprResultWriter::NewValue() {
  result_->value_ = result_->arena_->NewObject<LWQLValuePB>(result_->arena_);
  return *result_->value_;
}

template <>
void LWExprResultWriter::SetNull() {
  yb::SetNull(&NewValue());
}

bfql::TSOpcode GetTSWriteInstruction(const QLExpressionPB& ql_expr) {
  // "kSubDocInsert" instructs the tablet server to insert a new value or replace an existing value.
  if (ql_expr.has_tscall()) {
    return static_cast<bfql::TSOpcode>(ql_expr.tscall().opcode());
  }
  return bfql::TSOpcode::kScalarInsert;
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalExpr(const QLExpressionPB& ql_expr,
                                const QLTableRow& table_row,
                                QLExprResultWriter result_writer,
                                const Schema *schema) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:
      result_writer.SetExisting(&ql_expr.value());
      break;

    case QLExpressionPB::ExprCase::kColumnId:
      RETURN_NOT_OK(table_row.ReadColumn(ql_expr.column_id(), result_writer));
      break;

    case QLExpressionPB::ExprCase::kTuple: {
      QLValuePB* value = &result_writer.NewValue();
      auto tuple_value = value->mutable_tuple_value();
      for (auto const& elems : ql_expr.tuple().elems()) {
        QLExprResult temp;
        DCHECK(elems.has_column_id());
        RETURN_NOT_OK(table_row.ReadColumn(elems.column_id(), temp.Writer()));
        temp.MoveTo(tuple_value->add_elems());
      }
      result_writer.SetExisting(value);
      break;
    }

    case QLExpressionPB::ExprCase::kJsonColumn: {
      QLExprResult temp;
      const QLJsonColumnOperationsPB& json_ops = ql_expr.json_column();
      RETURN_NOT_OK(table_row.ReadColumn(json_ops.column_id(), temp.Writer()));
      if (IsNull(temp.Value())) {
        result_writer.SetNull();
      } else {
        RETURN_NOT_OK(common::Jsonb::ApplyJsonbOperators(
            temp.Value().jsonb_value(), json_ops, &result_writer.NewValue()));
      }
      break;
    }

    case QLExpressionPB::ExprCase::kSubscriptedCol:
      if (table_row.IsEmpty()) {
        result_writer.SetNull();
      } else {
        QLExprResult index_arg;
        const QLSubscriptedColPB& subcol = ql_expr.subscripted_col();
        RETURN_NOT_OK(EvalExpr(subcol.subscript_args(0), table_row, index_arg.Writer()));
        RETURN_NOT_OK(table_row.ReadSubscriptedColumn(subcol, index_arg.Value(), result_writer));
      }
      break;

    case QLExpressionPB::ExprCase::kBfcall:
      result_writer.NewValue() = VERIFY_RESULT(
          EvalBFCall<bfql::BFOpcode>(ql_expr.bfcall(), table_row));
      return Status::OK();

    case QLExpressionPB::ExprCase::kTscall:
      return EvalTSCall(ql_expr.tscall(), table_row, &result_writer.NewValue(), schema);

    case QLExpressionPB::ExprCase::kCondition:
      return EvalCondition(ql_expr.condition(), table_row, &result_writer.NewValue());

    case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      result_writer.SetNull();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalExpr(QLExpressionPB* ql_expr,
                                const QLTableRow& table_row,
                                const Schema *schema) {
  if (!ql_expr->has_value()) {
    QLExprResult temp;
    RETURN_NOT_OK(EvalExpr(*ql_expr, table_row, temp.Writer(), schema));
    temp.MoveTo(ql_expr->mutable_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::ReadExprValue(const QLExpressionPB& ql_expr,
                                     const QLTableRow& table_row,
                                     QLExprResultWriter result_writer) {
  if (ql_expr.expr_case() == QLExpressionPB::ExprCase::kTscall) {
    return ReadTSCallValue(ql_expr.tscall(), table_row, result_writer);
  } else {
    return EvalExpr(ql_expr, table_row, result_writer);
  }
}

//--------------------------------------------------------------------------------------------------

template <class OpCode, class Expr, class Row>
Result<QLValuePB> QLExprExecutor::EvalBFCall(const Expr& bfcall, const Row& table_row) {
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

  // First evaluate the arguments.
  std::vector<QLValuePB> args;
  args.reserve(bfcall.operands().size());
  ExprResult<QLValuePB> temp;
  for (const auto& operand : bfcall.operands()) {
    RETURN_NOT_OK(EvalExpr(operand, table_row, temp.Writer()));
    args.emplace_back();
    temp.MoveTo(&args.back());
  }

  // Execute the builtin call associated with the given opcode.
  return ExecBfunc(static_cast<OpCode>(bfcall.opcode()), args);
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalTSCall(const QLBCallPB& ql_expr,
                                  const QLTableRow& table_row,
                                  QLValuePB *result,
                                  const Schema *schema) {
  SetNull(result);
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

Status QLExprExecutor::ReadTSCallValue(const QLBCallPB& ql_expr,
                                       const QLTableRow& table_row,
                                       QLExprResultWriter result_writer) {
  result_writer.SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalCondition(const QLConditionPB& condition,
                                     const QLTableRow& table_row,
                                     bool* result) {
  QLValuePB result_pb;
  RETURN_NOT_OK(EvalCondition(condition, table_row, &result_pb));
  *result = result_pb.bool_value();
  return Status::OK();
}

Result<bool> Contains(
    const ::google::protobuf::RepeatedPtrField<::yb::QLValuePB>& elems, const QLValuePB& value) {
  for (const auto& lhs_element : elems) {
    if (!Comparable(lhs_element, value)) {
      return STATUS_FORMAT(
          RuntimeError, "values not comparable LHS:%s and RHS:%s",
          InternalTypeToCQLString(lhs_element.value_case()),
          InternalTypeToCQLString(value.value_case()));
    }
    if (lhs_element == value) {
      return true;
    }
  }
  return false;
}

template <class Operands, class Row, class Res>
Result<bool> Contains(
    QLExprExecutor* executor, const Operands& operands, const Row& table_row, Res* lhs) {
  Res rhs(lhs);
  RETURN_NOT_OK(EvalOperands(executor, operands, table_row, lhs->Writer(), rhs.Writer()));
  ::google::protobuf::RepeatedPtrField<::yb::QLValuePB> elems;

  if (lhs->Value().has_set_value()) {
    elems = lhs->Value().set_value().elems();
  } else if (lhs->Value().has_map_value()) {
    elems = lhs->Value().map_value().values();
  } else if (lhs->Value().has_list_value()) {
    elems = lhs->Value().list_value().elems();
  } else if (!IsNull(lhs->Value())) {
    return STATUS_FORMAT(
        RuntimeError, "value of LHS is not a collection. Received:%s",
        InternalTypeToCQLString(lhs->Value().value_case()));
  }

  // In case LHS is empty (lhs->Value().type() is QLValuePB::VALUE_NOT_SET)
  // elems will be empty and Contains() will return false.
  return Contains(elems, rhs.Value());
}

template <class Operands, class Row, class Res>
Result<bool> ContainsKey(
    QLExprExecutor* executor, const Operands& operands, const Row& table_row, Res* lhs) {
  Res rhs(lhs);
  RETURN_NOT_OK(EvalOperands(executor, operands, table_row, lhs->Writer(), rhs.Writer()));
  ::google::protobuf::RepeatedPtrField<::yb::QLValuePB> elems;

  if (lhs->Value().has_map_value()) {
    elems = lhs->Value().map_value().keys();
  } else if (!IsNull(lhs->Value())) {
    return STATUS_FORMAT(
        RuntimeError, "value of LHS is not a Map. Received:%s",
        InternalTypeToCQLString(lhs->Value().value_case()));
  }

  // In case LHS is empty (lhs->Value().type() is QLValuePB::VALUE_NOT_SET)
  // elems will be empty and Contains() will return false.
  return Contains(elems, rhs.Value());
}

template <class Operands, class Row, class Res>
Result<bool> In(
    QLExprExecutor* executor, const Operands& operands, const Row& table_row, Res* lhs) {
  Res rhs(lhs);
  RETURN_NOT_OK(EvalOperands(executor, operands, table_row, lhs->Writer(), rhs.Writer()));
  for (const auto& rhs_elem : rhs.Value().list_value().elems()) {
    if (rhs_elem.has_tuple_value() && lhs->Value().has_tuple_value()) {
      const auto& rhs_elem_tuple = rhs_elem.tuple_value().elems();
      const auto& lhs_tuple = lhs->Value().tuple_value().elems();
      if (rhs_elem_tuple.size() != lhs_tuple.size()) {
        return STATUS(RuntimeError, "Tuples of different size cannot be compared");
      }
      bool matched = true;
      auto r_itr = rhs_elem_tuple.begin();
      auto l_itr = lhs_tuple.begin();
      for (size_t i = 0; i < static_cast<size_t>(rhs_elem_tuple.size()); i++, ++r_itr, ++l_itr) {
        if (!Comparable(*r_itr, *r_itr)) {
          return STATUS(RuntimeError, "values not comparable");
        }
        if (*r_itr != *l_itr) {
          matched = false;
          break;
        }
      }
      if (matched) {
        return true;
      }
    } else {
      if (!Comparable(rhs_elem, lhs->Value())) {
        return STATUS(RuntimeError, "values not comparable");
      }
      if (rhs_elem == lhs->Value()) {
        return true;
      }
    }
  }

  return false;
}

template <class Operands, class Row, class Op, class Res>
Result<bool> EvalRelationalOp(
    QLExprExecutor* executor, const Operands& operands, const Row& table_row, const Op& op,
    Res* lhs) {
  Res rhs(lhs);
  RETURN_NOT_OK(EvalOperands(executor, operands, table_row, lhs->Writer(), rhs.Writer()));
  if (!Comparable(lhs->Value(), rhs.Value())) {
    return STATUS(RuntimeError, "values not comparable");
  }
  return op(lhs->Value(), rhs.Value());
}

template<bool Value, class Operands, class Row, class Res>
Result<bool> Is(
  QLExprExecutor* executor, const Operands& operands, const Row& table_row, Res* result) {
  RETURN_NOT_OK(EvalOperands(executor, operands, table_row, result->Writer()));
  if (result->Value().value_case() != InternalType::kBoolValue) {
    return STATUS(RuntimeError, "not a bool value");
  }
  return !IsNull(result->Value()) && result->Value().bool_value() == Value;
}

template<class Operands, class Row, class Res>
Result<bool> Between(
  QLExprExecutor* executor, const Operands& operands, const Row& table_row, Res* temp) {
  CHECK_EQ(operands.size(), 3);
  Res lower(temp), upper(temp);
  RETURN_NOT_OK(EvalOperands(
       executor, operands, table_row, temp->Writer(), lower.Writer(), upper.Writer()));
  if (!Comparable(temp->Value(), lower.Value()) || !Comparable(temp->Value(), upper.Value())) {
    return STATUS(RuntimeError, "values not comparable");
  }
  return temp->Value() >= lower.Value() && temp->Value() <= upper.Value();
}

Status QLExprExecutor::EvalCondition(const QLConditionPB& condition,
                                     const QLTableRow& table_row,
                                     QLValuePB *result) {
#define QL_EVALUATE_RELATIONAL_OP(op) \
  result->set_bool_value(VERIFY_RESULT(EvalRelationalOp(this, operands, table_row, op, &temp))); \
  return Status::OK();

  QLExprResult temp;
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case QL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.Get(0).expr_case(), QLExpressionPB::ExprCase::kCondition);
      QLValuePB sub_result;
      RETURN_NOT_OK(EvalCondition(operands.Get(0).condition(), table_row, &sub_result));
      result->set_bool_value(!sub_result.bool_value());
      return Status::OK();
    }

    case QL_OP_IS_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, temp.Writer()));
      result->set_bool_value(IsNull(temp.Value()));
      return Status::OK();

    case QL_OP_IS_NOT_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(operands.Get(0), table_row, temp.Writer()));
      result->set_bool_value(!IsNull(temp.Value()));
      return Status::OK();

    case QL_OP_IS_TRUE:
      result->set_bool_value(VERIFY_RESULT(Is<true>(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_IS_FALSE:
      result->set_bool_value(VERIFY_RESULT(Is<false>(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::equal_to<>());

    case QL_OP_LESS_THAN:
      QL_EVALUATE_RELATIONAL_OP(std::less<>());

    case QL_OP_LESS_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::less_equal<>());

    case QL_OP_GREATER_THAN:
      QL_EVALUATE_RELATIONAL_OP(std::greater<>());

    case QL_OP_GREATER_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::greater_equal<>());

    case QL_OP_NOT_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::not_equal_to<>());

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
      result->set_bool_value(VERIFY_RESULT(Between(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_NOT_BETWEEN:
      result->set_bool_value(!VERIFY_RESULT(Between(this, operands, table_row, &temp)));
      return Status::OK();

    // When a row exists, the primary key columns are always populated in the row (value-map) by
    // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
    // the row (value-map) is not empty.
    case QL_OP_EXISTS:
      result->set_bool_value(!table_row.IsEmpty());
      return Status::OK();

    case QL_OP_NOT_EXISTS:
      result->set_bool_value(table_row.IsEmpty());
      return Status::OK();

    case QL_OP_IN:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(VERIFY_RESULT(In(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_NOT_IN:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(!VERIFY_RESULT(In(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_CONTAINS_KEY:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(VERIFY_RESULT(ContainsKey(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_CONTAINS:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(VERIFY_RESULT(Contains(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_LIKE: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE:
      LOG(ERROR) << "Internal error: illegal or unknown operator " << condition.op();
      break;

    case QL_OP_NOOP:
      break;
  }

  SetNull(result);
  return STATUS(RuntimeError, "Internal error: illegal or unknown operator");

#undef QL_EVALUATE_RELATIONAL_OP
}

//--------------------------------------------------------------------------------------------------

bfpg::TSOpcode GetTSWriteInstruction(const PgsqlExpressionPB& ql_expr) {
  // "kSubDocInsert" instructs the tablet server to insert a new value or replace an existing value.
  if (ql_expr.has_tscall()) {
    return static_cast<bfpg::TSOpcode>(ql_expr.tscall().opcode());
  }
  return bfpg::TSOpcode::kScalarInsert;
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalExpr(const PgsqlExpressionPB& ql_expr,
                                const dockv::PgTableRow* table_row,
                                QLExprResultWriter result_writer,
                                const Schema *schema) {
  return DoEvalExpr(ql_expr, table_row, result_writer, schema);
}

template <class PB, class Row, class Writer>
Status QLExprExecutor::DoEvalExpr(const PB& ql_expr,
                                  const Row* table_row,
                                  Writer result_writer,
                                  const Schema *schema) {
  switch (ql_expr.expr_case()) {
    case PgsqlExpressionPB::ExprCase::kValue:
      result_writer.SetExisting(&ql_expr.value());
      break;

    case PgsqlExpressionPB::ExprCase::kColumnId:
      return EvalColumnRef(ql_expr.column_id(), table_row, result_writer);

    case PgsqlExpressionPB::ExprCase::kBfcall:
      result_writer.NewValue() = VERIFY_RESULT(
          EvalBFCall<bfpg::BFOpcode>(ql_expr.bfcall(), *table_row));
      return Status::OK();

    case PgsqlExpressionPB::ExprCase::kTscall:
      return EvalTSCall(ql_expr.tscall(), *table_row, &result_writer.NewValue(), schema);

    case PgsqlExpressionPB::ExprCase::kCondition:
      return EvalCondition(ql_expr.condition(), *table_row, &result_writer.NewValue());

    case PgsqlExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::kTuple: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::kAliasId: FALLTHROUGH_INTENDED;
    case PgsqlExpressionPB::ExprCase::EXPR_NOT_SET:
      result_writer.SetNull();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::ReadExprValue(const PgsqlExpressionPB& ql_expr,
                                     const dockv::PgTableRow& table_row,
                                     QLExprResultWriter result_writer) {
  if (ql_expr.expr_case() == PgsqlExpressionPB::ExprCase::kTscall) {
    return ReadTSCallValue(ql_expr.tscall(), table_row, result_writer);
  } else {
    return EvalExpr(ql_expr, table_row, result_writer);
  }
}

//--------------------------------------------------------------------------------------------------

template <class Row, class Writer>
Status QLExprExecutor::EvalColumnRef(
    ColumnIdRep col_id, const Row* table_row, Writer result_writer) {
  if (table_row == nullptr) {
    result_writer.SetNull();
    return Status::OK();
  }
  if (col_id >= 0) {
    result_writer.NewValue() = table_row->GetQLValuePB(col_id);
    return Status::OK();
  }

  return GetSpecialColumn(col_id, &result_writer.NewValue());
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalTSCall(const PgsqlBCallPB& ql_expr,
                                  const dockv::PgTableRow& table_row,
                                  QLValuePB *result,
                                  const Schema *schema) {
  SetNull(result);
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

Status QLExprExecutor::ReadTSCallValue(const PgsqlBCallPB& ql_expr,
                                       const dockv::PgTableRow& table_row,
                                       QLExprResultWriter result_writer) {
  result_writer.SetNull();
  return STATUS(RuntimeError, "Only tablet server can execute this operator");
}

//--------------------------------------------------------------------------------------------------

Status QLExprExecutor::EvalCondition(const PgsqlConditionPB& condition,
                                     const dockv::PgTableRow& table_row,
                                     bool* result) {
  QLValuePB result_pb;
  RETURN_NOT_OK(EvalCondition(condition, table_row, &result_pb));
  *result = result_pb.bool_value();
  return Status::OK();
}

template <class PB, class Row, class Value>
Status QLExprExecutor::EvalCondition(
    const PB& condition, const Row& table_row, Value* result) {
#define QL_EVALUATE_RELATIONAL_OP(op) \
  result->set_bool_value(VERIFY_RESULT(EvalRelationalOp(this, operands, table_row, op, &temp))); \
  return Status::OK();

  ExprResult<Value> temp(result);
  const auto& operands = condition.operands();
  switch (condition.op()) {
    case QL_OP_NOT: {
      CHECK_EQ(operands.size(), 1);
      CHECK_EQ(operands.begin()->expr_case(), PgsqlExpressionPB::ExprCase::kCondition);
      RETURN_NOT_OK(EvalCondition(operands.begin()->condition(), table_row, &temp.ForceNewValue()));
      result->set_bool_value(!temp.Value().bool_value());
      return Status::OK();
    }

    case QL_OP_IS_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(*operands.begin(), table_row, temp.Writer()));
      result->set_bool_value(IsNull(temp.Value()));
      return Status::OK();

    case QL_OP_IS_NOT_NULL:
      CHECK_EQ(operands.size(), 1);
      RETURN_NOT_OK(EvalExpr(*operands.begin(), table_row, temp.Writer()));
      result->set_bool_value(!IsNull(temp.Value()));
      return Status::OK();

    case QL_OP_IS_TRUE:
      result->set_bool_value(VERIFY_RESULT(Is<true>(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_IS_FALSE: {
      result->set_bool_value(VERIFY_RESULT(Is<false>(this, operands, table_row, &temp)));
      return Status::OK();
    }

    case QL_OP_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::equal_to<>());

    case QL_OP_LESS_THAN:
      QL_EVALUATE_RELATIONAL_OP(std::less<>());

    case QL_OP_LESS_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::less_equal<>());

    case QL_OP_GREATER_THAN:
      QL_EVALUATE_RELATIONAL_OP(std::greater<>());

    case QL_OP_GREATER_THAN_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::greater_equal<>());

    case QL_OP_NOT_EQUAL:
      QL_EVALUATE_RELATIONAL_OP(std::not_equal_to<>());

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
      result->set_bool_value(VERIFY_RESULT(Between(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_NOT_BETWEEN:
      result->set_bool_value(!VERIFY_RESULT(Between(this, operands, table_row, &temp)));
      return Status::OK();

      // When a row exists, the primary key columns are always populated in the row (value-map) by
      // DocRowwiseIterator and only when it exists. Therefore, the row exists if and only if
      // the row (value-map) is not empty.
    case QL_OP_EXISTS:
      result->set_bool_value(table_row.Exists());
      return Status::OK();

    case QL_OP_NOT_EXISTS:
      result->set_bool_value(!table_row.Exists());
      return Status::OK();

    case QL_OP_CONTAINS_KEY:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(VERIFY_RESULT(ContainsKey(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_CONTAINS:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(VERIFY_RESULT(Contains(this, operands, table_row, &temp)));
      return Status::OK();

    case QL_OP_IN:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(VERIFY_RESULT(In(this, operands, table_row, &temp)));
      break;

    case QL_OP_NOT_IN:
      CHECK_EQ(operands.size(), 2);
      result->set_bool_value(!VERIFY_RESULT(In(this, operands, table_row, &temp)));
      break;

    case QL_OP_LIKE: FALLTHROUGH_INTENDED;
    case QL_OP_NOT_LIKE:
      LOG(ERROR) << "Internal error: illegal or unknown operator " << condition.op();
      break;

    case QL_OP_NOOP:
      break;
  }

  SetNull(result);
  return STATUS(RuntimeError, "Internal error: illegal or unknown operator");

#undef QL_EVALUATE_RELATIONAL_OP
#undef QL_EVALUATE_BETWEEN
}

//--------------------------------------------------------------------------------------------------

const QLTableRow& QLTableRow::empty_row() {
  static QLTableRow empty_row;
  return empty_row;
}

size_t QLTableRow::ColumnCount() const {
  size_t result = 0;
  for (auto i : assigned_) {
    result += i;
  }
  return result;
}

void QLTableRow::Clear() {
  if (num_assigned_ == 0) {
    return;
  }

  memset(assigned_.data(), 0x0, assigned_.size());
  num_assigned_ = 0;
}

size_t QLTableRow::ColumnIndex(ColumnIdRep col_id) const {
  if (col_id < kFirstNonPreallocatedColumnId && col_id >= kFirstColumnIdRep) {
    return col_id - kFirstColumnIdRep;
  }
  const auto& col_iter = column_id_to_index_.find(col_id);
  if (col_iter == column_id_to_index_.end()) {
    return kInvalidIndex;
  }

  return col_iter->second;
}

const QLTableColumn* QLTableRow::FindColumn(ColumnIdRep col_id) const {
  size_t index = ColumnIndex(col_id);
  if (index == kInvalidIndex || index >= assigned_.size() || !assigned_[index]) {
    return nullptr;
  }

  return &values_[index];
}

const QLValuePB* QLTableRow::GetColumn(ColumnIdRep col_id) const {
  const auto* column = FindColumn(col_id);
  return column ? &column->value : nullptr;
}

void SetColumnValue(const QLValuePB& value, QLExprResultWriter writer) {
  writer.SetExisting(&value);
}

void SetColumnValue(const QLValuePB& value, LWExprResultWriter writer) {
  writer.NewValue() = value;
}

template <class Writer>
Status QLTableRow::DoReadColumn(ColumnIdRep col_id, Writer result_writer) const {
  auto value = GetColumn(col_id);
  if (value == nullptr) {
    result_writer.SetNull();
    return Status::OK();
  }

  SetColumnValue(*value, result_writer);
  return Status::OK();
}

Status QLTableRow::ReadColumn(ColumnIdRep col_id, QLExprResultWriter result_writer) const {
  return DoReadColumn(col_id, result_writer);
}

Status QLTableRow::ReadColumn(ColumnIdRep col_id, LWExprResultWriter result_writer) const {
  return DoReadColumn(col_id, result_writer);
}

Status QLTableRow::ReadSubscriptedColumn(const QLSubscriptedColPB& subcol,
                                         const QLValuePB& index_arg,
                                         QLExprResultWriter result_writer) const {
  const auto* value = GetColumn(subcol.column_id());
  if (!value) {
    // Does not exist.
    result_writer.SetNull();
    return Status::OK();
  }

  if (value->has_map_value()) {
    // map['key']
    auto& map = value->map_value();
    for (int i = 0; i < map.keys_size(); i++) {
      if (map.keys(i) == index_arg) {
        result_writer.SetExisting(&map.values(i));
        return Status::OK();
      }
    }
  } else if (value->has_list_value()) {
    // list[index]
    auto& list = value->list_value();
    if (index_arg.has_int32_value()) {
      int list_index = index_arg.int32_value();
      if (list_index >= 0 && list_index < list.elems_size()) {
        result_writer.SetExisting(&list.elems(list_index));
        return Status::OK();
      }
    }
  }

  result_writer.SetNull();
  return Status::OK();
}

Result<const QLTableColumn&> QLTableRow::Column(ColumnIdRep col_id) const {
  const auto* column = FindColumn(col_id);
  if (column == nullptr) {
    // Does not exist.
    return STATUS_FORMAT(
        InternalError, "Column ($0) unexpectedly not found in cache", col_id);
  }

  return *column;
}

Status QLTableRow::GetTTL(ColumnIdRep col_id, int64_t *ttl_seconds) const {
  *ttl_seconds = VERIFY_RESULT(Column(col_id)).get().ttl_seconds;
  return Status::OK();
}

Status QLTableRow::GetWriteTime(ColumnIdRep col_id, int64_t *write_time) const {
  const QLTableColumn& column = VERIFY_RESULT(Column(col_id));
  DCHECK_NE(QLTableColumn::kUninitializedWriteTime, column.write_time) << "Column id: " << col_id;
  *write_time = column.write_time;
  return Status::OK();
}

Status QLTableRow::GetValue(ColumnIdRep col_id, QLValue *column) const {
  *column = VERIFY_RESULT(Column(col_id)).get().value;
  return Status::OK();
}

Status QLTableRow::GetValue(const ColumnId& col, QLValue *column) const {
  return GetValue(col.rep(), column);
}

boost::optional<const QLValuePB&> QLTableRow::GetValue(ColumnIdRep col_id) const {
  const auto* column = FindColumn(col_id);
  if (column) {
    return column->value;
  }
  return boost::none;
}

bool QLTableRow::IsColumnSpecified(ColumnIdRep col_id) const {
  size_t index = ColumnIndex(col_id);
  if (index == kInvalidIndex) {
    LOG(DFATAL) << "Checking whether unknown column is specified: " << col_id;
    return false;
  }
  return assigned_[index];
}

void QLTableRow::MarkTombstoned(ColumnIdRep col_id) {
  AllocColumn(col_id).value.Clear();
}

bool QLTableRow::MatchColumn(ColumnIdRep col_id, const QLTableRow& source) const {
  const auto* this_column = FindColumn(col_id);
  const auto* source_column = source.FindColumn(col_id);
  if (this_column && source_column) {
    return this_column->value == source_column->value;
  }
  return !this_column && !source_column;
}

QLTableColumn& QLTableRow::AppendColumn() {
  values_.emplace_back();
  assigned_.push_back(true);
  ++num_assigned_;
  return values_.back();
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id) {
  size_t index = col_id;
  if (index < kFirstNonPreallocatedColumnId && index >= kFirstColumnIdRep) {
    index -= kFirstColumnIdRep;
    // We are in directly mapped part. Ensure that vector is big enough.
    if (values_.size() <= index) {
      // We don't need reserve here, because no allocation would take place.
      if (values_.size() < index) {
        values_.resize(index);
        assigned_.resize(index);
      }
      // This column was not yet allocated, so allocate it. Also vector has `col_id` size, so
      // new column will be added at `col_id` position.
      return AppendColumn();
    }
  } else {
    // We are in part that is mapped using `column_id_to_index_`, so need to allocate at least
    // part that is mapped directly, to avoid index overlapping.
    if (values_.size() < kPreallocatedSize) {
      values_.resize(kPreallocatedSize);
      assigned_.resize(kPreallocatedSize);
    }
    auto iterator_and_flag = column_id_to_index_.emplace(col_id, values_.size());
    if (iterator_and_flag.second) {
      return AppendColumn();
    }
    index = iterator_and_flag.first->second;
  }

  if (!assigned_[index]) {
    assigned_[index] = true;
    ++num_assigned_;
  }
  return values_[index];
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id, const QLValue& ql_value) {
  return AllocColumn(col_id, ql_value.value());
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id, const QLValuePB& ql_value) {
  QLTableColumn& result = AllocColumn(col_id);
  result.value = ql_value;
  return result;
}

QLTableColumn& QLTableRow::AllocColumn(ColumnIdRep col_id, QLValuePB&& ql_value) {
  QLTableColumn& result = AllocColumn(col_id);
  result.value = std::move(ql_value);
  return result;
}

void QLTableRow::CopyColumn(ColumnIdRep col_id, const QLTableRow& source) {
  const auto* value = source.FindColumn(col_id);
  if (value) {
    AllocColumn(col_id) = *value;
    return;
  }

  auto index = ColumnIndex(col_id);
  if (index == kInvalidIndex) {
    return;
  }
  if (assigned_[index]) {
    assigned_[index] = false;
    --num_assigned_;
  }
}

std::string QLTableRow::ToString() const {
  std::string ret("{ ");

  for (size_t i = 0; i != kPreallocatedSize; ++i) {
    if (i >= values_.size()) {
      break;
    }
    if (!assigned_[i]) {
      continue;
    }
    ret.append(Format("$0 => $1 ", i + kFirstColumnIdRep, values_[i]));
  }

  for (auto p : column_id_to_index_) {
    if (!assigned_[p.second]) {
      continue;
    }

    ret.append(Format("$0 => $1 ", p.first, values_[p.second]));
  }

  ret.append("}");
  return ret;
}

std::string QLTableRow::ToString(const Schema& schema) const {
  std::string ret;
  ret.append("{ ");

  for (size_t col_idx = 0; col_idx < schema.num_columns(); col_idx++) {
    const auto* value = GetColumn(schema.column_id(col_idx));
    if (value && value->value_case() != QLValuePB::VALUE_NOT_SET) {
      ret += value->ShortDebugString();
    } else {
      ret += "null";
    }
    ret += ' ';
  }
  ret.append("}");
  return ret;
}


const QLValuePB& QLExprResult::Value() {
  if (existing_value_) {
    return *existing_value_;
  }

  return value_;
}

void QLExprResult::MoveTo(QLValuePB* out) {
  if (existing_value_) {
    *out = *existing_value_;
    existing_value_ = nullptr;
  } else {
    value_.Swap(out);
  }
}

QLValuePB& QLExprResult::ForceNewValue() {
  if (existing_value_) {
    value_ = *existing_value_;
    existing_value_ = nullptr;
  }

  return value_;
}

QLExprResultWriter QLExprResult::Writer() {
  return QLExprResultWriter(this);
}


void LWExprResult::MoveTo(LWQLValuePB* out) {
  if (value_) {
    *out = *value_; // TODO(LW_PERFORM)
  } else {
    yb::SetNull(out);
  }
}

LWQLValuePB& LWExprResult::ForceNewValue() {
  return *(value_ = arena_->NewObject<LWQLValuePB>(arena_));
}

const LWQLValuePB& LWExprResult::Value() {
  if (!value_) {
    return ForceNewValue();
  }
  return *value_;
}

LWExprResultWriter LWExprResult::Writer() {
  return LWExprResultWriter(this);
}

std::string QLTableColumn::ToString() const {
  return Format("{ value: $0 ttl_seconds: $1 write_time: $2 }", value, ttl_seconds,
                write_time == kUninitializedWriteTime ? "kUninitializedWriteTime":
                                                        std::to_string(write_time));
}

} // namespace yb::qlexpr
