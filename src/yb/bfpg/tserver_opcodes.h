//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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

#include "yb/util/enums.h"

namespace yb {
namespace bfpg {

YB_DEFINE_ENUM(
  TSOpcode,
  ((kNoOp, 0))

  (kWriteTime)
  (kTtl)

  (kAvg)
  (kCount)
  (kMax)
  (kMin)
  (kSumInt8)
  (kSumInt16)
  (kSumInt32)
  (kSumInt64)
  (kSumFloat)
  (kSumDouble)

  (kScalarInsert)
  (kMapExtend)
  (kMapRemove)
  (kSetExtend)
  (kSetRemove)
  (kListAppend)
  (kListPrepend)
  (kListRemove)

  // Serialized YSQL/PG Expr Node.
  (kPgEvalExprCall)
);

bool IsAggregateOpcode(TSOpcode op);

} // namespace bfpg
} // namespace yb
