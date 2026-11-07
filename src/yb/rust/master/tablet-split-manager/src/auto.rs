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

//! Automatic splitting: `TabletSplitManager::DoSplitting` and
//! `CatalogManager::ShouldSplitValidCandidate`, as a pure pass over the catalog snapshot, the
//! reported replicas and the live tservers. The pass picks new splits; splits already registered
//! are finished by the `tablet-splits` controller and only count against the limits here.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::{is_inactive_state, CatalogSnapshot};
use master_table_lifecycle::table_replication_info;
use master_tablet_locations::{DriveInfo, TabletReplicas};
use master_ts_manager::placement::is_cloud_info_prefix;
use tokio::time::Instant;
use yb_proto::pb::catalog_entity_info::{
    sys_tablets_entry_pb, SysClusterConfigEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_net::{CloudInfoPB, PlacementInfoPB, ReplicationInfoPB};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;

use crate::hex;

/// Flags of automatic splitting, read on every pass.
#[derive(Clone, Copy, Debug)]
pub struct AutoSplitOptions {
    /// `enable_automatic_tablet_splitting`.
    pub enabled: fn() -> bool,
    /// `process_split_tablet_candidates_interval_msec`.
    pub interval: fn() -> Duration,
    /// `outstanding_tablet_split_limit`; 0 for no limit.
    pub outstanding_limit: fn() -> u64,
    /// `outstanding_tablet_split_limit_per_tserver`; 0 for no limit, negative for one derived
    /// from the CPU count.
    pub outstanding_limit_per_tserver: fn() -> i64,
    /// `tablet_split_limit_per_table`; 0 for no limit.
    pub split_limit_per_table: fn() -> u64,
    /// `tablet_split_low_phase_shard_count_per_node`.
    pub low_phase_shard_count_per_node: fn() -> i64,
    /// `tablet_split_high_phase_shard_count_per_node`.
    pub high_phase_shard_count_per_node: fn() -> i64,
    /// `tablet_split_low_phase_size_threshold_bytes`.
    pub low_phase_size_threshold_bytes: fn() -> i64,
    /// `tablet_split_high_phase_size_threshold_bytes`.
    pub high_phase_size_threshold_bytes: fn() -> i64,
    /// `tablet_force_split_threshold_bytes`.
    pub force_split_threshold_bytes: fn() -> i64,
    /// `sort_automatic_tablet_splitting_candidates`.
    pub sort_candidates: fn() -> bool,
    /// `tablet_split_min_size_ratio`.
    pub min_size_ratio: fn() -> f64,
    /// `replication_factor`.
    pub replication_factor: fn() -> i32,
    /// `max_create_tablets_per_ts`.
    pub max_create_tablets_per_ts: fn() -> i32,
    /// `prevent_split_for_ttl_tables_for_seconds`; zero for no recheck delay.
    pub prevent_split_for_ttl_tables: fn() -> Duration,
    /// `prevent_split_for_small_key_range_tablets_for_seconds`; zero for no recheck delay.
    pub prevent_split_for_small_key_range_tablets: fn() -> Duration,
}

impl Default for AutoSplitOptions {
    fn default() -> AutoSplitOptions {
        AutoSplitOptions {
            enabled: || false,
            interval: || Duration::ZERO,
            outstanding_limit: || 0,
            outstanding_limit_per_tserver: || -1,
            split_limit_per_table: || 0,
            low_phase_shard_count_per_node: || 1,
            high_phase_shard_count_per_node: || 24,
            low_phase_size_threshold_bytes: || 128 << 20,
            high_phase_size_threshold_bytes: || 10 << 30,
            force_split_threshold_bytes: || 100 << 30,
            sort_candidates: || true,
            min_size_ratio: || 0.8,
            replication_factor: || 3,
            max_create_tablets_per_ts: || 50,
            prevent_split_for_ttl_tables: || Duration::from_secs(86400),
            prevent_split_for_small_key_range_tablets: || Duration::from_secs(300),
        }
    }
}

impl AutoSplitOptions {
    /// `GetOutstandingTabletSplitLimitPerTserver`.
    fn limit_per_tserver(&self) -> usize {
        let flag = (self.outstanding_limit_per_tserver)();
        if flag >= 0 {
            return flag as usize;
        }
        // `NumEffectiveCPUs`: the standard library honors cgroup CPU quotas.
        let cpus = std::thread::available_parallelism().map_or(1, |n| n.get());
        if cpus <= 4 {
            1
        } else {
            2
        }
    }
}

/// `SplitPhase`: how sharded the table already is decides how large a tablet must grow.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(crate) enum SplitPhase {
    Low,
    High,
    Final,
}

impl SplitPhase {
    fn threshold(self, options: &AutoSplitOptions) -> i64 {
        match self {
            SplitPhase::Low => (options.low_phase_size_threshold_bytes)(),
            SplitPhase::High => (options.high_phase_size_threshold_bytes)(),
            SplitPhase::Final => (options.force_split_threshold_bytes)(),
        }
    }

    fn flag(self) -> &'static str {
        match self {
            SplitPhase::Low => "tablet_split_low_phase_size_threshold_bytes",
            SplitPhase::High => "tablet_split_high_phase_size_threshold_bytes",
            SplitPhase::Final => "tablet_force_split_threshold_bytes",
        }
    }
}

/// Tables, tablets and features automatic splitting is held off for, until a time. Kept across
/// terms, as C++ keeps them in the master's `TabletSplitManager`.
#[derive(Debug, Default)]
pub(crate) struct Disabled {
    /// `splitting_disabled_until_`: by feature (`DisableTabletSplitting`).
    pub features: HashMap<String, Instant>,
    /// `disable_splitting_for_ttl_table_until_`.
    pub ttl_tables: HashMap<Vec<u8>, Instant>,
    /// `disable_splitting_for_small_key_range_tablet_until_`.
    pub small_key_range_tablets: HashMap<Vec<u8>, Instant>,
}

/// `ValidateAgainstDisabledList`: an expired entry is dropped.
fn check_disabled(
    map: &mut HashMap<Vec<u8>, Instant>,
    id: &[u8],
    now: Instant,
) -> Result<(), String> {
    match map.get(id) {
        None => Ok(()),
        Some(until) if *until <= now => {
            map.remove(id);
            Ok(())
        }
        Some(until) => Err(format!(
            "Table/tablet is ignored for splitting for another {:?}. Id: {}",
            *until - now,
            String::from_utf8_lossy(id)
        )),
    }
}

impl Disabled {
    /// The feature splitting is disabled for now, if any.
    pub fn feature(&self, now: Instant) -> Option<(&str, Instant)> {
        self.features
            .iter()
            .find(|(_, until)| now <= **until)
            .map(|(f, until)| (f.as_str(), *until))
    }
}

/// The tservers a pass needs.
#[derive(Debug, Default)]
pub(crate) struct Cluster {
    /// Live tservers, blacklisted or not (`CanAddPartitionsToTable`).
    pub num_live: usize,
    /// The cloud info of the live tservers that are not blacklisted.
    pub valid: Vec<CloudInfoPB>,
    /// Tservers whose metrics ask not to split tables with a default TTL.
    pub ttl_split_disabled: HashSet<Vec<u8>>,
    /// The cluster config.
    pub config: SysClusterConfigEntryPB,
}

/// What a pass decided.
#[derive(Debug, Default)]
pub(crate) struct Plan {
    /// New splits, in the order they are to be started, with the leader's SST size.
    pub schedule: Vec<(Vec<u8>, u64)>,
    /// `tablet_split_candidates`: tablets eligible for a split this pass.
    pub candidates: usize,
    /// `outstanding_tablet_splits`: splits in flight, restarting, compacting or just scheduled.
    pub outstanding: usize,
}

/// `GetNumReplicasOrGlobalReplicationFactor`.
fn num_replicas(placement: &PlacementInfoPB, replication_factor: i32) -> usize {
    if placement.num_replicas() > 0 {
        placement.num_replicas() as usize
    } else {
        replication_factor.max(0) as usize
    }
}

/// `CatalogManagerUtil::GetReplicationFactor`.
fn replication_factor(info: &ReplicationInfoPB, options: &AutoSplitOptions) -> usize {
    match info.live_replicas.as_ref() {
        Some(live) => num_replicas(live, (options.replication_factor)()),
        None => (options.replication_factor)().max(0) as usize,
    }
}

/// `CanAddPartitionsToTable(NumPartitions() + 1)` and `tablet_split_limit_per_table`: limits a
/// manual split is held to as well.
pub(crate) fn check_partition_limits(
    snapshot: &CatalogSnapshot,
    table: &Entity,
    table_pb: &SysTablesEntryPB,
    num_live: usize,
    options: &AutoSplitOptions,
) -> Result<(), String> {
    let partitions = snapshot.active_tablets_of(&table.key.id).len();
    let info = table_replication_info(
        table_pb.replication_info.as_ref(),
        &cluster_config(snapshot),
    );
    let max_tablets = (options.max_create_tablets_per_ts)().max(0) as usize * num_live;
    let replicas_per_tablet = num_replicas(
        info.live_replicas.as_ref().unwrap_or_default(),
        (options.replication_factor)(),
    );
    if replicas_per_tablet > 1 && max_tablets > 0 && partitions + 1 > max_tablets {
        return Err(format!(
            "Cannot create more tablets, table: {}: The requested number of tablets ({}) is over \
             the permitted maximum ({max_tablets})",
            String::from_utf8_lossy(&table.key.id),
            partitions + 1
        ));
    }
    let limit = (options.split_limit_per_table)();
    if limit != 0 && partitions as u64 >= limit {
        return Err(format!(
            "Too many tablets for the table, table: {}, limit: {limit}",
            String::from_utf8_lossy(&table.key.id)
        ));
    }
    Ok(())
}

fn cluster_config(snapshot: &CatalogSnapshot) -> SysClusterConfigEntryPB {
    snapshot
        .cluster_config()
        .and_then(|c| c.parse().ok())
        .unwrap_or_default()
}

/// `GetTableSplitPhase`: by the table's partitions per tserver the table may use. A placement
/// without blocks matches no tserver (`FindTServersForPlacementInfo`), so such a table is not
/// split automatically, as in C++.
fn split_phase(
    table_id: &[u8],
    partitions: usize,
    info: &ReplicationInfoPB,
    cluster: &Cluster,
    options: &AutoSplitOptions,
) -> Result<SplitPhase, String> {
    let num_servers = match info.live_replicas.as_ref() {
        Some(live) => cluster
            .valid
            .iter()
            .filter(|ci| {
                live.placement_blocks.iter().any(|block| {
                    is_cloud_info_prefix(block.cloud_info.as_ref().unwrap_or_default(), ci)
                })
            })
            .count(),
        None => cluster.valid.len(),
    };
    if num_servers == 0 {
        return Err(format!(
            "No live, non-blacklisted tservers for table {}. Cannot calculate average number of \
             tablets per tserver.",
            String::from_utf8_lossy(table_id)
        ));
    }
    let per_server = (partitions / num_servers) as i64;
    Ok(if per_server < (options.low_phase_shard_count_per_node)() {
        SplitPhase::Low
    } else if per_server < (options.high_phase_shard_count_per_node)() {
        SplitPhase::High
    } else {
        SplitPhase::Final
    })
}

fn is_starting(state: RaftGroupStatePB) -> bool {
    matches!(
        state,
        RaftGroupStatePB::NOT_STARTED | RaftGroupStatePB::BOOTSTRAPPING
    )
}

/// `CheckLiveReplicasForSplit`: no replica bootstrapping, every replica but read replicas a
/// RUNNING voter, and exactly `rf` of them, so the children do not inherit a peer still being
/// bootstrapped.
fn check_live_replicas(
    tablet_id: &[u8],
    replicas: &TabletReplicas,
    rf: usize,
) -> Result<(), String> {
    let mut live = 0;
    for r in &replicas.replicas {
        let read_replica = r.member_type == PeerMemberType::OBSERVER;
        if is_starting(r.state) || (!read_replica && r.state != RaftGroupStatePB::RUNNING) {
            return Err(format!(
                "At least one tablet peer is not running or is being bootstrapped, tablet_id: {}, \
                 peer_uuid: {}, member type: {:?}, current RAFT state: {:?}",
                String::from_utf8_lossy(tablet_id),
                String::from_utf8_lossy(&r.ts_uuid),
                r.member_type,
                r.state
            ));
        }
        if read_replica {
            continue;
        }
        if r.member_type != PeerMemberType::VOTER {
            return Err(format!(
                "One tablet peer is not a voter (being added or remote bootstrapped), tablet_id: \
                 {}, peer_uuid: {}, member type: {:?}, current RAFT state: {:?}",
                String::from_utf8_lossy(tablet_id),
                String::from_utf8_lossy(&r.ts_uuid),
                r.member_type,
                r.state
            ));
        }
        live += 1;
    }
    if live != rf {
        return Err(format!(
            "Tablet {} is {} replicated, has {live} live replicas, expected replication factor is \
             {rf}",
            String::from_utf8_lossy(tablet_id),
            if live < rf { "under" } else { "over" }
        ));
    }
    Ok(())
}

#[allow(clippy::too_many_arguments)]
/// `ShouldSplitValidCandidate`: the tablet's phase if its leader's SST files are above the
/// phase's threshold. Also run right before a scheduled split registers its children.
pub(crate) fn should_split(
    snapshot: &CatalogSnapshot,
    tablet_id: &[u8],
    table_id: &[u8],
    table_pb: &SysTablesEntryPB,
    drive: &DriveInfo,
    replicas: &TabletReplicas,
    cluster: &Cluster,
    options: &AutoSplitOptions,
) -> Result<SplitPhase, String> {
    let id = String::from_utf8_lossy(tablet_id);
    if drive.may_have_orphaned_post_split_data {
        return Err(format!("Tablet {id} may have uncompacted post-split data."));
    }
    if drive.has_active_vector_index_backfill {
        return Err(format!(
            "Tablet {id} has a vector index backfill in progress."
        ));
    }
    let size = drive.sst_files_size as i64;
    let low = (options.low_phase_size_threshold_bytes)();
    if size < low {
        return Err(format!(
            "Tablet {id} SST size ({size}) < low phase size threshold ({low})."
        ));
    }
    let info = table_replication_info(table_pb.replication_info.as_ref(), &cluster.config);
    check_live_replicas(tablet_id, replicas, replication_factor(&info, options))?;
    let partitions = snapshot.active_tablets_of(table_id).len();
    let phase = split_phase(table_id, partitions, &info, cluster, options)?;
    let threshold = phase.threshold(options);
    if size <= threshold {
        return Err(format!(
            "Table {} is in the {phase:?} split phase. Tablet {id} size ({size}) <= {} \
             ({threshold}).",
            String::from_utf8_lossy(table_id),
            phase.flag()
        ));
    }
    Ok(phase)
}

/// `GetReplicasWithOutstandingCompaction`: tservers whose replica may still hold the data of the
/// split it came from. A replica being added (not a VOTER or OBSERVER) is not waited for.
fn outstanding_compaction(replicas: &TabletReplicas) -> BTreeSet<Vec<u8>> {
    replicas
        .replicas
        .iter()
        .filter(|r| {
            matches!(
                r.member_type,
                PeerMemberType::VOTER | PeerMemberType::OBSERVER
            )
        })
        .filter(|r| r.drive.may_have_orphaned_post_split_data)
        .map(|r| r.ts_uuid.to_vec())
        .collect()
}

/// The replicas of each tablet, read once per pass so a pass sees one consistent set.
struct ReplicaCache<'a> {
    lookup: &'a dyn Fn(&[u8]) -> Option<TabletReplicas>,
    cache: HashMap<Vec<u8>, TabletReplicas>,
}

impl ReplicaCache<'_> {
    fn get(&mut self, tablet_id: &[u8]) -> &TabletReplicas {
        if !self.cache.contains_key(tablet_id) {
            let replicas = (self.lookup)(tablet_id).unwrap_or_default();
            self.cache.insert(tablet_id.to_vec(), replicas);
        }
        &self.cache[tablet_id]
    }
}

struct Candidate {
    tablet_id: Vec<u8>,
    table_id: Vec<u8>,
    size: u64,
    phase: SplitPhase,
}

/// `OutstandingSplitState`: splits counted against the global and per-tserver limits.
#[derive(Default)]
struct Outstanding {
    with_task: BTreeSet<Vec<u8>>,
    compacting: BTreeMap<Vec<u8>, BTreeSet<Vec<u8>>>,
    restarting: BTreeSet<Vec<u8>>,
    scheduled: Vec<(Vec<u8>, u64)>,
    ts_splits: HashMap<Vec<u8>, HashSet<Vec<u8>>>,
}

impl Outstanding {
    fn count(&self) -> usize {
        self.with_task.len() + self.compacting.len() + self.restarting.len() + self.scheduled.len()
    }

    fn can_split_more(&self, options: &AutoSplitOptions) -> bool {
        let limit = (options.outstanding_limit)();
        limit == 0 || (self.count() as u64) < limit
    }

    /// `TrackTserverSplits`: the split occupies a slot on each tserver of `replicas`.
    fn track(&mut self, split: &[u8], replicas: &TabletReplicas) {
        for r in &replicas.replicas {
            self.ts_splits
                .entry(r.ts_uuid.to_vec())
                .or_default()
                .insert(split.to_vec());
        }
    }

    /// `CanSplitMoreOnReplicas`.
    fn can_split_more_on(&self, replicas: &TabletReplicas, limit: usize) -> Result<(), String> {
        if limit == 0 {
            return Ok(());
        }
        for r in &replicas.replicas {
            if let Some(splits) = self.ts_splits.get(&*r.ts_uuid) {
                if splits.len() >= limit {
                    return Err(format!(
                        "TServer {} already has {} >= {limit} ongoing splits, can't do more \
                         splits there",
                        String::from_utf8_lossy(&r.ts_uuid),
                        splits.len()
                    ));
                }
            }
        }
        Ok(())
    }
}

/// A table that may be split automatically: the checks of a manual split, the limits, and the
/// tables held off for a while.
fn validate_table(
    snapshot: &CatalogSnapshot,
    table: &Entity,
    cluster: &Cluster,
    disabled: &mut Disabled,
    options: &AutoSplitOptions,
    now: Instant,
) -> Result<SysTablesEntryPB, String> {
    crate::validate_table(snapshot, table).map_err(|e| e.status.message().to_string())?;
    check_disabled(&mut disabled.ttl_tables, &table.key.id, now)?;
    let table_pb: SysTablesEntryPB = table.parse().map_err(|e| e.to_string())?;
    check_partition_limits(snapshot, table, &table_pb, cluster.num_live, options)?;
    Ok(table_pb)
}

/// `TableProperties::IsEffectiveTTL` of the table's default TTL. The proto field is unsigned; C++
/// reads it as signed, so a value above `i64::MAX` is no TTL.
fn has_effective_ttl(table_pb: &SysTablesEntryPB) -> bool {
    let props = &table_pb.schema.table_properties;
    props.has_default_time_to_live() && (props.default_time_to_live() as i64) > 0
}

/// `DoSplitting`, without scheduling: the new splits to start, by priority.
pub(crate) fn plan(
    snapshot: &CatalogSnapshot,
    cluster: &Cluster,
    lookup: &dyn Fn(&[u8]) -> Option<TabletReplicas>,
    with_task: &HashSet<Vec<u8>>,
    disabled: &mut Disabled,
    options: &AutoSplitOptions,
    now: Instant,
) -> Plan {
    let mut replicas = ReplicaCache {
        lookup,
        cache: HashMap::new(),
    };
    let mut state = Outstanding::default();

    let mut tables = Vec::new();
    for table in snapshot.iter(SysRowEntryType::TABLE) {
        match validate_table(snapshot, table, cluster, disabled, options, now) {
            Ok(pb) => tables.push((table, pb)),
            Err(e) => tracing::trace!("Skipping table for splitting. {e}"),
        }
    }

    // Splits being started (`kGetTabletSplitKey`): on the parent's tservers and the children's.
    let mut parent_of: HashMap<Vec<u8>, Vec<u8>> = HashMap::new();
    for (table, _) in &tables {
        for tablet in snapshot.tablets_of(&table.key.id) {
            let Some(hot) = tablet.tablet() else { continue };
            for child in &hot.split_tablet_ids {
                parent_of.insert(child.clone(), tablet.key.id.to_vec());
            }
            if with_task.contains(&*tablet.key.id) {
                state.with_task.insert(tablet.key.id.to_vec());
                let r = replicas.get(&tablet.key.id).clone();
                state.track(&tablet.key.id, &r);
                for child in &hot.split_tablet_ids {
                    let r = replicas.get(child).clone();
                    state.track(&tablet.key.id, &r);
                }
            }
        }
    }

    let mut candidates = Vec::new();
    for (table, table_pb) in &tables {
        for tablet in snapshot.tablets_of(&table.key.id) {
            let Some(hot) = tablet.tablet() else { continue };
            let id = tablet.key.id.to_vec();
            if is_inactive_state(hot.state) || state.with_task.contains(&id) {
                continue;
            }
            let running = hot.state == sys_tablets_entry_pb::State::RUNNING as i32;
            let parent = parent_of.get(&id).cloned();
            if let Some(parent) = &parent {
                if state.with_task.contains(parent) {
                    continue;
                }
                if !running {
                    // `AddSplitToRestart`: counted on the parent's tservers too, where the child
                    // will appear.
                    if !state.compacting.contains_key(parent)
                        && state.restarting.insert(parent.clone())
                    {
                        let r = replicas.get(parent).clone();
                        state.track(parent, &r);
                    }
                    let r = replicas.get(&id).clone();
                    state.track(parent, &r);
                    continue;
                }
                let r = replicas.get(&id).clone();
                let compacting = outstanding_compaction(&r);
                if !compacting.is_empty() {
                    // `AddCompactingSplit`: a running child means both exist, so no restart.
                    let was_restarting = state.restarting.remove(parent);
                    let inserted = !state.compacting.contains_key(parent);
                    state
                        .compacting
                        .entry(parent.clone())
                        .or_default()
                        .extend(compacting);
                    if inserted && !was_restarting {
                        let pr = replicas.get(parent).clone();
                        state.track(parent, &pr);
                    }
                    state.track(parent, &r);
                    continue;
                }
            }
            // A registered split is finished by the `tablet-splits` controller.
            if !hot.split_tablet_ids.is_empty() {
                continue;
            }
            match candidate(
                snapshot,
                tablet,
                table,
                table_pb,
                parent.as_deref(),
                &mut replicas,
                cluster,
                disabled,
                options,
                now,
            ) {
                Ok(c) => candidates.push(c),
                Err(e) => tracing::trace!("Should not split tablet {}. {e}", hex_or_id(&id)),
            }
        }
    }

    let num_candidates = candidates.len();
    process_candidates(&mut state, candidates, &mut replicas, options);
    if !state.compacting.is_empty() {
        tracing::debug!(
            "Outstanding post-split compactions: {} split(s)",
            state.compacting.len()
        );
    }
    Plan {
        outstanding: state.count(),
        schedule: state.scheduled,
        candidates: num_candidates,
    }
}

fn hex_or_id(id: &[u8]) -> String {
    match std::str::from_utf8(id) {
        Ok(s) => s.to_string(),
        Err(_) => hex(id),
    }
}

/// `ValidateSplitCandidateTablet` and `ShouldSplitValidCandidate` for one tablet.
#[allow(clippy::too_many_arguments)]
fn candidate(
    snapshot: &CatalogSnapshot,
    tablet: &Entity,
    table: &Entity,
    table_pb: &SysTablesEntryPB,
    parent: Option<&[u8]>,
    replicas: &mut ReplicaCache,
    cluster: &Cluster,
    disabled: &mut Disabled,
    options: &AutoSplitOptions,
    now: Instant,
) -> Result<Candidate, String> {
    let id = &tablet.key.id;
    let hot = tablet.tablet().expect("tablet row");
    let r = replicas.get(id).clone();
    let leader = r
        .leader()
        .ok_or_else(|| format!("No leader found for tablet {}", hex_or_id(id)))?;
    if let Some(parent) = parent.and_then(|p| snapshot.get(SysRowEntryType::TABLET, p)) {
        let gone = parent
            .tablet()
            .is_some_and(|p| p.hidden || p.state == sys_tablets_entry_pb::State::DELETED as i32);
        if !gone {
            return Err(format!(
                "Cannot split tablet whose parent is not yet deleted. Child tablet id: {}, parent \
                 tablet id: {}.",
                hex_or_id(id),
                hex_or_id(&parent.key.id)
            ));
        }
    }
    if has_effective_ttl(table_pb) && cluster.ttl_split_disabled.contains(&*leader.ts_uuid) {
        let recheck = (options.prevent_split_for_ttl_tables)();
        if !recheck.is_zero() {
            disabled
                .ttl_tables
                .insert(table.key.id.to_vec(), now + recheck);
        }
        return Err(format!(
            "Tablet splitting is not supported for tables with default time to live, tablet_id: \
             {}",
            hex_or_id(id)
        ));
    }
    check_disabled(&mut disabled.small_key_range_tablets, id, now)?;
    if hot.state != sys_tablets_entry_pb::State::RUNNING as i32 {
        return Err(format!("Tablet is not in running state: {}", hot.state));
    }
    let drive = leader.drive.clone();
    let phase = should_split(
        snapshot,
        id,
        &table.key.id,
        table_pb,
        &drive,
        &r,
        cluster,
        options,
    )?;
    // Colocated tablets are not served by a Rust leader; checked last, as the row must be parsed.
    let pb: SysTabletsEntryPB = tablet.parse().map_err(|e| e.to_string())?;
    if pb.colocated() {
        return Err(format!(
            "Tablet splitting is not supported for colocated tables, tablet_id: {}",
            hex_or_id(id)
        ));
    }
    let compacting = outstanding_compaction(&r);
    if !compacting.is_empty() {
        return Err(format!(
            "Tablet {} may have uncompacted post-split data on {} tserver(s)",
            hex_or_id(id),
            compacting.len()
        ));
    }
    Ok(Candidate {
        tablet_id: id.to_vec(),
        table_id: table.key.id.to_vec(),
        size: drive.sst_files_size,
        phase,
    })
}

/// `ProcessCandidates`: below a table's largest candidate by more than `tablet_split_min_size_ratio`
/// waits; low phase goes first, then the largest; each within the global and per-tserver limits.
fn process_candidates(
    state: &mut Outstanding,
    mut candidates: Vec<Candidate>,
    replicas: &mut ReplicaCache,
    options: &AutoSplitOptions,
) {
    if !state.can_split_more(options) {
        return;
    }
    if (options.sort_candidates)() {
        let mut largest: HashMap<&[u8], u64> = HashMap::new();
        for c in &candidates {
            let l = largest.entry(c.table_id.as_slice()).or_default();
            *l = (*l).max(c.size);
        }
        let ratio = (options.min_size_ratio)();
        let keep: Vec<bool> = candidates
            .iter()
            .map(|c| c.size >= (ratio * largest[c.table_id.as_slice()] as f64) as u64)
            .collect();
        let mut keep = keep.into_iter();
        candidates.retain(|_| keep.next().unwrap());
        // Splitting an under-sharded table unlocks parallelism: low phase first. High and final
        // are not ordered between themselves. Stable, so ties keep catalog order.
        candidates.sort_by(|a, b| {
            (b.phase == SplitPhase::Low)
                .cmp(&(a.phase == SplitPhase::Low))
                .then(b.size.cmp(&a.size))
        });
    }
    let limit = options.limit_per_tserver();
    for c in candidates {
        if !state.can_split_more(options) {
            break;
        }
        let r = replicas.get(&c.tablet_id).clone();
        if let Err(e) = state.can_split_more_on(&r, limit) {
            tracing::trace!(
                "Not scheduling split for tablet {}. {e}",
                hex_or_id(&c.tablet_id)
            );
            continue;
        }
        state.track(&c.tablet_id, &r);
        state.scheduled.push((c.tablet_id, c.size));
    }
}
