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
// This module defines standard C++ functions that are used to support PGSQL builtin functions.
// Each of these functions have one or more entries in builtin library directory. Note that C++
// functions don't have to be defined here as long as they are linked to this lib.
//
// Once written, this function should not be changed to avoid compatibility issues. That is,
// server might runs one version while client use a different version of this function.
//
// See the header of file "/util/bfpg/bfpg.h" for more general info.
//--------------------------------------------------------------------------------------------------

#ifndef YB_BFPG_BFUNC_STANDARD_H_
#define YB_BFPG_BFUNC_STANDARD_H_

#include <string>

#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/uuid.h"

namespace yb {
namespace bfpg {

//--------------------------------------------------------------------------------------------------
// Dummy function for minimum opcode.
inline CHECKED_STATUS NoOp() {
  return Status::OK();
}

// ServerOperator that takes no argument and has no return value.
inline CHECKED_STATUS ServerOperator() {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// ServerOperator that takes 1 argument and has a return value.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ServerOperator(PTypePtr arg1, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// This is not used but implemented as an example for future coding.
// ServerOperator that takes 2 arguments and has a return value.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ServerOperator(PTypePtr arg1, PTypePtr arg2, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

//--------------------------------------------------------------------------------------------------
// "+" and "-".

template<typename PTypePtr, typename RTypePtr>
Status AddI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_double_value(x->double_value() + y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(x->string_value() + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(x->string_value() + std::to_string(y->double_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(std::to_string(x->double_value()) + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SubI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SubDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_double_value(x->double_value() - y->double_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Comparison.
template<typename PTypePtr, typename RTypePtr>
Status Equal(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->set_bool_value(false);
  } else {
    result->set_bool_value(x->value() == y->value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Now().
template<typename RTypePtr>
Status NowTimeUuid(RTypePtr result) {
  uuid_t linux_time_uuid;
  uuid_generate_time(linux_time_uuid);
  Uuid time_uuid(linux_time_uuid);
  CHECK_OK(time_uuid.IsTimeUuid());
  CHECK_OK(time_uuid.HashMACAddress());
  result->set_timeuuid_value(time_uuid);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

} // namespace bfpg
} // namespace yb

#endif  // YB_BFPG_BFUNC_STANDARD_H_
