//--------------------------------------------------------------------------------------------------
// Copyright (c) YugabyteDB, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/docdb/doc_expr.h"

#include <iostream>
#include <string>

#include "yb/bfql/bfunc_standard.h"

#include "yb/common/jsonb.h"
#include "yb/common/pg_system_attr.h"
#include "yb/common/pgsql_protocol.messages.h"
#include "yb/common/ql_datatype.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/docdb_pgapi.h"

#include "yb/gutil/endian.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/uuid.h"

using std::string;
using std::vector;

namespace yb {
namespace docdb {

using util::Decimal;
using qlexpr::QLExprResult;
using qlexpr::QLTableRow;

//--------------------------------------------------------------------------------------------------

DocExprExecutor::DocExprExecutor() = default;

DocExprExecutor::~DocExprExecutor() = default;

//--------------------------------------------------------------------------------------------------

Status DocExprExecutor::EvalTSCall(const QLBCallPB& tscall,
                                   const QLTableRow& table_row,
                                   QLValuePB* result,
                                   const Schema* schema) {
  return DoEvalTSCall(tscall, table_row, result, schema);
}

Status DocExprExecutor::EvalTSCall(const LWQLBCallPB& tscall,
                                   const QLTableRow& table_row,
                                   LWQLValuePB* result,
                                   const Schema* schema) {
  return DoEvalTSCall(tscall, table_row, result, schema);
}

template <class PB, class Value>
Status DocExprExecutor::DoEvalTSCall(const PB& tscall,
                                     const QLTableRow& table_row,
                                     Value* result,
                                     const Schema *schema) {
  bfql::TSOpcode tsopcode = static_cast<bfql::TSOpcode>(tscall.opcode());
  switch (tsopcode) {
    case bfql::TSOpcode::kNoOp:
    case bfql::TSOpcode::kScalarInsert:
      LOG(FATAL) << "Client should not generate function call instruction with operator "
                 << static_cast<int>(tsopcode);
      break;

    case bfql::TSOpcode::kTtl: {
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      int64_t ttl_seconds = -1;
      RETURN_NOT_OK(table_row.GetTTL(tscall.operands().begin()->column_id(), &ttl_seconds));
      if (ttl_seconds != -1) {
        result->set_int64_value(ttl_seconds);
      } else {
        SetNull(result);
      }
      return Status::OK();
    }

    case bfql::TSOpcode::kWriteTime: {
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      int64_t write_time = 0;
      RETURN_NOT_OK(table_row.GetWriteTime(tscall.operands().begin()->column_id(), &write_time));
      result->set_int64_value(write_time);
      return Status::OK();
    }

    case bfql::TSOpcode::kCount:
      if (tscall.operands().begin()->has_column_id()) {
        // Check if column value is NULL. CQL does not count NULL value of a column.
        qlexpr::ExprResult<Value> arg_result(result);
        RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
        if (IsNull(arg_result.Value())) {
          return Status::OK();
        }
      }
      return EvalCount(result);

    case bfql::TSOpcode::kSum: {
      qlexpr::ExprResult<Value> arg_result(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
      return EvalSum(arg_result.Value(), result);
    }

    case bfql::TSOpcode::kMin: {
      qlexpr::ExprResult<Value> arg_result(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
      return EvalMin(arg_result.Value(), result);
    }

    case bfql::TSOpcode::kMax: {
      qlexpr::ExprResult<Value> arg_result(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
      return EvalMax(arg_result.Value(), result);
    }

    case bfql::TSOpcode::kAvg: {
      qlexpr::ExprResult<Value> arg_result(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
      return EvalAvg(arg_result.Value(), result);
    }

    case bfql::TSOpcode::kMapExtend: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kMapRemove: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kSetExtend: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kSetRemove: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kListAppend: {
      // Return the value of the second operand. The first operand must be a column ID.
      qlexpr::ExprResult<Value> temp(result);
      RETURN_NOT_OK(EvalExpr(*std::next(tscall.operands().begin()), table_row, temp.Writer()));
      temp.MoveTo(result);
      return Status::OK();
    }
    case bfql::TSOpcode::kListPrepend: {
      // Return the value of the first operand. The second operand is a column ID.
      qlexpr::ExprResult<Value> temp(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, temp.Writer()));
      temp.MoveTo(result);
      return Status::OK();
    }
    case bfql::TSOpcode::kListRemove: {
      qlexpr::ExprResult<Value> org_list_result(result);
      qlexpr::ExprResult<Value> sub_list_result(result);
      RETURN_NOT_OK(EvalOperands(
          this, tscall.operands(), table_row, org_list_result.Writer(), sub_list_result.Writer()));
      const auto& org_list_value = org_list_result.Value();
      const auto& sub_list_value = sub_list_result.Value();

      result->mutable_list_value();
      if (!IsNull(org_list_value) && !IsNull(sub_list_value)) {
        const auto& org_list = org_list_value.list_value();
        const auto& sub_list = sub_list_value.list_value();
        for (const auto& org_elem : org_list.elems()) {
          bool should_remove = false;
          for (const auto& sub_elem : sub_list.elems()) {
            if (org_elem == sub_elem) {
              should_remove = true;
              break;
            }
          }
          if (!should_remove) {
            *result->mutable_list_value()->add_elems() = std::move(org_elem);
          }
        }
      }
      return Status::OK();
    }

    case bfql::TSOpcode::kToJson:
      return EvalParametricToJson(*tscall.operands().begin(), table_row, schema, result);
  }

  SetNull(result);
  return Status::OK();
}

Status DocExprExecutor::EvalTSCall(const PgsqlBCallPB& tscall,
                                   const dockv::PgTableRow& table_row,
                                   QLValuePB* result,
                                   const Schema *schema) {
  return DoEvalTSCall(tscall, table_row, result, schema);
}

Status DocExprExecutor::EvalTSCall(const LWPgsqlBCallPB& tscall,
                                   const dockv::PgTableRow& table_row,
                                   LWQLValuePB* result,
                                   const Schema *schema) {
  return DoEvalTSCall(tscall, table_row, result, schema);
}

template <class PB, class Value>
Status DocExprExecutor::DoEvalTSCall(const PB& tscall,
                                     const dockv::PgTableRow& table_row,
                                     Value* result,
                                     const Schema *schema) {
  bfpg::TSOpcode tsopcode = static_cast<bfpg::TSOpcode>(tscall.opcode());
  switch (tsopcode) {
    case bfpg::TSOpcode::kCount: {
      const auto& operand = *tscall.operands().begin();
      if (operand.has_column_id()) {
        // Check if column value is NULL. Postgres does not count NULL value of a column, unless
        // it's COUNT(*).
        qlexpr::ExprResult<Value> arg_result(result);
        RETURN_NOT_OK(EvalExpr(operand, table_row, arg_result.Writer()));
        if (IsNull(arg_result.Value())) {
          return Status::OK();
        }
      } else if (operand.has_value() && IsNull(operand.value())) {
        // We've got COUNT(null) which is bound to return zero.
        return Status::OK();
      }
      return EvalCount(result);
    }

    case bfpg::TSOpcode::kSumInt8:
      return EvalSumInt(*tscall.operands().begin(), table_row, result, [](const auto& value) {
        return value.int8_value();
      });

    case bfpg::TSOpcode::kSumInt16:
      return EvalSumInt(*tscall.operands().begin(), table_row, result, [](const auto& value) {
        return value.int16_value();
      });

    case bfpg::TSOpcode::kSumInt32:
      return EvalSumInt(*tscall.operands().begin(), table_row, result, [](const auto& value) {
        return value.int32_value();
      });

    case bfpg::TSOpcode::kSumInt64:
      return EvalSumInt(*tscall.operands().begin(), table_row, result, [](const auto& value) {
        return value.int64_value();
      });

    case bfpg::TSOpcode::kSumFloat:
      return EvalSumReal(
          *tscall.operands().begin(), table_row, result,
          [](const auto& value) { return value.float_value(); },
          [](float value, auto* out) { return out->set_float_value(value); });

    case bfpg::TSOpcode::kSumDouble:
      return EvalSumReal(
          *tscall.operands().begin(), table_row, result,
          [](const auto& value) { return value.double_value(); },
          [](double value, auto* out) { return out->set_double_value(value); });

    case bfpg::TSOpcode::kMin: {
      qlexpr::ExprResult<Value> arg_result(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
      return EvalMin(arg_result.Value(), result);
    }

    case bfpg::TSOpcode::kMax: {
      qlexpr::ExprResult<Value> arg_result(result);
      RETURN_NOT_OK(EvalExpr(*tscall.operands().begin(), table_row, arg_result.Writer()));
      return EvalMax(arg_result.Value(), result);
    }

    case bfpg::TSOpcode::kPgEvalExprCall: {
      // Support for serialized Postgres expression evaluation has been moved to separate class
      // DocPgExprExecutor, it should be instantiated to handle kPgEvalExprCall type of expressions
      DCHECK(false);
      return Status::OK();
    }

    default:
      LOG(FATAL) << "Client should not generate function call instruction with operator "
                 << static_cast<int>(tsopcode);
      break;
  }

  SetNull(result);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

template <class Value>
Status DocExprExecutor::EvalCount(Value* aggr_count) {
  if (IsNull(*aggr_count)) {
    aggr_count->set_int64_value(1);
  } else {
    aggr_count->set_int64_value(aggr_count->int64_value() + 1);
  }
  return Status::OK();
}

template <class Value>
Status DocExprExecutor::EvalSum(const Value& val, Value* aggr_sum) {
  if (IsNull(val)) {
    return Status::OK();
  }

  if (IsNull(*aggr_sum)) {
    *aggr_sum = val;
    return Status::OK();
  }

  switch (aggr_sum->value_case()) {
    case InternalType::kInt8Value:
      aggr_sum->set_int8_value(aggr_sum->int8_value() + val.int8_value());
      break;
    case InternalType::kInt16Value:
      aggr_sum->set_int16_value(aggr_sum->int16_value() + val.int16_value());
      break;
    case InternalType::kInt32Value:
      aggr_sum->set_int32_value(aggr_sum->int32_value() + val.int32_value());
      break;
    case InternalType::kInt64Value:
      aggr_sum->set_int64_value(aggr_sum->int64_value() + val.int64_value());
      break;
    case InternalType::kVarintValue: {
      auto value = (QLValue::varint_value(*aggr_sum) + QLValue::varint_value(val));
      if constexpr (std::is_same_v<Value, QLValuePB>) {
        aggr_sum->set_varint_value(value.EncodeToComparable());
      } else {
        aggr_sum->dup_varint_value(value.EncodeToComparable());
      }
      break;
    }
    case InternalType::kFloatValue:
      aggr_sum->set_float_value(aggr_sum->float_value() + val.float_value());
      break;
    case InternalType::kDoubleValue:
      aggr_sum->set_double_value(aggr_sum->double_value() + val.double_value());
      break;
    case InternalType::kDecimalValue: {
      Decimal sum, value;
      RETURN_NOT_OK(sum.DecodeFromComparable(aggr_sum->decimal_value()));
      RETURN_NOT_OK(value.DecodeFromComparable(val.decimal_value()));
      sum = sum + value;
      if constexpr (std::is_same_v<Value, QLValuePB>) {
        aggr_sum->set_decimal_value(sum.EncodeToComparable());
      } else {
        aggr_sum->dup_decimal_value(sum.EncodeToComparable());
      }
      break;
    }
    default:
      return STATUS(RuntimeError, "Cannot find SUM of this column");
  }
  return Status::OK();
}

template <class Expr, class Row, class Value, class Extractor>
Status DocExprExecutor::EvalSumInt(
    const Expr& operand, const Row& table_row, Value* aggr_sum,
    const Extractor& extractor) {
  qlexpr::ExprResult<Value> arg_result(aggr_sum);
  RETURN_NOT_OK(EvalExpr(operand, table_row, arg_result.Writer()));
  const auto& val = arg_result.Value();

  if (IsNull(val)) {
    return Status::OK();
  }

  if (IsNull(*aggr_sum)) {
    aggr_sum->set_int64_value(extractor(val));
  } else {
    aggr_sum->set_int64_value(aggr_sum->int64_value() + extractor(val));
  }

  return Status::OK();
}

template <class Expr, class Row, class Value, class Extractor, class Setter>
Status DocExprExecutor::EvalSumReal(
    const Expr& operand, const Row& table_row, Value* aggr_sum,
    const Extractor& extractor, const Setter& setter) {
  qlexpr::ExprResult<Value> arg_result(aggr_sum);
  RETURN_NOT_OK(EvalExpr(operand, table_row, arg_result.Writer()));
  const auto& val = arg_result.Value();

  if (IsNull(val)) {
    return Status::OK();
  }

  if (IsNull(*aggr_sum)) {
    setter(extractor(val), aggr_sum);
  } else {
    setter(extractor(*aggr_sum) + extractor(val), aggr_sum);
  }

  return Status::OK();
}

template <class Value>
Status DocExprExecutor::EvalMax(const Value& val, Value* aggr_max) {
  if (!IsNull(val) && (IsNull(*aggr_max) || *aggr_max < val)) {
    *aggr_max = val;
  }
  return Status::OK();
}

template <class Value>
Status DocExprExecutor::EvalMin(const Value& val, Value* aggr_min) {
  if (!IsNull(val) && (IsNull(*aggr_min) || *aggr_min > val)) {
    *aggr_min = val;
  }
  return Status::OK();
}

template <class Value>
Status DocExprExecutor::EvalAvg(const Value& val, Value* aggr_avg) {
  if (IsNull(val)) {
    return Status::OK();
  }

  bool was_null = IsNull(*aggr_avg);
  auto& map = *aggr_avg->mutable_map_value();
  if (was_null) {
    map.add_keys()->set_int64_value(1);
    *map.add_values() = val;
    return Status::OK();
  }

  RETURN_NOT_OK(EvalSum(val, &*map.mutable_values()->begin()));
  map.mutable_keys()->begin()->set_int64_value(map.keys().begin()->int64_value() + 1);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

namespace {

template <class Seq, class Value>
void DoUnpackFronzenUDT(
    const std::vector<std::string>& field_names, const Seq& seq, Value* value) {
  DCHECK_EQ(seq.elems_size(), field_names.size());
  auto& map = *value->mutable_map_value();

  if (implicit_cast<size_t>(seq.elems_size()) == field_names.size()) {
    int i = 0;
    for (const auto& elem : seq.elems()) {
      SetStringValue(*map.add_keys(), field_names[i]);
      *map.add_values() = elem;
      ++i;
    }
  }
}

void UnpackFrozenUDT(
    const std::vector<std::string>& field_names, QLSeqValuePB seq, QLValuePB* value) {
  DoUnpackFronzenUDT(field_names, seq, value);
}

void UnpackFrozenUDT(
    const std::vector<std::string>& field_names, const LWQLSeqValuePB& seq, LWQLValuePB* value) {
  DoUnpackFronzenUDT(field_names, seq, value);
}

void UnpackFrozenMap(const QLType& param_type, QLSeqValuePB seq, QLValuePB* value);
void UnpackFrozenMap(const QLType& param_type, const LWQLSeqValuePB& seq, LWQLValuePB* value);

template <class Value>
void UnpackUDTAndFrozen(const QLType::SharedPtr& type, Value* value) {
  if (type->IsUserDefined() && value->value_case() == QLValuePB::kMapValue) {
    // Change MAP<field_index:field_value> into MAP<field_name:field_value>
    // in case of UDT.
    const auto& field_names = type->udtype_field_names();
    auto& map = *value->mutable_map_value();
    int i = 0;
    auto value_it = map.mutable_values()->begin();
    for (auto& key : *map.mutable_keys()) {
      SetStringValue(key, field_names[i]);
      // Unpack nested FROZEN<UDT>, FROZEN<MAP>, etc.
      UnpackUDTAndFrozen(type->param_type(i), &*value_it);
      ++i;
      ++value_it;
    }
  } else if (type->IsFrozen() && value->value_case() == QLValuePB::kFrozenValue) {
    if (type->param_type()->IsUserDefined()) {
      // Change FROZEN[field_value,...] into MAP<field_name:field_value>
      // in case of FROZEN<UDT>.
      UnpackFrozenUDT(type->param_type()->udtype_field_names(), value->frozen_value(), value);
    } else if (type->param_type()->main() == DataType::MAP) {
      // Case: FROZEN<MAP>=[Key1,Value1,Key2,Value2] -> MAP<Key1:Value1, Key2:Value2>.
      UnpackFrozenMap(*type->param_type(), value->frozen_value(), value);
    } else {
      DCHECK(type->param_type()->main() == DataType::LIST ||
             type->param_type()->main() == DataType::SET);
      // Case: FROZEN<LIST/SET>
      for (auto& elem : *value->mutable_frozen_value()->mutable_elems()) {
        UnpackUDTAndFrozen(type->param_type()->param_type(), &elem);
      }
    }
  } else if (type->main() == DataType::LIST && value->value_case() == QLValuePB::kListValue) {
    for (auto& elem : *value->mutable_list_value()->mutable_elems()) {
      UnpackUDTAndFrozen(type->param_type(), &elem);
    }
  } else if (type->main() == DataType::SET && value->value_case() == QLValuePB::kSetValue) {
    for (auto& elem : *value->mutable_set_value()->mutable_elems()) {
      UnpackUDTAndFrozen(type->param_type(), &elem);
    }
  } else if (type->main() == DataType::MAP && value->value_case() == QLValuePB::kMapValue) {
    auto& map = *value->mutable_map_value();
    DCHECK_EQ(map.keys_size(), map.values_size());
    for (auto& elem : *map.mutable_keys()) {
      UnpackUDTAndFrozen(type->keys_type(), &elem);
    }
    for (auto& elem : *map.mutable_values()) {
      UnpackUDTAndFrozen(type->values_type(), &elem);
    }
  } else if (type->main() == DataType::TUPLE) {
    // https://github.com/YugaByte/yugabyte-db/issues/936
    LOG(FATAL) << "Tuple type not implemented yet";
  }
}

template <class Seq, class Value>
void DoUnpackFronzenMap(const QLType& param_type, const Seq& seq, Value* value) {
  DCHECK_EQ(seq.elems_size() % 2, 0);
  auto& map = *value->mutable_map_value();

  for (auto it = seq.elems().begin(); it != seq.elems().end();) {
    auto* const key = map.add_keys();
    *key = *it++;
    UnpackUDTAndFrozen(param_type.keys_type(), key);

    auto* const val = map.add_values();
    *val = *it++;
    UnpackUDTAndFrozen(param_type.values_type(), val);
  }
}

void UnpackFrozenMap(const QLType& param_type, QLSeqValuePB seq, QLValuePB* value) {
  DoUnpackFronzenMap(param_type, seq, value);
}

void UnpackFrozenMap(const QLType& param_type, const LWQLSeqValuePB& seq, LWQLValuePB* value) {
  DoUnpackFronzenMap(param_type, seq, value);
}

} // namespace

template <class PB, class Value>
Status DocExprExecutor::EvalParametricToJson(
    const PB& operand, const QLTableRow& table_row, const Schema *schema, Value* value) {
  qlexpr::ExprResult<Value> val(&operand);
  RETURN_NOT_OK(EvalExpr(operand, table_row, val.Writer(), schema));

  // Repack parametric types like UDT, FROZEN, SET<FROZEN>, etc.
  if (operand.has_column_id() && schema != nullptr) {
    Result<const ColumnSchema&> col = schema->column_by_id(ColumnId(operand.column_id()));
    DCHECK(col.ok());

    if (col.ok()) {
      UnpackUDTAndFrozen(col->type(), &val.ForceNewValue());
    }
  }

  // Direct call of ToJson() for elementary type.
  *value = std::move(VERIFY_RESULT(bfql::ToJson(val.Value(), bfql::BFFactory())));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

}  // namespace docdb
}  // namespace yb
