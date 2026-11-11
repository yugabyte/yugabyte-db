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

//! Namespaces and tables on the leader (W7): lookups, the namespace and table DDL RPCs, and the
//! controllers that assign new tablets and delete dropped ones. See docs/table-lifecycle.md.
//!
//! Every DDL is one catalog mutation; what happens after it (sending `CreateTablet`,
//! `ChangeMetadata` or `DeleteTablet`, and finishing the table) is a controller that works from
//! the persisted rows alone, so a new leader resumes it (K1).

use std::sync::Arc;
use tokio::time::Instant;

use master_catalog_state::{Catalog, CatalogWriter, Mutation, WriteOptions};
use master_leadership::{LeaderToken, Leadership};
use master_ts_manager::{master_error, TsManager};
use rand::RngCore;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB};

pub(crate) mod alter;
mod assign;
mod backfill;
mod create;
mod drop;
pub mod lookup;
mod namespaces;
mod placement;
mod read;
mod schedules;
mod time;
mod ysql;

#[cfg(test)]
mod tests;

pub use alter::TableAlterer;
pub use assign::TabletAssigner;
pub use create::TransactionTableController;
pub use drop::TableDeleter;
pub use placement::{replication_info_set, table_replication_info};
pub use schedules::covering_schedules;
pub use time::local_time_string;

/// The C++ steps the host runs: schema normalization, partitioning and alter steps, which are
/// engine formats Rust does not reimplement (K4). `method` is a `MasterHost::HandleOnHost` name.
pub trait SchemaHost: Send + Sync + 'static {
    /// Serves `method` with the serialized `request`; returns the serialized response. Blocks.
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status>;
}

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct LifecycleOptions {
    /// `replication_factor`, for tables and clusters with no replication info.
    pub replication_factor: fn() -> i32,
    /// `max_create_tablets_per_ts`.
    pub max_create_tablets_per_ts: fn() -> i32,
    /// `catalog_manager_check_ts_count_for_create_table`.
    pub check_ts_count_for_create_table: fn() -> bool,
    /// `tablet_creation_timeout_ms`.
    pub tablet_creation_timeout_ms: fn() -> i64,
    /// The `use_parent_table_id_field` AutoFlag: new tablets name their table by parent id.
    pub use_parent_table_id_field: fn() -> bool,
    /// `transaction_table_num_tablets`; 0 for the per-tserver count.
    pub transaction_table_num_tablets: fn() -> i32,
    /// `transaction_table_num_tablets_per_tserver`; -1 to auto-detect.
    pub transaction_table_num_tablets_per_tserver: fn() -> i32,
    /// `enable_delete_truncate_xcluster_replicated_table`.
    pub enable_delete_truncate_xcluster_replicated_table: fn() -> bool,
    /// `enable_ysql`: YSQL needs `system.transactions` from the start.
    pub enable_ysql: fn() -> bool,
    /// `txn_table_wait_min_ts_count`.
    pub txn_table_wait_min_ts_count: fn() -> i32,
    /// `use_create_table_leader_hint`: start each new tablet's first election at a chosen
    /// replica, so new tables' leaders are balanced.
    pub use_create_table_leader_hint: fn() -> bool,
    /// `ysql_disable_index_backfill`.
    pub ysql_disable_index_backfill: fn() -> bool,
    /// `ysql_index_backfill_rpc_timeout_ms`.
    pub ysql_index_backfill_rpc_timeout: fn() -> std::time::Duration,
}

impl Default for LifecycleOptions {
    fn default() -> LifecycleOptions {
        LifecycleOptions {
            replication_factor: || 3,
            max_create_tablets_per_ts: || 50,
            check_ts_count_for_create_table: || true,
            tablet_creation_timeout_ms: || 600_000,
            use_parent_table_id_field: || true,
            transaction_table_num_tablets: || 0,
            transaction_table_num_tablets_per_tserver: || -1,
            enable_delete_truncate_xcluster_replicated_table: || false,
            enable_ysql: || false,
            txn_table_wait_min_ts_count: || 1,
            use_create_table_leader_hint: || true,
            ysql_disable_index_backfill: || false,
            ysql_index_backfill_rpc_timeout: || std::time::Duration::from_secs(300),
        }
    }
}

/// Namespace and table DDL on the leader.
pub struct TableLifecycle {
    pub(crate) catalog: Arc<Catalog>,
    pub(crate) writer: Arc<CatalogWriter>,
    pub(crate) leadership: Arc<Leadership>,
    pub(crate) ts_manager: Arc<TsManager>,
    pub(crate) host: Arc<dyn SchemaHost>,
    pub(crate) options: LifecycleOptions,
    /// Held across the check and the write of anything that must be unique by name (namespace and
    /// table names). The leader is the only writer, and a mutation cannot read the absence of a
    /// name, only of a row.
    pub(crate) names: tokio::sync::Mutex<()>,
    /// The tables each DDL transaction's verification altered, until their tablets have the
    /// alter (`AddDdlTxnWaitingForSchemaVersion`).
    pub(crate) ddl_alters: std::sync::Mutex<std::collections::HashMap<Vec<u8>, Vec<Vec<u8>>>>,
}

impl TableLifecycle {
    /// Creates the leader's DDL service.
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        host: Arc<dyn SchemaHost>,
        options: LifecycleOptions,
    ) -> Arc<TableLifecycle> {
        Arc::new(TableLifecycle {
            catalog,
            writer,
            leadership,
            ts_manager,
            host,
            options,
            names: tokio::sync::Mutex::new(()),
            ddl_alters: Default::default(),
        })
    }

    /// Writes the mutation `build` returns, rebuilding it on conflict. `build` checks the request
    /// against the latest snapshot; a refusal writes nothing and is returned as is. A failed write
    /// is `NOT_THE_LEADER` when leadership was lost, as `CheckIfNoLongerLeader` maps it.
    pub(crate) async fn write<T>(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        build: impl FnMut(
            &master_catalog_state::CatalogSnapshot,
        ) -> Result<(Mutation, T), MasterErrorPB>,
    ) -> Result<T, MasterErrorPB> {
        let mut build = build;
        let options = WriteOptions {
            deadline: Some(deadline),
            ..WriteOptions::default()
        };
        let result = self
            .writer
            .mutate_with(token, options, |snapshot| {
                Ok(match build(snapshot) {
                    Ok((mutation, value)) => (mutation, Ok(value)),
                    Err(error) => (Mutation::new(), Err(error)),
                })
            })
            .await;
        match result {
            Ok((value, _)) => value,
            Err(status) => Err(write_error(&status)),
        }
    }
}

/// A `MasterErrorPB` with `code` and a status.
pub(crate) fn error(code: Code, status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    master_error(code, status, &message.into())
}

/// The error for a failed catalog write.
pub(crate) fn write_error(status: &Status) -> MasterErrorPB {
    match status.code {
        StatusCode::IllegalState | StatusCode::Aborted | StatusCode::ServiceUnavailable => error(
            Code::NOT_THE_LEADER,
            StatusCode::ServiceUnavailable,
            format!(
                "Operation requested can only be executed on a leader master, but this master \
                 is no longer the leader: {}",
                status.message
            ),
        ),
        _ => error(Code::UNKNOWN_ERROR, status.code, status.message.clone()),
    }
}

/// `GenerateObjectId`: a random version-4 UUID as 32 lowercase hex characters.
pub fn generate_object_id() -> Vec<u8> {
    let mut bytes = [0u8; 16];
    rand::rng().fill_bytes(&mut bytes);
    bytes[6] = (bytes[6] & 0x0f) | 0x40;
    bytes[8] = (bytes[8] & 0x3f) | 0x80;
    bytes
        .iter()
        .flat_map(|b| format!("{b:02x}").into_bytes())
        .collect()
}
