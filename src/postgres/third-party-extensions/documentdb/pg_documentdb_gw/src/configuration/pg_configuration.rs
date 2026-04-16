/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * src/configuration/pg_configuration.rs
 *
 *-------------------------------------------------------------------------
 */

use std::{collections::HashMap, sync::Arc};

use async_trait::async_trait;
use bson::{rawbson, RawBson};
use serde::Deserialize;
use tokio::{
    sync::RwLock,
    time::{Duration, Instant},
};

use crate::{
    configuration::{dynamic::POSTGRES_RECOVERY_KEY, DynamicConfiguration, SetupConfiguration},
    error::{DocumentDBError, Result},
    postgres::{Connection, ConnectionPool, QueryCatalog},
    requests::request_tracker::RequestTracker,
};

#[derive(Debug, Deserialize, Default, Clone)]
#[serde(rename_all = "PascalCase")]
pub struct HostConfig {
    #[serde(default)]
    is_primary: String,
    #[serde(default)]
    send_shutdown_responses: String,
}

/// Inner struct that holds the dependencies needed for loading configurations.
#[derive(Debug, Clone)]
struct PgConfigurationInner {
    query_catalog: QueryCatalog,
    dynamic_config_file: String,
    settings_prefixes: Vec<String>,
    system_requests_pool: Arc<ConnectionPool>,
}

impl PgConfigurationInner {
    /// Loads configurations from the database and config file using the provided connection.
    pub async fn load_configurations(&self, conn: &Connection) -> Result<HashMap<String, String>> {
        let mut configs = HashMap::new();

        match Self::load_host_config(&self.dynamic_config_file).await {
            Ok(host_config) => {
                configs.insert(
                    "IsPrimary".to_string(),
                    host_config.is_primary.to_lowercase(),
                );
                configs.insert(
                    "SendShutdownResponses".to_string(),
                    host_config.send_shutdown_responses.to_lowercase(),
                );
            }
            Err(e) => tracing::warn!("Host Config file not able to be loaded: {e}"),
        }

        let mut request_tracker = RequestTracker::new();
        let pg_config_rows = conn
            .query(
                self.query_catalog.pg_settings(),
                &[],
                &[],
                None,
                &mut request_tracker,
            )
            .await?;
        for pg_config in pg_config_rows {
            let mut key = pg_config.get::<_, String>(0);

            for settings_prefix in &self.settings_prefixes {
                if key.starts_with(settings_prefix) {
                    key = key[settings_prefix.len()..].to_string();
                    break;
                }
            }

            let mut value: String = pg_config.get(1);
            if value == "on" {
                value = "true".to_string();
            } else if value == "off" {
                value = "false".to_string();
            }
            configs.insert(key.to_owned(), value);
        }

        let pg_is_in_recovery_row = conn
            .query(
                self.query_catalog.pg_is_in_recovery(),
                &[],
                &[],
                None,
                &mut request_tracker,
            )
            .await?;
        let in_recovery: bool = pg_is_in_recovery_row.first().is_some_and(|row| row.get(0));
        configs.insert(POSTGRES_RECOVERY_KEY.to_string(), in_recovery.to_string());

        tracing::info!("Dynamic configurations loaded: {configs:?}");
        Ok(configs)
    }

    async fn load_host_config(dynamic_config_file: &str) -> Result<HostConfig> {
        let config: HostConfig = serde_json::from_str(
            &tokio::fs::read_to_string(dynamic_config_file).await?,
        )
        .map_err(|e| DocumentDBError::internal_error(format!("Failed to read config file: {e}")))?;
        Ok(config)
    }
}

#[derive(Debug)]
pub struct PgConfiguration {
    inner: PgConfigurationInner,
    values: RwLock<HashMap<String, String>>,
    last_update_at: RwLock<Instant>,
}

impl PgConfiguration {
    fn start_dynamic_configuration_refresh_thread(
        configuration: Arc<PgConfiguration>,
        refresh_interval: u32,
    ) {
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(Duration::from_secs(refresh_interval as u64));
            interval.tick().await;

            loop {
                interval.tick().await;

                match configuration
                    .inner
                    .system_requests_pool
                    .acquire_connection()
                    .await
                {
                    Ok(inner_conn) => {
                        let connection = Connection::new(inner_conn, false);
                        if let Err(e) = configuration
                            .reload_configuration_with_connection(&connection)
                            .await
                        {
                            tracing::error!("Failed to refresh configuration: {e}");
                        }
                    }
                    Err(e) => {
                        tracing::error!(
                            "Failed to acquire postgres connection to refresh configuration: {e}"
                        )
                    }
                }
            }
        });
    }

    pub async fn new(
        query_catalog: &QueryCatalog,
        setup_configuration: &dyn SetupConfiguration,
        system_requests_pool: &Arc<ConnectionPool>,
        settings_prefixes: Vec<String>,
    ) -> Result<Arc<Self>> {
        let connection = Connection::new(system_requests_pool.acquire_connection().await?, false);
        let dynamic_config_file = setup_configuration.dynamic_configuration_file();

        let inner = PgConfigurationInner {
            query_catalog: query_catalog.clone(),
            dynamic_config_file,
            settings_prefixes,
            system_requests_pool: system_requests_pool.clone(),
        };

        let values = RwLock::new(inner.load_configurations(&connection).await?);

        let configuration = Arc::new(PgConfiguration {
            inner,
            values,
            last_update_at: RwLock::new(Instant::now()),
        });

        let refresh_interval = setup_configuration.dynamic_configuration_refresh_interval_secs();
        Self::start_dynamic_configuration_refresh_thread(configuration.clone(), refresh_interval);

        Ok(configuration)
    }

    pub async fn last_update_at(&self) -> Instant {
        *self.last_update_at.read().await
    }

    pub async fn reload_configuration_with_connection(&self, conn: &Connection) -> Result<()> {
        let new_config = match self.inner.load_configurations(conn).await {
            Ok(config) => config,
            Err(e) => {
                tracing::error!("Failed to reload configuration: {e}");
                return Err(e);
            }
        };

        {
            let mut config_writable = self.values.write().await;
            *config_writable = new_config;
        }

        {
            let mut last_update = self.last_update_at.write().await;
            *last_update = Instant::now();
        }

        Ok(())
    }
}

#[async_trait]
impl DynamicConfiguration for PgConfiguration {
    async fn get_str(&self, key: &str) -> Option<String> {
        self.values.read().await.get(key).cloned()
    }

    async fn get_bool(&self, key: &str, default: bool) -> bool {
        let ret = self
            .values
            .read()
            .await
            .get(key)
            .map(|v| v.parse::<bool>().unwrap_or(default))
            .unwrap_or(default);
        ret
    }

    async fn get_i32(&self, key: &str, default: i32) -> i32 {
        let ret = self
            .values
            .read()
            .await
            .get(key)
            .map(|v| v.parse::<i32>().unwrap_or(default))
            .unwrap_or(default);
        ret
    }

    async fn get_u64(&self, key: &str, default: u64) -> u64 {
        let ret = self
            .values
            .read()
            .await
            .get(key)
            .map(|v| v.parse::<u64>().unwrap_or(default))
            .unwrap_or(default);
        ret
    }

    async fn equals_value(&self, key: &str, value: &str) -> bool {
        let ret = self
            .values
            .read()
            .await
            .get(key)
            .map(|v| v == value)
            .unwrap_or(false);
        ret
    }

    fn topology(&self) -> RawBson {
        let empty_doc: RawBson = rawbson!({});
        empty_doc
    }

    async fn enable_developer_explain(&self) -> bool {
        self.get_bool("enableDeveloperExplain", false).await
    }

    async fn max_connections(&self) -> usize {
        let max_connections = self.get_i32("max_connections", -1).await;
        match max_connections {
            n if n < 0 => {
                // theoretically we can't end up here, since Postgres always provide values
                tracing::error!("GUC max_connections is not setup correctly");
                return 25usize;
            }
            n => n as usize,
        }
    }

    async fn allow_transaction_snapshot(&self) -> bool {
        self.get_bool("mongoAllowTransactionSnapshot", false).await
    }

    fn as_any(&self) -> &dyn std::any::Any {
        self
    }
}
