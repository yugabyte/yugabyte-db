/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/processor/process.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    sync::Arc,
    time::{Duration, Instant},
};

use deadpool_postgres::{HookError, PoolError};
use tokio_postgres::error::SqlState;

use crate::{
    configuration::DynamicConfiguration,
    context::{ConnectionContext, RequestContext},
    error::{DocumentDBError, ErrorCode, Result},
    explain,
    postgres::PgDataClient,
    processor::{
        constant, cursor, data_description, data_management, indexing, ismaster, roles, session,
        transaction, users,
    },
    requests::RequestType,
    responses::Response,
};

enum Retry {
    Long,
    Short,
    None,
}

pub async fn process_request(
    request_context: &mut RequestContext<'_>,
    connection_context: &mut ConnectionContext,
    pg_data_client: impl PgDataClient,
) -> Result<Response> {
    let dynamic_config = connection_context.dynamic_configuration();

    transaction::handle(request_context, connection_context, &pg_data_client).await?;
    let start_time = Instant::now();

    let mut retries = 0;
    let result = loop {
        let response = match request_context.payload.request_type() {
            RequestType::Aggregate => {
                data_management::process_aggregate(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::BuildInfo => constant::process_build_info(&dynamic_config).await,
            RequestType::CollStats => {
                data_management::process_coll_stats(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::Compact => {
                data_management::process_compact(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::ConnectionStatus => {
                if dynamic_config.enable_connection_status().await {
                    users::process_connection_status(
                        request_context,
                        connection_context,
                        &pg_data_client,
                    )
                    .await
                } else {
                    constant::process_connection_status()
                }
            }
            RequestType::Count => {
                data_management::process_count(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::Create => {
                data_description::process_create(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::CreateIndex | RequestType::CreateIndexes => {
                indexing::process_create_indexes(
                    request_context,
                    connection_context,
                    &dynamic_config,
                    &pg_data_client,
                )
                .await
            }
            RequestType::Delete => {
                data_management::process_delete(
                    request_context,
                    connection_context,
                    &dynamic_config,
                    &pg_data_client,
                )
                .await
            }
            RequestType::Distinct => {
                data_management::process_distinct(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::Drop => {
                data_description::process_drop_collection(
                    request_context,
                    connection_context,
                    &dynamic_config,
                    &pg_data_client,
                )
                .await
            }
            RequestType::DropDatabase => {
                data_description::process_drop_database(
                    request_context,
                    connection_context,
                    &dynamic_config,
                    &pg_data_client,
                )
                .await
            }
            RequestType::Explain => {
                explain::process_explain(request_context, None, connection_context, &pg_data_client)
                    .await
            }
            RequestType::Find => {
                data_management::process_find(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::FindAndModify => {
                data_management::process_find_and_modify(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::GetCmdLineOpts => constant::process_get_cmd_line_opts(),
            RequestType::GetDefaultRWConcern => constant::process_get_rw_concern(request_context),
            RequestType::GetLog => constant::process_get_log(),
            RequestType::GetMore => {
                cursor::process_get_more(request_context, connection_context, &pg_data_client).await
            }
            RequestType::Hello => {
                ismaster::process(
                    request_context,
                    "isWritablePrimary",
                    connection_context,
                    &dynamic_config,
                )
                .await
            }
            RequestType::HostInfo => constant::process_host_info(),
            RequestType::Insert => {
                data_management::process_insert(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::IsDBGrid => constant::process_is_db_grid(connection_context),
            RequestType::IsMaster => {
                ismaster::process(
                    request_context,
                    "ismaster",
                    connection_context,
                    &dynamic_config,
                )
                .await
            }
            RequestType::ListCollections => {
                data_management::process_list_collections(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::ListDatabases => {
                data_management::process_list_databases(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::ListIndexes => {
                indexing::process_list_indexes(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::Ping => Ok(constant::ok_response()),
            RequestType::SaslContinue | RequestType::SaslStart | RequestType::Logout => {
                Err(DocumentDBError::internal_error(
                    "Command should have been handled by Auth".to_string(),
                ))
            }
            RequestType::Update => {
                data_management::process_update(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::Validate => {
                data_management::process_validate(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::DropIndexes => {
                indexing::process_drop_indexes(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::ShardCollection => {
                data_description::process_shard_collection(
                    request_context,
                    connection_context,
                    false,
                    &pg_data_client,
                )
                .await
            }
            RequestType::ReIndex => {
                indexing::process_reindex(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::CurrentOp => {
                data_management::process_current_op(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::KillOp => {
                data_management::process_kill_op(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::CollMod => {
                data_description::process_coll_mod(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::GetParameter => {
                data_management::process_get_parameter(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::KillCursors => {
                cursor::process_kill_cursors(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::DbStats => {
                data_management::process_db_stats(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::RenameCollection => {
                data_description::process_rename_collection(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
            RequestType::PrepareTransaction => constant::process_prepare_transaction(),
            RequestType::CommitTransaction => transaction::process_commit(connection_context).await,
            RequestType::AbortTransaction => transaction::process_abort(connection_context).await,
            RequestType::ListCommands => constant::list_commands(),
            RequestType::EndSessions => {
                session::end_sessions(request_context, connection_context).await
            }
            RequestType::ReshardCollection => {
                data_description::process_shard_collection(
                    request_context,
                    connection_context,
                    true,
                    &pg_data_client,
                )
                .await
            }
            RequestType::WhatsMyUri => constant::process_whats_my_uri(),
            RequestType::CreateUser => {
                users::process_create_user(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::DropUser => {
                users::process_drop_user(request_context, connection_context, &pg_data_client).await
            }
            RequestType::UpdateUser => {
                users::process_update_user(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::UsersInfo => {
                users::process_users_info(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::CreateRole => {
                roles::process_create_role(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::UpdateRole => {
                roles::process_update_role(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::DropRole => {
                roles::process_drop_role(request_context, connection_context, &pg_data_client).await
            }
            RequestType::RolesInfo => {
                roles::process_roles_info(request_context, connection_context, &pg_data_client)
                    .await
            }
            RequestType::UnshardCollection => {
                data_description::process_unshard_collection(
                    request_context,
                    connection_context,
                    &pg_data_client,
                )
                .await
            }
        };

        if response.is_ok()
            || start_time.elapsed()
                > Duration::from_secs(
                    connection_context
                        .service_context
                        .setup_configuration()
                        .postgres_command_timeout_secs(),
                )
        {
            return response;
        }

        let retry = match &response {
            // in the case of write conflict, we need to remove the transaction.
            Err(DocumentDBError::PostgresError(error, _)) => {
                retry_policy(
                    &dynamic_config,
                    error,
                    request_context.payload.request_type(),
                )
                .await
            }
            Err(DocumentDBError::PoolError(
                PoolError::PostCreateHook(HookError::Backend(error)),
                _,
            )) => {
                retry_policy(
                    &dynamic_config,
                    error,
                    request_context.payload.request_type(),
                )
                .await
            }
            Err(DocumentDBError::PoolError(PoolError::Backend(error), _)) => {
                retry_policy(
                    &dynamic_config,
                    error,
                    request_context.payload.request_type(),
                )
                .await
            }
            // Any other errors cannot be retried
            _ => Retry::None,
        };

        retries += 1;

        match retry {
            Retry::Short => {
                tracing::warn!("Retrying short: {retries}");
                tokio::time::sleep(Duration::from_millis(50)).await;
            }
            Retry::Long => {
                tracing::warn!("Retrying long: {retries}");
                tokio::time::sleep(Duration::from_secs(5)).await;
            }

            Retry::None => break response,
        }
    };

    if connection_context.transaction.is_some() {
        match result {
            Err(DocumentDBError::UntypedDocumentDBError(112, _, _, _))
            | Err(DocumentDBError::DocumentDBError(ErrorCode::WriteConflict, _, _))
            | Err(_)
                if request_context.payload.request_type() == &RequestType::Find
                    || request_context.payload.request_type() == &RequestType::Aggregate =>
            {
                transaction::process_abort(connection_context).await?;
            }
            _ => {}
        }
    }

    result
}

async fn retry_policy(
    dynamic_config: &Arc<dyn DynamicConfiguration>,
    error: &tokio_postgres::Error,
    request_type: &RequestType,
) -> Retry {
    if error.is_closed() {
        return Retry::Short;
    }
    match error.code() {
        Some(&SqlState::ADMIN_SHUTDOWN) => Retry::Short,
        Some(&SqlState::READ_ONLY_SQL_TRANSACTION) if dynamic_config.is_replica_cluster().await => {
            Retry::None
        }
        Some(&SqlState::READ_ONLY_SQL_TRANSACTION) => Retry::Long,
        Some(&SqlState::CONNECTION_FAILURE) => Retry::Long,
        Some(&SqlState::INVALID_AUTHORIZATION_SPECIFICATION) => Retry::Long,
        Some(&SqlState::T_R_DEADLOCK_DETECTED) if request_type == &RequestType::Update => {
            Retry::Long
        }
        _ => Retry::None,
    }
}
