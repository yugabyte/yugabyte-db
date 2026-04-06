/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/data_description.rs
 *
 *-------------------------------------------------------------------------
 */

use std::sync::Arc;

use bson::rawdoc;

use crate::{
    configuration::DynamicConfiguration,
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::PgDataClient,
    protocol::{self, OK_SUCCEEDED},
    responses::{RawResponse, Response},
};

pub async fn process_coll_mod(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_coll_mod(request_context, connection_context)
        .await
}

pub async fn process_create(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_create_collection(request_context, connection_context)
        .await
}

pub async fn process_drop_database(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    dynamic_config: &Arc<dyn DynamicConfiguration>,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let request_info = request_context.info;

    let db = request_info.db()?.to_string();

    // Invalidate cursors
    connection_context
        .service_context
        .cursor_store()
        .invalidate_cursors_by_database(&db)
        .await;

    let is_read_only_for_disk_full = dynamic_config.is_read_only_for_disk_full().await;
    pg_data_client
        .execute_drop_database(
            request_context,
            db.as_str(),
            is_read_only_for_disk_full,
            connection_context,
        )
        .await?;

    Ok(Response::Raw(RawResponse(rawdoc! {
        "ok": OK_SUCCEEDED,
        "dropped": db,
    })))
}

pub async fn process_drop_collection(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    dynamic_config: &Arc<dyn DynamicConfiguration>,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let request_info = request_context.info;

    let coll = request_info.collection()?.to_string();
    let coll_str = coll.as_str();
    let db = request_info.db()?.to_string();
    let db_str = db.as_str();

    // Invalidate cursors
    connection_context
        .service_context
        .cursor_store()
        .invalidate_cursors_by_collection(db_str, coll_str)
        .await;

    let is_read_only_for_disk_full = dynamic_config.is_read_only_for_disk_full().await;
    pg_data_client
        .execute_drop_collection(
            request_context,
            db_str,
            coll_str,
            is_read_only_for_disk_full,
            connection_context,
        )
        .await?;

    Ok(Response::Raw(RawResponse(rawdoc! {
        "ok": OK_SUCCEEDED,
        "dropped": coll,
    })))
}

pub async fn process_rename_collection(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let request = request_context.payload;
    let mut source: Option<String> = None;
    let mut target: Option<String> = None;
    let mut drop_target = false;
    request.extract_fields(|k, v| {
        match k {
            "renameCollection" => {
                source = Some(
                    v.as_str()
                        .ok_or(DocumentDBError::bad_value(
                            "renameCollection was not a string".to_string(),
                        ))?
                        .to_string(),
                )
            }
            "to" => {
                target = Some(
                    v.as_str()
                        .ok_or(DocumentDBError::bad_value(
                            "to was not a string".to_string(),
                        ))?
                        .to_string(),
                )
            }
            "dropTarget" => {
                drop_target = v.as_bool().unwrap_or(false);
            }
            _ => {}
        };
        Ok(())
    })?;

    let source = source.ok_or(DocumentDBError::bad_value(
        "'renameCollection' missing".to_string(),
    ))?;
    let target = target.ok_or(DocumentDBError::bad_value("'to' missing".to_string()))?;

    let (source_db, source_coll) = protocol::extract_database_and_collection_names(&source)?;
    let (target_db, target_coll) = protocol::extract_database_and_collection_names(&target)?;

    if source_db != target_db {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::CommandNotSupported,
            "renameCollection cannot change databases".to_string(),
        ));
    }

    if source_coll == target_coll {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::IllegalOperation,
            "Can't rename a collection to itself".to_string(),
        ));
    }

    pg_data_client
        .execute_rename_collection(
            request_context,
            source_db,
            source_coll,
            target_coll,
            drop_target,
            connection_context,
        )
        .await?;
    Ok(Response::ok())
}

pub async fn process_shard_collection(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    reshard: bool,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let collection_path = request_context.info.collection()?.to_string();
    let (db, collection) =
        protocol::extract_database_and_collection_names(collection_path.as_str())?;
    let key = request_context
        .payload
        .document()
        .get_document("key")
        .map_err(DocumentDBError::parse_failure())?;

    pg_data_client
        .execute_shard_collection(
            request_context,
            db,
            collection,
            key,
            reshard,
            connection_context,
        )
        .await?;

    Ok(Response::ok())
}

pub async fn process_unshard_collection(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_unshard_collection(request_context, connection_context)
        .await?;

    Ok(Response::ok())
}
