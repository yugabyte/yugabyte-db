/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/startup.rs
 *
 *-------------------------------------------------------------------------
 */

use std::sync::Arc;

use tokio::time::{Duration, Instant};

use crate::{
    configuration::{DynamicConfiguration, SetupConfiguration},
    context::ServiceContext,
    error::Result,
    postgres::{self, ConnectionPool, PoolManager, QueryCatalog},
    service::TlsProvider,
};

pub fn get_service_context(
    setup_configuration: Box<dyn SetupConfiguration>,
    dynamic_configuration: Arc<dyn DynamicConfiguration>,
    query_catalog: QueryCatalog,
    system_requests_pool: Arc<ConnectionPool>,
    authentication_pool: ConnectionPool,
    tls_provider: TlsProvider,
) -> ServiceContext {
    tracing::info!("Initial dynamic configuration: {dynamic_configuration:?}");

    let connection_pool_manager = PoolManager::new(
        query_catalog.clone(),
        setup_configuration.clone(),
        Arc::clone(&dynamic_configuration),
        system_requests_pool,
        authentication_pool,
    );

    let service_context = ServiceContext::new(
        setup_configuration.clone(),
        Arc::clone(&dynamic_configuration),
        query_catalog.clone(),
        connection_pool_manager,
        tls_provider,
    );

    postgres::clean_unused_pools(service_context.clone());

    service_context
}

pub async fn get_system_connection_pool(
    setup_configuration: &dyn SetupConfiguration,
    query_catalog: &QueryCatalog,
    pool_name: &str,
    max_size: usize,
) -> ConnectionPool {
    // Capture necessary values to avoid lifetime issues
    let postgres_system_user = setup_configuration.postgres_system_user();
    let full_pool_name = format!("{}-{}", setup_configuration.application_name(), pool_name);

    create_postgres_object(
        || async {
            ConnectionPool::new_with_user(
                setup_configuration,
                query_catalog,
                &postgres_system_user,
                None,
                full_pool_name.clone(),
                max_size,
            )
        },
        setup_configuration,
    )
    .await
}

pub async fn create_postgres_object<T, F, Fut>(
    create_func: F,
    setup_configuration: &dyn SetupConfiguration,
) -> T
where
    F: Fn() -> Fut,
    Fut: std::future::Future<Output = Result<T>>,
{
    let max_time = Duration::from_secs(setup_configuration.postgres_startup_wait_time_seconds());
    let wait_time = Duration::from_secs(10);
    let start = Instant::now();

    loop {
        match create_func().await {
            Ok(result) => {
                return result;
            }
            Err(e) => {
                if start.elapsed() < max_time {
                    tracing::warn!("Exception when creating postgres object {e:?}");
                    tokio::time::sleep(wait_time).await;
                    continue;
                } else {
                    panic!("Failed to create postgres object after {max_time:?}: {e}");
                }
            }
        }
    }
}
