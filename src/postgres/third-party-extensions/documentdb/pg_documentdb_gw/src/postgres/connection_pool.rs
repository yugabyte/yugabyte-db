/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/connection_pool.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{
    collections::hash_map::DefaultHasher,
    hash::{Hash, Hasher},
};

use deadpool_postgres::{Manager, Pool, Runtime, Status};
use tokio::{
    sync::RwLock,
    time::{Duration, Instant},
};
use tokio_postgres::NoTls;

use crate::{configuration::SetupConfiguration, error::Result, QueryCatalog};

const POOL_PRUNE_INTERVAL_SECS: u64 = 10;

fn pg_configuration(
    setup_configuration: &dyn SetupConfiguration,
    query_catalog: &QueryCatalog,
    user: &str,
    password: Option<&str>,
    application_name: &str,
) -> tokio_postgres::Config {
    let mut config = tokio_postgres::Config::new();

    let command_timeout_ms =
        Duration::from_secs(setup_configuration.postgres_command_timeout_secs())
            .as_millis()
            .to_string();

    let transaction_timeout_ms =
        Duration::from_secs(setup_configuration.transaction_timeout_secs())
            .as_millis()
            .to_string();

    config
        .host(setup_configuration.postgres_host_name())
        .port(setup_configuration.postgres_port())
        .dbname(setup_configuration.postgres_database())
        .user(user)
        .application_name(application_name)
        .options(
            query_catalog.set_search_path_and_timeout(&command_timeout_ms, &transaction_timeout_ms),
        );

    if let Some(pass) = password {
        config.password(pass);
    }

    config
}

pub type PoolConnection = deadpool_postgres::Object;

pub struct ConnectionPoolStatus {
    identifier: String,
    status: Status,
}

impl ConnectionPoolStatus {
    pub fn new(identifier: String, status: Status) -> Self {
        ConnectionPoolStatus { identifier, status }
    }

    pub fn identifier(&self) -> &str {
        &self.identifier
    }

    pub fn status(&self) -> Status {
        self.status
    }
}

#[derive(Debug)]
pub struct ConnectionPool {
    pool: Pool,
    last_used: RwLock<Instant>,
    identifier: String,
}

impl ConnectionPool {
    pub fn new_with_user(
        setup_configuration: &dyn SetupConfiguration,
        query_catalog: &QueryCatalog,
        user: &str,
        password: Option<&str>,
        application_name: String,
        max_size: usize,
    ) -> Result<Self> {
        let config = pg_configuration(
            setup_configuration,
            query_catalog,
            user,
            password,
            &application_name,
        );

        let manager = Manager::new(config, NoTls);

        let pool_builder = Pool::builder(manager)
            .runtime(Runtime::Tokio1)
            .max_size(max_size)
            // The time to wait while trying to establish a connection before terminating the attempt
            // Should be the same as the command timeout
            .wait_timeout(Some(Duration::from_secs(
                setup_configuration.postgres_command_timeout_secs(),
            )));
        let pool = pool_builder.build()?;

        let pool_copy = pool.clone();

        // how long a connection can be idle before it is pruned
        let idle_connection_max_age = Duration::from_secs(
            setup_configuration.postgres_idle_connection_timeout_minutes() * 60,
        );

        tokio::spawn(async move {
            // how many seconds to wait before pruning idle connections that are beyond idle lifetime
            let mut prune_interval =
                tokio::time::interval(Duration::from_secs(POOL_PRUNE_INTERVAL_SECS));

            loop {
                prune_interval.tick().await;
                pool_copy
                    .retain(|_, conn_metrics| conn_metrics.last_used() < idle_connection_max_age);
            }
        });
        let mut hasher = DefaultHasher::new();
        user.hash(&mut hasher);
        let pool_identifier = format!("{:x}-{application_name}-{max_size}", hasher.finish());

        Ok(ConnectionPool {
            pool,
            last_used: RwLock::new(Instant::now()),
            identifier: pool_identifier,
        })
    }

    pub async fn acquire_connection(&self) -> Result<PoolConnection> {
        let mut write_lock = self.last_used.write().await;
        *write_lock = Instant::now();
        Ok(self.pool.get().await?)
    }

    pub async fn last_used(&self) -> Instant {
        let read_lock = self.last_used.read().await;
        *read_lock
    }

    pub fn status(&self) -> ConnectionPoolStatus {
        ConnectionPoolStatus {
            identifier: self.identifier.clone(),
            status: self.pool.status(),
        }
    }
}
