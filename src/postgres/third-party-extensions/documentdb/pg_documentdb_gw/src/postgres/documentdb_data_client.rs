/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/documentdb_data_client.rs
 *
 *-------------------------------------------------------------------------
 */

use std::sync::Arc;

use async_trait::async_trait;
use bson::{RawDocument, RawDocumentBuf};
use tokio_postgres::{error::SqlState, types::Type, Row};

use crate::{
    auth::AuthState,
    context::{ConnectionContext, Cursor, RequestContext, ServiceContext},
    error::{DocumentDBError, Result},
    explain::Verbosity,
    postgres::{PgDataClient, PoolConnection},
    responses::{PgResponse, Response},
};

use super::{Connection, ConnectionPool, PgDocument, Timeout};

pub struct DocumentDBDataClient {
    connection_pool: Option<Arc<ConnectionPool>>,
}

impl DocumentDBDataClient {
    async fn acquire_pool_connection(&self) -> Result<PoolConnection> {
        self.connection_pool
            .as_ref()
            .ok_or(DocumentDBError::internal_error(
                "Acquiring connection to postgres on unauthorized data client".to_string(),
            ))?
            .acquire_connection()
            .await
    }
}

#[async_trait]
impl PgDataClient for DocumentDBDataClient {
    async fn new_authorized(
        service_context: &Arc<ServiceContext>,
        authorization: &AuthState,
    ) -> Result<Self> {
        let user = authorization.username()?;
        let pass = authorization
            .password
            .as_ref()
            .ok_or(DocumentDBError::internal_error(
                "Password is missing on pg data pool acquisition".to_string(),
            ))?;
        let connection_pool = Some(
            service_context
                .connection_pool_manager()
                .get_data_pool(user, pass)
                .await?,
        );

        Ok(DocumentDBDataClient { connection_pool })
    }

    async fn new_unauthorized(_: &Arc<ServiceContext>) -> Result<Self> {
        Ok(DocumentDBDataClient {
            connection_pool: None,
        })
    }

    async fn pull_connection_with_transaction(&self, in_transaction: bool) -> Result<Connection> {
        let pool_connection = self.acquire_pool_connection().await?;

        Ok(Connection::new(pool_connection, in_transaction))
    }

    async fn execute_aggregate(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let connection = self.pull_connection(connection_context).await?;

        #[allow(clippy::unnecessary_to_owned)]
        let aggregate_rows = connection
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .aggregate_cursor_first_page(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok((PgResponse::new(aggregate_rows), connection))
    }

    async fn execute_coll_stats(
        &self,
        request_context: &mut RequestContext<'_>,
        scale: f64,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (_, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let coll_stats_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .coll_stats(),
                &[Type::TEXT, Type::TEXT, Type::FLOAT8],
                &[
                    &request_info.db()?.to_string(),
                    &request_info.collection()?.to_string(),
                    &scale,
                ],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(coll_stats_rows)))
    }

    async fn execute_count_query(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let count_query_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .count_query(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(count_query_rows)))
    }

    async fn execute_create_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let create_collection_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .create_collection_view(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(create_collection_rows)))
    }

    async fn execute_create_indexes(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let create_indexes_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .create_indexes_background(),
                db,
                &PgDocument(request.document()),
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(create_indexes_rows)
    }

    async fn execute_wait_for_index(
        &self,
        request_context: &mut RequestContext<'_>,
        index_build_id: &PgDocument<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let wait_for_index_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .check_build_index_status(),
                &[Type::BYTEA],
                &[&index_build_id],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(wait_for_index_rows)
    }

    async fn execute_delete(
        &self,
        request_context: &mut RequestContext<'_>,
        is_read_only_for_disk_full: bool,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let query_catalog = connection_context.service_context.query_catalog();

        let delete_rows = self
            .run_readonly_if_needed(
                is_read_only_for_disk_full,
                self.pull_connection(connection_context).await?,
                query_catalog,
                move |connection| async move {
                    connection
                        .query(
                            query_catalog.delete(),
                            &[Type::TEXT, Type::BYTEA, Type::BYTEA],
                            &[
                                &request_info.db()?.to_string(),
                                &PgDocument(request.document()),
                                &request.extra(),
                            ],
                            Timeout::transaction(request_info.max_time_ms),
                            request_tracker,
                        )
                        .await
                },
            )
            .await?;

        Ok(delete_rows)
    }

    async fn execute_distinct_query(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let distinct_query_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .distinct_query(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(distinct_query_rows)))
    }

    async fn execute_drop_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        collection: &str,
        is_read_only_for_disk_full: bool,
        connection_context: &ConnectionContext,
    ) -> Result<()> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let query_catalog = connection_context.service_context.query_catalog();

        let _ = self
            .run_readonly_if_needed(
                is_read_only_for_disk_full,
                self.pull_connection(connection_context).await?,
                query_catalog,
                move |connection| async move {
                    connection
                        .query(
                            query_catalog.drop_collection(),
                            &[Type::TEXT, Type::TEXT],
                            &[&db, &collection],
                            Timeout::transaction(request_info.max_time_ms),
                            request_tracker,
                        )
                        .await
                },
            )
            .await?;

        Ok(())
    }

    async fn execute_drop_database(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        is_read_only_for_disk_full: bool,
        connection_context: &ConnectionContext,
    ) -> Result<()> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let query_catalog = connection_context.service_context.query_catalog();

        let _ = self
            .run_readonly_if_needed(
                is_read_only_for_disk_full,
                self.pull_connection(connection_context).await?,
                query_catalog,
                move |connection| async move {
                    connection
                        .query(
                            query_catalog.drop_database(),
                            &[Type::TEXT],
                            &[&db],
                            Timeout::transaction(request_info.max_time_ms),
                            request_tracker,
                        )
                        .await
                },
            )
            .await?;

        Ok(())
    }

    async fn execute_explain(
        &self,
        request_context: &mut RequestContext<'_>,
        query_base: &str,
        verbosity: Verbosity,
        connection_context: &ConnectionContext,
    ) -> Result<(Option<serde_json::Value>, String)> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let analyze = if !matches!(
            verbosity,
            Verbosity::QueryPlanner | Verbosity::AllShardsQueryPlan
        ) {
            "True"
        } else {
            "False"
        };
        let explain_query = connection_context
            .service_context
            .query_catalog()
            .explain(analyze, query_base);

        let explain_rows = match verbosity {
            Verbosity::AllShardsQueryPlan
            | Verbosity::AllShardsExecution
            | Verbosity::AllPlansExecution => {
                let mut pool_connection = self.acquire_pool_connection().await?;
                let transaction = pool_connection.transaction().await?;
                let explain_config_query = match verbosity {
                    Verbosity::AllPlansExecution => connection_context
                        .service_context
                        .query_catalog()
                        .set_explain_all_plans_true(),
                    _ => connection_context
                        .service_context
                        .query_catalog()
                        .set_explain_all_tasks_true(),
                };
                if !explain_config_query.is_empty() {
                    transaction.batch_execute(explain_config_query).await?;
                }
                let explain_prepared_stmt = transaction
                    .prepare_typed_cached(&explain_query, &[Type::TEXT, Type::BYTEA])
                    .await?;
                transaction
                    .query(
                        &explain_prepared_stmt,
                        &[&request_info.db()?, &PgDocument(request.document())],
                    )
                    .await?
            }
            _ =>
            {
                #[allow(clippy::unnecessary_to_owned)]
                self.pull_connection(connection_context)
                    .await?
                    .query_db_bson(
                        &explain_query,
                        &request_info.db()?.to_string(),
                        &PgDocument(request.document()),
                        Timeout::transaction(request_info.max_time_ms),
                        request_tracker,
                    )
                    .await?
            }
        };

        let explain_response = match explain_rows.first() {
            Some(row) => {
                let explain_json: serde_json::Value = row.try_get(0)?;
                Some(explain_json)
            }
            None => None,
        };

        Ok((explain_response, explain_query))
    }

    async fn execute_find(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let connection = self.pull_connection(connection_context).await?;

        #[allow(clippy::unnecessary_to_owned)]
        let find_rows = connection
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .find_cursor_first_page(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok((PgResponse::new(find_rows), connection))
    }

    async fn execute_find_and_modify(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let find_and_modify_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .find_and_modify(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(find_and_modify_rows)))
    }

    async fn execute_cursor_get_more(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        cursor: &Cursor,
        cursor_connection: &Option<Arc<Connection>>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let connection = if let Some(ref connection) = cursor_connection {
            connection
        } else {
            &self.pull_connection(connection_context).await?
        };

        let get_more_rows = connection
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .cursor_get_more(),
                &[Type::TEXT, Type::BYTEA, Type::BYTEA],
                &[
                    &db,
                    &PgDocument(request.document()),
                    &PgDocument(&cursor.continuation),
                ],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;
        Ok(get_more_rows)
    }

    async fn execute_insert(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let insert_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context.service_context.query_catalog().insert(),
                &[Type::TEXT, Type::BYTEA, Type::BYTEA],
                &[
                    &request_info.db()?.to_string(),
                    &PgDocument(request.document()),
                    &request.extra(),
                ],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(insert_rows)
    }

    async fn execute_list_collections(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let connection = self.pull_connection(connection_context).await?;

        #[allow(clippy::unnecessary_to_owned)]
        let list_collections_rows = connection
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .list_collections(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok((PgResponse::new(list_collections_rows), connection))
    }

    async fn execute_list_databases(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        // TODO: Handle the case where !nameOnly - the legacy gateway simply returns 0s in the appropriate format
        let filter = request.document().get_document("filter").ok();
        let filter_string = filter.map_or("", |_| "WHERE document @@ $1");

        let list_db_query = connection_context
            .service_context
            .query_catalog()
            .list_databases(filter_string);
        let connection = self.pull_connection(connection_context).await?;

        let list_database_rows = match filter {
            None => {
                connection
                    .query(
                        &list_db_query,
                        &[],
                        &[],
                        Timeout::transaction(request_info.max_time_ms),
                        request_tracker,
                    )
                    .await?
            }
            Some(filter) => {
                connection
                    .query(
                        &list_db_query,
                        &[Type::BYTEA],
                        &[&PgDocument(filter)],
                        Timeout::transaction(request_info.max_time_ms),
                        request_tracker,
                    )
                    .await?
            }
        };

        Ok(Response::Pg(PgResponse::new(list_database_rows)))
    }

    async fn execute_list_indexes(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<(PgResponse, Arc<Connection>)> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let connection = self.pull_connection(connection_context).await?;

        #[allow(clippy::unnecessary_to_owned)]
        let list_indexes_rows = connection
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .list_indexes_cursor_first_page(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok((PgResponse::new(list_indexes_rows), connection))
    }

    async fn execute_update(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let update_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .process_update(),
                &[Type::TEXT, Type::BYTEA, Type::BYTEA],
                &[
                    &request_info.db()?.to_string(),
                    &PgDocument(request.document()),
                    &request.extra(),
                ],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(update_rows)
    }

    async fn execute_validate(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let validate_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .validate(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(validate_rows)))
    }

    async fn execute_drop_indexes(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<PgResponse> {
        let (request, request_info, request_tracker) = request_context.get_components();
        #[allow(clippy::unnecessary_to_owned)]
        let drop_indexes_rows = self
            .pull_connection(connection_context)
            .await?
            .query_db_bson(
                connection_context
                    .service_context
                    .query_catalog()
                    .drop_indexes(),
                &request_info.db()?.to_string(),
                &PgDocument(request.document()),
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(PgResponse::new(drop_indexes_rows))
    }

    async fn execute_shard_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        db: &str,
        collection: &str,
        key: &RawDocument,
        reshard: bool,
        connection_context: &ConnectionContext,
    ) -> Result<()> {
        let (_, request_info, request_tracker) = request_context.get_components();
        self.pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .shard_collection(),
                &[Type::TEXT, Type::TEXT, Type::BYTEA, Type::BOOL],
                &[
                    &db.to_string(),
                    &collection.to_string(),
                    &PgDocument(key),
                    &reshard,
                ],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(())
    }

    async fn execute_reindex(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let reindex_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .re_index(),
                &[Type::TEXT, Type::TEXT],
                &[
                    &request_info.db()?.to_string(),
                    &request_info.collection()?.to_string(),
                ],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;
        Ok(Response::Pg(PgResponse::new(reindex_rows)))
    }

    async fn execute_current_op(
        &self,
        request_context: &mut RequestContext<'_>,
        filter: &RawDocumentBuf,
        all: bool,
        own_ops: bool,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let current_op_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .current_op(),
                &[Type::BYTEA, Type::BOOL, Type::BOOL],
                &[&PgDocument(filter), &all, &own_ops],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(current_op_rows)))
    }

    async fn execute_kill_op(
        &self,
        request_context: &mut RequestContext<'_>,
        _: &str,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let kill_op_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context.service_context.query_catalog().kill_op(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(kill_op_rows)))
    }

    async fn execute_coll_mod(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let coll_mod_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .coll_mod(),
                &[Type::TEXT, Type::TEXT, Type::BYTEA],
                &[
                    &request_info.db()?.to_string(),
                    &request_info.collection()?.to_string(),
                    &PgDocument(request.document()),
                ],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(coll_mod_rows)))
    }

    async fn execute_get_parameter(
        &self,
        request_context: &mut RequestContext<'_>,
        all: bool,
        show_details: bool,
        params: Vec<String>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let get_parameter_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .get_parameter(),
                &[Type::BOOL, Type::BOOL, Type::TEXT_ARRAY],
                &[&all, &show_details, &params],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(get_parameter_rows)))
    }

    async fn execute_db_stats(
        &self,
        request_context: &mut RequestContext<'_>,
        scale: f64,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let db_stats_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .db_stats(),
                &[Type::TEXT, Type::FLOAT8, Type::BOOL],
                &[&request_info.db()?.to_string(), &scale, &false],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(db_stats_rows)))
    }

    async fn execute_rename_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        source_db: &str,
        source_collection: &str,
        target_collection: &str,
        drop_target: bool,
        connection_context: &ConnectionContext,
    ) -> Result<Vec<Row>> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let rename_collection_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .rename_collection(),
                &[Type::TEXT, Type::TEXT, Type::TEXT, Type::BOOL],
                &[
                    &source_db,
                    &source_collection,
                    &target_collection,
                    &drop_target,
                ],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(rename_collection_rows)
    }

    async fn execute_create_user(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let create_user_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .create_user(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await
            .map_err(|e| {
                if let DocumentDBError::PostgresError(pg_error, _) = &e {
                    if let Some(code) = pg_error.code() {
                        if code == &SqlState::DUPLICATE_OBJECT {
                            return DocumentDBError::duplicate_user(
                                "The specified user already exists.".to_string(),
                            );
                        }
                    }
                }
                e
            })?;

        Ok(Response::Pg(PgResponse::new(create_user_rows)))
    }

    async fn execute_drop_user(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let drop_user_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .drop_user(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await
            .map_err(|e| {
                if let DocumentDBError::PostgresError(pg_error, _) = &e {
                    if let Some(code) = pg_error.code() {
                        if code == &SqlState::UNDEFINED_OBJECT {
                            return DocumentDBError::user_not_found(
                                "The specified user does not exist.".to_string(),
                            );
                        }
                    }
                }
                e
            })?;

        Ok(Response::Pg(PgResponse::new(drop_user_rows)))
    }

    async fn execute_update_user(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let update_user_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .update_user(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await
            .map_err(|e| {
                if let DocumentDBError::PostgresError(pg_error, _) = &e {
                    if let Some(code) = pg_error.code() {
                        if code == &SqlState::UNDEFINED_OBJECT {
                            return DocumentDBError::user_not_found(
                                "The specified user does not exist.".to_string(),
                            );
                        }
                    }
                }
                e
            })?;

        Ok(Response::Pg(PgResponse::new(update_user_rows)))
    }

    async fn execute_users_info(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let users_info_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .users_info(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::transaction(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(Response::Pg(PgResponse::new(users_info_rows)))
    }

    fn get_index_build_id<'a>(&self, index_response: &'a PgResponse) -> Result<PgDocument<'a>> {
        Ok(index_response.first()?.get(2))
    }

    async fn execute_unshard_collection(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<()> {
        let (request, request_info, request_tracker) = request_context.get_components();
        self.pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .unshard_collection(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;

        Ok(())
    }

    async fn execute_connection_status(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let connection_status_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .connection_status(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;
        Ok(Response::Pg(PgResponse::new(connection_status_rows)))
    }

    async fn execute_compact(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let compact_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context.service_context.query_catalog().compact(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;
        Ok(Response::Pg(PgResponse::new(compact_rows)))
    }

    async fn execute_kill_cursors(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
        cursor_ids: &[i64],
    ) -> Result<Response> {
        let (_, request_info, request_tracker) = request_context.get_components();
        let kill_cursors_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .kill_cursors(),
                &[Type::INT8_ARRAY],
                &[&cursor_ids],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;
        Ok(Response::Pg(PgResponse::new(kill_cursors_rows)))
    }

    async fn execute_create_role(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let create_role_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .create_role(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await
            .map_err(|e| {
                if let DocumentDBError::PostgresError(pg_error, _) = &e {
                    if let Some(code) = pg_error.code() {
                        if code == &SqlState::DUPLICATE_OBJECT {
                            return DocumentDBError::duplicate_role(
                                "The specified role already exists.".to_string(),
                            );
                        }
                    }
                }
                e
            })?;
        Ok(Response::Pg(PgResponse::new(create_role_rows)))
    }

    async fn execute_update_role(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let update_role_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .update_role(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await
            .map_err(|e| {
                if let DocumentDBError::PostgresError(pg_error, _) = &e {
                    if let Some(code) = pg_error.code() {
                        if code == &SqlState::UNDEFINED_OBJECT {
                            return DocumentDBError::role_not_found(
                                "The specified role does not exist.".to_string(),
                            );
                        }
                    }
                }
                e
            })?;
        Ok(Response::Pg(PgResponse::new(update_role_rows)))
    }

    async fn execute_drop_role(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let drop_role_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .drop_role(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await
            .map_err(|e| {
                if let DocumentDBError::PostgresError(pg_error, _) = &e {
                    if let Some(code) = pg_error.code() {
                        if code == &SqlState::UNDEFINED_OBJECT {
                            return DocumentDBError::role_not_found(
                                "The specified role does not exist.".to_string(),
                            );
                        }
                    }
                }
                e
            })?;
        Ok(Response::Pg(PgResponse::new(drop_role_rows)))
    }

    async fn execute_roles_info(
        &self,
        request_context: &mut RequestContext<'_>,
        connection_context: &ConnectionContext,
    ) -> Result<Response> {
        let (request, request_info, request_tracker) = request_context.get_components();
        let roles_info_rows = self
            .pull_connection(connection_context)
            .await?
            .query(
                connection_context
                    .service_context
                    .query_catalog()
                    .roles_info(),
                &[Type::BYTEA],
                &[&PgDocument(request.document())],
                Timeout::command(request_info.max_time_ms),
                request_tracker,
            )
            .await?;
        Ok(Response::Pg(PgResponse::new(roles_info_rows)))
    }
}
