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
// See the header of file "/util/bfpg/bfpg.h" for overall info.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <iostream>
#include <string>

#include <boost/preprocessor/list/for_each.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/to_list.hpp>

#include "yb/bfcommon/bfunc_convert.h"

#include "yb/common/ql_value.h"
#include "yb/common/value.messages.h"

#include "yb/gutil/casts.h"

#include "yb/util/date_time.h"
#include "yb/util/logging.h"
#include "yb/util/net/inetaddress.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_fwd.h"
#include "yb/util/stol_utils.h"

namespace yb {
namespace bfpg {

//--------------------------------------------------------------------------------------------------
// The following functions are for numeric conversions.

#define YB_PGSQL_DEFINE_CONVERT_INT(From, ToName, ToFunc) \
  using bfcommon::BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(ConvertI, From), To), ToName);

YB_BFCOMMON_APPLY_FOR_ALL_INT_CONVERSIONS(YB_PGSQL_DEFINE_CONVERT_INT)

// Conversion from float to others.
using bfcommon::ConvertFloatToFloat;
using bfcommon::ConvertFloatToDouble;
using bfcommon::ConvertDoubleToFloat;
using bfcommon::ConvertDoubleToDouble;

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

template<typename PTypePtr, typename RTypePtr>
Status ConvertStringToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBoolToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt8ToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt16ToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt32ToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInt64ToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertVarintToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertFloatToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDoubleToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDecimalToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDateToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertUuidToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertInetToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertListToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertMapToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertSetToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTupleToBlob(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

// The following functions are for conversions from blob / binary to other datatypes.

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToString(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToBool(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt8(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt16(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt32(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInt64(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToVarint(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToFloat(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToDouble(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToDecimal(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToDate(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTime(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTimestamp(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToUuid(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTimeuuid(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToInet(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToList(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToMap(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToSet(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertBlobToTuple(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions between date-time datatypes.
template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToDate(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToDate(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToTime(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToTime(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDateToTimestamp(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToTimestamp(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertDateToUnixTimestamp(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimestampToUnixTimestamp(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertTimeuuidToUnixTimestamp(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToMaxTimeuuid(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

template<typename PTypePtr, typename RTypePtr>
Status ConvertToMinTimeuuid(PTypePtr source, RTypePtr target) {
  return bfcommon::NotImplemented();
}

//--------------------------------------------------------------------------------------------------
// The following functions are for conversions from VarInt to the other numeric types.

using bfcommon::ConvertVarintToI8;
using bfcommon::ConvertVarintToI16;
using bfcommon::ConvertVarintToI32;
using bfcommon::ConvertVarintToI64;
using bfcommon::ConvertVarintToFloat;
using bfcommon::ConvertVarintToDouble;

} // namespace bfpg
} // namespace yb
