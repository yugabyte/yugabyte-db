/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/users.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::{
    context::{ConnectionContext, RequestContext},
    error::DocumentDBError,
    postgres::PgDataClient,
    responses::Response,
};

pub async fn process_create_user(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_create_user(request_context, connection_context)
        .await
}

pub async fn process_drop_user(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_drop_user(request_context, connection_context)
        .await
}

pub async fn process_update_user(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_update_user(request_context, connection_context)
        .await
}

pub async fn process_users_info(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_users_info(request_context, connection_context)
        .await
}

pub async fn process_connection_status(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_connection_status(request_context, connection_context)
        .await
}
