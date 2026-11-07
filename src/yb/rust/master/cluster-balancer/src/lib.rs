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

//! The cluster balancer as a pure planner (K8): `ClusterLoadBalancer` and `PerTableLoadState`
//! (`src/yb/master/cluster_balance.cc`, `cluster_balance_util.cc`) with today's policies, over a
//! view of the cluster instead of `CatalogManager`'s maps.
//!
//! A run takes a [`ClusterView`] (tservers, tables with their tablets' observed replicas, and the
//! balancer tasks still in flight) and returns the [`Action`]s to send, the warnings that kept it
//! from making progress, and its metrics. Like C++ it keeps no state between runs except whether
//! global balancing is allowed; the tasks in flight carry the rest. The caller (the
//! `cluster-balancer` controller) builds the view, sends the actions and tracks them as tasks.

pub mod controller;
mod distribution;
mod planner;
mod state;

#[cfg(test)]
mod tests;

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::sync::Arc;
use std::time::Duration;

use yb_proto::pb::common_net::{CloudInfoPB, ReplicationInfoPB};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;

pub use distribution::{calculate_optimal_load_distribution, calculate_table_load_difference};

/// A tserver as the balancer sees it (`TSDescriptor`).
#[derive(Clone, Debug, Default)]
pub struct TsInfo {
    /// The permanent uuid.
    pub uuid: String,
    /// The registration's cloud, region and zone.
    pub cloud_info: CloudInfoPB,
    /// The registration's placement uuid: the live cluster's, or a read replica cluster's.
    pub placement_uuid: String,
    /// `IsLiveAndHasReported`.
    pub live_and_reported: bool,
    /// `TimeSinceHeartbeat`.
    pub heartbeat_age: Duration,
    /// On the server blacklist, or with a faulty drive (`AddTSIfBlacklisted`).
    pub blacklisted: bool,
    /// On the leader blacklist.
    pub leader_blacklisted: bool,
    /// Summed over the data paths of the last metrics heartbeat.
    pub disk_used_bytes: u64,
    /// Disk capacity, summed like `disk_used_bytes`.
    pub disk_capacity_bytes: u64,
    /// `TabletsPendingDeletion`: tablets with a delete in flight to this tserver.
    pub pending_deletes: BTreeSet<String>,
}

/// One replica of a tablet, from the observed locations (`TabletReplica`).
#[derive(Clone, Debug)]
pub struct ReplicaInfo {
    /// The replica reports itself leader.
    pub is_leader: bool,
    /// The tablet peer's state.
    pub state: RaftGroupStatePB,
    /// Its member type in the Raft config.
    pub member_type: PeerMemberType,
    /// The data directory holding it.
    pub fs_data_dir: Arc<str>,
    /// `drive_info.total_size`.
    pub total_size: u64,
    /// The tserver asked not to move it (just split).
    pub should_disable_lb_move: bool,
}

impl Default for ReplicaInfo {
    fn default() -> ReplicaInfo {
        ReplicaInfo {
            is_leader: false,
            state: RaftGroupStatePB::RUNNING,
            member_type: PeerMemberType::VOTER,
            fs_data_dir: Arc::from(""),
            total_size: 0,
            should_disable_lb_move: false,
        }
    }
}

/// A tablet of a balanced table.
#[derive(Clone, Debug, Default)]
pub struct TabletInfo {
    /// The id.
    pub id: String,
    /// The catalog state is RUNNING; only such tablets are balanced.
    pub running: bool,
    /// Replicas by tserver uuid.
    pub replicas: BTreeMap<String, ReplicaInfo>,
    /// The committed config has a PRE_VOTER (`CountVotersInTransition`).
    pub config_in_transition: bool,
    /// Tservers a leader stepdown to failed within `min_leader_stepdown_retry_interval_ms`.
    pub recent_stepdown_failures: BTreeSet<String>,
}

/// The balancer tasks of a table still in flight, by tablet: the tserver added or removed, or the
/// new leader of a stepdown.
#[derive(Clone, Debug, Default)]
pub struct PendingTasks {
    /// ADD_SERVER tasks: the tserver added.
    pub adds: BTreeMap<String, String>,
    /// REMOVE_SERVER tasks: the tserver removed.
    pub removes: BTreeMap<String, String>,
    /// Leader stepdowns: the new leader, empty for any.
    pub stepdowns: BTreeMap<String, String>,
}

impl PendingTasks {
    /// All tasks in flight.
    pub fn len(&self) -> usize {
        self.adds.len() + self.removes.len() + self.stepdowns.len()
    }

    /// No task in flight.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}

/// A table, with its effective replication info (`GetTableReplicationInfoWithDefault`).
#[derive(Clone, Debug, Default)]
pub struct TableInfo {
    /// The id.
    pub id: String,
    /// A transaction status table, whose leaders follow preferred zones only with `transaction_tables_use_preferred_zones`.
    pub is_transaction_status_table: bool,
    /// `SkipLoadBalancing`: a system table, a colocated table other than the parent, or a table
    /// being dropped.
    pub skip: bool,
    /// A YSQL table whose tablespace placement is not known yet.
    pub placement_unknown: bool,
    /// The effective replication info: the table's, its tablespace's or the cluster's.
    pub replication_info: ReplicationInfoPB,
    /// Its tablets, active or not.
    pub tablets: Vec<TabletInfo>,
    /// Its balancer tasks in flight.
    pub pending: PendingTasks,
}

/// What a run looks at.
#[derive(Clone, Debug, Default)]
pub struct ClusterView {
    /// Every registered tserver, live or not (`GetAllDescriptors`).
    pub tservers: Vec<TsInfo>,
    /// The cluster config's live placement uuid.
    pub live_placement_uuid: String,
    /// The read replica placement uuids, in cluster config order.
    pub read_replica_placement_uuids: Vec<String>,
    /// Every table.
    pub tables: Vec<TableInfo>,
}

/// The balancer's flags (`Options` and the flags its code reads), read once per run.
#[derive(Clone, Debug)]
pub struct Options {
    /// Loads closer than this are balanced.
    pub min_load_variance_to_balance: f64,
    /// Whether the remote bootstrap limits apply.
    pub allow_limit_starting_tablets: bool,
    /// `load_balancer_max_concurrent_tablet_remote_bootstraps`; negative for no limit.
    pub max_tablet_remote_bootstraps: i32,
    /// `..._per_table`; negative for no limit.
    pub max_tablet_remote_bootstraps_per_table: i32,
    /// `load_balancer_max_inbound_remote_bootstraps_per_tserver`; negative for no limit.
    pub max_inbound_remote_bootstraps_per_ts: i32,
    /// `load_balancer_min_inbound_remote_bootstraps_per_tserver`.
    pub min_inbound_remote_bootstraps_per_ts: i32,
    /// `remote_bootstrap_rate_limit_bytes_per_sec * catalog_manager_bg_task_wait_ms / 1000`.
    pub max_inbound_bytes_per_ts: u64,
    /// Whether the over-replication limit applies.
    pub allow_limit_over_replicated_tablets: bool,
    /// `load_balancer_max_over_replicated_tablets`; negative for no limit.
    pub max_over_replicated_tablets_per_table: i32,
    /// `load_balancer_max_concurrent_removals`.
    pub max_concurrent_removals: i32,
    /// `load_balancer_max_concurrent_adds`.
    pub max_concurrent_adds: i32,
    /// `load_balancer_max_concurrent_moves`.
    pub max_concurrent_leader_moves: i32,
    /// -1 for `max_concurrent_leader_moves`.
    pub max_concurrent_leader_moves_per_table: i32,
    /// `leader_balance_threshold`; 0 balances leaders evenly.
    pub leader_balance_threshold: i32,
    /// `leader_balance_unresponsive_timeout_ms`.
    pub leader_balance_unresponsive_timeout: Duration,
    /// `allow_leader_balancing_dead_node`.
    pub allow_leader_balancing_dead_node: bool,
    /// `load_balancer_min_free_disk_space_pct`.
    pub min_free_disk_space_pct: u32,
    /// `replication_factor`.
    pub replication_factor: i32,
    /// `transaction_tables_use_preferred_zones`.
    pub transaction_tables_use_preferred_zones: bool,
    /// `enable_global_load_balancing`.
    pub enable_global_load_balancing: bool,
    /// `load_balancer_drive_aware`.
    pub drive_aware: bool,
    /// `load_balancer_ignore_cloud_info_similarity`.
    pub ignore_cloud_info_similarity: bool,
    /// `cluster_balancer_stepdown_to_preferred_leader_on_remove`.
    pub stepdown_to_preferred_leader_on_remove: bool,
    /// `TEST_load_balancer_handle_under_replicated_tablets_only`.
    pub handle_under_replicated_tablets_only: bool,
    /// False only in tests, as `cluster_balance_mocked.h` sets it.
    pub check_ts_liveness: bool,
}

impl Default for Options {
    fn default() -> Options {
        Options {
            min_load_variance_to_balance: 2.0,
            allow_limit_starting_tablets: true,
            max_tablet_remote_bootstraps: -1,
            max_tablet_remote_bootstraps_per_table: -1,
            max_inbound_remote_bootstraps_per_ts: 50,
            min_inbound_remote_bootstraps_per_ts: 4,
            max_inbound_bytes_per_ts: 256 << 20,
            allow_limit_over_replicated_tablets: true,
            max_over_replicated_tablets_per_table: 50,
            max_concurrent_removals: 50,
            max_concurrent_adds: 25,
            max_concurrent_leader_moves: 100,
            max_concurrent_leader_moves_per_table: -1,
            leader_balance_threshold: 0,
            leader_balance_unresponsive_timeout: Duration::from_millis(3000),
            allow_leader_balancing_dead_node: true,
            min_free_disk_space_pct: 5,
            replication_factor: 3,
            transaction_tables_use_preferred_zones: false,
            enable_global_load_balancing: true,
            drive_aware: true,
            ignore_cloud_info_similarity: false,
            stepdown_to_preferred_leader_on_remove: true,
            handle_under_replicated_tablets_only: false,
            check_ts_liveness: true,
        }
    }
}

impl Options {
    /// The `Options` constructor's normalization of "no limit".
    fn normalized(&self) -> Options {
        let unlimited = |v: i32| if v < 0 { i32::MAX } else { v };
        let mut o = self.clone();
        if o.max_concurrent_leader_moves_per_table == -1 {
            o.max_concurrent_leader_moves_per_table = o.max_concurrent_leader_moves;
        }
        o.max_tablet_remote_bootstraps = unlimited(o.max_tablet_remote_bootstraps);
        o.max_over_replicated_tablets_per_table =
            unlimited(o.max_over_replicated_tablets_per_table);
        o.max_tablet_remote_bootstraps_per_table =
            unlimited(o.max_tablet_remote_bootstraps_per_table);
        o.max_inbound_remote_bootstraps_per_ts = unlimited(o.max_inbound_remote_bootstraps_per_ts);
        o
    }
}

/// A change the run decided on, with the reason C++ logs and shows on the balancer page.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Action {
    /// `ScheduleAddServerTask`: ADD_SERVER of `ts_uuid` as `member_type`.
    AddReplica {
        /// The table.
        table_id: String,
        /// The tablet.
        tablet_id: String,
        /// The tserver.
        ts_uuid: String,
        /// For a move, the replica it replaces, removed in a later run; empty for an add.
        from_ts: String,
        /// Its member type in the Raft config.
        member_type: PeerMemberType,
        /// Why, as C++ logs it.
        reason: String,
    },
    /// `ScheduleRemoveServerTask`: REMOVE_SERVER of `ts_uuid`, which is not the leader.
    RemoveReplica {
        /// The table.
        table_id: String,
        /// The tablet.
        tablet_id: String,
        /// The tserver.
        ts_uuid: String,
        /// Why, as C++ logs it.
        reason: String,
    },
    /// `ScheduleTryStepDownTask`: the leader `from_ts` steps down to `to_ts` (any peer if empty),
    /// then, with `also_remove_replica`, is removed.
    MoveLeader {
        /// The table.
        table_id: String,
        /// The tablet.
        tablet_id: String,
        /// The current leader.
        from_ts: String,
        /// The new leader, empty for any peer.
        to_ts: String,
        /// Remove the old leader after the stepdown.
        also_remove_replica: bool,
        /// Why, as C++ logs it.
        reason: String,
    },
}

/// `ClusterBalancerWarningType`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub enum WarningType {
    /// A table was skipped.
    SkipTableLoadBalancing,
    /// A table's removals stopped on an error.
    TableRemoveReplicas,
    /// A table's adds (or, as in C++, leader moves) stopped on an error.
    TableAddReplicas,
    /// Not used, as in C++.
    TableLeaderMoves,
    /// An under-replicated tablet got no replica.
    TabletUnderReplicated,
    /// A wrongly placed replica had no destination.
    TabletWrongPlacement,
}

/// One example and a count per warning type.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct WarningCount {
    /// The first message of the type in the run.
    pub example_message: String,
    /// How many times it occurred.
    pub count: usize,
}

/// `ClusterBalancerActivityInfo` of one placement's run.
#[derive(Clone, Debug, Default)]
pub struct Activity {
    /// The placement balanced: the live cluster's uuid or a read replica cluster's.
    pub placement_uuid: String,
    /// Balancer tasks in flight at the start of the run.
    pub pending_tasks: usize,
    /// The actions decided.
    pub actions: Vec<Action>,
    /// What kept the run from making progress.
    pub warnings: BTreeMap<WarningType, WarningCount>,
    /// Some replica is still starting.
    pub has_ongoing_remote_bootstraps: bool,
}

impl Activity {
    /// No warnings, tasks, actions or remote bootstraps.
    pub fn is_idle(&self) -> bool {
        self.warnings.is_empty()
            && self.pending_tasks == 0
            && self.actions.is_empty()
            && !self.has_ongoing_remote_bootstraps
    }

    fn count_warning(&mut self, warning: WarningType, message: String) {
        tracing::debug!("{message}");
        self.warnings
            .entry(warning)
            .and_modify(|w| w.count += 1)
            .or_insert(WarningCount {
                example_message: message,
                count: 1,
            });
    }
}

/// `PerRunState`: metrics summed over the placements of one run.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RunMetrics {
    /// Tablets with a replica on a blacklisted or wrongly placed tserver.
    pub tablets_in_wrong_placement: u32,
    /// Tablets led from a leader-blacklisted tserver.
    pub blacklisted_leaders: u32,
    /// Replicas to add to reach the optimal distribution.
    pub total_table_load_difference: u32,
    /// Those replicas times their table's average tablet size.
    pub estimated_data_to_balance_bytes: u64,
}

/// What one run (all placements) decided.
#[derive(Clone, Debug, Default)]
pub struct RunResult {
    /// One per placement, live first, as C++ records them in the activity buffer.
    pub activities: Vec<Activity>,
    /// Summed over the placements.
    pub metrics: RunMetrics,
    /// Tables skipped because `TableInfo::skip`.
    pub skipped_tables: Vec<String>,
}

impl RunResult {
    /// Every placement's actions.
    pub fn actions(&self) -> impl Iterator<Item = &Action> {
        self.activities.iter().flat_map(|a| a.actions.iter())
    }
}

/// The balancer across runs: only whether global balancing is allowed survives a run, as in C++.
#[derive(Debug, Default)]
pub struct ClusterBalancer {
    can_perform_global_operations: bool,
}

impl ClusterBalancer {
    /// A balancer that does not balance globally until a run is idle.
    pub fn new() -> ClusterBalancer {
        ClusterBalancer::default()
    }

    /// `RunClusterBalancer`: the live placement, then each read replica placement.
    pub fn run(&mut self, view: &ClusterView, options: &Options) -> RunResult {
        planner::run(self, view, &options.normalized())
    }
}

/// `ClusterBalancerActivityBuffer`: the last `load_balancer_num_idle_runs` placement runs; idle
/// when all are.
#[derive(Debug)]
pub struct ActivityBuffer {
    capacity: usize,
    activities: VecDeque<Activity>,
}

impl ActivityBuffer {
    /// An empty buffer of `capacity` runs.
    pub fn new(capacity: usize) -> ActivityBuffer {
        ActivityBuffer {
            capacity: capacity.max(1),
            activities: VecDeque::new(),
        }
    }

    /// Records one placement run.
    pub fn record(&mut self, activity: Activity) {
        if self.activities.len() == self.capacity {
            self.activities.pop_front();
        }
        self.activities.push_back(activity);
    }

    /// Every recorded run was idle.
    pub fn is_idle(&self) -> bool {
        self.activities.iter().all(Activity::is_idle)
    }

    /// The last placement run.
    pub fn latest(&self) -> Option<&Activity> {
        self.activities.back()
    }

    /// The buffer's capacity follows the flag.
    pub fn set_capacity(&mut self, capacity: usize) {
        self.capacity = capacity.max(1);
        while self.activities.len() > self.capacity {
            self.activities.pop_front();
        }
    }
}

/// The Fx hash (as rustc uses): far cheaper than SipHash for the planner's short string keys,
/// which come from the catalog, not from untrusted input.
#[derive(Clone, Copy, Default)]
pub(crate) struct FxHasher {
    hash: u64,
}

impl FxHasher {
    fn add(&mut self, word: u64) {
        self.hash = (self.hash.rotate_left(5) ^ word).wrapping_mul(0x51_7c_c1_b7_27_22_0a_95);
    }
}

impl std::hash::Hasher for FxHasher {
    fn write(&mut self, bytes: &[u8]) {
        let mut chunks = bytes.chunks_exact(8);
        for chunk in &mut chunks {
            self.add(u64::from_le_bytes(chunk.try_into().unwrap()));
        }
        let rest = chunks.remainder();
        if !rest.is_empty() {
            let mut word = [0u8; 8];
            word[..rest.len()].copy_from_slice(rest);
            self.add(u64::from_le_bytes(word));
        }
    }

    fn write_u8(&mut self, i: u8) {
        self.add(u64::from(i));
    }

    fn write_usize(&mut self, i: usize) {
        self.add(i as u64);
    }

    fn finish(&self) -> u64 {
        self.hash
    }
}

/// A `HashMap` with [`FxHasher`].
pub(crate) type FxHashMap<K, V> =
    std::collections::HashMap<K, V, std::hash::BuildHasherDefault<FxHasher>>;

/// `TSDescriptor::generate_placement_id`: the key placement blocks are compared by.
pub(crate) fn placement_id(ci: &CloudInfoPB) -> String {
    format!(
        "{}:{}:{}",
        ci.placement_cloud(),
        ci.placement_region(),
        ci.placement_zone()
    )
}

/// `CatalogManagerUtil::CloudInfoSimilarity`.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
#[allow(clippy::enum_variant_names)]
pub(crate) enum Similarity {
    NoMatch,
    CloudMatch,
    RegionMatch,
    ZoneMatch,
}

/// `ComputeCloudInfoSimilarity`: a field set in both that differs ends the match.
pub(crate) fn similarity(a: &CloudInfoPB, b: &CloudInfoPB) -> Similarity {
    let differs =
        |x: &Option<String>, y: &Option<String>| matches!((x, y), (Some(x), Some(y)) if x != y);
    if differs(&a.placement_cloud, &b.placement_cloud) {
        Similarity::NoMatch
    } else if differs(&a.placement_region, &b.placement_region) {
        Similarity::CloudMatch
    } else if differs(&a.placement_zone, &b.placement_zone) {
        Similarity::RegionMatch
    } else {
        Similarity::ZoneMatch
    }
}

/// `TSDescriptor::MatchesCloudInfo` (`IsCloudInfoPrefix`).
pub(crate) fn matches_cloud_info(ts: &CloudInfoPB, block: &CloudInfoPB) -> bool {
    similarity(block, ts) == Similarity::ZoneMatch
}
