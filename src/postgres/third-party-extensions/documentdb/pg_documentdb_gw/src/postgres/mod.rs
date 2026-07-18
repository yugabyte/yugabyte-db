/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/mod.rs
 *
 *-------------------------------------------------------------------------
 */

mod connection;
mod connection_pool;
mod data_client;
mod document;
mod documentdb_data_client;
mod pool_manager;
mod query_catalog;
mod transaction;

pub use connection::{Connection, Timeout, TimeoutType};
pub use connection_pool::{ConnectionPool, ConnectionPoolStatus, PoolConnection};
pub use data_client::PgDataClient;
pub use document::PgDocument;
pub use documentdb_data_client::DocumentDBDataClient;
pub use pool_manager::{
    clean_unused_pools, PoolManager, AUTHENTICATION_MAX_CONNECTIONS,
    SYSTEM_REQUESTS_MAX_CONNECTIONS,
};
pub use query_catalog::{create_query_catalog, QueryCatalog};
pub use transaction::Transaction;
