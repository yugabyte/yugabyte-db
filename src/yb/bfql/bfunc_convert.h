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
//
// This module defines of C++ functions to support "cast" operators. Note that for "cast" operators,
// all arguments must of exact types. Type resolution will raise error if an argument type is
// only compatible / convertible but not equal.
//   Example: cast(int64 to string) will accept only int64 and string but not int32 and string.
//
// The conversion routines can be use for either or both of two different purposes.
// - Converting the value from one type to another.
// - Converting the value from one data representation to another.
//
// Once written, this function should not be changed to avoid compatibility issues. That is,
// server might runs one version while client use a different version of this function.
//
// See the header of file "/util/bfql/bfql.h" for overall info.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <iostream>
#include <string>

#include "yb/common/ql_datatype.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/status_fwd.h"
#include "yb/util/stol_utils.h"
#include "yb/util/uuid.h"

namespace yb {
namespace bfql {

static constexpr size_t kSizeBool = 1;
static constexpr size_t kSizeTinyInt = 1;
static constexpr size_t kSizeSmallInt = 2;
static constexpr size_t kSizeInt = 4;
static constexpr size_t kSizeBigInt = 8;
static constexpr size_t kSizeUuid = 16;
static constexpr size_t kByteSize = 8;
static constexpr size_t kHexBase = 16;

//--------------------------------------------------------------------------------------------------
template<typename SetResult, typename PTypePtr, typename RTypePtr>
Status SetNumericResult(SetResult set_result, PTypePtr source, DataType target_datatype,
                        RTypePtr target) {
  auto source_datatype = InternalToDataType(source->value_case());
  if (!QLType::IsExplicitlyConvertible(target_datatype, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(target_datatype));
  }

  switch(source->value_case()) {
    case InternalType::kInt8Value:
      RETURN_NOT_OK(set_result(source->int8_value(), target));
      break;
    case InternalType::kInt16Value:
      RETURN_NOT_OK(set_result(source->int16_value(), target));
      break;
    case InternalType::kInt32Value:
      RETURN_NOT_OK(set_result(source->int32_value(), target));
      break;
    case InternalType::kInt64Value:
      RETURN_NOT_OK(set_result(source->int64_value(), target));
      break;
    case InternalType::kFloatValue:
      RETURN_NOT_OK(set_result(source->float_value(), target));
      break;
    case InternalType::kDoubleValue:
      RETURN_NOT_OK(set_result(source->double_value(), target));
      break;
    case InternalType::kDecimalValue: {
        util::Decimal d;
        RETURN_NOT_OK(d.DecodeFromComparable(source->decimal_value()));

        if (target_datatype == DataType::FLOAT || target_datatype == DataType::DOUBLE) {
          // Convert via DOUBLE:
          RETURN_NOT_OK(set_result(VERIFY_RESULT(d.ToDouble()), target));
        } else { // Expected an Integer type
          RSTATUS_DCHECK(target_datatype == DataType::INT8 || target_datatype == DataType::INT16
              || target_datatype == DataType::INT32 || target_datatype == DataType::INT64,
              InvalidArgument, strings::Substitute("Unexpected target type: ",
                                                   QLType::ToCQLString(target_datatype)));
          // Convert via INT64:
          RETURN_NOT_OK(set_result(VERIFY_RESULT(VERIFY_RESULT(d.ToVarInt()).ToInt64()), target));
        }
      }
      break;
    default:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(target_datatype));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SetStringResult(PTypePtr source, RTypePtr target) {
  auto source_datatype = InternalToDataType(source->value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::STRING, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::STRING));
  }

  switch(source->value_case()) {
    case InternalType::kInt8Value:
      target->set_string_value(std::to_string(source->int8_value()));
      break;
    case InternalType::kInt16Value:
      target->set_string_value(std::to_string(source->int16_value()));
      break;
    case InternalType::kInt32Value:
      target->set_string_value(std::to_string(source->int32_value()));
      break;
    case InternalType::kInt64Value:
      target->set_string_value(std::to_string(source->int64_value()));
      break;
    case InternalType::kFloatValue:
      target->set_string_value(std::to_string(source->float_value()));
      break;
    case InternalType::kDoubleValue:
      target->set_string_value(std::to_string(source->double_value()));
      break;
    case InternalType::kStringValue:
      target->set_string_value(source->string_value());
      break;
    case InternalType::kBoolValue:
      target->set_string_value(source->bool_value() ? "true" : "false");
      break;
    case InternalType::kTimestampValue:
      target->set_string_value(DateTime::TimestampToString(QLValue::timestamp_value(*source)));
      break;
    case InternalType::kDateValue:
      target->set_string_value(VERIFY_RESULT(DateTime::DateToString(source->date_value())));
      break;
    case InternalType::kTimeValue:
      target->set_string_value(VERIFY_RESULT(DateTime::TimeToString(source->time_value())));
      break;
    case InternalType::kUuidValue:
      target->set_string_value(QLValue::uuid_value(*source).ToString());
      break;
    case InternalType::kTimeuuidValue:
      target->set_string_value(QLValue::timeuuid_value(*source).ToString());
      break;
    case InternalType::kBinaryValue:
      target->set_string_value("0x" + b2a_hex(source->binary_value()));
      break;
    case InternalType::kInetaddressValue:
      RETURN_NOT_OK(QLValue::inetaddress_value(*source).ToString(target->mutable_string_value()));
      break;
    case InternalType::kDecimalValue: {
        util::Decimal d;
        RETURN_NOT_OK(d.DecodeFromComparable(source->decimal_value()));
        target->set_string_value(d.ToString());
      }
      break;
    default:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::STRING));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SetTimestampResult(PTypePtr source, RTypePtr target) {
  auto source_datatype = InternalToDataType(source->value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::TIMESTAMP, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::TIMESTAMP));
  }

  switch(source->value_case()) {
    case InternalType::kTimeuuidValue: {
      auto time_uuid = QLValue::timeuuid_value(*source);
      int64_t unix_timestamp;
      RETURN_NOT_OK(time_uuid.ToUnixTimestamp(&unix_timestamp));
      target->set_timestamp_value(Timestamp(DateTime::AdjustPrecision(
          unix_timestamp, DateTime::kMillisecondPrecision, DateTime::kInternalPrecision))
          .ToInt64());
      break;
    }
    case InternalType::kDateValue:
      target->set_timestamp_value(DateTime::DateToTimestamp(source->date_value()).ToInt64());
      break;
    default:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::TIMESTAMP));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SetDateResult(PTypePtr source, RTypePtr target) {
  DataType source_datatype = InternalToDataType(source->value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::DATE, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::DATE));
  }

  switch(source->value_case()) {
    case InternalType::kTimestampValue:
      target->set_date_value(VERIFY_RESULT(DateTime::DateFromTimestamp(
          QLValue::timestamp_value(*source))));
      break;
    case InternalType::kTimeuuidValue: {
      Uuid time_uuid = QLValue::timeuuid_value(*source);
      int64_t unix_timestamp;
      RETURN_NOT_OK(time_uuid.ToUnixTimestamp(&unix_timestamp));
      target->set_date_value(VERIFY_RESULT(DateTime::DateFromUnixTimestamp(unix_timestamp)));
      break;
    }
    default:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::DATE));
  }
  return Status::OK();
}

template<typename RTypePtr, typename StrToNum, typename SetTarget>
Status StringToNumeric(const std::string& str_val, RTypePtr target, StrToNum strToNum,
                       SetTarget setTarget) {
  auto result = strToNum(str_val);
  RETURN_NOT_OK(result);
  return setTarget(*result, target);
}

//--------------------------------------------------------------------------------------------------
template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI8(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int8_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI16(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int16_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI32(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int32_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI64(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int64_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_float_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_double_value(source->int8_value());
  }
  return Status::OK();
}

// Conversion from int16 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI8(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int8_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI16(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int16_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI32(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int32_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI64(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int64_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_float_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_double_value(source->int16_value());
  }
  return Status::OK();
}

// Conversion from int32 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI8(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int8_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI16(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int16_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI32(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int32_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI64(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int64_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_float_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_double_value(source->int32_value());
  }
  return Status::OK();
}

// Conversion from int64 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI8(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int8_value(static_cast<int32_t>(source->int64_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI16(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int16_value(static_cast<int32_t>(source->int64_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI32(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int32_value(static_cast<int32_t>(source->int64_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI64(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int64_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_float_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_double_value(source->int64_value());
  }
  return Status::OK();
}

// Conversion from float to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertFloatToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_float_value(source->float_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertFloatToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_double_value(source->float_value());
  }
  return Status::OK();
}

// Conversion from double to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertDoubleToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_float_value(source->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDoubleToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_double_value(source->double_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for timestamp conversion.
template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToI64(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int64_value(QLValue::timestamp_value(*source).ToInt64());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_timestamp_value(DateTime::TimestampFromInt(source->int64_value()).ToInt64());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToString(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_string_value(QLValue::timestamp_value(*source).ToString());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    Timestamp ts;
    RETURN_NOT_OK(DateTime::TimestampFromString(source->string_value(), &ts));
    target->set_timestamp_value(ts.ToInt64());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for string conversion.
template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToString(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_string_value(source->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToInet(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_inetaddress_value(InetAddress(
        VERIFY_RESULT(HostToAddress(source->string_value()))));
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for boolean conversion.
template<typename PTypePtr, typename RTypePtr>
Status ConvertBoolToBool(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_bool_value(source->bool_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions to blob / binary from other datatypes.

template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string target_val = source->string_value();
    target->set_binary_value(target_val);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBoolToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int8_t byte_stream = (source->bool_value()) ? 1 : 0;
    target->set_binary_value(reinterpret_cast<void *> (&byte_stream), kSizeBool);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt8ToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int8_t byte_stream = source->int8_value();
    target->set_binary_value(reinterpret_cast<void *> (&byte_stream), kSizeTinyInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt16ToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int16_t source_val = source->int16_value();
    uint16* source_ptr = reinterpret_cast<uint16*> (&source_val);
    uint16 source_big_endian = BigEndian::FromHost16(*source_ptr);
    target->set_binary_value(reinterpret_cast<void*> (&source_big_endian), kSizeSmallInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt32ToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int32_t source_val = source->int32_value();
    uint32* source_ptr = reinterpret_cast<uint32*> (&source_val);
    uint32 source_big_endian = BigEndian::FromHost32(*source_ptr);
    target->set_binary_value(reinterpret_cast<void*> (&source_big_endian), kSizeInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt64ToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int64_t source_val = source->int64_value();
    uint64* source_ptr = reinterpret_cast<uint64*> (&source_val);
    uint64 source_big_endian = BigEndian::FromHost64(*source_ptr);
    target->set_binary_value(reinterpret_cast<void *> (&source_big_endian), kSizeBigInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertFloatToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    float source_val = source->float_value();
    uint32* source_ptr = reinterpret_cast<uint32*> (&source_val);
    uint32 source_big_endian = BigEndian::FromHost32(*source_ptr);
    target->set_binary_value(reinterpret_cast<void *> (&source_big_endian), kSizeInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDoubleToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    double source_val = source->double_value();
    uint64* source_ptr = reinterpret_cast<uint64*> (&source_val);
    uint64 source_big_endian = BigEndian::FromHost64(*source_ptr);
    target->set_binary_value(reinterpret_cast<void *> (&source_big_endian), kSizeBigInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDecimalToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDateToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    Timestamp source_val = QLValue::timestamp_value(*source);
    int64_t ts_int_value = source_val.ToInt64();
    ts_int_value = DateTime::AdjustPrecision(ts_int_value, DateTime::kInternalPrecision,
                                             DateTime::kMillisecondPrecision);
    uint64* source_ptr = reinterpret_cast<uint64*> (&ts_int_value);
    uint64 source_big_endian = BigEndian::FromHost64(*source_ptr);
    target->set_binary_value(reinterpret_cast<void *> (&source_big_endian), kSizeBigInt);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertUuidToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    const Uuid& source_val = QLValue::uuid_value(*source);
    source_val.ToBytes(target->mutable_binary_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToBlob(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    const Uuid& source_val = QLValue::timeuuid_value(*source);
    source_val.ToBytes(target->mutable_binary_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInetToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertListToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertMapToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertSetToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTupleToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

// The following functions are for conversions from blob / binary to other datatypes.

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToString(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_string_value(source->binary_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToBool(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeBool) {
      return STATUS(QLError, "The blob string is not a valid string for a boolean type.");
    }
    target->set_bool_value(blob[0] != 0);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt8(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeTinyInt) {
      return STATUS(QLError, "The blob string is not valid for tinyint type.");
    }
    target->set_int8_value(static_cast<int8_t> (blob[0]));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt16(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeSmallInt) {
      return STATUS(QLError, "The blob string is not valid for smallint type.");
    }
    uint16* target_ptr = reinterpret_cast<uint16*> (const_cast <char*> (blob.c_str()));
    uint16 target_little_endian = BigEndian::ToHost16(*target_ptr);
    int16_t* target_val_ptr = reinterpret_cast<int16_t*> (&target_little_endian);
    target->set_int16_value(*target_val_ptr);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt32(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeInt) {
      return STATUS(QLError, "The blob string is not valid for int type.");
    }
    uint32* target_ptr = reinterpret_cast<uint32*> (const_cast <char*> (blob.c_str()));
    uint32 target_little_endian = BigEndian::ToHost32(*target_ptr);
    int32_t* target_val_ptr = reinterpret_cast<int32_t*> (&target_little_endian);
    target->set_int32_value(*target_val_ptr);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt64(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeBigInt) {
      return STATUS(QLError, "The blob string is not valid for bigint type.");
    }
    uint64* target_ptr = reinterpret_cast<uint64*> (const_cast <char*> (blob.c_str()));
    uint64 target_little_endian = BigEndian::ToHost64(*target_ptr);
    int64_t* target_val_ptr = reinterpret_cast<int64_t*> (&target_little_endian);
    target->set_int64_value(*target_val_ptr);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToVarint(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeInt) {
      return STATUS(QLError, "The blob string is not valid for float type.");
    }
    uint32* target_ptr = reinterpret_cast<uint32*> (const_cast <char*> (blob.c_str()));
    uint32 target_little_endian = BigEndian::ToHost32(*target_ptr);
    float* target_val_ptr =  reinterpret_cast<float*> (&target_little_endian);
    target->set_float_value(*target_val_ptr);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeBigInt) {
      return STATUS(QLError, "The blob string is not valid for double type.");
    }
    uint64* target_ptr = reinterpret_cast<uint64*> (const_cast <char*> (blob.c_str()));
    uint64 target_little_endian = BigEndian::ToHost64(*target_ptr);
    double* target_val_ptr =  reinterpret_cast<double*> (&target_little_endian);
    target->set_double_value(*target_val_ptr);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToDecimal(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToDate(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTime(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeBigInt) {
      return STATUS(QLError, "The blob string is not a valid Timestamp.");
    }
    uint64* target_ptr = reinterpret_cast<uint64*> (const_cast <char*> (blob.c_str()));
    uint64 target_little_endian = BigEndian::ToHost64(*target_ptr);
    int64_t* target_val_ptr = reinterpret_cast<int64_t*> (&target_little_endian);
    int64_t target_val = DateTime::AdjustPrecision(*target_val_ptr,
                                                   DateTime::kMillisecondPrecision,
                                                   DateTime::kInternalPrecision);
    Timestamp ts(target_val);
    target->set_timestamp_value(ts.ToInt64());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToUuid(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeUuid) {
      return STATUS(QLError, "The blob string is not valid for UUID type.");
    }
    QLValue::set_uuid_value(VERIFY_RESULT(Uuid::FromSlice(blob)), &*target);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTimeuuid(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    std::string blob = source->binary_value();
    if (blob.size() != kSizeUuid) {
      return STATUS(QLError, "The blob string is not valid for UUID type.");
    }
    QLValue::set_timeuuid_value(VERIFY_RESULT(Uuid::FromSlice(blob)), &*target);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInet(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToList(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToMap(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToSet(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTuple(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions between date-time datatypes.
template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToDate(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetDateResult(source, target);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToDate(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetDateResult(source, target);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToTime(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToTime(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDateToTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetTimestampResult(source, target);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetTimestampResult(source, target);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDateToUnixTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_int64_value(DateTime::DateToUnixTimestamp(source->date_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToUnixTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int64_t unix_timestamp = DateTime::AdjustPrecision(QLValue::timestamp_value(*source).ToInt64(),
                                                       DateTime::kInternalPrecision,
                                                       DateTime::kMillisecondPrecision);
    target->set_int64_value(unix_timestamp);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToUnixTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    Uuid time_uuid = QLValue::timeuuid_value(*source);
    int64_t unix_timestamp;
    RETURN_NOT_OK(time_uuid.ToUnixTimestamp(&unix_timestamp));
    target->set_int64_value(unix_timestamp);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToMaxTimeuuid(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    return STATUS(RuntimeError, "Cannot get max timeuuid of null");
  } else {
    int64_t timestamp_ms = DateTime::AdjustPrecision(QLValue::timestamp_value(*source).ToInt64(),
                                                     DateTime::kInternalPrecision,
                                                     DateTime::kMillisecondPrecision);

    Uuid uuid;
    RETURN_NOT_OK(uuid.MaxFromUnixTimestamp(timestamp_ms));
    QLValue::set_timeuuid_value(uuid, &*target);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToMinTimeuuid(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    return STATUS(RuntimeError, "Cannot get max timeuuid of null");
  } else {
    int64_t timestamp_ms = DateTime::AdjustPrecision(QLValue::timestamp_value(*source).ToInt64(),
                                                     DateTime::kInternalPrecision,
                                                     DateTime::kMillisecondPrecision);

    Uuid uuid;
    RETURN_NOT_OK(uuid.MinFromUnixTimestamp(timestamp_ms));
    QLValue::set_timeuuid_value(uuid, &*target);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions from VarInt to the other numeric types.

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToI8(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int64_t val = VERIFY_RESULT(QLValue::varint_value(*source).ToInt64());
    if (val < INT8_MIN || val > INT8_MAX) {
      return STATUS(QLError, "VarInt cannot be converted to int8 due to overflow");
    }
    target->set_int8_value(static_cast<int32_t>(val));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToI16(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int64_t val = VERIFY_RESULT(QLValue::varint_value(*source).ToInt64());
    if (val < INT16_MIN || val > INT16_MAX) {
      return STATUS(QLError, "VarInt cannot be converted to int16 due to overflow");
    }
    target->set_int16_value(static_cast<int32_t>(val));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToI32(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int64_t val = VERIFY_RESULT(QLValue::varint_value(*source).ToInt64());
    if (val < INT32_MIN || val > INT32_MAX) {
      return STATUS(QLError, "VarInt cannot be converted to int32 due to overflow");
    }
    target->set_int32_value(static_cast<int32_t>(val));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToI64(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    int64_t val = VERIFY_RESULT(QLValue::varint_value(*source).ToInt64());
    target->set_int64_value(val);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToFloat(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    // This may lose precision, it should return the closest float value to the input number.
    target->set_float_value(static_cast<float>(VERIFY_RESULT(CheckedStold(
        QLValue::varint_value(*source).ToString()))));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToDouble(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    // This may lose precision, it should return the closest double value to the input number.
    target->set_double_value(VERIFY_RESULT(CheckedStold(
        QLValue::varint_value(*source).ToString())));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToVarint(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_varint_value(
        util::VarInt(static_cast<int64_t>(source->int8_value())).EncodeToComparable());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToVarint(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_varint_value(
        util::VarInt(static_cast<int64_t>(source->int16_value())).EncodeToComparable());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToVarint(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_varint_value(
        util::VarInt(static_cast<int64_t>(source->int32_value())).EncodeToComparable());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToVarint(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
  } else {
    target->set_varint_value(util::VarInt(source->int64_value()).EncodeToComparable());
  }
  return Status::OK();
}

template<typename RTypePtr>
Status ToInt32(int64_t val, RTypePtr target) {
  target->set_int32_value(static_cast<int32_t>(val));
  return Status::OK();
}

template<typename RTypePtr>
Status ToInt64(int64_t val, RTypePtr target) {
  target->set_int64_value(val);
  return Status::OK();
}

template<typename RTypePtr>
Status ToInt16(int16_t val, RTypePtr target) {
  target->set_int16_value(val);
  return Status::OK();
}

template<typename RTypePtr>
Status ToFloat(float val, RTypePtr target) {
  target->set_float_value(val);
  return Status::OK();
}

template<typename RTypePtr>
Status ToDouble(double val, RTypePtr target) {
  target->set_double_value(val);
  return Status::OK();
}

template<typename RTypePtr, typename PTypePtr, typename StrToNum, typename ToNumeric>
Status ConvertToNumeric(PTypePtr source, RTypePtr target, const DataType& data_type,
                        StrToNum strToNum, ToNumeric toNumeric) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  if (source->value_case() == InternalType::kStringValue) {
    return StringToNumeric<RTypePtr>(source->string_value(), target, strToNum, toNumeric);
  } else {
    return SetNumericResult(toNumeric, source, data_type, target);
  }
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToI32(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::INT32, CheckedStoi,
                          ToInt32<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToI16(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::INT16, CheckedStoi,
                          ToInt16<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToI64(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::INT64, CheckedStoll,
                          ToInt64<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToDouble(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::DOUBLE, CheckedStold,
                          ToDouble<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToFloat(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::FLOAT, CheckedStold,
                          ToFloat<RTypePtr>);
}

YB_DEFINE_ENUM(ConvertDecimalVia, (kUnknown)(kString)(kVarint)(kDecimal)(kInt64)(kDouble));

template<typename PTypePtr, typename RTypePtr>
Status ConvertToDecimal(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }

  const DataType source_datatype = InternalToDataType(source->value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::DECIMAL, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::DECIMAL));
  }

  int64_t int_num = 0;
  double double_num = 0.;
  ConvertDecimalVia convert = ConvertDecimalVia::kUnknown;

  switch(source->value_case()) {
    case InternalType::kStringValue:
      convert = ConvertDecimalVia::kString;
      break;
    case InternalType::kVarintValue:
      convert = ConvertDecimalVia::kVarint;
      break;
    case InternalType::kDecimalValue:
      convert = ConvertDecimalVia::kDecimal;
      break;

    case InternalType::kInt8Value:
      int_num = source->int8_value();
      convert = ConvertDecimalVia::kInt64;
      break;
    case InternalType::kInt16Value:
      int_num = source->int16_value();
      convert = ConvertDecimalVia::kInt64;
      break;
    case InternalType::kInt32Value:
      int_num = source->int32_value();
      convert = ConvertDecimalVia::kInt64;
      break;
    case InternalType::kInt64Value:
      int_num = source->int64_value();
      convert = ConvertDecimalVia::kInt64;
      break;

    case InternalType::kFloatValue:
      double_num = source->float_value();
      convert = ConvertDecimalVia::kDouble;
      break;
    case InternalType::kDoubleValue:
      double_num = source->double_value();
      convert = ConvertDecimalVia::kDouble;
      break;

    default: // Process all unexpected cases in the next switch.
      convert = ConvertDecimalVia::kUnknown;
  }

  util::Decimal d;
  switch(convert) {
    case ConvertDecimalVia::kString:
      RSTATUS_DCHECK_EQ(source->value_case(), InternalType::kStringValue,
          InvalidArgument, strings::Substitute("Invalid source type: ",
                                               QLType::ToCQLString(source_datatype)));
      RETURN_NOT_OK(d.FromString(source->string_value()));
      break;
    case ConvertDecimalVia::kVarint:
      RSTATUS_DCHECK_EQ(source->value_case(), InternalType::kVarintValue,
          InvalidArgument, strings::Substitute("Invalid source type: ",
                                               QLType::ToCQLString(source_datatype)));
      RETURN_NOT_OK(d.FromVarInt(QLValue::varint_value(*source)));
      break;
    case ConvertDecimalVia::kDecimal:
      RSTATUS_DCHECK_EQ(source->value_case(), InternalType::kDecimalValue,
          InvalidArgument, strings::Substitute("Invalid source type: ",
                                               QLType::ToCQLString(source_datatype)));
      RETURN_NOT_OK(d.DecodeFromComparable(source->decimal_value()));
      break;
    case ConvertDecimalVia::kInt64:
      RETURN_NOT_OK(d.FromVarInt(util::VarInt(int_num)));
      break;
    case ConvertDecimalVia::kDouble:
      RETURN_NOT_OK(d.FromDouble(double_num));
      break;
    case ConvertDecimalVia::kUnknown:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::DECIMAL));
  }

  target->set_decimal_value(d.EncodeToComparable());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToString(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetStringResult(source, target);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToTimestamp(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetTimestampResult(source, target);
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToDate(PTypePtr source, RTypePtr target) {
  if (IsNull(*source)) {
    SetNull(&*target);
    return Status::OK();
  }
  return SetDateResult(source, target);
}

} // namespace bfql
} // namespace yb
