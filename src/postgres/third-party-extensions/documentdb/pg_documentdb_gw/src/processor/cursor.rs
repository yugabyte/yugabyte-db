/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/cursor.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{sync::Arc, time::Duration};

use bson::{rawdoc, RawArrayBuf};

use crate::{
    context::{ConnectionContext, Cursor, CursorStoreEntry, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::{Connection, PgDataClient, PgDocument},
    protocol::OK_SUCCEEDED,
    requests::RequestInfo,
    responses::{PgResponse, RawResponse, Response},
};

pub async fn save_cursor(
    connection_context: &ConnectionContext,
    connection: Arc<Connection>,
    response: &PgResponse,
    request_info: &RequestInfo<'_>,
) -> Result<()> {
    if let Some((persist, cursor)) = response.get_cursor()? {
        let connection = if persist { Some(connection) } else { None };
        let dynamic_config = connection_context.service_context.dynamic_configuration();
        let cursor_timeout =
            if dynamic_config.enable_stateless_cursor_timeout().await && connection.is_none() {
                Duration::from_secs(dynamic_config.stateless_cursor_idle_timeout_sec().await)
            } else {
                Duration::from_secs(dynamic_config.default_cursor_idle_timeout_sec().await)
            };
        connection_context
            .add_cursor(
                connection,
                cursor,
                connection_context.auth_state.username()?,
                request_info.db()?,
                request_info.collection()?,
                cursor_timeout,
                request_info.session_id.map(|v| v.to_vec()),
            )
            .await;
    }
    Ok(())
}

pub async fn process_kill_cursors(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let request = request_context.payload;

    let _ = request
        .document()
        .get_str("killCursors")
        .map_err(DocumentDBError::parse_failure())?;

    let cursors = request
        .document()
        .get("cursors")?
        .ok_or(DocumentDBError::bad_value(
            "cursors was missing in killCursors request".to_string(),
        ))?
        .as_array()
        .ok_or(DocumentDBError::documentdb_error(
            ErrorCode::TypeMismatch,
            "killCursors cursors should be an array".to_string(),
        ))?;

    let mut cursor_ids = Vec::new();
    for value in cursors {
        let cursor = value?.as_i64().ok_or(DocumentDBError::bad_value(
            "Cursor was not a valid i64".to_string(),
        ))?;
        cursor_ids.push(cursor);
    }
    let (removed_cursors, missing_cursors) = connection_context
        .service_context
        .cursor_store()
        .kill_cursors(
            connection_context.auth_state.username()?.to_string(),
            &cursor_ids,
        )
        .await;

    if !removed_cursors.is_empty() {
        pg_data_client
            .execute_kill_cursors(request_context, connection_context, &removed_cursors)
            .await?;
    }

    let mut removed_cursor_buf = RawArrayBuf::new();
    for cursor in removed_cursors {
        removed_cursor_buf.push(cursor);
    }
    let mut missing_cursor_buf = RawArrayBuf::new();
    for cursor in missing_cursors {
        missing_cursor_buf.push(cursor);
    }

    Ok(Response::Raw(RawResponse(rawdoc! {
        "ok":OK_SUCCEEDED,
        "cursorsKilled": removed_cursor_buf,
        "cursorsNotFound": missing_cursor_buf,
        "cursorsAlive": [],
        "cursorsUnknown":[],
    })))
}

pub async fn process_get_more(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let request = request_context.payload;

    let mut id = None;
    request.extract_fields(|k, v| {
        if k == "getMore" {
            id = Some(v.as_i64().ok_or(DocumentDBError::bad_value(
                "getMore value should be an i64".to_string(),
            ))?)
        }
        Ok(())
    })?;
    let id = id.ok_or(DocumentDBError::bad_value(
        "getMore not present in document".to_string(),
    ))?;
    let CursorStoreEntry {
        conn: cursor_connection,
        cursor,
        db,
        collection,
        session_id,
        mut cursor_timeout,
        ..
    } = connection_context
        .get_cursor(id, connection_context.auth_state.username()?)
        .await
        .ok_or(DocumentDBError::documentdb_error(
            ErrorCode::CursorNotFound,
            "Provided cursor was not found.".to_string(),
        ))?;

    let results = pg_data_client
        .execute_cursor_get_more(
            request_context,
            &db,
            &cursor,
            &cursor_connection,
            connection_context,
        )
        .await?;

    if !connection_context
        .service_context
        .dynamic_configuration()
        .enable_stateless_cursor_timeout()
        .await
    {
        cursor_timeout = Duration::from_secs(
            connection_context
                .service_context
                .dynamic_configuration()
                .default_cursor_idle_timeout_sec()
                .await,
        );
    }

    if let Some(row) = results.first() {
        let continuation: Option<PgDocument> = row.try_get(1)?;
        if let Some(continuation) = continuation {
            connection_context
                .add_cursor(
                    cursor_connection,
                    Cursor {
                        cursor_id: id,
                        continuation: continuation.0.to_raw_document_buf(),
                    },
                    connection_context.auth_state.username()?,
                    &db,
                    &collection,
                    cursor_timeout,
                    session_id,
                )
                .await;
        }
    }

    Ok(Response::Pg(PgResponse::new(results)))
}
