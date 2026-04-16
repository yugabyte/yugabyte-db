/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/responses/pg.rs
 *
 *-------------------------------------------------------------------------
 */

use bson::{Bson, Document, RawDocument, RawDocumentBuf};

use documentdb_macros::documentdb_int_error_mapping;
use tokio_postgres::{error::SqlState, Row};

use crate::{
    context::{ConnectionContext, Cursor},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::PgDocument,
    responses::constant::{duplicate_key_violation_message, pg_returned_invalid_response_message},
};

use super::{raw::RawResponse, Response};

/// Response from PG. This holds ownership of the response from the backend
#[derive(Debug)]
pub struct PgResponse {
    rows: Vec<Row>,
}

impl PgResponse {
    pub fn new(rows: Vec<Row>) -> Self {
        PgResponse { rows }
    }

    pub fn first(&self) -> Result<&Row> {
        self.rows
            .first()
            .ok_or(DocumentDBError::pg_response_empty())
    }

    pub fn as_raw_document(&self) -> Result<&RawDocument> {
        match self.rows.first() {
            Some(row) => {
                let content: PgDocument = row.try_get(0)?;
                Ok(content.0)
            }
            None => Err(DocumentDBError::pg_response_empty()),
        }
    }

    pub fn get_cursor(&self) -> Result<Option<(bool, Cursor)>> {
        match self.rows.first() {
            Some(row) => {
                let cols = row.columns();
                if cols.len() != 4 {
                    Ok(None)
                } else {
                    let continuation: Option<PgDocument> = row.try_get(1)?;
                    match continuation {
                        Some(continuation) => {
                            let persist: bool = row.try_get(2)?;
                            let cursor_id: i64 = row.try_get(3)?;
                            Ok(Some((
                                persist,
                                Cursor {
                                    continuation: continuation.0.to_raw_document_buf(),
                                    cursor_id,
                                },
                            )))
                        }
                        None => Ok(None),
                    }
                }
            }
            None => Err(DocumentDBError::pg_response_empty()),
        }
    }

    /// If 'writeErrors' is present, it transforms each error by potentially mapping them to the known DocumentDB error codes.
    pub async fn transform_write_errors(
        self,
        connection_context: &ConnectionContext,
        activity_id: &str,
    ) -> Result<Response> {
        if let Ok(Some(_)) = self.as_raw_document()?.get("writeErrors") {
            // TODO: Conceivably faster without conversion to document
            let mut response = Document::try_from(self.as_raw_document()?)?;
            let write_errors = response.get_array_mut("writeErrors").map_err(|e| {
                DocumentDBError::internal_error(pg_returned_invalid_response_message(e))
            })?;

            for value in write_errors {
                self.transform_error(connection_context, value, activity_id)
                    .await?;
            }
            let raw = RawDocumentBuf::from_document(&response)?;
            return Ok(Response::Raw(RawResponse(raw)));
        }
        Ok(Response::Pg(self))
    }

    async fn transform_error(
        &self,
        context: &ConnectionContext,
        error_bson: &mut Bson,
        activity_id: &str,
    ) -> Result<()> {
        let doc = error_bson
            .as_document_mut()
            .ok_or(DocumentDBError::internal_error(
                "Failed to convert BSON write error into BSON document.".to_string(),
            ))?;
        let msg = doc.get_str("errmsg").unwrap_or("").to_owned();
        let code = doc.get_i32_mut("code").map_err(|e| {
            DocumentDBError::internal_error(pg_returned_invalid_response_message(e))
        })?;

        if let Some((known, opt, error_code)) = PgResponse::known_pg_error(
            context,
            &PgResponse::i32_to_postgres_sqlstate(code)?,
            &msg,
            activity_id,
        )
        .await
        {
            if known == ErrorCode::WriteConflict as i32
                || known == ErrorCode::InternalError as i32
                || known == ErrorCode::LockTimeout as i32
                || known == ErrorCode::Unauthorized as i32
            {
                return Err(DocumentDBError::UntypedDocumentDBError(
                    known,
                    opt.unwrap_or_default().to_string(),
                    error_code.to_string(),
                    std::backtrace::Backtrace::capture(),
                ));
            }

            *code = known;
            doc.insert("errmsg", error_code);
        }
        Ok(())
    }

    pub fn i32_to_postgres_sqlstate(code: &i32) -> Result<SqlState> {
        let mut code = *code;
        let mut chars = [0_u8; 5];
        for char in &mut chars {
            *char = (code & 0x3F) as u8 + b'0';
            code >>= 6;
        }

        Ok(SqlState::from_code(str::from_utf8(&chars).map_err(
            |_| {
                DocumentDBError::internal_error(format!(
                    "Failed to map command error code '{chars:?}' to SQL state."
                ))
            },
        )?))
    }

    pub fn postgres_sqlstate_to_i32(sql_state: &SqlState) -> i32 {
        let mut i = 0;
        let mut res = 0;
        for byte in sql_state.code().as_bytes() {
            res += (((byte - b'0') & 0x3F) as i32) << i;
            i += 6;
        }
        res
    }

    documentdb_int_error_mapping!();

    pub async fn known_pg_error<'a>(
        connection_context: &'a ConnectionContext,
        state: &'a SqlState,
        msg: &'a str,
        activity_id: &str,
    ) -> Option<(i32, Option<String>, &'a str)> {
        if let Some((known, code_name)) = PgResponse::from_known_external_error_code(state) {
            if known == ErrorCode::NotWritablePrimary as i32 {
                return Some((
                    known,
                    Some(msg.to_string()),
                    "This may be due to the database disk being full",
                ));
            }

            return Some((known, Some(code_name.to_string()), msg));
        }

        let code = PgResponse::postgres_sqlstate_to_i32(state);
        if (PgResponse::API_ERROR_CODE_MIN..PgResponse::API_ERROR_CODE_MAX).contains(&code) {
            return Some((code - PgResponse::API_ERROR_CODE_MIN, None, msg));
        }

        // Handle specific pg states and map them to DocumentDB error codes
        match *state {
            SqlState::UNIQUE_VIOLATION | SqlState::EXCLUSION_VIOLATION => {
                if connection_context.transaction.is_some() {
                    tracing::error!(
                        activity_id = activity_id,
                        "Duplicate key error during transaction."
                    );
                    Some((
                        ErrorCode::WriteConflict as i32,
                        Some(format!("{:?}", SqlState::UNIQUE_VIOLATION)),
                        duplicate_key_violation_message(),
                    ))
                } else {
                    tracing::error!(activity_id = activity_id, "Duplicate key error.");
                    Some((
                        ErrorCode::DuplicateKey as i32,
                        Some(format!("{:?}", SqlState::UNIQUE_VIOLATION)),
                        duplicate_key_violation_message(),
                    ))
                }
            }
            SqlState::DISK_FULL => Some((
                ErrorCode::OutOfDiskSpace as i32,
                Some(format!("{:?}", SqlState::DISK_FULL)),
                "The database disk is full",
            )),
            SqlState::UNDEFINED_TABLE => Some((
                ErrorCode::NamespaceNotFound as i32,
                Some(format!("{:?}", SqlState::UNDEFINED_TABLE)),
                msg,
            )),
            SqlState::QUERY_CANCELED => {
                if connection_context.transaction.is_some() {
                    tracing::error!(
                        activity_id = activity_id,
                        "Query canceled during transaction."
                    );
                    Some((ErrorCode::ExceededTimeLimit as i32, Some(format!("{:?}", SqlState::QUERY_CANCELED)), "The command being executed was terminated due to a command timeout. This may be due to concurrent transactions."))
                } else {
                    tracing::error!(activity_id = activity_id, "Query canceled.");
                    Some((ErrorCode::ExceededTimeLimit as i32, Some(format!("{:?}", SqlState::QUERY_CANCELED)), "The command being executed was terminated due to a command timeout. This may be due to concurrent transactions. Consider increasing the maxTimeMS on the command."))
                }
            }
            SqlState::LOCK_NOT_AVAILABLE => {
                if connection_context.transaction.is_some() {
                    tracing::error!(
                        activity_id = activity_id,
                        "Lock not available error during transaction."
                    );
                    Some((
                        ErrorCode::WriteConflict as i32,
                        Some(format!("{:?}", SqlState::LOCK_NOT_AVAILABLE)),
                        msg,
                    ))
                } else {
                    tracing::error!(activity_id = activity_id, "Lock not available error.");
                    Some((
                        ErrorCode::LockTimeout as i32,
                        Some(format!("{:?}", SqlState::LOCK_NOT_AVAILABLE)),
                        msg,
                    ))
                }
            }
            SqlState::FEATURE_NOT_SUPPORTED => Some((
                ErrorCode::CommandNotSupported as i32,
                Some(format!("{:?}", SqlState::FEATURE_NOT_SUPPORTED)),
                msg,
            )),
            SqlState::DATA_EXCEPTION => {
                if msg.contains("dimensions, not") || msg.contains("not allowed in vector") {
                    tracing::error!(
                        activity_id = activity_id,
                        "Dimensions are not allowed in vector error."
                    );
                    Some((
                        ErrorCode::BadValue as i32,
                        Some(format!("{:?}", SqlState::DATA_EXCEPTION)),
                        msg,
                    ))
                } else {
                    Some((
                        ErrorCode::InternalError as i32,
                        Some(format!("{:?}", SqlState::DATA_EXCEPTION)),
                        "An unexpected internal error has occurred",
                    ))
                }
            }
            SqlState::PROGRAM_LIMIT_EXCEEDED => {
                if msg.contains("MB, maintenance_work_mem is") {
                    tracing::error!(activity_id = activity_id, "Index creation requires resources too large to fit in the resource memory limit.");
                    Some((
                        ErrorCode::ExceededMemoryLimit as i32,
                        Some(format!("{:?}", SqlState::PROGRAM_LIMIT_EXCEEDED)),
                        "index creation requires resources too large to fit in the resource memory limit, please try creating index with less number of documents or creating index before inserting documents into collection"
                    ))
                } else if msg.contains("index row size") && msg.contains("exceeds maximum") {
                    let error_message = "Index key is too large";
                    tracing::error!(activity_id = activity_id, "{error_message}");
                    Some((
                        ErrorCode::CannotBuildIndexKeys as i32,
                        Some(format!("{:?}", SqlState::PROGRAM_LIMIT_EXCEEDED)),
                        error_message,
                    ))
                } else {
                    Some((
                        ErrorCode::InternalError as i32,
                        Some(format!("{:?}", SqlState::PROGRAM_LIMIT_EXCEEDED)),
                        msg,
                    ))
                }
            }
            SqlState::NUMERIC_VALUE_OUT_OF_RANGE
                if msg.contains("is out of range for type halfvec") =>
            {
                let error_message =
                    "Some values in the vector are out of range for half vector index";
                tracing::error!(activity_id = activity_id, "{error_message}");
                Some((
                    ErrorCode::BadValue as i32,
                    Some(format!("{:?}", SqlState::NUMERIC_VALUE_OUT_OF_RANGE)),
                    error_message,
                ))
            }
            SqlState::OBJECT_NOT_IN_PREREQUISITE_STATE
                if msg.contains("diskann index needs to be upgraded to version") =>
            {
                let error_message = "The diskann index needs to be upgraded to the latest version, please drop and recreate the index";
                tracing::error!(activity_id = activity_id, "{error_message}");
                Some((
                    ErrorCode::InvalidOptions as i32,
                    Some(format!("{:?}", SqlState::OBJECT_NOT_IN_PREREQUISITE_STATE)),
                    error_message,
                ))
            }
            SqlState::INTERNAL_ERROR => {
                if msg.contains("tsquery stack too small") {
                    // When the search terms have more than 32 nested levels, tsquery raises the PG internal error with message "tsquery stack too small".
                    // This can happen in find commands or $match aggregation stages with $text filter.
                    let error_message = "$text query is exceeding the maximum allowed depth(32), please simplify the query";
                    tracing::error!(activity_id = activity_id, "{error_message}");
                    Some((
                        ErrorCode::BadValue as i32,
                        Some(format!("{:?}", SqlState::INTERNAL_ERROR)),
                        error_message,
                    ))
                } else {
                    Some((
                        ErrorCode::InternalError as i32,
                        Some(format!("{:?}", SqlState::INTERNAL_ERROR)),
                        msg,
                    ))
                }
            }
            SqlState::INVALID_TEXT_REPRESENTATION => Some((
                ErrorCode::BadValue as i32,
                Some(format!("{:?}", SqlState::INVALID_TEXT_REPRESENTATION)),
                msg,
            )),
            SqlState::INVALID_PARAMETER_VALUE => Some((
                ErrorCode::BadValue as i32,
                Some(format!("{:?}", SqlState::INVALID_PARAMETER_VALUE)),
                msg,
            )),
            SqlState::INVALID_ARGUMENT_FOR_NTH_VALUE => Some((
                ErrorCode::BadValue as i32,
                Some(format!("{:?}", SqlState::INVALID_ARGUMENT_FOR_NTH_VALUE)),
                msg,
            )),
            SqlState::READ_ONLY_SQL_TRANSACTION
                if connection_context
                    .dynamic_configuration()
                    .is_replica_cluster()
                    .await =>
            {
                let error_message = "Cannot execute the operation on this replica cluster";
                tracing::error!(activity_id = activity_id, "{error_message}");
                Some((
                    ErrorCode::IllegalOperation as i32,
                    Some("IllegalOperation".to_string()),
                    error_message,
                ))
            }
            SqlState::READ_ONLY_SQL_TRANSACTION => Some((
                ErrorCode::ExceededTimeLimit as i32,
                Some("ExceededTimeLimit".to_string()),
                "Exceeded time limit while waiting for a new primary to be elected",
            )),
            SqlState::INSUFFICIENT_PRIVILEGE => Some((
                ErrorCode::Unauthorized as i32,
                Some("Unauthorized".to_string()),
                "Unauthorized",
            )),
            SqlState::T_R_DEADLOCK_DETECTED => Some((
                ErrorCode::WriteConflict as i32,
                Some("Could not acquire lock for operation due to deadlock".to_string()),
                msg,
            )),
            SqlState::UNDEFINED_OBJECT => Some((
                ErrorCode::UserNotFound as i32,
                Some("UserNotFound".to_string()),
                msg,
            )),
            SqlState::DUPLICATE_OBJECT => Some((
                ErrorCode::Location51003 as i32,
                Some("User already exists".to_string()),
                msg,
            )),
            _ => None,
        }
    }

    /// Corresponds to the PG ErrorCode 0000Y.
    /// Smallest value of the documentdb backend error.
    pub const API_ERROR_CODE_MIN: i32 = 687865856;

    /// Corresponds to the PG ErrorCode 000PY.
    /// Largest value of the documentdb backend error.
    pub const API_ERROR_CODE_MAX: i32 = 696254464;
}
