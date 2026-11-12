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
mod colocation;
mod create;
mod drop;
mod flush;
pub mod lookup;
mod namespaces;
mod placement;
mod read;
mod schedules;
mod tablespaces;
mod time;
mod truncate;
mod verifier;
mod ysql;

#[cfg(test)]
mod tests;

pub use alter::TableAlterer;
pub use assign::TabletAssigner;
pub use colocation::{ColocatedTableAdder, TabletRpc};
pub use create::{
    stateful_service_table_name, TransactionTableController, YsqlSystemTablesController,
    YsqlSystemTablesOptions,
};
pub use drop::TableDeleter;
pub use flush::{FlushOptions, Flushes};
pub use placement::{replication_info_set, table_replication_info};
pub use schedules::covering_schedules;
pub use tablespaces::{
    uses_tablespaces, TablespaceController, TablespaceOptions, Tablespaces,
    TRANSACTION_TABLE_PREFIX,
};
pub use time::local_time_string;
pub use truncate::{TruncateOptions, Truncates};
pub use verifier::{DdlVerifier, VerifierOptions};

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
    /// Tablespace placement.
    pub tablespaces: TablespaceOptions,
    /// `TEST_pause_ddl_rollback`: `IsYsqlDdlVerificationDone` fails.
    pub pause_ddl_rollback: fn() -> bool,
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
            tablespaces: TablespaceOptions {
                enabled: || true,
                refresh_interval: || std::time::Duration::from_secs(30),
                auto_create_local_transaction_tables: || true,
                name_transaction_tables_with_tablespace_id: || false,
            },
            pause_ddl_rollback: || false,
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
    /// The YSQL databases being created, by term and id: the rest of the PREPARING ones were
    /// interrupted by a leader change.
    pub(crate) creating_databases: std::sync::Mutex<std::collections::HashSet<(i64, Vec<u8>)>>,
    /// The tablespace placement the leader knows.
    pub(crate) tablespaces: Arc<Tablespaces>,
    /// Rollbacks to a sub-transaction, by transaction id.
    pub(crate) subtxn_rollbacks:
        std::sync::Mutex<std::collections::HashMap<Vec<u8>, ysql::SubTxnRollback>>,
    /// Tserver RPCs for the DDL paths that call tservers directly; set once the master's RPC
    /// client exists.
    pub(crate) tablet_rpc: std::sync::OnceLock<TabletRpc>,
    /// Held while a tablespace's transaction status table is created
    /// (`tablespace_transaction_table_creation_mutex_`).
    pub(crate) local_transaction_tables: tokio::sync::Mutex<()>,
}

impl TableLifecycle {
    /// Gives the DDL paths that call tservers directly the master's RPC client.
    pub fn set_tablet_rpc(&self, rpc: TabletRpc) {
        let _ = self.tablet_rpc.set(rpc);
    }

    /// `WaitForTransactionTableVersionUpdateToPropagate`: every live tserver gets the current
    /// transaction tables version (`UpdateTransactionTablesVersion`); the first failure is
    /// returned. Without an RPC client (tests) the next heartbeats carry it.
    pub(crate) async fn propagate_transaction_tables_version(
        &self,
        token: &LeaderToken,
    ) -> Result<(), yb_host_api::Status> {
        use protobuf::Message;
        use yb_proto::pb::tserver_admin::{
            UpdateTransactionTablesVersionRequestPB, UpdateTransactionTablesVersionResponsePB,
        };
        let Some(rpc) = self.tablet_rpc.get() else {
            return Ok(());
        };
        let version = self
            .catalog
            .snapshot()
            .get(
                yb_proto::pb::master_types::SysRowEntryType::SYS_CONFIG,
                master_catalog_state::defaults::TRANSACTION_TABLES_CONFIG_ID,
            )
            .and_then(|r| {
                r.parse::<yb_proto::pb::catalog_entity_info::SysConfigEntryPB>()
                    .ok()
            })
            .map_or(0, |c| c.transaction_tables_config().version());
        let mut req = UpdateTransactionTablesVersionRequestPB::new();
        req.set_version(version);
        let bytes = req.write_to_bytes().map_err(|e| {
            yb_host_api::Status::new(yb_host_api::StatusCode::Corruption, e.to_string())
        })?;
        let timeout = (rpc.rpc_timeout)();
        let mut calls = tokio::task::JoinSet::new();
        for descriptor in self.ts_manager.live() {
            let Some((host, port)) = self.ts_manager.rpc_address(&descriptor.uuid) else {
                continue;
            };
            let client = rpc.rpc.clone();
            let bytes = bytes.clone();
            calls.spawn(async move {
                let resp = client
                    .call(
                        &host,
                        port,
                        "yb.tserver.TabletServerAdminService",
                        "UpdateTransactionTablesVersion",
                        &bytes,
                        timeout,
                    )
                    .await?;
                let resp = UpdateTransactionTablesVersionResponsePB::parse_from_bytes(&resp)
                    .map_err(|e| {
                        yb_host_api::Status::new(yb_host_api::StatusCode::Corruption, e.to_string())
                    })?;
                match resp.error.as_ref() {
                    Some(e) => Err(crate::backfill::ts_status(&e.status)),
                    None => Ok(()),
                }
            });
        }
        self.leadership.check(token)?;
        while let Some(result) = calls.join_next().await {
            result.map_err(|e| {
                yb_host_api::Status::new(yb_host_api::StatusCode::InternalError, e.to_string())
            })??;
        }
        Ok(())
    }

    /// The tablespace placement the leader knows, for everything else that places tablets.
    pub fn tablespaces(&self) -> Arc<Tablespaces> {
        self.tablespaces.clone()
    }

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
            creating_databases: Default::default(),
            tablespaces: Tablespaces::new(options.tablespaces),
            subtxn_rollbacks: Default::default(),
            local_transaction_tables: tokio::sync::Mutex::new(()),
            tablet_rpc: std::sync::OnceLock::new(),
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
