/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/postgres/pool_manager.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{collections::HashMap, hash::Hash, sync::Arc};

use tokio::{
    sync::RwLock,
    time::{interval, Duration},
};

use crate::{
    configuration::{DynamicConfiguration, SetupConfiguration},
    context::ServiceContext,
    error::{DocumentDBError, Result},
    postgres::{Connection, ConnectionPool, ConnectionPoolStatus, QueryCatalog},
    telemetry::event_id::EventId,
};

type ClientKey = (String, String, usize);

pub const SYSTEM_REQUESTS_MAX_CONNECTIONS: usize = 2;
pub const AUTHENTICATION_MAX_CONNECTIONS: usize = 5;

/// How often we need to cleanup the old connection pools
const POSTGRES_POOL_CLEANUP_INTERVAL_SEC: u64 = 300;
/// The threshold when a connection pool needs to be disposed
const POSTGRES_POOL_DISPOSE_INTERVAL_SEC: u64 = 7200;

pub struct PoolManager {
    query_catalog: QueryCatalog,
    setup_configuration: Box<dyn SetupConfiguration>,
    dynamic_configuration: Arc<dyn DynamicConfiguration>,

    // Connection pool for system requests that is shared between ServiceContext and DynamicConfiguration
    system_requests_pool: Arc<ConnectionPool>,
    system_auth_pool: ConnectionPool,

    // Maps user credentials to their respective connection pools
    // We need Arc on the ConnectionPool to allow sharing across threads from different connections
    user_data_pools: RwLock<HashMap<ClientKey, Arc<ConnectionPool>>>,
    system_shared_pools: RwLock<HashMap<usize, Arc<ConnectionPool>>>,
}

impl PoolManager {
    pub fn new(
        query_catalog: QueryCatalog,
        setup_configuration: Box<dyn SetupConfiguration>,
        dynamic_configuration: Arc<dyn DynamicConfiguration>,
        system_requests_pool: Arc<ConnectionPool>,
        system_auth_pool: ConnectionPool,
    ) -> Self {
        PoolManager {
            query_catalog,
            setup_configuration,
            dynamic_configuration,
            system_requests_pool,
            system_auth_pool,
            user_data_pools: RwLock::new(HashMap::new()),
            system_shared_pools: RwLock::new(HashMap::new()),
        }
    }

    pub async fn get_data_pool(
        &self,
        username: &str,
        password: &str,
    ) -> Result<Arc<ConnectionPool>> {
        let max_connections = self.dynamic_configuration.max_connections().await;
        let read_lock = self.user_data_pools.read().await;

        match read_lock.get(&(username.to_string(), password.to_string(), max_connections)) {
            None => Err(DocumentDBError::internal_error(
                "Connection pool missing for user.".to_string(),
            )),
            Some(pool_ref) => Ok(Arc::clone(pool_ref)),
        }
    }

    pub async fn system_requests_connection(&self) -> Result<Connection> {
        Ok(Connection::new(
            self.system_requests_pool.acquire_connection().await?,
            false,
        ))
    }

    pub async fn authentication_connection(&self) -> Result<Connection> {
        Ok(Connection::new(
            self.system_auth_pool.acquire_connection().await?,
            false,
        ))
    }

    async fn get_real_max_connections(&self, max_connections: usize) -> usize {
        let system_connection_budget = self.dynamic_configuration.system_connection_budget().await;

        let mut real_max_connections = max_connections - system_connection_budget;
        if real_max_connections < system_connection_budget {
            real_max_connections = system_connection_budget;
        }
        real_max_connections
    }

    pub async fn allocate_data_pool(&self, username: &str, password: &str) -> Result<()> {
        let max_connections = self.dynamic_configuration.max_connections().await;

        let key = (username.to_string(), password.to_string(), max_connections);

        if self.user_data_pools.read().await.contains_key(&key) {
            return Ok(());
        }

        let mut write_lock = self.user_data_pools.write().await;

        // Check again after acquiring write lock to handle race condition
        if write_lock.contains_key(&key) {
            return Ok(());
        }

        let user_data_pool = Arc::new(ConnectionPool::new_with_user(
            self.setup_configuration.as_ref(),
            &self.query_catalog,
            username,
            Some(password),
            format!("{}-UserData", self.setup_configuration.application_name()),
            self.get_real_max_connections(max_connections).await,
        )?);

        write_lock.insert(key, user_data_pool);

        Ok(())
    }

    pub async fn get_system_shared_pool(&self) -> Result<Arc<ConnectionPool>> {
        let max_connections = self.dynamic_configuration.max_connections().await;

        if let Some(pool_ref) = self.system_shared_pools.read().await.get(&max_connections) {
            return Ok(Arc::clone(pool_ref));
        }

        let mut write_lock = self.system_shared_pools.write().await;

        // Check again after acquiring write lock to handle race condition
        if let Some(pool_ref) = write_lock.get(&max_connections) {
            return Ok(Arc::clone(pool_ref));
        }

        let system_shared_pool = Arc::new(ConnectionPool::new_with_user(
            self.setup_configuration.as_ref(),
            &self.query_catalog,
            &self.setup_configuration.postgres_system_user(),
            None,
            format!("{}-SharedData", self.setup_configuration.application_name()),
            self.get_real_max_connections(max_connections).await,
        )?);

        write_lock.insert(max_connections, Arc::clone(&system_shared_pool));

        Ok(system_shared_pool)
    }

    pub async fn clean_unused_pools(&self, max_age: Duration) {
        async fn clean<K>(map: &RwLock<HashMap<K, Arc<ConnectionPool>>>, max_age: Duration)
        where
            K: Clone + Eq + Hash,
        {
            let mut keys_to_remove = Vec::new();
            {
                let read_lock = map.read().await;
                for (key, pool) in read_lock.iter() {
                    if pool.last_used().await.elapsed() > max_age {
                        keys_to_remove.push(key.clone());
                    }
                }
            }

            {
                let mut write_lock = map.write().await;
                for key in keys_to_remove {
                    write_lock.remove(&key);
                }
            }
        }

        clean(&self.user_data_pools, max_age).await;
        clean(&self.system_shared_pools, max_age).await;
    }

    pub async fn report_pool_stats(&self) -> Vec<ConnectionPoolStatus> {
        async fn report<K>(
            map: &RwLock<HashMap<K, Arc<ConnectionPool>>>,
            reports: &mut Vec<ConnectionPoolStatus>,
        ) where
            K: Clone + Eq + Hash,
        {
            let read_lock = map.read().await;
            for (_, pool) in read_lock.iter() {
                reports.push(pool.status())
            }
        }

        let mut pool_stats = vec![
            self.system_auth_pool.status(),
            self.system_requests_pool.status(),
        ];

        report(&self.user_data_pools, &mut pool_stats).await;
        report(&self.system_shared_pools, &mut pool_stats).await;

        pool_stats
    }
}

pub fn clean_unused_pools(service_context: ServiceContext) {
    tokio::spawn(async move {
        let mut cleanup_interval =
            interval(Duration::from_secs(POSTGRES_POOL_CLEANUP_INTERVAL_SEC));
        let max_age = Duration::from_secs(POSTGRES_POOL_DISPOSE_INTERVAL_SEC);
        loop {
            cleanup_interval.tick().await;

            tracing::info!(
                event_id = EventId::ConnectionPool.code(),
                "Performing the cleanup of unused pools"
            );

            service_context
                .connection_pool_manager()
                .clean_unused_pools(max_age)
                .await;
        }
    });
}
