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
// Convert internal error code into readable texts. This text doesn't have to be English, and this
// file can be translated into any languages that YugaByte supports.
//--------------------------------------------------------------------------------------------------
#include "yb/yql/cql/ql/util/errcodes.h"

#include "yb/common/ql_protocol.pb.h"
#include "yb/util/enums.h"

namespace yb {
namespace ql {

const std::unordered_map<ErrorCode, const char*, EnumHash> kQLErrorMessage {
  //------------------------------------------------------------------------------------------------
  { ErrorCode::SUCCESS, "Success" },

  //------------------------------------------------------------------------------------------------
  // Warning. Start with 100.
  { ErrorCode::WARNING, "Warning" },
  { ErrorCode::NOTFOUND, "Not Found" },

  //------------------------------------------------------------------------------------------------
  // System errors [-1, -9).
  { ErrorCode::FAILURE, "Internal Failure" },
  { ErrorCode::SERVER_ERROR, "Server Error" },
  { ErrorCode::STALE_METADATA, "Stale Metadata" },
  { ErrorCode::UNAUTHORIZED, "Unauthorized"},

  //------------------------------------------------------------------------------------------------
  // Limitation related errors [-1, -50).
  { ErrorCode::LIMITATION_ERROR, "Implementation Limitation" },
  { ErrorCode::SQL_STATEMENT_INVALID, "Invalid SQL Statement" },
  { ErrorCode::CQL_STATEMENT_INVALID, "Invalid CQL Statement" },
  { ErrorCode::FEATURE_NOT_YET_IMPLEMENTED, "Feature Not Yet Implemented" },
  { ErrorCode::FEATURE_NOT_SUPPORTED, "Feature Not Supported" },

  //------------------------------------------------------------------------------------------------
  // Lexical errors [-50, -100).
  { ErrorCode::LEXICAL_ERROR, "Lexical Error" },
  { ErrorCode::CHARACTER_NOT_IN_REPERTOIRE, "Character Not in Repertoire" },
  { ErrorCode::INVALID_ESCAPE_SEQUENCE, "Invalid Escape Sequence" },
  { ErrorCode::NAME_TOO_LONG, "Name Too Long" },
  { ErrorCode::NONSTANDARD_USE_OF_ESCAPE_CHARACTER, "Nonstandard Use of Escape Character" },

  //------------------------------------------------------------------------------------------------
  // Syntax errors [-100, -200).
  { ErrorCode::SYNTAX_ERROR, "Syntax Error" },
  { ErrorCode::INVALID_PARAMETER_VALUE, "Invalid Parameter Value" },
  { ErrorCode::INVALID_COLUMN_DEFINITION, "Invalid Column Definition" },

  //------------------------------------------------------------------------------------------------
  // Semantic errors [-200, -300).
  { ErrorCode::SEM_ERROR, "Semantic Error" },
  { ErrorCode::DATATYPE_MISMATCH, "Datatype Mismatch" },
  { ErrorCode::DUPLICATE_OBJECT, "Duplicate Object" },
  { ErrorCode::UNDEFINED_COLUMN, "Undefined Column" },
  { ErrorCode::DUPLICATE_COLUMN, "Duplicate Column" },
  { ErrorCode::MISSING_PRIMARY_KEY, "Missing Primary Key" },
  { ErrorCode::INVALID_PRIMARY_COLUMN_TYPE, "Invalid Primary Key Column Datatype" },
  { ErrorCode::MISSING_ARGUMENT_FOR_PRIMARY_KEY, "Missing Argument for Primary Key" },
  { ErrorCode::NULL_ARGUMENT_FOR_PRIMARY_KEY, "Null Argument for Primary Key" },
  { ErrorCode::INCOMPARABLE_DATATYPES, "Incomparable Datatypes" },
  { ErrorCode::INVALID_TABLE_PROPERTY, "Invalid Table Property" },
  { ErrorCode::DUPLICATE_TABLE_PROPERTY, "Duplicate Table Property" },
  { ErrorCode::INVALID_DATATYPE, "Invalid Datatype" },
  { ErrorCode::SYSTEM_NAMESPACE_READONLY, "System Namespace is Read-Only" },
  { ErrorCode::INVALID_FUNCTION_CALL, "Invalid Function Call" },
  { ErrorCode::NO_NAMESPACE_USED, "No Namespace Used" },
  { ErrorCode::INSERT_TABLE_OF_COUNTERS, "Insert into table of counters not allowed" },
  { ErrorCode::INVALID_COUNTING_EXPR, "Counters can only be incremented or decremented" },
  { ErrorCode::DUPLICATE_TYPE, "Duplicate Type" },
  { ErrorCode::DUPLICATE_TYPE_FIELD, "Duplicate Type Field" },
  { ErrorCode::ALTER_KEY_COLUMN, "Alter key column" },
  { ErrorCode::INVALID_ROLE_DEFINITION, "Invalid Role Definition" },
  { ErrorCode::DUPLICATE_ROLE, "Duplicate Role"},
  { ErrorCode::NULL_IN_COLLECTIONS, "null is not supported inside collections"},
  { ErrorCode::INVALID_UPDATE_PROPERTY, "Invalid Update Property" },
  { ErrorCode::DUPLICATE_UPDATE_PROPERTY, "Duplicate Update Property" },

  //------------------------------------------------------------------------------------------------
  // Execution errors [-300, x).
  { ErrorCode::EXEC_ERROR, "Execution Error" },
  { ErrorCode::OBJECT_NOT_FOUND, "Object Not Found" },
  { ErrorCode::INVALID_TABLE_DEFINITION, "Invalid Table Definition" },
  { ErrorCode::WRONG_METADATA_VERSION, "Wrong Metadata Version" },
  { ErrorCode::INVALID_ARGUMENTS, "Invalid Arguments" },
  { ErrorCode::TOO_FEW_ARGUMENTS, "Too Few Arguments" },
  { ErrorCode::TOO_MANY_ARGUMENTS, "Too Many Arguments" },
  { ErrorCode::KEYSPACE_ALREADY_EXISTS, "Keyspace Already Exists" },
  { ErrorCode::KEYSPACE_NOT_FOUND, "Keyspace Not Found" },
  { ErrorCode::TABLET_NOT_FOUND, "Tablet Not Found" },
  { ErrorCode::UNPREPARED_STATEMENT, "Unprepared Statement" },
  { ErrorCode::TYPE_NOT_FOUND, "Type Not Found" },
  { ErrorCode::INVALID_TYPE_DEFINITION, "Invalid Type Definition" },
  { ErrorCode::RESTART_REQUIRED, "Restart Required" },
  { ErrorCode::ROLE_NOT_FOUND, "Role Not Found"},
  { ErrorCode::RESOURCE_NOT_FOUND, "Resource Not Found"},
  { ErrorCode::INVALID_REQUEST, "Invalid Request"},
  { ErrorCode::PERMISSION_NOT_FOUND, "Permission Not Found"},
  { ErrorCode::CONDITION_NOT_SATISFIED, "Condition Not Satisfied"},
};

ErrorCode GetErrorCode(const Status& s) {
  QLError ql_error(s);
  return ql_error != ErrorCode::SUCCESS ? ql_error.value() : ErrorCode::FAILURE;
}

const char *ErrorText(const ErrorCode error_code) {
  auto it = kQLErrorMessage.find(error_code);
  if (it == kQLErrorMessage.end()) {
    LOG(DFATAL) << "Unknown error code: " << to_underlying(error_code);
    return "Unknown error";
  }
  return it->second;
}

Status ErrorStatus(const ErrorCode code, const std::string& mesg) {
  return STATUS(QLError, ErrorText(code), mesg, QLError(code));
}

std::string FormatForComparisonFailureMessage(ErrorCode op, ErrorCode) {
  return ErrorText(op);
}

ErrorCode QLStatusToErrorCode(QLResponsePB::QLStatus status) {
  switch (status) {
    case QLResponsePB::YQL_STATUS_OK:
      return ErrorCode::SUCCESS;
    case QLResponsePB::YQL_STATUS_SCHEMA_VERSION_MISMATCH:
      return ErrorCode::WRONG_METADATA_VERSION;
    case QLResponsePB::YQL_STATUS_RUNTIME_ERROR:
      return ErrorCode::SERVER_ERROR;
    case QLResponsePB::YQL_STATUS_USAGE_ERROR:
      return ErrorCode::EXEC_ERROR;
    case QLResponsePB::YQL_STATUS_RESTART_REQUIRED_ERROR:
      return ErrorCode::RESTART_REQUIRED;
    case QLResponsePB::YQL_STATUS_QUERY_ERROR:
      return ErrorCode::EXEC_ERROR;
  }
  FATAL_INVALID_ENUM_VALUE(QLResponsePB::QLStatus, status);
}

static const std::string kQLErrorCategoryName = "ql error";

static StatusCategoryRegisterer ql_error_category_registerer(
    StatusCategoryDescription::Make<QLErrorTag>(&kQLErrorCategoryName));

}  // namespace ql
}  // namespace yb
