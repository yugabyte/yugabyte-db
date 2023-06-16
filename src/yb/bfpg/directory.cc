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
//--------------------------------------------------------------------------------------------------

#include "yb/bfpg/directory.h"

#include "yb/common/value.messages.h"

namespace yb {
namespace bfpg {

using std::vector;

constexpr DataType VOID = DataType::UNKNOWN_DATA;
constexpr DataType ANYTYPE = DataType::NULL_VALUE_TYPE;
using DataType::INT8;
using DataType::INT16;
using DataType::INT32;
using DataType::INT64;
using DataType::STRING;
using DataType::BOOL;
using DataType::FLOAT;
using DataType::DOUBLE;
using DataType::TIMESTAMP;
using DataType::DECIMAL;
using DataType::VARINT;
// const DataType TYPEARGS = DataType::TYPEARGS;
using DataType::DATE;
using DataType::TIME;
using DataType::TIMEUUID;

// IMPORTANT NOTES:
// - If your cpp_function is defined in a different name space, you must "#include" its header file
//   in "bfpg/directory.h" and enter the function full name here (such as XX::YY::func_name).
// - All yql function names in this table MUST be in lower case.
// - New entry must be added at the end for backward compatibility reason. We use the order of
//   this table to generate OPCODE, so changing the order might end up loading wrong operator for
//   a given OPCODE when connecting with server of older release version.
// - See "directory.h" header file for more explanation.
//
// Left to right: cpp_name, ql_name, return_type, argument_types.
const vector<BFDecl> kBFDirectory = {
  // Add this no op entry at the begining just in case we need a MIN value for operators.
  { "NoOp", "NO_OP", VOID, {} },

  //------------------------------------------------------------------------------------------------
  // Conversion routines.
  // - The following code block defines the first set of primitive conversion routines.
  // - Do not add newly-introduced conversion routines here. Because the order of this table is
  //   used to generate OPCODE enum, whose values must be the same for the lifetime of YugaByte
  //   for compatibility reasons, all new entries must be added at the end.

  // Numeric conversion.
  { "ConvertI8ToI16",       "cast", INT16,  {INT8, INT16} },
  { "ConvertI8ToI32",       "cast", INT32,  {INT8, INT32} },
  { "ConvertI8ToI64",       "cast", INT64,  {INT8, INT64} },
  { "ConvertI8ToFloat",     "cast", FLOAT,  {INT8, FLOAT} },
  { "ConvertI8ToDouble",    "cast", DOUBLE, {INT8, DOUBLE} },

  { "ConvertI16ToI8",       "cast", INT8,   {INT16, INT8} },
  { "ConvertI16ToI32",      "cast", INT32,  {INT16, INT32} },
  { "ConvertI16ToI64",      "cast", INT64,  {INT16, INT64} },
  { "ConvertI16ToFloat",    "cast", FLOAT,  {INT16, FLOAT} },
  { "ConvertI16ToDouble",   "cast", DOUBLE, {INT16, DOUBLE} },

  { "ConvertI32ToI8",       "cast", INT8,   {INT32, INT8} },
  { "ConvertI32ToI16",      "cast", INT16,  {INT32, INT16} },
  { "ConvertI32ToI64",      "cast", INT64,  {INT32, INT64} },
  { "ConvertI32ToFloat",    "cast", FLOAT,  {INT32, FLOAT} },
  { "ConvertI32ToDouble",   "cast", DOUBLE, {INT32, DOUBLE} },

  { "ConvertI64ToI8",       "cast", INT8,   {INT64, INT8} },
  { "ConvertI64ToI16",      "cast", INT16,  {INT64, INT16} },
  { "ConvertI64ToI32",      "cast", INT32,  {INT64, INT32} },
  { "ConvertI64ToFloat",    "cast", FLOAT,  {INT64, FLOAT} },
  { "ConvertI64ToDouble",   "cast", DOUBLE, {INT64, DOUBLE} },

  { "ConvertDoubleToFloat", "cast", FLOAT,  {DOUBLE, FLOAT} },
  { "ConvertFloatToDouble", "cast", DOUBLE, {FLOAT, DOUBLE} },

  { "ConvertTimestampToDate", "todate", DATE, {TIMESTAMP}, TSOpcode::kNoOp, false },
  { "ConvertTimestampToTime", "totime", TIME, {TIMESTAMP}, TSOpcode::kNoOp, false },
  { "ConvertDateToTimestamp", "totimestamp", TIMESTAMP, {DATE}, TSOpcode::kNoOp, false },
  { "ConvertDateToUnixTimestamp", "tounixtimestamp", INT64, {DATE}, TSOpcode::kNoOp, false },
  { "ConvertTimestampToUnixTimestamp", "tounixtimestamp", INT64, {TIMESTAMP}, TSOpcode::kNoOp,
    false },

  //------------------------------------------------------------------------------------------------
  // Aggregate functions.
  // - Have TSERVER_OPCODE to instruct tablet server how to execute these calls.
  // - SUM and AVG only take numeric arguments.
  // - MIN and MAX can take arguments of any types.
  { "ServerOperator", "count", INT64, {ANYTYPE}, TSOpcode::kCount },

  // Cassandra behavior: SUM() has exactly the same datatype as the input argument's type.
  { "ServerOperator", "sum", INT64, {INT8}, TSOpcode::kSumInt8 },
  { "ServerOperator", "sum", INT64, {INT16}, TSOpcode::kSumInt16 },
  { "ServerOperator", "sum", INT64, {INT32}, TSOpcode::kSumInt32 },
  { "ServerOperator", "sum", INT64, {INT64}, TSOpcode::kSumInt64 },
  { "ServerOperator", "sum", FLOAT, {FLOAT}, TSOpcode::kSumFloat },
  { "ServerOperator", "sum", DOUBLE, {DOUBLE}, TSOpcode::kSumDouble },

  // Cassandra behavior: AVG() has exactly the same datatype as the input argument's type.
  { "ServerOperator", "avg", INT8, {INT8}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", INT16, {INT16}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", INT32, {INT32}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", INT64, {INT64}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", FLOAT, {FLOAT}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", DOUBLE, {DOUBLE}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", VARINT, {VARINT}, TSOpcode::kAvg, false },
  { "ServerOperator", "avg", DECIMAL, {DECIMAL}, TSOpcode::kAvg, false },

  { "ServerOperator", "min", ANYTYPE, {ANYTYPE}, TSOpcode::kMin },
  { "ServerOperator", "max", ANYTYPE, {ANYTYPE}, TSOpcode::kMax },

  { "ConvertVarintToI8",       "cast", INT8,   {VARINT, INT8} },
  { "ConvertVarintToI16",      "cast", INT16,  {VARINT, INT16} },
  { "ConvertVarintToI32",      "cast", INT32,  {VARINT, INT32} },
  { "ConvertVarintToI64",      "cast", INT64,  {VARINT, INT64} },
  { "ConvertVarintToFloat",    "cast", FLOAT,  {VARINT, FLOAT} },
  { "ConvertVarintToDouble",   "cast", DOUBLE, {VARINT, DOUBLE} },

  // Uuid and timeuuid functions.
  { "NowTimeUuid", "now", TIMEUUID, {} },

  //------------------------------------------------------------------------------------------------
  // PGSQL standard functions.
  // "+".
  { "AddI64I64", "+", INT64, {INT64, INT64} },
  { "AddDoubleDouble", "+", DOUBLE, {DOUBLE, DOUBLE} },
  { "AddStringString", "+", STRING, {STRING, STRING} },
  { "AddStringDouble", "+", STRING, {STRING, DOUBLE} },
  { "AddDoubleString", "+", STRING, {DOUBLE, STRING} },

  // "-".
  { "SubI64I64", "-", INT64, {INT64, INT64} },
  { "SubDoubleDouble", "-", DOUBLE, {DOUBLE, DOUBLE} },

  // "=="
  { "Equal", "==", BOOL, {ANYTYPE, ANYTYPE} },
};

} // namespace bfpg
} // namespace yb
