/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/roles.rs
 *
 *-------------------------------------------------------------------------
 */

use crate::{
    context::{ConnectionContext, RequestContext},
    error::DocumentDBError,
    postgres::PgDataClient,
    responses::Response,
};

pub async fn process_create_role(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_create_role(request_context, connection_context)
        .await
}

pub async fn process_update_role(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_update_role(request_context, connection_context)
        .await
}

pub async fn process_drop_role(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_drop_role(request_context, connection_context)
        .await
}

pub async fn process_roles_info(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: &impl PgDataClient,
) -> Result<Response, DocumentDBError> {
    pg_data_client
        .execute_roles_info(request_context, connection_context)
        .await
}
