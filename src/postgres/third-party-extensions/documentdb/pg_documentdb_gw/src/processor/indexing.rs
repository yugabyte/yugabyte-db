/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/indexing.rs
 *
 *-------------------------------------------------------------------------
 */

use std::sync::Arc;
use std::time::{Duration, Instant};

use bson::{Document, RawDocumentBuf};

use crate::postgres::PgDataClient;
use crate::responses::constant::pg_returned_invalid_response_message;
use crate::{
    configuration::DynamicConfiguration,
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::PgDocument,
    responses::{PgResponse, RawResponse, Response},
};

use super::cursor::save_cursor;

pub async fn process_create_indexes(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    dynamic_config: &Arc<dyn DynamicConfiguration>,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let db = request_context.info.db()?.to_string();
    if db == "config" || db == "admin" {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::IllegalOperation,
            "Creating indexes in the \"config\" or \"admin\" databases is not allowed".to_string(),
        ));
    }

    let create_indexes_rows = pg_data_client
        .execute_create_indexes(request_context, &db, connection_context)
        .await?;

    let row = create_indexes_rows
        .first()
        .ok_or(DocumentDBError::pg_response_empty())?;
    let success: bool = row.get(1);
    let response = PgResponse::new(create_indexes_rows);
    if success {
        wait_for_index(
            request_context,
            response,
            connection_context,
            dynamic_config,
            pg_data_client,
        )
        .await
    } else {
        parse_create_index_error(&response)
    }
}

pub async fn wait_for_index(
    request_context: &mut RequestContext<'_>,
    create_result: PgResponse,
    connection_context: &ConnectionContext,
    dynamic_config: &Arc<dyn DynamicConfiguration>,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let start_time = Instant::now();
    let index_build_id: PgDocument = pg_data_client.get_index_build_id(&create_result)?;

    if index_build_id.0.is_empty() {
        return Ok(Response::Pg(create_result));
    }

    let mut interval = tokio::time::interval(Duration::from_millis(
        dynamic_config.index_build_sleep_milli_secs().await as u64,
    ));
    loop {
        interval.tick().await;
        let wait_for_index_rows = pg_data_client
            .execute_wait_for_index(request_context, &index_build_id, connection_context)
            .await?;

        let row = wait_for_index_rows
            .first()
            .ok_or(DocumentDBError::pg_response_empty())?;

        let success: bool = row.get(1);

        if !success {
            return parse_create_index_error(&PgResponse::new(wait_for_index_rows));
        }

        let complete: bool = row.get(2);
        if complete {
            return Ok(Response::Pg(create_result));
        }

        if let Some(max_time_ms) = request_context.info.max_time_ms {
            let max_time_ms = max_time_ms.try_into().map_err(|_| {
                DocumentDBError::internal_error("Failed to convert max_time_ms to u128".to_string())
            })?;
            if start_time.elapsed().as_millis() > max_time_ms {
                return Err(DocumentDBError::documentdb_error(ErrorCode::ExceededTimeLimit, "The command being executed was terminated due to a command timeout. This may be due to concurrent transactions. Consider increasing the maxTimeMS on the command.".to_string()));
            }
        }
    }
}

fn parse_create_index_error(response: &PgResponse) -> Result<Response> {
    let response = response.as_raw_document()?;
    let raw = response
        .get_document("raw")
        .map_err(DocumentDBError::pg_response_invalid)?;

    let mut errmsg = None;
    let mut code = None;
    for shard in raw.into_iter() {
        let (_, v) = shard?;
        for entry in v.as_document().ok_or(DocumentDBError::internal_error(
            "CreateIndex shard was not a document".to_string(),
        ))? {
            let (k, v) = entry?;
            match k {
                "errmsg" => {
                    errmsg = Some(v.as_str().ok_or(DocumentDBError::internal_error(
                        "errmsg was not a string".to_string(),
                    ))?);
                }
                "code" => {
                    code = Some(v.as_i32().ok_or(DocumentDBError::internal_error(
                        "Code was not an i32".to_string(),
                    ))?);
                }
                _ => {}
            }
        }
    }
    let code = code.ok_or(DocumentDBError::internal_error(
        "errmsg was missing in create index result".to_string(),
    ))?;
    let errmsg = errmsg.ok_or(DocumentDBError::internal_error(
        "errmsg was missing in create index result".to_string(),
    ))?;
    Err(DocumentDBError::PostgresDocumentDBError(
        code,
        errmsg.to_string(),
        std::backtrace::Backtrace::capture(),
    ))
}

pub async fn process_reindex(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_reindex(request_context, connection_context)
        .await
}

pub async fn process_drop_indexes(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let response = pg_data_client
        .execute_drop_indexes(request_context, connection_context)
        .await?;

    // TODO: It should not be needed to convert the document, but the backend returns ok:true instead of ok:1
    let mut response = Document::try_from(response.as_raw_document()?)?;
    let is_response_ok = response
        .get_bool("ok")
        .map_err(|e| DocumentDBError::internal_error(pg_returned_invalid_response_message(e)))?;

    response.insert("ok", if is_response_ok { 1 } else { 0 });

    if is_response_ok {
        Ok(Response::Raw(RawResponse(RawDocumentBuf::from_document(
            &response,
        )?)))
    } else {
        let error_message = response.get_str("errmsg").map_err(|e| {
            DocumentDBError::internal_error(pg_returned_invalid_response_message(e))
        })?;
        let error_code = response.get_i32("code").map_err(|e| {
            DocumentDBError::internal_error(pg_returned_invalid_response_message(e))
        })?;

        Err(DocumentDBError::PostgresDocumentDBError(
            error_code,
            error_message.to_string(),
            std::backtrace::Backtrace::capture(),
        ))
    }
}

pub async fn process_list_indexes(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let (response, conn) = pg_data_client
        .execute_list_indexes(request_context, connection_context)
        .await?;

    save_cursor(connection_context, conn, &response, request_context.info).await?;
    Ok(Response::Pg(response))
}
