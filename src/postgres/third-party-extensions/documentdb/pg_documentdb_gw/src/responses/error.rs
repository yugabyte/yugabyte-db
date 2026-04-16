/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/responses/error.rs
 *
 *-------------------------------------------------------------------------
 */

use std::error::Error;

use bson::{raw::ValueAccessErrorKind, RawDocumentBuf};
use deadpool_postgres::PoolError;
use serde::{Deserialize, Serialize};
use tokio_postgres::error::SqlState;

use crate::{
    context::ConnectionContext,
    error::{DocumentDBError, ErrorCode},
    protocol::OK_FAILED,
    responses::constant::{documentdb_error_message, value_access_error_message},
    responses::pg::PgResponse,
};

/// Display and Debug trait are not implemented explicitly to avoid logging PII mistakenly.
#[derive(Clone, Serialize, Deserialize)]
#[non_exhaustive]
pub struct CommandError {
    pub ok: f64,

    /// The error code in i32, e.g. InternalError has error code 1.
    pub code: i32,

    /// The error string, e.g. Internal Error.
    #[serde(rename = "codeName", default)]
    pub code_name: String,

    /// A human-readable description of the error, sent to the client.
    #[serde(rename = "errmsg", default = "String::new")]
    pub message: String,
}

impl CommandError {
    pub fn new(code: i32, code_name: String, msg: String) -> Self {
        CommandError {
            ok: OK_FAILED,
            code,
            code_name,
            message: msg,
        }
    }

    pub fn to_raw_document_buf(&self) -> RawDocumentBuf {
        // The key names used here must match with the field names expected by the driver sdk on errors.
        let mut doc = RawDocumentBuf::new();
        doc.append("ok", self.ok);
        doc.append("code", self.code);
        doc.append("codeName", self.code_name.clone());
        doc.append("errmsg", self.message.clone());
        doc
    }

    fn internal(msg: String) -> Self {
        CommandError::new(
            ErrorCode::InternalError as i32,
            "Internal Error".to_string(),
            msg,
        )
    }

    pub async fn from_error(
        connection_context: &ConnectionContext,
        err: &DocumentDBError,
        activity_id: &str,
    ) -> Self {
        match err {
            DocumentDBError::IoError(e, _) => CommandError::internal(e.to_string()),
            DocumentDBError::PostgresError(e, _) => {
                Self::from_pg_error(connection_context, e, activity_id).await
            }
            DocumentDBError::PoolError(PoolError::Backend(e), _) => {
                Self::from_pg_error(connection_context, e, activity_id).await
            }
            DocumentDBError::PostgresDocumentDBError(e, msg, _) => {
                if let Ok(state) = PgResponse::i32_to_postgres_sqlstate(e) {
                    if let Some(known_error) =
                        Self::known_pg_error(connection_context, &state, msg, activity_id).await
                    {
                        return known_error;
                    }
                }
                CommandError::new(*e, documentdb_error_message(), msg.to_string())
            }
            DocumentDBError::RawBsonError(e, _) => {
                CommandError::internal(format!("Raw BSON error: {e}"))
            }
            DocumentDBError::PoolError(e, _) => CommandError::internal(format!("Pool error: {e}")),
            DocumentDBError::CreatePoolError(e, _) => {
                CommandError::internal(format!("Create pool error: {e}"))
            }
            DocumentDBError::BuildPoolError(e, _) => {
                CommandError::internal(format!("Build pool error: {e}"))
            }
            DocumentDBError::DocumentDBError(error_code, msg, _) => {
                CommandError::new(*error_code as i32, error_code.to_string(), msg.to_string())
            }
            DocumentDBError::UntypedDocumentDBError(error_code, msg, code, _) => {
                CommandError::new(*error_code, code.clone(), msg.to_string())
            }
            DocumentDBError::SSLErrorStack(error_stack, _) => {
                CommandError::internal(format!("SSL error stack: {error_stack}"))
            }
            DocumentDBError::SSLError(error, _) => {
                CommandError::internal(format!("SSL error: {error}"))
            }
            DocumentDBError::ValueAccessError(error, _) => match &error.kind {
                ValueAccessErrorKind::UnexpectedType {
                    actual, expected, ..
                } => {
                    tracing::error!(
                        activity_id = activity_id,
                        "Type mismatch error: expected {expected:?} but got {actual:?}"
                    );
                    CommandError::new(
                        ErrorCode::TypeMismatch as i32,
                        value_access_error_message(),
                        format!(
                            "Expected {:?} but got {:?}, at key {}",
                            expected,
                            actual,
                            error.key()
                        ),
                    )
                }
                ValueAccessErrorKind::InvalidBson(_) => {
                    let error_message = "Value is not a valid BSON";
                    tracing::error!(activity_id = activity_id, "{error_message}");
                    CommandError::new(
                        ErrorCode::BadValue as i32,
                        value_access_error_message(),
                        error_message.to_string(),
                    )
                }
                ValueAccessErrorKind::NotPresent => {
                    let error_message = "Value is not present";
                    tracing::error!(activity_id = activity_id, "{error_message}");
                    CommandError::new(
                        ErrorCode::BadValue as i32,
                        value_access_error_message(),
                        error_message.to_string(),
                    )
                }
                _ => {
                    tracing::error!(activity_id = activity_id, "Hit generic ValueAccessError.");
                    CommandError::new(
                        ErrorCode::BadValue as i32,
                        value_access_error_message(),
                        "Unexpected value".to_string(),
                    )
                }
            },
        }
    }

    pub async fn from_pg_error(
        context: &ConnectionContext,
        e: &tokio_postgres::Error,
        activity_id: &str,
    ) -> Self {
        if let Some(state) = e.code() {
            if let Some(known_error) = Self::known_pg_error(
                context,
                state,
                e.as_db_error().map_or("", |e| e.message()),
                activity_id,
            )
            .await
            {
                return known_error;
            }
            CommandError::new(
                PgResponse::postgres_sqlstate_to_i32(state),
                documentdb_error_message(),
                Self::pg_error_to_msg(e),
            )
        } else {
            CommandError::internal(e.to_string())
        }
    }

    async fn known_pg_error(
        context: &ConnectionContext,
        state: &SqlState,
        msg: &str,
        activity_id: &str,
    ) -> Option<Self> {
        if let Some((code, code_name, error_msg)) =
            PgResponse::known_pg_error(context, state, msg, activity_id).await
        {
            Some(CommandError::new(
                code,
                code_name.unwrap_or(documentdb_error_message()),
                error_msg.to_string(),
            ))
        } else {
            None
        }
    }

    fn pg_error_to_msg(e: &tokio_postgres::Error) -> String {
        if let Some(db_error) = e.as_db_error() {
            db_error.message().to_owned()
        } else {
            e.source().map_or("No Cause.".to_owned(), |s| s.to_string())
        }
    }
}
