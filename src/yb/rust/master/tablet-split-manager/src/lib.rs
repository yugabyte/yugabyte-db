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

//! Tablet splits on the leader: `CatalogManager::SplitTablet`, `DoSplitTablet` and
//! `DeleteNotServingTablet`, with the candidate checks of `TabletSplitManager`.
//!
//! A split is one catalog mutation (K1): the children as CREATING tablets with the parent's
//! committed config, the parent's `split_tablet_ids`, and the table's next partition list version.
//! The `tablet-splits` controller then sends `SplitTablet` to the parent's leader for every parent
//! whose children are not all running, so a new leader finishes a split the last one registered,
//! as C++'s `DoSplitting` restarts them. The tserver creates the children; once they serve, its
//! `DeleteNotServingTablet` retires the parent.

mod auto;
#[cfg(test)]
mod tests;

pub use auto::AutoSplitOptions;

use std::collections::{BTreeSet, HashMap, HashSet};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::{
    is_inactive_state, Catalog, CatalogSnapshot, CatalogWriter, Mutation, WriteOptions,
};
use master_leadership::{LeaderToken, Leadership};
use master_table_lifecycle::lookup::{is_hidden, is_system_table};
use master_table_lifecycle::{
    covering_schedules, generate_object_id, local_time_string, SchemaHost,
};
use master_task_runtime::{Attempt, BoxFuture, Controller, RetryPolicy, TermScope, TsRpcExecutor};
use master_ts_manager::placement::{blacklist_addresses, is_blacklisted};
use master_ts_manager::{master_error, DeleteReplica, TsManager};
use protobuf::{Enum, EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysCDCStreamEntryPB,
    SysClusterConfigEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common::PartitionPB;
use yb_proto::pb::common_types::{IndexPermissions, TableType};
use yb_proto::pb::master_admin::{DeleteNotServingTabletRequestPB, SplitTabletRequestPB};
use yb_proto::pb::master_rust_peer::{
    EncodedPartitionKeysRequestPB, EncodedPartitionKeysResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::operations::SplitTabletRequestPB as TsSplitTabletRequestPB;
use yb_proto::pb::tablet_types::TabletDataState;
use yb_proto::pb::tserver_admin::SplitTabletResponsePB as TsSplitTabletResponsePB;
use yb_proto::pb::tserver_service::{GetSplitKeyRequestPB, GetSplitKeyResponsePB};
use yb_proto::pb::tserver_types::tablet_server_error_pb::Code as TsCode;

use sys_tablets_entry_pb::State as TabletState;

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct SplitOptions {
    /// The `use_parent_table_id_field` AutoFlag: children name their table by parent id.
    pub use_parent_table_id_field: fn() -> bool,
    /// `TEST_enable_multi_way_tablet_split`.
    pub enable_multi_way_split: fn() -> bool,
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
    /// `catalog_manager_bg_task_wait_ms`.
    pub interval: fn() -> Duration,
    /// Automatic splitting.
    pub auto: AutoSplitOptions,
}

impl Default for SplitOptions {
    fn default() -> SplitOptions {
        SplitOptions {
            use_parent_table_id_field: || true,
            enable_multi_way_split: || false,
            rpc_timeout: || Duration::from_secs(15),
            interval: || Duration::from_secs(1),
            auto: AutoSplitOptions::default(),
        }
    }
}

/// Splits on the leader.
pub struct TabletSplitManager {
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    leadership: Arc<Leadership>,
    ts_manager: Arc<TsManager>,
    host: Arc<dyn SchemaHost>,
    /// Set once the master's RPC client exists ([`TabletSplitManager::set_rpc`]).
    rpc: OnceLock<Arc<dyn RpcClient>>,
    executor: Arc<TsRpcExecutor>,
    retry: RetryPolicy,
    options: SplitOptions,
    /// Splits being started (`AsyncGetTabletSplitKey`, then the registration): by parent.
    starting: Arc<Mutex<HashSet<Vec<u8>>>>,
    auto: Mutex<AutoState>,
}

/// Automatic splitting's state, kept across terms.
#[derive(Default)]
struct AutoState {
    disabled: auto::Disabled,
    last_run: Option<Instant>,
}

/// Removes a split from [`TabletSplitManager::starting`] when it is done or abandoned.
struct Starting {
    set: Arc<Mutex<HashSet<Vec<u8>>>>,
    tablet_id: Vec<u8>,
}

impl Starting {
    /// None if the split is already being started.
    fn begin(set: &Arc<Mutex<HashSet<Vec<u8>>>>, tablet_id: &[u8]) -> Option<Starting> {
        set.lock()
            .unwrap()
            .insert(tablet_id.to_vec())
            .then(|| Starting {
                set: set.clone(),
                tablet_id: tablet_id.to_vec(),
            })
    }
}

impl Drop for Starting {
    fn drop(&mut self) {
        self.set.lock().unwrap().remove(&self.tablet_id);
    }
}

fn error(code: Code, status: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    master_error(code, status, &message.into())
}

fn internal(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::InternalError, e.to_string())
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

impl TabletSplitManager {
    /// The split manager of this master.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        host: Arc<dyn SchemaHost>,
        executor: Arc<TsRpcExecutor>,
        retry: RetryPolicy,
        options: SplitOptions,
    ) -> Arc<TabletSplitManager> {
        Arc::new(TabletSplitManager {
            catalog,
            writer,
            leadership,
            ts_manager,
            host,
            rpc: OnceLock::new(),
            executor,
            retry,
            options,
            starting: Arc::default(),
            auto: Mutex::default(),
        })
    }

    /// `MasterAdmin.SplitTablet`: a manual split. The tablet is checked now; its leader is then
    /// asked for the split key, and the split is registered, in the background, as C++ schedules
    /// `AsyncGetTabletSplitKey`.
    pub fn split_tablet(
        self: &Arc<Self>,
        token: LeaderToken,
        req: &SplitTabletRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let tablet_id = req.tablet_id().to_vec();
        tracing::info!(
            "Got tablet to split: {}, is manual split: true",
            String::from_utf8_lossy(&tablet_id)
        );
        let tablet = snapshot
            .get(SysRowEntryType::TABLET, &tablet_id)
            .ok_or_else(|| {
                error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::NotFound,
                    format!("Tablet {} not found", String::from_utf8_lossy(&tablet_id)),
                )
            })?;
        validate_split_candidate(&snapshot, tablet, true)?;
        self.check_limits(&snapshot, &tablet.tablet().expect("tablet row").table_id)?;
        let split_factor = if req.has_split_factor() {
            req.split_factor()
        } else {
            2
        };
        if split_factor < 2 {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "Split factor must be at least 2",
            ));
        }
        if split_factor != 2 && !(self.options.enable_multi_way_split)() {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                format!(
                    "Split factor must be 2 when --TEST_enable_multi_way_tablet_split is \
                     disabled, requested: {split_factor}"
                ),
            ));
        }
        let Some(starting) = Starting::begin(&self.starting, &tablet_id) else {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::AlreadyPresent,
                format!(
                    "A split of tablet {} is already being started",
                    String::from_utf8_lossy(&tablet_id)
                ),
            ));
        };
        let this = self.clone();
        tokio::spawn(async move {
            this.start_split(token, starting, split_factor, true).await;
        });
        Ok(())
    }

    /// `AsyncGetTabletSplitKey`, then `SplitTabletWithKeys`.
    async fn start_split(
        &self,
        token: LeaderToken,
        starting: Starting,
        split_factor: i32,
        manual: bool,
    ) {
        let tablet_id = &starting.tablet_id;
        match self.split_key(token, tablet_id, split_factor, manual).await {
            Ok(keys) => {
                if let Err(status) = self.do_split(token, tablet_id, keys, manual).await {
                    tracing::warn!(
                        "Failed to split tablet {}: {status}",
                        String::from_utf8_lossy(tablet_id)
                    );
                }
            }
            Err(status) => tracing::warn!(
                "GetSplitKey of tablet {} failed: {status}",
                String::from_utf8_lossy(tablet_id)
            ),
        }
    }

    /// The table limits a manual split is held to as well: `CanAddPartitionsToTable` and
    /// `tablet_split_limit_per_table`.
    fn check_limits(
        &self,
        snapshot: &CatalogSnapshot,
        table_id: &[u8],
    ) -> Result<(), MasterErrorPB> {
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Ok(());
        };
        let table_pb: SysTablesEntryPB = table
            .parse()
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        auto::check_partition_limits(
            snapshot,
            table,
            &table_pb,
            self.ts_manager.live().len(),
            &self.options.auto,
        )
        .map_err(|m| error(Code::REACHED_SPLIT_LIMIT, StatusCode::IllegalState, m))
    }

    /// The tservers automatic splitting looks at, now.
    fn cluster(&self, snapshot: &CatalogSnapshot) -> auto::Cluster {
        let config: SysClusterConfigEntryPB = snapshot
            .cluster_config()
            .and_then(|c| c.parse().ok())
            .unwrap_or_default();
        let blacklist = blacklist_addresses(&config);
        let live = self.ts_manager.live();
        auto::Cluster {
            num_live: live.len(),
            valid: live
                .iter()
                .filter(|d| !is_blacklisted(d, &blacklist))
                .map(|d| d.pb.registration.cloud_info.clone().unwrap_or_default())
                .collect(),
            ttl_split_disabled: live
                .iter()
                .filter(|d| {
                    d.metrics
                        .as_ref()
                        .is_some_and(|m| m.disable_tablet_split_if_default_ttl())
                })
                .map(|d| d.uuid.to_vec())
                .collect(),
            config,
        }
    }

    /// `MaybeDoSplitting`: one automatic splitting pass, starting the splits it picks.
    fn auto_split(self: &Arc<Self>, token: LeaderToken, scope: &TermScope) {
        let options = &self.options.auto;
        if !(options.enabled)() {
            return;
        }
        let now = Instant::now();
        let plan = {
            let mut state = self.auto.lock().unwrap();
            if let Some((feature, until)) = state.disabled.feature(now) {
                tracing::trace!(
                    "Skipping splitting run because automatic tablet splitting is disabled for                      another {:?} by feature {feature}",
                    until - now
                );
                return;
            }
            if state
                .last_run
                .is_some_and(|last| now.duration_since(last) < (options.interval)())
            {
                return;
            }
            let snapshot = self.catalog.snapshot();
            let cluster = self.cluster(&snapshot);
            let starting = self.starting.lock().unwrap().clone();
            let locations = &self.ts_manager.locations;
            let plan = auto::plan(
                &snapshot,
                &cluster,
                &|id| locations.get(id),
                &starting,
                &mut state.disabled,
                options,
                now,
            );
            state.last_run = Some(Instant::now());
            if plan.candidates + plan.outstanding > 0 {
                tracing::debug!(
                    "Automatic splitting: {} candidate(s), {} outstanding split(s), {} new",
                    plan.candidates,
                    plan.outstanding,
                    plan.schedule.len()
                );
            }
            // Marked as starting before the lock is released, so `IsTabletSplittingComplete` sees
            // them.
            plan.schedule
                .into_iter()
                .filter_map(|(id, size)| Starting::begin(&self.starting, &id).map(|s| (s, size)))
                .collect::<Vec<_>>()
        };
        for (starting, size) in plan {
            tracing::info!(
                "Scheduled split for tablet_id: {} with size {size} bytes",
                String::from_utf8_lossy(&starting.tablet_id)
            );
            let this = self.clone();
            scope.entity().spawn("auto-split", async move {
                this.start_split(token, starting, 2, false).await;
            });
        }
    }

    /// `DisableTabletSplitting`: no automatic splits for a while, for a feature (a restore).
    pub fn disable_splitting_for(&self, duration: Duration, feature: &str) {
        tracing::info!(
            "Disabling tablet splitting for {} milliseconds for feature {feature}.",
            duration.as_millis()
        );
        self.auto
            .lock()
            .unwrap()
            .disabled
            .features
            .insert(feature.to_string(), Instant::now() + duration);
    }

    /// `ReenableSplittingFor`.
    pub fn reenable_splitting_for(&self, feature: &str) {
        self.auto.lock().unwrap().disabled.features.remove(feature);
    }

    /// `IsTabletSplittingComplete`: no split being started, no split child not yet running, and,
    /// if asked, no split parent left.
    pub fn is_tablet_splitting_complete(&self, wait_for_parent_deletion: bool) -> bool {
        // Held while a pass picks splits, so a pass that began before splitting was disabled
        // has scheduled its splits by now (`WaitUntilIdle`).
        let _pass = self.auto.lock().unwrap();
        if !self.starting.lock().unwrap().is_empty() {
            return false;
        }
        let snapshot = self.catalog.snapshot();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else { continue };
            use sys_tables_entry_pb::State as TableState;
            if hot.state == TableState::DELETED as i32 {
                continue;
            }
            let tablets = snapshot.tablets_of(&table.key.id);
            let children: HashSet<&[u8]> = tablets
                .iter()
                .filter_map(|t| t.tablet())
                .flat_map(|t| t.split_tablet_ids.iter().map(|c| c.as_slice()))
                .collect();
            let active: HashSet<&[u8]> = snapshot
                .active_tablets_of(&table.key.id)
                .into_iter()
                .map(|t| &*t.key.id)
                .collect();
            for tablet in &tablets {
                let Some(t) = tablet.tablet() else { continue };
                let id: &[u8] = &tablet.key.id;
                if active.contains(id) {
                    if children.contains(id) && t.state != TabletState::RUNNING as i32 {
                        return false;
                    }
                } else if wait_for_parent_deletion && !is_inactive_state(t.state) && !t.hidden {
                    return false;
                }
            }
        }
        true
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    fn rpc(&self) -> Result<Arc<dyn RpcClient>, Status> {
        self.rpc
            .get()
            .cloned()
            .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No RPC client yet"))
    }

    fn leader_of(&self, tablet_id: &[u8]) -> Option<Vec<u8>> {
        self.ts_manager
            .locations
            .get(tablet_id)
            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
    }

    /// `AsyncGetTabletSplitKey`: the partition keys the leader splits at.
    async fn split_key(
        &self,
        token: LeaderToken,
        tablet_id: &[u8],
        split_factor: i32,
        manual: bool,
    ) -> Result<Vec<Vec<u8>>, Status> {
        let leader = self.leader_of(tablet_id).ok_or_else(|| {
            Status::new(StatusCode::ServiceUnavailable, "No leader for the tablet")
        })?;
        let mut req = GetSplitKeyRequestPB::new();
        req.set_tablet_id(tablet_id.to_vec());
        req.set_is_manual_split(manual);
        req.set_split_factor(split_factor);
        let rpc_timeout = (self.options.rpc_timeout)();
        let rpc = self.rpc()?;
        let resp: GetSplitKeyResponsePB = self
            .executor
            .run(&leader, "GetSplitKey", &self.retry, |_| {
                let current = self.leadership.check(&token);
                let address = self.ts_manager.rpc_address(&leader);
                let (rpc, req) = (rpc.clone(), req.clone());
                async move {
                    current.map_err(Attempt::Fail)?;
                    let (host, port) = address.ok_or_else(|| {
                        Attempt::Retry(Status::new(
                            StatusCode::NotFound,
                            "tserver is not registered",
                        ))
                    })?;
                    let bytes = req
                        .write_to_bytes()
                        .map_err(|e| Attempt::Fail(internal(e)))?;
                    let resp = rpc
                        .call(
                            &host,
                            port,
                            "yb.tserver.TabletServerService",
                            "GetSplitKey",
                            &bytes,
                            rpc_timeout,
                        )
                        .await
                        .map_err(Attempt::Retry)?;
                    let resp = GetSplitKeyResponsePB::parse_from_bytes(&resp)
                        .map_err(|e| Attempt::Retry(corrupt(e)))?;
                    let Some(e) = resp.error.as_ref() else {
                        return Ok(resp);
                    };
                    let status = Status::new(
                        StatusCode::from_i32(e.status.code().value()),
                        e.status.message().to_string(),
                    );
                    if matches!(
                        e.code(),
                        TsCode::TABLET_SPLIT_DISABLED_TTL_EXPIRY
                            | TsCode::TABLET_SPLIT_KEY_RANGE_TOO_SMALL
                    ) {
                        self.hold_off(tablet_id, e.code());
                        return Err(Attempt::Fail(status));
                    }
                    // An IllegalState other than NOT_THE_LEADER (not compacted yet after an
                    // earlier split, or all data under one key) is not retried.
                    if status.code == StatusCode::IllegalState && e.code() != TsCode::NOT_THE_LEADER
                    {
                        return Err(Attempt::Fail(status));
                    }
                    Err(Attempt::Retry(status))
                }
            })
            .await?;
        if !resp.split_partition_keys.is_empty() {
            Ok(resp.split_partition_keys)
        } else {
            Ok(vec![resp.DEPRECATED_split_partition_key().to_vec()])
        }
    }

    /// After `GetSplitKey` refused: a table whose files expire by TTL is not split again for
    /// `prevent_split_for_ttl_tables_for_seconds`, a tablet too small a key range to split for
    /// `prevent_split_for_small_key_range_tablets_for_seconds`.
    fn hold_off(&self, tablet_id: &[u8], code: TsCode) {
        let now = Instant::now();
        let mut state = self.auto.lock().unwrap();
        if code == TsCode::TABLET_SPLIT_DISABLED_TTL_EXPIRY {
            tracing::info!(
                "Tablet split not supported for tablets with TTL file expiration: {}",
                String::from_utf8_lossy(tablet_id)
            );
            let recheck = (self.options.auto.prevent_split_for_ttl_tables)();
            let table = self
                .catalog
                .snapshot()
                .get(SysRowEntryType::TABLET, tablet_id)
                .and_then(|t| t.tablet().map(|t| t.table_id.clone()));
            if let (false, Some(table)) = (recheck.is_zero(), table) {
                state.disabled.ttl_tables.insert(table, now + recheck);
            }
        } else {
            tracing::info!(
                "Tablet key range is too small to split, disabling splitting temporarily."
            );
            let recheck = (self.options.auto.prevent_split_for_small_key_range_tablets)();
            if !recheck.is_zero() {
                state
                    .disabled
                    .small_key_range_tablets
                    .insert(tablet_id.to_vec(), now + recheck);
            }
        }
    }

    /// `DoSplitTablet`: registers the children in one write, unless they already are (a retried
    /// split). The `tablet-splits` controller sends the split to the tserver.
    pub async fn do_split(
        &self,
        token: LeaderToken,
        tablet_id: &[u8],
        split_partition_keys: Vec<Vec<u8>>,
        manual: bool,
    ) -> Result<Vec<Vec<u8>>, Status> {
        if split_partition_keys.is_empty() {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                "Unsupported number of split keys: 0",
            ));
        }
        let use_parent_table_id = (self.options.use_parent_table_id_field)();
        let snapshot = self.catalog.snapshot();
        let cluster = (!manual).then(|| self.cluster(&snapshot));
        let num_live = self.ts_manager.live().len();
        let locations = &self.ts_manager.locations;
        let (children, _) = self
            .writer
            .mutate(&token, |snapshot| {
                let tablet = snapshot
                    .get(SysRowEntryType::TABLET, tablet_id)
                    .ok_or_else(|| {
                        Status::new(
                            StatusCode::NotFound,
                            format!("Tablet {} not found", String::from_utf8_lossy(tablet_id)),
                        )
                    })?;
                validate_split_candidate(snapshot, tablet, manual).map_err(|e| {
                    Status::new(
                        StatusCode::from_i32(e.status.code().value()),
                        e.status.message().to_string(),
                    )
                })?;
                let mut parent: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                if !parent.split_tablet_ids.is_empty() {
                    tracing::info!(
                        "Resuming tablet split: {}",
                        String::from_utf8_lossy(tablet_id)
                    );
                    return Ok((Mutation::new(), parent.split_tablet_ids.clone()));
                }
                let table = snapshot
                    .get(SysRowEntryType::TABLE, parent.table_id())
                    .ok_or_else(|| {
                        Status::new(StatusCode::NotFound, "The tablet's table is gone")
                    })?;
                let table_pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                auto::check_partition_limits(
                    snapshot,
                    table,
                    &table_pb,
                    num_live,
                    &self.options.auto,
                )
                .map_err(|m| Status::new(StatusCode::IllegalState, m))?;
                // The cluster may have moved on since the split was picked: another phase, or a
                // replica being added (`ShouldSplitValidCandidate` again).
                if let Some(cluster) = &cluster {
                    let replicas = locations.get(tablet_id).unwrap_or_default();
                    let drive = replicas.leader().map(|l| l.drive.clone()).ok_or_else(|| {
                        Status::new(StatusCode::IllegalState, "No leader for the tablet")
                    })?;
                    auto::should_split(
                        snapshot,
                        tablet_id,
                        &table.key.id,
                        &table_pb,
                        &drive,
                        &replicas,
                        cluster,
                        &self.options.auto,
                    )
                    .map_err(|e| {
                        Status::new(
                            StatusCode::InvalidArgument,
                            format!(
                                "Tablet split candidate {} is no longer a valid split candidate: \
                                 {e}",
                                String::from_utf8_lossy(tablet_id)
                            ),
                        )
                    })?;
                }
                let partitions =
                    child_partitions(tablet_id, &parent.partition, &split_partition_keys)?;
                let mut ids: Vec<Vec<u8>> =
                    partitions.iter().map(|_| generate_object_id()).collect();
                ids.sort();
                let mut mutation = Mutation::new().read(tablet).read(table);
                for (id, partition) in ids.iter().zip(&partitions) {
                    // `SetupTabletInfo(CREATING)` and `RegisterNewTabletsForSplit`.
                    let mut child = SysTabletsEntryPB::new();
                    child.set_table_id(parent.table_id().to_vec());
                    if use_parent_table_id {
                        child.set_hosted_tables_mapped_by_parent_id(true);
                    } else {
                        child.table_ids.push(parent.table_id().to_vec());
                    }
                    child.partition = MessageField::some(partition.clone());
                    child.state = Some(EnumOrUnknown::new(TabletState::CREATING));
                    child.committed_consensus_state = parent.committed_consensus_state.clone();
                    child.set_split_depth(parent.split_depth() + 1);
                    child.set_split_parent_tablet_id(tablet_id.to_vec());
                    mutation = mutation.read_absent(SysRowEntryType::TABLET, id).upsert(
                        SysRowEntryType::TABLET,
                        id,
                        child.write_to_bytes().map_err(internal)?,
                    );
                    parent.split_tablet_ids.push(id.clone());
                }
                let mut table_pb = table_pb;
                let version = table_pb.partition_list_version() + 1;
                table_pb.set_partition_list_version(version);
                tracing::info!(
                    "Registering tablets {} to split tablet {} by partition key(s) {}, partition \
                     list version {version}",
                    ids.iter()
                        .map(|i| String::from_utf8_lossy(i).into_owned())
                        .collect::<Vec<_>>()
                        .join(", "),
                    String::from_utf8_lossy(tablet_id),
                    split_partition_keys
                        .iter()
                        .map(|k| hex(k))
                        .collect::<Vec<_>>()
                        .join(", ")
                );
                mutation = mutation
                    .upsert(
                        SysRowEntryType::TABLET,
                        tablet_id,
                        parent.write_to_bytes().map_err(internal)?,
                    )
                    .upsert(
                        SysRowEntryType::TABLE,
                        &table.key.id,
                        table_pb.write_to_bytes().map_err(internal)?,
                    );
                Ok((mutation, ids))
            })
            .await?;
        Ok(children)
    }

    /// `MasterAdmin.DeleteNotServingTablet`: the tserver retires a split parent once its children
    /// serve. The parent becomes DELETED and its replicas are deleted.
    pub async fn delete_not_serving_tablet(
        &self,
        token: LeaderToken,
        deadline: Instant,
        req: &DeleteNotServingTabletRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let tablet_id = req.tablet_id().to_vec();
        tracing::info!(
            "Servicing DeleteNotServingTablet request for tablet {}",
            String::from_utf8_lossy(&tablet_id)
        );
        let reason = format!(
            "Not serving tablet deleted upon request at {}",
            local_time_string()
        );
        let result = self
            .writer
            .mutate_with(
                &token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..WriteOptions::default()
                },
                |snapshot| {
                    let checked = check_can_delete(snapshot, &tablet_id);
                    let tablet = match checked {
                        Ok(t) => t,
                        Err(e) => return Ok((Mutation::new(), Err(e))),
                    };
                    let mut pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                    pb.state = Some(EnumOrUnknown::new(TabletState::DELETED));
                    pb.set_state_msg(reason.clone().into_bytes());
                    let mut mutation = Mutation::new().read(&tablet);
                    // The children must stay running while the parent goes.
                    for child in &pb.split_tablet_ids {
                        if let Some(c) = snapshot.get(SysRowEntryType::TABLET, child) {
                            mutation = mutation.read(c);
                        }
                    }
                    let peers: Vec<Vec<u8>> = pb
                        .committed_consensus_state
                        .config
                        .peers
                        .iter()
                        .map(|p| p.permanent_uuid().to_vec())
                        .collect();
                    Ok((
                        mutation.upsert(
                            SysRowEntryType::TABLET,
                            &tablet_id,
                            pb.write_to_bytes().map_err(internal)?,
                        ),
                        Ok(peers),
                    ))
                },
            )
            .await;
        let peers = match result {
            Ok((Ok(peers), _)) => peers,
            Ok((Err(e), _)) => return Err(e),
            Err(status) => return Err(error(Code::UNKNOWN_ERROR, status.code, status.message)),
        };
        // Every replica in the committed config or the observed locations; a tablet of a live
        // table: active transactions are not aborted, as `AsyncDeleteReplica` decides.
        let mut replicas: BTreeSet<Vec<u8>> = peers.into_iter().collect();
        if let Some(observed) = self.ts_manager.locations.get(&tablet_id) {
            replicas.extend(observed.replicas.iter().map(|r| r.ts_uuid.to_vec()));
        }
        for ts_uuid in replicas {
            self.ts_manager.deletes.push(DeleteReplica {
                token,
                ts_uuid: ts_uuid.into(),
                tablet_id: tablet_id.clone(),
                delete_type: TabletDataState::TABLET_DATA_DELETED,
                cas_config_index: None,
                hide_only: false,
                abort_active_txns: false,
                reason: reason.clone(),
            });
        }
        Ok(())
    }

    /// The encoded keys of the children's start partition keys (but the first's).
    fn encoded_keys(
        &self,
        partition_schema: &MessageField<yb_proto::pb::common::PartitionSchemaPB>,
        keys: &[Vec<u8>],
    ) -> Result<Vec<Vec<u8>>, Status> {
        let mut req = EncodedPartitionKeysRequestPB::new();
        req.partition_schema = partition_schema.clone();
        req.partition_keys = keys.to_vec();
        let bytes = self.host.call(
            "EncodedPartitionKeys",
            &req.write_to_bytes().map_err(internal)?,
        )?;
        let resp = EncodedPartitionKeysResponsePB::parse_from_bytes(&bytes).map_err(corrupt)?;
        if let Some(e) = resp.error.as_ref() {
            return Err(Status::new(
                StatusCode::from_i32(e.status.code().value()),
                e.status.message().to_string(),
            ));
        }
        Ok(resp.encoded_keys)
    }
}

/// `CreateNewTabletsPartition`: the children's partitions, each nonempty.
fn child_partitions(
    tablet_id: &[u8],
    parent: &MessageField<PartitionPB>,
    keys: &[Vec<u8>],
) -> Result<Vec<PartitionPB>, Status> {
    let start = parent.partition_key_start();
    let end = parent.partition_key_end();
    let n = keys.len() + 1;
    let mut partitions = Vec::with_capacity(n);
    for i in 0..n {
        let from = if i == 0 {
            start
        } else {
            keys[i - 1].as_slice()
        };
        let to = if i == n - 1 { end } else { keys[i].as_slice() };
        // An empty end is the end of the key space.
        let valid = from < to || (i == n - 1 && end.is_empty() && to.is_empty());
        if !valid {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                format!(
                    "Can't split tablet {} [partition_key_start: {} partition_key_end: {}) into \
                     {n} tablets due to invalid sub-partition at index {i}: \
                     [partition_key_start: {} partition_key_end: {})",
                    String::from_utf8_lossy(tablet_id),
                    hex(start),
                    hex(end),
                    hex(from),
                    hex(to)
                ),
            ));
        }
        let mut partition = PartitionPB::new();
        partition.hash_buckets = parent.hash_buckets.clone();
        partition.set_partition_key_start(from.to_vec());
        partition.set_partition_key_end(to.to_vec());
        partitions.push(partition);
    }
    Ok(partitions)
}

/// `ValidateSplitCandidateTable` and `ValidateSplitCandidateTablet`, for what a Rust leader
/// serves: YCQL tables that are not colocated.
pub fn validate_split_candidate(
    snapshot: &CatalogSnapshot,
    tablet: &Entity,
    manual: bool,
) -> Result<(), MasterErrorPB> {
    let not_supported = |m: String| error(Code::UNKNOWN_ERROR, StatusCode::NotSupported, m);
    let hot = tablet
        .tablet()
        .ok_or_else(|| not_supported("Not a tablet".into()))?;
    let table = snapshot
        .get(SysRowEntryType::TABLE, &hot.table_id)
        .ok_or_else(|| {
            error(
                Code::OBJECT_NOT_FOUND,
                StatusCode::NotFound,
                "The tablet's table does not exist",
            )
        })?;
    validate_table(snapshot, table)?;
    validate_tablet(snapshot, tablet, manual)
}

/// `ValidateSplitCandidateTable` without the limits and the tables held off for a while.
pub(crate) fn validate_table(
    snapshot: &CatalogSnapshot,
    table: &Entity,
) -> Result<(), MasterErrorPB> {
    let not_supported = |m: String| error(Code::UNKNOWN_ERROR, StatusCode::NotSupported, m);
    let table_hot = table.table().expect("table row");
    let table_pb: SysTablesEntryPB = table
        .parse()
        .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
    let name = || {
        format!(
            "{} [id={}]",
            table_hot.name,
            String::from_utf8_lossy(&table.key.id)
        )
    };
    use sys_tables_entry_pb::State as TableState;
    if table_hot.state == TableState::DELETING as i32
        || table_hot.state == TableState::DELETED as i32
    {
        return Err(not_supported(format!(
            "Table is being deleted; ignoring for splitting. table: {}",
            name()
        )));
    }
    if table_hot.hidden || is_hidden(table) {
        return Err(not_supported(format!(
            "Table is hidden; ignoring for splitting. table: {}",
            name()
        )));
    }
    if table_pb.table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE {
        return Err(not_supported(format!(
            "Tablet splitting is not supported for transaction status tables, table: {}",
            name()
        )));
    }
    if is_system_table(snapshot, table) {
        return Err(not_supported(format!(
            "Tablet splitting is not supported for system table: {}",
            name()
        )));
    }
    if table_pb.table_type() == TableType::REDIS_TABLE_TYPE {
        return Err(not_supported(format!(
            "Tablet splitting is not supported for YEDIS tables, table: {}",
            name()
        )));
    }
    if table_pb.table_type() == TableType::PGSQL_TABLE_TYPE {
        return Err(not_supported(format!(
            "Splitting a YSQL table on a Rust leader is not supported yet (W9): {}",
            name()
        )));
    }
    if !table_pb.hosted_stateful_services.is_empty() {
        return Err(error(
            Code::INVALID_REQUEST,
            StatusCode::NotSupported,
            format!(
                "Tablet splitting is not supported on tables that host stateful services, table: \
                 {}",
                name()
            ),
        ));
    }
    // A covered table's parent would be hidden, not deleted: W10.
    if !covering_schedules(snapshot, SysRowEntryType::TABLE, &table.key.id).is_empty() {
        return Err(not_supported(format!(
            "Splitting a table a snapshot schedule covers is not supported on a Rust leader yet \
             (W10): {}",
            name()
        )));
    }
    // A split must update the streams' tablet mappings (`ProcessSplitTabletResult`), which a
    // Rust leader does not do yet.
    if streamed(snapshot, &table.key.id) {
        return Err(not_supported(format!(
            "Splitting a table in an xCluster or CDC stream is not supported on a Rust leader \
             yet: {}",
            name()
        )));
    }
    // `IsBackfilling`: the index backfill writes to the indexed table's tablets.
    if table_pb
        .indexes
        .iter()
        .any(|i| i.index_permissions() == IndexPermissions::INDEX_PERM_DO_BACKFILL)
    {
        return Err(error(
            Code::SPLIT_OR_BACKFILL_IN_PROGRESS,
            StatusCode::IllegalState,
            format!("Backfill operation in progress, table: {}", name()),
        ));
    }
    Ok(())
}

/// A CDC stream on the table that is not being deleted, or an xCluster stream into it.
fn streamed(snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
    let outbound = snapshot.iter(SysRowEntryType::CDC_STREAM).any(|stream| {
        stream.parse::<SysCDCStreamEntryPB>().is_ok_and(|pb| {
            pb.table_id.iter().any(|t| t.as_bytes() == table_id)
                && !matches!(
                    pb.state(),
                    sys_cdcstream_entry_pb::State::DELETING
                        | sys_cdcstream_entry_pb::State::DELETED
                )
        })
    });
    let config: SysClusterConfigEntryPB = snapshot
        .cluster_config()
        .and_then(|c| c.parse().ok())
        .unwrap_or_default();
    outbound
        || config
            .consumer_registry
            .producer_map
            .values()
            .flat_map(|producer| producer.stream_map.values())
            .any(|stream| stream.consumer_table_id.as_bytes() == table_id)
}

/// `ValidateSplitCandidateTablet`, without TTL and the tablets held off for a while.
fn validate_tablet(
    snapshot: &CatalogSnapshot,
    tablet: &Entity,
    manual: bool,
) -> Result<(), MasterErrorPB> {
    let not_supported = |m: String| error(Code::UNKNOWN_ERROR, StatusCode::NotSupported, m);
    let hot = tablet.tablet().expect("tablet row");
    let tablet_pb: SysTabletsEntryPB = tablet
        .parse()
        .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
    // A child is split only once its parent is gone.
    if let Some(parent) = snapshot.get(SysRowEntryType::TABLET, tablet_pb.split_parent_tablet_id())
    {
        let deleted = parent
            .tablet()
            .is_some_and(|p| p.state == TabletState::DELETED as i32);
        if !deleted && !parent.tablet().is_some_and(|p| p.hidden) {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                format!(
                    "Cannot split tablet whose parent is not yet deleted. Child tablet id: {}, \
                     parent tablet id: {}.",
                    String::from_utf8_lossy(&tablet.key.id),
                    String::from_utf8_lossy(tablet_pb.split_parent_tablet_id())
                ),
            ));
        }
    }
    if tablet_pb.colocated() {
        return Err(not_supported(format!(
            "Tablet splitting is not supported for colocated tables, tablet_id: {}",
            String::from_utf8_lossy(&tablet.key.id)
        )));
    }
    if hot.state != TabletState::RUNNING as i32 {
        return Err(error(
            Code::TABLET_NOT_RUNNING,
            StatusCode::IllegalState,
            format!(
                "Tablet is not in running state: {}",
                TabletState::from_i32(hot.state).map_or("UNKNOWN".into(), |s| format!("{s:?}"))
            ),
        ));
    }
    let _ = manual;
    Ok(())
}

/// `CheckIfCanDeleteSingleTablet`: the tablet, not deleted, with its range covered by running
/// tablets of its table.
fn check_can_delete(
    snapshot: &CatalogSnapshot,
    tablet_id: &[u8],
) -> Result<Arc<Entity>, MasterErrorPB> {
    let tablet = snapshot
        .get(SysRowEntryType::TABLET, tablet_id)
        .ok_or_else(|| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::NotFound,
                format!("Tablet {} not found", String::from_utf8_lossy(tablet_id)),
            )
        })?;
    let hot = tablet.tablet().expect("tablet row");
    if hot.state == TabletState::DELETED as i32 {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::NotFound,
            format!(
                "Tablet {} has been already deleted",
                String::from_utf8_lossy(tablet_id)
            ),
        ));
    }
    if snapshot
        .get(SysRowEntryType::TABLE, &hot.table_id)
        .is_some_and(|t| {
            !covering_schedules(snapshot, SysRowEntryType::TABLE, &t.key.id).is_empty()
        })
    {
        return Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::NotSupported,
            "Hiding a split parent of a table a snapshot schedule covers is not supported on a \
             Rust leader yet (W10)",
        ));
    }
    let gap = |from: &[u8], to: &[u8]| {
        let s = |k: &[u8]| {
            if k.is_empty() {
                "{empty}".to_string()
            } else {
                hex(k)
            }
        };
        error(
            Code::UNKNOWN_ERROR,
            StatusCode::IllegalState,
            format!(
                "Can't delete tablet {} not covered by child tablets. Partition gap: {} ... {}",
                String::from_utf8_lossy(tablet_id),
                s(from),
                s(to)
            ),
        )
    };
    let (start, end) = (hot.partition_start.as_slice(), hot.partition_end.as_slice());
    let mut running: Vec<(&[u8], &[u8])> = snapshot
        .tablets_of(&hot.table_id)
        .into_iter()
        .filter(|t| &*t.key.id != tablet_id)
        .filter_map(|t| t.tablet())
        .filter(|t| t.state == TabletState::RUNNING as i32)
        .filter(|t| {
            // In range: starts at or after the start and before the end.
            t.partition_start.as_slice() >= start
                && (end.is_empty() || t.partition_start.as_slice() < end)
        })
        .map(|t| (t.partition_start.as_slice(), t.partition_end.as_slice()))
        .collect();
    running.sort();
    // C++ lets a tablet over the whole key space go with nothing covering it, since its empty
    // start and end compare equal; some running tablet must cover it here.
    if running.is_empty() {
        return Err(gap(start, end));
    }
    let mut key: &[u8] = start;
    for (from, to) in running {
        if key != from {
            return Err(gap(key, from));
        }
        key = to;
        if !end.is_empty() && key >= end {
            break;
        }
    }
    if key != end {
        return Err(gap(key, end));
    }
    Ok(tablet.clone())
}

/// The `tablet-splits` controller for one term: `SplitTablet` to the leader of every registered
/// split whose children are not all running, once per term and again after a failure.
pub struct SplitController {
    manager: Arc<TabletSplitManager>,
    /// Parents sent this term, and whether the send failed for good.
    sent: Arc<Mutex<HashMap<Vec<u8>, bool>>>,
}

impl SplitController {
    /// A controller for the term that just began.
    pub fn new(manager: Arc<TabletSplitManager>) -> Arc<SplitController> {
        Arc::new(SplitController {
            manager,
            sent: Arc::default(),
        })
    }

    fn pass(&self, scope: &TermScope) {
        let Ok(token) = self.manager.leadership.token() else {
            return;
        };
        self.manager.auto_split(token, scope);
        let snapshot = self.manager.catalog.snapshot();
        for tablet in snapshot.iter(SysRowEntryType::TABLET) {
            let Some(hot) = tablet.tablet() else { continue };
            if hot.split_tablet_ids.is_empty()
                || hot.state == TabletState::DELETED as i32
                || hot.hidden
            {
                continue;
            }
            let children_running = hot.split_tablet_ids.iter().all(|c| {
                snapshot
                    .get(SysRowEntryType::TABLET, c)
                    .and_then(|c| c.tablet())
                    .is_some_and(|c| c.state == TabletState::RUNNING as i32)
            });
            if children_running {
                continue;
            }
            if self.sent.lock().unwrap().get(&*tablet.key.id) == Some(&false) {
                continue;
            }
            self.sent
                .lock()
                .unwrap()
                .insert(tablet.key.id.to_vec(), false);
            let (manager, sent) = (self.manager.clone(), self.sent.clone());
            let tablet = tablet.clone();
            let snapshot = snapshot.clone();
            scope.entity().spawn("split-tablet", async move {
                if let Err(status) = send_split(&manager, token, &snapshot, &tablet).await {
                    tracing::warn!(
                        "SplitTablet of {} failed: {status}",
                        String::from_utf8_lossy(&tablet.key.id)
                    );
                    sent.lock().unwrap().insert(tablet.key.id.to_vec(), true);
                }
            });
        }
    }
}

/// `AsyncSplitTablet`: the split by the registered children's partitions.
async fn send_split(
    manager: &TabletSplitManager,
    token: LeaderToken,
    snapshot: &CatalogSnapshot,
    tablet: &Entity,
) -> Result<(), Status> {
    let hot = tablet.tablet().expect("tablet row");
    let table = snapshot
        .get(SysRowEntryType::TABLE, &hot.table_id)
        .ok_or_else(|| Status::new(StatusCode::NotFound, "The tablet's table is gone"))?;
    let table_pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
    let mut partition_keys = Vec::new();
    for child in hot.split_tablet_ids.iter().skip(1) {
        let c = snapshot
            .get(SysRowEntryType::TABLET, child)
            .and_then(|c| c.tablet())
            .ok_or_else(|| Status::new(StatusCode::NotFound, "A split child is gone"))?;
        partition_keys.push(c.partition_start.clone());
    }
    let encoded_keys = manager.encoded_keys(&table_pb.partition_schema, &partition_keys)?;
    let mut req = TsSplitTabletRequestPB::new();
    req.set_tablet_id(tablet.key.id.to_vec());
    req.set_DEPRECATED_new_tablet1_id(hot.split_tablet_ids[0].clone());
    req.set_DEPRECATED_new_tablet2_id(hot.split_tablet_ids[1].clone());
    req.set_DEPRECATED_split_encoded_key(encoded_keys[0].clone());
    req.set_DEPRECATED_split_partition_key(partition_keys[0].clone());
    req.new_tablet_ids = hot.split_tablet_ids.clone();
    req.split_encoded_keys = encoded_keys;
    req.split_partition_keys = partition_keys;
    let tablet_id = tablet.key.id.to_vec();
    let rpc_timeout = (manager.options.rpc_timeout)();
    let rpc = manager.rpc()?;
    let leader = manager
        .leader_of(&tablet_id)
        .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No leader for the tablet"))?;
    tracing::info!(
        "Sending SplitTablet of {} to {}",
        String::from_utf8_lossy(&tablet_id),
        String::from_utf8_lossy(&leader)
    );
    manager
        .executor
        .run(&leader, "SplitTablet", &manager.retry, |_| {
            let current = manager.leadership.check(&token);
            // The leader may move between attempts: `AsyncTabletLeaderTask` picks it again.
            let leader = manager
                .leader_of(&tablet_id)
                .unwrap_or_else(|| leader.clone());
            let address = manager.ts_manager.rpc_address(&leader);
            let (rpc, mut req) = (rpc.clone(), req.clone());
            req.set_dest_uuid(leader);
            async move {
                current.map_err(Attempt::Fail)?;
                let (host, port) = address.ok_or_else(|| {
                    Attempt::Retry(Status::new(
                        StatusCode::NotFound,
                        "tserver is not registered",
                    ))
                })?;
                let bytes = req
                    .write_to_bytes()
                    .map_err(|e| Attempt::Fail(internal(e)))?;
                let resp = rpc
                    .call(
                        &host,
                        port,
                        "yb.tserver.TabletServerAdminService",
                        "SplitTablet",
                        &bytes,
                        rpc_timeout,
                    )
                    .await
                    .map_err(Attempt::Retry)?;
                let resp = TsSplitTabletResponsePB::parse_from_bytes(&resp)
                    .map_err(|e| Attempt::Retry(corrupt(e)))?;
                let Some(e) = resp.error.as_ref() else {
                    return Ok(());
                };
                let status = Status::new(
                    StatusCode::from_i32(e.status.code().value()),
                    e.status.message().to_string(),
                );
                if status.code == StatusCode::AlreadyPresent {
                    return Ok(());
                }
                Err(Attempt::Retry(status))
            }
        })
        .await
}

impl Controller for SplitController {
    fn name(&self) -> &'static str {
        "tablet-splits"
    }

    fn interval(&self) -> Duration {
        (self.manager.options.interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move { self.pass(scope) })
    }
}
