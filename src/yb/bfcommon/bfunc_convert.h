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

#pragma once

#include <iostream>
#include <string>

#include <boost/preprocessor/list/for_each.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/to_list.hpp>

#include "yb/bfql/base_operator.h"

#include "yb/common/ql_datatype.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"
#include "yb/common/value.messages.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/endian.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/util/date_time.h"
#include "yb/util/decimal.h"
#include "yb/util/enums.h"
#include "yb/util/logging.h"
#include "yb/util/memory/arena.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_fwd.h"
#include "yb/util/stol_utils.h"
#include "yb/util/uuid.h"

namespace yb::bfcommon {

static constexpr size_t kSizeBool = 1;
static constexpr size_t kSizeTinyInt = 1;
static constexpr size_t kSizeSmallInt = 2;
static constexpr size_t kSizeInt = 4;
static constexpr size_t kSizeBigInt = 8;
static constexpr size_t kSizeUuid = 16;
static constexpr size_t kByteSize = 8;
static constexpr size_t kHexBase = 16;

//--------------------------------------------------------------------------------------------------
template<typename SetResult, typename RTypePtr>
Status SetNumericResult(
    SetResult set_result, BFParam source, DataType target_datatype, RTypePtr target) {
  auto source_datatype = InternalToDataType(source.value_case());
  if (!QLType::IsExplicitlyConvertible(target_datatype, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(target_datatype));
  }

  switch(source.value_case()) {
    case InternalType::kInt8Value:
      set_result(source.int8_value(), target);
      break;
    case InternalType::kInt16Value:
      set_result(source.int16_value(), target);
      break;
    case InternalType::kInt32Value:
      set_result(source.int32_value(), target);
      break;
    case InternalType::kInt64Value:
      set_result(source.int64_value(), target);
      break;
    case InternalType::kFloatValue:
      set_result(source.float_value(), target);
      break;
    case InternalType::kDoubleValue:
      set_result(source.double_value(), target);
      break;
    case InternalType::kDecimalValue: {
        util::Decimal d;
        RETURN_NOT_OK(d.DecodeFromComparable(source.decimal_value()));

        if (target_datatype == DataType::FLOAT || target_datatype == DataType::DOUBLE) {
          // Convert via DOUBLE:
          set_result(VERIFY_RESULT(d.ToDouble()), target);
        } else { // Expected an Integer type
          RSTATUS_DCHECK(target_datatype == DataType::INT8 || target_datatype == DataType::INT16
              || target_datatype == DataType::INT32 || target_datatype == DataType::INT64,
              InvalidArgument, strings::Substitute("Unexpected target type: ",
                                                   QLType::ToCQLString(target_datatype)));
          // Convert via INT64:
          set_result(VERIFY_RESULT(VERIFY_RESULT(d.ToVarInt()).ToInt64()), target);
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

inline Result<std::string> GetString(BFParam source) {
  switch(source.value_case()) {
    case InternalType::kInt8Value:
      return std::to_string(source.int8_value());
    case InternalType::kInt16Value:
      return std::to_string(source.int16_value());
    case InternalType::kInt32Value:
      return std::to_string(source.int32_value());
    case InternalType::kInt64Value:
      return std::to_string(source.int64_value());
    case InternalType::kFloatValue:
      return std::to_string(source.float_value());
    case InternalType::kDoubleValue:
      return std::to_string(source.double_value());
    case InternalType::kStringValue:
      return AsString(source.string_value());
    case InternalType::kBoolValue:
      return source.bool_value() ? "true" : "false";
    case InternalType::kTimestampValue:
      return DateTime::TimestampToString(QLValue::timestamp_value(source));
    case InternalType::kDateValue:
      return VERIFY_RESULT(DateTime::DateToString(source.date_value()));
    case InternalType::kTimeValue:
      return VERIFY_RESULT(DateTime::TimeToString(source.time_value()));
    case InternalType::kUuidValue:
      return QLValue::uuid_value(source).ToString();
    case InternalType::kTimeuuidValue:
      return QLValue::timeuuid_value(source).ToString();
    case InternalType::kBinaryValue:
      return "0x" + b2a_hex(source.binary_value());
    case InternalType::kInetaddressValue:
      return QLValue::inetaddress_value(source).ToString();
    case InternalType::kDecimalValue: {
        util::Decimal d;
        RETURN_NOT_OK(d.DecodeFromComparable(source.decimal_value()));
        return d.ToString();
      }
    default:
      return STATUS_FORMAT(QLError, "Cannot cast $0 to $1",
                           QLType::ToCQLString(InternalToDataType(source.value_case())),
                           QLType::ToCQLString(DataType::STRING));
  }
}

inline Result<BFRetValue> MakeStringResult(BFParam source, BFFactory factory) {
  auto source_datatype = InternalToDataType(source.value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::STRING, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::STRING));
  }

  BFRetValue result = factory();
  result.set_string_value(VERIFY_RESULT(GetString(source)));
  return result;
}

inline Result<BFRetValue> MakeTimestampResult(BFParam source, BFFactory factory) {
  auto source_datatype = InternalToDataType(source.value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::TIMESTAMP, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::TIMESTAMP));
  }

  BFRetValue result = factory();
  switch(source.value_case()) {
    case InternalType::kTimeuuidValue: {
      auto time_uuid = QLValue::timeuuid_value(source);
      int64_t unix_timestamp;
      RETURN_NOT_OK(time_uuid.ToUnixTimestamp(&unix_timestamp));
      result.set_timestamp_value(Timestamp(DateTime::AdjustPrecision(
          unix_timestamp, DateTime::kMillisecondPrecision, DateTime::kInternalPrecision))
          .ToInt64());
      break;
    }
    case InternalType::kDateValue:
      result.set_timestamp_value(DateTime::DateToTimestamp(source.date_value()).ToInt64());
      break;
    default:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::TIMESTAMP));
  }
  return result;
}

inline Result<BFRetValue> MakeDateResult(BFParam source, BFFactory factory) {
  auto source_datatype = InternalToDataType(source.value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::DATE, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::DATE));
  }

  BFRetValue result = factory();
  switch(source.value_case()) {
    case InternalType::kTimestampValue:
      result.set_date_value(VERIFY_RESULT(DateTime::DateFromTimestamp(
          QLValue::timestamp_value(source))));
      break;
    case InternalType::kTimeuuidValue: {
      auto time_uuid = QLValue::timeuuid_value(source);
      int64_t unix_timestamp;
      RETURN_NOT_OK(time_uuid.ToUnixTimestamp(&unix_timestamp));
      result.set_date_value(VERIFY_RESULT(DateTime::DateFromUnixTimestamp(unix_timestamp)));
      break;
    }
    default:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::DATE));
  }
  return result;
}

template<typename RTypePtr, typename StrToNum, typename SetTarget>
Status StringToNumeric(const std::string_view& str_val, RTypePtr target, StrToNum str_to_num,
                       SetTarget set_target) {
  set_target(VERIFY_RESULT(str_to_num(str_val)), target);
  return Status::OK();
}

#define YB_BFCOMMON_DEFINE_CONVERT(FromName, FromFunc, ToName, ToFunc) \
inline Result<BFRetValue> BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(Convert, FromName), To), ToName)( \
    BFParam source, BFFactory factory) { \
  if (IsNull(source)) { \
    return source; \
  } \
  BFRetValue result = factory(); \
  result.BOOST_PP_CAT(BOOST_PP_CAT(set_, ToFunc), _value)( \
      source.BOOST_PP_CAT(FromFunc, _value)()); \
  return result; \
}

#define YB_BFCOMMON_DEFINE_CONVERT_INT(From, ToName, ToFunc) \
  YB_BFCOMMON_DEFINE_CONVERT(BOOST_PP_CAT(I, From), BOOST_PP_CAT(int, From), ToName, ToFunc)

#define YB_BFCOMMON_DEFINE_CONVERT_INT_TO_INT(macro, From, To) \
  macro(From, BOOST_PP_CAT(I, To), BOOST_PP_CAT(int, To))

//--------------------------------------------------------------------------------------------------
#define YB_BFCOMMON_INT_BITS (8)(16)(32)(64)
#define YB_BFCOMMON_INT_BITS_LIST BOOST_PP_SEQ_TO_LIST(YB_BFCOMMON_INT_BITS)

#define YB_BFCOMMON_DEFINE_CONVERT_INT_FROM_TO(r, data_tuple, to) \
  YB_BFCOMMON_DEFINE_CONVERT_INT_TO_INT( \
      BOOST_PP_TUPLE_ELEM(2, 0, data_tuple), BOOST_PP_TUPLE_ELEM(2, 1, data_tuple), to)

#define YB_BFCOMMON_DEFINE_CONVERT_INT_FROM(r, data, elem) \
  BOOST_PP_LIST_FOR_EACH( \
      YB_BFCOMMON_DEFINE_CONVERT_INT_FROM_TO, (data, elem), YB_BFCOMMON_INT_BITS_LIST) \
  data(elem, Float, float) \
  data(elem, Double, double)

#define YB_BFCOMMON_APPLY_FOR_ALL_INT_CONVERSIONS(macro) \
  BOOST_PP_SEQ_FOR_EACH( \
      YB_BFCOMMON_DEFINE_CONVERT_INT_FROM, macro, YB_BFCOMMON_INT_BITS)

YB_BFCOMMON_APPLY_FOR_ALL_INT_CONVERSIONS(YB_BFCOMMON_DEFINE_CONVERT_INT)

// Conversion from float to others.
YB_BFCOMMON_DEFINE_CONVERT(Float, float, Float, float);
YB_BFCOMMON_DEFINE_CONVERT(Float, float, Double, double);
YB_BFCOMMON_DEFINE_CONVERT(Double, double, Float, float);
YB_BFCOMMON_DEFINE_CONVERT(Double, double, Double, double);

//--------------------------------------------------------------------------------------------------
// The following functions are for timestamp conversion.
inline Result<BFRetValue> ConvertTimestampToI64(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_int64_value(QLValue::timestamp_value(source).ToInt64());
  return result;
}

inline Result<BFRetValue> ConvertI64ToTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_timestamp_value(DateTime::TimestampFromInt(source.int64_value()).ToInt64());
  return result;
}

inline Result<BFRetValue> ConvertTimestampToString(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_string_value(QLValue::timestamp_value(source).ToString());
  return result;
}

inline Result<BFRetValue> ConvertStringToTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto ts = VERIFY_RESULT(DateTime::TimestampFromString(source.string_value()));
  result.set_timestamp_value(ts.ToInt64());
  return result;
}

//--------------------------------------------------------------------------------------------------
// The following functions are for string conversion.
inline Result<BFRetValue> ConvertStringToString(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_string_value(source.string_value());
  return result;
}

inline Result<BFRetValue> ConvertStringToInet(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_inetaddress_value(InetAddress(
      VERIFY_RESULT(HostToAddress(AsString(source.string_value())))).ToBytes());
  return result;
}

//--------------------------------------------------------------------------------------------------
// The following functions are for boolean conversion.
inline Result<BFRetValue> ConvertBoolToBool(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_bool_value(source.bool_value());
  return result;
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions to blob / binary from other datatypes.

inline Result<BFRetValue> ConvertStringToBlob(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_binary_value(source.string_value());
  return result;
}

inline Result<BFRetValue> ConvertBoolToBlob(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  int8_t byte_stream = (source.bool_value()) ? 1 : 0;
  Slice(pointer_cast<char*>(&byte_stream), kSizeBool).AssignTo(result.mutable_binary_value());
  return result;
}

template <class Extractor>
Result<BFRetValue> ConvertToBlob(
    BFParam source, BFFactory factory, const Extractor& extractor) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto value = extractor(source);
  auto big_endian = Load<decltype(value), BigEndian>(&value);
  Slice(pointer_cast<char*>(&big_endian), sizeof(value)).AssignTo(result.mutable_binary_value());
  return result;
}

inline Result<BFRetValue> ConvertInt8ToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    return static_cast<int8_t>(source.int8_value());
  });
}

inline Result<BFRetValue> ConvertInt16ToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    return static_cast<int16_t>(source.int16_value());
  });
}

inline Result<BFRetValue> ConvertInt32ToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    return source.int32_value();
  });
}

inline Result<BFRetValue> ConvertInt64ToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    return source.int64_value();
  });
}

inline Result<BFRetValue> ConvertFloatToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    auto value = source.float_value();
    return *pointer_cast<uint32_t*>(&value);
  });
}

inline Result<BFRetValue> ConvertDoubleToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    auto value = source.double_value();
    return *pointer_cast<uint64_t*>(&value);
  });
}

inline Result<BFRetValue> ConvertTimestampToBlob(BFParam source, BFFactory factory) {
  return ConvertToBlob(source, factory, [](BFParam source) {
    Timestamp source_val = QLValue::timestamp_value(source);
    auto ts_int_value = source_val.ToInt64();
    return DateTime::AdjustPrecision(
        ts_int_value, DateTime::kInternalPrecision, DateTime::kMillisecondPrecision);

  });
}

inline Result<BFRetValue> ConvertUuidToBlob(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  QLValue::uuid_value(source).ToBytes(result.mutable_binary_value());
  return result;
}

inline Result<BFRetValue> ConvertTimeuuidToBlob(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  QLValue::timeuuid_value(source).ToBytes(result.mutable_binary_value());
  return result;
}

// The following functions are for conversions from blob / binary to other datatypes.

inline Result<BFRetValue> ConvertBlobToString(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_string_value(source.binary_value());
  return result;
}

template <class Setter>
inline Result<BFRetValue> ConvertBlobToEx(
    BFParam source, BFFactory factory, const char* type_name, size_t size,
    const Setter& setter) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto blob = source.binary_value();
  if (blob.size() != size) {
    return STATUS_FORMAT(
        QLError, "The blob string of length) is not a valid string for a $1 type",
        blob.size(), type_name);
  }
  RETURN_NOT_OK(setter(blob, &result));
  return result;
}

template <class Setter>
inline Result<BFRetValue> ConvertBlobTo(
    BFParam source, BFFactory factory, const char* type_name, size_t size,
    const Setter& setter) {
  return ConvertBlobToEx(source, factory, type_name, size, [&setter](Slice blob, auto* out) {
    setter(blob, out);
    return Status();
  });
}

inline Result<BFRetValue> ConvertBlobToBool(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "boolean", kSizeBool, [](Slice blob, auto* out) {
    out->set_bool_value(blob[0] != 0);
  });
}

inline Result<BFRetValue> ConvertBlobToInt8(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "tinyint", kSizeTinyInt, [](Slice blob, auto* out) {
    out->set_int8_value(blob[0]);
  });
}

inline Result<BFRetValue> ConvertBlobToInt16(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "smallint", kSizeSmallInt, [](Slice blob, auto* out) {
    out->set_int16_value(BigEndian::Load16(blob.cdata()));
  });
}

inline Result<BFRetValue> ConvertBlobToInt32(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "int", kSizeInt, [](Slice blob, auto* out) {
    out->set_int32_value(BigEndian::Load32(blob.cdata()));
  });
}

inline Result<BFRetValue> ConvertBlobToInt64(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "bigint", kSizeBigInt, [](Slice blob, auto* out) {
    out->set_int64_value(BigEndian::Load64(blob.cdata()));
  });
}

inline Result<BFRetValue> ConvertBlobToFloat(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "float", kSizeInt, [](Slice blob, auto* out) {
    auto int_value = BigEndian::Load32(blob.cdata());
    out->set_float_value(*pointer_cast<float*>(&int_value));
  });
}

inline Result<BFRetValue> ConvertBlobToDouble(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "double", kSizeBigInt, [](Slice blob, auto* out) {
    auto int_value = BigEndian::Load64(blob.cdata());
    out->set_double_value(*pointer_cast<double*>(&int_value));
  });
}

inline Result<BFRetValue> ConvertBlobToTimestamp(BFParam source, BFFactory factory) {
  return ConvertBlobTo(source, factory, "Timestamp", kSizeBigInt, [](Slice blob, auto* out) {
    auto target_val = DateTime::AdjustPrecision(BigEndian::Load64(blob.cdata()),
                                                DateTime::kMillisecondPrecision,
                                                DateTime::kInternalPrecision);
    Timestamp ts(target_val);
    out->set_timestamp_value(ts.ToInt64());
  });
}

inline Result<BFRetValue> ConvertBlobToUuid(BFParam source, BFFactory factory) {
  return ConvertBlobToEx(source, factory, "UUID", kSizeUuid, [](Slice blob, auto* out) -> Status {
    VERIFY_RESULT(Uuid::FromSlice(blob)).ToBytes(out->mutable_uuid_value());
    return Status::OK();
  });
}

inline Result<BFRetValue> ConvertBlobToTimeuuid(BFParam source, BFFactory factory) {
  return ConvertBlobToEx(source, factory, "UUID", kSizeUuid, [](Slice blob, auto* out) -> Status {
    QLValue::set_timeuuid_value(VERIFY_RESULT(Uuid::FromSlice(blob)), out);
    return Status::OK();
  });
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions between date-time datatypes.
inline Result<BFRetValue> ConvertTimeuuidToDate(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeDateResult(source, factory);
}

inline Result<BFRetValue> ConvertTimestampToDate(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeDateResult(source, factory);
}

inline Result<BFRetValue> ConvertDateToTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeTimestampResult(source, factory);
}

inline Result<BFRetValue> ConvertTimeuuidToTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeTimestampResult(source, factory);
}

inline Result<BFRetValue> ConvertDateToUnixTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_int64_value(DateTime::DateToUnixTimestamp(source.date_value()));
  return result;
}

inline Result<BFRetValue> ConvertTimestampToUnixTimestamp(
    BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto unix_timestamp = DateTime::AdjustPrecision(QLValue::timestamp_value(source).ToInt64(),
                                                  DateTime::kInternalPrecision,
                                                  DateTime::kMillisecondPrecision);
  result.set_int64_value(unix_timestamp);
  return result;
}

inline Result<BFRetValue> ConvertTimeuuidToUnixTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto time_uuid = QLValue::timeuuid_value(source);
  int64_t unix_timestamp;
  RETURN_NOT_OK(time_uuid.ToUnixTimestamp(&unix_timestamp));
  result.set_int64_value(unix_timestamp);
  return result;
}

inline Result<BFRetValue> ConvertToMaxTimeuuid(BFParam source, BFFactory factory) {
  SCHECK(!IsNull(source), RuntimeError, "Cannot get max timeuuid of null");
  BFRetValue result = factory();
  auto timestamp_ms = DateTime::AdjustPrecision(QLValue::timestamp_value(source).ToInt64(),
                                                DateTime::kInternalPrecision,
                                                DateTime::kMillisecondPrecision);

  Uuid uuid;
  RETURN_NOT_OK(uuid.MaxFromUnixTimestamp(timestamp_ms));
  QLValue::set_timeuuid_value(uuid, &result);
  return result;
}

inline Result<BFRetValue> ConvertToMinTimeuuid(BFParam source, BFFactory factory) {
  SCHECK(!IsNull(source), RuntimeError, "Cannot get max timeuuid of null");
  BFRetValue result = factory();
  auto timestamp_ms = DateTime::AdjustPrecision(QLValue::timestamp_value(source).ToInt64(),
                                                DateTime::kInternalPrecision,
                                                DateTime::kMillisecondPrecision);

  Uuid uuid;
  RETURN_NOT_OK(uuid.MinFromUnixTimestamp(timestamp_ms));
  QLValue::set_timeuuid_value(uuid, &result);
  return result;
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions from VarInt to the other numeric types.

inline Result<BFRetValue> ConvertVarintToI8(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto val = VERIFY_RESULT(QLValue::varint_value(source).ToInt64());
  SCHECK(val >= INT8_MIN && val <= INT8_MAX, QLError,
         "VarInt cannot be converted to int8 due to overflow");
  result.set_int8_value(static_cast<int32_t>(val));
  return result;
}

inline Result<BFRetValue> ConvertVarintToI16(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto val = VERIFY_RESULT(QLValue::varint_value(source).ToInt64());
  SCHECK(val >= INT16_MIN && val <= INT16_MAX, QLError,
         "VarInt cannot be converted to int16 due to overflow");
  result.set_int16_value(static_cast<int32_t>(val));
  return result;
}

inline Result<BFRetValue> ConvertVarintToI32(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  auto val = VERIFY_RESULT(QLValue::varint_value(source).ToInt64());
  SCHECK(val >= INT32_MIN && val <= INT32_MAX, QLError,
         "VarInt cannot be converted to int32 due to overflow");
  result.set_int32_value(static_cast<int32_t>(val));
  return result;
}

inline Result<BFRetValue> ConvertVarintToI64(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_int64_value(VERIFY_RESULT(QLValue::varint_value(source).ToInt64()));
  return result;
}

inline Result<BFRetValue> ConvertVarintToFloat(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  // This may lose precision, it should return the closest float value to the input number.
  result.set_float_value(static_cast<float>(VERIFY_RESULT(CheckedStold(
      QLValue::varint_value(source).ToString()))));
  return result;
}

inline Result<BFRetValue> ConvertVarintToDouble(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  // This may lose precision, it should return the closest double value to the input number.
  result.set_double_value(VERIFY_RESULT(CheckedStold(
      QLValue::varint_value(source).ToString())));
  return result;
}

inline Result<BFRetValue> ConvertI8ToVarint(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_varint_value(VarInt(source.int8_value()).EncodeToComparable());
  return result;
}

inline Result<BFRetValue> ConvertI16ToVarint(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_varint_value(VarInt(source.int16_value()).EncodeToComparable());
  return result;
}

inline Result<BFRetValue> ConvertI32ToVarint(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_varint_value(VarInt(source.int32_value()).EncodeToComparable());
  return result;
}

inline Result<BFRetValue> ConvertI64ToVarint(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  result.set_varint_value(VarInt(source.int64_value()).EncodeToComparable());
  return result;
}

template<typename RTypePtr>
void ToInt32(int64_t val, RTypePtr target) {
  target->set_int32_value(static_cast<int32_t>(val));
}

template<typename RTypePtr>
void ToInt64(int64_t val, RTypePtr target) {
  target->set_int64_value(val);
}

template<typename RTypePtr>
void ToInt16(int16_t val, RTypePtr target) {
  target->set_int16_value(val);
}

template<typename RTypePtr>
void ToFloat(float val, RTypePtr target) {
  target->set_float_value(val);
}

template<typename RTypePtr>
void ToDouble(double val, RTypePtr target) {
  target->set_double_value(val);
}

template<typename StrToNum, typename ToNumeric>
inline Result<BFRetValue> ConvertToNumeric(
    BFParam source, BFFactory factory, DataType data_type,
    StrToNum str_to_num, ToNumeric to_numeric) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();
  if (source.value_case() == InternalType::kStringValue) {
    RETURN_NOT_OK(StringToNumeric(source.string_value(), &result, str_to_num, to_numeric));
  } else {
    RETURN_NOT_OK(SetNumericResult(to_numeric, source, data_type, &result));
  }
  return result;
}

inline Result<BFRetValue> ConvertToI32(BFParam source, BFFactory factory) {
  return ConvertToNumeric(source, factory, DataType::INT32, CheckedStoi, ToInt32<BFValue*>);
}

inline Result<BFRetValue> ConvertToI16(BFParam source, BFFactory factory) {
  return ConvertToNumeric(source, factory, DataType::INT16, CheckedStoi, ToInt16<BFValue*>);
}

inline Result<BFRetValue> ConvertToI64(BFParam source, BFFactory factory) {
  return ConvertToNumeric(source, factory, DataType::INT64, CheckedStoll, ToInt64<BFValue*>);
}

inline Result<BFRetValue> ConvertToDouble(BFParam source, BFFactory factory) {
  return ConvertToNumeric(source, factory, DataType::DOUBLE, CheckedStold, ToDouble<BFValue*>);
}

inline Result<BFRetValue> ConvertToFloat(BFParam source, BFFactory factory) {
  return ConvertToNumeric(source, factory, DataType::FLOAT, CheckedStold, ToFloat<BFValue*>);
}

YB_DEFINE_ENUM(ConvertDecimalVia, (kUnknown)(kString)(kVarint)(kDecimal)(kInt64)(kDouble));

inline Result<BFRetValue> ConvertToDecimal(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  BFRetValue result = factory();

  auto source_datatype = InternalToDataType(source.value_case());
  if (!QLType::IsExplicitlyConvertible(DataType::DECIMAL, source_datatype)) {
    return STATUS_SUBSTITUTE(QLError, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::DECIMAL));
  }

  int64_t int_num = 0;
  double double_num = 0.;
  auto convert = ConvertDecimalVia::kUnknown;

  switch(source.value_case()) {
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
      int_num = source.int8_value();
      convert = ConvertDecimalVia::kInt64;
      break;
    case InternalType::kInt16Value:
      int_num = source.int16_value();
      convert = ConvertDecimalVia::kInt64;
      break;
    case InternalType::kInt32Value:
      int_num = source.int32_value();
      convert = ConvertDecimalVia::kInt64;
      break;
    case InternalType::kInt64Value:
      int_num = source.int64_value();
      convert = ConvertDecimalVia::kInt64;
      break;

    case InternalType::kFloatValue:
      double_num = source.float_value();
      convert = ConvertDecimalVia::kDouble;
      break;
    case InternalType::kDoubleValue:
      double_num = source.double_value();
      convert = ConvertDecimalVia::kDouble;
      break;

    default: // Process all unexpected cases in the next switch.
      convert = ConvertDecimalVia::kUnknown;
  }

  util::Decimal d;
  switch(convert) {
    case ConvertDecimalVia::kString:
      RSTATUS_DCHECK_EQ(source.value_case(), InternalType::kStringValue,
          InvalidArgument, strings::Substitute("Invalid source type: ",
                                               QLType::ToCQLString(source_datatype)));
      RETURN_NOT_OK(d.FromString(source.string_value()));
      break;
    case ConvertDecimalVia::kVarint:
      RSTATUS_DCHECK_EQ(source.value_case(), InternalType::kVarintValue,
          InvalidArgument, strings::Substitute("Invalid source type: ",
                                               QLType::ToCQLString(source_datatype)));
      RETURN_NOT_OK(d.FromVarInt(QLValue::varint_value(source)));
      break;
    case ConvertDecimalVia::kDecimal:
      RSTATUS_DCHECK_EQ(source.value_case(), InternalType::kDecimalValue,
          InvalidArgument, strings::Substitute("Invalid source type: ",
                                               QLType::ToCQLString(source_datatype)));
      RETURN_NOT_OK(d.DecodeFromComparable(source.decimal_value()));
      break;
    case ConvertDecimalVia::kInt64:
      RETURN_NOT_OK(d.FromVarInt(VarInt(int_num)));
      break;
    case ConvertDecimalVia::kDouble:
      RETURN_NOT_OK(d.FromDouble(double_num));
      break;
    case ConvertDecimalVia::kUnknown:
      return STATUS_SUBSTITUTE(QLError, "Cannot cast $0 to $1",
                               QLType::ToCQLString(source_datatype),
                               QLType::ToCQLString(DataType::DECIMAL));
  }

  result.set_decimal_value(d.EncodeToComparable());
  return result;
}

inline Result<BFRetValue> ConvertToString(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeStringResult(source, factory);
}

inline Result<BFRetValue> ConvertToTimestamp(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeTimestampResult(source, factory);
}

inline Result<BFRetValue> ConvertToDate(BFParam source, BFFactory factory) {
  if (IsNull(source)) {
    return source;
  }
  return MakeDateResult(source, factory);
}

inline Status NotImplemented() {
  return STATUS(RuntimeError, "Not yet implemented");
}

} // namespace yb::bfcommon
