//LICENSE Portions Copyright 2019-2021 ZomboDB, LLC.
//LICENSE
//LICENSE Portions Copyright 2021-2023 Technology Concepts & Design, Inc.
//LICENSE
//LICENSE Portions Copyright 2023-2023 PgCentral Foundation, Inc. <contact@pgcentral.org>
//LICENSE
//LICENSE All rights reserved.
//LICENSE
//LICENSE Use of this source code is governed by the MIT license that can be found in the LICENSE file.
use std::fmt::{Display, Formatter};

/// This list of SQL Error Codes is taken directly from Postgres 12's generated "utils/errcodes.h"
#[allow(non_camel_case_types)]
#[derive(Clone, Copy, Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PgSqlErrorCode {
    /// Class 00 - Successful Completion
    ERRCODE_SUCCESSFUL_COMPLETION = MAKE_SQLSTATE('0', '0', '0', '0', '0') as isize,

    /// Class 01 - Warning
    ERRCODE_WARNING = MAKE_SQLSTATE('0', '1', '0', '0', '0') as isize,
    ERRCODE_WARNING_DYNAMIC_RESULT_SETS_RETURNED = MAKE_SQLSTATE('0', '1', '0', '0', 'C') as isize,
    ERRCODE_WARNING_IMPLICIT_ZERO_BIT_PADDING = MAKE_SQLSTATE('0', '1', '0', '0', '8') as isize,
    ERRCODE_WARNING_NULL_VALUE_ELIMINATED_IN_SET_FUNCTION =
        MAKE_SQLSTATE('0', '1', '0', '0', '3') as isize,
    ERRCODE_WARNING_PRIVILEGE_NOT_GRANTED = MAKE_SQLSTATE('0', '1', '0', '0', '7') as isize,
    ERRCODE_WARNING_PRIVILEGE_NOT_REVOKED = MAKE_SQLSTATE('0', '1', '0', '0', '6') as isize,
    ERRCODE_WARNING_STRING_DATA_RIGHT_TRUNCATION = MAKE_SQLSTATE('0', '1', '0', '0', '4') as isize,
    ERRCODE_WARNING_DEPRECATED_FEATURE = MAKE_SQLSTATE('0', '1', 'P', '0', '1') as isize,

    /// Class 02 - No Data (this is also a warning class per the SQL standard) as isize,
    ERRCODE_NO_DATA = MAKE_SQLSTATE('0', '2', '0', '0', '0') as isize,
    ERRCODE_NO_ADDITIONAL_DYNAMIC_RESULT_SETS_RETURNED =
        MAKE_SQLSTATE('0', '2', '0', '0', '1') as isize,

    /// Class 03 - SQL Statement Not Yet Complete
    ERRCODE_SQL_STATEMENT_NOT_YET_COMPLETE = MAKE_SQLSTATE('0', '3', '0', '0', '0') as isize,

    /// Class 08 - Connection Exception
    ERRCODE_CONNECTION_EXCEPTION = MAKE_SQLSTATE('0', '8', '0', '0', '0') as isize,
    ERRCODE_CONNECTION_DOES_NOT_EXIST = MAKE_SQLSTATE('0', '8', '0', '0', '3') as isize,
    ERRCODE_CONNECTION_FAILURE = MAKE_SQLSTATE('0', '8', '0', '0', '6') as isize,
    ERRCODE_SQLCLIENT_UNABLE_TO_ESTABLISH_SQLCONNECTION =
        MAKE_SQLSTATE('0', '8', '0', '0', '1') as isize,
    ERRCODE_SQLSERVER_REJECTED_ESTABLISHMENT_OF_SQLCONNECTION =
        MAKE_SQLSTATE('0', '8', '0', '0', '4') as isize,
    ERRCODE_TRANSACTION_RESOLUTION_UNKNOWN = MAKE_SQLSTATE('0', '8', '0', '0', '7') as isize,
    ERRCODE_PROTOCOL_VIOLATION = MAKE_SQLSTATE('0', '8', 'P', '0', '1') as isize,

    /// Class 09 - Triggered Action Exception
    ERRCODE_TRIGGERED_ACTION_EXCEPTION = MAKE_SQLSTATE('0', '9', '0', '0', '0') as isize,

    /// Class 0A - Feature Not Supported
    ERRCODE_FEATURE_NOT_SUPPORTED = MAKE_SQLSTATE('0', 'A', '0', '0', '0') as isize,

    /// Class 0B - Invalid Transaction Initiation
    ERRCODE_INVALID_TRANSACTION_INITIATION = MAKE_SQLSTATE('0', 'B', '0', '0', '0') as isize,

    /// Class 0F - Locator Exception
    ERRCODE_LOCATOR_EXCEPTION = MAKE_SQLSTATE('0', 'F', '0', '0', '0') as isize,
    ERRCODE_L_E_INVALID_SPECIFICATION = MAKE_SQLSTATE('0', 'F', '0', '0', '1') as isize,

    /// Class 0L - Invalid Grantor
    ERRCODE_INVALID_GRANTOR = MAKE_SQLSTATE('0', 'L', '0', '0', '0') as isize,
    ERRCODE_INVALID_GRANT_OPERATION = MAKE_SQLSTATE('0', 'L', 'P', '0', '1') as isize,

    /// Class 0P - Invalid Role Specification
    ERRCODE_INVALID_ROLE_SPECIFICATION = MAKE_SQLSTATE('0', 'P', '0', '0', '0') as isize,

    /// Class 0Z - Diagnostics Exception
    ERRCODE_DIAGNOSTICS_EXCEPTION = MAKE_SQLSTATE('0', 'Z', '0', '0', '0') as isize,
    ERRCODE_STACKED_DIAGNOSTICS_ACCESSED_WITHOUT_ACTIVE_HANDLER =
        MAKE_SQLSTATE('0', 'Z', '0', '0', '2') as isize,

    /// Class 20 - Case Not Found
    ERRCODE_CASE_NOT_FOUND = MAKE_SQLSTATE('2', '0', '0', '0', '0') as isize,

    /// Class 21 - Cardinality Violation
    ERRCODE_CARDINALITY_VIOLATION = MAKE_SQLSTATE('2', '1', '0', '0', '0') as isize,

    /// Class 22 - Data Exception
    ERRCODE_DATA_EXCEPTION = MAKE_SQLSTATE('2', '2', '0', '0', '0') as isize,
    ERRCODE_ARRAY_ELEMENT_ERROR = MAKE_SQLSTATE('2', '2', '0', '2', 'E') as isize,
    //    ERRCODE_ARRAY_SUBSCRIPT_ERROR = MAKE_SQLSTATE('2', '2', '0', '2', 'E') as isize,
    ERRCODE_CHARACTER_NOT_IN_REPERTOIRE = MAKE_SQLSTATE('2', '2', '0', '2', '1') as isize,
    ERRCODE_DATETIME_FIELD_OVERFLOW = MAKE_SQLSTATE('2', '2', '0', '0', '8') as isize,
    //    ERRCODE_DATETIME_VALUE_OUT_OF_RANGE = MAKE_SQLSTATE('2', '2', '0', '0', '8') as isize,
    ERRCODE_DIVISION_BY_ZERO = MAKE_SQLSTATE('2', '2', '0', '1', '2') as isize,
    ERRCODE_ERROR_IN_ASSIGNMENT = MAKE_SQLSTATE('2', '2', '0', '0', '5') as isize,
    ERRCODE_ESCAPE_CHARACTER_CONFLICT = MAKE_SQLSTATE('2', '2', '0', '0', 'B') as isize,
    ERRCODE_INDICATOR_OVERFLOW = MAKE_SQLSTATE('2', '2', '0', '2', '2') as isize,
    ERRCODE_INTERVAL_FIELD_OVERFLOW = MAKE_SQLSTATE('2', '2', '0', '1', '5') as isize,
    ERRCODE_INVALID_ARGUMENT_FOR_LOG = MAKE_SQLSTATE('2', '2', '0', '1', 'E') as isize,
    ERRCODE_INVALID_ARGUMENT_FOR_NTILE = MAKE_SQLSTATE('2', '2', '0', '1', '4') as isize,
    ERRCODE_INVALID_ARGUMENT_FOR_NTH_VALUE = MAKE_SQLSTATE('2', '2', '0', '1', '6') as isize,
    ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION = MAKE_SQLSTATE('2', '2', '0', '1', 'F') as isize,
    ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION =
        MAKE_SQLSTATE('2', '2', '0', '1', 'G') as isize,
    ERRCODE_INVALID_CHARACTER_VALUE_FOR_CAST = MAKE_SQLSTATE('2', '2', '0', '1', '8') as isize,
    ERRCODE_INVALID_DATETIME_FORMAT = MAKE_SQLSTATE('2', '2', '0', '0', '7') as isize,
    ERRCODE_INVALID_ESCAPE_CHARACTER = MAKE_SQLSTATE('2', '2', '0', '1', '9') as isize,
    ERRCODE_INVALID_ESCAPE_OCTET = MAKE_SQLSTATE('2', '2', '0', '0', 'D') as isize,
    ERRCODE_INVALID_ESCAPE_SEQUENCE = MAKE_SQLSTATE('2', '2', '0', '2', '5') as isize,
    ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER = MAKE_SQLSTATE('2', '2', 'P', '0', '6') as isize,
    ERRCODE_INVALID_INDICATOR_PARAMETER_VALUE = MAKE_SQLSTATE('2', '2', '0', '1', '0') as isize,
    ERRCODE_INVALID_PARAMETER_VALUE = MAKE_SQLSTATE('2', '2', '0', '2', '3') as isize,
    ERRCODE_INVALID_PRECEDING_OR_FOLLOWING_SIZE = MAKE_SQLSTATE('2', '2', '0', '1', '3') as isize,
    ERRCODE_INVALID_REGULAR_EXPRESSION = MAKE_SQLSTATE('2', '2', '0', '1', 'B') as isize,
    ERRCODE_INVALID_ROW_COUNT_IN_LIMIT_CLAUSE = MAKE_SQLSTATE('2', '2', '0', '1', 'W') as isize,
    ERRCODE_INVALID_ROW_COUNT_IN_RESULT_OFFSET_CLAUSE =
        MAKE_SQLSTATE('2', '2', '0', '1', 'X') as isize,
    ERRCODE_INVALID_TABLESAMPLE_ARGUMENT = MAKE_SQLSTATE('2', '2', '0', '2', 'H') as isize,
    ERRCODE_INVALID_TABLESAMPLE_REPEAT = MAKE_SQLSTATE('2', '2', '0', '2', 'G') as isize,
    ERRCODE_INVALID_TIME_ZONE_DISPLACEMENT_VALUE = MAKE_SQLSTATE('2', '2', '0', '0', '9') as isize,
    ERRCODE_INVALID_USE_OF_ESCAPE_CHARACTER = MAKE_SQLSTATE('2', '2', '0', '0', 'C') as isize,
    ERRCODE_MOST_SPECIFIC_TYPE_MISMATCH = MAKE_SQLSTATE('2', '2', '0', '0', 'G') as isize,
    ERRCODE_NULL_VALUE_NOT_ALLOWED = MAKE_SQLSTATE('2', '2', '0', '0', '4') as isize,
    ERRCODE_NULL_VALUE_NO_INDICATOR_PARAMETER = MAKE_SQLSTATE('2', '2', '0', '0', '2') as isize,
    ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE = MAKE_SQLSTATE('2', '2', '0', '0', '3') as isize,
    ERRCODE_SEQUENCE_GENERATOR_LIMIT_EXCEEDED = MAKE_SQLSTATE('2', '2', '0', '0', 'H') as isize,
    ERRCODE_STRING_DATA_LENGTH_MISMATCH = MAKE_SQLSTATE('2', '2', '0', '2', '6') as isize,
    ERRCODE_STRING_DATA_RIGHT_TRUNCATION = MAKE_SQLSTATE('2', '2', '0', '0', '1') as isize,
    ERRCODE_SUBSTRING_ERROR = MAKE_SQLSTATE('2', '2', '0', '1', '1') as isize,
    ERRCODE_TRIM_ERROR = MAKE_SQLSTATE('2', '2', '0', '2', '7') as isize,
    ERRCODE_UNTERMINATED_C_STRING = MAKE_SQLSTATE('2', '2', '0', '2', '4') as isize,
    ERRCODE_ZERO_LENGTH_CHARACTER_STRING = MAKE_SQLSTATE('2', '2', '0', '0', 'F') as isize,
    ERRCODE_FLOATING_POINT_EXCEPTION = MAKE_SQLSTATE('2', '2', 'P', '0', '1') as isize,
    ERRCODE_INVALID_TEXT_REPRESENTATION = MAKE_SQLSTATE('2', '2', 'P', '0', '2') as isize,
    ERRCODE_INVALID_BINARY_REPRESENTATION = MAKE_SQLSTATE('2', '2', 'P', '0', '3') as isize,
    ERRCODE_BAD_COPY_FILE_FORMAT = MAKE_SQLSTATE('2', '2', 'P', '0', '4') as isize,
    ERRCODE_UNTRANSLATABLE_CHARACTER = MAKE_SQLSTATE('2', '2', 'P', '0', '5') as isize,
    ERRCODE_NOT_AN_XML_DOCUMENT = MAKE_SQLSTATE('2', '2', '0', '0', 'L') as isize,
    ERRCODE_INVALID_XML_DOCUMENT = MAKE_SQLSTATE('2', '2', '0', '0', 'M') as isize,
    ERRCODE_INVALID_XML_CONTENT = MAKE_SQLSTATE('2', '2', '0', '0', 'N') as isize,
    ERRCODE_INVALID_XML_COMMENT = MAKE_SQLSTATE('2', '2', '0', '0', 'S') as isize,
    ERRCODE_INVALID_XML_PROCESSING_INSTRUCTION = MAKE_SQLSTATE('2', '2', '0', '0', 'T') as isize,
    ERRCODE_DUPLICATE_JSON_OBJECT_KEY_VALUE = MAKE_SQLSTATE('2', '2', '0', '3', '0') as isize,
    ERRCODE_INVALID_JSON_TEXT = MAKE_SQLSTATE('2', '2', '0', '3', '2') as isize,
    ERRCODE_INVALID_SQL_JSON_SUBSCRIPT = MAKE_SQLSTATE('2', '2', '0', '3', '3') as isize,
    ERRCODE_MORE_THAN_ONE_SQL_JSON_ITEM = MAKE_SQLSTATE('2', '2', '0', '3', '4') as isize,
    ERRCODE_NO_SQL_JSON_ITEM = MAKE_SQLSTATE('2', '2', '0', '3', '5') as isize,
    ERRCODE_NON_NUMERIC_SQL_JSON_ITEM = MAKE_SQLSTATE('2', '2', '0', '3', '6') as isize,
    ERRCODE_NON_UNIQUE_KEYS_IN_A_JSON_OBJECT = MAKE_SQLSTATE('2', '2', '0', '3', '7') as isize,
    ERRCODE_SINGLETON_SQL_JSON_ITEM_REQUIRED = MAKE_SQLSTATE('2', '2', '0', '3', '8') as isize,
    ERRCODE_SQL_JSON_ARRAY_NOT_FOUND = MAKE_SQLSTATE('2', '2', '0', '3', '9') as isize,
    ERRCODE_SQL_JSON_MEMBER_NOT_FOUND = MAKE_SQLSTATE('2', '2', '0', '3', 'A') as isize,
    ERRCODE_SQL_JSON_NUMBER_NOT_FOUND = MAKE_SQLSTATE('2', '2', '0', '3', 'B') as isize,
    ERRCODE_SQL_JSON_OBJECT_NOT_FOUND = MAKE_SQLSTATE('2', '2', '0', '3', 'C') as isize,
    ERRCODE_TOO_MANY_JSON_ARRAY_ELEMENTS = MAKE_SQLSTATE('2', '2', '0', '3', 'D') as isize,
    ERRCODE_TOO_MANY_JSON_OBJECT_MEMBERS = MAKE_SQLSTATE('2', '2', '0', '3', 'E') as isize,
    ERRCODE_SQL_JSON_SCALAR_REQUIRED = MAKE_SQLSTATE('2', '2', '0', '3', 'F') as isize,

    /// Class 23 - Integrity Constraint Violation
    ERRCODE_INTEGRITY_CONSTRAINT_VIOLATION = MAKE_SQLSTATE('2', '3', '0', '0', '0') as isize,
    ERRCODE_RESTRICT_VIOLATION = MAKE_SQLSTATE('2', '3', '0', '0', '1') as isize,
    ERRCODE_NOT_NULL_VIOLATION = MAKE_SQLSTATE('2', '3', '5', '0', '2') as isize,
    ERRCODE_FOREIGN_KEY_VIOLATION = MAKE_SQLSTATE('2', '3', '5', '0', '3') as isize,
    ERRCODE_UNIQUE_VIOLATION = MAKE_SQLSTATE('2', '3', '5', '0', '5') as isize,
    ERRCODE_CHECK_VIOLATION = MAKE_SQLSTATE('2', '3', '5', '1', '4') as isize,
    ERRCODE_EXCLUSION_VIOLATION = MAKE_SQLSTATE('2', '3', 'P', '0', '1') as isize,

    /// Class 24 - Invalid Cursor State
    ERRCODE_INVALID_CURSOR_STATE = MAKE_SQLSTATE('2', '4', '0', '0', '0') as isize,

    /// Class 25 - Invalid Transaction State
    ERRCODE_INVALID_TRANSACTION_STATE = MAKE_SQLSTATE('2', '5', '0', '0', '0') as isize,
    ERRCODE_ACTIVE_SQL_TRANSACTION = MAKE_SQLSTATE('2', '5', '0', '0', '1') as isize,
    ERRCODE_BRANCH_TRANSACTION_ALREADY_ACTIVE = MAKE_SQLSTATE('2', '5', '0', '0', '2') as isize,
    ERRCODE_HELD_CURSOR_REQUIRES_SAME_ISOLATION_LEVEL =
        MAKE_SQLSTATE('2', '5', '0', '0', '8') as isize,
    ERRCODE_INAPPROPRIATE_ACCESS_MODE_FOR_BRANCH_TRANSACTION =
        MAKE_SQLSTATE('2', '5', '0', '0', '3') as isize,
    ERRCODE_INAPPROPRIATE_ISOLATION_LEVEL_FOR_BRANCH_TRANSACTION =
        MAKE_SQLSTATE('2', '5', '0', '0', '4') as isize,
    ERRCODE_NO_ACTIVE_SQL_TRANSACTION_FOR_BRANCH_TRANSACTION =
        MAKE_SQLSTATE('2', '5', '0', '0', '5') as isize,
    ERRCODE_READ_ONLY_SQL_TRANSACTION = MAKE_SQLSTATE('2', '5', '0', '0', '6') as isize,
    ERRCODE_SCHEMA_AND_DATA_STATEMENT_MIXING_NOT_SUPPORTED =
        MAKE_SQLSTATE('2', '5', '0', '0', '7') as isize,
    ERRCODE_NO_ACTIVE_SQL_TRANSACTION = MAKE_SQLSTATE('2', '5', 'P', '0', '1') as isize,
    ERRCODE_IN_FAILED_SQL_TRANSACTION = MAKE_SQLSTATE('2', '5', 'P', '0', '2') as isize,
    ERRCODE_IDLE_IN_TRANSACTION_SESSION_TIMEOUT = MAKE_SQLSTATE('2', '5', 'P', '0', '3') as isize,

    /// Class 26 - Invalid SQL Statement Name
    ERRCODE_INVALID_SQL_STATEMENT_NAME = MAKE_SQLSTATE('2', '6', '0', '0', '0') as isize,

    /// Class 27 - Triggered Data Change Violation
    ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION = MAKE_SQLSTATE('2', '7', '0', '0', '0') as isize,

    /// Class 28 - Invalid Authorization Specification
    ERRCODE_INVALID_AUTHORIZATION_SPECIFICATION = MAKE_SQLSTATE('2', '8', '0', '0', '0') as isize,
    ERRCODE_INVALID_PASSWORD = MAKE_SQLSTATE('2', '8', 'P', '0', '1') as isize,

    /// Class 2B - Dependent Privilege Descriptors Still Exist
    ERRCODE_DEPENDENT_PRIVILEGE_DESCRIPTORS_STILL_EXIST =
        MAKE_SQLSTATE('2', 'B', '0', '0', '0') as isize,
    ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST = MAKE_SQLSTATE('2', 'B', 'P', '0', '1') as isize,

    /// Class 2D - Invalid Transaction Termination
    ERRCODE_INVALID_TRANSACTION_TERMINATION = MAKE_SQLSTATE('2', 'D', '0', '0', '0') as isize,

    /// Class 2F - SQL Routine Exception
    ERRCODE_SQL_ROUTINE_EXCEPTION = MAKE_SQLSTATE('2', 'F', '0', '0', '0') as isize,
    ERRCODE_S_R_E_FUNCTION_EXECUTED_NO_RETURN_STATEMENT =
        MAKE_SQLSTATE('2', 'F', '0', '0', '5') as isize,
    ERRCODE_S_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED =
        MAKE_SQLSTATE('2', 'F', '0', '0', '2') as isize,
    ERRCODE_S_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED =
        MAKE_SQLSTATE('2', 'F', '0', '0', '3') as isize,
    ERRCODE_S_R_E_READING_SQL_DATA_NOT_PERMITTED = MAKE_SQLSTATE('2', 'F', '0', '0', '4') as isize,

    /// Class 34 - Invalid Cursor Name
    ERRCODE_INVALID_CURSOR_NAME = MAKE_SQLSTATE('3', '4', '0', '0', '0') as isize,

    /// Class 38 - External Routine Exception
    ERRCODE_EXTERNAL_ROUTINE_EXCEPTION = MAKE_SQLSTATE('3', '8', '0', '0', '0') as isize,
    ERRCODE_E_R_E_CONTAINING_SQL_NOT_PERMITTED = MAKE_SQLSTATE('3', '8', '0', '0', '1') as isize,
    ERRCODE_E_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED =
        MAKE_SQLSTATE('3', '8', '0', '0', '2') as isize,
    ERRCODE_E_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED =
        MAKE_SQLSTATE('3', '8', '0', '0', '3') as isize,
    ERRCODE_E_R_E_READING_SQL_DATA_NOT_PERMITTED = MAKE_SQLSTATE('3', '8', '0', '0', '4') as isize,

    /// Class 39 - External Routine Invocation Exception
    ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION = MAKE_SQLSTATE('3', '9', '0', '0', '0') as isize,
    ERRCODE_E_R_I_E_INVALID_SQLSTATE_RETURNED = MAKE_SQLSTATE('3', '9', '0', '0', '1') as isize,
    ERRCODE_E_R_I_E_NULL_VALUE_NOT_ALLOWED = MAKE_SQLSTATE('3', '9', '0', '0', '4') as isize,
    ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED = MAKE_SQLSTATE('3', '9', 'P', '0', '1') as isize,
    ERRCODE_E_R_I_E_SRF_PROTOCOL_VIOLATED = MAKE_SQLSTATE('3', '9', 'P', '0', '2') as isize,
    ERRCODE_E_R_I_E_EVENT_TRIGGER_PROTOCOL_VIOLATED =
        MAKE_SQLSTATE('3', '9', 'P', '0', '3') as isize,

    /// Class 3B - Savepoint Exception
    ERRCODE_SAVEPOINT_EXCEPTION = MAKE_SQLSTATE('3', 'B', '0', '0', '0') as isize,
    ERRCODE_S_E_INVALID_SPECIFICATION = MAKE_SQLSTATE('3', 'B', '0', '0', '1') as isize,

    /// Class 3D - Invalid Catalog Name
    ERRCODE_INVALID_CATALOG_NAME = MAKE_SQLSTATE('3', 'D', '0', '0', '0') as isize,

    /// Class 3F - Invalid Schema Name
    ERRCODE_INVALID_SCHEMA_NAME = MAKE_SQLSTATE('3', 'F', '0', '0', '0') as isize,

    /// Class 40 - Transaction Rollback
    ERRCODE_TRANSACTION_ROLLBACK = MAKE_SQLSTATE('4', '0', '0', '0', '0') as isize,
    ERRCODE_T_R_INTEGRITY_CONSTRAINT_VIOLATION = MAKE_SQLSTATE('4', '0', '0', '0', '2') as isize,
    ERRCODE_T_R_SERIALIZATION_FAILURE = MAKE_SQLSTATE('4', '0', '0', '0', '1') as isize,
    ERRCODE_T_R_STATEMENT_COMPLETION_UNKNOWN = MAKE_SQLSTATE('4', '0', '0', '0', '3') as isize,
    ERRCODE_T_R_DEADLOCK_DETECTED = MAKE_SQLSTATE('4', '0', 'P', '0', '1') as isize,

    /// Class 42 - Syntax Error or Access Rule Violation
    ERRCODE_SYNTAX_ERROR_OR_ACCESS_RULE_VIOLATION = MAKE_SQLSTATE('4', '2', '0', '0', '0') as isize,
    ERRCODE_SYNTAX_ERROR = MAKE_SQLSTATE('4', '2', '6', '0', '1') as isize,
    ERRCODE_INSUFFICIENT_PRIVILEGE = MAKE_SQLSTATE('4', '2', '5', '0', '1') as isize,
    ERRCODE_CANNOT_COERCE = MAKE_SQLSTATE('4', '2', '8', '4', '6') as isize,
    ERRCODE_GROUPING_ERROR = MAKE_SQLSTATE('4', '2', '8', '0', '3') as isize,
    ERRCODE_WINDOWING_ERROR = MAKE_SQLSTATE('4', '2', 'P', '2', '0') as isize,
    ERRCODE_INVALID_RECURSION = MAKE_SQLSTATE('4', '2', 'P', '1', '9') as isize,
    ERRCODE_INVALID_FOREIGN_KEY = MAKE_SQLSTATE('4', '2', '8', '3', '0') as isize,
    ERRCODE_INVALID_NAME = MAKE_SQLSTATE('4', '2', '6', '0', '2') as isize,
    ERRCODE_NAME_TOO_LONG = MAKE_SQLSTATE('4', '2', '6', '2', '2') as isize,
    ERRCODE_RESERVED_NAME = MAKE_SQLSTATE('4', '2', '9', '3', '9') as isize,
    ERRCODE_DATATYPE_MISMATCH = MAKE_SQLSTATE('4', '2', '8', '0', '4') as isize,
    ERRCODE_INDETERMINATE_DATATYPE = MAKE_SQLSTATE('4', '2', 'P', '1', '8') as isize,
    ERRCODE_COLLATION_MISMATCH = MAKE_SQLSTATE('4', '2', 'P', '2', '1') as isize,
    ERRCODE_INDETERMINATE_COLLATION = MAKE_SQLSTATE('4', '2', 'P', '2', '2') as isize,
    ERRCODE_WRONG_OBJECT_TYPE = MAKE_SQLSTATE('4', '2', '8', '0', '9') as isize,
    ERRCODE_GENERATED_ALWAYS = MAKE_SQLSTATE('4', '2', '8', 'C', '9') as isize,
    ERRCODE_UNDEFINED_COLUMN = MAKE_SQLSTATE('4', '2', '7', '0', '3') as isize,
    //    ERRCODE_UNDEFINED_CURSOR = MAKE_SQLSTATE('3', '4', '0', '0', '0') as isize,
    //    ERRCODE_UNDEFINED_DATABASE = MAKE_SQLSTATE('3', 'D', '0', '0', '0') as isize,
    ERRCODE_UNDEFINED_FUNCTION = MAKE_SQLSTATE('4', '2', '8', '8', '3') as isize,
    //    ERRCODE_UNDEFINED_PSTATEMENT = MAKE_SQLSTATE('2', '6', '0', '0', '0') as isize,
    //    ERRCODE_UNDEFINED_SCHEMA = MAKE_SQLSTATE('3', 'F', '0', '0', '0') as isize,
    ERRCODE_UNDEFINED_TABLE = MAKE_SQLSTATE('4', '2', 'P', '0', '1') as isize,
    ERRCODE_UNDEFINED_PARAMETER = MAKE_SQLSTATE('4', '2', 'P', '0', '2') as isize,
    ERRCODE_UNDEFINED_OBJECT = MAKE_SQLSTATE('4', '2', '7', '0', '4') as isize,
    ERRCODE_DUPLICATE_COLUMN = MAKE_SQLSTATE('4', '2', '7', '0', '1') as isize,
    ERRCODE_DUPLICATE_CURSOR = MAKE_SQLSTATE('4', '2', 'P', '0', '3') as isize,
    ERRCODE_DUPLICATE_DATABASE = MAKE_SQLSTATE('4', '2', 'P', '0', '4') as isize,
    ERRCODE_DUPLICATE_FUNCTION = MAKE_SQLSTATE('4', '2', '7', '2', '3') as isize,
    ERRCODE_DUPLICATE_PSTATEMENT = MAKE_SQLSTATE('4', '2', 'P', '0', '5') as isize,
    ERRCODE_DUPLICATE_SCHEMA = MAKE_SQLSTATE('4', '2', 'P', '0', '6') as isize,
    ERRCODE_DUPLICATE_TABLE = MAKE_SQLSTATE('4', '2', 'P', '0', '7') as isize,
    ERRCODE_DUPLICATE_ALIAS = MAKE_SQLSTATE('4', '2', '7', '1', '2') as isize,
    ERRCODE_DUPLICATE_OBJECT = MAKE_SQLSTATE('4', '2', '7', '1', '0') as isize,
    ERRCODE_AMBIGUOUS_COLUMN = MAKE_SQLSTATE('4', '2', '7', '0', '2') as isize,
    ERRCODE_AMBIGUOUS_FUNCTION = MAKE_SQLSTATE('4', '2', '7', '2', '5') as isize,
    ERRCODE_AMBIGUOUS_PARAMETER = MAKE_SQLSTATE('4', '2', 'P', '0', '8') as isize,
    ERRCODE_AMBIGUOUS_ALIAS = MAKE_SQLSTATE('4', '2', 'P', '0', '9') as isize,
    ERRCODE_INVALID_COLUMN_REFERENCE = MAKE_SQLSTATE('4', '2', 'P', '1', '0') as isize,
    ERRCODE_INVALID_COLUMN_DEFINITION = MAKE_SQLSTATE('4', '2', '6', '1', '1') as isize,
    ERRCODE_INVALID_CURSOR_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '1') as isize,
    ERRCODE_INVALID_DATABASE_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '2') as isize,
    ERRCODE_INVALID_FUNCTION_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '3') as isize,
    ERRCODE_INVALID_PSTATEMENT_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '4') as isize,
    ERRCODE_INVALID_SCHEMA_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '5') as isize,
    ERRCODE_INVALID_TABLE_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '6') as isize,
    ERRCODE_INVALID_OBJECT_DEFINITION = MAKE_SQLSTATE('4', '2', 'P', '1', '7') as isize,

    /// Class 44 - WITH CHECK OPTION Violation
    ERRCODE_WITH_CHECK_OPTION_VIOLATION = MAKE_SQLSTATE('4', '4', '0', '0', '0') as isize,

    /// Class 53 - Insufficient Resources
    ERRCODE_INSUFFICIENT_RESOURCES = MAKE_SQLSTATE('5', '3', '0', '0', '0') as isize,
    ERRCODE_DISK_FULL = MAKE_SQLSTATE('5', '3', '1', '0', '0') as isize,
    ERRCODE_OUT_OF_MEMORY = MAKE_SQLSTATE('5', '3', '2', '0', '0') as isize,
    ERRCODE_TOO_MANY_CONNECTIONS = MAKE_SQLSTATE('5', '3', '3', '0', '0') as isize,
    ERRCODE_CONFIGURATION_LIMIT_EXCEEDED = MAKE_SQLSTATE('5', '3', '4', '0', '0') as isize,

    /// Class 54 - Program Limit Exceeded
    ERRCODE_PROGRAM_LIMIT_EXCEEDED = MAKE_SQLSTATE('5', '4', '0', '0', '0') as isize,
    ERRCODE_STATEMENT_TOO_COMPLEX = MAKE_SQLSTATE('5', '4', '0', '0', '1') as isize,
    ERRCODE_TOO_MANY_COLUMNS = MAKE_SQLSTATE('5', '4', '0', '1', '1') as isize,
    ERRCODE_TOO_MANY_ARGUMENTS = MAKE_SQLSTATE('5', '4', '0', '2', '3') as isize,

    /// Class 55 - Object Not In Prerequisite State
    ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE = MAKE_SQLSTATE('5', '5', '0', '0', '0') as isize,
    ERRCODE_OBJECT_IN_USE = MAKE_SQLSTATE('5', '5', '0', '0', '6') as isize,
    ERRCODE_CANT_CHANGE_RUNTIME_PARAM = MAKE_SQLSTATE('5', '5', 'P', '0', '2') as isize,
    ERRCODE_LOCK_NOT_AVAILABLE = MAKE_SQLSTATE('5', '5', 'P', '0', '3') as isize,
    ERRCODE_UNSAFE_NEW_ENUM_VALUE_USAGE = MAKE_SQLSTATE('5', '5', 'P', '0', '4') as isize,

    /// Class 57 - Operator Intervention
    ERRCODE_OPERATOR_INTERVENTION = MAKE_SQLSTATE('5', '7', '0', '0', '0') as isize,
    ERRCODE_QUERY_CANCELED = MAKE_SQLSTATE('5', '7', '0', '1', '4') as isize,
    ERRCODE_ADMIN_SHUTDOWN = MAKE_SQLSTATE('5', '7', 'P', '0', '1') as isize,
    ERRCODE_CRASH_SHUTDOWN = MAKE_SQLSTATE('5', '7', 'P', '0', '2') as isize,
    ERRCODE_CANNOT_CONNECT_NOW = MAKE_SQLSTATE('5', '7', 'P', '0', '3') as isize,
    ERRCODE_DATABASE_DROPPED = MAKE_SQLSTATE('5', '7', 'P', '0', '4') as isize,

    /// Class 58 - System Error (errors external to PostgreSQL itself) as isize,
    ERRCODE_SYSTEM_ERROR = MAKE_SQLSTATE('5', '8', '0', '0', '0') as isize,
    ERRCODE_IO_ERROR = MAKE_SQLSTATE('5', '8', '0', '3', '0') as isize,
    ERRCODE_UNDEFINED_FILE = MAKE_SQLSTATE('5', '8', 'P', '0', '1') as isize,
    ERRCODE_DUPLICATE_FILE = MAKE_SQLSTATE('5', '8', 'P', '0', '2') as isize,

    /// Class 72 - Snapshot Failure
    ERRCODE_SNAPSHOT_TOO_OLD = MAKE_SQLSTATE('7', '2', '0', '0', '0') as isize,

    /// Class F0 - Configuration File Error
    ERRCODE_CONFIG_FILE_ERROR = MAKE_SQLSTATE('F', '0', '0', '0', '0') as isize,
    ERRCODE_LOCK_FILE_EXISTS = MAKE_SQLSTATE('F', '0', '0', '0', '1') as isize,

    /// Class HV - Foreign Data Wrapper Error (SQL/MED) as isize,
    ERRCODE_FDW_ERROR = MAKE_SQLSTATE('H', 'V', '0', '0', '0') as isize,
    ERRCODE_FDW_COLUMN_NAME_NOT_FOUND = MAKE_SQLSTATE('H', 'V', '0', '0', '5') as isize,
    ERRCODE_FDW_DYNAMIC_PARAMETER_VALUE_NEEDED = MAKE_SQLSTATE('H', 'V', '0', '0', '2') as isize,
    ERRCODE_FDW_FUNCTION_SEQUENCE_ERROR = MAKE_SQLSTATE('H', 'V', '0', '1', '0') as isize,
    ERRCODE_FDW_INCONSISTENT_DESCRIPTOR_INFORMATION =
        MAKE_SQLSTATE('H', 'V', '0', '2', '1') as isize,
    ERRCODE_FDW_INVALID_ATTRIBUTE_VALUE = MAKE_SQLSTATE('H', 'V', '0', '2', '4') as isize,
    ERRCODE_FDW_INVALID_COLUMN_NAME = MAKE_SQLSTATE('H', 'V', '0', '0', '7') as isize,
    ERRCODE_FDW_INVALID_COLUMN_NUMBER = MAKE_SQLSTATE('H', 'V', '0', '0', '8') as isize,
    ERRCODE_FDW_INVALID_DATA_TYPE = MAKE_SQLSTATE('H', 'V', '0', '0', '4') as isize,
    ERRCODE_FDW_INVALID_DATA_TYPE_DESCRIPTORS = MAKE_SQLSTATE('H', 'V', '0', '0', '6') as isize,
    ERRCODE_FDW_INVALID_DESCRIPTOR_FIELD_IDENTIFIER =
        MAKE_SQLSTATE('H', 'V', '0', '9', '1') as isize,
    ERRCODE_FDW_INVALID_HANDLE = MAKE_SQLSTATE('H', 'V', '0', '0', 'B') as isize,
    ERRCODE_FDW_INVALID_OPTION_INDEX = MAKE_SQLSTATE('H', 'V', '0', '0', 'C') as isize,
    ERRCODE_FDW_INVALID_OPTION_NAME = MAKE_SQLSTATE('H', 'V', '0', '0', 'D') as isize,
    ERRCODE_FDW_INVALID_STRING_LENGTH_OR_BUFFER_LENGTH =
        MAKE_SQLSTATE('H', 'V', '0', '9', '0') as isize,
    ERRCODE_FDW_INVALID_STRING_FORMAT = MAKE_SQLSTATE('H', 'V', '0', '0', 'A') as isize,
    ERRCODE_FDW_INVALID_USE_OF_NULL_POINTER = MAKE_SQLSTATE('H', 'V', '0', '0', '9') as isize,
    ERRCODE_FDW_TOO_MANY_HANDLES = MAKE_SQLSTATE('H', 'V', '0', '1', '4') as isize,
    ERRCODE_FDW_OUT_OF_MEMORY = MAKE_SQLSTATE('H', 'V', '0', '0', '1') as isize,
    ERRCODE_FDW_NO_SCHEMAS = MAKE_SQLSTATE('H', 'V', '0', '0', 'P') as isize,
    ERRCODE_FDW_OPTION_NAME_NOT_FOUND = MAKE_SQLSTATE('H', 'V', '0', '0', 'J') as isize,
    ERRCODE_FDW_REPLY_HANDLE = MAKE_SQLSTATE('H', 'V', '0', '0', 'K') as isize,
    ERRCODE_FDW_SCHEMA_NOT_FOUND = MAKE_SQLSTATE('H', 'V', '0', '0', 'Q') as isize,
    ERRCODE_FDW_TABLE_NOT_FOUND = MAKE_SQLSTATE('H', 'V', '0', '0', 'R') as isize,
    ERRCODE_FDW_UNABLE_TO_CREATE_EXECUTION = MAKE_SQLSTATE('H', 'V', '0', '0', 'L') as isize,
    ERRCODE_FDW_UNABLE_TO_CREATE_REPLY = MAKE_SQLSTATE('H', 'V', '0', '0', 'M') as isize,
    ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION = MAKE_SQLSTATE('H', 'V', '0', '0', 'N') as isize,

    /// Class P0 - PL/pgSQL Error
    ERRCODE_PLPGSQL_ERROR = MAKE_SQLSTATE('P', '0', '0', '0', '0') as isize,
    ERRCODE_RAISE_EXCEPTION = MAKE_SQLSTATE('P', '0', '0', '0', '1') as isize,
    ERRCODE_NO_DATA_FOUND = MAKE_SQLSTATE('P', '0', '0', '0', '2') as isize,
    ERRCODE_TOO_MANY_ROWS = MAKE_SQLSTATE('P', '0', '0', '0', '3') as isize,
    ERRCODE_ASSERT_FAILURE = MAKE_SQLSTATE('P', '0', '0', '0', '4') as isize,

    /// Class XX - Internal Error
    ERRCODE_INTERNAL_ERROR = MAKE_SQLSTATE('X', 'X', '0', '0', '0') as isize,
    ERRCODE_DATA_CORRUPTED = MAKE_SQLSTATE('X', 'X', '0', '0', '1') as isize,
    ERRCODE_INDEX_CORRUPTED = MAKE_SQLSTATE('X', 'X', '0', '0', '2') as isize,
}

impl Display for PgSqlErrorCode {
    fn fmt(&self, f: &mut Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

impl From<i32> for PgSqlErrorCode {
    fn from(error_code: i32) -> Self {
        (error_code as isize).into()
    }
}

impl From<isize> for PgSqlErrorCode {
    fn from(error_code: isize) -> Self {
        match error_code {
            x if x == PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION as isize => {
                PgSqlErrorCode::ERRCODE_SUCCESSFUL_COMPLETION
            }

            x if x == PgSqlErrorCode::ERRCODE_WARNING as isize => PgSqlErrorCode::ERRCODE_WARNING,
            x if x == PgSqlErrorCode::ERRCODE_WARNING_DYNAMIC_RESULT_SETS_RETURNED as isize => {
                PgSqlErrorCode::ERRCODE_WARNING_DYNAMIC_RESULT_SETS_RETURNED
            }
            x if x == PgSqlErrorCode::ERRCODE_WARNING_IMPLICIT_ZERO_BIT_PADDING as isize => {
                PgSqlErrorCode::ERRCODE_WARNING_IMPLICIT_ZERO_BIT_PADDING
            }
            x if x
                == PgSqlErrorCode::ERRCODE_WARNING_NULL_VALUE_ELIMINATED_IN_SET_FUNCTION
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_WARNING_NULL_VALUE_ELIMINATED_IN_SET_FUNCTION
            }

            x if x == PgSqlErrorCode::ERRCODE_WARNING_PRIVILEGE_NOT_GRANTED as isize => {
                PgSqlErrorCode::ERRCODE_WARNING_PRIVILEGE_NOT_GRANTED
            }
            x if x == PgSqlErrorCode::ERRCODE_WARNING_PRIVILEGE_NOT_REVOKED as isize => {
                PgSqlErrorCode::ERRCODE_WARNING_PRIVILEGE_NOT_REVOKED
            }
            x if x == PgSqlErrorCode::ERRCODE_WARNING_STRING_DATA_RIGHT_TRUNCATION as isize => {
                PgSqlErrorCode::ERRCODE_WARNING_STRING_DATA_RIGHT_TRUNCATION
            }
            x if x == PgSqlErrorCode::ERRCODE_WARNING_DEPRECATED_FEATURE as isize => {
                PgSqlErrorCode::ERRCODE_WARNING_DEPRECATED_FEATURE
            }

            x if x == PgSqlErrorCode::ERRCODE_NO_DATA as isize => PgSqlErrorCode::ERRCODE_NO_DATA,
            x if x
                == PgSqlErrorCode::ERRCODE_NO_ADDITIONAL_DYNAMIC_RESULT_SETS_RETURNED as isize =>
            {
                PgSqlErrorCode::ERRCODE_NO_ADDITIONAL_DYNAMIC_RESULT_SETS_RETURNED
            }

            x if x == PgSqlErrorCode::ERRCODE_SQL_STATEMENT_NOT_YET_COMPLETE as isize => {
                PgSqlErrorCode::ERRCODE_SQL_STATEMENT_NOT_YET_COMPLETE
            }

            x if x == PgSqlErrorCode::ERRCODE_CONNECTION_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_CONNECTION_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_CONNECTION_DOES_NOT_EXIST as isize => {
                PgSqlErrorCode::ERRCODE_CONNECTION_DOES_NOT_EXIST
            }
            x if x == PgSqlErrorCode::ERRCODE_CONNECTION_FAILURE as isize => {
                PgSqlErrorCode::ERRCODE_CONNECTION_FAILURE
            }
            x if x
                == PgSqlErrorCode::ERRCODE_SQLCLIENT_UNABLE_TO_ESTABLISH_SQLCONNECTION as isize =>
            {
                PgSqlErrorCode::ERRCODE_SQLCLIENT_UNABLE_TO_ESTABLISH_SQLCONNECTION
            }

            x if x
                == PgSqlErrorCode::ERRCODE_SQLSERVER_REJECTED_ESTABLISHMENT_OF_SQLCONNECTION
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_SQLSERVER_REJECTED_ESTABLISHMENT_OF_SQLCONNECTION
            }

            x if x == PgSqlErrorCode::ERRCODE_TRANSACTION_RESOLUTION_UNKNOWN as isize => {
                PgSqlErrorCode::ERRCODE_TRANSACTION_RESOLUTION_UNKNOWN
            }
            x if x == PgSqlErrorCode::ERRCODE_PROTOCOL_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_PROTOCOL_VIOLATION
            }

            x if x == PgSqlErrorCode::ERRCODE_TRIGGERED_ACTION_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_TRIGGERED_ACTION_EXCEPTION
            }

            x if x == PgSqlErrorCode::ERRCODE_FEATURE_NOT_SUPPORTED as isize => {
                PgSqlErrorCode::ERRCODE_FEATURE_NOT_SUPPORTED
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_TRANSACTION_INITIATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TRANSACTION_INITIATION
            }

            x if x == PgSqlErrorCode::ERRCODE_LOCATOR_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_LOCATOR_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_L_E_INVALID_SPECIFICATION as isize => {
                PgSqlErrorCode::ERRCODE_L_E_INVALID_SPECIFICATION
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_GRANTOR as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_GRANTOR
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_GRANT_OPERATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_GRANT_OPERATION
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_ROLE_SPECIFICATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ROLE_SPECIFICATION
            }

            x if x == PgSqlErrorCode::ERRCODE_DIAGNOSTICS_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_DIAGNOSTICS_EXCEPTION
            }
            x if x
                == PgSqlErrorCode::ERRCODE_STACKED_DIAGNOSTICS_ACCESSED_WITHOUT_ACTIVE_HANDLER
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_STACKED_DIAGNOSTICS_ACCESSED_WITHOUT_ACTIVE_HANDLER
            }

            x if x == PgSqlErrorCode::ERRCODE_CASE_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_CASE_NOT_FOUND
            }

            x if x == PgSqlErrorCode::ERRCODE_CARDINALITY_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_CARDINALITY_VIOLATION
            }

            x if x == PgSqlErrorCode::ERRCODE_DATA_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_DATA_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_ARRAY_ELEMENT_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_ARRAY_ELEMENT_ERROR
            }
            //    x if x == PgSqlErrorCode::ERRCODE_ARRAY_SUBSCRIPT_ERROR as isize => PgSqlErrorCode::ERRCODE_ARRAY_SUBSCRIPT_ERROR,
            x if x == PgSqlErrorCode::ERRCODE_CHARACTER_NOT_IN_REPERTOIRE as isize => {
                PgSqlErrorCode::ERRCODE_CHARACTER_NOT_IN_REPERTOIRE
            }
            x if x == PgSqlErrorCode::ERRCODE_DATETIME_FIELD_OVERFLOW as isize => {
                PgSqlErrorCode::ERRCODE_DATETIME_FIELD_OVERFLOW
            }
            //    x if x == PgSqlErrorCode::ERRCODE_DATETIME_VALUE_OUT_OF_RANGE as isize => PgSqlErrorCode::ERRCODE_DATETIME_VALUE_OUT_OF_RANGE,
            x if x == PgSqlErrorCode::ERRCODE_DIVISION_BY_ZERO as isize => {
                PgSqlErrorCode::ERRCODE_DIVISION_BY_ZERO
            }
            x if x == PgSqlErrorCode::ERRCODE_ERROR_IN_ASSIGNMENT as isize => {
                PgSqlErrorCode::ERRCODE_ERROR_IN_ASSIGNMENT
            }
            x if x == PgSqlErrorCode::ERRCODE_ESCAPE_CHARACTER_CONFLICT as isize => {
                PgSqlErrorCode::ERRCODE_ESCAPE_CHARACTER_CONFLICT
            }
            x if x == PgSqlErrorCode::ERRCODE_INDICATOR_OVERFLOW as isize => {
                PgSqlErrorCode::ERRCODE_INDICATOR_OVERFLOW
            }
            x if x == PgSqlErrorCode::ERRCODE_INTERVAL_FIELD_OVERFLOW as isize => {
                PgSqlErrorCode::ERRCODE_INTERVAL_FIELD_OVERFLOW
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_LOG as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_LOG
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_NTILE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_NTILE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_NTH_VALUE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_NTH_VALUE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION
            }
            x if x
                == PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION as isize =>
            {
                PgSqlErrorCode::ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_CHARACTER_VALUE_FOR_CAST as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_CHARACTER_VALUE_FOR_CAST
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_DATETIME_FORMAT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_DATETIME_FORMAT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ESCAPE_CHARACTER as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ESCAPE_CHARACTER
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ESCAPE_OCTET as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ESCAPE_OCTET
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ESCAPE_SEQUENCE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ESCAPE_SEQUENCE
            }
            x if x == PgSqlErrorCode::ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER as isize => {
                PgSqlErrorCode::ERRCODE_NONSTANDARD_USE_OF_ESCAPE_CHARACTER
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_INDICATOR_PARAMETER_VALUE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_INDICATOR_PARAMETER_VALUE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_PARAMETER_VALUE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_PRECEDING_OR_FOLLOWING_SIZE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_PRECEDING_OR_FOLLOWING_SIZE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_REGULAR_EXPRESSION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_REGULAR_EXPRESSION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_ROW_COUNT_IN_LIMIT_CLAUSE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_ROW_COUNT_IN_LIMIT_CLAUSE
            }
            x if x
                == PgSqlErrorCode::ERRCODE_INVALID_ROW_COUNT_IN_RESULT_OFFSET_CLAUSE as isize =>
            {
                PgSqlErrorCode::ERRCODE_INVALID_ROW_COUNT_IN_RESULT_OFFSET_CLAUSE
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_TABLESAMPLE_ARGUMENT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TABLESAMPLE_ARGUMENT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_TABLESAMPLE_REPEAT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TABLESAMPLE_REPEAT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_TIME_ZONE_DISPLACEMENT_VALUE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TIME_ZONE_DISPLACEMENT_VALUE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_USE_OF_ESCAPE_CHARACTER as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_USE_OF_ESCAPE_CHARACTER
            }
            x if x == PgSqlErrorCode::ERRCODE_MOST_SPECIFIC_TYPE_MISMATCH as isize => {
                PgSqlErrorCode::ERRCODE_MOST_SPECIFIC_TYPE_MISMATCH
            }
            x if x == PgSqlErrorCode::ERRCODE_NULL_VALUE_NOT_ALLOWED as isize => {
                PgSqlErrorCode::ERRCODE_NULL_VALUE_NOT_ALLOWED
            }
            x if x == PgSqlErrorCode::ERRCODE_NULL_VALUE_NO_INDICATOR_PARAMETER as isize => {
                PgSqlErrorCode::ERRCODE_NULL_VALUE_NO_INDICATOR_PARAMETER
            }
            x if x == PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE as isize => {
                PgSqlErrorCode::ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE
            }
            x if x == PgSqlErrorCode::ERRCODE_SEQUENCE_GENERATOR_LIMIT_EXCEEDED as isize => {
                PgSqlErrorCode::ERRCODE_SEQUENCE_GENERATOR_LIMIT_EXCEEDED
            }
            x if x == PgSqlErrorCode::ERRCODE_STRING_DATA_LENGTH_MISMATCH as isize => {
                PgSqlErrorCode::ERRCODE_STRING_DATA_LENGTH_MISMATCH
            }
            x if x == PgSqlErrorCode::ERRCODE_STRING_DATA_RIGHT_TRUNCATION as isize => {
                PgSqlErrorCode::ERRCODE_STRING_DATA_RIGHT_TRUNCATION
            }
            x if x == PgSqlErrorCode::ERRCODE_SUBSTRING_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_SUBSTRING_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_TRIM_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_TRIM_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_UNTERMINATED_C_STRING as isize => {
                PgSqlErrorCode::ERRCODE_UNTERMINATED_C_STRING
            }
            x if x == PgSqlErrorCode::ERRCODE_ZERO_LENGTH_CHARACTER_STRING as isize => {
                PgSqlErrorCode::ERRCODE_ZERO_LENGTH_CHARACTER_STRING
            }
            x if x == PgSqlErrorCode::ERRCODE_FLOATING_POINT_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_FLOATING_POINT_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_TEXT_REPRESENTATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TEXT_REPRESENTATION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_BINARY_REPRESENTATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_BINARY_REPRESENTATION
            }
            x if x == PgSqlErrorCode::ERRCODE_BAD_COPY_FILE_FORMAT as isize => {
                PgSqlErrorCode::ERRCODE_BAD_COPY_FILE_FORMAT
            }
            x if x == PgSqlErrorCode::ERRCODE_UNTRANSLATABLE_CHARACTER as isize => {
                PgSqlErrorCode::ERRCODE_UNTRANSLATABLE_CHARACTER
            }
            x if x == PgSqlErrorCode::ERRCODE_NOT_AN_XML_DOCUMENT as isize => {
                PgSqlErrorCode::ERRCODE_NOT_AN_XML_DOCUMENT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_XML_DOCUMENT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_XML_DOCUMENT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_XML_CONTENT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_XML_CONTENT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_XML_COMMENT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_XML_COMMENT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_XML_PROCESSING_INSTRUCTION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_XML_PROCESSING_INSTRUCTION
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_JSON_OBJECT_KEY_VALUE as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_JSON_OBJECT_KEY_VALUE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_JSON_TEXT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_JSON_TEXT
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_SQL_JSON_SUBSCRIPT as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_SQL_JSON_SUBSCRIPT
            }
            x if x == PgSqlErrorCode::ERRCODE_MORE_THAN_ONE_SQL_JSON_ITEM as isize => {
                PgSqlErrorCode::ERRCODE_MORE_THAN_ONE_SQL_JSON_ITEM
            }
            x if x == PgSqlErrorCode::ERRCODE_NO_SQL_JSON_ITEM as isize => {
                PgSqlErrorCode::ERRCODE_NO_SQL_JSON_ITEM
            }
            x if x == PgSqlErrorCode::ERRCODE_NON_NUMERIC_SQL_JSON_ITEM as isize => {
                PgSqlErrorCode::ERRCODE_NON_NUMERIC_SQL_JSON_ITEM
            }
            x if x == PgSqlErrorCode::ERRCODE_NON_UNIQUE_KEYS_IN_A_JSON_OBJECT as isize => {
                PgSqlErrorCode::ERRCODE_NON_UNIQUE_KEYS_IN_A_JSON_OBJECT
            }
            x if x == PgSqlErrorCode::ERRCODE_SINGLETON_SQL_JSON_ITEM_REQUIRED as isize => {
                PgSqlErrorCode::ERRCODE_SINGLETON_SQL_JSON_ITEM_REQUIRED
            }
            x if x == PgSqlErrorCode::ERRCODE_SQL_JSON_ARRAY_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_SQL_JSON_ARRAY_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_SQL_JSON_MEMBER_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_SQL_JSON_MEMBER_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_SQL_JSON_NUMBER_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_SQL_JSON_NUMBER_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_SQL_JSON_OBJECT_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_SQL_JSON_OBJECT_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_TOO_MANY_JSON_ARRAY_ELEMENTS as isize => {
                PgSqlErrorCode::ERRCODE_TOO_MANY_JSON_ARRAY_ELEMENTS
            }
            x if x == PgSqlErrorCode::ERRCODE_TOO_MANY_JSON_OBJECT_MEMBERS as isize => {
                PgSqlErrorCode::ERRCODE_TOO_MANY_JSON_OBJECT_MEMBERS
            }
            x if x == PgSqlErrorCode::ERRCODE_SQL_JSON_SCALAR_REQUIRED as isize => {
                PgSqlErrorCode::ERRCODE_SQL_JSON_SCALAR_REQUIRED
            }

            x if x == PgSqlErrorCode::ERRCODE_INTEGRITY_CONSTRAINT_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_INTEGRITY_CONSTRAINT_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_RESTRICT_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_RESTRICT_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_NOT_NULL_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_NOT_NULL_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_FOREIGN_KEY_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_FOREIGN_KEY_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_UNIQUE_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_UNIQUE_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_CHECK_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_CHECK_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_EXCLUSION_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_EXCLUSION_VIOLATION
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_CURSOR_STATE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_CURSOR_STATE
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_TRANSACTION_STATE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TRANSACTION_STATE
            }
            x if x == PgSqlErrorCode::ERRCODE_ACTIVE_SQL_TRANSACTION as isize => {
                PgSqlErrorCode::ERRCODE_ACTIVE_SQL_TRANSACTION
            }
            x if x == PgSqlErrorCode::ERRCODE_BRANCH_TRANSACTION_ALREADY_ACTIVE as isize => {
                PgSqlErrorCode::ERRCODE_BRANCH_TRANSACTION_ALREADY_ACTIVE
            }
            x if x
                == PgSqlErrorCode::ERRCODE_HELD_CURSOR_REQUIRES_SAME_ISOLATION_LEVEL as isize =>
            {
                PgSqlErrorCode::ERRCODE_HELD_CURSOR_REQUIRES_SAME_ISOLATION_LEVEL
            }

            x if x
                == PgSqlErrorCode::ERRCODE_INAPPROPRIATE_ACCESS_MODE_FOR_BRANCH_TRANSACTION
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_INAPPROPRIATE_ACCESS_MODE_FOR_BRANCH_TRANSACTION
            }

            x if x
                == PgSqlErrorCode::ERRCODE_INAPPROPRIATE_ISOLATION_LEVEL_FOR_BRANCH_TRANSACTION
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_INAPPROPRIATE_ISOLATION_LEVEL_FOR_BRANCH_TRANSACTION
            }

            x if x
                == PgSqlErrorCode::ERRCODE_NO_ACTIVE_SQL_TRANSACTION_FOR_BRANCH_TRANSACTION
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_NO_ACTIVE_SQL_TRANSACTION_FOR_BRANCH_TRANSACTION
            }

            x if x == PgSqlErrorCode::ERRCODE_READ_ONLY_SQL_TRANSACTION as isize => {
                PgSqlErrorCode::ERRCODE_READ_ONLY_SQL_TRANSACTION
            }
            x if x
                == PgSqlErrorCode::ERRCODE_SCHEMA_AND_DATA_STATEMENT_MIXING_NOT_SUPPORTED
                    as isize =>
            {
                PgSqlErrorCode::ERRCODE_SCHEMA_AND_DATA_STATEMENT_MIXING_NOT_SUPPORTED
            }

            x if x == PgSqlErrorCode::ERRCODE_NO_ACTIVE_SQL_TRANSACTION as isize => {
                PgSqlErrorCode::ERRCODE_NO_ACTIVE_SQL_TRANSACTION
            }
            x if x == PgSqlErrorCode::ERRCODE_IN_FAILED_SQL_TRANSACTION as isize => {
                PgSqlErrorCode::ERRCODE_IN_FAILED_SQL_TRANSACTION
            }
            x if x == PgSqlErrorCode::ERRCODE_IDLE_IN_TRANSACTION_SESSION_TIMEOUT as isize => {
                PgSqlErrorCode::ERRCODE_IDLE_IN_TRANSACTION_SESSION_TIMEOUT
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_SQL_STATEMENT_NAME as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_SQL_STATEMENT_NAME
            }

            x if x == PgSqlErrorCode::ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_TRIGGERED_DATA_CHANGE_VIOLATION
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_AUTHORIZATION_SPECIFICATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_AUTHORIZATION_SPECIFICATION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_PASSWORD as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_PASSWORD
            }

            x if x
                == PgSqlErrorCode::ERRCODE_DEPENDENT_PRIVILEGE_DESCRIPTORS_STILL_EXIST as isize =>
            {
                PgSqlErrorCode::ERRCODE_DEPENDENT_PRIVILEGE_DESCRIPTORS_STILL_EXIST
            }

            x if x == PgSqlErrorCode::ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST as isize => {
                PgSqlErrorCode::ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_TRANSACTION_TERMINATION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TRANSACTION_TERMINATION
            }

            x if x == PgSqlErrorCode::ERRCODE_SQL_ROUTINE_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_SQL_ROUTINE_EXCEPTION
            }
            x if x
                == PgSqlErrorCode::ERRCODE_S_R_E_FUNCTION_EXECUTED_NO_RETURN_STATEMENT as isize =>
            {
                PgSqlErrorCode::ERRCODE_S_R_E_FUNCTION_EXECUTED_NO_RETURN_STATEMENT
            }

            x if x == PgSqlErrorCode::ERRCODE_S_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED as isize => {
                PgSqlErrorCode::ERRCODE_S_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED
            }

            x if x == PgSqlErrorCode::ERRCODE_S_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED as isize => {
                PgSqlErrorCode::ERRCODE_S_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED
            }

            x if x == PgSqlErrorCode::ERRCODE_S_R_E_READING_SQL_DATA_NOT_PERMITTED as isize => {
                PgSqlErrorCode::ERRCODE_S_R_E_READING_SQL_DATA_NOT_PERMITTED
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_CURSOR_NAME as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_CURSOR_NAME
            }

            x if x == PgSqlErrorCode::ERRCODE_EXTERNAL_ROUTINE_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_EXTERNAL_ROUTINE_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_E_CONTAINING_SQL_NOT_PERMITTED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_E_CONTAINING_SQL_NOT_PERMITTED
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_E_MODIFYING_SQL_DATA_NOT_PERMITTED
            }

            x if x == PgSqlErrorCode::ERRCODE_E_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_E_PROHIBITED_SQL_STATEMENT_ATTEMPTED
            }

            x if x == PgSqlErrorCode::ERRCODE_E_R_E_READING_SQL_DATA_NOT_PERMITTED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_E_READING_SQL_DATA_NOT_PERMITTED
            }

            x if x == PgSqlErrorCode::ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_EXTERNAL_ROUTINE_INVOCATION_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_I_E_INVALID_SQLSTATE_RETURNED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_I_E_INVALID_SQLSTATE_RETURNED
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_I_E_NULL_VALUE_NOT_ALLOWED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_I_E_NULL_VALUE_NOT_ALLOWED
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_I_E_TRIGGER_PROTOCOL_VIOLATED
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_I_E_SRF_PROTOCOL_VIOLATED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_I_E_SRF_PROTOCOL_VIOLATED
            }
            x if x == PgSqlErrorCode::ERRCODE_E_R_I_E_EVENT_TRIGGER_PROTOCOL_VIOLATED as isize => {
                PgSqlErrorCode::ERRCODE_E_R_I_E_EVENT_TRIGGER_PROTOCOL_VIOLATED
            }

            x if x == PgSqlErrorCode::ERRCODE_SAVEPOINT_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_SAVEPOINT_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_S_E_INVALID_SPECIFICATION as isize => {
                PgSqlErrorCode::ERRCODE_S_E_INVALID_SPECIFICATION
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_CATALOG_NAME as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_CATALOG_NAME
            }

            x if x == PgSqlErrorCode::ERRCODE_INVALID_SCHEMA_NAME as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_SCHEMA_NAME
            }

            x if x == PgSqlErrorCode::ERRCODE_TRANSACTION_ROLLBACK as isize => {
                PgSqlErrorCode::ERRCODE_TRANSACTION_ROLLBACK
            }
            x if x == PgSqlErrorCode::ERRCODE_T_R_INTEGRITY_CONSTRAINT_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_T_R_INTEGRITY_CONSTRAINT_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_T_R_SERIALIZATION_FAILURE as isize => {
                PgSqlErrorCode::ERRCODE_T_R_SERIALIZATION_FAILURE
            }
            x if x == PgSqlErrorCode::ERRCODE_T_R_STATEMENT_COMPLETION_UNKNOWN as isize => {
                PgSqlErrorCode::ERRCODE_T_R_STATEMENT_COMPLETION_UNKNOWN
            }
            x if x == PgSqlErrorCode::ERRCODE_T_R_DEADLOCK_DETECTED as isize => {
                PgSqlErrorCode::ERRCODE_T_R_DEADLOCK_DETECTED
            }

            x if x == PgSqlErrorCode::ERRCODE_SYNTAX_ERROR_OR_ACCESS_RULE_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_SYNTAX_ERROR_OR_ACCESS_RULE_VIOLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_SYNTAX_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_SYNTAX_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_INSUFFICIENT_PRIVILEGE as isize => {
                PgSqlErrorCode::ERRCODE_INSUFFICIENT_PRIVILEGE
            }
            x if x == PgSqlErrorCode::ERRCODE_CANNOT_COERCE as isize => {
                PgSqlErrorCode::ERRCODE_CANNOT_COERCE
            }
            x if x == PgSqlErrorCode::ERRCODE_GROUPING_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_GROUPING_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_WINDOWING_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_WINDOWING_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_RECURSION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_RECURSION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_FOREIGN_KEY as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_FOREIGN_KEY
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_NAME as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_NAME
            }
            x if x == PgSqlErrorCode::ERRCODE_NAME_TOO_LONG as isize => {
                PgSqlErrorCode::ERRCODE_NAME_TOO_LONG
            }
            x if x == PgSqlErrorCode::ERRCODE_RESERVED_NAME as isize => {
                PgSqlErrorCode::ERRCODE_RESERVED_NAME
            }
            x if x == PgSqlErrorCode::ERRCODE_DATATYPE_MISMATCH as isize => {
                PgSqlErrorCode::ERRCODE_DATATYPE_MISMATCH
            }
            x if x == PgSqlErrorCode::ERRCODE_INDETERMINATE_DATATYPE as isize => {
                PgSqlErrorCode::ERRCODE_INDETERMINATE_DATATYPE
            }
            x if x == PgSqlErrorCode::ERRCODE_COLLATION_MISMATCH as isize => {
                PgSqlErrorCode::ERRCODE_COLLATION_MISMATCH
            }
            x if x == PgSqlErrorCode::ERRCODE_INDETERMINATE_COLLATION as isize => {
                PgSqlErrorCode::ERRCODE_INDETERMINATE_COLLATION
            }
            x if x == PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE as isize => {
                PgSqlErrorCode::ERRCODE_WRONG_OBJECT_TYPE
            }
            x if x == PgSqlErrorCode::ERRCODE_GENERATED_ALWAYS as isize => {
                PgSqlErrorCode::ERRCODE_GENERATED_ALWAYS
            }
            x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_COLUMN as isize => {
                PgSqlErrorCode::ERRCODE_UNDEFINED_COLUMN
            }
            //    x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_CURSOR as isize => PgSqlErrorCode::ERRCODE_UNDEFINED_CURSOR,
            //    x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_DATABASE as isize => PgSqlErrorCode::ERRCODE_UNDEFINED_DATABASE,
            x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_FUNCTION as isize => {
                PgSqlErrorCode::ERRCODE_UNDEFINED_FUNCTION
            }
            //    x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_PSTATEMENT as isize => PgSqlErrorCode::ERRCODE_UNDEFINED_PSTATEMENT,
            //    x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_SCHEMA as isize => PgSqlErrorCode::ERRCODE_UNDEFINED_SCHEMA,
            x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_TABLE as isize => {
                PgSqlErrorCode::ERRCODE_UNDEFINED_TABLE
            }
            x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_PARAMETER as isize => {
                PgSqlErrorCode::ERRCODE_UNDEFINED_PARAMETER
            }
            x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_OBJECT as isize => {
                PgSqlErrorCode::ERRCODE_UNDEFINED_OBJECT
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_COLUMN as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_COLUMN
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_CURSOR as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_CURSOR
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_DATABASE as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_DATABASE
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_FUNCTION as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_FUNCTION
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_PSTATEMENT as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_PSTATEMENT
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_SCHEMA as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_SCHEMA
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_TABLE as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_TABLE
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_ALIAS as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_ALIAS
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_OBJECT as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_OBJECT
            }
            x if x == PgSqlErrorCode::ERRCODE_AMBIGUOUS_COLUMN as isize => {
                PgSqlErrorCode::ERRCODE_AMBIGUOUS_COLUMN
            }
            x if x == PgSqlErrorCode::ERRCODE_AMBIGUOUS_FUNCTION as isize => {
                PgSqlErrorCode::ERRCODE_AMBIGUOUS_FUNCTION
            }
            x if x == PgSqlErrorCode::ERRCODE_AMBIGUOUS_PARAMETER as isize => {
                PgSqlErrorCode::ERRCODE_AMBIGUOUS_PARAMETER
            }
            x if x == PgSqlErrorCode::ERRCODE_AMBIGUOUS_ALIAS as isize => {
                PgSqlErrorCode::ERRCODE_AMBIGUOUS_ALIAS
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_COLUMN_REFERENCE as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_COLUMN_REFERENCE
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_COLUMN_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_COLUMN_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_CURSOR_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_CURSOR_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_DATABASE_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_DATABASE_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_FUNCTION_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_FUNCTION_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_PSTATEMENT_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_PSTATEMENT_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_SCHEMA_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_SCHEMA_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_TABLE_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_TABLE_DEFINITION
            }
            x if x == PgSqlErrorCode::ERRCODE_INVALID_OBJECT_DEFINITION as isize => {
                PgSqlErrorCode::ERRCODE_INVALID_OBJECT_DEFINITION
            }

            x if x == PgSqlErrorCode::ERRCODE_WITH_CHECK_OPTION_VIOLATION as isize => {
                PgSqlErrorCode::ERRCODE_WITH_CHECK_OPTION_VIOLATION
            }

            x if x == PgSqlErrorCode::ERRCODE_INSUFFICIENT_RESOURCES as isize => {
                PgSqlErrorCode::ERRCODE_INSUFFICIENT_RESOURCES
            }
            x if x == PgSqlErrorCode::ERRCODE_DISK_FULL as isize => {
                PgSqlErrorCode::ERRCODE_DISK_FULL
            }
            x if x == PgSqlErrorCode::ERRCODE_OUT_OF_MEMORY as isize => {
                PgSqlErrorCode::ERRCODE_OUT_OF_MEMORY
            }
            x if x == PgSqlErrorCode::ERRCODE_TOO_MANY_CONNECTIONS as isize => {
                PgSqlErrorCode::ERRCODE_TOO_MANY_CONNECTIONS
            }
            x if x == PgSqlErrorCode::ERRCODE_CONFIGURATION_LIMIT_EXCEEDED as isize => {
                PgSqlErrorCode::ERRCODE_CONFIGURATION_LIMIT_EXCEEDED
            }

            x if x == PgSqlErrorCode::ERRCODE_PROGRAM_LIMIT_EXCEEDED as isize => {
                PgSqlErrorCode::ERRCODE_PROGRAM_LIMIT_EXCEEDED
            }
            x if x == PgSqlErrorCode::ERRCODE_STATEMENT_TOO_COMPLEX as isize => {
                PgSqlErrorCode::ERRCODE_STATEMENT_TOO_COMPLEX
            }
            x if x == PgSqlErrorCode::ERRCODE_TOO_MANY_COLUMNS as isize => {
                PgSqlErrorCode::ERRCODE_TOO_MANY_COLUMNS
            }
            x if x == PgSqlErrorCode::ERRCODE_TOO_MANY_ARGUMENTS as isize => {
                PgSqlErrorCode::ERRCODE_TOO_MANY_ARGUMENTS
            }

            x if x == PgSqlErrorCode::ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE as isize => {
                PgSqlErrorCode::ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE
            }
            x if x == PgSqlErrorCode::ERRCODE_OBJECT_IN_USE as isize => {
                PgSqlErrorCode::ERRCODE_OBJECT_IN_USE
            }
            x if x == PgSqlErrorCode::ERRCODE_CANT_CHANGE_RUNTIME_PARAM as isize => {
                PgSqlErrorCode::ERRCODE_CANT_CHANGE_RUNTIME_PARAM
            }
            x if x == PgSqlErrorCode::ERRCODE_LOCK_NOT_AVAILABLE as isize => {
                PgSqlErrorCode::ERRCODE_LOCK_NOT_AVAILABLE
            }
            x if x == PgSqlErrorCode::ERRCODE_UNSAFE_NEW_ENUM_VALUE_USAGE as isize => {
                PgSqlErrorCode::ERRCODE_UNSAFE_NEW_ENUM_VALUE_USAGE
            }

            x if x == PgSqlErrorCode::ERRCODE_OPERATOR_INTERVENTION as isize => {
                PgSqlErrorCode::ERRCODE_OPERATOR_INTERVENTION
            }
            x if x == PgSqlErrorCode::ERRCODE_QUERY_CANCELED as isize => {
                PgSqlErrorCode::ERRCODE_QUERY_CANCELED
            }
            x if x == PgSqlErrorCode::ERRCODE_ADMIN_SHUTDOWN as isize => {
                PgSqlErrorCode::ERRCODE_ADMIN_SHUTDOWN
            }
            x if x == PgSqlErrorCode::ERRCODE_CRASH_SHUTDOWN as isize => {
                PgSqlErrorCode::ERRCODE_CRASH_SHUTDOWN
            }
            x if x == PgSqlErrorCode::ERRCODE_CANNOT_CONNECT_NOW as isize => {
                PgSqlErrorCode::ERRCODE_CANNOT_CONNECT_NOW
            }
            x if x == PgSqlErrorCode::ERRCODE_DATABASE_DROPPED as isize => {
                PgSqlErrorCode::ERRCODE_DATABASE_DROPPED
            }

            x if x == PgSqlErrorCode::ERRCODE_SYSTEM_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_SYSTEM_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_IO_ERROR as isize => PgSqlErrorCode::ERRCODE_IO_ERROR,
            x if x == PgSqlErrorCode::ERRCODE_UNDEFINED_FILE as isize => {
                PgSqlErrorCode::ERRCODE_UNDEFINED_FILE
            }
            x if x == PgSqlErrorCode::ERRCODE_DUPLICATE_FILE as isize => {
                PgSqlErrorCode::ERRCODE_DUPLICATE_FILE
            }

            x if x == PgSqlErrorCode::ERRCODE_SNAPSHOT_TOO_OLD as isize => {
                PgSqlErrorCode::ERRCODE_SNAPSHOT_TOO_OLD
            }

            x if x == PgSqlErrorCode::ERRCODE_CONFIG_FILE_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_CONFIG_FILE_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_LOCK_FILE_EXISTS as isize => {
                PgSqlErrorCode::ERRCODE_LOCK_FILE_EXISTS
            }

            x if x == PgSqlErrorCode::ERRCODE_FDW_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_FDW_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_COLUMN_NAME_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_FDW_COLUMN_NAME_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_DYNAMIC_PARAMETER_VALUE_NEEDED as isize => {
                PgSqlErrorCode::ERRCODE_FDW_DYNAMIC_PARAMETER_VALUE_NEEDED
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_FUNCTION_SEQUENCE_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_FDW_FUNCTION_SEQUENCE_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INCONSISTENT_DESCRIPTOR_INFORMATION as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INCONSISTENT_DESCRIPTOR_INFORMATION
            }

            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_ATTRIBUTE_VALUE as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_ATTRIBUTE_VALUE
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_COLUMN_NAME as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_COLUMN_NAME
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_COLUMN_NUMBER as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_COLUMN_NUMBER
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_DATA_TYPE as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_DATA_TYPE
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_DATA_TYPE_DESCRIPTORS as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_DATA_TYPE_DESCRIPTORS
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_DESCRIPTOR_FIELD_IDENTIFIER as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_DESCRIPTOR_FIELD_IDENTIFIER
            }

            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_HANDLE as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_HANDLE
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_OPTION_INDEX as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_OPTION_INDEX
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_OPTION_NAME as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_OPTION_NAME
            }
            x if x
                == PgSqlErrorCode::ERRCODE_FDW_INVALID_STRING_LENGTH_OR_BUFFER_LENGTH as isize =>
            {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_STRING_LENGTH_OR_BUFFER_LENGTH
            }

            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_STRING_FORMAT as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_STRING_FORMAT
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_INVALID_USE_OF_NULL_POINTER as isize => {
                PgSqlErrorCode::ERRCODE_FDW_INVALID_USE_OF_NULL_POINTER
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_TOO_MANY_HANDLES as isize => {
                PgSqlErrorCode::ERRCODE_FDW_TOO_MANY_HANDLES
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_OUT_OF_MEMORY as isize => {
                PgSqlErrorCode::ERRCODE_FDW_OUT_OF_MEMORY
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_NO_SCHEMAS as isize => {
                PgSqlErrorCode::ERRCODE_FDW_NO_SCHEMAS
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_OPTION_NAME_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_FDW_OPTION_NAME_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_REPLY_HANDLE as isize => {
                PgSqlErrorCode::ERRCODE_FDW_REPLY_HANDLE
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_SCHEMA_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_FDW_SCHEMA_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_TABLE_NOT_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_FDW_TABLE_NOT_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_UNABLE_TO_CREATE_EXECUTION as isize => {
                PgSqlErrorCode::ERRCODE_FDW_UNABLE_TO_CREATE_EXECUTION
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_UNABLE_TO_CREATE_REPLY as isize => {
                PgSqlErrorCode::ERRCODE_FDW_UNABLE_TO_CREATE_REPLY
            }
            x if x == PgSqlErrorCode::ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION as isize => {
                PgSqlErrorCode::ERRCODE_FDW_UNABLE_TO_ESTABLISH_CONNECTION
            }

            x if x == PgSqlErrorCode::ERRCODE_PLPGSQL_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_PLPGSQL_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_RAISE_EXCEPTION as isize => {
                PgSqlErrorCode::ERRCODE_RAISE_EXCEPTION
            }
            x if x == PgSqlErrorCode::ERRCODE_NO_DATA_FOUND as isize => {
                PgSqlErrorCode::ERRCODE_NO_DATA_FOUND
            }
            x if x == PgSqlErrorCode::ERRCODE_TOO_MANY_ROWS as isize => {
                PgSqlErrorCode::ERRCODE_TOO_MANY_ROWS
            }
            x if x == PgSqlErrorCode::ERRCODE_ASSERT_FAILURE as isize => {
                PgSqlErrorCode::ERRCODE_ASSERT_FAILURE
            }

            x if x == PgSqlErrorCode::ERRCODE_INTERNAL_ERROR as isize => {
                PgSqlErrorCode::ERRCODE_INTERNAL_ERROR
            }
            x if x == PgSqlErrorCode::ERRCODE_DATA_CORRUPTED as isize => {
                PgSqlErrorCode::ERRCODE_DATA_CORRUPTED
            }
            x if x == PgSqlErrorCode::ERRCODE_INDEX_CORRUPTED as isize => {
                PgSqlErrorCode::ERRCODE_INDEX_CORRUPTED
            }

            _ => PgSqlErrorCode::ERRCODE_INTERNAL_ERROR,
        }
    }
}

#[allow(non_snake_case)]
#[inline]
const fn PGSIXBIT(ch: i32) -> i32 {
    ((ch) - '0' as i32) & 0x3F
}

#[allow(non_snake_case)]
#[inline]
const fn MAKE_SQLSTATE(ch1: char, ch2: char, ch3: char, ch4: char, ch5: char) -> i32 {
    PGSIXBIT(ch1 as i32)
        + (PGSIXBIT(ch2 as i32) << 6)
        + (PGSIXBIT(ch3 as i32) << 12)
        + (PGSIXBIT(ch4 as i32) << 18)
        + (PGSIXBIT(ch5 as i32) << 24)
}
