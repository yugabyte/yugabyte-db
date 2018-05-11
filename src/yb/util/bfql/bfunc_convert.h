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

#ifndef YB_UTIL_BFQL_BFUNC_CONVERT_H_
#define YB_UTIL_BFQL_BFUNC_CONVERT_H_

#include <iostream>
#include <string>

#include "yb/common/ql_value.h"
#include "yb/util/date_time.h"
#include "yb/util/logging.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/status.h"
#include "yb/util/stol_utils.h"
#include "yb/util/uuid.h"

namespace yb {
namespace bfql {

//--------------------------------------------------------------------------------------------------
// The following functions are for numeric conversions.

// Conversion for int8.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToI8(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int8_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToI16(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int16_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToI32(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int32_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToI64(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int64_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_float_value(source->int8_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_double_value(source->int8_value());
  }
  return Status::OK();
}

// Conversion from int16 to others.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToI8(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int8_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToI16(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int16_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToI32(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int32_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToI64(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int64_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_float_value(source->int16_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_double_value(source->int16_value());
  }
  return Status::OK();
}

// Conversion from int32 to others.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToI8(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int8_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToI16(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int16_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToI32(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int32_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToI64(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int64_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_float_value(source->int32_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_double_value(source->int32_value());
  }
  return Status::OK();
}

// Conversion from int64 to others.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToI8(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int8_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToI16(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int16_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToI32(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int32_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToI64(PTypePtr source, RTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int64_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_float_value(source->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_double_value(source->int64_value());
  }
  return Status::OK();
}

// Conversion from float to others.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertFloatToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_float_value(source->float_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertFloatToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_double_value(source->float_value());
  }
  return Status::OK();
}

// Conversion from double to others.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDoubleToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_float_value(source->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDoubleToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_double_value(source->double_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for timestamp conversion.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimestampToI64(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_int64_value(source->timestamp_value().ToInt64());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToTimestamp(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_timestamp_value(DateTime::TimestampFromInt(source->int64_value()).ToInt64());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimestampToString(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_string_value(source->timestamp_value().ToString());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertStringToTimestamp(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
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
CHECKED_STATUS ConvertStringToString(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_string_value(source->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertStringToInet(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    InetAddress addr;
    RETURN_NOT_OK(addr.FromString(source->string_value()));
    target->set_inetaddress_value(addr);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for boolean conversion.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBoolToBool(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_bool_value(source->bool_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions to blob / binary from other datatypes.

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertStringToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBoolToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertInt8ToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertInt16ToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertInt32ToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertInt64ToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertFloatToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDoubleToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDecimalToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDateToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimeToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimestampToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertUuidToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimeuuidToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertInetToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertListToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertMapToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertSetToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTupleToBlob(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

// The following functions are for conversions from blob / binary to other datatypes.

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToString(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToBool(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToInt8(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToInt16(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToInt32(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToInt64(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToVarint(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToFloat(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToDouble(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToDecimal(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToDate(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToTime(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToTimestamp(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToUuid(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToTimeuuid(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToInet(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToList(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToMap(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToSet(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertBlobToTuple(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions between date-time datatypes.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimeuuidToDate(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimestampToDate(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimeuuidToTime(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimestampToTime(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDateToTimestamp(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimeuuidToTimestamp(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    Uuid time_uuid = source->timeuuid_value();
    int64_t unix_timestamp;
    RETURN_NOT_OK(time_uuid.toUnixTimestamp(&unix_timestamp));
    target->set_timestamp_value(Timestamp(DateTime::AdjustPrecision
                                              (unix_timestamp,
                                               DateTime::kYQLUnixTimestampPrecision,
                                               DateTime::kInternalPrecision)));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertDateToUnixTimestamp(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimestampToUnixTimestamp(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    int64_t unix_timestamp = DateTime::AdjustPrecision(source->timestamp_value().ToInt64(),
                                                       DateTime::kInternalPrecision,
                                                       DateTime::kYQLUnixTimestampPrecision);
    target->set_int64_value(unix_timestamp);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertTimeuuidToUnixTimestamp(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    Uuid time_uuid = source->timeuuid_value();
    int64_t unix_timestamp;
    RETURN_NOT_OK(time_uuid.toUnixTimestamp(&unix_timestamp));
    target->set_int64_value(unix_timestamp);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToMaxTimeuuid(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToMinTimeuuid(PTypePtr source, RTypePtr target) {
  return STATUS(RuntimeError, "Not yet implemented");
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions from VarInt to the other numeric types.

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToI8(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    int64_t val;
    RETURN_NOT_OK(source->varint_value().ToInt64(&val));
    if (val < INT8_MIN || val > INT8_MAX) {
      return STATUS(InvalidArgument, "VarInt cannot be converted to int8 due to overflow");
    }
    target->set_int8_value(val);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToI16(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    int64_t val;
    RETURN_NOT_OK(source->varint_value().ToInt64(&val));
    if (val < INT16_MIN || val > INT16_MAX) {
      return STATUS(InvalidArgument, "VarInt cannot be converted to int16 due to overflow");
    }
    target->set_int16_value(val);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToI32(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    int64_t val;
    RETURN_NOT_OK(source->varint_value().ToInt64(&val));
    if (val < INT32_MIN || val > INT32_MAX) {
      return STATUS(InvalidArgument, "VarInt cannot be converted to int32 due to overflow");
    }
    target->set_int32_value(val);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToI64(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    int64_t val;
    RETURN_NOT_OK(source->varint_value().ToInt64(&val));
    target->set_int64_value(val);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToFloat(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    // This may lose precision, it should return the closest float value to the input number.
    util::Decimal val;
    RETURN_NOT_OK(val.FromVarInt(source->varint_value()));
    auto dbl = val.ToDouble();
    RETURN_NOT_OK(dbl);
    target->set_float_value(static_cast<float>(*dbl));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertVarintToDouble(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    // This may lose precision, it should return the closest double value to the input number.
    util::Decimal val;
    RETURN_NOT_OK(val.FromVarInt(source->varint_value()));
    auto dbl = val.ToDouble();
    RETURN_NOT_OK(dbl);
    target->set_double_value(*dbl);
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI8ToVarint(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_varint_value(util::VarInt(static_cast<int64_t>(source->int8_value())));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI16ToVarint(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_varint_value(util::VarInt(static_cast<int64_t>(source->int16_value())));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI32ToVarint(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_varint_value(util::VarInt(static_cast<int64_t>(source->int32_value())));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertI64ToVarint(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    target->set_varint_value(util::VarInt(source->int64_value()));
  }
  return Status::OK();
}

template<typename SetResult, typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SetNumericResult(SetResult set_result, PTypePtr source, DataType target_datatype,
                       RTypePtr target) {
  DataType source_datatype = QLValue::FromInternalDataType(source->type());
  if (!QLType::IsExplicitlyConvertible(target_datatype, source_datatype)) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(target_datatype));
  }

  switch(source->type()) {
    case QLValue::InternalType::kInt32Value:
      RETURN_NOT_OK(set_result(source->int32_value(), target));
      break;
    case QLValue::InternalType::kInt16Value:
      RETURN_NOT_OK(set_result(source->int16_value(), target));
      break;
    case QLValue::InternalType::kInt64Value:
      RETURN_NOT_OK(set_result(source->int64_value(), target));
      break;
    case QLValue::InternalType::kFloatValue:
      RETURN_NOT_OK(set_result(source->float_value(), target));
      break;
    case QLValue::InternalType::kDoubleValue:
      RETURN_NOT_OK(set_result(source->double_value(), target));
      break;
    default:
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid datatype for cast: $0", source->type());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SetStringResult(PTypePtr source, RTypePtr target) {
  DataType source_datatype = QLValue::FromInternalDataType(source->type());
  if (!QLType::IsExplicitlyConvertible(DataType::STRING, source_datatype)) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Cannot convert $0 to $1",
                             QLType::ToCQLString(source_datatype),
                             QLType::ToCQLString(DataType::STRING));
  }

  switch(source->type()) {
    case QLValue::InternalType::kInt32Value:
      target->set_string_value(std::to_string(source->int32_value()));
      break;
    case QLValue::InternalType::kInt16Value:
      target->set_string_value(std::to_string(source->int16_value()));
      break;
    case QLValue::InternalType::kInt64Value:
      target->set_string_value(std::to_string(source->int64_value()));
      break;
    case QLValue::InternalType::kFloatValue:
      target->set_string_value(std::to_string(source->float_value()));
      break;
    case QLValue::InternalType::kDoubleValue:
      target->set_string_value(std::to_string(source->double_value()));
      break;
    case QLValue::InternalType::kStringValue:
      target->set_string_value(source->string_value());
      break;
    default:
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid datatype for cast: $0", source->type());
  }
  return Status::OK();
}

template<typename RTypePtr, typename StrToNum, typename SetTarget>
CHECKED_STATUS StringToNumeric(const string& str_val, RTypePtr target, StrToNum strToNum,
                               SetTarget setTarget) {
  auto result = strToNum(str_val);
  RETURN_NOT_OK(result);
  setTarget(*result, target);
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS ToInt32(int32_t val, RTypePtr target) {
  target->set_int32_value(val);
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS ToInt64(int64_t val, RTypePtr target) {
  target->set_int64_value(val);
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS ToInt16(int16_t val, RTypePtr target) {
  target->set_int16_value(val);
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS ToFloat(float val, RTypePtr target) {
  target->set_float_value(val);
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS ToDouble(double val, RTypePtr target) {
  target->set_double_value(val);
  return Status::OK();
}

template<typename RTypePtr, typename PTypePtr, typename StrToNum, typename ToNumeric>
CHECKED_STATUS ConvertToNumeric(PTypePtr source, RTypePtr target, const DataType& data_type,
                                StrToNum strToNum, ToNumeric toNumeric) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    if (source->type() == QLValue::InternalType::kStringValue) {
      RETURN_NOT_OK(StringToNumeric<RTypePtr>(source->string_value(), target,
                                              strToNum, toNumeric));
    } else {
      RETURN_NOT_OK(SetNumericResult(toNumeric, source, data_type, target));
    }
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToI32(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::INT32, util::CheckedStoi,
                          ToInt32<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToI16(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::INT16, util::CheckedStoi,
                          ToInt16<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToI64(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::INT64, util::CheckedStoll,
                          ToInt64<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToDouble(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::DOUBLE, util::CheckedStold,
                          ToDouble<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToFloat(PTypePtr source, RTypePtr target) {
  return ConvertToNumeric(source, target, DataType::FLOAT, util::CheckedStold,
                          ToFloat<RTypePtr>);
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ConvertToString(PTypePtr source, RTypePtr target) {
  if (source->IsNull()) {
    target->SetNull();
  } else {
    return SetStringResult(source, target);
  }
  return Status::OK();
}

} // namespace bfql
} // namespace yb

#endif  // YB_UTIL_BFQL_BFUNC_CONVERT_H_
