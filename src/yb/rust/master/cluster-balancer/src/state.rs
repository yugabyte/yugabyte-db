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

//! `GlobalLoadState` and `PerTableLoadState` (`cluster_balance_util.cc`). Sets are ordered as the
//! C++ `std::set`s are; where C++ iterates an unordered map the port iterates in key order, so
//! ties C++ leaves to hash order are broken by uuid or path here.

use std::cmp::Ordering;
use std::collections::{BTreeMap, BTreeSet, HashMap};

use yb_proto::pb::common_net::{CloudInfoPB, PlacementInfoPB};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;

use crate::{matches_cloud_info, placement_id, Activity, Options, ReplicaInfo, TabletInfo, TsInfo};

pub(crate) type Result<T> = std::result::Result<T, String>;

/// Which replicas a placement run balances.
#[derive(Clone, Debug)]
pub(crate) struct RunMode {
    pub read_only: bool,
    /// The placement balanced (the live one in a live run).
    pub placement_uuid: String,
    pub live_placement_uuid: String,
}

/// `CBTabletMetadata`.
#[derive(Clone, Debug, Default)]
pub(crate) struct TabletMeta {
    pub running: i32,
    pub starting: i32,
    pub is_under_replicated: bool,
    /// Blocks below their minimum, by placement id.
    pub under_replicated_placements: BTreeMap<String, CloudInfoPB>,
    /// Blocks above their maximum, by placement id.
    pub over_max_placements: BTreeSet<String>,
    /// Running and starting replicas per block, by placement id.
    pub placement_replica_counts: HashMap<String, usize>,
    pub is_over_replicated: bool,
    pub over_replicated_tablet_servers: BTreeSet<String>,
    pub wrong_placement_tablet_servers: BTreeSet<String>,
    pub removal_pending_tablet_servers: BTreeSet<String>,
    pub blacklisted_tablet_servers: BTreeSet<String>,
    pub leader_blacklisted_tablet_servers: BTreeSet<String>,
    pub leader_uuid: String,
    pub leader_stepdown_failures: BTreeSet<String>,
    /// The largest replica's size.
    pub size: u64,
}

impl TabletMeta {
    pub fn is_missing_replicas(&self) -> bool {
        self.is_under_replicated || !self.under_replicated_placements.is_empty()
    }

    fn has_wrong_placements(&self) -> bool {
        !self.wrong_placement_tablet_servers.is_empty()
            || !self.blacklisted_tablet_servers.is_empty()
    }

    pub fn has_over_max_placements(&self) -> bool {
        !self.over_max_placements.is_empty()
    }

    pub fn num_replicas(&self) -> i32 {
        self.running + self.starting
    }

    /// An unknown size (0) is assumed to be big.
    pub fn size_or_default(&self) -> u64 {
        if self.size == 0 {
            1 << 30
        } else {
            self.size
        }
    }

    pub fn can_add_ts_to_missing_placements(&self, ts: &TsInfo) -> bool {
        self.under_replicated_placements
            .values()
            .any(|ci| matches_cloud_info(&ts.cloud_info, ci))
    }
}

/// `CBTabletServerMetadata`.
#[derive(Clone, Debug, Default)]
pub(crate) struct TsMeta {
    /// Index into `GlobalState::ts`.
    pub desc: usize,
    pub path_to_tablets: BTreeMap<String, BTreeSet<String>>,
    pub path_to_starting_tablets_count: HashMap<String, i32>,
    pub sorted_path_load_by_tablets_count: Vec<String>,
    pub sorted_path_load_by_leader_count: Vec<String>,
    pub running_tablets: BTreeSet<String>,
    pub starting_tablets: BTreeSet<String>,
    pub leaders: BTreeSet<String>,
    pub path_to_leaders: BTreeMap<String, BTreeSet<String>>,
    pub disabled_by_ts_tablets: BTreeSet<String>,
}

/// `CBTabletServerGlobalMetadata`.
#[derive(Clone, Debug, Default)]
pub(crate) struct TsGlobal {
    pub running_tablets_count: i32,
    pub starting_tablets_count: i32,
    pub leaders_count: i32,
    pub starting_tablets_size: u64,
    pub disk_used_bytes: u64,
    pub disk_capacity_bytes: u64,
}

/// `GlobalLoadState`, reset for each placement.
pub(crate) struct GlobalState<'a> {
    pub options: &'a Options,
    pub ts: &'a [TsInfo],
    pub ts_index: HashMap<&'a str, usize>,
    pub total_starting_tablets: i32,
    pub drive_aware: bool,
    pub blacklisted: BTreeSet<String>,
    pub leader_blacklisted: BTreeSet<String>,
    pub activity: Activity,
    pub per_ts: HashMap<String, TsGlobal>,
}

impl<'a> GlobalState<'a> {
    /// `ResetGlobalState` and `SetBlacklistAndPendingDeleteTS`.
    pub fn new(options: &'a Options, ts: &'a [TsInfo]) -> GlobalState<'a> {
        GlobalState {
            options,
            ts,
            ts_index: ts
                .iter()
                .enumerate()
                .map(|(i, t)| (t.uuid.as_str(), i))
                .collect(),
            total_starting_tablets: 0,
            drive_aware: options.drive_aware,
            blacklisted: ts
                .iter()
                .filter(|t| t.blacklisted)
                .map(|t| t.uuid.clone())
                .collect(),
            leader_blacklisted: ts
                .iter()
                .filter(|t| t.leader_blacklisted)
                .map(|t| t.uuid.clone())
                .collect(),
            activity: Activity::default(),
            per_ts: HashMap::new(),
        }
    }

    pub fn ts_info(&self, uuid: &str) -> Option<&'a TsInfo> {
        self.ts_index.get(uuid).map(|i| &self.ts[*i])
    }

    pub fn global_load(&self, uuid: &str) -> i32 {
        self.per_ts
            .get(uuid)
            .map_or(0, |m| m.starting_tablets_count + m.running_tablets_count)
    }

    pub fn global_leader_load(&self, uuid: &str) -> i32 {
        self.per_ts.get(uuid).map_or(0, |m| m.leaders_count)
    }

    pub fn estimated_disk_used_bytes(&self, uuid: &str) -> u64 {
        self.per_ts
            .get(uuid)
            .map_or(0, |m| m.disk_used_bytes + m.starting_tablets_size)
    }

    /// Whether the tserver can take `additional` bytes and keep `min_free_disk_space_pct` free.
    pub fn has_free_disk_space_for(&self, uuid: &str, additional: u64) -> bool {
        let min_free_pct = u64::from(self.options.min_free_disk_space_pct.min(100));
        if min_free_pct == 0 {
            return true;
        }
        let Some(m) = self.per_ts.get(uuid).filter(|m| m.disk_capacity_bytes != 0) else {
            return true;
        };
        let capacity = m.disk_capacity_bytes;
        let max_used = capacity - capacity * min_free_pct / 100;
        m.disk_used_bytes + m.starting_tablets_size + additional <= max_used
    }

    pub fn tservers_low_on_disk(&self) -> Vec<String> {
        let mut low: Vec<String> = self
            .per_ts
            .iter()
            .filter(|(uuid, _)| !self.has_free_disk_space_for(uuid, 0))
            .map(|(uuid, m)| {
                format!(
                    "{uuid} ({} of {} bytes in use)",
                    self.estimated_disk_used_bytes(uuid),
                    m.disk_capacity_bytes
                )
            })
            .collect();
        low.sort();
        low
    }
}

/// `IsReplicaConsensusReady`: running and a VOTER or OBSERVER.
fn is_replica_consensus_ready(
    ts_uuid: &str,
    tablet_id: &str,
    replica: &ReplicaInfo,
) -> Result<bool> {
    use RaftGroupStatePB::*;
    if matches!(
        replica.state,
        NOT_STARTED | BOOTSTRAPPING | FAILED | QUIESCING | SHUTDOWN
    ) {
        return Ok(false);
    }
    // A RUNNING peer still PRE_VOTER has not caught up yet and counts as remote bootstrapping.
    match replica.member_type {
        PeerMemberType::OBSERVER | PeerMemberType::VOTER => Ok(true),
        PeerMemberType::PRE_OBSERVER | PeerMemberType::PRE_VOTER => Ok(false),
        other => Err(format!(
            "Unexpected member type {other:?} for peer {ts_uuid} of tablet {tablet_id}"
        )),
    }
}

/// `CloudInfoContainsCloudInfo`: `lhs`, with its unset fields as wildcards, covers `rhs`.
fn cloud_info_contains(lhs: &CloudInfoPB, rhs: &CloudInfoPB) -> bool {
    if !lhs.has_placement_cloud() {
        return true;
    }
    if lhs.placement_cloud() != rhs.placement_cloud() {
        return false;
    }
    if !lhs.has_placement_region() {
        return true;
    }
    if lhs.placement_region() != rhs.placement_region() {
        return false;
    }
    if !lhs.has_placement_zone() {
        return true;
    }
    lhs.placement_zone() == rhs.placement_zone()
}

/// `PerTableLoadState`.
pub(crate) struct TableState<'a> {
    pub table_id: String,
    pub options: &'a Options,
    pub mode: &'a RunMode,
    pub per_tablet_meta: HashMap<String, TabletMeta>,
    pub per_ts_meta: BTreeMap<String, TsMeta>,
    pub placement: PlacementInfoPB,
    placement_block_max: HashMap<String, usize>,
    pub total_running: i32,
    pub total_starting: i32,
    pub sorted_load: Vec<String>,
    pub tablets_missing_replicas: BTreeSet<String>,
    pub tablets_over_replicated: BTreeSet<String>,
    pub tablets_over_max_placements: BTreeSet<String>,
    pub tablets_wrong_placement: BTreeSet<String>,
    pub tablets_with_badly_placed_leaders: BTreeSet<String>,
    pub tablets_added: BTreeSet<String>,
    /// By leader affinity priority, each sorted by leader load; leader-blacklisted tservers are
    /// in the last priority.
    pub sorted_leader_load: Vec<Vec<String>>,
    pub use_preferred_zones: bool,
    pub allow_only_leader_balancing: bool,
    pub affinitized_zones: Vec<Vec<CloudInfoPB>>,
    pub num_running_tablets: usize,
    pub average_tablet_size: u64,
    initialized: bool,
}

impl<'a> TableState<'a> {
    pub fn new(table_id: &str, options: &'a Options, mode: &'a RunMode) -> TableState<'a> {
        TableState {
            table_id: table_id.to_string(),
            options,
            mode,
            per_tablet_meta: HashMap::new(),
            per_ts_meta: BTreeMap::new(),
            placement: PlacementInfoPB::new(),
            placement_block_max: HashMap::new(),
            total_running: 0,
            total_starting: 0,
            sorted_load: Vec::new(),
            tablets_missing_replicas: BTreeSet::new(),
            tablets_over_replicated: BTreeSet::new(),
            tablets_over_max_placements: BTreeSet::new(),
            tablets_wrong_placement: BTreeSet::new(),
            tablets_with_badly_placed_leaders: BTreeSet::new(),
            tablets_added: BTreeSet::new(),
            sorted_leader_load: Vec::new(),
            use_preferred_zones: true,
            allow_only_leader_balancing: false,
            affinitized_zones: Vec::new(),
            num_running_tablets: 0,
            average_tablet_size: 0,
            initialized: false,
        }
    }

    fn not_initialized(&self, what: &str, tablet_id: &str) -> String {
        format!("PerTableLoadState not initialized before calling {what} for tablet {tablet_id}")
    }

    fn ts_meta(&self, uuid: &str) -> Result<&TsMeta> {
        self.per_ts_meta.get(uuid).ok_or_else(|| {
            format!(
                "Found uninitialized ts_meta: ts_uuid: {uuid}, table_uuid: {}",
                self.table_id
            )
        })
    }

    fn ts_meta_mut(&mut self, uuid: &str) -> Result<&mut TsMeta> {
        let table_id = self.table_id.clone();
        self.per_ts_meta.get_mut(uuid).ok_or_else(|| {
            format!("Found uninitialized ts_meta: ts_uuid: {uuid}, table_uuid: {table_id}")
        })
    }

    pub fn tablet_meta(&self, tablet_id: &str) -> Result<&TabletMeta> {
        self.per_tablet_meta
            .get(tablet_id)
            .ok_or_else(|| format!("No balancer metadata for tablet {tablet_id}"))
    }

    pub fn load(&self, uuid: &str) -> usize {
        self.per_ts_meta
            .get(uuid)
            .map_or(0, |m| m.starting_tablets.len() + m.running_tablets.len())
    }

    pub fn leader_load(&self, uuid: &str) -> usize {
        self.per_ts_meta.get(uuid).map_or(0, |m| m.leaders.len())
    }

    /// Running replicas on the tserver that are over-replicated, so about to be removed.
    pub fn possibly_transient_load(&self, uuid: &str) -> usize {
        self.per_ts_meta.get(uuid).map_or(0, |m| {
            m.running_tablets
                .iter()
                .filter(|t| {
                    self.per_tablet_meta
                        .get(*t)
                        .is_some_and(|tm| tm.is_over_replicated)
                })
                .count()
        })
    }

    /// Replicas on the drive of the tserver that holds the tablet.
    pub fn tablet_drive_load(&self, uuid: &str, tablet_id: &str) -> usize {
        self.per_ts_meta
            .get(uuid)
            .and_then(|m| {
                m.path_to_tablets
                    .values()
                    .find(|tablets| tablets.contains(tablet_id))
                    .map(BTreeSet::len)
            })
            .unwrap_or(0)
    }

    /// `CompareLoad`: table load, then global load, then (for a tablet) the drive's load, then uuid.
    pub fn compare_load(
        &self,
        a: &str,
        b: &str,
        tablet_id: Option<&str>,
        g: &GlobalState,
    ) -> Ordering {
        self.load(a)
            .cmp(&self.load(b))
            .then_with(|| g.global_load(a).cmp(&g.global_load(b)))
            .then_with(|| match tablet_id {
                Some(t) => self
                    .tablet_drive_load(a, t)
                    .cmp(&self.tablet_drive_load(b, t)),
                None => Ordering::Equal,
            })
            .then_with(|| a.cmp(b))
    }

    /// `LeaderLoadComparator`: not leader-blacklisted first, then leader load, global leader load
    /// and uuid.
    fn compare_leader_load(&self, a: &str, b: &str, g: &GlobalState) -> Ordering {
        g.leader_blacklisted
            .contains(a)
            .cmp(&g.leader_blacklisted.contains(b))
            .then_with(|| self.leader_load(a).cmp(&self.leader_load(b)))
            .then_with(|| g.global_leader_load(a).cmp(&g.global_leader_load(b)))
            .then_with(|| a.cmp(b))
    }

    fn is_ts_in_live_placement(&self, ts: &TsInfo) -> bool {
        ts.placement_uuid == self.mode.live_placement_uuid
    }

    /// `ShouldSkipReplica`: a replica of the other replica type, or of another read replica
    /// cluster, or on a tserver no longer known.
    fn should_skip_replica(&self, ts_uuid: &str, g: &GlobalState) -> bool {
        let Some(ts) = g.ts_info(ts_uuid) else {
            return true;
        };
        let live = self.is_ts_in_live_placement(ts);
        if !self.mode.read_only {
            !live
        } else {
            live || ts.placement_uuid != self.mode.placement_uuid
        }
    }

    /// `UpdateTablet`, for a RUNNING tablet.
    pub fn update_tablet(&mut self, tablet: &TabletInfo, g: &mut GlobalState) -> Result<()> {
        let tablet_id = tablet.id.as_str();
        self.per_tablet_meta
            .entry(tablet_id.to_string())
            .or_default();
        let replica_count = tablet
            .replicas
            .keys()
            .filter(|ts| !self.should_skip_replica(ts, g))
            .count();
        for (ts_uuid, replica) in &tablet.replicas {
            let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
            // Even a skipped replica tells whether the tablet has a leader at all.
            if replica.is_leader {
                meta.leader_uuid = ts_uuid.clone();
            }
            meta.size = meta.size.max(replica.total_size);
            if self.should_skip_replica(ts_uuid, g) {
                continue;
            }
            let Some(ts_meta) = self.per_ts_meta.get(ts_uuid) else {
                return Err(format!(
                    "TServer {ts_uuid} has a replica of tablet {tablet_id} but is not in the \
                     master's tserver list (replaced, or registered after this load-balancer run \
                     started)"
                ));
            };
            // A replica on a dead tserver is only balanced off if the tserver is blacklisted.
            if self.options.check_ts_liveness
                && !g.ts[ts_meta.desc].live_and_reported
                && !g.blacklisted.contains(ts_uuid)
            {
                if !self.options.allow_leader_balancing_dead_node {
                    return Err(format!(
                        "Master leader has not yet received heartbeat from ts {ts_uuid}. \
                         Aborting load balancing."
                    ));
                }
                self.allow_only_leader_balancing = true;
            }
            if replica.is_leader {
                self.add_leader_tablet(tablet_id, ts_uuid, &replica.fs_data_dir, g)?;
            }
            if is_replica_consensus_ready(ts_uuid, tablet_id, replica)? {
                self.add_running_tablet(tablet_id, ts_uuid, &replica.fs_data_dir, g)?;
            } else {
                // Usually a remote bootstrap; SHUTDOWN and QUIESCING peers land here too.
                self.add_starting_tablet(tablet_id, ts_uuid, g)?;
                *self
                    .ts_meta_mut(ts_uuid)?
                    .path_to_starting_tablets_count
                    .entry(replica.fs_data_dir.clone())
                    .or_default() += 1;
                g.activity.has_ongoing_remote_bootstraps = true;
            }
            if replica.should_disable_lb_move {
                self.ts_meta_mut(ts_uuid)?
                    .disabled_by_ts_tablets
                    .insert(tablet_id.to_string());
            }
            let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
            if g.blacklisted.contains(ts_uuid) {
                meta.blacklisted_tablet_servers.insert(ts_uuid.clone());
            }
            if replica.is_leader && g.leader_blacklisted.contains(ts_uuid) {
                meta.leader_blacklisted_tablet_servers
                    .insert(ts_uuid.clone());
            }
        }

        let num_replicas = self.placement.num_replicas();
        {
            let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
            meta.is_over_replicated = (num_replicas as i64) < replica_count as i64;
            meta.is_under_replicated = (num_replicas as i64) > replica_count as i64;
        }

        if self.placement.placement_blocks.is_empty() {
            // Without placement every replica is a removal candidate of an over-replicated tablet.
            let candidates: Vec<String> = tablet
                .replicas
                .keys()
                .filter(|ts| !self.should_skip_replica(ts, g))
                .cloned()
                .collect();
            let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
            if meta.is_over_replicated {
                meta.over_replicated_tablet_servers.extend(candidates);
            }
        } else {
            let mut placement_to_replicas: BTreeMap<String, (CloudInfoPB, Vec<String>)> =
                BTreeMap::new();
            let mut placement_to_min: HashMap<String, usize> = HashMap::new();
            for block in &self.placement.placement_blocks {
                let id = placement_id(&block.cloud_info);
                placement_to_replicas
                    .entry(id.clone())
                    .or_insert_with(|| (block.cloud_info.clone().unwrap_or_default(), Vec::new()));
                placement_to_min.insert(id, block.min_num_replicas().max(0) as usize);
            }
            let mut wrong = Vec::new();
            for ts_uuid in tablet.replicas.keys() {
                if self.should_skip_replica(ts_uuid, g) {
                    continue;
                }
                match self.valid_placement(ts_uuid, g) {
                    Some(ci) => placement_to_replicas
                        .entry(placement_id(&ci))
                        .or_insert_with(|| (ci, Vec::new()))
                        .1
                        .push(ts_uuid.clone()),
                    // The config or schema changed: the replica should no longer live here.
                    None => wrong.push(ts_uuid.clone()),
                }
            }
            let maxima: HashMap<String, usize> = placement_to_replicas
                .keys()
                .map(|id| (id.clone(), self.placement_block_max_replicas(id)))
                .collect();
            let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
            meta.wrong_placement_tablet_servers.extend(wrong);
            for (id, (_, replicas)) in &placement_to_replicas {
                meta.placement_replica_counts
                    .insert(id.clone(), replicas.len());
            }
            for (id, (ci, replicas)) in &placement_to_replicas {
                let min = placement_to_min.get(id).copied().unwrap_or(0);
                if min > replicas.len() {
                    // Blocks below their minimum are repaired first.
                    meta.under_replicated_placements
                        .insert(id.clone(), ci.clone());
                } else if replicas.len() > maxima[id] {
                    meta.over_max_placements.insert(id.clone());
                }
            }
            // Removal candidates: the replicas of a block above its maximum if there is one, else
            // those of every block above its minimum.
            if meta.is_over_replicated {
                let any_over_max = meta.has_over_max_placements();
                for (id, (_, replicas)) in &placement_to_replicas {
                    let candidate = if any_over_max {
                        meta.over_max_placements.contains(id)
                    } else {
                        replicas.len() > placement_to_min.get(id).copied().unwrap_or(0)
                    };
                    if candidate {
                        meta.over_replicated_tablet_servers
                            .extend(replicas.iter().cloned());
                    }
                }
            }
        }
        let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
        meta.leader_stepdown_failures = tablet.recent_stepdown_failures.clone();
        let (missing, over, over_max, wrong, badly_placed_leader) = (
            meta.is_missing_replicas(),
            meta.is_over_replicated,
            meta.has_over_max_placements(),
            meta.has_wrong_placements(),
            !meta.leader_blacklisted_tablet_servers.is_empty(),
        );
        if missing {
            self.tablets_missing_replicas.insert(tablet_id.to_string());
        }
        if over {
            self.tablets_over_replicated.insert(tablet_id.to_string());
        }
        // A maximum violation is repaired by an add-before-remove move only when neither the
        // missing-replica nor the over-replication path owns the tablet.
        if over_max && !over && !missing {
            self.tablets_over_max_placements
                .insert(tablet_id.to_string());
        }
        if wrong {
            self.tablets_wrong_placement.insert(tablet_id.to_string());
        }
        if badly_placed_leader {
            self.tablets_with_badly_placed_leaders
                .insert(tablet_id.to_string());
        }
        Ok(())
    }

    /// `UpdateTabletServer`.
    pub fn update_tablet_server(&mut self, desc: usize, g: &mut GlobalState) {
        let ts = &g.ts[desc];
        let uuid = ts.uuid.clone();
        self.per_ts_meta.entry(uuid.clone()).or_default().desc = desc;
        g.per_ts.entry(uuid.clone()).or_insert_with(|| TsGlobal {
            disk_used_bytes: ts.disk_used_bytes,
            disk_capacity_bytes: ts.disk_capacity_bytes,
            ..TsGlobal::default()
        });
        let is_blacklisted = g.blacklisted.contains(&uuid);
        let is_leader_blacklisted = g.leader_blacklisted.contains(&uuid);
        if self.options.check_ts_liveness && !ts.live_and_reported {
            return;
        }
        let live = self.is_ts_in_live_placement(ts);
        if !self.mode.read_only {
            if !live {
                return;
            }
        } else {
            if live {
                return;
            }
            if ts.placement_uuid.is_empty() {
                tracing::warn!("Read only ts {uuid} has empty placement uuid");
            } else if ts.placement_uuid != self.mode.placement_uuid {
                return;
            }
        }
        self.sorted_load.push(uuid.clone());
        if self.mode.read_only {
            return;
        }
        // Leaders go only to live, non-blacklisted tservers that heartbeat recently enough.
        if !is_blacklisted && ts.heartbeat_age < self.options.leader_balance_unresponsive_timeout {
            let mut priority = 0;
            if is_leader_blacklisted {
                priority = self.affinitized_zones.len();
            } else if !self.affinitized_zones.is_empty() {
                while priority < self.affinitized_zones.len()
                    && !self.affinitized_zones[priority]
                        .iter()
                        .any(|zone| cloud_info_contains(zone, &ts.cloud_info))
                {
                    priority += 1;
                }
            }
            if self.sorted_leader_load.len() <= priority {
                self.sorted_leader_load.resize(priority + 1, Vec::new());
            }
            self.sorted_leader_load[priority].push(uuid);
        }
    }

    /// `CanAddTabletToTabletServer`. `from_ts` is the source of a move, or empty: a move within
    /// one block may go to a block at its maximum, since the remove that follows restores it.
    pub fn can_add_tablet_to_tablet_server(
        &self,
        tablet_id: &str,
        to_ts: &str,
        from_ts: &str,
        g: &GlobalState,
    ) -> Result<bool> {
        let Some(ts_meta) = self.per_ts_meta.get(to_ts) else {
            return Ok(false);
        };
        if self.options.check_ts_liveness && !g.ts[ts_meta.desc].live_and_reported {
            return Ok(false);
        }
        if self.tablets_added.contains(tablet_id) {
            return Ok(false);
        }
        if g.blacklisted.contains(to_ts) {
            return Ok(false);
        }
        if ts_meta.running_tablets.contains(tablet_id)
            || ts_meta.starting_tablets.contains(tablet_id)
        {
            return Ok(false);
        }
        let tablet_meta = self
            .per_tablet_meta
            .get(tablet_id)
            .ok_or_else(|| format!("No balancer metadata for tablet {tablet_id}"))?;
        if tablet_meta.removal_pending_tablet_servers.contains(to_ts) {
            return Ok(false);
        }
        let to_placement = self.valid_placement(to_ts, g);
        if !self.placement.placement_blocks.is_empty() && to_placement.is_none() {
            return Ok(false);
        }
        if let Some(to_placement) = &to_placement {
            let to_id = placement_id(to_placement);
            let mut projected = tablet_meta
                .placement_replica_counts
                .get(&to_id)
                .copied()
                .unwrap_or(0);
            // The source is only removed once the add over-replicates the tablet, which an
            // under-replicated tablet never reaches.
            if !from_ts.is_empty() && !tablet_meta.is_under_replicated {
                if let Some(from_placement) = self.valid_placement(from_ts, g) {
                    if placement_id(&from_placement) == to_id {
                        projected = projected.saturating_sub(1);
                    }
                }
            }
            if projected >= self.placement_block_max_replicas(&to_id) {
                return Ok(false);
            }
        }
        let global = g.per_ts.get(to_ts).cloned().unwrap_or_default();
        if global.starting_tablets_count >= self.options.max_inbound_remote_bootstraps_per_ts {
            return Ok(false);
        }
        // Enough starting tablets for parallelism and enough bytes to last until the next run.
        if global.starting_tablets_count >= self.options.min_inbound_remote_bootstraps_per_ts
            && tablet_meta.size_or_default() + global.starting_tablets_size
                > self.options.max_inbound_bytes_per_ts
        {
            return Ok(false);
        }
        if !g.has_free_disk_space_for(to_ts, tablet_meta.size_or_default()) {
            return Ok(false);
        }
        if g.ts[ts_meta.desc].pending_deletes.contains(tablet_id) {
            tracing::info!(
                "tablet server {to_ts} has a pending delete for tablet {tablet_id}. Not allowing \
                 it to take another replica."
            );
            return Ok(false);
        }
        Ok(true)
    }

    /// `CachePlacementBlockMaxReplicas`.
    pub fn cache_placement_block_max_replicas(&mut self) {
        let num_replicas = self.placement.num_replicas();
        self.placement_block_max = self
            .placement
            .placement_blocks
            .iter()
            .map(|b| {
                let max = if b.has_max_num_replicas() {
                    b.max_num_replicas()
                } else {
                    num_replicas
                };
                (placement_id(&b.cloud_info), max.max(0) as usize)
            })
            .collect();
    }

    /// The effective maximum of a block, by placement id; without a block, the replication factor.
    pub fn placement_block_max_replicas(&self, id: &str) -> usize {
        self.placement_block_max
            .get(id)
            .copied()
            .unwrap_or(self.placement.num_replicas().max(0) as usize)
    }

    /// `GetValidPlacement`: the first block the tserver matches; without blocks, its own cloud
    /// info.
    pub fn valid_placement(&self, uuid: &str, g: &GlobalState) -> Option<CloudInfoPB> {
        let ts = self
            .per_ts_meta
            .get(uuid)
            .map(|m| &g.ts[m.desc])
            .or_else(|| g.ts_info(uuid))?;
        if self.placement.placement_blocks.is_empty() {
            return Some(ts.cloud_info.clone());
        }
        self.placement
            .placement_blocks
            .iter()
            .find(|b| matches_cloud_info(&ts.cloud_info, &b.cloud_info))
            .map(|b| b.cloud_info.clone().unwrap_or_default())
    }

    /// `CanSelectWrongPlacementReplicaToMove`: a (from, to) pair moving a replica off a blacklisted
    /// tserver (preferring a destination in the same placement), else off a wrongly placed one.
    pub fn select_wrong_placement_replica_to_move(
        &self,
        tablet_id: &str,
        g: &GlobalState,
    ) -> Result<Option<(String, String)>> {
        let meta = self.tablet_meta(tablet_id)?;
        let mut fallback: Option<(String, String)> = None;
        for from_uuid in &meta.blacklisted_tablet_servers {
            let invalid_placement = meta.wrong_placement_tablet_servers.contains(from_uuid);
            for to_uuid in &self.sorted_load {
                let can_add =
                    self.can_add_tablet_to_tablet_server(tablet_id, to_uuid, from_uuid, g)?;
                let found = if invalid_placement && can_add {
                    // A wrongly placed source's placement need not be kept.
                    true
                } else if can_add {
                    let from_ci = self.valid_placement(from_uuid, g);
                    let to_ci = self.valid_placement(to_uuid, g);
                    match (from_ci, to_ci) {
                        (Some(f), Some(t)) if placement_id(&f) == placement_id(&t) => true,
                        _ => {
                            // Another placement still works as a fallback; the last pair wins.
                            fallback = Some((from_uuid.clone(), to_uuid.clone()));
                            false
                        }
                    }
                } else {
                    false
                };
                if found {
                    return Ok(Some((from_uuid.clone(), to_uuid.clone())));
                }
            }
            if fallback.is_none() {
                let from_placement = self.valid_placement(from_uuid, g);
                let blocked_by_max = meta.is_under_replicated
                    && from_placement.as_ref().is_some_and(|p| {
                        let id = placement_id(p);
                        meta.placement_replica_counts.get(&id).copied().unwrap_or(0)
                            >= self.placement_block_max_replicas(&id)
                    });
                tracing::info!(
                    "Cannot move tablet {tablet_id} from blacklisted tablet server {from_uuid}: \
                     no eligible destination tablet servers{}",
                    if blocked_by_max {
                        ". Its placement is at its max_num_replicas and the tablet is \
                         under-replicated; add tservers where its missing replicas can go"
                    } else {
                        ""
                    }
                );
            }
        }
        if fallback.is_some() {
            return Ok(fallback);
        }
        if let Some(from_uuid) = meta.wrong_placement_tablet_servers.iter().next() {
            for to_uuid in &self.sorted_load {
                if self.can_add_tablet_to_tablet_server(tablet_id, to_uuid, from_uuid, g)? {
                    return Ok(Some((from_uuid.clone(), to_uuid.clone())));
                }
            }
        }
        Ok(None)
    }

    pub fn set_initialized(&mut self) {
        self.initialized = true;
    }

    /// `AddReplica`.
    pub fn add_replica(&mut self, tablet_id: &str, to_ts: &str, g: &mut GlobalState) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("AddReplica", tablet_id));
        }
        self.add_starting_tablet(tablet_id, to_ts, g)?;
        self.tablets_added.insert(tablet_id.to_string());
        self.sort_load(g);
        Ok(())
    }

    /// `RemoveReplica`.
    pub fn remove_replica(
        &mut self,
        tablet_id: &str,
        from_ts: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("RemoveReplica", tablet_id));
        }
        self.remove_running_tablet(tablet_id, from_ts, g)?;
        if self.ts_meta(from_ts)?.starting_tablets.contains(tablet_id) {
            tracing::error!(
                "Invalid request: remove starting tablet {tablet_id} from ts {from_ts}"
            );
        }
        let meta = self
            .per_tablet_meta
            .entry(tablet_id.to_string())
            .or_default();
        meta.removal_pending_tablet_servers
            .insert(from_ts.to_string());
        if meta.leader_uuid == from_ts {
            self.move_leader(tablet_id, from_ts, "", "", g)?;
        }
        // Only one replica of a tablet is removed per run.
        self.tablets_over_replicated.remove(tablet_id);
        self.per_tablet_meta
            .get_mut(tablet_id)
            .unwrap()
            .is_over_replicated = false;
        self.tablets_wrong_placement.remove(tablet_id);
        self.sort_load(g);
        Ok(())
    }

    /// `SortLoad`.
    pub fn sort_load(&mut self, g: &GlobalState) {
        let mut sorted = std::mem::take(&mut self.sorted_load);
        sorted.sort_by(|a, b| self.compare_load(a, b, None, g));
        self.sorted_load = sorted;
        if g.drive_aware {
            self.sort_drive_load();
        }
    }

    /// `SortDriveLoad`: each tserver's paths by decreasing replica count; the empty path (a
    /// tserver that reported no path) last.
    fn sort_drive_load(&mut self) {
        for ts in self.sorted_load.clone() {
            let Some(m) = self.per_ts_meta.get_mut(&ts) else {
                continue;
            };
            let mut loads: Vec<(String, usize)> = Vec::new();
            let mut empty_path_found = false;
            for (path, running) in &m.path_to_tablets {
                if path.is_empty() {
                    empty_path_found = true;
                    continue;
                }
                let starting = m
                    .path_to_starting_tablets_count
                    .get(path)
                    .copied()
                    .unwrap_or(0);
                loads.push((path.clone(), running.len() + starting.max(0) as usize));
            }
            loads.sort_by(|a, b| b.1.cmp(&a.1));
            m.sorted_path_load_by_tablets_count = loads.into_iter().map(|(p, _)| p).collect();
            if empty_path_found {
                m.sorted_path_load_by_tablets_count.push(String::new());
            }
        }
    }

    /// `MoveLeader`: `to_ts` empty for a stepdown to any peer.
    pub fn move_leader(
        &mut self,
        tablet_id: &str,
        from_ts: &str,
        to_ts: &str,
        to_ts_path: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("MoveLeader", tablet_id));
        }
        let meta = self
            .per_tablet_meta
            .entry(tablet_id.to_string())
            .or_default();
        if meta.leader_uuid != from_ts {
            return Err(format!(
                "Tablet {tablet_id} has leader {}, but {from_ts} expected.",
                meta.leader_uuid
            ));
        }
        meta.leader_uuid = to_ts.to_string();
        self.remove_leader_tablet(tablet_id, from_ts, g)?;
        if !to_ts.is_empty() {
            self.add_leader_tablet(tablet_id, to_ts, to_ts_path, g)?;
        }
        self.sort_leader_load(g);
        Ok(())
    }

    /// `SortLeaderLoad`.
    pub fn sort_leader_load(&mut self, g: &GlobalState) {
        let mut sets = std::mem::take(&mut self.sorted_leader_load);
        for set in &mut sets {
            set.sort_by(|a, b| self.compare_leader_load(a, b, g));
        }
        self.sorted_leader_load = sets;
        if g.drive_aware {
            self.sort_drive_leader_load();
        }
    }

    /// `SortDriveLeaderLoad`: each tserver's paths by increasing leader count, the empty path
    /// first when the tserver reported one or has no leaders.
    fn sort_drive_leader_load(&mut self) {
        let tservers: Vec<String> = self.sorted_leader_load.iter().flatten().cloned().collect();
        for ts in tservers {
            let Some(m) = self.per_ts_meta.get_mut(&ts) else {
                continue;
            };
            let mut loads: Vec<(String, usize)> = Vec::new();
            let mut empty_path_found = false;
            for (path, leaders) in &m.path_to_leaders {
                if path.is_empty() {
                    empty_path_found = true;
                    continue;
                }
                loads.push((path.clone(), leaders.len()));
            }
            for path in m.path_to_tablets.keys() {
                if !path.is_empty() && !m.path_to_leaders.contains_key(path) {
                    loads.push((path.clone(), 0));
                }
            }
            loads.sort_by_key(|l| l.1);
            m.sorted_path_load_by_leader_count.clear();
            if empty_path_found || m.path_to_leaders.is_empty() {
                m.sorted_path_load_by_leader_count.push(String::new());
            }
            m.sorted_path_load_by_leader_count
                .extend(loads.into_iter().map(|(p, _)| p));
        }
    }

    /// `AdjustLeaderBalanceThreshold`: a threshold below tablets / tservers cannot be met.
    pub fn adjust_leader_balance_threshold(&self, zone_set_size: usize) -> usize {
        let threshold = self.options.leader_balance_threshold.max(0) as usize;
        if threshold == 0 {
            return 0;
        }
        let min = if zone_set_size == 0 {
            0
        } else {
            self.per_tablet_meta.len().div_ceil(zone_set_size)
        };
        if threshold < min {
            tracing::warn!(
                "leader_balance_threshold flag is set to {threshold} but is too low for the \
                 current configuration. Adjusting it to {min}."
            );
            return min;
        }
        threshold
    }

    fn add_running_tablet(
        &mut self,
        tablet_id: &str,
        uuid: &str,
        path: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        if self.initialized {
            return Err(format!(
                "AddRunningTablet called after PerTableLoadState initialized for tablet {tablet_id}"
            ));
        }
        let m = self.ts_meta_mut(uuid)?;
        let inserted = m.running_tablets.insert(tablet_id.to_string());
        m.path_to_tablets
            .entry(path.to_string())
            .or_default()
            .insert(tablet_id.to_string());
        if inserted {
            g.per_ts
                .entry(uuid.to_string())
                .or_default()
                .running_tablets_count += 1;
            self.total_running += 1;
            self.per_tablet_meta
                .entry(tablet_id.to_string())
                .or_default()
                .running += 1;
        }
        Ok(())
    }

    fn remove_running_tablet(
        &mut self,
        tablet_id: &str,
        uuid: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        let m = self.ts_meta_mut(uuid)?;
        if !m.running_tablets.remove(tablet_id) {
            return Err(format!(
                "Could not find running tablet to remove: ts_uuid: {uuid}, tablet_id: {tablet_id}"
            ));
        }
        // C++ stops at the first path that does not hold the tablet (an inverted check), so the
        // tablet can stay listed on its path; it is dropped from every path here.
        for tablets in m.path_to_tablets.values_mut() {
            tablets.remove(tablet_id);
        }
        g.per_ts
            .entry(uuid.to_string())
            .or_default()
            .running_tablets_count -= 1;
        self.total_running -= 1;
        self.per_tablet_meta
            .entry(tablet_id.to_string())
            .or_default()
            .running -= 1;
        Ok(())
    }

    /// `AddStartingTablet`. After initialization an add over-replicates the tablet, unless it was
    /// missing replicas.
    pub fn add_starting_tablet(
        &mut self,
        tablet_id: &str,
        uuid: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        let inserted = self
            .ts_meta_mut(uuid)?
            .starting_tablets
            .insert(tablet_id.to_string());
        if !inserted {
            return Ok(());
        }
        let size = self
            .per_tablet_meta
            .entry(tablet_id.to_string())
            .or_default()
            .size_or_default();
        let global = g.per_ts.entry(uuid.to_string()).or_default();
        global.starting_tablets_count += 1;
        global.starting_tablets_size += size;
        self.total_starting += 1;
        g.total_starting_tablets += 1;
        let missing = self.tablets_missing_replicas.contains(tablet_id);
        let meta = self.per_tablet_meta.get_mut(tablet_id).unwrap();
        meta.starting += 1;
        if self.initialized && !missing {
            meta.is_over_replicated = true;
            self.tablets_over_replicated.insert(tablet_id.to_string());
        }
        Ok(())
    }

    fn add_leader_tablet(
        &mut self,
        tablet_id: &str,
        uuid: &str,
        path: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        let m = self.ts_meta_mut(uuid)?;
        let inserted = m.leaders.insert(tablet_id.to_string());
        m.path_to_leaders
            .entry(path.to_string())
            .or_default()
            .insert(tablet_id.to_string());
        if inserted {
            g.per_ts.entry(uuid.to_string()).or_default().leaders_count += 1;
        }
        Ok(())
    }

    fn remove_leader_tablet(
        &mut self,
        tablet_id: &str,
        uuid: &str,
        g: &mut GlobalState,
    ) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("RemoveLeaderTablet", tablet_id));
        }
        if self.ts_meta_mut(uuid)?.leaders.remove(tablet_id) {
            g.per_ts.entry(uuid.to_string()).or_default().leaders_count -= 1;
        }
        Ok(())
    }
}
