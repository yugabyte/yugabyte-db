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

#include "yb/bfcommon/bfunc_convert.h"

#include "yb/bfql/base_operator.h"

namespace yb {
namespace bfql {

//--------------------------------------------------------------------------------------------------

#define YB_QL_DEFINE_CONVERT_INT(From, ToName, ToFunc) \
  using bfcommon::BOOST_PP_CAT(BOOST_PP_CAT(BOOST_PP_CAT(ConvertI, From), To), ToName);

YB_BFCOMMON_APPLY_FOR_ALL_INT_CONVERSIONS(YB_QL_DEFINE_CONVERT_INT)

// Conversion from float to others.
using bfcommon::ConvertFloatToFloat;
using bfcommon::ConvertFloatToDouble;
using bfcommon::ConvertDoubleToFloat;
using bfcommon::ConvertDoubleToDouble;

//--------------------------------------------------------------------------------------------------
// The following functions are for timestamp conversion.
using bfcommon::ConvertTimestampToI64;
using bfcommon::ConvertI64ToTimestamp;
using bfcommon::ConvertTimestampToString;
using bfcommon::ConvertStringToTimestamp;
using bfcommon::ConvertStringToString;
using bfcommon::ConvertStringToInet;
using bfcommon::ConvertBoolToBool;
using bfcommon::ConvertStringToBlob;
using bfcommon::ConvertBoolToBlob;
using bfcommon::ConvertInt8ToBlob;
using bfcommon::ConvertInt16ToBlob;
using bfcommon::ConvertInt32ToBlob;
using bfcommon::ConvertInt64ToBlob;
using bfcommon::ConvertFloatToBlob;
using bfcommon::ConvertDoubleToBlob;
using bfcommon::ConvertTimestampToBlob;
using bfcommon::ConvertUuidToBlob;
using bfcommon::ConvertTimeuuidToBlob;
using bfcommon::ConvertBlobToString;
using bfcommon::ConvertBlobToBool;
using bfcommon::ConvertBlobToInt8;
using bfcommon::ConvertBlobToInt16;
using bfcommon::ConvertBlobToInt32;
using bfcommon::ConvertBlobToInt64;
using bfcommon::ConvertBlobToFloat;
using bfcommon::ConvertBlobToDouble;
using bfcommon::ConvertBlobToTimestamp;
using bfcommon::ConvertBlobToUuid;
using bfcommon::ConvertBlobToTimeuuid;
using bfcommon::ConvertTimeuuidToDate;
using bfcommon::ConvertTimestampToDate;
using bfcommon::ConvertDateToTimestamp;
using bfcommon::ConvertTimeuuidToTimestamp;
using bfcommon::ConvertDateToUnixTimestamp;
using bfcommon::ConvertTimestampToUnixTimestamp;
using bfcommon::ConvertTimeuuidToUnixTimestamp;
using bfcommon::ConvertToMaxTimeuuid;
using bfcommon::ConvertToMinTimeuuid;
using bfcommon::ConvertVarintToI8;
using bfcommon::ConvertVarintToI16;
using bfcommon::ConvertVarintToI32;
using bfcommon::ConvertVarintToI64;
using bfcommon::ConvertVarintToFloat;
using bfcommon::ConvertVarintToDouble;
using bfcommon::ConvertI8ToVarint;
using bfcommon::ConvertI16ToVarint;
using bfcommon::ConvertI32ToVarint;
using bfcommon::ConvertI64ToVarint;
using bfcommon::ConvertToI32;
using bfcommon::ConvertToI16;
using bfcommon::ConvertToI64;
using bfcommon::ConvertToDouble;
using bfcommon::ConvertToFloat;
using bfcommon::ConvertToDecimal;
using bfcommon::ConvertToString;
using bfcommon::ConvertToTimestamp;
using bfcommon::ConvertToDate;

inline Result<BFRetValue> ConvertTimeuuidToTime(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertTimestampToTime(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertVarintToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertDecimalToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertDateToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertTimeToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertInetToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertListToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertMapToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertSetToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertTupleToBlob(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToVarint(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToDecimal(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToDate(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToTime(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToInet(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToList(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToMap(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToSet(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

inline Result<BFRetValue> ConvertBlobToTuple(const BFValue& source, BFFactory factory) {
  return bfcommon::NotImplemented();
}

} // namespace bfql
} // namespace yb
