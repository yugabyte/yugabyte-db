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

//! `GlobalLoadState` and `PerTableLoadState` (`cluster_balance_util.cc`).
//!
//! Tservers and tablets are indices: [`Ts`] into the tservers sorted by uuid, [`Tab`] into a
//! table's tablets sorted by id. Index order is id order, so the sets iterate as the C++
//! `std::set`s of ids do and ties broken by id are broken by index. Where C++ iterates an unordered
//! map the port iterates in id order, so ties C++ leaves to hash order are broken by id here.

use std::cmp::Ordering;
use std::collections::{BTreeMap, BTreeSet};
use std::sync::Arc;

use yb_proto::pb::common_net::{CloudInfoPB, PlacementInfoPB};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;

use crate::{
    matches_cloud_info, placement_id, Activity, FxHashMap, Options, ReplicaInfo, TableInfo,
    TabletInfo, TsInfo,
};

pub(crate) type Result<T> = std::result::Result<T, String>;

/// A tserver: an index into [`GlobalState::ts`].
pub(crate) type Ts = u32;
/// A tablet: an index into its [`TableState::tablets`].
pub(crate) type Tab = u32;
/// A placement block: the index of the first block of the table's placement with its cloud info.
pub(crate) type Block = usize;

/// The leader of a tablet on a tserver the view does not know.
pub(crate) const UNKNOWN_TS: Ts = Ts::MAX;

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
    pub under_replicated_placements: BTreeSet<Block>,
    pub over_max_placements: BTreeSet<Block>,
    /// Running and starting replicas per block.
    pub placement_replica_counts: FxHashMap<Block, usize>,
    pub is_over_replicated: bool,
    pub over_replicated_tablet_servers: BTreeSet<Ts>,
    pub wrong_placement_tablet_servers: BTreeSet<Ts>,
    pub removal_pending_tablet_servers: BTreeSet<Ts>,
    pub blacklisted_tablet_servers: BTreeSet<Ts>,
    pub leader_blacklisted_tablet_servers: BTreeSet<Ts>,
    /// `leader_uuid`: none, a tserver, or [`UNKNOWN_TS`].
    pub leader: Option<Ts>,
    pub leader_stepdown_failures: BTreeSet<Ts>,
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

    pub fn count_in(&self, block: Block) -> usize {
        self.placement_replica_counts
            .get(&block)
            .copied()
            .unwrap_or(0)
    }
}

/// `CBTabletServerMetadata`.
#[derive(Clone, Debug, Default)]
pub(crate) struct TsMeta {
    /// `GetValidPlacement`, computed once per table.
    pub placement: Option<Block>,
    pub path_to_tablets: BTreeMap<Arc<str>, BTreeSet<Tab>>,
    pub path_to_starting_tablets_count: FxHashMap<Arc<str>, i32>,
    pub sorted_path_load_by_tablets_count: Vec<Arc<str>>,
    pub sorted_path_load_by_leader_count: Vec<Arc<str>>,
    pub running_tablets: BTreeSet<Tab>,
    pub starting_tablets: BTreeSet<Tab>,
    pub leaders: BTreeSet<Tab>,
    pub path_to_leaders: BTreeMap<Arc<str>, BTreeSet<Tab>>,
    pub disabled_by_ts_tablets: BTreeSet<Tab>,
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
    /// Sorted by uuid.
    pub ts: Vec<&'a TsInfo>,
    pub ts_index: FxHashMap<&'a str, Ts>,
    pub total_starting_tablets: i32,
    pub drive_aware: bool,
    pub blacklisted: Vec<bool>,
    pub leader_blacklisted: Vec<bool>,
    pub activity: Activity,
    pub per_ts: Vec<TsGlobal>,
}

impl<'a> GlobalState<'a> {
    /// `ResetGlobalState` and `SetBlacklistAndPendingDeleteTS`.
    pub fn new(options: &'a Options, tservers: &'a [TsInfo]) -> GlobalState<'a> {
        let mut ts: Vec<&TsInfo> = tservers.iter().collect();
        ts.sort_by(|a, b| a.uuid.cmp(&b.uuid));
        ts.dedup_by(|a, b| a.uuid == b.uuid);
        GlobalState {
            options,
            ts_index: ts
                .iter()
                .enumerate()
                .map(|(i, t)| (t.uuid.as_str(), i as Ts))
                .collect(),
            total_starting_tablets: 0,
            drive_aware: options.drive_aware,
            blacklisted: ts.iter().map(|t| t.blacklisted).collect(),
            leader_blacklisted: ts.iter().map(|t| t.leader_blacklisted).collect(),
            activity: Activity::default(),
            per_ts: ts
                .iter()
                .map(|t| TsGlobal {
                    disk_used_bytes: t.disk_used_bytes,
                    disk_capacity_bytes: t.disk_capacity_bytes,
                    ..TsGlobal::default()
                })
                .collect(),
            ts,
        }
    }

    pub fn index(&self, uuid: &str) -> Option<Ts> {
        self.ts_index.get(uuid).copied()
    }

    pub fn uuid(&self, ts: Ts) -> &'a str {
        &self.ts[ts as usize].uuid
    }

    pub fn info(&self, ts: Ts) -> Option<&'a TsInfo> {
        self.ts.get(ts as usize).copied()
    }

    pub fn is_blacklisted(&self, ts: Ts) -> bool {
        self.blacklisted.get(ts as usize).copied().unwrap_or(false)
    }

    pub fn is_leader_blacklisted(&self, ts: Ts) -> bool {
        self.leader_blacklisted
            .get(ts as usize)
            .copied()
            .unwrap_or(false)
    }

    pub fn global_load(&self, ts: Ts) -> i32 {
        let m = &self.per_ts[ts as usize];
        m.starting_tablets_count + m.running_tablets_count
    }

    pub fn global_leader_load(&self, ts: Ts) -> i32 {
        self.per_ts[ts as usize].leaders_count
    }

    pub fn estimated_disk_used_bytes(&self, ts: Ts) -> u64 {
        let m = &self.per_ts[ts as usize];
        m.disk_used_bytes + m.starting_tablets_size
    }

    /// Whether the tserver can take `additional` bytes and keep `min_free_disk_space_pct` free.
    pub fn has_free_disk_space_for(&self, ts: Ts, additional: u64) -> bool {
        let min_free_pct = u64::from(self.options.min_free_disk_space_pct.min(100));
        if min_free_pct == 0 {
            return true;
        }
        let m = &self.per_ts[ts as usize];
        if m.disk_capacity_bytes == 0 {
            return true;
        }
        let capacity = m.disk_capacity_bytes;
        let max_used = capacity - capacity * min_free_pct / 100;
        m.disk_used_bytes + m.starting_tablets_size + additional <= max_used
    }

    pub fn tservers_low_on_disk(&self) -> Vec<String> {
        (0..self.ts.len() as Ts)
            .filter(|ts| !self.has_free_disk_space_for(*ts, 0))
            .map(|ts| {
                format!(
                    "{} ({} of {} bytes in use)",
                    self.uuid(ts),
                    self.estimated_disk_used_bytes(ts),
                    self.per_ts[ts as usize].disk_capacity_bytes
                )
            })
            .collect()
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

/// A placement block, as `PerTableLoadState` keys its maps by cloud info: blocks with the same
/// cloud info are one, with the last one's minimum and maximum (map assignment order).
#[derive(Clone, Debug)]
pub(crate) struct BlockInfo {
    pub ci: CloudInfoPB,
    pub min: usize,
    pub max: usize,
}

/// `PerTableLoadState`.
pub(crate) struct TableState<'a> {
    pub table_id: &'a str,
    pub options: &'a Options,
    pub mode: &'a RunMode,
    /// The table's tablets, by [`Tab`].
    pub tablets: Vec<&'a TabletInfo>,
    pub tablet_index: FxHashMap<&'a str, Tab>,
    pub per_tablet_meta: Vec<Option<TabletMeta>>,
    /// By [`Ts`], for every tserver.
    pub per_ts_meta: Vec<TsMeta>,
    pub placement: PlacementInfoPB,
    /// By [`Block`]; a block whose cloud info an earlier block has is not used.
    pub blocks: Vec<BlockInfo>,
    /// Each block's [`Block`].
    block_of: Vec<Block>,
    pub total_running: i32,
    pub total_starting: i32,
    pub sorted_load: Vec<Ts>,
    pub tablets_missing_replicas: BTreeSet<Tab>,
    pub tablets_over_replicated: BTreeSet<Tab>,
    pub tablets_over_max_placements: BTreeSet<Tab>,
    pub tablets_wrong_placement: BTreeSet<Tab>,
    pub tablets_with_badly_placed_leaders: BTreeSet<Tab>,
    pub tablets_added: BTreeSet<Tab>,
    /// By leader affinity priority, each sorted by leader load; leader-blacklisted tservers are
    /// in the last priority.
    pub sorted_leader_load: Vec<Vec<Ts>>,
    pub use_preferred_zones: bool,
    pub allow_only_leader_balancing: bool,
    pub affinitized_zones: Vec<Vec<CloudInfoPB>>,
    pub num_running_tablets: usize,
    pub average_tablet_size: u64,
    initialized: bool,
}

impl<'a> TableState<'a> {
    pub fn new(
        table: &'a TableInfo,
        options: &'a Options,
        mode: &'a RunMode,
        num_ts: usize,
    ) -> TableState<'a> {
        let mut tablets: Vec<&TabletInfo> = table.tablets.iter().collect();
        tablets.sort_by(|a, b| a.id.cmp(&b.id));
        tablets.dedup_by(|a, b| a.id == b.id);
        TableState {
            table_id: &table.id,
            options,
            mode,
            tablet_index: tablets
                .iter()
                .enumerate()
                .map(|(i, t)| (t.id.as_str(), i as Tab))
                .collect(),
            per_tablet_meta: vec![None; tablets.len()],
            tablets,
            per_ts_meta: vec![TsMeta::default(); num_ts],
            placement: PlacementInfoPB::new(),
            blocks: Vec::new(),
            block_of: Vec::new(),
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

    pub fn tablet_id(&self, tablet: Tab) -> &'a str {
        &self.tablets[tablet as usize].id
    }

    pub fn tablet_index(&self, id: &str) -> Option<Tab> {
        self.tablet_index.get(id).copied()
    }

    fn not_initialized(&self, what: &str, tablet: Tab) -> String {
        format!(
            "PerTableLoadState not initialized before calling {what} for tablet {}",
            self.tablet_id(tablet)
        )
    }

    pub fn tablet_meta(&self, tablet: Tab) -> Result<&TabletMeta> {
        self.per_tablet_meta[tablet as usize]
            .as_ref()
            .ok_or_else(|| format!("No balancer metadata for tablet {}", self.tablet_id(tablet)))
    }

    /// `per_tablet_meta_[tablet_id]`: the entry, made if missing.
    pub fn meta_mut(&mut self, tablet: Tab) -> &mut TabletMeta {
        self.per_tablet_meta[tablet as usize].get_or_insert_with(TabletMeta::default)
    }

    pub fn ts_meta(&self, ts: Ts) -> &TsMeta {
        &self.per_ts_meta[ts as usize]
    }

    pub fn load(&self, ts: Ts) -> usize {
        let m = self.ts_meta(ts);
        m.starting_tablets.len() + m.running_tablets.len()
    }

    pub fn leader_load(&self, ts: Ts) -> usize {
        self.ts_meta(ts).leaders.len()
    }

    /// Running replicas on the tserver that are over-replicated, so about to be removed.
    pub fn possibly_transient_load(&self, ts: Ts) -> usize {
        self.ts_meta(ts)
            .running_tablets
            .iter()
            .filter(|t| {
                self.per_tablet_meta[**t as usize]
                    .as_ref()
                    .is_some_and(|m| m.is_over_replicated)
            })
            .count()
    }

    /// Replicas on the drive of the tserver that holds the tablet.
    pub fn tablet_drive_load(&self, ts: Ts, tablet: Tab) -> usize {
        self.ts_meta(ts)
            .path_to_tablets
            .values()
            .find(|tablets| tablets.contains(&tablet))
            .map(BTreeSet::len)
            .unwrap_or(0)
    }

    /// `CompareLoad`: table load, then global load, then (for a tablet) the drive's load, then uuid.
    pub fn compare_load(&self, a: Ts, b: Ts, tablet: Option<Tab>, g: &GlobalState) -> Ordering {
        self.load(a)
            .cmp(&self.load(b))
            .then_with(|| g.global_load(a).cmp(&g.global_load(b)))
            .then_with(|| match tablet {
                Some(t) => self
                    .tablet_drive_load(a, t)
                    .cmp(&self.tablet_drive_load(b, t)),
                None => Ordering::Equal,
            })
            .then_with(|| a.cmp(&b))
    }

    /// `LeaderLoadComparator`: not leader-blacklisted first, then leader load, global leader load
    /// and uuid.
    fn compare_leader_load(&self, a: Ts, b: Ts, g: &GlobalState) -> Ordering {
        g.is_leader_blacklisted(a)
            .cmp(&g.is_leader_blacklisted(b))
            .then_with(|| self.leader_load(a).cmp(&self.leader_load(b)))
            .then_with(|| g.global_leader_load(a).cmp(&g.global_leader_load(b)))
            .then_with(|| a.cmp(&b))
    }

    fn is_ts_in_live_placement(&self, ts: &TsInfo) -> bool {
        ts.placement_uuid == self.mode.live_placement_uuid
    }

    /// `ShouldSkipReplica`: a replica of the other replica type, or of another read replica
    /// cluster, or on a tserver no longer known.
    fn should_skip_replica(&self, ts: Option<Ts>, g: &GlobalState) -> bool {
        let Some(ts) = ts.and_then(|ts| g.info(ts)) else {
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
    pub fn update_tablet(&mut self, tablet: Tab, g: &mut GlobalState) -> Result<()> {
        let info = self.tablets[tablet as usize];
        let tablet_id = info.id.as_str();
        self.meta_mut(tablet);
        let replicas: Vec<(&str, Option<Ts>, &ReplicaInfo)> = info
            .replicas
            .iter()
            .map(|(uuid, r)| (uuid.as_str(), g.index(uuid), r))
            .collect();
        let replica_count = replicas
            .iter()
            .filter(|(_, ts, _)| !self.should_skip_replica(*ts, g))
            .count();
        for &(uuid, ts, replica) in &replicas {
            let meta = self.meta_mut(tablet);
            // Even a skipped replica tells whether the tablet has a leader at all.
            if replica.is_leader {
                meta.leader = Some(ts.unwrap_or(UNKNOWN_TS));
            }
            meta.size = meta.size.max(replica.total_size);
            if self.should_skip_replica(ts, g) {
                continue;
            }
            let ts = ts.expect("a known tserver");
            // A replica on a dead tserver is only balanced off if the tserver is blacklisted.
            if self.options.check_ts_liveness
                && !g.ts[ts as usize].live_and_reported
                && !g.is_blacklisted(ts)
            {
                if !self.options.allow_leader_balancing_dead_node {
                    return Err(format!(
                        "Master leader has not yet received heartbeat from ts {uuid}. Aborting \
                         load balancing."
                    ));
                }
                self.allow_only_leader_balancing = true;
            }
            if replica.is_leader {
                self.add_leader_tablet(tablet, ts, &replica.fs_data_dir, g);
            }
            if is_replica_consensus_ready(uuid, tablet_id, replica)? {
                self.add_running_tablet(tablet, ts, &replica.fs_data_dir, g)?;
            } else {
                // Usually a remote bootstrap; SHUTDOWN and QUIESCING peers land here too.
                self.add_starting_tablet(tablet, ts, g);
                *self.per_ts_meta[ts as usize]
                    .path_to_starting_tablets_count
                    .entry(replica.fs_data_dir.clone())
                    .or_default() += 1;
                g.activity.has_ongoing_remote_bootstraps = true;
            }
            if replica.should_disable_lb_move {
                self.per_ts_meta[ts as usize]
                    .disabled_by_ts_tablets
                    .insert(tablet);
            }
            let meta = self.meta_mut(tablet);
            if g.is_blacklisted(ts) {
                meta.blacklisted_tablet_servers.insert(ts);
            }
            if replica.is_leader && g.is_leader_blacklisted(ts) {
                meta.leader_blacklisted_tablet_servers.insert(ts);
            }
        }

        let num_replicas = self.placement.num_replicas() as i64;
        let mut by_block: BTreeMap<Block, Vec<Ts>> = (0..self.blocks.len())
            .filter(|b| self.block_of[*b] == *b)
            .map(|b| (b, Vec::new()))
            .collect();
        let mut wrong = Vec::new();
        for &(_, ts, _) in &replicas {
            if self.should_skip_replica(ts, g) {
                continue;
            }
            let ts = ts.expect("a known tserver");
            match self.per_ts_meta[ts as usize].placement {
                Some(block) => by_block.entry(block).or_default().push(ts),
                // The config or schema changed: the replica should no longer live here.
                None => wrong.push(ts),
            }
        }
        let meta = self.per_tablet_meta[tablet as usize].as_mut().unwrap();
        meta.is_over_replicated = num_replicas < replica_count as i64;
        meta.is_under_replicated = num_replicas > replica_count as i64;
        meta.wrong_placement_tablet_servers.extend(wrong);
        for (block, replicas) in &by_block {
            meta.placement_replica_counts.insert(*block, replicas.len());
        }
        for (block, replicas) in &by_block {
            let info = &self.blocks[*block];
            if info.min > replicas.len() {
                // Blocks below their minimum are repaired first.
                meta.under_replicated_placements.insert(*block);
            } else if replicas.len() > info.max {
                meta.over_max_placements.insert(*block);
            }
        }
        // Removal candidates: the replicas of a block above its maximum if there is one, else
        // those of every block above its minimum.
        if meta.is_over_replicated {
            let any_over_max = meta.has_over_max_placements();
            for (block, replicas) in &by_block {
                let candidate = if any_over_max {
                    meta.over_max_placements.contains(block)
                } else {
                    replicas.len() > self.blocks[*block].min
                };
                if candidate {
                    meta.over_replicated_tablet_servers
                        .extend(replicas.iter().copied());
                }
            }
        }
        meta.leader_stepdown_failures = info
            .recent_stepdown_failures
            .iter()
            .filter_map(|u| g.index(u))
            .collect();
        let (missing, over, over_max, wrong, badly_placed_leader) = (
            meta.is_missing_replicas(),
            meta.is_over_replicated,
            meta.has_over_max_placements(),
            meta.has_wrong_placements(),
            !meta.leader_blacklisted_tablet_servers.is_empty(),
        );
        if missing {
            self.tablets_missing_replicas.insert(tablet);
        }
        if over {
            self.tablets_over_replicated.insert(tablet);
        }
        // A maximum violation is repaired by an add-before-remove move only when neither the
        // missing-replica nor the over-replication path owns the tablet.
        if over_max && !over && !missing {
            self.tablets_over_max_placements.insert(tablet);
        }
        if wrong {
            self.tablets_wrong_placement.insert(tablet);
        }
        if badly_placed_leader {
            self.tablets_with_badly_placed_leaders.insert(tablet);
        }
        Ok(())
    }

    /// Sets the table's placement: `CachePlacementBlockMaxReplicas`, and the blocks by cloud info.
    pub fn set_placement(&mut self, placement: PlacementInfoPB) {
        let num_replicas = placement.num_replicas();
        let mut by_id: FxHashMap<String, Block> = FxHashMap::default();
        self.blocks.clear();
        self.block_of.clear();
        for (i, b) in placement.placement_blocks.iter().enumerate() {
            let max = if b.has_max_num_replicas() {
                b.max_num_replicas()
            } else {
                num_replicas
            };
            let info = BlockInfo {
                ci: b.cloud_info.clone().unwrap_or_default(),
                min: b.min_num_replicas().max(0) as usize,
                max: max.max(0) as usize,
            };
            let block = *by_id.entry(placement_id(&b.cloud_info)).or_insert(i);
            // The later block's minimum and maximum win, as map assignment does in C++.
            if block != i {
                self.blocks[block].min = info.min;
                self.blocks[block].max = info.max;
            }
            self.blocks.push(info);
            self.block_of.push(block);
        }
        self.placement = placement;
    }

    /// `GetValidPlacement` of a tserver: the first block it matches.
    fn placement_of(&self, ts: &TsInfo) -> Option<Block> {
        self.placement
            .placement_blocks
            .iter()
            .position(|b| matches_cloud_info(&ts.cloud_info, &b.cloud_info))
            .map(|i| self.block_of[i])
    }

    /// `GetValidPlacement`, as cached for the table.
    pub fn valid_placement(&self, ts: Ts) -> Option<Block> {
        self.per_ts_meta[ts as usize].placement
    }

    /// `PlacementBlockMaxReplicas`.
    pub fn block_max(&self, block: Block) -> usize {
        self.blocks[block].max
    }

    /// `UpdateTabletServer`.
    pub fn update_tablet_server(&mut self, ts: Ts, g: &mut GlobalState) {
        let info = g.ts[ts as usize];
        self.per_ts_meta[ts as usize].placement = self.placement_of(info);
        if self.options.check_ts_liveness && !info.live_and_reported {
            return;
        }
        let live = self.is_ts_in_live_placement(info);
        if !self.mode.read_only {
            if !live {
                return;
            }
        } else {
            if live {
                return;
            }
            if info.placement_uuid.is_empty() {
                tracing::warn!("Read only ts {} has empty placement uuid", info.uuid);
            } else if info.placement_uuid != self.mode.placement_uuid {
                return;
            }
        }
        self.sorted_load.push(ts);
        if self.mode.read_only {
            return;
        }
        // Leaders go only to live, non-blacklisted tservers that heartbeat recently enough.
        if !g.is_blacklisted(ts)
            && info.heartbeat_age < self.options.leader_balance_unresponsive_timeout
        {
            let mut priority = 0;
            if g.is_leader_blacklisted(ts) {
                priority = self.affinitized_zones.len();
            } else if !self.affinitized_zones.is_empty() {
                while priority < self.affinitized_zones.len()
                    && !self.affinitized_zones[priority]
                        .iter()
                        .any(|zone| cloud_info_contains(zone, &info.cloud_info))
                {
                    priority += 1;
                }
            }
            if self.sorted_leader_load.len() <= priority {
                self.sorted_leader_load.resize(priority + 1, Vec::new());
            }
            self.sorted_leader_load[priority].push(ts);
        }
    }

    /// `CanAddTabletToTabletServer`. `from` is the source of a move: a move within one block may go
    /// to a block at its maximum, since the remove that follows restores it.
    pub fn can_add_tablet_to_tablet_server(
        &self,
        tablet: Tab,
        to: Ts,
        from: Option<Ts>,
        g: &GlobalState,
    ) -> Result<bool> {
        let ts_meta = self.ts_meta(to);
        if self.options.check_ts_liveness && !g.ts[to as usize].live_and_reported {
            return Ok(false);
        }
        if self.tablets_added.contains(&tablet) {
            return Ok(false);
        }
        if g.is_blacklisted(to) {
            return Ok(false);
        }
        if ts_meta.running_tablets.contains(&tablet) || ts_meta.starting_tablets.contains(&tablet) {
            return Ok(false);
        }
        let tablet_meta = self.tablet_meta(tablet)?;
        if tablet_meta.removal_pending_tablet_servers.contains(&to) {
            return Ok(false);
        }
        let Some(to_block) = self.valid_placement(to) else {
            return Ok(false);
        };
        let mut projected = tablet_meta.count_in(to_block);
        // The source is only removed once the add over-replicates the tablet, which an
        // under-replicated tablet never reaches.
        if let Some(from) = from {
            if !tablet_meta.is_under_replicated && self.valid_placement(from) == Some(to_block) {
                projected = projected.saturating_sub(1);
            }
        }
        if projected >= self.block_max(to_block) {
            return Ok(false);
        }
        let global = &g.per_ts[to as usize];
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
        if !g.has_free_disk_space_for(to, tablet_meta.size_or_default()) {
            return Ok(false);
        }
        let tablet_id = self.tablet_id(tablet);
        if g.ts[to as usize].pending_deletes.contains(tablet_id) {
            tracing::info!(
                "tablet server {} has a pending delete for tablet {tablet_id}. Not allowing it to \
                 take another replica.",
                g.uuid(to)
            );
            return Ok(false);
        }
        Ok(true)
    }

    /// `CanSelectWrongPlacementReplicaToMove`: a (from, to) pair moving a replica off a blacklisted
    /// tserver (preferring a destination in the same placement), else off a wrongly placed one.
    pub fn select_wrong_placement_replica_to_move(
        &self,
        tablet: Tab,
        g: &GlobalState,
    ) -> Result<Option<(Ts, Ts)>> {
        let meta = self.tablet_meta(tablet)?;
        let mut fallback: Option<(Ts, Ts)> = None;
        for &from in &meta.blacklisted_tablet_servers {
            let invalid_placement = meta.wrong_placement_tablet_servers.contains(&from);
            for &to in &self.sorted_load {
                let can_add = self.can_add_tablet_to_tablet_server(tablet, to, Some(from), g)?;
                let found = if invalid_placement && can_add {
                    // A wrongly placed source's placement need not be kept.
                    true
                } else if can_add {
                    match (self.valid_placement(from), self.valid_placement(to)) {
                        (Some(f), Some(t)) if f == t => true,
                        _ => {
                            // Another placement still works as a fallback; the last pair wins.
                            fallback = Some((from, to));
                            false
                        }
                    }
                } else {
                    false
                };
                if found {
                    return Ok(Some((from, to)));
                }
            }
            if fallback.is_none() {
                let blocked_by_max = meta.is_under_replicated
                    && self
                        .valid_placement(from)
                        .is_some_and(|b| meta.count_in(b) >= self.block_max(b));
                tracing::info!(
                    "Cannot move tablet {} from blacklisted tablet server {}: no eligible \
                     destination tablet servers{}",
                    self.tablet_id(tablet),
                    g.uuid(from),
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
        if let Some(&from) = meta.wrong_placement_tablet_servers.iter().next() {
            for &to in &self.sorted_load {
                if self.can_add_tablet_to_tablet_server(tablet, to, Some(from), g)? {
                    return Ok(Some((from, to)));
                }
            }
        }
        Ok(None)
    }

    pub fn set_initialized(&mut self) {
        self.initialized = true;
    }

    /// `AddReplica`.
    pub fn add_replica(&mut self, tablet: Tab, to: Ts, g: &mut GlobalState) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("AddReplica", tablet));
        }
        self.add_starting_tablet(tablet, to, g);
        self.tablets_added.insert(tablet);
        self.sort_load(g);
        Ok(())
    }

    /// `RemoveReplica`.
    pub fn remove_replica(&mut self, tablet: Tab, from: Ts, g: &mut GlobalState) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("RemoveReplica", tablet));
        }
        self.remove_running_tablet(tablet, from, g)?;
        if self.ts_meta(from).starting_tablets.contains(&tablet) {
            tracing::error!(
                "Invalid request: remove starting tablet {} from ts {}",
                self.tablet_id(tablet),
                g.uuid(from)
            );
        }
        let meta = self.meta_mut(tablet);
        meta.removal_pending_tablet_servers.insert(from);
        if meta.leader == Some(from) {
            self.move_leader(tablet, from, None, None, g)?;
        }
        // Only one replica of a tablet is removed per run.
        self.tablets_over_replicated.remove(&tablet);
        self.meta_mut(tablet).is_over_replicated = false;
        self.tablets_wrong_placement.remove(&tablet);
        self.sort_load(g);
        Ok(())
    }

    /// `SortLoad`.
    pub fn sort_load(&mut self, g: &GlobalState) {
        let mut sorted = std::mem::take(&mut self.sorted_load);
        sorted.sort_by(|a, b| self.compare_load(*a, *b, None, g));
        self.sorted_load = sorted;
        if g.drive_aware {
            self.sort_drive_load();
        }
    }

    /// `SortDriveLoad`: each tserver's paths by decreasing replica count; the empty path (a
    /// tserver that reported no path) last.
    fn sort_drive_load(&mut self) {
        for i in 0..self.sorted_load.len() {
            let m = &mut self.per_ts_meta[self.sorted_load[i] as usize];
            let mut loads: Vec<(Arc<str>, usize)> = Vec::new();
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
            m.sorted_path_load_by_tablets_count.clear();
            m.sorted_path_load_by_tablets_count
                .extend(loads.into_iter().map(|(p, _)| p));
            if empty_path_found {
                m.sorted_path_load_by_tablets_count.push(Arc::from(""));
            }
        }
    }

    /// `MoveLeader`: `to` none for a stepdown to any peer.
    pub fn move_leader(
        &mut self,
        tablet: Tab,
        from: Ts,
        to: Option<Ts>,
        to_path: Option<&Arc<str>>,
        g: &mut GlobalState,
    ) -> Result<()> {
        if !self.initialized {
            return Err(self.not_initialized("MoveLeader", tablet));
        }
        let tablet_id = self.tablet_id(tablet);
        let meta = self.meta_mut(tablet);
        if meta.leader != Some(from) {
            return Err(format!(
                "Tablet {tablet_id} has leader {}, but {} expected.",
                match meta.leader {
                    None => "",
                    Some(UNKNOWN_TS) => "<unknown>",
                    Some(l) => g.uuid(l),
                },
                g.uuid(from)
            ));
        }
        meta.leader = to;
        self.remove_leader_tablet(tablet, from, g);
        if let Some(to) = to {
            let empty: Arc<str> = Arc::from("");
            self.add_leader_tablet(tablet, to, to_path.unwrap_or(&empty), g);
        }
        self.sort_leader_load(g);
        Ok(())
    }

    /// `SortLeaderLoad`.
    pub fn sort_leader_load(&mut self, g: &GlobalState) {
        let mut sets = std::mem::take(&mut self.sorted_leader_load);
        for set in &mut sets {
            set.sort_by(|a, b| self.compare_leader_load(*a, *b, g));
        }
        self.sorted_leader_load = sets;
        if g.drive_aware {
            self.sort_drive_leader_load();
        }
    }

    /// `SortDriveLeaderLoad`: each tserver's paths by increasing leader count, the empty path
    /// first when the tserver reported one or has no leaders.
    fn sort_drive_leader_load(&mut self) {
        for p in 0..self.sorted_leader_load.len() {
            for i in 0..self.sorted_leader_load[p].len() {
                let m = &mut self.per_ts_meta[self.sorted_leader_load[p][i] as usize];
                let mut loads: Vec<(Arc<str>, usize)> = Vec::new();
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
                    m.sorted_path_load_by_leader_count.push(Arc::from(""));
                }
                m.sorted_path_load_by_leader_count
                    .extend(loads.into_iter().map(|(p, _)| p));
            }
        }
    }

    /// `AdjustLeaderBalanceThreshold`: a threshold below tablets / tservers cannot be met.
    pub fn adjust_leader_balance_threshold(&self, zone_set_size: usize) -> usize {
        let threshold = self.options.leader_balance_threshold.max(0) as usize;
        if threshold == 0 {
            return 0;
        }
        let tablets = self.per_tablet_meta.iter().filter(|m| m.is_some()).count();
        let min = if zone_set_size == 0 {
            0
        } else {
            tablets.div_ceil(zone_set_size)
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
        tablet: Tab,
        ts: Ts,
        path: &Arc<str>,
        g: &mut GlobalState,
    ) -> Result<()> {
        if self.initialized {
            return Err(format!(
                "AddRunningTablet called after PerTableLoadState initialized for tablet {}",
                self.tablet_id(tablet)
            ));
        }
        let m = &mut self.per_ts_meta[ts as usize];
        let inserted = m.running_tablets.insert(tablet);
        match m.path_to_tablets.get_mut(path) {
            Some(tablets) => {
                tablets.insert(tablet);
            }
            None => {
                m.path_to_tablets
                    .insert(path.clone(), BTreeSet::from([tablet]));
            }
        }
        if inserted {
            g.per_ts[ts as usize].running_tablets_count += 1;
            self.total_running += 1;
            self.meta_mut(tablet).running += 1;
        }
        Ok(())
    }

    fn remove_running_tablet(&mut self, tablet: Tab, ts: Ts, g: &mut GlobalState) -> Result<()> {
        let m = &mut self.per_ts_meta[ts as usize];
        if !m.running_tablets.remove(&tablet) {
            return Err(format!(
                "Could not find running tablet to remove: ts_uuid: {}, tablet_id: {}",
                g.uuid(ts),
                self.tablet_id(tablet)
            ));
        }
        // C++ stops at the first path that does not hold the tablet (an inverted check), so the
        // tablet can stay listed on its path; it is dropped from every path here.
        for tablets in m.path_to_tablets.values_mut() {
            tablets.remove(&tablet);
        }
        g.per_ts[ts as usize].running_tablets_count -= 1;
        self.total_running -= 1;
        self.meta_mut(tablet).running -= 1;
        Ok(())
    }

    /// `AddStartingTablet`. After initialization an add over-replicates the tablet, unless it was
    /// missing replicas.
    pub fn add_starting_tablet(&mut self, tablet: Tab, ts: Ts, g: &mut GlobalState) {
        if !self.per_ts_meta[ts as usize]
            .starting_tablets
            .insert(tablet)
        {
            return;
        }
        let size = self.meta_mut(tablet).size_or_default();
        let global = &mut g.per_ts[ts as usize];
        global.starting_tablets_count += 1;
        global.starting_tablets_size += size;
        self.total_starting += 1;
        g.total_starting_tablets += 1;
        let missing = self.tablets_missing_replicas.contains(&tablet);
        let initialized = self.initialized;
        let meta = self.meta_mut(tablet);
        meta.starting += 1;
        if initialized && !missing {
            meta.is_over_replicated = true;
            self.tablets_over_replicated.insert(tablet);
        }
    }

    fn add_leader_tablet(&mut self, tablet: Tab, ts: Ts, path: &Arc<str>, g: &mut GlobalState) {
        let m = &mut self.per_ts_meta[ts as usize];
        let inserted = m.leaders.insert(tablet);
        match m.path_to_leaders.get_mut(path) {
            Some(leaders) => {
                leaders.insert(tablet);
            }
            None => {
                m.path_to_leaders
                    .insert(path.clone(), BTreeSet::from([tablet]));
            }
        }
        if inserted {
            g.per_ts[ts as usize].leaders_count += 1;
        }
    }

    fn remove_leader_tablet(&mut self, tablet: Tab, ts: Ts, g: &mut GlobalState) {
        if self.per_ts_meta[ts as usize].leaders.remove(&tablet) {
            g.per_ts[ts as usize].leaders_count -= 1;
        }
    }
}
