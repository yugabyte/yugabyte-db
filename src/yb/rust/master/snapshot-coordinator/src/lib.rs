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

//! The leader half of `MasterSnapshotCoordinator` (K5): snapshots, snapshot schedules, restores
//! and PITR.
//!
//! Every master, C++ or Rust, runs the C++ coordinator's apply half: `CREATE_ON_MASTER`,
//! `DELETE_ON_MASTER` and `RESTORE_SYS_CATALOG` operations and the `SNAPSHOT`,
//! `SNAPSHOT_SCHEDULE` and `SNAPSHOT_RESTORATION` rows. On a Rust master that coordinator never
//! acts as leader. This crate does what its leader does, from the same rows: it submits those
//! operations through the host, sends `TabletSnapshotOp` to tablet leaders, and writes the rows'
//! later versions through the catalog writer. What is in flight is per term and derived from the
//! rows when a term starts: a tablet still in its object's initial state is sent again.

mod clone;
mod collect;
mod controller;
mod hidden;
pub mod ids;
mod import;
mod restore;
mod schedules;
mod snapshots;
pub mod state;
mod tablet_ops;
#[cfg(test)]
mod tests;

pub use clone::CloneOptions;
pub use collect::{CollectFlags, SEQUENCES_DATA_TABLE_ID};
pub use controller::SnapshotController;
pub use import::ImportOptions;

use std::collections::{HashMap, HashSet};
use std::sync::{Arc, Mutex, MutexGuard, OnceLock};
use std::time::Duration;

use master_catalog_state::{Catalog, CatalogWriter};
use master_leadership::Leadership;
use master_task_runtime::{BoxFuture, RetryPolicy, TsRpcExecutor};
use master_ts_manager::{master_error, master_error_coded, master_error_tagged, TsManager};
use tokio::sync::watch;
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::backup::TabletSnapshotOpRequestPB;
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use state::Outcome;

/// Rows as (id, data).
pub type Rows = Vec<(Vec<u8>, Vec<u8>)>;

/// What the coordinator needs from the C++ host.
pub trait SnapshotHost: Send + Sync + 'static {
    /// Submits a snapshot operation (`CREATE_ON_MASTER`, `DELETE_ON_MASTER`,
    /// `RESTORE_SYS_CATALOG`) to the sys catalog tablet in `term`; resolves once it is applied.
    fn submit(
        &self,
        term: i64,
        request: TabletSnapshotOpRequestPB,
    ) -> BoxFuture<'static, Result<(), Status>>;
    /// `Clock::MaxGlobalNow`, the time snapshots are taken at.
    fn max_global_now(&self) -> Result<u64, Status>;
    /// `Clock::Now`: what tablet RPCs propagate and what expiry, deletion and completion times
    /// are measured with. Propagating `max_global_now` would move each tserver's clock ahead by
    /// the maximum skew on every RPC, until tservers crash on clock skew.
    fn now(&self) -> Result<u64, Status>;
    /// The `entry_type` rows of the sys catalog as of `read_time`, once the tablet's safe time
    /// passes it. Blocks.
    fn scan_at(
        &self,
        entry_type: SysRowEntryType,
        read_time: u64,
        timeout: Duration,
    ) -> Result<Rows, Status>;
    /// One of the host's C++ steps (`HandleOnHost`). Blocks.
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status>;
}

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct SnapshotOptions {
    /// `snapshot_coordinator_poll_interval_ms`.
    pub poll_interval: fn() -> Duration,
    /// `snapshot_coordinator_cleanup_delay_ms`.
    pub cleanup_delay: fn() -> Duration,
    /// `snapshot_coordinator_missing_creating_snapshot_grace_sec`.
    pub missing_creating_snapshot_grace: fn() -> Duration,
    /// `max_concurrent_snapshot_rpcs`.
    pub max_concurrent_snapshot_rpcs: fn() -> i64,
    /// `max_concurrent_snapshot_rpcs_per_tserver`.
    pub max_concurrent_snapshot_rpcs_per_tserver: fn() -> i64,
    /// `max_concurrent_restoration_rpcs`.
    pub max_concurrent_restoration_rpcs: fn() -> i64,
    /// `max_concurrent_restoration_rpcs_per_tserver`.
    pub max_concurrent_restoration_rpcs_per_tserver: fn() -> i64,
    /// `default_snapshot_retention_hours`.
    pub default_retention_hours: fn() -> i32,
    /// `enable_namespace_snapshot_workflow`.
    pub namespace_snapshot_workflow: fn() -> bool,
    /// The `enable_object_retention_due_to_snapshots` AutoFlag.
    pub object_retention_due_to_snapshots: fn() -> bool,
    /// `enable_export_snapshot_using_relfilenode`.
    pub export_snapshot_using_relfilenode: fn() -> bool,
    /// `TEST_treat_hours_as_milliseconds_for_snapshot_expiry`.
    pub treat_hours_as_milliseconds: fn() -> bool,
    /// `TEST_mark_snapshot_as_failed`.
    pub mark_snapshot_as_failed: fn() -> bool,
    /// `TEST_skip_sending_restore_finished`.
    pub skip_sending_restore_finished: fn() -> bool,
    /// `TEST_skip_sys_catalog_tablet_restore`.
    pub skip_sys_catalog_tablet_restore: fn() -> bool,
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
    /// `ImportSnapshotMeta`'s flags.
    pub import: ImportOptions,
    /// `CloneNamespace`'s flags.
    pub clone: CloneOptions,
}

impl Default for SnapshotOptions {
    fn default() -> Self {
        SnapshotOptions {
            poll_interval: || Duration::from_secs(5),
            cleanup_delay: || Duration::from_secs(30),
            missing_creating_snapshot_grace: || Duration::from_secs(300),
            max_concurrent_snapshot_rpcs: || -1,
            max_concurrent_snapshot_rpcs_per_tserver: || 1,
            max_concurrent_restoration_rpcs: || -1,
            max_concurrent_restoration_rpcs_per_tserver: || 1,
            default_retention_hours: || 24,
            namespace_snapshot_workflow: || true,
            object_retention_due_to_snapshots: || true,
            export_snapshot_using_relfilenode: || false,
            treat_hours_as_milliseconds: || false,
            mark_snapshot_as_failed: || false,
            skip_sending_restore_finished: || false,
            skip_sys_catalog_tablet_restore: || false,
            rpc_timeout: || Duration::from_secs(15),
            import: ImportOptions::default(),
            clone: CloneOptions::default(),
        }
    }
}

/// What the leader holds for one term: nothing here outlives it.
#[derive(Default)]
pub(crate) struct TermState {
    pub(crate) term: i64,
    /// Tablet RPCs in flight, by snapshot or restoration id.
    pub(crate) running: HashMap<Vec<u8>, HashSet<Vec<u8>>>,
    /// Tablet outcomes not yet written, by snapshot or restoration id.
    pub(crate) outcomes: HashMap<Vec<u8>, HashMap<Vec<u8>, Outcome>>,
    /// The snapshot each schedule is creating, and since when (`creating_snapshot_data_`).
    pub(crate) creating: HashMap<Vec<u8>, (Vec<u8>, Instant)>,
    /// Snapshots whose `DELETE_ON_MASTER` was submitted this term (`delete_started_`).
    pub(crate) deleting: HashSet<Vec<u8>>,
    /// When each object was first seen with every tablet done, for the cleanup delay.
    /// Keyed by the object's initial state too: a snapshot deleted after it completed waits
    /// the cleanup delay from when its deletion was done.
    pub(crate) done_at: HashMap<(Vec<u8>, i32), Instant>,
    /// Objects whose row is being removed or updated.
    pub(crate) writing: HashSet<Vec<u8>>,
    /// Restorations whose `RESTORE_FINISHED` was sent.
    pub(crate) finished: HashSet<Vec<u8>>,
    /// When each PITR restoration was first seen waiting for tablet reports.
    pub(crate) reports_wait: HashMap<Vec<u8>, Instant>,
    /// Clones started this term.
    pub(crate) clones: HashMap<Vec<u8>, clone::CloneRun>,
}

/// The leader half of the snapshot coordinator.
pub struct SnapshotCoordinator {
    pub(crate) catalog: Arc<Catalog>,
    pub(crate) writer: Arc<CatalogWriter>,
    pub(crate) leadership: Arc<Leadership>,
    pub(crate) ts_manager: Arc<TsManager>,
    pub(crate) host: Arc<dyn SnapshotHost>,
    pub(crate) rpc: OnceLock<Arc<dyn RpcClient>>,
    pub(crate) executor: Arc<TsRpcExecutor>,
    pub(crate) retry: RetryPolicy,
    pub(crate) options: SnapshotOptions,
    pub(crate) term: Mutex<TermState>,
    /// What [`Self::state`] gives a task of an earlier term.
    pub(crate) stale: Mutex<TermState>,
    /// Namespaces a snapshot is being created of, with those snapshots (`retained_namespaces_`):
    /// their tables are hidden rather than deleted until the snapshots end.
    pub(crate) retained_namespaces: Mutex<HashMap<Vec<u8>, HashSet<Vec<u8>>>>,
    /// Wakes the controller before its interval, as C++ polls again out of band.
    pub(crate) poke: watch::Sender<u64>,
    /// Hooks into other leader components.
    pub(crate) hooks: OnceLock<Arc<dyn RestoreHooks>>,
    /// CDC streams, which keep some hidden split parents; set once.
    pub(crate) xrepl_hooks: OnceLock<Arc<dyn master_table_lifecycle::XreplHooks>>,
}

/// What a finished PITR tells other leader components.
pub trait RestoreHooks: Send + Sync + 'static {
    /// Index backfills of restored tables resume (`AddPendingBackFill`).
    fn resume_backfill(&self, table_id: &[u8]);
    /// Splitting, disabled for the restore, is allowed again (`ReenableSplittingFor`).
    fn reenable_splitting(&self, feature: &str);
    /// Splitting is disabled while a PITR runs (`PrepareForSnapshotRestore`).
    fn disable_splitting(&self, duration: Duration, feature: &str);
    /// Advances a YSQL database's OID counters past every OID in use (`AdvanceOidCounters`).
    fn advance_oid_counters(&self, namespace_id: Vec<u8>)
        -> BoxFuture<'static, Result<(), Status>>;
    /// Whether the namespace is the target of automatic-mode xCluster replication.
    fn is_automatic_xcluster_target(&self, namespace_id: &[u8]) -> bool;
}

impl SnapshotCoordinator {
    /// The coordinator of this master.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        host: Arc<dyn SnapshotHost>,
        executor: Arc<TsRpcExecutor>,
        retry: RetryPolicy,
        options: SnapshotOptions,
    ) -> Arc<SnapshotCoordinator> {
        Arc::new(SnapshotCoordinator {
            catalog,
            writer,
            leadership,
            ts_manager,
            host,
            rpc: OnceLock::new(),
            executor,
            retry,
            options,
            term: Mutex::default(),
            stale: Mutex::default(),
            retained_namespaces: Mutex::default(),
            poke: watch::channel(0).0,
            hooks: OnceLock::new(),
            xrepl_hooks: OnceLock::new(),
        })
    }

    /// CDC streams, which keep some hidden split parents (`ShouldRetainHiddenTablet`); set once.
    pub fn set_xrepl_hooks(&self, hooks: Arc<dyn master_table_lifecycle::XreplHooks>) {
        let _ = self.xrepl_hooks.set(hooks);
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    /// The other leader components a restore informs; set once.
    pub fn set_hooks(&self, hooks: Arc<dyn RestoreHooks>) {
        let _ = self.hooks.set(hooks);
    }

    pub(crate) fn rpc(&self) -> Result<Arc<dyn RpcClient>, Status> {
        self.rpc
            .get()
            .cloned()
            .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No RPC client yet"))
    }

    /// This term's state, cleared when a later term starts. A task of an earlier term gets a
    /// scratch state, so it cannot change the current term's.
    pub(crate) fn state(&self, term: i64) -> MutexGuard<'_, TermState> {
        let mut state = self.term.lock().unwrap();
        if term < state.term {
            drop(state);
            let mut stale = self.stale.lock().unwrap();
            *stale = TermState {
                term,
                ..Default::default()
            };
            return stale;
        }
        if state.term != term {
            *state = TermState {
                term,
                ..Default::default()
            };
        }
        state
    }

    /// Runs the controller soon.
    pub(crate) fn wake(&self) {
        self.poke.send_modify(|n| *n += 1);
    }

    /// The controller's trigger besides catalog changes.
    pub fn subscribe(&self) -> watch::Receiver<u64> {
        self.poke.subscribe()
    }

    /// `IsNamespaceRetained`: a snapshot of the namespace is being created, so its tables must be
    /// hidden rather than deleted.
    pub fn is_namespace_retained(&self, namespace_id: &[u8]) -> bool {
        self.retained_namespaces
            .lock()
            .unwrap()
            .get(namespace_id)
            .is_some_and(|s| !s.is_empty())
    }

    /// Waits until nothing else writes the object's row this term, and holds it: a snapshot
    /// operation submitted outside the catalog writer (`DELETE_ON_MASTER`) must not race a
    /// write of the same row prepared from the version before it.
    pub(crate) async fn lock_object(&self, term: i64, id: &[u8]) -> ObjectGuard<'_> {
        loop {
            if self.state(term).writing.insert(id.to_vec()) {
                return ObjectGuard {
                    coordinator: self,
                    term,
                    id: id.to_vec(),
                };
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }

    /// `GetRpcLimit`: how many tablet RPCs one object may have in flight.
    pub(crate) fn rpc_limit(&self, total: i64, per_tserver: i64) -> usize {
        if total == 0 {
            return i32::MAX as usize;
        }
        if total > 0 {
            return total as usize;
        }
        let live = self.ts_manager.live().len().max(1);
        (live as i64 * per_tserver).max(1) as usize
    }
}

/// Holds an object's row against other writes ([`SnapshotCoordinator::lock_object`]).
pub(crate) struct ObjectGuard<'a> {
    coordinator: &'a SnapshotCoordinator,
    term: i64,
    id: Vec<u8>,
}

impl Drop for ObjectGuard<'_> {
    fn drop(&mut self) {
        self.coordinator.state(self.term).writing.remove(&self.id);
    }
}

pub(crate) fn error(code: Code, status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    master_error(code, status, &message.into())
}

/// `STATUS(Kind, msg, msg2, MasterError(code))`: the code encoded in the status.
pub(crate) fn coded(code: Code, status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    master_error_coded(code, status, &message.into())
}

/// `STATUS_EC_FORMAT(Kind, MasterError(code), ...)`: "<message>: <CODE>", the code encoded.
pub(crate) fn tagged(code: Code, status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    master_error_tagged(code, status, &message.into())
}

/// A status as the UNKNOWN_ERROR the handlers' `SetupError` makes of a status without a code.
pub(crate) fn unknown(status: &Status) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, status.code, status.message.clone())
}

pub(crate) fn internal(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::InternalError, e.to_string())
}

pub(crate) fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

/// The `error` of a host response, which `HandleRequest` sets from the C++ step's status.
pub(crate) fn host_error(resp: &dyn protobuf::MessageDyn) -> Option<MasterErrorPB> {
    let field = resp.descriptor_dyn().field_by_name("error")?;
    let value = field.get_singular(resp)?;
    let message = value.to_message()?;
    let message: &dyn protobuf::MessageDyn = &*message;
    message.downcast_ref::<MasterErrorPB>().cloned()
}

pub(crate) fn status_of(error: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(protobuf::Enum::value(&error.status.code())),
        error.status.message().to_string(),
    )
}
