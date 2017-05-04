//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
  { "ConvertI8ToI16", "cast", VOID, {INT8, INT16} },
  { "ConvertI8ToI32", "cast", VOID, {INT8, INT32} },
  { "ConvertI8ToI64", "cast", VOID, {INT8, INT64} },
  { "ConvertI8ToFloat", "cast", VOID, {INT8, FLOAT} },
  { "ConvertI8ToDouble", "cast", VOID, {INT8, DOUBLE} },

  { "ConvertI16ToI8", "cast", VOID, {INT16, INT16} },
  { "ConvertI16ToI32", "cast", VOID, {INT16, INT32} },
  { "ConvertI16ToI64", "cast", VOID, {INT16, INT64} },
  { "ConvertI16ToFloat", "cast", VOID, {INT16, FLOAT} },
  { "ConvertI16ToDouble", "cast", VOID, {INT16, DOUBLE} },

  { "ConvertI32ToI8", "cast", VOID, {INT32, INT16} },
  { "ConvertI32ToI16", "cast", VOID, {INT32, INT16} },
  { "ConvertI32ToI64", "cast", VOID, {INT32, INT64} },
  { "ConvertI32ToFloat", "cast", VOID, {INT32, FLOAT} },
  { "ConvertI32ToDouble", "cast", VOID, {INT32, DOUBLE} },

  { "ConvertI64ToI8", "cast", VOID, {INT64, INT8} },
  { "ConvertI64ToI16", "cast", VOID, {INT64, INT16} },
  { "ConvertI64ToI32", "cast", VOID, {INT64, INT32} },
  { "ConvertI64ToFloat", "cast", VOID, {INT64, FLOAT} },
  { "ConvertI64ToDouble", "cast", VOID, {INT64, DOUBLE} },

  { "ConvertFloatToDouble", "cast", VOID, {FLOAT, DOUBLE} },
  { "ConvertDoubleToFloat", "cast", VOID, {DOUBLE, FLOAT} },

  // Token().
  { "Token", "token", INT64, {TYPEARGS} },

  // "+".
  { "AddI64I64", "+", INT64,  {INT64, INT64} },
  { "AddDoubleDouble", "+", DOUBLE, {DOUBLE, DOUBLE} },
  { "AddStringString", "+", STRING, {STRING, STRING} },
  { "AddStringDouble", "+", STRING, {STRING, DOUBLE} },
  { "AddDoubleString", "+", STRING, {DOUBLE, STRING} },
};

} // namespace bfyql
} // namespace yb
