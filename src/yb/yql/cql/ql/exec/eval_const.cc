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

#include <string>

#include "yb/common/jsonb.h"
#include "yb/common/ql_datatype.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/bytes_formatter.h"
#include "yb/util/date_time.h"
#include "yb/util/enums.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"
#include "yb/util/uuid.h"

#include "yb/yql/cql/ql/exec/exec_context.h"
#include "yb/yql/cql/ql/exec/executor.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"

using std::string;

namespace yb {
namespace ql {


Status Executor::PTConstToPB(const PTExpr::SharedPtr& expr,
                             QLValuePB *const_pb,
                             bool negate) {
  if (expr->internal_type() == InternalType::VALUE_NOT_SET) {
    SetNull(const_pb);
  }

  switch (expr->expr_op()) {
    case ExprOperator::kUMinus:
      return PTUMinusToPB(static_cast<const PTOperator1*>(expr.get()), const_pb);

    case ExprOperator::kBindVar: {
      QLExpressionPB expr_pb;
      RETURN_NOT_OK(PTExprToPB(static_cast<const PTBindVar*>(expr.get()), &expr_pb));
      *const_pb = std::move(*expr_pb.mutable_value());
      return Status::OK();
    }

    case ExprOperator::kConst: FALLTHROUGH_INTENDED;
    case ExprOperator::kCollection:
      break;

    default:
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Invalid constant expression ($0)", expr->QLName());
  }

  const PTExpr *const_pt = expr.get();
  switch (const_pt->ql_type_id()) {
    case DataType::NULL_VALUE_TYPE: {
      SetNull(const_pb);
      break;
    }
    case DataType::VARINT: {
      return PTExprToPB(static_cast<const PTConstVarInt*>(const_pt), const_pb, negate);
    }
    case DataType::DECIMAL: {
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
    case DataType::TUPLE: FALLTHROUGH_INTENDED;
    case DataType::FROZEN: FALLTHROUGH_INTENDED;
    case DataType::USER_DEFINED_TYPE: {
      DCHECK(!negate) << "Invalid datatype for negation";
      return PTExprToPB(static_cast<const PTCollectionExpr *>(const_pt), const_pb);
    }

    default:
      return STATUS_FORMAT(RuntimeError,
                           "Unknown datatype ($0) for QL constant value",
                           const_pt->ql_type_id());
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstVarInt *const_pt, QLValuePB *const_pb,
                            bool negate) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kInt8Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok() ||
          !(value <= INT8_MAX && value >= INT8_MIN)) {
        return exec_context_->Error(const_pt->loc(), "Invalid tiny integer/int8",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int8_value(narrow_cast<int32>(value));
      break;
    }
    case InternalType::kInt16Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok() ||
          !(value <= INT16_MAX && value >= INT16_MIN)) {
        return exec_context_->Error(const_pt->loc(), "Invalid small integer/int16",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int16_value(narrow_cast<int32>(value));
      break;
    }
    case InternalType::kInt32Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok() ||
          !(value <= INT32_MAX && value >= INT32_MIN)) {
        return exec_context_->Error(const_pt->loc(), "Invalid integer/int32",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int32_value(narrow_cast<int32>(value));
      break;
    }
    case InternalType::kInt64Value: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok() ||
          !(value <= INT64_MAX && value >= INT64_MIN)) {
        return exec_context_->Error(const_pt->loc(), "Invalid big integer/int64",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_int64_value(value);
      break;
    }
    case InternalType::kFloatValue: {
      long double value;
      if (!const_pt->ToDouble(&value, negate).ok()) {
        return exec_context_->Error(const_pt->loc(), "Invalid float",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_float_value(value);
      break;
    }
    case InternalType::kDoubleValue: {
      long double value;
      if (!const_pt->ToDouble(&value, negate).ok()) {
        return exec_context_->Error(const_pt->loc(), "Invalid double",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_double_value(value);
      break;
    }
    case InternalType::kDecimalValue: {
      return const_pt->ToDecimal(const_pb->mutable_decimal_value(), negate);
    }
    case InternalType::kTimestampValue: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok()) {
        return exec_context_->Error(const_pt->loc(), "Invalid integer",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_timestamp_value(DateTime::TimestampFromInt(value).ToInt64());
      break;
    }
    case InternalType::kDateValue: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok() ||
          value < std::numeric_limits<uint32_t>::min() ||
          value > std::numeric_limits<uint32_t>::max()) {
        return exec_context_->Error(const_pt->loc(), "Invalid date",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_date_value(static_cast<uint32_t>(value));
      break;
    }
    case InternalType::kTimeValue: {
      int64_t value;
      if (!const_pt->ToInt64(&value, negate).ok() ||
          value < DateTime::kMinTime || value > DateTime::kMaxTime) {
        return exec_context_->Error(const_pt->loc(), "Invalid time",
                                    ErrorCode::INVALID_ARGUMENTS);
      }
      const_pb->set_time_value(value);
      break;
    }
    case InternalType::kVarintValue: {
      return const_pt->ToVarInt(const_pb->mutable_varint_value(), negate);
    }

    default:
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "varint",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstDecimal *const_pt, QLValuePB *const_pb,
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
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "decimal",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

// The following numeric functions might be needed if we fold constant at compile time.
// Leave them here for now.
Status Executor::PTExprToPB(const PTConstInt *const_pt, QLValuePB *const_pb,
                            bool negate) {
  int64_t value = const_pt->value();
  if (negate) {
    value = -value;
  }

  switch (const_pt->expected_internal_type()) {
    case InternalType::kInt8Value:
      const_pb->set_int8_value(narrow_cast<int32>(value));
      break;
    case InternalType::kInt16Value:
      const_pb->set_int16_value(narrow_cast<int32>(value));
      break;
    case InternalType::kInt32Value:
      const_pb->set_int32_value(narrow_cast<int32>(value));
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
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "int",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstDouble *const_pt, QLValuePB *const_pb,
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
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "double",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstText *const_pt, QLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kStringValue:
      return const_pt->ToString(const_pb->mutable_string_value());
    case InternalType::kTimestampValue: {
      int64_t value = 0;
      RETURN_NOT_OK(const_pt->ToTimestamp(&value));
      const_pb->set_timestamp_value(value);
      break;
    }
    case InternalType::kDateValue: {
      uint32_t value = 0;
      RETURN_NOT_OK(const_pt->ToDate(&value));
      const_pb->set_date_value(value);
      break;
    }
    case InternalType::kTimeValue: {
      int64_t value = 0;
      RETURN_NOT_OK(const_pt->ToTime(&value));
      const_pb->set_time_value(value);
      break;
    }
    case InternalType::kInetaddressValue: {
      InetAddress value;
      RETURN_NOT_OK(const_pt->ToInetaddress(&value));

      QLValue ql_const;
      ql_const.set_inetaddress_value(value);
      *const_pb = std::move(*ql_const.mutable_value());
      break;
    }
    case InternalType::kJsonbValue: {
      std::string value;
      RETURN_NOT_OK(const_pt->ToString(&value));
      common::Jsonb jsonb;
      RETURN_NOT_OK(jsonb.FromString(value));

      QLValue ql_const;
      ql_const.set_jsonb_value(jsonb.MoveSerializedJsonb());
      *const_pb = std::move(*ql_const.mutable_value());
      break;
    }

    default:
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "text",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstBool *const_pt, QLValuePB *const_pb) {
  switch (const_pt->expected_internal_type()) {
    case InternalType::kBoolValue:
      const_pb->set_bool_value(const_pt->value());
      break;
    default:
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "bool",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstBinary *const_pt, QLValuePB *const_pb) {
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
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "binary",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTConstUuid *const_pt, QLValuePB *const_pb) {
  const auto& value = const_pt->value();
  switch (const_pt->expected_internal_type()) {
    case InternalType::kUuidValue: {
      Uuid uuid = VERIFY_RESULT(Uuid::FromString(value->c_str()));

      QLValue ql_const;
      ql_const.set_uuid_value(uuid);
      *const_pb = std::move(*ql_const.mutable_value());
      break;
    }
    case InternalType::kTimeuuidValue: {
      Uuid uuid = VERIFY_RESULT(Uuid::FromString(value->c_str()));
      RETURN_NOT_OK(uuid.IsTimeUuid());

      QLValue ql_const;
      ql_const.set_timeuuid_value(uuid);
      *const_pb = std::move(*ql_const.mutable_value());
      break;
    }
    default:
      return STATUS_SUBSTITUTE(RuntimeError,
                               "Illegal datatype conversion: $0 to $1", "uuid",
                               InternalTypeToCQLString(const_pt->expected_internal_type()));
  }
  return Status::OK();
}

Status Executor::PTExprToPB(const PTCollectionExpr *const_pt, QLValuePB *const_pb) {
  switch (const_pt->ql_type()->main()) {
    case DataType::MAP: {
      QLMapValuePB *map_value = const_pb->mutable_map_value();
      for (auto &key : const_pt->keys()) {
        // Expect key to be constant because CQL only allows collection of constants.
        QLValuePB *key_pb = map_value->add_keys();
        RETURN_NOT_OK(PTConstToPB(key, key_pb));
      }

      for (auto &value : const_pt->values()) {
        // Expect value to be constant because CQL only allows collection of constants.
        QLValuePB *value_pb = map_value->add_values();
        RETURN_NOT_OK(PTConstToPB(value, value_pb));
      }
      break;
    }

    case DataType::SET: {
      QLSeqValuePB *set_value = const_pb->mutable_set_value();
      for (auto &elem : const_pt->values()) {
        // Expected elem to be constant because CQL only allows collection of constants.
        QLValuePB *elem_pb = set_value->add_elems();
        RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
      }
      break;
    }

    case DataType::LIST: {
      QLSeqValuePB *list_value = const_pb->mutable_list_value();
      for (auto &elem : const_pt->values()) {
        // Expected elem to be constant because CQL only allows collection of constants.
        QLValuePB *elem_pb = list_value->add_elems();
        RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
      }
      break;
    }

    case DataType::TUPLE: {
      QLSeqValuePB *tuple_value = const_pb->mutable_tuple_value();
      for (auto &elem : const_pt->values()) {
        // Expected elem to be constant because CQL only allows collection of constants.
        QLValuePB *elem_pb = tuple_value->add_elems();
        RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
      }
      break;
    }

    case DataType::USER_DEFINED_TYPE: {
      // Internally UDTs are maps with field names as keys
      QLMapValuePB *map_value = const_pb->mutable_map_value();
      auto field_values = const_pt->udtype_field_values();
      for (size_t i = 0; i < field_values.size(); i++) {
        // Skipping unset fields.
        if (field_values[i] != nullptr) {
          QLValuePB *key_pb = map_value->add_keys();
          key_pb->set_int16_value(narrow_cast<int16_t>(i));
          // Expect value to be constant because CQL only allows collection of constants.
          QLValuePB *value_pb = map_value->add_values();
          RETURN_NOT_OK(PTConstToPB(field_values[i], value_pb));
        }
      }
      break;
    }

    case DataType::FROZEN: {
      // For frozen types we need to do the de-duplication and ordering at the QL level since we
      // serialize it here already.
      QLSeqValuePB *frozen_value = const_pb->mutable_frozen_value();

      switch (const_pt->ql_type()->param_type(0)->main()) {
        case DataType::MAP: {
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

          for (auto &pair : map_values) {
            *frozen_value->add_elems() = std::move(pair.first);
            *frozen_value->add_elems() = std::move(pair.second);
          }
          break;
        }

        case DataType::SET: {
          std::set<QLValuePB> set_values;
          for (const auto &elem : const_pt->values()) {
            QLValuePB elem_pb;
            RETURN_NOT_OK(PTConstToPB(elem, &elem_pb));
            set_values.insert(std::move(elem_pb));
          }

          for (auto &elem : set_values) {
            *frozen_value->add_elems() = std::move(elem);
          }
          break;
        }

        case DataType::LIST: {
          for (auto &elem : const_pt->values()) {
            // Expected elem to be constant because CQL only allows collection of constants.
            QLValuePB *elem_pb = frozen_value->add_elems();
            RETURN_NOT_OK(PTConstToPB(elem, elem_pb));
          }
          break;
        }

        case DataType::USER_DEFINED_TYPE: {
          // Internally UDTs are maps with field names as keys
          auto field_values = const_pt->udtype_field_values();
          for (size_t i = 0; i < field_values.size(); i++) {
            QLValuePB *value_pb = frozen_value->add_elems();
            if (field_values[i] != nullptr) {
              // Expect value to be constant because CQL only allows collection of constants.
              RETURN_NOT_OK(PTConstToPB(field_values[i], value_pb));
            }
          }
          break;
        }

        default:
          return STATUS_FORMAT(InternalError, "unsupported type $0",
                               const_pt->ql_type()->param_type(0)->main());
      }
      break;
    }

    default:
      return STATUS_FORMAT(InternalError, "unsupported type $0", const_pt->ql_type()->main());
  }

  return Status::OK();
}


}  // namespace ql
}  // namespace yb
