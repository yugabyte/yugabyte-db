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
// This module defines the opcodes to instruct tablet-servers on how to operate a request when the
// operations are beyond the scope of this library. For example, this library won't execute an
// aggregate functions, so YQL compiler would compile aggregate functions into SCALL instruction
// (i.e. system call) and sends the instruction to tablet-server to execute.
//
// Example: SELECT AVG(col) FROM tab;
// - Client generates a YQLSCallPB to represent a server-call or system-call.
//     message YQLSCallPB {
//       optional int32 sopcode = 1;
//       repeated YQLExpressionPB operands = 2;
//     }
// - Server uses the provided sopcode to process the request appropriately.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <stdint.h>

namespace yb {
namespace bfql {

enum class TSOpcode : int32_t {
  kNoOp = 0,

  kWriteTime,
  kTtl,

  kAvg,
  kCount,
  kMax,
  kMin,
  kSum,

  kScalarInsert,
  kMapExtend,
  kMapRemove,
  kSetExtend,
  kSetRemove,
  kListAppend,
  kListPrepend,
  kListRemove,

  kToJson,
};

bool IsAggregateOpcode(TSOpcode op);

} // namespace bfql
} // namespace yb
