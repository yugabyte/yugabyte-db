/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/context/service.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{sync::Arc, time::Duration};

use crate::{
    configuration::{DynamicConfiguration, SetupConfiguration},
    context::{CursorStore, TransactionStore},
    postgres::{PoolManager, QueryCatalog},
    service::TlsProvider,
};

pub struct ServiceContextInner {
    pub setup_configuration: Box<dyn SetupConfiguration>,
    pub dynamic_configuration: Arc<dyn DynamicConfiguration>,
    pub connection_pool_manager: PoolManager,
    pub cursor_store: CursorStore,
    pub transaction_store: TransactionStore,
    pub query_catalog: QueryCatalog,
    pub tls_provider: TlsProvider,
}

#[derive(Clone)]
pub struct ServiceContext(Arc<ServiceContextInner>);

impl ServiceContext {
    pub fn new(
        setup_configuration: Box<dyn SetupConfiguration>,
        dynamic_configuration: Arc<dyn DynamicConfiguration>,
        query_catalog: QueryCatalog,
        connection_pool_manager: PoolManager,
        tls_provider: TlsProvider,
    ) -> Self {
        tracing::info!("Initial dynamic configuration: {dynamic_configuration:?}");

        let timeout_secs = setup_configuration.transaction_timeout_secs();
        let cursor_store = CursorStore::new(dynamic_configuration.clone(), true);

        let inner = ServiceContextInner {
            setup_configuration,
            dynamic_configuration,
            connection_pool_manager,
            cursor_store,
            transaction_store: TransactionStore::new(Duration::from_secs(timeout_secs)),
            query_catalog,
            tls_provider,
        };
        ServiceContext(Arc::new(inner))
    }

    pub fn cursor_store(&self) -> &CursorStore {
        &self.0.cursor_store
    }

    pub fn setup_configuration(&self) -> &dyn SetupConfiguration {
        self.0.setup_configuration.as_ref()
    }

    pub fn dynamic_configuration(&self) -> Arc<dyn DynamicConfiguration> {
        self.0.dynamic_configuration.clone()
    }

    pub fn transaction_store(&self) -> &TransactionStore {
        &self.0.transaction_store
    }

    pub fn query_catalog(&self) -> &QueryCatalog {
        &self.0.query_catalog
    }

    pub fn tls_provider(&self) -> &TlsProvider {
        &self.0.tls_provider
    }

    pub fn connection_pool_manager(&self) -> &PoolManager {
        &self.0.connection_pool_manager
    }
}
