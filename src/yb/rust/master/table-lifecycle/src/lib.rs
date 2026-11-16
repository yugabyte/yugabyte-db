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
mod ddl_log;
mod drop;
mod flush;
mod health;
pub mod lookup;
pub mod major_upgrade;
mod namespaces;
mod placement;
mod read;
mod roles;
mod schedules;
mod status_tablets;
mod tablespaces;
mod time;
mod truncate;
mod udtypes;
mod verifier;
mod ysql;

#[cfg(test)]
mod tests;

pub use alter::{defer_schema_push, start_schema_push, TableAlterer};
pub use assign::TabletAssigner;
pub use colocation::{ColocatedTableAdder, TabletRpc};
pub use create::{
    stateful_service_table_name, TransactionTableController, YsqlSystemTablesController,
    YsqlSystemTablesOptions,
};
pub use drop::TableDeleter;
pub use flush::{FlushOptions, Flushes};
pub use placement::{replication_info_set, table_replication_info};
pub use read::{pg_table_all_oids, PgTableAllOids};
pub use schedules::{covering_schedules, Retainer, Retention};
pub use tablespaces::{
    uses_tablespaces, TablespaceController, TablespaceOptions, Tablespaces,
    TRANSACTION_TABLE_PREFIX,
};
pub use time::local_time_string;
pub use truncate::{TruncateOptions, Truncates};
pub use verifier::{DdlVerifier, VerifierOptions};
pub use ysql::{pg_database_oid, pg_table_oid};

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
    /// `enable_truncate_cdcsdk_table`.
    pub enable_truncate_cdcsdk_table: fn() -> bool,
    /// `TEST_running_test`: test sync points are passed on the host.
    pub sync_points: fn() -> bool,
    /// `enable_table_rewrite_for_cdcsdk_table`.
    pub enable_table_rewrite_for_cdcsdk_table: fn() -> bool,
    /// The `cdc_enable_dynamic_schema_changes` AutoFlag.
    pub cdc_enable_dynamic_schema_changes: fn() -> bool,
    /// `enable_ysql`: YSQL needs `system.transactions` from the start.
    pub enable_ysql: fn() -> bool,
    /// `txn_table_wait_min_ts_count`.
    pub txn_table_wait_min_ts_count: fn() -> i32,
    /// `ysql_legacy_colocated_database_creation`: a colocated database gets a parent table of its
    /// own instead of a default tablegroup.
    pub legacy_colocated_database_creation: fn() -> bool,
    /// `autoscale_transaction_tables`.
    pub autoscale_transaction_tables: fn() -> bool,
    /// `ysql_enable_colocated_tables_with_tablespaces`.
    pub colocated_tables_with_tablespaces: fn() -> bool,
    /// `vector_index_store_payload`: new vector indexes store a payload instead of a reverse
    /// mapping.
    pub vector_index_store_payload: fn() -> bool,
    /// `vector_index_backend`.
    pub vector_index_backend: fn() -> String,
    /// `TEST_sequential_colocation_ids`: colocation ids from 20001 up instead of random ones.
    pub sequential_colocation_ids: fn() -> bool,
    /// `TEST_colocation_ids`: comma-separated colocation ids tried before any other.
    pub colocation_ids: fn() -> String,
    /// `transaction_table_check_interval_sec`.
    pub transaction_table_check_interval: fn() -> std::time::Duration,
    /// `use_create_table_leader_hint`: start each new tablet's first election at a chosen
    /// replica, so new tables' leaders are balanced.
    pub use_create_table_leader_hint: fn() -> bool,
    /// `ysql_disable_index_backfill`.
    pub ysql_disable_index_backfill: fn() -> bool,
    /// `disable_index_backfill`: YCQL indexes are built without a backfill.
    pub disable_index_backfill: fn() -> bool,
    /// `disable_index_backfill_for_non_txn_tables`.
    pub disable_index_backfill_for_non_txn_tables: fn() -> bool,
    /// `defer_index_backfill`: a YCQL index waits at DO_BACKFILL for
    /// `LaunchBackfillIndexForTable`.
    pub defer_index_backfill: fn() -> bool,
    /// `allow_batching_non_deferred_indexes`: YCQL indexes of a table ready together are
    /// backfilled together.
    pub allow_batching_non_deferred_indexes: fn() -> bool,
    /// `index_backfill_rpc_timeout_ms`: a YCQL backfill chunk's timeout.
    pub index_backfill_rpc_timeout: fn() -> std::time::Duration,
    /// `ddl_requester_liveness_check_interval_secs`.
    pub ddl_requester_liveness_check_interval: fn() -> std::time::Duration,
    /// `TEST_skip_ddl_requester_liveness_check`.
    pub skip_ddl_requester_liveness_check: fn() -> bool,
    /// `are_nodes_safe_to_take_down_timeout_buffer_ms`.
    pub nodes_safe_to_take_down_buffer: fn() -> std::time::Duration,
    /// `ysql_index_backfill_rpc_timeout_ms`.
    pub ysql_index_backfill_rpc_timeout: fn() -> std::time::Duration,
    /// Tablespace placement.
    pub tablespaces: TablespaceOptions,
    /// `TEST_pause_ddl_rollback`: a DDL transaction's tables keep their DDL state while it is
    /// set, and `IsYsqlDdlVerificationDone` fails.
    pub pause_ddl_rollback: fn() -> bool,
    /// `TEST_simulate_crash_after_table_marked_deleting`: DeleteTable fails after its write.
    pub simulate_crash_after_table_marked_deleting: fn() -> bool,
    /// `TEST_delay_clearing_fully_applied_ms`: the wait before an alter or a backfill start clears
    /// the fully applied schema, which keeps the table ALTERING that much longer.
    pub delay_clearing_fully_applied: fn() -> std::time::Duration,
    /// `TEST_fail_table_creation_at_preparing_state`: a YSQL `CreateTable` fails after
    /// persisting the table, so the DDL transaction has to roll it back.
    pub fail_table_creation_at_preparing_state: fn() -> bool,
    /// `TEST_pause_before_upsert_ysql_sys_table`: a new YSQL system table's row, or a new
    /// database's copied catalog rows, wait to be written while set.
    pub pause_before_upsert_ysql_sys_table: fn() -> bool,
    /// This build's YSQL major version; None when YSQL is off. A catalog on another version is
    /// mid major upgrade.
    pub ysql_major_version: Option<u32>,
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
            enable_truncate_cdcsdk_table: || false,
            sync_points: || false,
            enable_table_rewrite_for_cdcsdk_table: || true,
            cdc_enable_dynamic_schema_changes: || true,
            enable_ysql: || false,
            txn_table_wait_min_ts_count: || 1,
            legacy_colocated_database_creation: || false,
            autoscale_transaction_tables: || false,
            colocated_tables_with_tablespaces: || false,
            vector_index_store_payload: || false,
            vector_index_backend: || "yb_hnsw_hnswlib".to_string(),
            sequential_colocation_ids: || false,
            colocation_ids: String::new,
            transaction_table_check_interval: || std::time::Duration::from_secs(900),
            use_create_table_leader_hint: || true,
            ysql_disable_index_backfill: || false,
            disable_index_backfill: || false,
            disable_index_backfill_for_non_txn_tables: || true,
            defer_index_backfill: || false,
            allow_batching_non_deferred_indexes: || true,
            index_backfill_rpc_timeout: || std::time::Duration::from_secs(30),
            nodes_safe_to_take_down_buffer: || std::time::Duration::from_secs(2),
            ddl_requester_liveness_check_interval: || std::time::Duration::from_secs(10),
            skip_ddl_requester_liveness_check: || false,
            ysql_index_backfill_rpc_timeout: || std::time::Duration::from_secs(300),
            tablespaces: TablespaceOptions {
                enabled: || true,
                refresh_interval: || std::time::Duration::from_secs(30),
                auto_create_local_transaction_tables: || true,
                name_transaction_tables_with_tablespace_id: || false,
            },
            pause_ddl_rollback: || false,
            simulate_crash_after_table_marked_deleting: || false,
            delay_clearing_fully_applied: || std::time::Duration::ZERO,
            fail_table_creation_at_preparing_state: || false,
            pause_before_upsert_ysql_sys_table: || false,
            ysql_major_version: None,
        }
    }
}

/// How an index backfill picks its read time (`XClusterBackfillDecision`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BackfillDecision {
    /// The tablets' safe time, as without xCluster.
    TabletSafeTime,
    /// A backfill on this universe at this hybrid time.
    RunLocalAt(u64),
    /// No backfill: the source's backfill writes, made at this hybrid time, are replicated.
    DeferToReplicated(u64),
}

/// What CDC streams need from tablet creation, splits and hidden-tablet cleanup, which
/// `master-xrepl` provides. Logical replication streams get their retention barriers and
/// `cdc_state` rows when a tablet is created (`cdc_sdk_set_retention_barriers`,
/// `PopulateCDCStateTableOnNewTableCreation`); a split adds the children's rows
/// (`UpdateCDCProducerOnTabletSplit`); split parents and dropped tables are kept hidden until the
/// streams have read them (`retained_by_cdcsdk_`).
pub trait XreplHooks: Send + Sync {
    /// Whether a new tablet of the table gets CDCSDK retention barriers.
    fn retention_barriers(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        table_id: &[u8],
    ) -> bool;

    /// A replica created the tablet with barriers held from `safe_op_id`.
    fn created(
        &self,
        table_id: Vec<u8>,
        tablet_id: Vec<u8>,
        safe_op_id: yb_proto::pb::opid::OpIdPB,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>>;

    /// The children of a split of the table were registered.
    fn split_registered(
        &self,
        table_id: Vec<u8>,
        parent: Vec<u8>,
        children: Vec<Vec<u8>>,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>>;

    /// Whether a tablet is retained (hidden, not deleted) for the streams: a split parent they
    /// have not finished, or a tablet of a dropped table they still read.
    fn retains_hidden_tablet(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        tablet_id: &[u8],
    ) -> bool;

    /// Whether a dropped table is retained for the streams
    /// (`CDCSDKPopulateDeleteRetainerInfoForTableDrop`).
    fn retains_dropped_table(
        &self,
        snapshot: &master_catalog_state::CatalogSnapshot,
        table_id: &[u8],
    ) -> bool;

    /// The xCluster part of a new YSQL table's row (`ProcessCreateTableReq`,
    /// `ValidateCreateTableRequest`): on an automatic mode target, its source table and the
    /// schema packings rows of it replicated before it existed.
    fn xcluster_create_table(
        &self,
        _snapshot: &master_catalog_state::CatalogSnapshot,
        _req: &yb_proto::pb::master_ddl::CreateTableRequestPB,
        _table: &mut yb_proto::pb::catalog_entity_info::SysTablesEntryPB,
    ) -> Result<(), Status> {
        Ok(())
    }

    /// A YSQL DDL on the table committed (`ClearXClusterFieldsAfterYsqlDDL`): what xCluster put in
    /// the row for it is dropped, and rows of its own go in the same write.
    fn xcluster_after_ddl(
        &self,
        _snapshot: &master_catalog_state::CatalogSnapshot,
        _table_id: &[u8],
        _table: &mut yb_proto::pb::catalog_entity_info::SysTablesEntryPB,
        mutation: master_catalog_state::Mutation,
    ) -> Result<master_catalog_state::Mutation, Status> {
        Ok(mutation)
    }

    /// `IsNamespaceInAutomaticDDLMode`: the database replicates in automatic DDL mode, so a DDL
    /// transaction's abort must also restore its tables' next column id.
    fn is_automatic_ddl_mode(
        &self,
        _snapshot: &master_catalog_state::CatalogSnapshot,
        _namespace_id: &[u8],
    ) -> bool {
        false
    }

    /// `TryGetXClusterInfoForIndexBackfill`: how the backfill of `index_ids` on `indexed_table`
    /// picks its read time.
    fn backfill_decision(
        &self,
        _index_ids: Vec<Vec<u8>>,
        _indexed_table_id: Vec<u8>,
    ) -> master_task_runtime::BoxFuture<'_, Result<BackfillDecision, Status>> {
        Box::pin(async { Ok(BackfillDecision::TabletSafeTime) })
    }

    /// `MarkIndexBackfillCompleted`: before the indexes are marked backfilled.
    fn backfill_completed(
        &self,
        _index_ids: Vec<Vec<u8>>,
    ) -> master_task_runtime::BoxFuture<'_, Result<(), Status>> {
        Box::pin(async { Ok(()) })
    }

    /// Work a new table's tablets wait for before it runs (`GetPostTabletCreateTasks`): its
    /// xCluster streams and its place in inbound replication groups. False while it runs; an
    /// error leaves the table PREPARING and fails `IsCreateTableDone`.
    fn post_tablet_create(
        &self,
        _table_id: Vec<u8>,
    ) -> master_task_runtime::BoxFuture<'_, Result<bool, Status>> {
        Box::pin(async { Ok(true) })
    }
}

/// A schema version and the DDL transaction whose `AlterTable` made it.
pub(crate) type AlterTxn = (u32, Vec<u8>);

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
    /// Whether a dropped table must be hidden; set once the snapshot coordinator exists.
    pub(crate) retention: std::sync::OnceLock<Arc<dyn Retention>>,
    /// What CDC streams need from DDL; set once the CDC streams service exists.
    pub(crate) xrepl_hooks: std::sync::OnceLock<Arc<dyn XreplHooks>>,
    /// Tables whose post tablet creation work failed, with the error `IsCreateTableDone` reports
    /// (`SetCreateTableErrorStatus`). Kept in memory, as C++ keeps it on the TableInfo.
    pub(crate) create_errors: std::sync::Mutex<std::collections::HashMap<Vec<u8>, Status>>,
    /// Held while a tablespace's transaction status table is created
    /// (`tablespace_transaction_table_creation_mutex_`).
    pub(crate) local_transaction_tables: tokio::sync::Mutex<()>,
    /// The DDL transaction of each YSQL `AlterTable` request, by table, with the schema version it
    /// made. Only the `AlterSchema` of that version aborts other transactions on the tablets, as
    /// only `AsyncAlterTable`s that `SendAlterTableRequest` gets the request for do; alters an
    /// index, the DDL verifier or a new leader send do not.
    pub(crate) alter_txns: std::sync::Mutex<std::collections::HashMap<Vec<u8>, AlterTxn>>,
    /// By indexed table: the schema version whose backfill PG asked for and the asking DDL
    /// transaction, until that backfill starts (`SetPendingBackfillRequesterTransaction`).
    pub(crate) backfill_requesters: std::sync::Mutex<
        std::collections::HashMap<Vec<u8>, (u32, yb_proto::pb::common::TransactionMetadataPB)>,
    >,
}

impl TableLifecycle {
    /// Gives drops the snapshot coordinator's retention rules.
    pub fn set_retention(&self, retention: Arc<dyn Retention>) {
        let _ = self.retention.set(retention);
    }

    /// The retention rules: the coordinator's, or schedules alone before it is set.
    pub fn retention(&self) -> Arc<dyn Retention> {
        self.retention
            .get()
            .cloned()
            .unwrap_or_else(|| Arc::new(schedules::SchedulesOnly))
    }

    /// A C++ test sync point (`TEST_SYNC_POINT`), which itests order themselves against with
    /// `LoadDependency`.
    pub(crate) async fn sync_point(&self, name: &'static str) {
        use protobuf::Message;
        if !(self.options.sync_points)() {
            return;
        }
        let mut req = yb_proto::pb::master_rust_peer::TestSyncPointCallbackRequestPB::new();
        req.set_name(name.to_string());
        let Ok(bytes) = req.write_to_bytes() else {
            return;
        };
        let host = self.host.clone();
        if let Ok(Err(s)) =
            tokio::task::spawn_blocking(move || host.call("TestSyncPointCallback", &bytes)).await
        {
            tracing::warn!("Sync point {name}: {s}");
        }
    }

    /// Gives DDL what CDC streams need from it.
    pub fn set_xrepl_hooks(&self, hooks: Arc<dyn XreplHooks>) {
        let _ = self.xrepl_hooks.set(hooks);
    }

    /// What CDC streams need from DDL, once set.
    pub fn xrepl_hooks(&self) -> Option<Arc<dyn XreplHooks>> {
        self.xrepl_hooks.get().cloned()
    }

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
            alter_txns: Default::default(),
            backfill_requesters: Default::default(),
            tablet_rpc: std::sync::OnceLock::new(),
            retention: std::sync::OnceLock::new(),
            xrepl_hooks: std::sync::OnceLock::new(),
            create_errors: Default::default(),
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

impl TableLifecycle {
    /// Waits while `TEST_pause_before_upsert_ysql_sys_table` is set, as C++'s
    /// `CompleteCreateYsqlSysTable` does before its write.
    pub(crate) async fn pause_before_upsert_ysql_sys_table(&self) {
        if !(self.options.pause_before_upsert_ysql_sys_table)() {
            return;
        }
        tracing::info!("TEST: pausing before writing YSQL system table rows");
        while (self.options.pause_before_upsert_ysql_sys_table)() {
            tokio::time::sleep(std::time::Duration::from_millis(100)).await;
        }
    }
}

/// `STATUS_EC_FORMAT(.., MasterError(code), ..)`: "<message>: <CODE>", with the code encoded.
pub(crate) fn tagged_error(
    code: Code,
    status: StatusCode,
    message: impl Into<String>,
) -> MasterErrorPB {
    master_ts_manager::master_error_tagged(code, status, &message.into())
}

/// `STATUS(Kind, msg, msg2, MasterError(code))`: the code encoded, the message as given.
pub(crate) fn coded_error(
    code: Code,
    status: StatusCode,
    message: impl Into<String>,
) -> MasterErrorPB {
    master_ts_manager::master_error_coded(code, status, &message.into())
}

/// The error for a failed catalog write.
pub(crate) fn write_error(status: &Status) -> MasterErrorPB {
    match status.code {
        StatusCode::IllegalState | StatusCode::Aborted | StatusCode::ServiceUnavailable => {
            coded_error(
                Code::NOT_THE_LEADER,
                StatusCode::ServiceUnavailable,
                format!(
                    "Operation requested can only be executed on a leader master, but this master \
                 is no longer the leader: {}",
                    status.message
                ),
            )
        }
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
