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

#include "yb/bfcommon/bfdecl.h"

namespace yb::bfcommon {

//--------------------------------------------------------------------------------------------------
// Dummy function for minimum opcode.
inline Result<BFRetValue> NoOp() {
  static const BFValue result;
  return result;
}

// ServerOperator that takes no argument and has no return value.
inline Status ServerOperator() {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// ServerOperator that takes 1 argument and has a return value.
inline Status ServerOperator(const BFValue& arg1, BFFactory factory) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// This is not used but implemented as an example for future coding.
// ServerOperator that takes 2 arguments and has a return value.
inline Status ServerOperator(const BFValue& arg1, const BFValue& arg2, BFFactory factory) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

//--------------------------------------------------------------------------------------------------
// "+" and "-".

inline Result<BFRetValue> AddI64I64(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_int64_value(x.int64_value() + y.int64_value());
  }
  return result;
}

inline Result<BFRetValue> AddDoubleDouble(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_double_value(x.double_value() + y.double_value());
  }
  return result;
}

inline Result<BFRetValue> AddStringString(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_string_value(AsString(x.string_value()) + AsString(y.string_value()));
  }
  return result;
}

inline Result<BFRetValue> AddStringDouble(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_string_value(AsString(x.string_value()) + std::to_string(y.double_value()));
  }
  return result;
}

inline Result<BFRetValue> AddDoubleString(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_string_value(std::to_string(x.double_value()) + AsString(y.string_value()));
  }
  return result;
}

inline Result<BFRetValue> SubI64I64(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_int64_value(x.int64_value() - y.int64_value());
  }
  return result;
}

inline Result<BFRetValue> SubDoubleDouble(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (!IsNull(x) && !IsNull(y)) {
    result.set_double_value(x.double_value() - y.double_value());
  }
  return result;
}

inline Result<BFRetValue> NowTimeUuid(BFFactory factory) {
  uuid_t linux_time_uuid;
  uuid_generate_time(linux_time_uuid);
  Uuid time_uuid(linux_time_uuid);
  RETURN_NOT_OK(time_uuid.IsTimeUuid());
  RETURN_NOT_OK(time_uuid.HashMACAddress());
  BFRetValue result = factory();
  QLValue::set_timeuuid_value(time_uuid, &result);
  return result;
}

} // namespace yb::bfcommon
