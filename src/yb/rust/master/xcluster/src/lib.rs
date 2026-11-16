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

//! xCluster on a Rust leader (`src/yb/master/xcluster/`).
//!
//! The source side is the DB-scoped outbound replication groups
//! (`XCLUSTER_OUTBOUND_REPLICATION_GROUP` rows): each namespace's tables get an xCluster stream,
//! checkpointed by a task per CHECKPOINTING namespace, and a table created later gets its stream
//! before it runs. The streams themselves are master-xrepl's. The other universe's masters are
//! called through the host's `XClusterRemoteClientHolder` (K9).

mod alter;
mod autoflags;
mod automatic;
mod checkpoint;
mod config;
mod consumer;
mod controller;
mod failover;
mod inbound;
mod legacy;
mod outbound;
mod persistence;
mod remote;
mod safe_time;
mod setup;
mod splits;
mod target_tables;
#[cfg(test)]
mod tests;

pub use controller::XClusterController;
pub use failover::FailoverSnapshots;
pub use remote::RemoteMasters;

use std::collections::HashMap;
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::{Catalog, CatalogSnapshot, CatalogWriter};
use master_leadership::Leadership;
use master_ts_manager::TsManager;
use master_xrepl::{CreatesTables, Xrepl};
use protobuf::Message;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, SysTablesEntryPB};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

/// `kDDLQueuePgSchemaName`: the schema of the DDL replication extension's tables.
pub const DDL_QUEUE_PG_SCHEMA_NAME: &str = "yb_xcluster_ddl_replication";
/// `kDDLQueueTableName`.
pub const DDL_QUEUE_TABLE_NAME: &str = "ddl_queue";
/// `kDDLReplicatedTableName`.
pub const DDL_REPLICATED_TABLE_NAME: &str = "replicated_ddls";
/// `kPgSequencesDataTableId`: `system_postgres.sequences_data`.
pub const SEQUENCES_DATA_TABLE_ID: &str = "0000ffff00003000800000000000ffff";
/// `kSequencesDataAliasTableIdMid`.
const SEQUENCES_DATA_ALIAS_MID: &str = ".sequences_data_for.";

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct XClusterOptions {
    /// `enable_xcluster_api_v2`.
    pub api_v2: fn() -> bool,
    /// `xcluster_enable_ddl_replication`.
    pub ddl_replication: fn() -> bool,
    /// `TEST_force_automatic_ddl_replication_mode`.
    pub force_automatic_ddl_mode: fn() -> bool,
    /// `max_xcluster_streams_to_checkpoint_in_parallel`.
    pub max_streams_to_checkpoint: fn() -> u32,
    /// `xcluster_ysql_statement_timeout_sec`.
    pub ysql_statement_timeout: fn() -> Duration,
    /// `enable_xcluster_wal_anchor_stream_infra` and `enable_xcluster_wal_anchor_stream`.
    pub wal_anchor_streams: fn() -> bool,
    /// `xcluster_enable_target_applied_filter`.
    pub target_applied_filter: fn() -> bool,
    /// `TEST_xcluster_fail_table_stream_checkpoint`.
    pub fail_table_stream_checkpoint: fn() -> bool,
    /// `TEST_block_xcluster_checkpoint_namespace_task`.
    pub block_checkpoint_namespace_task: fn() -> bool,
    /// `TEST_simulate_EnsureSequenceUpdatesAreInWal_failure`.
    pub fail_ensure_sequence_updates: fn() -> bool,
    /// `cdc_wal_retention_time_secs`: how long a dropped table's streams keep it.
    pub wal_retention: fn() -> Duration,
    /// `skip_fields_moved_to_xcluster_guarded_info`.
    pub skip_fields_moved_to_guarded_info: fn() -> bool,
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
    /// `catalog_manager_bg_task_wait_ms`.
    pub bg_task_interval: fn() -> Duration,
    /// The `enable_xcluster_auto_flag_validation` AutoFlag.
    pub auto_flag_validation: fn() -> bool,
    /// `xcluster_ensure_sequence_updates_in_wal_timeout_sec`.
    pub ensure_sequence_updates_timeout: fn() -> Duration,
    /// `cdc_read_rpc_timeout_ms`: calls to the other universe's masters.
    pub remote_timeout: fn() -> Duration,
    /// `check_bootstrap_required`.
    pub check_bootstrap_required: fn() -> bool,
    /// `TEST_allow_ycql_transactional_xcluster`.
    pub allow_ycql_transactional: fn() -> bool,
    /// `TEST_pause_at_start_of_setup_replication_group_ms`.
    pub pause_at_start_of_setup: fn() -> Duration,
    /// `TEST_fail_universe_replication_merge`.
    pub fail_universe_replication_merge: fn() -> bool,
    /// `TEST_exit_unfinished_merging`: an alter reports success without merging.
    pub exit_unfinished_merging: fn() -> bool,
    /// `TEST_exit_unfinished_deleting`: a delete leaves the group DELETING.
    pub exit_unfinished_deleting: fn() -> bool,
    /// `disable_universe_gc`.
    pub disable_universe_gc: fn() -> bool,
    /// `xcluster_failover_task_timeout_secs`: the steps before the restorations.
    pub failover_task_timeout: fn() -> Duration,
    /// `xcluster_failover_restoration_wait_secs`, per namespace.
    pub failover_restoration_wait: fn() -> Duration,
    /// `xcluster_failover_restoration_poll_interval_ms`.
    pub failover_restoration_poll_interval: fn() -> Duration,
    /// `xcluster_failover_snapshot_delete_retry_secs`.
    pub failover_snapshot_delete_retry: fn() -> Duration,
    /// `xcluster_failover_snapshot_retention_hours`.
    pub failover_snapshot_retention_hours: fn() -> i32,
    /// `TEST_pause_xcluster_failover_before_delete_replication`.
    pub pause_failover_before_delete: fn() -> bool,
    /// `TEST_xcluster_failover_fail_restore_namespace_id`.
    pub failover_fail_restore_namespace: fn() -> String,
    /// `TEST_xcluster_failover_fail_create_snapshot`.
    pub failover_fail_create_snapshot: fn() -> bool,
    /// `TEST_xcluster_failover_fail_delete_replication`.
    pub failover_fail_delete_replication: fn() -> bool,
    /// `xcluster_cleanup_tables_frequency_secs`: how often old DDL replication rows are deleted;
    /// zero or less disables it.
    pub cleanup_tables_frequency: fn() -> i32,
    /// `xcluster_ddl_tables_retention_secs`.
    pub ddl_tables_retention: fn() -> Duration,
    /// `xcluster_skip_health_check_on_replication_setup`.
    pub skip_health_check: fn() -> bool,
    /// `disable_xcluster_db_scoped_new_table_processing`: new tables are neither checkpointed on
    /// the source nor added to the inbound group on the target.
    pub disable_new_table_processing: fn() -> bool,
    /// `TEST_return_legacy_universe_replication_info`.
    pub return_legacy_universe_replication_info: fn() -> bool,
    /// `xcluster_safe_time_update_interval_secs`.
    pub safe_time_interval: fn() -> Duration,
    /// `xcluster_alter_universe_replication_setup_timeout_ms`.
    pub alter_setup_timeout: fn() -> Duration,
    /// `xcluster_skip_schema_compatibility_checks_on_alter`.
    pub skip_schema_compatibility_checks_on_alter: fn() -> bool,
    /// The `xcluster_store_older_schema_versions` AutoFlag.
    pub store_older_schema_versions: fn() -> bool,
    /// `xcluster_max_old_schema_versions`.
    pub max_old_schema_versions: fn() -> u32,
    /// The `enforce_xcluster_guarded_lease` AutoFlag.
    pub enforce_guarded_lease: fn() -> bool,
    /// The `persist_tserver_registry` AutoFlag.
    pub persist_tserver_registry: fn() -> bool,
    /// `max_clock_skew_usec`.
    pub max_clock_skew: fn() -> Duration,
    /// `wait_replication_drain_retry_timeout_ms`.
    pub drain_retry_timeout: fn() -> Duration,
}

impl Default for XClusterOptions {
    fn default() -> Self {
        XClusterOptions {
            api_v2: || true,
            ddl_replication: || true,
            force_automatic_ddl_mode: || false,
            max_streams_to_checkpoint: || 200,
            ysql_statement_timeout: || Duration::from_secs(300),
            wal_anchor_streams: || false,
            target_applied_filter: || false,
            fail_table_stream_checkpoint: || false,
            block_checkpoint_namespace_task: || false,
            fail_ensure_sequence_updates: || false,
            wal_retention: || Duration::from_secs(4 * 3600),
            skip_fields_moved_to_guarded_info: || false,
            rpc_timeout: || Duration::from_secs(30),
            bg_task_interval: || Duration::from_secs(1),
            auto_flag_validation: || false,
            ensure_sequence_updates_timeout: || Duration::from_secs(180),
            remote_timeout: || Duration::from_secs(30),
            check_bootstrap_required: || false,
            allow_ycql_transactional: || false,
            pause_at_start_of_setup: || Duration::ZERO,
            fail_universe_replication_merge: || false,
            exit_unfinished_merging: || false,
            exit_unfinished_deleting: || false,
            disable_universe_gc: || false,
            skip_health_check: || false,
            cleanup_tables_frequency: || 30 * 60,
            ddl_tables_retention: || Duration::from_secs(7 * 24 * 60 * 60),
            failover_task_timeout: || Duration::from_secs(600),
            failover_restoration_wait: || Duration::from_secs(120),
            failover_restoration_poll_interval: || Duration::from_millis(1000),
            failover_snapshot_delete_retry: || Duration::from_secs(30),
            failover_snapshot_retention_hours: || 1,
            pause_failover_before_delete: || false,
            failover_fail_restore_namespace: String::new,
            failover_fail_create_snapshot: || false,
            failover_fail_delete_replication: || false,
            disable_new_table_processing: || false,
            return_legacy_universe_replication_info: || false,
            safe_time_interval: || Duration::from_secs(1),
            alter_setup_timeout: || Duration::from_secs(600),
            skip_schema_compatibility_checks_on_alter: || false,
            store_older_schema_versions: || true,
            max_old_schema_versions: || 50,
            enforce_guarded_lease: || false,
            persist_tserver_registry: || true,
            max_clock_skew: || Duration::from_millis(500),
            drain_retry_timeout: || Duration::from_millis(2000),
        }
    }
}

/// The OID counters, which the leader's YSQL DDL owns.
pub trait XClusterHooks: Send + Sync {
    /// `AdvanceOidCounters`: the namespace's counters move above every OID it uses.
    fn advance_oid_counters(
        &self,
        namespace_id: Vec<u8>,
    ) -> master_task_runtime::BoxFuture<'static, Result<(), Status>>;

    /// One normal-space OID reserved now: above every one the namespace used.
    fn reserve_normal_oid(
        &self,
        namespace_id: Vec<u8>,
    ) -> master_task_runtime::BoxFuture<'static, Result<u32, Status>>;

    /// The namespace's next normal-space OID is above `oid` (`ReservePgsqlOids` from it).
    fn reserve_oids_above(
        &self,
        namespace_id: Vec<u8>,
        oid: u32,
    ) -> master_task_runtime::BoxFuture<'static, Result<(), Status>>;
}

/// xCluster on the leader.
pub struct XCluster {
    /// Itself, for the tasks its hooks start.
    pub(crate) me: std::sync::Weak<XCluster>,
    /// The post tablet creation task of each new table: its term and, once done, its outcome.
    pub(crate) post_create: Mutex<target_tables::PostCreateTasks>,
    pub(crate) catalog: Arc<Catalog>,
    pub(crate) writer: Arc<CatalogWriter>,
    pub(crate) leadership: Arc<Leadership>,
    pub(crate) ts_manager: Arc<TsManager>,
    pub(crate) xrepl: Arc<Xrepl>,
    pub(crate) tables: Arc<dyn CreatesTables>,
    pub(crate) rpc: OnceLock<Arc<dyn RpcClient>>,
    pub(crate) remote: Arc<dyn RemoteMasters>,
    pub(crate) options: XClusterOptions,
    /// What only the wiring can do.
    pub(crate) hooks: OnceLock<Arc<dyn XClusterHooks>>,
    /// The checkpoint task of each (group, namespace) and the term it runs in
    /// (`XClusterCheckpointNamespaceTask`).
    pub(crate) checkpoints: Mutex<HashMap<(String, String), (i64, tokio::task::AbortHandle)>>,
    /// The inbound groups being set up, until `IsSetupUniverseReplicationDone` reports them.
    pub(crate) setups: Mutex<HashMap<String, Arc<Mutex<inbound::SetupTask>>>>,
    /// What tservers reported of each producer tablet (`replication_error_map_`).
    pub(crate) statuses: Mutex<HashMap<inbound::StatusKey, inbound::TabletStatus>>,
    /// The safe time computation's state.
    pub(crate) safe_time: Mutex<safe_time::SafeTimeState>,
    /// Hidden split parents the target is done with, for the hidden-objects cleanup; a new
    /// leader finds them again from `cdc_state`.
    pub(crate) released_split_parents: Mutex<std::collections::HashSet<Vec<u8>>>,
    /// Tables a `HandleNewSchemaForAutomaticXClusterTargetTask` runs for.
    pub(crate) new_schema_tasks: Mutex<std::collections::HashSet<String>>,
    /// The failover running for each group and the term it runs in.
    pub(crate) failovers: Mutex<HashMap<String, i64>>,
    pub(crate) failover_snapshots: OnceLock<Arc<dyn failover::FailoverSnapshots>>,
    /// The left-behind groups this term still has to clear, and the term (`universes_to_clear_`).
    pub(crate) universes_to_clear: Mutex<(i64, std::collections::VecDeque<String>)>,
    /// Serializes `XClusterStopPersisting` and `XClusterStartPersisting`.
    pub(crate) persisting: tokio::sync::Mutex<()>,
}

impl XCluster {
    /// The service of this master.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        xrepl: Arc<Xrepl>,
        tables: Arc<dyn CreatesTables>,
        remote: Arc<dyn RemoteMasters>,
        options: XClusterOptions,
    ) -> Arc<XCluster> {
        Arc::new_cyclic(|me| XCluster {
            me: me.clone(),
            post_create: Mutex::default(),
            catalog,
            writer,
            leadership,
            ts_manager,
            xrepl,
            tables,
            rpc: OnceLock::new(),
            remote,
            options,
            hooks: OnceLock::new(),
            checkpoints: Mutex::default(),
            setups: Mutex::default(),
            statuses: Mutex::default(),
            safe_time: Mutex::default(),
            released_split_parents: Mutex::default(),
            persisting: tokio::sync::Mutex::new(()),
            new_schema_tasks: Mutex::default(),
            failovers: Mutex::default(),
            universes_to_clear: Mutex::new((-1, Default::default())),
            failover_snapshots: OnceLock::new(),
        })
    }

    /// Set once, before any leader term.
    pub fn set_hooks(&self, hooks: Arc<dyn XClusterHooks>) {
        let _ = self.hooks.set(hooks);
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    pub(crate) fn rpc(&self) -> Result<Arc<dyn RpcClient>, Status> {
        self.rpc
            .get()
            .cloned()
            .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No RPC client yet"))
    }

    /// One of the host's C++ steps, off the async threads.
    pub(crate) async fn host_call<Req: Message, Resp: protobuf::MessageFull>(
        &self,
        method: &'static str,
        req: Req,
    ) -> Result<Resp, Status> {
        self.xrepl.host_call(method, req).await
    }
}

/// An RPC error of the master with no more specific code, as C++ handlers return a status.
pub(crate) fn unknown(status: Status) -> MasterErrorPB {
    master_ts_manager::master_error_coded(Code::UNKNOWN_ERROR, status.code, &status.message)
}

pub(crate) fn status(code: StatusCode, message: impl Into<String>) -> Status {
    Status::new(code, message.into())
}

pub(crate) fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

pub(crate) fn status_of(e: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
        e.status.message().to_string(),
    )
}

/// `SCHECK_PB_FIELDS_NOT_EMPTY`: the named fields that are empty, in C++'s words.
pub(crate) fn required(fields: &[(&str, bool)]) -> Result<(), MasterErrorPB> {
    let empty: Vec<&str> = fields.iter().filter(|(_, e)| *e).map(|(n, _)| *n).collect();
    if empty.is_empty() {
        return Ok(());
    }
    Err(unknown(status(
        StatusCode::InvalidArgument,
        format!("Empty required arguments: [{}]", empty.join(", ")),
    )))
}

/// `StatusToPB`.
pub(crate) fn app_status(result: &Result<(), Status>) -> yb_proto::pb::wire_protocol::AppStatusPB {
    match result {
        Ok(()) => {
            let mut pb = yb_proto::pb::wire_protocol::AppStatusPB::new();
            pb.code = Some(protobuf::EnumOrUnknown::from_i32(0));
            pb
        }
        Err(s) => unknown(s.clone()).status.unwrap_or_default(),
    }
}

/// `TableInfo::is_running`: PREPARING, RUNNING or ALTERING, hidden or not. PREPARING counts, as
/// in C++: a table that automatic-mode DDL replication creates must take the source's later
/// schema changes (`InsertPackedSchemaForXClusterTarget`) before its post-create xCluster work,
/// which waits for them to replicate, lets it run.
pub(crate) fn is_running(table: &Entity) -> bool {
    table.table().is_some_and(|t| {
        t.state == sys_tables_entry_pb::State::PREPARING as i32
            || t.state == sys_tables_entry_pb::State::RUNNING as i32
            || t.state == sys_tables_entry_pb::State::ALTERING as i32
    })
}

/// `GetSequencesDataAliasForNamespace`: the id the sequences of one database are replicated as.
pub fn sequences_data_alias(namespace_id: &str) -> String {
    format!("{SEQUENCES_DATA_TABLE_ID}{SEQUENCES_DATA_ALIAS_MID}{namespace_id}")
}

/// Whether the table is `ddl_queue` of the DDL replication extension.
pub(crate) fn is_ddl_queue_table(pb: &SysTablesEntryPB) -> bool {
    pb.table_type() == TableType::PGSQL_TABLE_TYPE
        && pb.schema.DEPRECATED_pgschema_name() == DDL_QUEUE_PG_SCHEMA_NAME
        && pb.name() == DDL_QUEUE_TABLE_NAME.as_bytes()
}

/// Whether the table is `replicated_ddls` of the DDL replication extension.
pub(crate) fn is_replicated_ddls_table(pb: &SysTablesEntryPB) -> bool {
    pb.table_type() == TableType::PGSQL_TABLE_TYPE
        && pb.schema.DEPRECATED_pgschema_name() == DDL_QUEUE_PG_SCHEMA_NAME
        && pb.name() == DDL_REPLICATED_TABLE_NAME.as_bytes()
}

/// A table a replication group can have: its id (for `sequences_data`, the namespace's alias),
/// name and schema.
#[derive(Clone, Debug)]
pub(crate) struct TableDesignator {
    pub id: String,
    pub name: String,
    pub pgschema_name: String,
}

/// `IsTableEligibleForXClusterReplication`: a visible user YSQL table that is not a colocated
/// table (only the parent is replicated), the sequences table, `replicated_ddls`, or (outside
/// automatic mode) a materialized view.
pub(crate) fn is_eligible(
    snapshot: &CatalogSnapshot,
    table: &Entity,
    pb: &SysTablesEntryPB,
    automatic_ddl_mode: bool,
) -> bool {
    if !master_table_lifecycle::lookup::is_visible_to_client(table) {
        return false;
    }
    if pb.table_type() != TableType::PGSQL_TABLE_TYPE
        || master_table_lifecycle::lookup::is_system_table(snapshot, table)
    {
        return false;
    }
    if master_table_lifecycle::lookup::is_colocation_parent_table_id(&table.key.id) {
        return true;
    }
    if pb.is_matview() && !automatic_ddl_mode {
        return false;
    }
    if pb.colocated() {
        return false;
    }
    if &*table.key.id == SEQUENCES_DATA_TABLE_ID.as_bytes() {
        return false;
    }
    !is_replicated_ddls_table(pb)
}

/// `GetTablesEligibleForXClusterReplication`: in automatic mode also the namespace's alias of
/// `sequences_data`, once that table exists.
pub(crate) fn eligible_tables(
    snapshot: &CatalogSnapshot,
    namespace_id: &str,
    automatic_ddl_mode: bool,
) -> Result<Vec<TableDesignator>, Status> {
    let mut out = Vec::new();
    for table in snapshot.iter(SysRowEntryType::TABLE) {
        if table
            .table()
            .is_none_or(|t| t.namespace_id != namespace_id.as_bytes())
        {
            continue;
        }
        let pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
        if is_eligible(snapshot, table, &pb, automatic_ddl_mode) {
            out.push(designator(&table.key.id, &pb));
        }
    }
    if automatic_ddl_mode {
        if let Some(seq) = snapshot.get(SysRowEntryType::TABLE, SEQUENCES_DATA_TABLE_ID.as_bytes())
        {
            let pb: SysTablesEntryPB = seq.parse().map_err(corrupt)?;
            if pb.state() != sys_tables_entry_pb::State::UNKNOWN {
                let mut d = designator(&seq.key.id, &pb);
                d.id = sequences_data_alias(namespace_id);
                out.push(d);
            }
        }
    }
    out.sort_by(|a, b| a.id.cmp(&b.id));
    Ok(out)
}

fn designator(id: &[u8], pb: &SysTablesEntryPB) -> TableDesignator {
    TableDesignator {
        id: String::from_utf8_lossy(id).into_owned(),
        name: String::from_utf8_lossy(pb.name()).into_owned(),
        pgschema_name: pb.schema.DEPRECATED_pgschema_name().to_string(),
    }
}
