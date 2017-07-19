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

#ifndef YB_UTIL_BFQL_TSERVER_OPCODES_H_
#define YB_UTIL_BFQL_TSERVER_OPCODES_H_

namespace yb {
namespace bfql {

enum class TSOpcode : int32_t {
  kNoOp = 0,
  kWriteTime,
  kTtl,
  kCount,
  kSum,
  kAvg,
  kMin,
  kMax,
};

} // namespace bfql
} // namespace yb

#endif  // YB_UTIL_BFQL_TSERVER_OPCODES_H_
