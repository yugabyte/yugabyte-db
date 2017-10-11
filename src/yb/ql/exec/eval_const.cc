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

#include <yb/util/bytes_formatter.h>
#include "yb/ql/exec/executor.h"
#include "yb/util/logging.h"
#include "yb/util/bfql/bfunc.h"
#include "yb/util/net/inetaddress.h"

#include "yb/util/decimal.h"

namespace yb {
namespace ql {

CHECKED_STATUS Executor::PTConstToPB(const PTExpr::SharedPtr& expr,
                                     QLValuePB *const_pb,
                                     bool negate) {
  DCHECK(expr->expr_op() == ExprOperator::kConst ||
         expr->expr_op() == ExprOperator::kCollection ||
         expr->expr_op() == ExprOperator::kUMinus ||
         expr->expr_op() == ExprOperator::kBindVar);

  if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
      QLValue::SetNull(const_pb);
  }

  if (expr->expr_op() == ExprOperator::kUMinus) {
    return PTUMinusToPB(static_cast<const PTOperator1*>(expr.get()), const_pb);
  }

  if (expr->expr_op() == ExprOperator::kBindVar) {
    QLExpressionPB expr_pb;
    RETURN_NOT_OK(PTExprToPB(static_cast<const PTBindVar*>(expr.get()), &expr_pb));
    const_pb->Swap(expr_pb.mutable_value());
    return Status::OK();
  }

  const PTExpr *const_pt = expr.get();
  switch (const_pt->ql_type_id()) {
    case DataType::NULL_VALUE_TYPE: {
      QLValue::SetNull(const_pb);
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
    case DataType::MAP: FALLTHROUGH_INTENDED;
    case DataType::SET: FALLTHROUGH_INTENDED;
    case DataType::LIST: FALLTHROUGH_INTENDED;
    case DataType::FROZEN: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTCollectionExpr *>(const_pt), const_pb);
    }

    default:
      LOG(FATAL) << "Unknown datatype for QL constant value";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstVarInt *const_pt, QLValuePB *const_pb,
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
      return const_pt->ToDecimal(const_pb->mutable_decimal_value(), negate);
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

CHECKED_STATUS Executor::PTExprToPB(const PTConstDecimal *const_pt, QLValuePB *const_pb,
                                    bool negate) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kDecimalValue: {
      return const_pt->ToDecimal(const_pb->mutable_decimal_value(), negate);
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
CHECKED_STATUS Executor::PTExprToPB(const PTConstInt *const_pt, QLValuePB *const_pb,
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

CHECKED_STATUS Executor::PTExprToPB(const PTConstDouble *const_pt, QLValuePB *const_pb,
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

CHECKED_STATUS Executor::PTExprToPB(const PTConstText *const_pt, QLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kStringValue:
      return const_pt->ToString(const_pb->mutable_string_value());
    case InternalType::kTimestampValue: {
      int64_t value;
      RETURN_NOT_OK(const_pt->ToTimestamp(&value));
      const_pb->set_timestamp_value(value);
      break;
    }
    case InternalType::kInetaddressValue: {
      InetAddress value;
      RETURN_NOT_OK(const_pt->ToInetaddress(&value));
      QLValue::set_inetaddress_value(value, const_pb);
      break;
    }

    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstBool *const_pt, QLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kBoolValue:
      const_pb->set_bool_value(const_pt->value());
      break;
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTConstBinary *const_pt, QLValuePB *const_pb) {
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

CHECKED_STATUS Executor::PTExprToPB(const PTConstUuid *const_pt, QLValuePB *const_pb) {
  const auto& value = const_pt->value();
  switch (const_pt->expected_internal_type()) {
    case InternalType::kUuidValue: {
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromString(value->c_str()));
      QLValue::set_uuid_value(uuid, const_pb);
      break;
    }
    case InternalType::kTimeuuidValue: {
      Uuid uuid;
      RETURN_NOT_OK(uuid.FromString(value->c_str()));
      RETURN_NOT_OK(uuid.IsTimeUuid());
      QLValue::set_timeuuid_value(uuid, const_pb);
      break;
    }
    default:
      LOG(FATAL) << "Illegal datatype conversion";
  }
  return Status::OK();
}

CHECKED_STATUS Executor::PTExprToPB(const PTCollectionExpr *const_pt, QLValuePB *const_pb) {
  switch (const_pt->ql_type()->main()) {
    case MAP: {
      QLValue::set_map_value(const_pb);

      for (auto &key : const_pt->keys()) {
        // Expect key to be constant because CQL only allows collection of constants.
        QLValuePB *key_pb = QLValue::add_map_key(const_pb);
        RETURN_NOT_OK(PTConstToPB(key, key_pb));
      }

      for (auto &value : const_pt->values()) {
        // Expect value to be constant because CQL only allows collection of constants.
        QLValuePB *value_pb = QLValue::add_map_value(const_pb);
        RETURN_NOT_OK(PTConstToPB(value, value_pb));
      }
      break;
    }

    case SET: {
      QLValue::set_set_value(const_pb);

      for (auto &elem : const_pt->values()) {
        // Expected elem to be constant because CQL only allows collection of constants.
        QLValuePB *elem_pb = QLValue::add_set_elem(const_pb);
        RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
      }
      break;
    }

    case LIST: {
      QLValue::set_list_value(const_pb);
      for (auto &elem : const_pt->values()) {
        // Expected elem to be constant because CQL only allows collection of constants.
        QLValuePB *elem_pb = QLValue::add_list_elem(const_pb);
        RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
      }
      break;
    }

    case USER_DEFINED_TYPE: {
      // Internally UDTs are maps with field names as keys
      QLValue::set_map_value(const_pb);
      auto field_values = const_pt->udtype_field_values();
      for (int i = 0; i < field_values.size(); i++) {
        // Skipping unset fields.
        if (field_values[i] != nullptr) {
          QLValuePB *key_pb = QLValue::add_map_key(const_pb);
          key_pb->set_int16_value(i);
          // Expect value to be constant because CQL only allows collection of constants.
          QLValuePB *value_pb = QLValue::add_map_value(const_pb);
          RETURN_NOT_OK(PTConstToPB(field_values[i], value_pb));
        }
      }
      break;
    }

    case FROZEN: {
      // For frozen types we need to do the de-duplication and ordering at the QL level since we
      // serialize it here already.
      QLValue::set_frozen_value(const_pb);

      switch (const_pt->ql_type()->param_type(0)->main()) {
        case MAP: {
          std::map<QLValuePB, QLValuePB> map_values;
          auto keys_it = const_pt->keys().begin();
          auto values_it = const_pt->values().begin();
          while (keys_it != const_pt->keys().end() && values_it != const_pt->values().end()) {
            QLValuePB key_pb;
            RETURN_NOT_OK(PTConstToPB(*keys_it, &key_pb));
            RETURN_NOT_OK(PTConstToPB(*values_it, &map_values[key_pb]));
            keys_it++;
            values_it++;
          }

          for (const auto &pair : map_values) {
            QLValuePB *key_pb = QLValue::add_frozen_elem(const_pb);
            key_pb->CopyFrom(pair.first);
            QLValuePB *value_pb = QLValue::add_frozen_elem(const_pb);
            value_pb->CopyFrom(pair.second);
          }
          break;
        }

        case SET: {
          std::set<QLValuePB> set_values;
          for (const auto &elem : const_pt->values()) {
            QLValuePB elem_pb;
            RETURN_NOT_OK(PTConstToPB(elem, &elem_pb));
            set_values.insert(elem_pb);
          }

          for (const auto &elem : set_values) {
            QLValuePB *elem_pb = QLValue::add_frozen_elem(const_pb);
            elem_pb->CopyFrom(elem);
          }
          break;
        }

        case LIST: {
          for (auto &elem : const_pt->values()) {
            // Expected elem to be constant because CQL only allows collection of constants.
            QLValuePB *elem_pb = QLValue::add_frozen_elem(const_pb);
            RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
          }
          break;
        }

        case USER_DEFINED_TYPE: {
          // Internally UDTs are maps with field names as keys
          auto field_values = const_pt->udtype_field_values();
          for (int i = 0; i < field_values.size(); i++) {
            QLValuePB *value_pb = QLValue::add_frozen_elem(const_pb);
            if (field_values[i] != nullptr) {
              // Expect value to be constant because CQL only allows collection of constants.
              RETURN_NOT_OK(PTConstToPB(field_values[i], value_pb));
            }
          }
          break;
        }

        default:
          FATAL_INVALID_ENUM_VALUE(DataType, const_pt->ql_type()->param_type(0)->main());
      }
      break;
    }

    default:
      FATAL_INVALID_ENUM_VALUE(DataType, const_pt->ql_type()->main());
  }

  return Status::OK();
}


}  // namespace ql
}  // namespace yb
