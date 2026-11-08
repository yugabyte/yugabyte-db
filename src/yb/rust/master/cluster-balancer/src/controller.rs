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

//! The `cluster-balancer` controller: each pass builds a [`ClusterView`] from the catalog, the
//! tserver registry and the observed locations, runs the planner, and sends its actions as
//! `AsyncAddServerTask`, `AsyncRemoveServerTask` and `AsyncTryStepDown` do. The tasks in flight
//! live for the term and go back into the next view, as C++'s table tasks do.

use std::collections::{BTreeSet, HashMap, HashSet};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::{Catalog, CatalogSnapshot};
use master_leadership::{LeaderToken, Leadership};
use master_table_lifecycle::lookup::{is_colocation_parent_table_id, is_system_table};
use master_table_lifecycle::table_replication_info;
use master_task_runtime::{Attempt, BoxFuture, Controller, RetryPolicy, TermScope, TsRpcExecutor};
use master_ts_manager::placement::{
    blacklist_addresses, is_blacklisted, leader_blacklist_addresses,
};
use master_ts_manager::TsManager;
use protobuf::{Enum, EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus::{
    ChangeConfigRequestPB, ChangeConfigResponsePB, LeaderStepDownRequestPB,
    LeaderStepDownResponsePB,
};
use yb_proto::pb::consensus_metadata::{PeerMemberType, RaftPeerPB};
use yb_proto::pb::consensus_types::ChangeConfigType;
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tserver_types::tablet_server_error_pb::Code as TsCode;

use crate::{
    Action, ActivityBuffer, ClusterBalancer, ClusterView, Options, PendingTasks, ReplicaInfo,
    RunMetrics, TableInfo, TabletInfo, TsInfo,
};

const CONSENSUS_SERVICE: &str = "yb.consensus.ConsensusService";

/// What the balancer RPCs read: the recent runs and the last run's metrics.
#[derive(Debug)]
pub struct BalancerStatus {
    activities: Mutex<ActivityBuffer>,
    metrics: Mutex<RunMetrics>,
    /// When this master's current leader term began (`TimeSinceElectedLeader`).
    leader_since: Mutex<Option<Instant>>,
}

impl BalancerStatus {
    /// An empty status keeping `num_idle_runs` runs.
    pub fn new(num_idle_runs: usize) -> BalancerStatus {
        BalancerStatus {
            activities: Mutex::new(ActivityBuffer::new(num_idle_runs)),
            metrics: Mutex::default(),
            leader_since: Mutex::default(),
        }
    }

    /// How long this master has led, if it does.
    pub fn time_since_elected(&self) -> Option<Duration> {
        self.leader_since.lock().unwrap().map(|t| t.elapsed())
    }

    /// `ClusterBalancerActivityBuffer::IsIdle`: every recent placement run was idle.
    pub fn is_idle(&self) -> bool {
        self.activities.lock().unwrap().is_idle()
    }

    /// The last run's metrics.
    pub fn metrics(&self) -> RunMetrics {
        self.metrics.lock().unwrap().clone()
    }

    /// The last placement run.
    pub fn latest(&self) -> Option<crate::Activity> {
        self.activities.lock().unwrap().latest().cloned()
    }

    fn record(&self, result: &crate::RunResult, num_idle_runs: usize) {
        let mut activities = self.activities.lock().unwrap();
        activities.set_capacity(num_idle_runs);
        for activity in &result.activities {
            activities.record(activity.clone());
        }
        *self.metrics.lock().unwrap() = result.metrics.clone();
    }
}

/// The flags the controller reads, each pass.
#[derive(Clone)]
pub struct BalancerFlags {
    /// `enable_load_balancing` and `ChangeLoadBalancerState`.
    pub enabled: Arc<dyn Fn() -> bool + Send + Sync>,
    /// `load_balancer_initial_delay_secs`: no balancing this soon after becoming leader.
    pub initial_delay: fn() -> Duration,
    /// `catalog_manager_bg_task_wait_ms`.
    pub interval: fn() -> Duration,
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
    /// `min_leader_stepdown_retry_interval_ms`.
    pub min_leader_stepdown_retry_interval: fn() -> Duration,
    /// `load_balancer_num_idle_runs`.
    pub num_idle_runs: fn() -> usize,
    /// The planner's flags.
    pub options: fn() -> Options,
    /// `TEST_pause_catalog_manager_bg_loop_start` and `_end`: tests hold the C++ background loop,
    /// which runs the balancer, before or after a run, to get exactly one run.
    pub pause_before_run: fn() -> bool,
    /// See `pause_before_run`.
    pub pause_after_run: fn() -> bool,
    /// Publishes the "cluster" metrics after each pass: whether balancing is enabled, and the
    /// run's metrics when it ran (`ReportMetrics`, `UpdatePerRunMetrics`).
    pub report: MetricsReporter,
}

/// See [`BalancerFlags::report`].
pub type MetricsReporter = Arc<dyn Fn(bool, Option<&crate::RunMetrics>) + Send + Sync>;

/// Waits while `pause` holds, as `TEST_PAUSE_IF_FLAG` does.
async fn wait_while(pause: fn() -> bool) {
    while pause() {
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

/// The balancer for one leader term.
pub struct BalancerController(Arc<Inner>);

struct Inner {
    catalog: Arc<Catalog>,
    leadership: Arc<Leadership>,
    ts_manager: Arc<TsManager>,
    rpc: Arc<dyn RpcClient>,
    executor: Arc<TsRpcExecutor>,
    retry: RetryPolicy,
    flags: BalancerFlags,
    status: Arc<BalancerStatus>,
    planner: Mutex<ClusterBalancer>,
    /// The tasks in flight, by table.
    tasks: Mutex<HashMap<String, PendingTasks>>,
    /// Per tablet, when a stepdown to each intended leader last failed.
    stepdown_failures: Mutex<HashMap<String, HashMap<String, Instant>>>,
    term_start: Instant,
}

impl BalancerController {
    /// A balancer for the term that just began.
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        catalog: Arc<Catalog>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        rpc: Arc<dyn RpcClient>,
        executor: Arc<TsRpcExecutor>,
        retry: RetryPolicy,
        flags: BalancerFlags,
        status: Arc<BalancerStatus>,
    ) -> Arc<BalancerController> {
        *status.leader_since.lock().unwrap() = Some(Instant::now());
        Arc::new(BalancerController(Arc::new(Inner {
            catalog,
            leadership,
            ts_manager,
            rpc,
            executor,
            retry,
            flags,
            status,
            planner: Mutex::default(),
            tasks: Mutex::default(),
            stepdown_failures: Mutex::default(),
            term_start: Instant::now(),
        })))
    }
}

impl Controller for BalancerController {
    fn name(&self) -> &'static str {
        "cluster-balancer"
    }

    fn interval(&self) -> Duration {
        (self.0.flags.interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            wait_while(self.0.flags.pause_before_run).await;
            // A run is O(tablets) (seconds at millions of tablets): off the async workers.
            let inner = self.0.clone();
            let Ok(Some((token, result))) = tokio::task::spawn_blocking(move || inner.plan()).await
            else {
                return;
            };
            for action in result.actions() {
                self.0.send(scope, token, action.clone());
            }
            wait_while(self.0.flags.pause_after_run).await;
        })
    }
}

fn utf8(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

impl Inner {
    /// One run of the planner over the cluster now, recorded for the RPCs.
    fn plan(&self) -> Option<(LeaderToken, crate::RunResult)> {
        let token = self.leadership.token().ok()?;
        let enabled = (self.flags.enabled)();
        if !enabled || self.term_start.elapsed() <= (self.flags.initial_delay)() {
            (self.flags.report)(enabled, None);
            return None;
        }
        let snapshot = self.catalog.snapshot();
        let view = self.view(&snapshot);
        let result = self
            .planner
            .lock()
            .unwrap()
            .run(&view, &(self.flags.options)());
        self.status.record(&result, (self.flags.num_idle_runs)());
        (self.flags.report)(true, Some(&result.metrics));
        Some((token, result))
    }

    /// The planner's view of the cluster now.
    fn view(&self, snapshot: &CatalogSnapshot) -> ClusterView {
        let config: SysClusterConfigEntryPB = snapshot
            .cluster_config()
            .and_then(|c| c.parse().ok())
            .unwrap_or_default();
        let blacklist = blacklist_addresses(&config);
        let leader_blacklist = leader_blacklist_addresses(&config);
        let live: HashSet<Vec<u8>> = self
            .ts_manager
            .live()
            .into_iter()
            .map(|d| d.uuid.to_vec())
            .collect();
        let pending_deletes = self.ts_manager.deletes.pending();
        let tservers = self
            .ts_manager
            .all()
            .into_iter()
            .map(|d| {
                let reg = &d.pb.registration;
                let (used, capacity) = d.metrics.as_ref().map_or((0, 0), |m| {
                    m.path_metrics.iter().fold((0, 0), |(u, c), p| {
                        (u + p.used_space(), c + p.total_space())
                    })
                });
                TsInfo {
                    uuid: utf8(&d.uuid),
                    cloud_info: reg.cloud_info.clone().unwrap_or_default(),
                    placement_uuid: utf8(reg.placement_uuid()),
                    live_and_reported: live.contains(&*d.uuid) && d.has_tablet_report,
                    heartbeat_age: d.last_heartbeat.map_or(Duration::MAX, |t| t.elapsed()),
                    blacklisted: is_blacklisted(&d, &blacklist),
                    leader_blacklisted: is_blacklisted(&d, &leader_blacklist),
                    disk_used_bytes: used,
                    disk_capacity_bytes: capacity,
                    pending_deletes: pending_deletes
                        .get(&*d.uuid)
                        .map(|t| t.iter().map(|id| utf8(id)).collect())
                        .unwrap_or_default(),
                }
            })
            .collect();
        let replication = config.replication_info.clone().unwrap_or_default();
        let retry_interval = (self.flags.min_leader_stepdown_retry_interval)();
        let failures = self.stepdown_failures.lock().unwrap().clone();
        let tasks = self.tasks.lock().unwrap().clone();
        let locations = &self.ts_manager.locations;
        let mut tables = Vec::new();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else { continue };
            let id = utf8(&table.key.id);
            let started_deleting = hot.state == sys_tables_entry_pb::State::DELETING as i32
                || hot.state == sys_tables_entry_pb::State::DELETED as i32;
            let pb = table.parse::<yb_proto::pb::catalog_entity_info::SysTablesEntryPB>();
            // A colocated table other than the parent lives in the parent's tablets.
            let secondary = pb.as_ref().is_ok_and(|pb| pb.colocated())
                && !is_colocation_parent_table_id(&table.key.id);
            if started_deleting || secondary || is_system_table(snapshot, table) {
                tables.push(TableInfo {
                    id,
                    skip: true,
                    ..TableInfo::default()
                });
                continue;
            }
            let Ok(pb) = pb else { continue };
            let tablets = snapshot
                .tablets_of(&table.key.id)
                .into_iter()
                .map(|tablet| {
                    let tablet_id = utf8(&tablet.key.id);
                    let observed = locations.get(&tablet.key.id).unwrap_or_default();
                    let replicas = observed
                        .replicas
                        .iter()
                        .map(|r| {
                            (
                                utf8(&r.ts_uuid),
                                ReplicaInfo {
                                    is_leader: r.role == PeerRole::LEADER,
                                    state: r.state,
                                    member_type: r.member_type,
                                    fs_data_dir: r.fs_data_dir.clone(),
                                    total_size: r.drive.total_size,
                                    should_disable_lb_move: r.should_disable_lb_move,
                                },
                            )
                        })
                        .collect();
                    TabletInfo {
                        running: tablet.tablet().is_some_and(|t| {
                            t.state == sys_tablets_entry_pb::State::RUNNING as i32
                        }),
                        replicas,
                        // From the leader-reported config, as the observed locations hold it.
                        config_in_transition: observed
                            .replicas
                            .iter()
                            .any(|r| r.member_type == PeerMemberType::PRE_VOTER),
                        recent_stepdown_failures: failures
                            .get(&tablet_id)
                            .map(|f| {
                                f.iter()
                                    .filter(|(_, at)| at.elapsed() < retry_interval)
                                    .map(|(ts, _)| ts.clone())
                                    .collect::<BTreeSet<_>>()
                            })
                            .unwrap_or_default(),
                        id: tablet_id,
                    }
                })
                .collect();
            tables.push(TableInfo {
                is_transaction_status_table: hot.table_type
                    == yb_proto::pb::common_types::TableType::TRANSACTION_STATUS_TABLE_TYPE.value(),
                replication_info: table_replication_info(pb.replication_info.as_ref(), &config),
                tablets,
                pending: tasks.get(&id).cloned().unwrap_or_default(),
                id,
                ..TableInfo::default()
            });
        }
        ClusterView {
            tservers,
            live_placement_uuid: utf8(replication.live_replicas.placement_uuid()),
            read_replica_placement_uuids: replication
                .read_replicas
                .iter()
                .map(|p| utf8(p.placement_uuid()))
                .collect(),
            tables,
        }
    }

    /// Tracks the action as a task of its table and sends it.
    fn send(self: &Arc<Self>, scope: &TermScope, token: LeaderToken, action: Action) {
        let this = self.clone();
        match action {
            Action::AddReplica {
                table_id,
                tablet_id,
                ts_uuid,
                member_type,
                ..
            } => {
                self.track(&table_id, |t| &mut t.adds, &tablet_id, &ts_uuid);
                scope.entity().spawn("add-server", async move {
                    let result = this
                        .change_config(token, &tablet_id, &ts_uuid, Some(member_type))
                        .await;
                    this.log_change_config("ADD_SERVER", &tablet_id, &ts_uuid, result);
                    this.untrack(&table_id, |t| &mut t.adds, &tablet_id);
                });
            }
            Action::RemoveReplica {
                table_id,
                tablet_id,
                ts_uuid,
                ..
            } => {
                self.track(&table_id, |t| &mut t.removes, &tablet_id, &ts_uuid);
                scope.entity().spawn("remove-server", async move {
                    let result = this.change_config(token, &tablet_id, &ts_uuid, None).await;
                    this.log_change_config("REMOVE_SERVER", &tablet_id, &ts_uuid, result);
                    this.untrack(&table_id, |t| &mut t.removes, &tablet_id);
                });
            }
            Action::MoveLeader {
                table_id,
                tablet_id,
                from_ts,
                to_ts,
                also_remove_replica,
                ..
            } => {
                self.track(&table_id, |t| &mut t.stepdowns, &tablet_id, &to_ts);
                scope.entity().spawn("try-step-down", async move {
                    this.step_down(token, &tablet_id, &from_ts, &to_ts).await;
                    this.untrack(&table_id, |t| &mut t.stepdowns, &tablet_id);
                    if also_remove_replica {
                        this.track(&table_id, |t| &mut t.removes, &tablet_id, &from_ts);
                        let result = this.change_config(token, &tablet_id, &from_ts, None).await;
                        this.log_change_config("REMOVE_SERVER", &tablet_id, &from_ts, result);
                        this.untrack(&table_id, |t| &mut t.removes, &tablet_id);
                    }
                });
            }
        }
    }

    fn track(
        &self,
        table_id: &str,
        kind: fn(&mut PendingTasks) -> &mut std::collections::BTreeMap<String, String>,
        tablet_id: &str,
        ts_uuid: &str,
    ) {
        let mut tasks = self.tasks.lock().unwrap();
        kind(tasks.entry(table_id.to_string()).or_default())
            .insert(tablet_id.to_string(), ts_uuid.to_string());
    }

    fn untrack(
        &self,
        table_id: &str,
        kind: fn(&mut PendingTasks) -> &mut std::collections::BTreeMap<String, String>,
        tablet_id: &str,
    ) {
        let mut tasks = self.tasks.lock().unwrap();
        if let Some(t) = tasks.get_mut(table_id) {
            kind(t).remove(tablet_id);
            if t.is_empty() {
                tasks.remove(table_id);
            }
        }
    }

    fn log_change_config(
        &self,
        kind: &str,
        tablet_id: &str,
        ts_uuid: &str,
        result: Result<(), Status>,
    ) {
        match result {
            Ok(()) => tracing::info!(
                "Change config {kind} of tablet {tablet_id} for replica {ts_uuid} is done"
            ),
            Err(status) => tracing::warn!(
                "Change config {kind} of tablet {tablet_id} for replica {ts_uuid} failed: {status}"
            ),
        }
    }

    /// The tablet's committed config index and whether it is still RUNNING.
    fn committed_index(&self, tablet_id: &[u8]) -> Option<i64> {
        let snapshot = self.catalog.snapshot();
        let tablet = snapshot.get(SysRowEntryType::TABLET, tablet_id)?;
        if tablet.tablet()?.state != sys_tablets_entry_pb::State::RUNNING as i32 {
            return None;
        }
        let pb: SysTabletsEntryPB = tablet.parse().ok()?;
        Some(pb.committed_consensus_state.config.committed_op_index())
    }

    /// `AsyncAddServerTask` (with a member type) or `AsyncRemoveServerTask`: `ChangeConfig` on the
    /// tablet's leader, with the committed config index as the compare-and-set index.
    async fn change_config(
        self: &Arc<Self>,
        token: LeaderToken,
        tablet_id: &str,
        ts_uuid: &str,
        member_type: Option<PeerMemberType>,
    ) -> Result<(), Status> {
        let aborted = |m: String| Status::new(StatusCode::Aborted, m);
        let Some(cas_index) = self.committed_index(tablet_id.as_bytes()) else {
            return Err(aborted("Tablet is not running".into()));
        };
        let mut server = RaftPeerPB::new();
        server.set_permanent_uuid(ts_uuid.as_bytes().to_vec());
        if let Some(member_type) = member_type {
            let Some(desc) = self
                .ts_manager
                .live()
                .into_iter()
                .find(|d| *d.uuid == *ts_uuid.as_bytes())
            else {
                return Err(Status::new(
                    StatusCode::TimedOut,
                    format!("Could not find desired replica {ts_uuid} in live set"),
                ));
            };
            let reg = &desc.pb.registration;
            if reg.private_rpc_addresses.is_empty() {
                return Err(Status::new(
                    StatusCode::IllegalState,
                    format!("Candidate replacement {ts_uuid} has no registered rpc address"),
                ));
            }
            server.member_type = Some(EnumOrUnknown::new(member_type));
            server.last_known_private_addr = reg.private_rpc_addresses.clone();
            server.last_known_broadcast_addr = reg.broadcast_addresses.clone();
            if reg.cloud_info.is_some() {
                server.cloud_info = reg.cloud_info.clone();
            }
        }
        let mut req = ChangeConfigRequestPB::new();
        req.set_tablet_id(tablet_id.as_bytes().to_vec());
        req.type_ = Some(EnumOrUnknown::new(if member_type.is_some() {
            ChangeConfigType::ADD_SERVER
        } else {
            ChangeConfigType::REMOVE_SERVER
        }));
        req.set_cas_config_opid_index(cas_index);
        req.server = Some(server).into();
        let Some(leader) = self
            .ts_manager
            .locations
            .get(tablet_id.as_bytes())
            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
        else {
            return Err(aborted("The tablet has no known leader".into()));
        };
        req.set_dest_uuid(leader.clone());
        let rpc_timeout = (self.flags.rpc_timeout)();
        let this = self.clone();
        self.executor
            .run(&leader, "ChangeConfig", &self.retry, |_| {
                let current = this.leadership.check(&token);
                let address = this.ts_manager.rpc_address(&leader);
                // A newer committed config makes this change stale.
                let index = this.committed_index(tablet_id.as_bytes());
                let (rpc, req) = (this.rpc.clone(), req.clone());
                async move {
                    current.map_err(Attempt::Fail)?;
                    match index {
                        None => {
                            return Err(Attempt::Fail(Status::new(
                                StatusCode::Aborted,
                                "Tablet is not running",
                            )))
                        }
                        Some(latest) if latest > cas_index => {
                            return Err(Attempt::Fail(Status::new(
                                StatusCode::Aborted,
                                format!(
                                    "Latest config has opid_index of {latest} while this task \
                                     has opid_index of {cas_index}"
                                ),
                            )))
                        }
                        _ => {}
                    }
                    let (host, port) = address.ok_or_else(|| {
                        Attempt::Retry(Status::new(
                            StatusCode::NotFound,
                            "tserver is not registered",
                        ))
                    })?;
                    let bytes = req.write_to_bytes().map_err(|e| {
                        Attempt::Fail(Status::new(StatusCode::InternalError, e.to_string()))
                    })?;
                    let resp = rpc
                        .call(
                            &host,
                            port,
                            CONSENSUS_SERVICE,
                            "ChangeConfig",
                            &bytes,
                            rpc_timeout,
                        )
                        .await
                        .map_err(Attempt::Retry)?;
                    let resp = ChangeConfigResponsePB::parse_from_bytes(&resp).map_err(|e| {
                        Attempt::Retry(Status::new(StatusCode::Corruption, e.to_string()))
                    })?;
                    let Some(error) = resp.error.as_ref() else {
                        return Ok(());
                    };
                    let status = Status::new(
                        StatusCode::from_i32(error.status.code().value()),
                        error.status.message().to_string(),
                    );
                    match error.code() {
                        // Not retried, as `AsyncChangeConfigTask::HandleResponse`.
                        TsCode::CAS_FAILED
                        | TsCode::ADD_CHANGE_CONFIG_ALREADY_PRESENT
                        | TsCode::REMOVE_CHANGE_CONFIG_NOT_PRESENT
                        | TsCode::LEADER_NEEDS_STEP_DOWN
                        | TsCode::NOT_THE_LEADER => Err(Attempt::Fail(status)),
                        _ => Err(Attempt::Retry(status)),
                    }
                }
            })
            .await
    }

    /// `AsyncTryStepDown`: one `LeaderStepDown` to the leader; a failed stepdown keeps the
    /// planner from nominating the same new leader for `min_leader_stepdown_retry_interval_ms`.
    async fn step_down(
        self: &Arc<Self>,
        token: LeaderToken,
        tablet_id: &str,
        from: &str,
        to: &str,
    ) {
        let mut req = LeaderStepDownRequestPB::new();
        req.set_dest_uuid(from.as_bytes().to_vec());
        req.set_tablet_id(tablet_id.as_bytes().to_vec());
        if !to.is_empty() {
            req.set_new_leader_uuid(to.as_bytes().to_vec());
        }
        tracing::info!(
            "Stepping down leader {from} for tablet {tablet_id}{}",
            if to.is_empty() {
                String::new()
            } else {
                format!(" with new leader {to}")
            }
        );
        let once = RetryPolicy {
            max_attempts: 1,
            ..self.retry
        };
        let rpc_timeout = (self.flags.rpc_timeout)();
        let this = self.clone();
        let result = self
            .executor
            .run(from.as_bytes(), "LeaderStepDown", &once, |_| {
                let current = this.leadership.check(&token);
                let address = this.ts_manager.rpc_address(from.as_bytes());
                let (rpc, req) = (this.rpc.clone(), req.clone());
                async move {
                    current.map_err(Attempt::Fail)?;
                    let (host, port) = address.ok_or_else(|| {
                        Attempt::Fail(Status::new(
                            StatusCode::NotFound,
                            "tserver is not registered",
                        ))
                    })?;
                    let bytes = req.write_to_bytes().map_err(|e| {
                        Attempt::Fail(Status::new(StatusCode::InternalError, e.to_string()))
                    })?;
                    let resp = rpc
                        .call(
                            &host,
                            port,
                            CONSENSUS_SERVICE,
                            "LeaderStepDown",
                            &bytes,
                            rpc_timeout,
                        )
                        .await
                        .map_err(Attempt::Fail)?;
                    LeaderStepDownResponsePB::parse_from_bytes(&resp).map_err(|e| {
                        Attempt::Fail(Status::new(StatusCode::Corruption, e.to_string()))
                    })
                }
            })
            .await;
        match result {
            Ok(resp) => {
                let failed = resp
                    .error
                    .as_ref()
                    .is_some_and(|e| e.status.code().value() != 0);
                if failed && !to.is_empty() {
                    // C++ registers the failure under the old leader's uuid, but looks failures up
                    // by the intended leader; they are recorded by the intended leader here.
                    self.stepdown_failures
                        .lock()
                        .unwrap()
                        .entry(tablet_id.to_string())
                        .or_default()
                        .insert(to.to_string(), Instant::now());
                }
                tracing::info!(
                    "Leader step down of tablet {tablet_id} from {from} done, failed: {failed}"
                );
            }
            Err(status) => tracing::warn!(
                "Leader step down of tablet {tablet_id} from {from} failed: {status}"
            ),
        }
    }
}
