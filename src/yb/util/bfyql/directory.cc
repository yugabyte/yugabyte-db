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

#include "yb/util/bfyql/directory.h"

namespace yb {
namespace bfyql {

using std::initializer_list;
using std::vector;

const DataType VOID = DataType::NULL_VALUE_TYPE;
const DataType INT8 = DataType::INT8;
const DataType INT16 = DataType::INT16;
const DataType INT32 = DataType::INT32;
const DataType INT64 = DataType::INT64;
const DataType STRING = DataType::STRING;
// Unused currently.
// const DataType BOOL = DataType::BOOL;
const DataType FLOAT = DataType::FLOAT;
const DataType DOUBLE = DataType::DOUBLE;
// Unused currently.
// const DataType BINARY = DataType::BINARY;
// Unused currently.
// const DataType TIMESTAMP = DataType::TIMESTAMP;
const DataType TYPEARGS = DataType::TYPEARGS;

// IMPORTANT NOTES:
// - If your cpp_function is defined in a different name space, you must "#include" its header file
//   in "bfyql/directory.h" and enter the function full name here (such as XX::YY::func_name).
// - All yql function names in this table MUST be in lower case.
// - New entry must be added at the end for backward compatibility reason. We use the order of
//   this table to generate OPCODE, so changing the order might end up loading wrong operator for
//   a given OPCODE when connecting with server of older release version.
// - See "directory.h" header file for more explanation.
//
// Left to right: cpp_name, yql_name, return_type, argument_types.
const vector<BFDecl> kBFDirectory = {
  // Add this no op entry at the begining just in case we need a MIN value for operators.
  { "NoOp", "NO_OP", VOID, {} },

  // Numeric conversion.
  { "ConvertI8ToI16", "cast", INT16, {INT8, INT16} },
  { "ConvertI8ToI32", "cast", INT32, {INT8, INT32} },
  { "ConvertI8ToI64", "cast", INT64, {INT8, INT64} },
  { "ConvertI8ToFloat", "cast", FLOAT, {INT8, FLOAT} },
  { "ConvertI8ToDouble", "cast", DOUBLE, {INT8, DOUBLE} },

  { "ConvertI16ToI8", "cast", INT8, {INT16, INT8} },
  { "ConvertI16ToI32", "cast", INT32, {INT16, INT32} },
  { "ConvertI16ToI64", "cast", INT64, {INT16, INT64} },
  { "ConvertI16ToFloat", "cast", FLOAT, {INT16, FLOAT} },
  { "ConvertI16ToDouble", "cast", DOUBLE, {INT16, DOUBLE} },

  { "ConvertI32ToI8", "cast", INT8, {INT32, INT8} },
  { "ConvertI32ToI16", "cast", INT16, {INT32, INT16} },
  { "ConvertI32ToI64", "cast", INT64, {INT32, INT64} },
  { "ConvertI32ToFloat", "cast", FLOAT, {INT32, FLOAT} },
  { "ConvertI32ToDouble", "cast", DOUBLE, {INT32, DOUBLE} },

  { "ConvertI64ToI8", "cast", INT8, {INT64, INT8} },
  { "ConvertI64ToI16", "cast", INT16, {INT64, INT16} },
  { "ConvertI64ToI32", "cast", INT32, {INT64, INT32} },
  { "ConvertI64ToFloat", "cast", FLOAT, {INT64, FLOAT} },
  { "ConvertI64ToDouble", "cast", DOUBLE, {INT64, DOUBLE} },

  { "ConvertFloatToDouble", "cast", DOUBLE, {FLOAT, DOUBLE} },
  { "ConvertDoubleToFloat", "cast", FLOAT, {DOUBLE, FLOAT} },

  // Token().
  { "Token", "token", INT64, {TYPEARGS} },

  // Counter functions.
  { "IncCounter", "+counter", INT64,  {INT64, INT64} },
  { "DecCounter", "-counter", INT64,  {INT64, INT64} },

  // "+".
  { "AddI64I64", "+", INT64,  {INT64, INT64} },
  { "AddDoubleDouble", "+", DOUBLE, {DOUBLE, DOUBLE} },
  { "AddStringString", "+", STRING, {STRING, STRING} },
  { "AddStringDouble", "+", STRING, {STRING, DOUBLE} },
  { "AddDoubleString", "+", STRING, {DOUBLE, STRING} },
  { "AddMapMap", "+", MAP, {MAP, MAP} },
  { "AddMapSet", "+", MAP, {MAP, SET} }, // only needed to allow adding empty set '{}' to a Map
  { "AddSetSet", "+", SET, {SET, SET} },
  { "AddListList", "+", LIST, {LIST, LIST} },

  // "-".
  { "SubI64I64", "-", INT64,  {INT64, INT64} },
  { "SubDoubleDouble", "-", DOUBLE, {DOUBLE, DOUBLE} },
  { "SubMapSet", "-", MAP, {MAP, SET} },
  { "SubSetSet", "-", SET, {SET, SET} },
  { "SubListList", "-", LIST, {LIST, LIST} },

  // SELECT ... WHERE ttl(int_column) > 5;
  // void TTL(int x)
  { "ttl", "ttl", INT64, {INT32}},

  // SELECT ... WHERE writetime(int_column) > 5;
  // void TTL(int x)
  { "writetime", "writetime", INT64, {INT32}},
};

} // namespace bfyql
} // namespace yb
