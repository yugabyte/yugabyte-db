/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/data_client.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{future::Future, sync::Arc};

use async_trait::async_trait;
use bson::{RawDocument, RawDocumentBuf};
use tokio_postgres::Row;

use crate::{
    auth::AuthState,
    context::{ConnectionContext, Cursor, RequestContext, ServiceContext},
    error::Result,
    explain::Verbosity,
    postgres::Transaction,
    responses::{PgResponse, Response},
};

use super::{Connection, PgDocument, QueryCatalog};

#[async_trait]
pub trait PgDataClient: Send + Sync {
    async fn new_authorized(
        service_context: &Arc<ServiceContext>,
        authorization: &AuthState,
    ) -> Result<Self>
    where
        Self: Sized;

    async fn new_unauthorized(service_context: &Arc<ServiceContext>) -> Result<Self>
    where
        Self: Sized;

    async fn pull_connection(
        &self,
        connection_context: &ConnectionContext,
    ) -> Result<Arc<Connection>> {
        if let Some((session_id, _)) = connection_context.transaction.as_ref() {
            if let Some(connection) = connection_context
                .service_context
                .transaction_store()
                .get_connection(session_id)
                .await
            {
                return Ok(connection);
            }
        }

        Ok(Arc::new(
            self.pull_connection_with_transaction(false).await?,
        ))
    }

    async fn pull_connection_with_transaction(&self, in_transaction: bool) -> Result<Connection>;

    async fn execute_aggregate(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)>;

    async fn execute_coll_stats(
        &self,
        request_context: &mut RequestContext<'_>,
        scale: f64,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_count_query(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_create_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_create_indexes(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_wait_for_index(
        &self,
        request_context: &mut RequestContext<'_>,
        index_build_id: &PgDocument<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_delete(
        &self,
        request_context: &mut RequestContext<'_>,
        is_read_only_for_disk_full: bool,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_distinct_query(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_drop_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        collection: &str,
        is_read_only_for_disk_full: bool,
        connection_context: &ConnectionContext,
    ) -> Result<()>;

    async fn execute_drop_database(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        is_read_only_for_disk_full: bool,
        connection_context: &ConnectionContext,
    ) -> Result<()>;

    async fn execute_explain(
        &self,
        request_context: &mut RequestContext<'_>,
        query_base: &str,
        verbosity: Verbosity,
        connection_context: &ConnectionContext,
    ) -> Result<(Option<serde_json::Value>, String)>;

    async fn execute_find(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)>;

    async fn execute_find_and_modify(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_cursor_get_more(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        cursor: &Cursor,
        cursor_connection: &Option<Arc<Connection>>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_insert(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_list_collections(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)>;

    async fn execute_list_databases(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_list_indexes(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)>;

    async fn execute_update(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_validate(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_drop_indexes(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<PgResponse>;

    #[allow(clippy::too_many_arguments)]
    async fn execute_shard_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        collection: &str,
        key: &RawDocument,
        reshard: bool,
        connection_context: &ConnectionContext,
    ) -> Result<()>;

    async fn execute_reindex(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_current_op(
        &self,
        request_context: &mut RequestContext<'_>,
        filter: &RawDocumentBuf,
        all: bool,
        own_ops: bool,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_kill_op(
        &self,
        request_context: &mut RequestContext<'_>,
        operation_id: &str,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_coll_mod(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_get_parameter(
        &self,
        request_context: &mut RequestContext<'_>,
        all: bool,
        show_details: bool,
        params: Vec<String>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_db_stats(
        &self,
        request_context: &mut RequestContext<'_>,
        scale: f64,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    #[allow(clippy::too_many_arguments)]
    async fn execute_rename_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        source_db: &str,
        source_collection: &str,
        target_collection: &str,
        drop_target: bool,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>>;

    async fn execute_create_user(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_drop_user(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_update_user(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_users_info(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_connection_status(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_compact(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_kill_cursors(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
        cursor_ids: &[i64],
    ) -> Result<Response>;

    async fn execute_create_role(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_update_role(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_drop_role(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn execute_roles_info(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response>;

    async fn run_readonly_if_needed<F, Fut>(
        &self,
        is_read_only_for_disk_full: bool,
        connection: Arc<Connection>,
        query_catalog: &QueryCatalog,
        f: F,
    ) -> Result<Vec<Row>>
    where
        F: FnOnce(Arc<Connection>) -> Fut + Send,
        Fut: Future<Output = Result<Vec<Row>>> + Send,
    {
        if !connection.in_transaction && is_read_only_for_disk_full {
            let mut transaction =
                Transaction::start(connection, tokio_postgres::IsolationLevel::RepeatableRead)
                    .await?;

            // Allow to write for this transaction
            let connection = transaction.get_connection();
            connection
                .batch_execute(query_catalog.set_allow_write())
                .await?;

            let result = f(connection).await?;
            transaction.commit().await?;
            Ok(result)
        } else {
            f(connection).await
        }
    }

    // TODO: This is a temporary solution to get the index build ID from the create indexes response.
    // it's a processing logic, not a data client logic, but for sake of simplicity, we put it here.
    // It should be refactored later to a more appropriate place related to the processing
    fn get_index_build_id<'a>(&self, index_response: &'a PgResponse) -> Result<PgDocument<'a>>;

    async fn execute_unshard_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<()>;
}
