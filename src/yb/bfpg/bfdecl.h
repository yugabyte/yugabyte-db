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
// A builtin function is specified in PGSQL but implemented in C++, and this module represents the
// metadata of a builtin function.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/bfcommon/bfdecl.h"
#include "yb/bfpg/tserver_opcodes.h"

#include "yb/common/value.fwd.h"
#include "yb/common/value.pb.h"

#include "yb/gutil/macros.h"

#include "yb/util/memory/arena_fwd.h"

namespace yb {
namespace bfpg {

struct BFDeclTraits {
  static bool is_collection_op(TSOpcode tsopcode) {
    switch (tsopcode) {
      case TSOpcode::kMapExtend: FALLTHROUGH_INTENDED;
      case TSOpcode::kMapRemove: FALLTHROUGH_INTENDED;
      case TSOpcode::kSetExtend: FALLTHROUGH_INTENDED;
      case TSOpcode::kSetRemove: FALLTHROUGH_INTENDED;
      case TSOpcode::kListAppend: FALLTHROUGH_INTENDED;
      case TSOpcode::kListPrepend: FALLTHROUGH_INTENDED;
      case TSOpcode::kListRemove:
        return true;
      default:
        return false;
    }
  }

  static bool is_aggregate_op(TSOpcode tsopcode) {
    switch (tsopcode) {
      case TSOpcode::kAvg: FALLTHROUGH_INTENDED;
      case TSOpcode::kCount: FALLTHROUGH_INTENDED;
      case TSOpcode::kMax: FALLTHROUGH_INTENDED;
      case TSOpcode::kMin: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt8: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt16: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt32: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumInt64: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumFloat: FALLTHROUGH_INTENDED;
      case TSOpcode::kSumDouble:
        return true;
      default:
        return false;
    }
  }
};

using BFDecl = bfcommon::BFDecl<TSOpcode, BFDeclTraits>;

using bfcommon::BFValue;
using bfcommon::BFRetValue;
using bfcommon::BFParam;
using bfcommon::BFParams;
using bfcommon::BFFunctions;
using bfcommon::BFFactory;

} // namespace bfpg
} // namespace yb
