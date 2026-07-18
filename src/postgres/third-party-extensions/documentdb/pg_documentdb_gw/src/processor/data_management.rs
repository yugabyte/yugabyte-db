/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/data_management.rs
 *
 *-------------------------------------------------------------------------
 */

use std::sync::Arc;

use bson::{spec::ElementType, RawBsonRef, RawDocumentBuf};

use crate::{
    bson::convert_to_bool,
    configuration::DynamicConfiguration,
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    postgres::PgDataClient,
    processor::cursor,
    responses::{PgResponse, Response},
};

pub async fn process_delete(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    dynamic_config: &Arc<dyn DynamicConfiguration>,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let is_read_only_for_disk_full = dynamic_config.is_read_only_for_disk_full().await;
    let delete_rows = pg_data_client
        .execute_delete(
            request_context,
            is_read_only_for_disk_full,
            connection_context,
        )
        .await?;

    PgResponse::new(delete_rows)
        .transform_write_errors(connection_context, request_context.activity_id)
        .await
}

pub async fn process_find(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let (response, conn) = pg_data_client
        .execute_find(request_context, connection_context)
        .await?;

    cursor::save_cursor(connection_context, conn, &response, request_context.info).await?;
    Ok(Response::Pg(response))
}

pub async fn process_insert(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let insert_rows = pg_data_client
        .execute_insert(request_context, connection_context)
        .await?;

    PgResponse::new(insert_rows)
        .transform_write_errors(connection_context, request_context.activity_id)
        .await
}

pub async fn process_aggregate(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let (response, conn) = pg_data_client
        .execute_aggregate(request_context, connection_context)
        .await?;
    cursor::save_cursor(connection_context, conn, &response, request_context.info).await?;
    Ok(Response::Pg(response))
}

pub async fn process_update(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let update_rows = pg_data_client
        .execute_update(request_context, connection_context)
        .await?;

    PgResponse::new(update_rows)
        .transform_write_errors(connection_context, request_context.activity_id)
        .await
}

pub async fn process_list_databases(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_list_databases(request_context, connection_context)
        .await
}

pub async fn process_list_collections(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let (response, conn) = pg_data_client
        .execute_list_collections(request_context, connection_context)
        .await?;

    cursor::save_cursor(connection_context, conn, &response, request_context.info).await?;
    Ok(Response::Pg(response))
}

pub async fn process_validate(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_validate(request_context, connection_context)
        .await
}

pub async fn process_find_and_modify(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_find_and_modify(request_context, connection_context)
        .await
}

pub async fn process_distinct(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_distinct_query(request_context, connection_context)
        .await
}

pub async fn process_count(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    // we need to ensure that the collection is correctly set up before we can execute the count query
    request_context.info.collection()?;

    pg_data_client
        .execute_count_query(request_context, connection_context)
        .await
}

fn convert_to_scale(scale: RawBsonRef) -> Result<f64> {
    match scale.element_type() {
        ElementType::Double => Ok(scale.as_f64().expect("Type of bson was checked.")),
        ElementType::Int32 => Ok(f64::from(
            scale.as_i32().expect("Type of bson was checked."),
        )),
        ElementType::Int64 => Ok(scale.as_i64().expect("Type of bson was checked.") as f64),
        ElementType::Undefined => Ok(1.0),
        ElementType::Null => Ok(1.0),
        _ => Err(DocumentDBError::documentdb_error(
            ErrorCode::TypeMismatch,
            format!(
                "Unexpected bson type for scale: {:#?}",
                scale.element_type()
            ),
        )),
    }
}

pub async fn process_coll_stats(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    // allow floats and ints, the backend will truncate
    let scale = if let Some(scale) = request_context.payload.document().get("scale")? {
        convert_to_scale(scale)?
    } else {
        1.0
    };

    pg_data_client
        .execute_coll_stats(request_context, scale, connection_context)
        .await
}

pub async fn process_db_stats(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    // allow floats and ints, the backend will truncate
    let scale = if let Some(scale) = request_context.payload.document().get("scale")? {
        convert_to_scale(scale)?
    } else {
        1.0
    };

    pg_data_client
        .execute_db_stats(request_context, scale, connection_context)
        .await
}

pub async fn process_current_op(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let mut filter = RawDocumentBuf::new();
    let mut all = false;
    let mut own_ops = false;
    request_context.payload.extract_fields(|k, v| {
        match k {
            "all" => all = v.as_bool().unwrap_or(false),
            "ownOps" => own_ops = v.as_bool().unwrap_or(false),
            _ => filter.append(k, v.to_raw_bson()),
        }
        Ok(())
    })?;

    pg_data_client
        .execute_current_op(request_context, &filter, all, own_ops, connection_context)
        .await
}

pub async fn process_kill_op(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let (request, request_info, _) = request_context.get_components();

    let mut operation_id: Option<String> = None;
    request.extract_fields(|key, value| {
        match key {
            // The "op" field contains the operation ID to kill
            "op" => {
                if let Some(op_str) = value.as_str() {
                    operation_id = Some(op_str.to_string());
                } else {
                    return Err(DocumentDBError::type_mismatch(format!(
                        "Expected \"op\" field to be a string, but got {:?}",
                        value.element_type()
                    )));
                }
            }
            _ => {
                // Ignore other fields
            }
        }
        Ok(())
    })?;

    let op_id = operation_id
        .ok_or_else(|| DocumentDBError::bad_value("Did not provide \"op\" field".to_string()))?;

    // Validate that the command is run against the admin database
    if request_info.db()? != "admin" {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::Unauthorized,
            "killOp may only be run against the admin database.".to_string(),
        ));
    }

    pg_data_client
        .execute_kill_op(request_context, &op_id, connection_context)
        .await
}

async fn get_parameter(
    connection_context: &ConnectionContext,
    request_context: &mut RequestContext<'_>,
    all: bool,
    show_details: bool,
    params: Vec<String>,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_get_parameter(
            request_context,
            all,
            show_details,
            params,
            connection_context,
        )
        .await
}

pub async fn process_get_parameter(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    let (request, request_info, _) = request_context.get_components();

    let mut all_parameters = false;
    let mut show_details = false;
    let mut star = false;
    let mut params = Vec::new();
    request.extract_fields(|k, v| {
        match k {
            "getParameter" => {
                if v.as_str().is_some_and(|s| s == "*") {
                    star = true;
                } else if let Some(doc) = v.as_document() {
                    for pair in doc {
                        let (k, v) = pair?;
                        match k {
                            "allParameters" => {
                                all_parameters =
                                    convert_to_bool(v).ok_or(DocumentDBError::type_mismatch(
                                        "allParameters should be a bool".to_string(),
                                    ))?
                            }
                            "showDetails" => {
                                show_details =
                                    convert_to_bool(v).ok_or(DocumentDBError::type_mismatch(
                                        "showDetails should be convertible to a bool".to_string(),
                                    ))?
                            }
                            _ => {}
                        }
                    }
                }
            }
            _ => params.push(k.to_string()),
        }
        Ok(())
    })?;
    if request_info.db()? != "admin" {
        return Err(DocumentDBError::documentdb_error(
            ErrorCode::Unauthorized,
            "getParameter may only be run against the admin database.".to_string(),
        ));
    }

    if star {
        return get_parameter(
            connection_context,
            request_context,
            true,
            false,
            vec![],
            pg_data_client,
        )
        .await;
    }

    get_parameter(
        connection_context,
        request_context,
        all_parameters,
        show_details,
        params,
        pg_data_client,
    )
    .await
}

pub async fn process_compact(
    request_context: &mut RequestContext<'_>,
    connection_context: &ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response> {
    pg_data_client
        .execute_compact(request_context, connection_context)
        .await
}
