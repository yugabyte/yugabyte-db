/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/error.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{backtrace::Backtrace, fmt::Display, io};

use bson::raw::ValueAccessError;
use deadpool_postgres::{BuildError, CreatePoolError, PoolError};
use documentdb_macros::documentdb_error_code_enum;
use openssl::error::ErrorStack;

use crate::responses::constant::pg_returned_invalid_response_message;

documentdb_error_code_enum!();

pub enum DocumentDBError {
    IoError(io::Error, Backtrace),
    DocumentDBError(ErrorCode, String, Backtrace),
    UntypedDocumentDBError(i32, String, String, Backtrace),
    PostgresError(tokio_postgres::Error, Backtrace),
    PostgresDocumentDBError(i32, String, Backtrace),
    PoolError(PoolError, Backtrace),
    CreatePoolError(CreatePoolError, Backtrace),
    BuildPoolError(BuildError, Backtrace),
    RawBsonError(bson::raw::Error, Backtrace),
    SSLError(openssl::ssl::Error, Backtrace),
    SSLErrorStack(ErrorStack, Backtrace),
    ValueAccessError(ValueAccessError, Backtrace),
}

impl DocumentDBError {
    pub fn parse_failure<'a, E: std::fmt::Display>() -> impl Fn(E) -> Self + 'a {
        move |e| DocumentDBError::bad_value(format!("Failed to parse: {e}"))
    }

    pub fn pg_response_empty() -> Self {
        DocumentDBError::internal_error("PG returned no rows in response".to_string())
    }

    pub fn pg_response_invalid(e: ValueAccessError) -> Self {
        DocumentDBError::internal_error(pg_returned_invalid_response_message(e))
    }

    pub fn sasl_payload_invalid() -> Self {
        DocumentDBError::authentication_failed("Sasl payload invalid.".to_string())
    }

    pub fn unauthorized(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::Unauthorized, msg, Backtrace::capture())
    }

    pub fn authentication_failed(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::AuthenticationFailed, msg, Backtrace::capture())
    }

    pub fn bad_value(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::BadValue, msg, Backtrace::capture())
    }

    pub fn internal_error(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::InternalError, msg, Backtrace::capture())
    }

    pub fn type_mismatch(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::TypeMismatch, msg, Backtrace::capture())
    }

    pub fn user_not_found(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::UserNotFound, msg, Backtrace::capture())
    }

    pub fn role_not_found(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::RoleNotFound, msg, Backtrace::capture())
    }

    pub fn duplicate_user(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::Location51003, msg, Backtrace::capture())
    }

    pub fn duplicate_role(msg: String) -> Self {
        DocumentDBError::DocumentDBError(ErrorCode::Location51002, msg, Backtrace::capture())
    }

    pub fn reauthentication_required(msg: String) -> Self {
        DocumentDBError::DocumentDBError(
            ErrorCode::ReauthenticationRequired,
            msg,
            Backtrace::capture(),
        )
    }

    #[allow(clippy::self_named_constructors)]
    pub fn documentdb_error(e: ErrorCode, msg: String) -> Self {
        DocumentDBError::DocumentDBError(e, msg, Backtrace::capture())
    }

    pub fn error_code_enum(&self) -> Option<ErrorCode> {
        match self {
            DocumentDBError::DocumentDBError(code, _, _) => Some(*code),
            DocumentDBError::UntypedDocumentDBError(code, _, _, _) => ErrorCode::from_i32(*code),
            _ => None,
        }
    }
}

/// The result type for all methods that can return an error
pub type Result<T> = std::result::Result<T, DocumentDBError>;

impl From<io::Error> for DocumentDBError {
    fn from(error: io::Error) -> Self {
        DocumentDBError::IoError(error, Backtrace::capture())
    }
}

impl From<tokio_postgres::Error> for DocumentDBError {
    fn from(error: tokio_postgres::Error) -> Self {
        DocumentDBError::PostgresError(error, Backtrace::capture())
    }
}

impl From<bson::raw::Error> for DocumentDBError {
    fn from(error: bson::raw::Error) -> Self {
        DocumentDBError::RawBsonError(error, Backtrace::capture())
    }
}

impl From<PoolError> for DocumentDBError {
    fn from(error: PoolError) -> Self {
        DocumentDBError::PoolError(error, Backtrace::capture())
    }
}

impl From<CreatePoolError> for DocumentDBError {
    fn from(error: CreatePoolError) -> Self {
        DocumentDBError::CreatePoolError(error, Backtrace::capture())
    }
}

impl From<BuildError> for DocumentDBError {
    fn from(error: BuildError) -> Self {
        DocumentDBError::BuildPoolError(error, Backtrace::capture())
    }
}

impl From<ErrorStack> for DocumentDBError {
    fn from(error: ErrorStack) -> Self {
        DocumentDBError::SSLErrorStack(error, Backtrace::capture())
    }
}

impl From<openssl::ssl::Error> for DocumentDBError {
    fn from(error: openssl::ssl::Error) -> Self {
        DocumentDBError::SSLError(error, Backtrace::capture())
    }
}

impl From<ValueAccessError> for DocumentDBError {
    fn from(error: ValueAccessError) -> Self {
        DocumentDBError::ValueAccessError(error, Backtrace::capture())
    }
}

impl Display for ErrorCode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{self:?}")
    }
}

// When DocumentDBError is logged with {e} style, this Display trait here is used.
// To ensure the PII content of the DocumentDBError is not logged, always redirect to the PII free Debug implementation.
impl Display for DocumentDBError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f) // reuse Debug impl
    }
}

// When DocumentDBError is logged with {e:?} style, this Debug trait here is used.
// DocumentDBError's message field contains PII content and should not be logged.
impl std::fmt::Debug for DocumentDBError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DocumentDBError::IoError(error, backtrace) => f
                .debug_struct("IoError")
                .field("error", error)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::DocumentDBError(code, msg, backtrace) => f
                .debug_struct("DocumentDBError")
                .field("code", code)
                // TODO: Redact message when DocumentDBError::DocumentDBError supports err_hint field.
                .field("message", msg)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::UntypedDocumentDBError(code, msg, code_name, backtrace) => f
                .debug_struct("UntypedDocumentDBError")
                .field("code", code)
                // TODO: Redact message when DocumentDBError::UntypedDocumentDBError supports err_hint field.
                .field("message", msg)
                .field("code_name", code_name)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::PostgresError(error, backtrace) => {
                if let Some(dbe) = error.as_db_error() {
                    f.debug_struct("PostgresError")
                        .field("sql_state", &dbe.code())
                        .field("message", &"[REDACTED]")
                        .field("hint", &dbe.hint())
                        .field("backtrace", backtrace)
                        .finish()
                } else {
                    f.debug_struct("PostgresError")
                        .field(
                            "error_type",
                            &std::any::type_name::<tokio_postgres::Error>(),
                        )
                        .field("backtrace", backtrace)
                        .finish()
                }
            }
            DocumentDBError::PostgresDocumentDBError(code, _msg, backtrace) => f
                .debug_struct("PostgresDocumentDBError")
                .field("code", code)
                .field("message", &"[REDACTED]")
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::PoolError(error, backtrace) => f
                .debug_struct("PoolError")
                .field("error", error)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::CreatePoolError(error, backtrace) => f
                .debug_struct("CreatePoolError")
                .field("error", error)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::BuildPoolError(error, backtrace) => f
                .debug_struct("BuildPoolError")
                .field("error", error)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::RawBsonError(_error, backtrace) => f
                .debug_struct("RawBsonError")
                .field("error_type", &std::any::type_name::<bson::raw::Error>())
                .field("error_display", &"[REDACTED]")
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::SSLError(error, backtrace) => f
                .debug_struct("SSLError")
                .field("error", error)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::SSLErrorStack(error, backtrace) => f
                .debug_struct("SSLErrorStack")
                .field("error", error)
                .field("backtrace", backtrace)
                .finish(),
            DocumentDBError::ValueAccessError(_error, backtrace) => f
                .debug_struct("ValueAccessError")
                .field("error_type", &std::any::type_name::<ValueAccessError>())
                .field("error_display", &"[REDACTED]")
                .field("backtrace", backtrace)
                .finish(),
        }
    }
}
