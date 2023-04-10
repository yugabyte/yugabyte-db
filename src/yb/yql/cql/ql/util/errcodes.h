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
// SQL error codes.
// - All error code must be < ErrorCode::SUCCESS
//   Compilation or execution should eventually stop after an error is raised.
// - All warning code must be > ErrorCode::SUCCESS
//   Compilation and execution should continue after a warning is raised.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/util/status_ec.h"

// Return an unauthorized error if authentication is not enabled through the flag
// use_cassandra_authentication.
#define RETURN_NOT_AUTH_ENABLED(s)  do { \
  if (!FLAGS_use_cassandra_authentication) {                                                      \
    return s->Error(this, "You have to be logged in and not anonymous to perform this request",   \
                    ErrorCode::UNAUTHORIZED);                                                     \
  }                                                                                               \
} while (false);

namespace yb {
namespace ql {

enum class ErrorCode : int64_t {
  //------------------------------------------------------------------------------------------------
  // All error codes < SUCCESS
  // All warning codes > SUCCESS
  SUCCESS = 0,

  //------------------------------------------------------------------------------------------------
  // Warning. [1, +)
  WARNING = 1,
  NOTFOUND = 100,

  //------------------------------------------------------------------------------------------------
  // System Error [-1, -9)
  // Failure with no specific reason. Most likely, this is used for a system or coding error.
  FAILURE = -1,
  SERVER_ERROR = -2,
  STALE_METADATA = -3,
  UNAUTHORIZED  = -4,

  //------------------------------------------------------------------------------------------------
  // Limitation errors [-10, -50).
  LIMITATION_ERROR = -10,

  // SQL specific error codes. CQL might not support certain SQL syntax and vice versa.
  SQL_STATEMENT_INVALID = -11,
  // CQL specific error codes. CQL might not support certain SQL syntax and vice versa.
  CQL_STATEMENT_INVALID = -12,

  FEATURE_NOT_YET_IMPLEMENTED = -13,
  FEATURE_NOT_SUPPORTED = -14,

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
  INVALID_COLUMN_DEFINITION = -102,

  //------------------------------------------------------------------------------------------------
  // Semantic errors [-200, -300).
  SEM_ERROR = -200,
  DATATYPE_MISMATCH = -201,
  DUPLICATE_OBJECT = -202,
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
  INVALID_FUNCTION_CALL = -214,
  NO_NAMESPACE_USED = -215,
  INSERT_TABLE_OF_COUNTERS = -216,
  INVALID_COUNTING_EXPR = -217,
  DUPLICATE_TYPE = -218,
  DUPLICATE_TYPE_FIELD = -219,
  ALTER_KEY_COLUMN = -220,
  // reserved code: INCOMPATIBLE_COPARTITION_SCHEMA = -221
  INVALID_ROLE_DEFINITION = -222,
  DUPLICATE_ROLE = -223,
  NULL_IN_COLLECTIONS = -224,
  DUPLICATE_UPDATE_PROPERTY = -225,
  INVALID_UPDATE_PROPERTY = -226,

  //------------------------------------------------------------------------------------------------
  // Execution errors [-300, x).
  EXEC_ERROR = -300,
  OBJECT_NOT_FOUND = -301,
  INVALID_TABLE_DEFINITION = -302,
  WRONG_METADATA_VERSION = -303,
  INVALID_ARGUMENTS = -304,
  TOO_FEW_ARGUMENTS = -305,
  TOO_MANY_ARGUMENTS = -306,
  KEYSPACE_ALREADY_EXISTS = -307,
  KEYSPACE_NOT_FOUND = -308,
  TABLET_NOT_FOUND = -309,
  UNPREPARED_STATEMENT = -310,
  TYPE_NOT_FOUND = -311,
  INVALID_TYPE_DEFINITION = -312,
  RESTART_REQUIRED = -313,
  ROLE_NOT_FOUND = -314,
  RESOURCE_NOT_FOUND = -315,
  INVALID_REQUEST = -316,
  PERMISSION_NOT_FOUND = -317,
  CONDITION_NOT_SATISFIED = -318,

};

// Return SQL error code from an Status if it is a SQL error. Otherwise, return FAILURE.
ErrorCode GetErrorCode(const Status& s);

// Mapping errcode to text messages.
const char *ErrorText(ErrorCode code);

// Return an error status with the error code.
Status ErrorStatus(ErrorCode code, const std::string& mesg = "");

// Font for error message is RED.
constexpr const char *kErrorFontStart = "\033[31m";
constexpr const char *kErrorFontEnd = "\033[0m";

std::string FormatForComparisonFailureMessage(ErrorCode op, ErrorCode other);

struct QLErrorTag : IntegralErrorTag<ErrorCode> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 4;

  static std::string ToMessage(Value code) {
    return ErrorText(code);
  }
};

typedef StatusErrorCodeImpl<QLErrorTag> QLError;

}  // namespace ql
}  // namespace yb
