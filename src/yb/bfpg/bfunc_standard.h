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

#pragma once

#include <string>

#include "yb/bfcommon/bfunc_standard.h"

#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/uuid.h"

namespace yb {
namespace bfpg {

using bfcommon::NoOp;
using bfcommon::ServerOperator;
using bfcommon::AddI64I64;
using bfcommon::AddDoubleDouble;
using bfcommon::AddStringString;
using bfcommon::AddStringDouble;
using bfcommon::AddDoubleString;
using bfcommon::SubI64I64;
using bfcommon::SubDoubleDouble;
using bfcommon::NowTimeUuid;

//--------------------------------------------------------------------------------------------------
// Comparison.
inline Result<BFRetValue> Equal(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  result.set_bool_value(!IsNull(x) && !IsNull(y) && (x == y));
  return result;
}

} // namespace bfpg
} // namespace yb
