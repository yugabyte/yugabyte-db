//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// This module defines of C++ functions to support "cast" operators. Note that for "cast" operators,
// all arguments must of exact types. Type resolution will raise error if an argument type is
// only compatible / convertible but not equal.
//   Example: cast(int64 to string) will accept only int64 and string but not int32 and string.
//
// Once written, this function should not be changed to avoid compatibility issues. That is,
// server might runs one version while client use a different version of this function.
//
// See the header of file "/util/bfyql/bfyql.h" for overall info.
//--------------------------------------------------------------------------------------------------

#ifndef YB_UTIL_BFYQL_BFUNC_CONVERT_H_
#define YB_UTIL_BFYQL_BFUNC_CONVERT_H_

#include <iostream>
#include <string>

#include "yb/util/status.h"
#include "yb/util/logging.h"

namespace yb {
namespace bfyql {

//--------------------------------------------------------------------------------------------------
// The following functions are for numeric conversions.

// Conversion from int8 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI16(PTypePtr source, PTypePtr target) {
  target->set_int16_value(source->int8_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI32(PTypePtr source, PTypePtr target) {
  target->set_int32_value(source->int8_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToI64(PTypePtr source, PTypePtr target) {
  target->set_int64_value(source->int8_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToFloat(PTypePtr source, PTypePtr target) {
  target->set_float_value(source->int8_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI8ToDouble(PTypePtr source, PTypePtr target) {
  target->set_double_value(source->int8_value());
  return Status::OK();
}

// Conversion from int16 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI8(PTypePtr source, PTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  target->set_int8_value(source->int16_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI32(PTypePtr source, PTypePtr target) {
  target->set_int32_value(source->int16_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToI64(PTypePtr source, PTypePtr target) {
  target->set_int64_value(source->int16_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToFloat(PTypePtr source, PTypePtr target) {
  target->set_float_value(source->int16_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI16ToDouble(PTypePtr source, PTypePtr target) {
  target->set_double_value(source->int16_value());
  return Status::OK();
}

// Conversion from int32 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI8(PTypePtr source, PTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  target->set_int8_value(source->int32_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI16(PTypePtr source, PTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  target->set_int16_value(source->int32_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToI64(PTypePtr source, PTypePtr target) {
  target->set_int64_value(source->int32_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToFloat(PTypePtr source, PTypePtr target) {
  target->set_float_value(source->int32_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI32ToDouble(PTypePtr source, PTypePtr target) {
  target->set_double_value(source->int32_value());
  return Status::OK();
}

// Conversion from int64 to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI8(PTypePtr source, PTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  target->set_int8_value(source->int64_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI16(PTypePtr source, PTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  target->set_int16_value(source->int64_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToI32(PTypePtr source, PTypePtr target) {
  // TODO(neil) Overflow? When we truely support expressions, these loose-ends must be fixed.
  target->set_int32_value(source->int64_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToFloat(PTypePtr source, PTypePtr target) {
  target->set_float_value(source->int64_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToDouble(PTypePtr source, PTypePtr target) {
  target->set_double_value(source->int64_value());
  return Status::OK();
}

// Conversion from float to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertFloatToDouble(PTypePtr source, PTypePtr target) {
  target->set_double_value(source->float_value());
  return Status::OK();
}

// Conversion from double to others.
template<typename PTypePtr, typename RTypePtr>
Status ConvertDoubleToFloat(PTypePtr source, PTypePtr target) {
  target->set_float_value(source->double_value());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for timestamp conversion.
template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToI64(PTypePtr source, PTypePtr target) {
  // target->set_int64_value(source->timestamp_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertI64ToTimestamp(PTypePtr source, PTypePtr target) {
  // target->set_timestamp_value(source->int64_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToString(PTypePtr source, PTypePtr target) {
  LOG(FATAL) << "Not yet supported";
  // target->set_string_value(source->timestamp_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToTimestamp(PTypePtr source, PTypePtr target) {
  LOG(FATAL) << "Not yet supported";
  // target->set_timestamp_value(source->string_value());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for string conversion.

template<typename PTypePtr, typename RTypePtr>
Status ConvertInetToString(PTypePtr source, PTypePtr target) {
  LOG(FATAL) << "Not yet supported";
  // target->set_string_value(source->inet_value());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToInet(PTypePtr source, PTypePtr target) {
  LOG(FATAL) << "Not yet supported";
  // target->set_inet_value(source->string_value());
  return Status::OK();
}

} // namespace bfyql
} // namespace yb

#endif  // YB_UTIL_BFYQL_BFUNC_CONVERT_H_
