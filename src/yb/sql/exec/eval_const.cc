//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/util/bfyql/bfunc.h"
#include "yb/util/net/inetaddress.h"

#include "yb/util/decimal.h"

namespace yb {
namespace sql {

CHECKED_STATUS Executor::PTConstToPB(const PTExpr::SharedPtr& expr,
                                     YQLValuePB *const_pb,
                                     bool negate) {
  DCHECK(expr->expr_op() == ExprOperator::kConst ||
         expr->expr_op() == ExprOperator::kCollection ||
         expr->expr_op() == ExprOperator::kUMinus);
  if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
      YQLValue::SetNull(const_pb);
  }

  if (expr->expr_op() == ExprOperator::kUMinus) {
    return PTUMinusToPB(static_cast<const PTOperator1*>(expr.get()), const_pb);
  }

  const PTExpr *const_pt = expr.get();
  switch (const_pt->yql_type_id()) {
    case DataType::NULL_VALUE_TYPE: {
      YQLValue::SetNull(const_pb);
      break;
    }
    case DataType::VARINT: {
      DCHECK(const_pt->internal_type() == InternalType::kStringValue)
        << "Expecting internal type to be string from the parser";
      return PTExprToPB(static_cast<const PTConstVarInt*>(const_pt), const_pb, negate);
    }
    case DataType::DECIMAL: {
      DCHECK(const_pt->internal_type() == InternalType::kStringValue)
        << "Expecting internal type to be string from the parser";
      return PTExprToPB(static_cast<const PTConstDecimal*>(const_pt), const_pb, negate);
    }
    case DataType::INT64: { // Might be an obsolete case.
      return PTExprToPB(static_cast<const PTConstInt*>(const_pt), const_pb, negate);
    }
    case DataType::DOUBLE: { // Might be an obsolete case.
      return PTExprToPB(static_cast<const PTConstDouble*>(const_pt), const_pb, negate);
    }
    case DataType::STRING: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTConstText*>(const_pt), const_pb);
    }
    case DataType::BOOL: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTConstBool*>(const_pt), const_pb);
    }
    case DataType::UUID: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTConstUuid*>(const_pt), const_pb);
    }
    case DataType::BINARY: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTConstBinary*>(const_pt), const_pb);
    }
    case DataType::MAP: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTMapExpr*>(const_pt), const_pb);
    }
    case DataType::SET: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTSetExpr*>(const_pt), const_pb);
    }
    case DataType::LIST: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTListExpr*>(const_pt), const_pb);
    }
    default:
      LOG(FATAL) << "Unknown datatype for YQL constant value";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstVarInt *const_pt, YQLValuePB *const_pb,
                                    bool negate) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kInt8Value: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToInt64(&value, negate));
      const_pb->set_int8_value(value);
      break;
    }
    case InternalType::kInt16Value: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToInt64(&value, negate));
      const_pb->set_int16_value(value);
      break;
    }
    case InternalType::kInt32Value: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToInt64(&value, negate));
      const_pb->set_int32_value(value);
      break;
    }
    case InternalType::kInt64Value: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToInt64(&value, negate));
      const_pb->set_int64_value(value);
      break;
    }
    case InternalType::kFloatValue: {
      long double value;
      RETURN_NOT_OK(const_pt->ToDouble(&value, negate));
      const_pb->set_float_value(value);
      break;
    }
    case InternalType::kDoubleValue: {
      long double value;
      RETURN_NOT_OK(const_pt->ToDouble(&value, negate));
      const_pb->set_double_value(value);
      break;
    }
    case InternalType::kDecimalValue: {
      std::string value;
      RETURN_NOT_OK(const_pt->ToDecimal(&value, negate));
      const_pb->set_decimal_value(value);
      break;
    }
    case InternalType::kTimestampValue: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToInt64(&value, negate));
      const_pb->set_timestamp_value(DateTime::TimestampFromInt(value).ToInt64());
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstDecimal *const_pt, YQLValuePB *const_pb,
                                    bool negate) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kDecimalValue: {
      string svalue;
      RETURN_NOT_OK(const_pt->ToDecimal(&svalue, negate));
      const_pb->set_decimal_value(svalue);
      break;
    }
    case InternalType::kFloatValue: {
      long double value;
      RETURN_NOT_OK(const_pt->ToDouble(&value, negate));
      const_pb->set_float_value(value);
      break;
    }
    case InternalType::kDoubleValue: {
      long double value;
      RETURN_NOT_OK(const_pt->ToDouble(&value, negate));
      const_pb->set_double_value(value);
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

// The following numeric functions might be needed if we fold constant at compile time.
// Leave them here for now.
CHECKED_STATUS Executor::PTExprToPB(const PTConstInt *const_pt, YQLValuePB *const_pb,
                                    bool negate) {
  int64_t value = const_pt->value();
  if (negate) {
    value = -value;
  }

  switch (const_pt->expected_internal_type()) {
    case InternalType::kInt8Value:
      const_pb->set_int8_value(value);
      break;
    case InternalType::kInt16Value:
      const_pb->set_int16_value(value);
      break;
    case InternalType::kInt32Value:
      const_pb->set_int32_value(value);
      break;
    case InternalType::kInt64Value:
      const_pb->set_int64_value(value);
      break;
    case InternalType::kFloatValue:
      const_pb->set_float_value(value);
      break;
    case InternalType::kDoubleValue:
      const_pb->set_double_value(value);
      break;
    case InternalType::kTimestampValue:
      const_pb->set_timestamp_value(DateTime::TimestampFromInt(value).ToInt64());
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstDouble *const_pt, YQLValuePB *const_pb,
                                    bool negate) {
  long double value = const_pt->value();
  if (negate) {
    value = -value;
  }

  switch (const_pt->expected_internal_type()) {
    case InternalType::kFloatValue:
      const_pb->set_float_value(value);
      break;
    case InternalType::kDoubleValue:
      const_pb->set_double_value(value);
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstText *const_pt, YQLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kStringValue: {
      string value;
      RETURN_NOT_OK(const_pt->ToString(&value));
      const_pb->set_string_value(value);
      break;
    }
    case InternalType::kTimestampValue: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToTimestamp(&value));
      const_pb->set_timestamp_value(value);
      break;
    }
    case InternalType::kInetaddressValue: {
      InetAddress value;
      RETURN_NOT_OK(const_pt->ToInetaddress(&value));
      YQLValue::set_inetaddress_value(value, const_pb);
      break;
    }

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstBool *const_pt, YQLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kBoolValue:
      const_pb->set_bool_value(const_pt->value());
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstBinary *const_pt, YQLValuePB *const_pb) {
  const auto& value = const_pt->value();
  switch (const_pt->expected_internal_type()) {
    case InternalType::kBinaryValue: {
      int input_size = static_cast<int>(value->size());
      if (input_size % 2 != 0) {
        return STATUS(RuntimeError, "Invalid binary input, expected even number of hex digits");
      }

      string bytes;
      a2b_hex(value->c_str(), &bytes, input_size / 2);
      const_pb->set_binary_value(bytes);
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstUuid *const_pt, YQLValuePB *const_pb) {
  const auto& value = const_pt->value();
  switch (const_pt->expected_internal_type()) {
    case InternalType::kUuidValue: {
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromString(value->c_str()));
      YQLValue::set_uuid_value(uuid, const_pb);
      break;
    }
    case InternalType::kTimeuuidValue: {
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromString(value->c_str()));
      RETURN_NOT_OK(uuid.IsTimeUuid());
      YQLValue::set_timeuuid_value(uuid, const_pb);
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTMapExpr *const_pt, YQLValuePB *const_pb) {
  YQLValue::set_map_value(const_pb);
  for (auto &key : const_pt->keys()) {
    // Expected key to be constant because CQL only allows collection of constants.
    YQLValuePB *key_pb = YQLValue::add_map_key(const_pb);
    RETURN_NOT_OK(PTConstToPB(key, key_pb));
  }
  for (auto &value : const_pt->values()) {
    // Expected key to be constant because CQL only allows collection of constants.
    YQLValuePB *value_pb = YQLValue::add_map_value(const_pb);
    RETURN_NOT_OK(PTConstToPB(value, value_pb));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTSetExpr *const_pt, YQLValuePB *const_pb) {
  // Check if this expr is a null map.
  if (const_pt->expected_internal_type() == InternalType::kMapValue) {
    DCHECK_EQ(const_pt->elems().size(), 0) << "Not an empty map";
    YQLValue::set_map_value(const_pb);
    return Status::OK();
  }

  // This is a set.
  YQLValue::set_set_value(const_pb);
  for (auto &elem : const_pt->elems()) {
    // Expected elem to be constant because CQL only allows collection of constants.
    YQLValuePB *elem_pb = YQLValue::add_set_elem(const_pb);
    RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
  }

  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTListExpr *const_pt, YQLValuePB *const_pb) {
  YQLValue::set_list_value(const_pb);
  for (auto &elem : const_pt->elems()) {
    // Expected elem to be constant because CQL only allows collection of constants.
    YQLValuePB *elem_pb = YQLValue::add_list_elem(const_pb);
    RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
  }

  return Status::OK();
}

}  // namespace sql
}  // namespace yb
