/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * tests/pool_manager_tests.rs
 *
 *-------------------------------------------------------------------------
 */

pub mod common;

use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

use async_trait::async_trait;
use bson::{rawbson, RawBson};
use documentdb_gateway::{
    configuration::{DynamicConfiguration, SetupConfiguration},
    postgres::{
        create_query_catalog, ConnectionPool, PoolManager, AUTHENTICATION_MAX_CONNECTIONS,
        SYSTEM_REQUESTS_MAX_CONNECTIONS,
    },
};

#[derive(Debug)]
struct MaxConnectionConfig {
    // needed for interior mutability
    max_conn: AtomicUsize,
}

impl MaxConnectionConfig {
    fn max_conn(&self) -> usize {
        self.max_conn.load(Ordering::Relaxed)
    }

    fn set_max_conn(&self, value: usize) {
        self.max_conn.store(value, Ordering::Relaxed)
    }
}

#[async_trait]
impl DynamicConfiguration for MaxConnectionConfig {
    async fn get_str(&self, _: &str) -> Option<String> {
        Option::None
    }

    async fn get_bool(&self, _: &str, _: bool) -> bool {
        false
    }

    async fn get_i32(&self, _: &str, _: i32) -> i32 {
        i32::default()
    }

    async fn get_u64(&self, _: &str, _: u64) -> u64 {
        u64::default()
    }

    async fn equals_value(&self, _: &str, _: &str) -> bool {
        false
    }

    fn topology(&self) -> RawBson {
        rawbson!({})
    }

    async fn enable_developer_explain(&self) -> bool {
        false
    }

    async fn max_connections(&self) -> usize {
        self.max_conn()
    }

    async fn allow_transaction_snapshot(&self) -> bool {
        false
    }

    // Needed to downcast to concrete type
    fn as_any(&self) -> &dyn std::any::Any {
        self
    }

    // for testing simplicity set system_budget to be 0
    async fn system_connection_budget(&self) -> usize {
        0
    }
}

fn test_pool_manager(dynamic_configuration: Arc<MaxConnectionConfig>) -> PoolManager {
    let query_catalog = create_query_catalog();
    let setup_config = common::setup_configuration();
    let postgres_system_user = setup_config.postgres_system_user();
    let system_requests_pool = ConnectionPool::new_with_user(
        &setup_config,
        &query_catalog,
        &postgres_system_user,
        None,
        format!("{}-SystemRequests", setup_config.application_name()),
        SYSTEM_REQUESTS_MAX_CONNECTIONS,
    )
    .expect("Failed to create system pool");

    let authentication_pool = ConnectionPool::new_with_user(
        &setup_config,
        &query_catalog,
        &postgres_system_user,
        None,
        format!("{}-PreAuthRequests", setup_config.application_name()),
        AUTHENTICATION_MAX_CONNECTIONS,
    )
    .expect("Failed to create authentication pool");

    PoolManager::new(
        query_catalog,
        Box::new(setup_config),
        dynamic_configuration,
        Arc::new(system_requests_pool),
        authentication_pool,
    )
}

#[tokio::test]
async fn validate_pool_reusage() {
    let dynamic_configuration = Arc::new(MaxConnectionConfig {
        max_conn: 100.into(),
    });
    let pool_manager = test_pool_manager(Arc::clone(&dynamic_configuration));

    assert_eq!(
        2,
        pool_manager.report_pool_stats().await.len(),
        "by default only 2 system pools exist"
    );

    for _ in 0..10 {
        let shared_pool_result = pool_manager.get_system_shared_pool().await;
        assert!(
            shared_pool_result.is_ok(),
            "Couldn't allocate shared system pool"
        );

        let shared_pool = shared_pool_result.unwrap();
        assert_eq!(
            dynamic_configuration.max_conn(),
            shared_pool.status().status().max_size,
            "Should have the same size as declared by MaxConnectionConfig"
        );

        assert_eq!(
            3,
            pool_manager.report_pool_stats().await.len(),
            "2 system pools + 1 shared pool"
        )
    }
}

#[tokio::test]
async fn validate_max_conn_change() {
    let dynamic_configuration = Arc::new(MaxConnectionConfig {
        max_conn: 100.into(),
    });
    let pool_manager = test_pool_manager(Arc::clone(&dynamic_configuration));

    let shared_pool = pool_manager.get_system_shared_pool().await.unwrap();

    // change the max connection
    dynamic_configuration.set_max_conn(42);

    let new_shared_pool = pool_manager.get_system_shared_pool().await.unwrap();

    assert_ne!(
        shared_pool.status().status().max_size,
        new_shared_pool.status().status().max_size,
        "New pool doesn't have updated size"
    );

    assert_eq!(
        4,
        pool_manager.report_pool_stats().await.len(),
        "2 system pool + 2 shared system pool"
    );
}

#[tokio::test]
async fn validate_user_pwd_change() {
    let dynamic_configuration = Arc::new(MaxConnectionConfig {
        max_conn: 100.into(),
    });
    let pool_manager = test_pool_manager(Arc::clone(&dynamic_configuration));

    // on first iteration it will allocate the user pool and all the rest iterations will be no-op
    for _ in 0..10 {
        pool_manager
            .allocate_data_pool("user", "before")
            .await
            .unwrap();

        assert_eq!(
            3,
            pool_manager.report_pool_stats().await.len(),
            "2 system pool + 1 user pool"
        );
    }

    pool_manager
        .allocate_data_pool("user", "after")
        .await
        .unwrap();

    assert_eq!(
        4,
        pool_manager.report_pool_stats().await.len(),
        "2 system pool + 2 user pool"
    );
}
