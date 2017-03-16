//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
// SQL error codes.
// - All error code must be < ErrorCode::SUCCESS
//   Compilation or execution should eventually stop after an error is raised.
// - All warning code must be > ErrorCode::SUCCESS
//   Compilation and execution should continue after a warning is raised.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_ERRCODES_H_
#define YB_SQL_UTIL_ERRCODES_H_

#include <cstdint>

namespace yb {
namespace sql {

enum class ErrorCode : int64_t {
  // Implementation related errors [-1, -50).
  // Failure with no specific reason. Most likely, this is used for a system or coding error.
  FAILURE = -1,
  // SQL specific error codes. CQL might not support certain SQL syntax and vice versa.
  SQL_STATEMENT_INVALID = -2,
  // CQL specific error codes. CQL might not support certain SQL syntax and vice versa.
  CQL_STATEMENT_INVALID = -3,

  FEATURE_NOT_YET_IMPLEMENTED = -10,
  FEATURE_NOT_SUPPORTED = -11,

  //------------------------------------------------------------------------------------------------
  // Lexical errors [-50, -100).
  LEXICAL_ERROR = -50,
  CHARACTER_NOT_IN_REPERTOIRE = -51,
  INVALID_ESCAPE_SEQUENCE = -52,
  NAME_TOO_LONG = -53,
  NONSTANDARD_USE_OF_ESCAPE_CHARACTER = -55,

  //------------------------------------------------------------------------------------------------
  // Syntax errors [-100, -200).
  SYNTAX_ERROR = -100,
  INVALID_PARAMETER_VALUE = -101,

  //------------------------------------------------------------------------------------------------
  // Semantic errors [-200, -300).
  SEM_ERROR = -200,
  DATATYPE_MISMATCH = -201,
  DUPLICATE_TABLE = -202,
  UNDEFINED_COLUMN = -203,
  DUPLICATE_COLUMN = -204,
  MISSING_PRIMARY_KEY = -205,
  INVALID_PRIMARY_COLUMN_TYPE = -206,
  MISSING_ARGUMENT_FOR_PRIMARY_KEY = -207,
  NULL_ARGUMENT_FOR_PRIMARY_KEY = -208,
  INCOMPARABLE_DATATYPES = -209,
  INVALID_TABLE_PROPERTY = -210,
  DUPLICATE_TABLE_PROPERTY = -211,
  INVALID_DATATYPE = -212,
  SYSTEM_NAMESPACE_READONLY = -213,

  //------------------------------------------------------------------------------------------------
  // Execution errors [-300, x).
  EXEC_ERROR = -300,
  TABLE_NOT_FOUND = -301,
  INVALID_TABLE_DEFINITION = -302,
  WRONG_METADATA_VERSION = -303,
  INVALID_ARGUMENTS = -304,
  TOO_FEW_ARGUMENTS = -305,
  TOO_MANY_ARGUMENTS = -306,
  KEYSPACE_ALREADY_EXISTS = -307,
  KEYSPACE_NOT_FOUND = -308,
  TABLET_NOT_FOUND = -309,

  //------------------------------------------------------------------------------------------------
  // All error codes < SUCCESS
  // All warning codes > SUCCESS
  SUCCESS = 0,

  //------------------------------------------------------------------------------------------------
  // Warning. Start with 100.
  NOTFOUND = 100,
};

// Mapping errcode to text messages.
const char *ErrorText(ErrorCode code);

// Font for error message is RED.
constexpr const char *kErrorFontStart = "\033[31m";
constexpr const char *kErrorFontEnd = "\033[0m";

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_UTIL_ERRCODES_H_
