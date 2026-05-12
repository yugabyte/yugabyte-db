/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/connections.rs
 *
 *-------------------------------------------------------------------------
 */

use std::time::Duration;

use tokio_postgres::{
    types::{ToSql, Type},
    Row,
};

use crate::{
    error::Result,
    postgres::{PgDocument, PoolConnection},
    requests::{request_tracker::RequestTracker, RequestIntervalKind},
};

// Provides functions which coerce bson to BYTEA. Any statement binding a PgDocument should use query_typed and not query
// WrongType { postgres: Other(Other { name: "bson", oid: 18934, kind: Simple, schema: "schema_name" }), rust: "document_gateway::postgres::document::PgDocument" })
// Will be occur if the wrong one is used.
#[derive(Debug)]
pub struct Connection {
    pool_connection: PoolConnection,
    pub in_transaction: bool,
}

pub enum TimeoutType {
    // Transaction timeout uses SET LOCAL inside a transaction, it cannot be used if cursors need to be persisted
    Transaction,

    // Command timeout uses SET outside of a transaction, should be used when a transaction cannot be started
    Command,
}

pub struct Timeout {
    timeout_type: TimeoutType,
    max_time_ms: i64,
}

impl Timeout {
    pub fn command(max_time_ms: Option<i64>) -> Option<Self> {
        max_time_ms.map(|m| Timeout {
            timeout_type: TimeoutType::Command,
            max_time_ms: m,
        })
    }

    pub fn transaction(max_time_ms: Option<i64>) -> Option<Self> {
        max_time_ms.map(|m| Timeout {
            timeout_type: TimeoutType::Transaction,
            max_time_ms: m,
        })
    }
}

impl Connection {
    async fn query_internal(
        &self,
        query: &str,
        parameter_types: &[Type],
        params: &[&(dyn ToSql + Sync)],
    ) -> Result<Vec<Row>> {
        let statement = self
            .pool_connection
            .prepare_typed_cached(query, parameter_types)
            .await?;
        Ok(self.pool_connection.query(&statement, params).await?)
    }

    pub async fn query(
        &self,
        query: &str,
        parameter_types: &[Type],
        params: &[&(dyn ToSql + Sync)],
        timeout: Option<Timeout>,
        request_tracker: &mut RequestTracker,
    ) -> Result<Vec<Row>> {
        match timeout {
            Some(Timeout {
                timeout_type: _,
                max_time_ms,
            }) if self.in_transaction => {
                let set_timeout_start = request_tracker.start_timer();
                self.pool_connection
                    .batch_execute(&format!("set local statement_timeout to {max_time_ms}"))
                    .await?;
                request_tracker.record_duration(
                    RequestIntervalKind::PostgresSetStatementTimeout,
                    set_timeout_start,
                );

                let request_start = request_tracker.start_timer();
                let results = self.query_internal(query, parameter_types, params).await;
                request_tracker.record_duration(RequestIntervalKind::ProcessRequest, request_start);

                let set_timeout_start = request_tracker.start_timer();
                self.pool_connection
                    .batch_execute(&format!(
                        "set local statement_timeout to {}",
                        Duration::from_secs(120).as_millis()
                    ))
                    .await?;
                request_tracker.record_duration(
                    RequestIntervalKind::PostgresSetStatementTimeout,
                    set_timeout_start,
                );

                Ok(results?)
            }
            Some(Timeout {
                timeout_type: TimeoutType::Transaction,
                max_time_ms,
            }) => {
                let begin_transaction_start = request_tracker.start_timer();
                self.pool_connection.batch_execute("BEGIN").await?;
                request_tracker.record_duration(
                    RequestIntervalKind::PostgresBeginTransaction,
                    begin_transaction_start,
                );

                let set_timeout_start = request_tracker.start_timer();
                self.pool_connection
                    .batch_execute(&format!("set local statement_timeout to {max_time_ms}"))
                    .await?;
                request_tracker.record_duration(
                    RequestIntervalKind::PostgresSetStatementTimeout,
                    set_timeout_start,
                );

                let request_start = request_tracker.start_timer();
                let results = match self.query_internal(query, parameter_types, params).await {
                    Ok(results) => Ok(results),
                    Err(e) => {
                        self.pool_connection.batch_execute("ROLLBACK").await?;
                        Err(e)
                    }
                }?;
                request_tracker.record_duration(RequestIntervalKind::ProcessRequest, request_start);

                let commit_start = request_tracker.start_timer();
                self.pool_connection.batch_execute("COMMIT").await?;
                request_tracker
                    .record_duration(RequestIntervalKind::PostgresTransactionCommit, commit_start);

                Ok(results)
            }
            Some(Timeout {
                timeout_type: TimeoutType::Command,
                max_time_ms,
            }) => {
                let set_timeout_start = request_tracker.start_timer();
                self.pool_connection
                    .batch_execute(&format!("set statement_timeout to {max_time_ms}"))
                    .await?;
                request_tracker.record_duration(
                    RequestIntervalKind::PostgresSetStatementTimeout,
                    set_timeout_start,
                );

                let request_start = request_tracker.start_timer();
                let results = self.query_internal(query, parameter_types, params).await;
                request_tracker.record_duration(RequestIntervalKind::ProcessRequest, request_start);

                let set_timeout_start = request_tracker.start_timer();
                self.pool_connection
                    .batch_execute(&format!(
                        "set statement_timeout to {}",
                        Duration::from_secs(120).as_millis()
                    ))
                    .await?;
                request_tracker.record_duration(
                    RequestIntervalKind::PostgresSetStatementTimeout,
                    set_timeout_start,
                );

                Ok(results?)
            }
            None => {
                let request_start = request_tracker.start_timer();
                let results = self.query_internal(query, parameter_types, params).await;
                request_tracker.record_duration(RequestIntervalKind::ProcessRequest, request_start);

                results
            }
        }
    }

    pub async fn query_db_bson(
        &self,
        query: &str,
        db: &str,
        bson: &PgDocument<'_>,
        timeout: Option<Timeout>,
        request_tracker: &mut RequestTracker,
    ) -> Result<Vec<Row>> {
        self.query(
            query,
            &[Type::TEXT, Type::BYTEA],
            &[&db, bson],
            timeout,
            request_tracker,
        )
        .await
    }

    pub async fn batch_execute(&self, query: &str) -> Result<()> {
        Ok(self.pool_connection.batch_execute(query).await?)
    }

    pub fn new(pool_connection: PoolConnection, in_transaction: bool) -> Self {
        Connection {
            pool_connection,
            in_transaction,
        }
    }
}
