//--------------------------------------------------------------------------------------------------
// errcodes.txt
//      PostgreSQL error codes
//
// Portions Copyright (c) YugaByte, Inc.
// Portions Copyright (c) 2003-2015, PostgreSQL Global Development Group
//
// This list serves as the basis for generating source files containing error
// codes. It is kept in a common format to make sure all these source files have
// the same contents.
// The files generated from this one are:
//
//   src/include/utils/errcodes.h
//      macros defining errcode constants to be used in the rest of the source
//
//   src/pl/plpgsql/src/plerrcodes.h
//      a list of PL/pgSQL condition names and their SQLSTATE codes
//
//   doc/src/sgml/errcodes-list.sgml
//      a SGML table of error codes for inclusion in the documentation
//
// The format of this file is one error code per line, with the following
// whitespace-separated fields:
//
//      sqlstate    E/W/S    errcode_macro_name    spec_name
//
// where sqlstate is a five-character string following the SQLSTATE conventions,
// the second field indicates if the code means an error, a warning or success,
// errcode_macro_name is the C macro name starting with ERRCODE that will be put
// in errcodes.h, and spec_name is a lowercase, underscore-separated name that
// will be used as the PL/pgSQL condition name and will also be included in the
// SGML list. The last field is optional, if not present the PL/pgSQL condition
// and the SGML entry will not be generated.
//
// Empty lines and lines starting with a hash are comments.
//
// There are also special lines in the format of:
//
//      Section: section description
//
// that is, lines starting with the string "Section:". They are used to delimit
// error classes as defined in the SQL spec, and are necessary for SGML output.
//
//
//      SQLSTATE codes for errors.
//
// The SQL99 code set is rather impoverished, especially in the area of
// syntactical and semantic errors.  We have borrowed codes from IBM's DB2
// and invented our own codes to develop a useful code set.
//
// When adding a new code, make sure it is placed in the most appropriate
// class (the first two characters of the code value identify the class).
// The listing is organized by class to make this prominent.
//
// Each class should have a generic '000' subclass.  However,
// the generic '000' subclass code should be used for an error only
// when there is not a more-specific subclass code defined.
//
// The SQL spec requires that all the elements of a SQLSTATE code be
// either digits or upper-case ASCII characters.
//
// Classes that begin with 0-4 or A-H are defined by the
// standard. Within such a class, subclass values defined by the
// standard must begin with 0-4 or A-H. To define a new error code,
// ensure that it is either in an "implementation-defined class" (it
// begins with 5-9 or I-Z), or its subclass falls outside the range of
// error codes that could be present in future versions of the
// standard (i.e. the subclass value begins with 5-9 or I-Z).
//
// The convention is that new error codes defined by PostgreSQL in a
// class defined by the standard have a subclass value that begins
// with 'P'. In addition, error codes defined by PostgreSQL clients
// (such as ecpg) have a class value that begins with 'Y'.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_UTIL_ERRCODES_H_
#define YB_SQL_UTIL_ERRCODES_H_

#include <cstdint>

//--------------------------------------------------------------------------------------------------
// NOTE: All errcodes in this modules are defined by PostgreSql.
//--------------------------------------------------------------------------------------------------

namespace yb {
namespace sql {

enum class ErrorCode : int64_t {
  // TODO(neil):
  // All error codes < SUCCESSFUL_COMPLETION
  // All warning codes > SUCCESSFUL_COMPLETION
  SUCCESSFUL_COMPLETION = 0,

  // Error codes. YbSql statement execution would stop after reporting these messages.
  NO_DATA,
  NO_ADDITIONAL_DYNAMIC_RESULT_SETS_RETURNED,
  SQL_STATEMENT_NOT_YET_COMPLETE,
  DDL_EXECUTION_RERUN_NOT_ALLOWED,
  CONNECTION_EXCEPTION,
  CONNECTION_DOES_NOT_EXIST,
  CONNECTION_FAILURE,
  SQLCLIENT_UNABLE_TO_ESTABLISH_SQLCONNECTION,
  SQLSERVER_REJECTED_ESTABLISHMENT_OF_SQLCONNECTION,
  TRANSACTION_RESOLUTION_UNKNOWN,
  PROTOCOL_VIOLATION,
  TRIGGERED_ACTION_EXCEPTION,
  FEATURE_NOT_SUPPORTED,
  FEATURE_NOT_YET_IMPLEMENTED,
  INVALID_TRANSACTION_INITIATION,
  LOCATOR_EXCEPTION,
  L_E_INVALID_SPECIFICATION,
  INVALID_GRANTOR,
  INVALID_GRANT_OPERATION,
  INVALID_ROLE_SPECIFICATION,
  DIAGNOSTICS_EXCEPTION,
  STACKED_DIAGNOSTICS_ACCESSED_WITHOUT_ACTIVE_HANDLER,
  CASE_NOT_FOUND,
  CARDINALITY_VIOLATION,
  DATA_EXCEPTION,
  ARRAY_ELEMENT_ERROR,
  ARRAY_SUBSCRIPT_ERROR,
  CHARACTER_NOT_IN_REPERTOIRE,
  DATETIME_FIELD_OVERFLOW,
  DATETIME_VALUE_OUT_OF_RANGE,
  DIVISION_BY_ZERO,
  ERROR_IN_ASSIGNMENT,
  ESCAPE_CHARACTER_CONFLICT,
  INDICATOR_OVERFLOW,
  INTERVAL_FIELD_OVERFLOW,
  INVALID_ARGUMENT_FOR_LOG,
  INVALID_ARGUMENT_FOR_NTILE,
  INVALID_ARGUMENT_FOR_NTH_VALUE,
  INVALID_ARGUMENT_FOR_POWER_FUNCTION,
  INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION,
  INVALID_CHARACTER_VALUE_FOR_CAST,
  INVALID_DATETIME_FORMAT,
  INVALID_ESCAPE_CHARACTER,
  INVALID_ESCAPE_OCTET,
  INVALID_ESCAPE_SEQUENCE,
  NONSTANDARD_USE_OF_ESCAPE_CHARACTER,
  INVALID_INDICATOR_PARAMETER_VALUE,
  INVALID_PARAMETER_VALUE,
  INVALID_REGULAR_EXPRESSION,
  INVALID_ROW_COUNT_IN_LIMIT_CLAUSE,
  INVALID_ROW_COUNT_IN_RESULT_OFFSET_CLAUSE,
  INVALID_TABLESAMPLE_ARGUMENT,
  INVALID_TABLESAMPLE_REPEAT,
  INVALID_TIME_ZONE_DISPLACEMENT_VALUE,
  INVALID_USE_OF_ESCAPE_CHARACTER,
  MOST_SPECIFIC_TYPE_MISMATCH,
  NULL_VALUE_NOT_ALLOWED,
  NULL_VALUE_NO_INDICATOR_PARAMETER,
  NUMERIC_VALUE_OUT_OF_RANGE,
  STRING_DATA_LENGTH_MISMATCH,
  STRING_DATA_RIGHT_TRUNCATION,
  SUBSTRING_ERROR,
  TRIM_ERROR,
  UNTERMINATED_C_STRING,
  ZERO_LENGTH_CHARACTER_STRING,
  FLOATING_POINT_EXCEPTION,
  INVALID_TEXT_REPRESENTATION,
  INVALID_BINARY_REPRESENTATION,
  BAD_COPY_FILE_FORMAT,
  UNTRANSLATABLE_CHARACTER,
  NOT_AN_XML_DOCUMENT,
  INVALID_XML_DOCUMENT,
  INVALID_XML_CONTENT,
  INVALID_XML_COMMENT,
  INVALID_XML_PROCESSING_INSTRUCTION,
  INTEGRITY_CONSTRAINT_VIOLATION,
  RESTRICT_VIOLATION,
  NOT_NULL_VIOLATION,
  FOREIGN_KEY_VIOLATION,
  UNIQUE_VIOLATION,
  CHECK_VIOLATION,
  EXCLUSION_VIOLATION,
  INVALID_CURSOR_STATE,
  INVALID_TRANSACTION_STATE,
  ACTIVE_SQL_TRANSACTION,
  BRANCH_TRANSACTION_ALREADY_ACTIVE,
  HELD_CURSOR_REQUIRES_SAME_ISOLATION_LEVEL,
  INAPPROPRIATE_ACCESS_MODE_FOR_BRANCH_TRANSACTION,
  INAPPROPRIATE_ISOLATION_LEVEL_FOR_BRANCH_TRANSACTION,
  NO_ACTIVE_SQL_TRANSACTION_FOR_BRANCH_TRANSACTION,
  READ_ONLY_SQL_TRANSACTION,
  SCHEMA_AND_DATA_STATEMENT_MIXING_NOT_SUPPORTED,
  NO_ACTIVE_SQL_TRANSACTION,
  IN_FAILED_SQL_TRANSACTION,
  INVALID_SQL_STATEMENT_NAME,
  TRIGGERED_DATA_CHANGE_VIOLATION,
  INVALID_AUTHORIZATION_SPECIFICATION,
  INVALID_PASSWORD,
  DEPENDENT_PRIVILEGE_DESCRIPTORS_STILL_EXIST,
  DEPENDENT_OBJECTS_STILL_EXIST,
  INVALID_TRANSACTION_TERMINATION,
  SQL_ROUTINE_EXCEPTION,
  S_R_E_FUNCTION_EXECUTED_NO_RETURN_STATEMENT,
  S_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED,
  S_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED,
  S_R_E_READING_SQL_DATA_NOT_PERMITTED,
  INVALID_CURSOR_NAME,
  EXTERNAL_ROUTINE_EXCEPTION,
  E_R_E_CONTAINING_SQL_NOT_PERMITTED,
  E_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED,
  E_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED,
  E_R_E_READING_SQL_DATA_NOT_PERMITTED,
  EXTERNAL_ROUTINE_INVOCATION_EXCEPTION,
  E_R_I_E_INVALID_SQLSTATE_RETURNED,
  E_R_I_E_NULL_VALUE_NOT_ALLOWED,
  E_R_I_E_TRIGGER_PROTOCOL_VIOLATED,
  E_R_I_E_SRF_PROTOCOL_VIOLATED,
  E_R_I_E_EVENT_TRIGGER_PROTOCOL_VIOLATED,
  SAVEPOINT_EXCEPTION,
  S_E_INVALID_SPECIFICATION,
  INVALID_CATALOG_NAME,
  INVALID_SCHEMA_NAME,
  TRANSACTION_ROLLBACK,
  T_R_INTEGRITY_CONSTRAINT_VIOLATION,
  T_R_SERIALIZATION_FAILURE,
  T_R_STATEMENT_COMPLETION_UNKNOWN,
  T_R_DEADLOCK_DETECTED,
  SYNTAX_ERROR_OR_ACCESS_RULE_VIOLATION,
  SYNTAX_ERROR,
  INSUFFICIENT_PRIVILEGE,
  CANNOT_COERCE,
  GROUPING_ERROR,
  WINDOWING_ERROR,
  INVALID_RECURSION,
  INVALID_FOREIGN_KEY,
  INVALID_NAME,
  NAME_TOO_LONG,
  RESERVED_NAME,
  DATATYPE_MISMATCH,
  INDETERMINATE_DATATYPE,
  COLLATION_MISMATCH,
  INDETERMINATE_COLLATION,
  WRONG_METADATA_VERSION,
  WRONG_OBJECT_TYPE,
  UNDEFINED_COLUMN,
  UNDEFINED_CURSOR,
  UNDEFINED_DATABASE,
  UNDEFINED_FUNCTION,
  UNDEFINED_PSTATEMENT,
  UNDEFINED_SCHEMA,
  UNDEFINED_TABLE,
  UNDEFINED_PARAMETER,
  UNDEFINED_OBJECT,
  DUPLICATE_COLUMN,
  DUPLICATE_CURSOR,
  DUPLICATE_DATABASE,
  DUPLICATE_FUNCTION,
  DUPLICATE_PSTATEMENT,
  DUPLICATE_SCHEMA,
  DUPLICATE_TABLE,
  DUPLICATE_ALIAS,
  DUPLICATE_OBJECT,
  AMBIGUOUS_COLUMN,
  AMBIGUOUS_FUNCTION,
  AMBIGUOUS_PARAMETER,
  AMBIGUOUS_ALIAS,
  INVALID_COLUMN_REFERENCE,
  INVALID_COLUMN_DEFINITION,
  INVALID_CURSOR_DEFINITION,
  INVALID_DATABASE_DEFINITION,
  INVALID_FUNCTION_DEFINITION,
  INVALID_PSTATEMENT_DEFINITION,
  INVALID_SCHEMA_DEFINITION,
  INVALID_TABLE_DEFINITION,
  INVALID_OBJECT_DEFINITION,
  WITH_CHECK_OPTION_VIOLATION,
  INSUFFICIENT_RESOURCES,
  DISK_FULL,
  OUT_OF_MEMORY,
  TOO_MANY_CONNECTIONS,
  CONFIGURATION_LIMIT_EXCEEDED,
  PROGRAM_LIMIT_EXCEEDED,
  STATEMENT_TOO_COMPLEX,
  TOO_MANY_COLUMNS,
  TOO_FEW_ARGUMENTS,
  TOO_MANY_ARGUMENTS,
  MISSING_ARGUMENT_FOR_PRIMARY_KEY,
  OBJECT_NOT_IN_PREREQUISITE_STATE,
  OBJECT_IN_USE,
  CANT_CHANGE_RUNTIME_PARAM,
  LOCK_NOT_AVAILABLE,
  OPERATOR_INTERVENTION,
  QUERY_CANCELED,
  ADMIN_SHUTDOWN,
  CRASH_SHUTDOWN,
  CANNOT_CONNECT_NOW,
  DATABASE_DROPPED,
  SYSTEM_ERROR,
  IO_ERROR,
  UNDEFINED_FILE,
  DUPLICATE_FILE,
  CONFIG_FILE_ERROR,
  LOCK_FILE_EXISTS,
  FDW_ERROR,
  FDW_COLUMN_NAME_NOT_FOUND,
  FDW_DYNAMIC_PARAMETER_VALUE_NEEDED,
  FDW_FUNCTION_SEQUENCE_ERROR,
  FDW_INCONSISTENT_DESCRIPTOR_INFORMATION,
  FDW_INVALID_ATTRIBUTE_VALUE,
  FDW_INVALID_COLUMN_NAME,
  FDW_INVALID_COLUMN_NUMBER,
  FDW_INVALID_DATA_TYPE,
  FDW_INVALID_DATA_TYPE_DESCRIPTORS,
  FDW_INVALID_DESCRIPTOR_FIELD_IDENTIFIER,
  FDW_INVALID_HANDLE,
  FDW_INVALID_OPTION_INDEX,
  FDW_INVALID_OPTION_NAME,
  FDW_INVALID_STRING_LENGTH_OR_BUFFER_LENGTH,
  FDW_INVALID_STRING_FORMAT,
  FDW_INVALID_USE_OF_NULL_POINTER,
  FDW_TOO_MANY_HANDLES,
  FDW_OUT_OF_MEMORY,
  FDW_NO_SCHEMAS,
  FDW_OPTION_NAME_NOT_FOUND,
  FDW_REPLY_HANDLE,
  FDW_SCHEMA_NOT_FOUND,
  FDW_TABLE_NOT_FOUND,
  FDW_UNABLE_TO_CREATE_EXECUTION,
  FDW_UNABLE_TO_CREATE_REPLY,
  FDW_UNABLE_TO_ESTABLISH_CONNECTION,
  PLPGSQL_ERROR,
  RAISE_EXCEPTION,
  NO_DATA_FOUND,
  TOO_MANY_ROWS,
  ASSERT_FAILURE,
  INTERNAL_ERROR,
  DATA_CORRUPTED,
  INDEX_CORRUPTED,

  // Warning codes. YbSql statement execution wouldn't stop after reporting these messages.
  WARNING,
  WARNING_DYNAMIC_RESULT_SETS_RETURNED,
  WARNING_IMPLICIT_ZERO_BIT_PADDING,
  WARNING_NULL_VALUE_ELIMINATED_IN_SET_FUNCTION,
  WARNING_PRIVILEGE_NOT_GRANTED,
  WARNING_PRIVILEGE_NOT_REVOKED,
  WARNING_STRING_DATA_RIGHT_TRUNCATION,
  WARNING_DEPRECATED_FEATURE,
};

// Mapping errcode to text messages.
const char *ErrorText(ErrorCode code);

// Font for error message is RED.
constexpr const char *kErrorFontStart = "\033[31m";
constexpr const char *kErrorFontEnd = "\033[0m";

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_UTIL_ERRCODES_H_
