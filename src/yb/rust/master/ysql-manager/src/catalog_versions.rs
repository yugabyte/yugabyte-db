// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

//! The heartbeat catalog versions cache (`enable_heartbeat_pg_catalog_versions_cache`). The host
//! keeps it, as CatalogManager does, and resets it whenever the ready term changes; the
//! `pg-catalog-versions` controller refreshes it (`YsqlManager::RefreshPgCatalogVersionCacheOnce`)
//! every half heartbeat interval, or after `pg_catalog_versions_cache_retry_ms` when a read fails.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use master_leadership::Leadership;
use master_table_lifecycle::SchemaHost;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use protobuf::Message;
use yb_proto::pb::master_rust_peer::{
    RefreshPgCatalogVersionCacheRequestPB, RefreshPgCatalogVersionCacheResponsePB,
};

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct CatalogVersionsOptions {
    /// `enable_heartbeat_pg_catalog_versions_cache`.
    pub enabled: fn() -> bool,
    /// Half of `heartbeat_interval_ms`.
    pub interval: fn() -> Duration,
    /// `pg_catalog_versions_cache_retry_ms`.
    pub retry: fn() -> Duration,
}

/// Refreshes the host's cache in the term `term`. Blocks. False when the read failed.
pub fn refresh(host: &dyn SchemaHost, term: i64) -> bool {
    let mut req = RefreshPgCatalogVersionCacheRequestPB::new();
    req.set_term(term);
    let Ok(bytes) = req.write_to_bytes() else {
        return false;
    };
    match host.call("RefreshPgCatalogVersionCache", &bytes) {
        Ok(response) => RefreshPgCatalogVersionCacheResponsePB::parse_from_bytes(&response)
            .is_ok_and(|resp| resp.refreshed()),
        Err(status) => {
            tracing::warn!("Catalog versions refresh failed: {status}");
            false
        }
    }
}

/// The `pg-catalog-versions` controller for one term.
pub struct CatalogVersionsController {
    host: Arc<dyn SchemaHost>,
    leadership: Arc<Leadership>,
    options: CatalogVersionsOptions,
    failed: AtomicBool,
}

impl CatalogVersionsController {
    /// A controller for the term that just began.
    pub fn new(
        host: Arc<dyn SchemaHost>,
        leadership: Arc<Leadership>,
        options: CatalogVersionsOptions,
    ) -> Arc<CatalogVersionsController> {
        Arc::new(CatalogVersionsController {
            host,
            leadership,
            options,
            failed: AtomicBool::new(false),
        })
    }
}

impl Controller for CatalogVersionsController {
    fn name(&self) -> &'static str {
        "pg-catalog-versions"
    }

    fn interval(&self) -> Duration {
        if self.failed.load(Ordering::Relaxed) {
            (self.options.retry)()
        } else {
            (self.options.interval)()
        }
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            if !(self.options.enabled)() {
                return;
            }
            let Ok(token) = self.leadership.token() else {
                return;
            };
            let host = self.host.clone();
            let term = token.term();
            let refreshed = tokio::task::spawn_blocking(move || refresh(&*host, term))
                .await
                .unwrap_or(false);
            self.failed.store(!refreshed, Ordering::Relaxed);
        })
    }
}
