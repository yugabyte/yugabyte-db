/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/ismaster.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    sync::Arc,
    time::{SystemTime, UNIX_EPOCH},
};

use bson::rawdoc;

use crate::{
    configuration::DynamicConfiguration,
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    protocol::{MAX_BSON_OBJECT_SIZE, MAX_MESSAGE_SIZE_BYTES, OK_SUCCEEDED},
    responses::{RawResponse, Response},
};

pub async fn process(
    request_context: &mut RequestContext<'_>,
    writeable_primary_field: &str,
    connection_context: &mut ConnectionContext,
    dynamic_configuration: &Arc<dyn DynamicConfiguration>,
) -> Result<Response> {
    let request = request_context.payload;
    let local_time = i64::try_from(
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|_| {
                DocumentDBError::internal_error("Failed to get the current time".to_string())
            })?
            .as_millis(),
    )
    .map_err(|_| DocumentDBError::internal_error("Current time exceeded an i64".to_string()))?;

    if let Ok(client) = request.document().get_document("client") {
        if connection_context.client_information.is_some() {
            return Err(DocumentDBError::documentdb_error(
                ErrorCode::ClientMetadataCannotBeMutated,
                "Client metadata cannot be mutated".to_string(),
            ));
        }
        connection_context.client_information = Some(client.to_raw_document_buf());
    }

    let mut response_doc = rawdoc! {
        writeable_primary_field: true,
        "msg": "isdbgrid",
        "maxBsonObjectSize": MAX_BSON_OBJECT_SIZE,
        "maxMessageSizeBytes": MAX_MESSAGE_SIZE_BYTES,
        "maxWriteBatchSize": dynamic_configuration.max_write_batch_size().await,
        "localTime": local_time,
        "logicalSessionTimeoutMinutes": 30,
        "minWireVersion": 0,
        "maxWireVersion": dynamic_configuration.server_version().await.max_wire_protocol(),
        "readOnly": dynamic_configuration.read_only().await,
        "connectionId": connection_context.get_connection_id_hash(),
        "saslSupportedMechs": ["SCRAM-SHA-256"],
        "internal": dynamic_configuration.topology(),
        "ok": OK_SUCCEEDED,
    };

    // Add the operationTime field if change streams GUC is enabled
    if dynamic_configuration.enable_change_streams().await {
        response_doc.append(
            "operationTime",
            bson::Timestamp {
                time: (local_time / 1000) as u32,
                increment: (local_time % 1000) as u32,
            },
        );
    }

    Ok(Response::Raw(RawResponse(response_doc)))
}
