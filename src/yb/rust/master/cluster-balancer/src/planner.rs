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

//! `ClusterLoadBalancer::RunClusterBalancerWithOptions` and the handlers it calls, in C++'s order:
//! under-replicated tablets first (fewest replicas first, across tables), then per table the
//! removals, the adds (wrong placement, then blocks above their maximum, then load) and the leader
//! moves, within the global add, remove and leader-move budgets less the tasks in flight.

use std::borrow::Cow;
use std::collections::{BTreeSet, HashMap};
use std::sync::Arc;

use yb_proto::pb::common_net::{CloudInfoPB, PlacementBlockPB, PlacementInfoPB, ReplicationInfoPB};
use yb_proto::pb::consensus_metadata::PeerMemberType;

use crate::distribution::{
    calculate_optimal_load_distribution, calculate_table_load_difference, TsTableLoadMap,
};
use crate::state::{GlobalState, Result, RunMode, Tab, TableState, Ts, TsMeta, UNKNOWN_TS};
use crate::{
    matches_cloud_info, placement_id, similarity, Action, Activity, ClusterBalancer, ClusterView,
    FxHashMap, Options, PendingTasks, RunMetrics, RunResult, Similarity, TableInfo, WarningType,
};

/// A leader move (`LeaderMoveDetails`).
struct LeaderMove {
    tablet: Tab,
    from: Ts,
    /// None for any peer.
    to: Option<Ts>,
    to_path: Option<Arc<str>>,
    reason: String,
    also_remove_replica: bool,
}

/// A table's balancer tasks in flight, by index.
#[derive(Default)]
struct Pending {
    adds: FxHashMap<Tab, Ts>,
    removes: FxHashMap<Tab, Ts>,
    /// The new leader, none for any peer.
    stepdowns: FxHashMap<Tab, Option<Ts>>,
}

/// One placement's run. Tservers and tablets are indices into `g.ts` and each table's `tablets`;
/// ids are only looked up to build actions and messages.
pub(crate) struct Run<'a, 'b> {
    balancer: &'b mut ClusterBalancer,
    options: &'a Options,
    g: GlobalState<'a>,
    tables: Vec<(&'a TableInfo, TableState<'a>)>,
    /// By table index.
    pending: Vec<Pending>,
    /// By table index: the leader stepdowns in flight, which take from the table's budget.
    stepdowns_in_flight: Vec<usize>,
    actions: Vec<Action>,
    pending_tasks: usize,
    read_only: bool,
}

pub(crate) fn run(
    balancer: &mut ClusterBalancer,
    view: &ClusterView,
    options: &Options,
) -> RunResult {
    let mut result = RunResult::default();
    let mut modes = vec![RunMode {
        read_only: false,
        placement_uuid: view.live_placement_uuid.clone(),
        live_placement_uuid: view.live_placement_uuid.clone(),
    }];
    for uuid in &view.read_replica_placement_uuids {
        modes.push(RunMode {
            read_only: true,
            placement_uuid: uuid.clone(),
            live_placement_uuid: view.live_placement_uuid.clone(),
        });
    }
    // C++ registers each task on its table as it sends it, so a later placement's run counts the
    // tasks an earlier one decided as in flight, against its budgets and as the tablet's task.
    let mut decided: HashMap<String, PendingTasks> = HashMap::new();
    for mode in &modes {
        let activity = run_placement(balancer, view, options, mode, &decided, &mut result);
        for action in &activity.actions {
            let (table_id, tablet_id, kind, ts) = match action {
                Action::AddReplica {
                    table_id,
                    tablet_id,
                    ts_uuid,
                    ..
                } => (table_id, tablet_id, 0, ts_uuid),
                Action::RemoveReplica {
                    table_id,
                    tablet_id,
                    ts_uuid,
                    ..
                } => (table_id, tablet_id, 1, ts_uuid),
                Action::MoveLeader {
                    table_id,
                    tablet_id,
                    to_ts,
                    ..
                } => (table_id, tablet_id, 2, to_ts),
            };
            let tasks = decided.entry(table_id.clone()).or_default();
            let tasks = match kind {
                0 => &mut tasks.adds,
                1 => &mut tasks.removes,
                _ => &mut tasks.stepdowns,
            };
            tasks.insert(tablet_id.clone(), ts.clone());
        }
        result.activities.push(activity);
    }
    result
}

/// The table's tasks in flight: the view's, and those earlier placements decided in this run.
fn pending_of<'p>(
    table: &'p TableInfo,
    decided: &HashMap<String, PendingTasks>,
) -> Cow<'p, PendingTasks> {
    let Some(more) = decided.get(&table.id) else {
        return Cow::Borrowed(&table.pending);
    };
    let mut pending = table.pending.clone();
    for (into, from) in [
        (&mut pending.adds, &more.adds),
        (&mut pending.removes, &more.removes),
        (&mut pending.stepdowns, &more.stepdowns),
    ] {
        into.extend(from.iter().map(|(k, v)| (k.clone(), v.clone())));
    }
    Cow::Owned(pending)
}

impl<'a, 'b> Run<'a, 'b> {
    fn new(
        balancer: &'b mut ClusterBalancer,
        view: &'a ClusterView,
        options: &'a Options,
        mode: &RunMode,
    ) -> Run<'a, 'b> {
        Run {
            balancer,
            options,
            g: GlobalState::new(options, &view.tservers),
            tables: Vec::new(),
            pending: Vec::new(),
            stepdowns_in_flight: Vec::new(),
            actions: Vec::new(),
            pending_tasks: 0,
            read_only: mode.read_only,
        }
    }

    /// `ResetTableStatePtr`, `PopulateReplicationInfo`, `InitializeTSDescriptors` and
    /// `CountPendingTasks` for a table. A task on a tablet no longer in the table is ignored; one
    /// naming an unknown tserver skips the table.
    fn add_table(
        &mut self,
        table: &'a TableInfo,
        tasks: &PendingTasks,
        mode: &'a RunMode,
    ) -> Result<()> {
        let mut state = TableState::new(table, self.options, mode, self.g.ts.len());
        populate_replication_info(&mut state, table, &table.replication_info, self.options);
        for ts in 0..self.g.ts.len() as Ts {
            state.update_tablet_server(ts, &mut self.g);
        }
        self.pending_tasks += tasks.len();
        let g = &self.g;
        let ts = |uuid: &str| {
            g.index(uuid).ok_or_else(|| {
                format!(
                    "Found uninitialized ts_meta: ts_uuid: {uuid}, table_uuid: {}",
                    table.id
                )
            })
        };
        let mut pending = Pending::default();
        for (tablet, uuid) in &tasks.adds {
            if let Some(tablet) = state.tablet_index(tablet) {
                pending.adds.insert(tablet, ts(uuid)?);
            }
        }
        for (tablet, uuid) in &tasks.removes {
            if let Some(tablet) = state.tablet_index(tablet) {
                pending.removes.insert(tablet, ts(uuid)?);
            }
        }
        for (tablet, uuid) in &tasks.stepdowns {
            if let Some(tablet) = state.tablet_index(tablet) {
                let to = if uuid.is_empty() {
                    None
                } else {
                    Some(ts(uuid)?)
                };
                pending.stepdowns.insert(tablet, to);
            }
        }
        // Pending adds count as starting replicas.
        let mut adds: Vec<(Tab, Ts)> = pending.adds.iter().map(|(t, s)| (*t, *s)).collect();
        adds.sort_unstable();
        for (tablet, ts) in adds {
            state.add_starting_tablet(tablet, ts, &mut self.g);
        }
        self.tables.push((table, state));
        self.pending.push(pending);
        self.stepdowns_in_flight.push(tasks.stepdowns.len());
        Ok(())
    }
}

/// `RunClusterBalancerWithOptions`.
fn run_placement(
    balancer: &mut ClusterBalancer,
    view: &ClusterView,
    options: &Options,
    mode: &RunMode,
    decided: &HashMap<String, PendingTasks>,
    result: &mut RunResult,
) -> Activity {
    let mut run = Run::new(balancer, view, options, mode);
    let mut remaining_adds = options.max_concurrent_adds;
    let mut remaining_removals = options.max_concurrent_removals;
    let mut remaining_leader_moves = options.max_concurrent_leader_moves;
    let (mut pending_adds, mut pending_removes, mut pending_stepdowns) = (0, 0, 0);

    for table in &view.tables {
        if table.skip {
            if !mode.read_only {
                result.skipped_tables.push(table.id.clone());
            }
            continue;
        }
        if table.placement_unknown {
            run.g.activity.count_warning(
                WarningType::SkipTableLoadBalancing,
                format!(
                    "Skipping cluster balancing for table {} as its placement information is not \
                     available yet",
                    table.id
                ),
            );
            continue;
        }
        let tasks = pending_of(table, decided);
        pending_adds += tasks.adds.len() as i32;
        pending_removes += tasks.removes.len() as i32;
        pending_stepdowns += tasks.stepdowns.len() as i32;
        if let Err(e) = run.add_table(table, &tasks, mode) {
            run.g.activity.count_warning(
                WarningType::SkipTableLoadBalancing,
                format!("Skipping cluster balancing for table {}: {e}", table.id),
            );
        }
    }
    if pending_adds + pending_removes + pending_stepdowns > 0 {
        tracing::info!(
            "Total pending adds={pending_adds}, total pending removals={pending_removes}, total \
             pending leader stepdowns={pending_stepdowns}"
        );
    }
    set_remaining(pending_adds, &mut remaining_adds);
    set_remaining(pending_removes, &mut remaining_removals);
    set_remaining(pending_stepdowns, &mut remaining_leader_moves);

    // Analyze every table first: adds to one table change the global loads the others sort by.
    let mut analyzed = Vec::new();
    for index in 0..run.tables.len() {
        if let Err(e) = run.analyze_tablets(index) {
            let table_id = run.tables[index].0.id.clone();
            run.g.activity.count_warning(
                WarningType::SkipTableLoadBalancing,
                format!("Skipping cluster balancing for table {table_id}: {e}"),
            );
            continue;
        }
        run.add_load_metrics(index, &mut result.metrics);
        analyzed.push(index);
    }

    run.process_under_replicated_tablets(&analyzed, &mut remaining_adds);

    for &index in &analyzed {
        if remaining_adds == 0 && remaining_removals == 0 && remaining_leader_moves == 0 {
            break;
        }
        let table_id = run.tables[index].0.id.clone();
        {
            let (_, state) = &mut run.tables[index];
            state.sort_load(&run.g);
            state.sort_leader_load(&run.g);
        }
        if !options.handle_under_replicated_tablets_only {
            while remaining_removals > 0 {
                if run.tables[index].1.allow_only_leader_balancing {
                    break;
                }
                match run.handle_remove_replicas(index) {
                    Ok(true) => remaining_removals -= 1,
                    Ok(false) => break,
                    Err(e) => {
                        run.g.activity.count_warning(
                            WarningType::TableRemoveReplicas,
                            format!("Skipping removing replicas for table {table_id}: {e}"),
                        );
                        break;
                    }
                }
            }
        }
        while remaining_adds > 0 {
            if run.tables[index].1.allow_only_leader_balancing {
                break;
            }
            match run.handle_add_replicas(index) {
                Ok(true) => remaining_adds -= 1,
                Ok(false) => break,
                Err(e) => {
                    run.g.activity.count_warning(
                        WarningType::TableAddReplicas,
                        format!("Skipping adding replicas for table {table_id}: {e}"),
                    );
                    break;
                }
            }
        }
        if options.handle_under_replicated_tablets_only {
            continue;
        }
        let mut table_remaining_leader_moves = options.max_concurrent_leader_moves_per_table;
        set_remaining(
            run.stepdowns_in_flight[index] as i32,
            &mut table_remaining_leader_moves,
        );
        while remaining_leader_moves > 0 && table_remaining_leader_moves > 0 {
            match run.handle_leader_moves(index) {
                Ok(true) => {
                    remaining_leader_moves -= 1;
                    table_remaining_leader_moves -= 1;
                }
                Ok(false) => break,
                Err(e) => {
                    // C++ counts this under kTableAddReplicas.
                    run.g.activity.count_warning(
                        WarningType::TableAddReplicas,
                        format!("Skipping moving leaders for table {table_id}: {e}"),
                    );
                    break;
                }
            }
        }
    }

    let low_on_disk = run.g.tservers_low_on_disk();
    if !low_on_disk.is_empty() {
        tracing::warn!(
            "{} tablet server(s) are low on disk, with less than {}% of their capacity free: {:?}",
            low_on_disk.len(),
            options.min_free_disk_space_pct,
            low_on_disk
        );
    }

    let mut activity = std::mem::take(&mut run.g.activity);
    activity.placement_uuid = mode.placement_uuid.clone();
    activity.pending_tasks = run.pending_tasks;
    activity.actions = run.actions;
    // Global balancing is allowed once a run is idle, until a non-global move is made.
    run.balancer.can_perform_global_operations |= activity.is_idle();
    activity
}

/// `set_remaining`: the budget less the tasks in flight, never below 0.
fn set_remaining(pending: i32, remaining: &mut i32) {
    if pending > *remaining {
        *remaining = 0;
    } else {
        *remaining -= pending;
    }
}

/// `PopulateReplicationInfo`.
fn populate_replication_info(
    state: &mut TableState,
    table: &TableInfo,
    replication_info: &ReplicationInfoPB,
    options: &Options,
) {
    let mut has_read_replicas = true;
    let mut placement = if !state.mode.read_only {
        replication_info.live_replicas.clone().unwrap_or_default()
    } else if replication_info.read_replicas.is_empty() {
        // No read replicas configured: existing ones are over-replicated and removed.
        has_read_replicas = false;
        let mut p = PlacementInfoPB::new();
        p.set_num_replicas(0);
        p
    } else {
        replication_info
            .read_replicas
            .iter()
            .find(|p| p.placement_uuid() == state.mode.placement_uuid.as_bytes())
            .unwrap_or(&replication_info.read_replicas[0])
            .clone()
    };
    if placement.num_replicas() == 0 && has_read_replicas {
        placement.set_num_replicas(options.replication_factor);
    }
    if placement.placement_blocks.is_empty() {
        // A wildcard block matches every tserver.
        placement.placement_blocks.push(PlacementBlockPB::new());
    }
    state.set_placement(placement);
    state.use_preferred_zones =
        !table.is_transaction_status_table || options.transaction_tables_use_preferred_zones;
    if state.use_preferred_zones {
        state.affinitized_zones = affinitized_zones(replication_info);
    }
}

/// `CatalogManagerUtil::GetAllAffinitizedZones`.
pub(crate) fn affinitized_zones(replication_info: &ReplicationInfoPB) -> Vec<Vec<CloudInfoPB>> {
    let dedup = |zones: &[CloudInfoPB]| {
        let mut seen = BTreeSet::new();
        zones
            .iter()
            .filter(|ci| seen.insert(placement_id(ci)))
            .cloned()
            .collect::<Vec<_>>()
    };
    if !replication_info.multi_affinitized_leaders.is_empty() {
        replication_info
            .multi_affinitized_leaders
            .iter()
            .map(|set| dedup(&set.zones))
            .filter(|set| !set.is_empty())
            .collect()
    } else {
        let set = dedup(&replication_info.affinitized_leaders);
        if set.is_empty() {
            Vec::new()
        } else {
            vec![set]
        }
    }
}

/// `GetTServerTabletsByDrive`: the source's tablets per path, most loaded path first.
fn tablets_by_drive(drive_aware: bool, m: &TsMeta) -> Vec<&BTreeSet<Tab>> {
    if !drive_aware {
        return vec![&m.running_tablets];
    }
    m.sorted_path_load_by_tablets_count
        .iter()
        .filter_map(|path| m.path_to_tablets.get(path))
        .collect()
}

/// `GetLeadersOnTSToMove`: the destination's replicas of `leaders`, by its least-led path first.
fn leaders_on_ts_to_move(
    drive_aware: bool,
    leaders: &BTreeSet<Tab>,
    to: &TsMeta,
) -> Vec<(Tab, Option<Arc<str>>)> {
    if !drive_aware {
        return to
            .running_tablets
            .iter()
            .filter(|t| leaders.contains(t))
            .map(|t| (*t, None))
            .collect();
    }
    to.sorted_path_load_by_leader_count
        .iter()
        .filter_map(|path| to.path_to_tablets.get(path).map(|t| (path, t)))
        .flat_map(|(path, tablets)| {
            tablets
                .iter()
                .filter(|t| leaders.contains(t))
                .map(move |t| (*t, Some(path.clone())))
        })
        .collect()
}

impl Run<'_, '_> {
    fn can_balance_global_load(&self) -> bool {
        self.options.enable_global_load_balancing && self.balancer.can_perform_global_operations
    }

    fn member_type(&self) -> PeerMemberType {
        if self.read_only {
            PeerMemberType::PRE_OBSERVER
        } else {
            PeerMemberType::PRE_VOTER
        }
    }

    /// `AnalyzeTablets`.
    fn analyze_tablets(&mut self, index: usize) -> Result<()> {
        let (_, state) = &mut self.tables[index];
        let g = &mut self.g;
        state.num_running_tablets = 0;
        let mut total_size = 0;
        for tablet in 0..state.tablets.len() as Tab {
            // RUNNING in the catalog: created and started once; its replicas may still be
            // bootstrapping.
            if state.tablets[tablet as usize].running {
                state.num_running_tablets += 1;
                state.update_tablet(tablet, g)?;
                total_size += state.tablet_meta(tablet)?.size;
            }
        }
        state.average_tablet_size = if state.num_running_tablets == 0 {
            0
        } else {
            total_size / state.num_running_tablets as u64
        };
        state.set_initialized();
        state.sort_load(g);
        state.sort_leader_load(g);
        // The tasks in flight, as if done.
        let pending = &self.pending[index];
        for tablet in 0..state.tablets.len() as Tab {
            if let Some(from) = pending.removes.get(&tablet) {
                state.remove_replica(tablet, *from, g)?;
            }
            if let Some(to) = pending.stepdowns.get(&tablet) {
                match state.meta_mut(tablet).leader {
                    Some(from) if from != UNKNOWN_TS => {
                        state.move_leader(tablet, from, *to, None, g)?
                    }
                    _ => {
                        return Err(format!(
                            "Tablet {} has a leader stepdown in flight but no known leader",
                            state.tablet_id(tablet)
                        ))
                    }
                }
            }
            if let Some(to) = pending.adds.get(&tablet) {
                state.add_replica(tablet, *to, g)?;
            }
        }
        Ok(())
    }

    /// The table's distance from its optimal distribution, for the metrics only. Block maximums
    /// and disk space are not considered, so it can stay above 0 while the balancer is idle.
    fn add_load_metrics(&self, index: usize, metrics: &mut RunMetrics) {
        let (table, state) = &self.tables[index];
        let mut current = TsTableLoadMap::new();
        let mut valid = Vec::new();
        for &ts in &state.sorted_load {
            current.insert(self.g.uuid(ts).to_string(), state.load(ts));
            if !self.g.is_blacklisted(ts) {
                valid.push(self.g.ts[ts as usize]);
            }
        }
        match calculate_optimal_load_distribution(
            &valid,
            &state.placement,
            &current,
            state.num_running_tablets,
        ) {
            Ok(goal) => {
                let adds = calculate_table_load_difference(&current, &goal);
                metrics.total_table_load_difference += adds as u32;
                metrics.estimated_data_to_balance_bytes += adds as u64 * state.average_tablet_size;
            }
            Err(e) => tracing::debug!(
                "No valid load distribution found for table {}: {e}",
                table.id
            ),
        }
        metrics.tablets_in_wrong_placement += state.tablets_wrong_placement.len() as u32;
        metrics.blacklisted_leaders += state.tablets_with_badly_placed_leaders.len() as u32;
    }

    /// `ProcessUnderReplicatedTablets`: tablets with fewest replicas first, across tables.
    fn process_under_replicated_tablets(&mut self, analyzed: &[usize], remaining_adds: &mut i32) {
        let mut under_replicated: Vec<(i32, usize, Tab)> = Vec::new();
        for &index in analyzed {
            let state = &self.tables[index].1;
            for &tablet in &state.tablets_missing_replicas {
                let Ok(meta) = state.tablet_meta(tablet) else {
                    continue;
                };
                // Without a leader no config change can be made.
                if meta.leader.is_none() {
                    continue;
                }
                under_replicated.push((meta.num_replicas(), index, tablet));
            }
        }
        under_replicated.sort_by_key(|(count, _, _)| *count);
        for (_, index, tablet) in under_replicated {
            if *remaining_adds == 0 {
                break;
            }
            if self.tables[index].1.allow_only_leader_balancing {
                continue;
            }
            self.tables[index].1.sort_load(&self.g);
            match self.handle_add_if_missing_placement(index, tablet) {
                Ok(Some(_)) => *remaining_adds -= 1,
                other => {
                    let why = match other {
                        Err(e) => e,
                        _ => "no valid tservers to place tablet".to_string(),
                    };
                    let tablet_id = self.tables[index].1.tablet_id(tablet);
                    self.g.activity.count_warning(
                        WarningType::TabletUnderReplicated,
                        format!(
                            "Skipping adding replicas for under-replicated tablet {tablet_id}: \
                             {why}"
                        ),
                    );
                }
            }
        }
    }

    /// `CheckRemoteBootstrapLimits`.
    fn check_remote_bootstrap_limits(&self, index: usize) -> Result<()> {
        let state = &self.tables[index].1;
        if self.options.allow_limit_starting_tablets {
            if self.g.total_starting_tablets >= self.options.max_tablet_remote_bootstraps {
                return Err(format!(
                    "Cannot add replicas. Currently remote bootstrapping {} tablets, when our max \
                     allowed is {}",
                    self.g.total_starting_tablets, self.options.max_tablet_remote_bootstraps
                ));
            }
            if state.total_starting >= self.options.max_tablet_remote_bootstraps_per_table {
                return Err(format!(
                    "Cannot add replicas. Currently remote bootstrapping {} tablets for table {}, \
                     when our max allowed is {} per table",
                    state.total_starting,
                    state.table_id,
                    self.options.max_tablet_remote_bootstraps_per_table
                ));
            }
        }
        Ok(())
    }

    /// `CanAddReplicas`: also bounds the space amplification of add-then-remove moves.
    fn can_add_replicas(&self, index: usize) -> Result<()> {
        self.check_remote_bootstrap_limits(index)?;
        let state = &self.tables[index].1;
        if self.options.allow_limit_over_replicated_tablets
            && state.tablets_over_replicated.len()
                >= self.options.max_over_replicated_tablets_per_table.max(0) as usize
        {
            return Err(format!(
                "Cannot add replicas. Currently have a total overreplication of {}, when max \
                 allowed is {}, overreplicated tablets: {}",
                state.tablets_over_replicated.len(),
                self.options.max_over_replicated_tablets_per_table,
                state
                    .tablets_over_replicated
                    .iter()
                    .map(|t| state.tablet_id(*t))
                    .collect::<Vec<_>>()
                    .join(", ")
            ));
        }
        Ok(())
    }

    /// `HandleAddIfMissingPlacement`: the destination, if a replica was added.
    fn handle_add_if_missing_placement(&mut self, index: usize, tablet: Tab) -> Result<Option<Ts>> {
        let meta = self.tables[index].1.tablet_meta(tablet)?.clone();
        // Below RF the add restores RF, so only the bootstrap limits apply; a block below its
        // minimum at RF is over-replicated by the add, so the over-replication limit applies too.
        if meta.is_under_replicated {
            self.check_remote_bootstrap_limits(index)?;
        } else {
            self.can_add_replicas(index)?;
        }
        for ts in self.tables[index].1.sorted_load.clone() {
            let state = &self.tables[index].1;
            let info = self.g.ts[ts as usize];
            // `CanAddTSToMissingPlacements`.
            let in_missing_placement = || {
                meta.under_replicated_placements
                    .iter()
                    .any(|b| matches_cloud_info(&info.cloud_info, &state.blocks[*b].ci))
            };
            if (meta.under_replicated_placements.is_empty() || in_missing_placement())
                && state.can_add_tablet_to_tablet_server(tablet, ts, None, &self.g)?
            {
                let reason = if meta.under_replicated_placements.is_empty() {
                    format!(
                        "Tablet has fewer replicas than its replication factor, adding a replica \
                         in placement ({})",
                        short(&info.cloud_info)
                    )
                } else {
                    format!(
                        "Placement ({}) does not have enough replicas of this tablet",
                        short(&info.cloud_info)
                    )
                };
                self.add_or_move_replica(index, tablet, None, ts, reason)?;
                self.tables[index]
                    .1
                    .tablets_missing_replicas
                    .remove(&tablet);
                return Ok(Some(ts));
            }
        }
        Ok(None)
    }

    /// `HandleAddReplicas`.
    fn handle_add_replicas(&mut self, index: usize) -> Result<bool> {
        self.can_add_replicas(index)?;
        // Draining blacklisted or wrongly placed tservers unblocks decommissioning: first.
        if self.handle_add_if_wrong_placement(index)? {
            return Ok(true);
        }
        if self.handle_add_if_over_max_placement(index)? {
            return Ok(true);
        }
        self.load_to_move(index)
    }

    /// `HandleAddIfWrongPlacement`.
    fn handle_add_if_wrong_placement(&mut self, index: usize) -> Result<bool> {
        for tablet in self.tables[index].1.tablets_wrong_placement.clone() {
            let state = &self.tables[index].1;
            // An over-replicated tablet needs a remove, not another replica.
            if state.tablets_over_replicated.contains(&tablet) {
                continue;
            }
            match state.select_wrong_placement_replica_to_move(tablet, &self.g)? {
                Some((from, to)) => {
                    let reason = format!(
                        "Add replica to replace replica on blacklisted or wrong-placement tserver \
                         {}",
                        self.g.uuid(from)
                    );
                    self.add_or_move_replica(index, tablet, Some(from), to, reason)?;
                    return Ok(true);
                }
                None => {
                    let tablet_id = state.tablet_id(tablet);
                    self.g.activity.count_warning(
                        WarningType::TabletWrongPlacement,
                        format!(
                            "Could not find a valid tserver to host tablet {tablet_id}, which has \
                             replicas on blacklisted / wrong-placement tservers. A valid tserver \
                             is one that matches the placement information, is not blacklisted, \
                             and does not already host a copy of the tablet."
                        ),
                    )
                }
            }
        }
        Ok(false)
    }

    /// `HandleAddIfOverMaxPlacement`: add outside the blocks above their maximum; the remove path
    /// then drops a replica from the offending block.
    fn handle_add_if_over_max_placement(&mut self, index: usize) -> Result<bool> {
        for tablet in self.tables[index].1.tablets_over_max_placements.clone() {
            let state = &self.tables[index].1;
            if state.tablets_added.contains(&tablet) {
                continue;
            }
            let meta = state.tablet_meta(tablet)?;
            for &to in &state.sorted_load {
                let Some(to_block) = state.valid_placement(to) else {
                    continue;
                };
                if meta.over_max_placements.contains(&to_block) {
                    continue;
                }
                if !state.can_add_tablet_to_tablet_server(tablet, to, None, &self.g)? {
                    continue;
                }
                // The logged source: a running replica in an offending block.
                let mut from: Option<(Ts, &CloudInfoPB)> = None;
                for uuid in state.tablets[tablet as usize].replicas.keys() {
                    let Some(ts) = self.g.index(uuid) else {
                        continue;
                    };
                    if !state.ts_meta(ts).running_tablets.contains(&tablet) {
                        continue;
                    }
                    if let Some(b) = state.valid_placement(ts) {
                        if meta.over_max_placements.contains(&b) {
                            from = Some((ts, &state.blocks[b].ci));
                            break;
                        }
                    }
                }
                let reason = format!(
                    "Placement {} exceeds its max_num_replicas",
                    from.map(|(_, ci)| short(ci)).unwrap_or_default()
                );
                self.add_or_move_replica(index, tablet, from.map(|(ts, _)| ts), to, reason)?;
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// `GetLoadToMove`: from the most to the least loaded tserver, while their loads differ by
    /// at least `min_load_variance_to_balance` (or globally, once the tables are balanced).
    fn load_to_move(&mut self, index: usize) -> Result<bool> {
        let state = &self.tables[index].1;
        if state.sorted_load.is_empty() {
            return Ok(false);
        }
        let min_variance = self.options.min_load_variance_to_balance;
        let sorted = state.sorted_load.clone();
        let last_pos = sorted.len() as isize - 1;
        let mut left: isize = 0;
        while left <= last_pos {
            let low = sorted[left as usize];
            // A blacklisted tserver takes no load. The last one is never skipped: it ends the
            // search below.
            if left < last_pos && self.g.is_blacklisted(low) {
                left += 1;
                continue;
            }
            let mut right = last_pos;
            while right >= 0 {
                let state = &self.tables[index].1;
                let high = sorted[right as usize];
                let load_variance = state.load(high) as isize - state.load(low) as isize;
                let mut is_global_move = false;
                if left == right || (load_variance as f64) < min_variance {
                    if right == last_pos && load_variance == 0 {
                        return Ok(false);
                    }
                    if load_variance > 0 && self.can_balance_global_load() {
                        let global_variance = self.g.global_load(high) - self.g.global_load(low);
                        if (global_variance as f64) < min_variance {
                            if right == last_pos {
                                return Ok(false);
                            }
                            break;
                        }
                        is_global_move = true;
                    } else {
                        if right == last_pos {
                            return Ok(false);
                        }
                        break;
                    }
                }
                // Over-replicated replicas on the source are about to be removed: if that alone
                // would even the loads, adding more replicas here only adds space.
                if !is_global_move
                    && ((load_variance - state.possibly_transient_load(high) as isize) as f64)
                        < min_variance
                {
                    right -= 1;
                    continue;
                }
                if let Some(tablet) = self.tablet_to_move(index, high, low)? {
                    let reason = if is_global_move {
                        format!(
                            "Source tserver has more tablets (globally) than destination ({} > {})",
                            self.g.global_load(high),
                            self.g.global_load(low)
                        )
                    } else {
                        format!(
                            "Source tserver has more tablets for this table than destination ({} \
                             > {})",
                            state.load(high),
                            state.load(low)
                        )
                    };
                    self.add_or_move_replica(index, tablet, Some(high), low, reason)?;
                    if !is_global_move {
                        self.balancer.can_perform_global_operations = false;
                    }
                    return Ok(true);
                }
                right -= 1;
            }
            left += 1;
        }
        Err("Cluster balancing algorithm reached illegal state.".to_string())
    }

    /// `GetTabletToMove`: from the source's most loaded drive, a tablet the destination can take,
    /// in the same placement block, preferring one whose leader is closest to the destination.
    fn tablet_to_move(&self, index: usize, from: Ts, to: Ts) -> Result<Option<Tab>> {
        let state = &self.tables[index].1;
        let from_meta = state.ts_meta(from);
        let same_placement = matches!(
            (state.valid_placement(from), state.valid_placement(to)),
            (Some(f), Some(t)) if f == t
        );
        let to_ci = &self.g.ts[to as usize].cloud_info;
        for tablets in tablets_by_drive(self.g.drive_aware, from_meta) {
            let mut result: Option<(Tab, Similarity)> = None;
            for &tablet in tablets {
                // Not a tablet already over-replicated, nor one just split.
                if state.tablets_over_replicated.contains(&tablet)
                    || from_meta.disabled_by_ts_tablets.contains(&tablet)
                {
                    continue;
                }
                if !state.can_add_tablet_to_tablet_server(tablet, to, Some(from), &self.g)? {
                    continue;
                }
                if !state.placement.placement_blocks.is_empty() && !same_placement {
                    continue;
                }
                let leader = state.tablet_meta(tablet)?.leader;
                let ci_similarity = match leader.and_then(|l| self.g.info(l)) {
                    Some(leader) if !self.options.ignore_cloud_info_similarity => {
                        similarity(&leader.cloud_info, to_ci)
                    }
                    _ => Similarity::NoMatch,
                };
                if result.is_some_and(|(_, s)| ci_similarity <= s) {
                    continue;
                }
                result = Some((tablet, ci_similarity));
            }
            if let Some((tablet, _)) = result {
                return Ok(Some(tablet));
            }
        }
        Ok(None)
    }

    /// `HandleRemoveReplicas`: wrongly placed replicas first, then over-replication, from the most
    /// loaded tserver.
    fn handle_remove_replicas(&mut self, index: usize) -> Result<bool> {
        if self.handle_remove_if_wrong_placement(index)? {
            return Ok(true);
        }
        for tablet in self.tables[index].1.tablets_over_replicated.clone() {
            let state = &self.tables[index].1;
            // Wait for a pending ADD_SERVER.
            let meta = state.tablet_meta(tablet)?;
            if state.tablets[tablet as usize].config_in_transition || meta.starting > 0 {
                continue;
            }
            let victim = meta
                .over_replicated_tablet_servers
                .iter()
                .filter(|ts| !state.ts_meta(**ts).starting_tablets.contains(&tablet))
                .max_by(|a, b| state.compare_load(**a, **b, Some(tablet), &self.g))
                .copied();
            let Some(victim) = victim else {
                return Err(format!(
                    "No tservers to remove from over-replicated tablet {}",
                    state.tablet_id(tablet)
                ));
            };
            self.remove_replica(
                index,
                tablet,
                victim,
                "Tablet is over-replicated (this is expected if the tablet is being moved)".into(),
            )?;
            return Ok(true);
        }
        Ok(false)
    }

    /// `HandleRemoveIfWrongPlacement`: an over-replicated tablet's blacklisted replica, else a
    /// wrongly placed one.
    fn handle_remove_if_wrong_placement(&mut self, index: usize) -> Result<bool> {
        for tablet in self.tables[index].1.tablets_wrong_placement.clone() {
            let state = &self.tables[index].1;
            if !state.tablets_over_replicated.contains(&tablet) {
                continue;
            }
            if state.tablets[tablet as usize].config_in_transition {
                continue;
            }
            let meta = state.tablet_meta(tablet)?;
            let target = meta
                .blacklisted_tablet_servers
                .iter()
                .next()
                .or_else(|| meta.wrong_placement_tablet_servers.iter().next())
                .copied();
            if let Some(target) = target {
                self.remove_replica(
                    index,
                    tablet,
                    target,
                    "Tserver is blacklisted or incompatible placement info for tablet".into(),
                )?;
                return Ok(true);
            }
        }
        Ok(false)
    }

    /// `HandleLeaderMoves`: across leader affinity priorities first, then within them.
    fn handle_leader_moves(&mut self, index: usize) -> Result<bool> {
        let mut leader_move = None;
        if self.tables[index].1.use_preferred_zones {
            leader_move = self.leader_to_move_across_priorities(index);
        }
        if leader_move.is_none() {
            leader_move = self.leader_to_move_within_priorities(index)?;
        }
        let Some(leader_move) = leader_move else {
            return Ok(false);
        };
        self.move_leader(index, leader_move)?;
        Ok(true)
    }

    /// `GetLeaderToMoveAcrossAffinitizedPriorities`.
    fn leader_to_move_across_priorities(&self, index: usize) -> Option<LeaderMove> {
        let state = &self.tables[index].1;
        let sorted = &state.sorted_leader_load;
        for lower in (1..sorted.len()).rev() {
            for &from in sorted[lower].iter().rev() {
                if state.leader_load(from) == 0 {
                    // Blacklisted tservers sort last whatever their load; past them, a tserver
                    // with no leaders means none of the rest have any.
                    if self.g.is_leader_blacklisted(from) {
                        continue;
                    }
                    break;
                }
                let leaders = &state.ts_meta(from).leaders;
                for (higher, set) in sorted.iter().enumerate().take(lower) {
                    for &to in set {
                        let peers =
                            leaders_on_ts_to_move(self.g.drive_aware, leaders, state.ts_meta(to));
                        if let Some((tablet, to_path)) = peers.into_iter().next() {
                            return Some(LeaderMove {
                                tablet,
                                from,
                                to: Some(to),
                                to_path,
                                reason: format!(
                                    "Source tserver has lower leader priority ({lower}) than \
                                     destination ({higher})"
                                ),
                                also_remove_replica: false,
                            });
                        }
                    }
                }
            }
        }
        None
    }

    /// `GetLeaderToMoveWithinAffinitizedPriorities`.
    fn leader_to_move_within_priorities(&mut self, index: usize) -> Result<Option<LeaderMove>> {
        for set in self.tables[index].1.sorted_leader_load.clone() {
            if let Some(m) = self.leader_to_move(index, &set)? {
                return Ok(Some(m));
            }
        }
        Ok(None)
    }

    /// `GetLeaderToMove` within one priority: from the most to the least led tserver while their
    /// leader counts differ by at least 2, or off a leader-blacklisted tserver, or globally.
    fn leader_to_move(&mut self, index: usize, sorted: &[Ts]) -> Result<Option<LeaderMove>> {
        if sorted.is_empty() {
            return Ok(None);
        }
        let state = &self.tables[index].1;
        let threshold = state.adjust_leader_balance_threshold(sorted.len());
        // Is there anything to move: a leader on a leader-blacklisted tserver, or a tserver above
        // the threshold?
        for &high in sorted.iter().rev() {
            let high_load = state.leader_load(high);
            if self.g.is_leader_blacklisted(high) {
                if high_load > 0 {
                    break;
                }
                continue;
            }
            if threshold > 0 && high_load <= threshold {
                return Ok(None);
            }
            break;
        }
        let min_variance = self.options.min_load_variance_to_balance;
        let last_pos = sorted.len() as isize - 1;
        for left in 0..=last_pos {
            let low = sorted[left as usize];
            if self.g.is_leader_blacklisted(low) {
                // Past the tservers that may take leaders.
                return Ok(None);
            }
            let mut right = last_pos;
            while right >= 0 {
                let state = &self.tables[index].1;
                let high = sorted[right as usize];
                let high_blacklisted = self.g.is_leader_blacklisted(high);
                let high_load = state.leader_load(high) as isize;
                let low_load = state.leader_load(low) as isize;
                let load_variance = high_load - low_load;
                let mut is_global_move = false;
                if high_blacklisted && high_load == 0 {
                    right -= 1;
                    continue;
                }
                let mut reason = String::new();
                if load_variance as f64 >= min_variance {
                    reason = format!(
                        "Source tserver has more leaders for this table than destination \
                         ({high_load} > {low_load})"
                    );
                } else if high_blacklisted {
                    reason = "Leader is on leader blacklisted tserver".to_string();
                }
                if left == right || reason.is_empty() {
                    if load_variance == 0 && right == last_pos {
                        return Ok(None);
                    }
                    if load_variance > 0 && self.can_balance_global_load() {
                        let global_high = self.g.global_leader_load(high);
                        let global_low = self.g.global_leader_load(low);
                        // Unlike replicas, a leader also needs a peer on the destination, so
                        // the next destination may still have one: break, not return.
                        if ((global_high - global_low) as f64) < min_variance {
                            break;
                        }
                        reason = format!(
                            "Source tserver has more global leaders than destination \
                             ({global_high} > {global_low})"
                        );
                        is_global_move = true;
                    } else {
                        break;
                    }
                }
                let leaders = &state.ts_meta(high).leaders;
                for (tablet, to_path) in
                    leaders_on_ts_to_move(self.g.drive_aware, leaders, state.ts_meta(low))
                {
                    // Not to a tserver a stepdown to failed recently.
                    if state
                        .tablet_meta(tablet)
                        .is_ok_and(|m| m.leader_stepdown_failures.contains(&low))
                    {
                        tracing::info!(
                            "Cannot move tablet {} leader from TS {} to TS {} yet: a previous \
                             attempt with the same intended leader failed recently",
                            state.tablet_id(tablet),
                            self.g.uuid(high),
                            self.g.uuid(low)
                        );
                        continue;
                    }
                    if !is_global_move {
                        self.balancer.can_perform_global_operations = false;
                    }
                    return Ok(Some(LeaderMove {
                        tablet,
                        from: high,
                        to: Some(low),
                        to_path,
                        reason: reason.clone(),
                        also_remove_replica: false,
                    }));
                }
                right -= 1;
            }
        }
        Err("Cluster balancing algorithm reached invalid state!".to_string())
    }

    /// `AddOrMoveReplica`: ADD_SERVER; for a move, the remove follows in a later run.
    fn add_or_move_replica(
        &mut self,
        index: usize,
        tablet: Tab,
        from: Option<Ts>,
        to: Ts,
        reason: String,
    ) -> Result<()> {
        let member_type = self.member_type();
        let (table, state) = &mut self.tables[index];
        let tablet_id = state.tablet_id(tablet);
        let from_uuid = from.map(|f| self.g.uuid(f)).unwrap_or("");
        let to_uuid = self.g.uuid(to);
        if from_uuid.is_empty() {
            tracing::info!("Adding replica of tablet {tablet_id} to {to_uuid}. Reason: {reason}");
        } else {
            tracing::info!(
                "Moving tablet {tablet_id} from {from_uuid} to {to_uuid}. Reason: {reason}"
            );
        }
        if self.pending[index].adds.contains_key(&tablet) {
            return Err("Sending duplicate add replica task.".to_string());
        }
        self.actions.push(Action::AddReplica {
            table_id: table.id.clone(),
            tablet_id: tablet_id.to_string(),
            ts_uuid: to_uuid.to_string(),
            from_ts: from_uuid.to_string(),
            member_type,
            reason,
        });
        state.add_replica(tablet, to, &mut self.g)
    }

    /// `ClusterLoadBalancer::RemoveReplica`: a leader is first stepped down (to the best peer by
    /// leader affinity) and removed after.
    fn remove_replica(&mut self, index: usize, tablet: Tab, ts: Ts, reason: String) -> Result<()> {
        let state = &self.tables[index].1;
        tracing::info!(
            "Removing replica of tablet {} from {}. Reason: {reason}",
            state.tablet_id(tablet),
            self.g.uuid(ts)
        );
        if state.tablet_meta(tablet)?.leader == Some(ts) {
            let to = if self.options.stepdown_to_preferred_leader_on_remove {
                self.select_best_leader_after_stepdown(index, tablet, ts)
            } else {
                None
            };
            self.move_leader(
                index,
                LeaderMove {
                    tablet,
                    from: ts,
                    to,
                    to_path: None,
                    reason,
                    also_remove_replica: true,
                },
            )?;
        } else {
            if self.pending[index].removes.contains_key(&tablet) {
                return Err("Sending duplicate remove replica task.".to_string());
            }
            self.actions.push(Action::RemoveReplica {
                table_id: self.tables[index].0.id.clone(),
                tablet_id: state.tablet_id(tablet).to_string(),
                ts_uuid: self.g.uuid(ts).to_string(),
                reason,
            });
        }
        let (_, state) = &mut self.tables[index];
        state.remove_replica(tablet, ts, &mut self.g)
    }

    /// `SelectBestLeaderAfterStepdown`: the running peer in the most preferred leader zone, then
    /// with the fewest leaders. Never a blacklisted tserver: its replica may lag, and a nominated
    /// peer that is not caught up fails the stepdown.
    fn select_best_leader_after_stepdown(
        &self,
        index: usize,
        tablet: Tab,
        exclude: Ts,
    ) -> Option<Ts> {
        let state = &self.tables[index].1;
        let affinity = |ts: Ts| -> usize {
            if self.g.is_leader_blacklisted(ts) {
                return state.affinitized_zones.len() + 1;
            }
            let ci = &self.g.ts[ts as usize].cloud_info;
            state
                .affinitized_zones
                .iter()
                .position(|zones| zones.iter().any(|z| matches_cloud_info(ci, z)))
                .unwrap_or(state.affinitized_zones.len())
        };
        // In uuid order, so ties go to the lowest uuid.
        let mut candidates: Vec<(Ts, usize)> = (0..self.g.ts.len() as Ts)
            .filter(|ts| {
                *ts != exclude
                    && !self.g.is_blacklisted(*ts)
                    && state.ts_meta(*ts).running_tablets.contains(&tablet)
            })
            .map(|ts| (ts, affinity(ts)))
            .collect();
        candidates.sort_by(|a, b| {
            a.1.cmp(&b.1)
                .then_with(|| state.leader_load(a.0).cmp(&state.leader_load(b.0)))
        });
        candidates.first().map(|(ts, _)| *ts)
    }

    /// `ClusterLoadBalancer::MoveLeader`.
    fn move_leader(&mut self, index: usize, m: LeaderMove) -> Result<()> {
        let (table, state) = &mut self.tables[index];
        let tablet_id = state.tablet_id(m.tablet);
        let from_uuid = self.g.uuid(m.from);
        let to_uuid = m.to.map(|t| self.g.uuid(t)).unwrap_or("");
        tracing::info!(
            "Moving leader of tablet {tablet_id} from {from_uuid} to {to_uuid}. Reason: {}",
            m.reason
        );
        let actual = state.tablet_meta(m.tablet)?.leader;
        if actual != Some(m.from) {
            let actual = match actual {
                None => "",
                Some(UNKNOWN_TS) => "<unknown>",
                Some(l) => self.g.uuid(l),
            };
            return Err(format!(
                "Cannot send leader stepdown for tablet {tablet_id} to peer {from_uuid} as it is \
                 not the leader. Actual leader is {actual}"
            ));
        }
        if self.pending[index].stepdowns.contains_key(&m.tablet) {
            return Err("Sending duplicate leader stepdown task.".to_string());
        }
        self.actions.push(Action::MoveLeader {
            table_id: table.id.clone(),
            tablet_id: tablet_id.to_string(),
            from_ts: from_uuid.to_string(),
            to_ts: to_uuid.to_string(),
            also_remove_replica: m.also_remove_replica,
            reason: m.reason,
        });
        state.move_leader(m.tablet, m.from, m.to, m.to_path.as_ref(), &mut self.g)
    }
}

/// `CloudInfoPB::ShortDebugString`.
fn short(ci: &CloudInfoPB) -> String {
    protobuf::text_format::print_to_string(ci)
}

/// Single steps of a run over the first table, as `LoadBalancerMockedBase` drives them, with
/// tservers and tablets by id.
#[cfg(test)]
pub(crate) mod testing {
    use super::*;

    /// `ResetLoadBalancerAndAnalyzeTablets`.
    pub(crate) fn analyze<'a, 'b>(
        balancer: &'b mut ClusterBalancer,
        view: &'a ClusterView,
        options: &'a Options,
        mode: &'a RunMode,
    ) -> Result<Run<'a, 'b>> {
        let mut run = Run::new(balancer, view, options, mode);
        run.add_table(&view.tables[0], &view.tables[0].pending, mode)?;
        run.analyze_tablets(0)?;
        Ok(run)
    }

    impl Run<'_, '_> {
        pub(crate) fn state(&self) -> &TableState<'_> {
            &self.tables[0].1
        }

        pub(crate) fn total_starting_tablets(&self) -> i32 {
            self.g.total_starting_tablets
        }

        fn tablet(&self, id: &str) -> Tab {
            self.state()
                .tablet_index(id)
                .expect("a tablet of the table")
        }

        fn ts(&self, uuid: &str) -> Ts {
            self.g.index(uuid).expect("a known tserver")
        }

        /// The tablet's leader as the state has it.
        pub(crate) fn leader_of(&self, tablet: &str) -> Option<String> {
            let meta = self.state().tablet_meta(self.tablet(tablet)).ok()?;
            meta.leader.map(|l| self.g.uuid(l).to_string())
        }

        /// Whether the state counts the tserver as the tablet's leader.
        pub(crate) fn leads(&self, ts: &str, tablet: &str) -> bool {
            self.state()
                .ts_meta(self.ts(ts))
                .leaders
                .contains(&self.tablet(tablet))
        }

        fn last_action(&self, before: usize) -> Option<&Action> {
            (self.actions.len() > before)
                .then(|| self.actions.last())
                .flatten()
        }

        /// `HandleAddReplicas`: (tablet, from, to) of the add.
        pub(crate) fn add_replicas(&mut self) -> Result<Option<(String, String, String)>> {
            let before = self.actions.len();
            if !self.handle_add_replicas(0)? {
                return Ok(None);
            }
            match self.last_action(before) {
                Some(Action::AddReplica {
                    tablet_id,
                    from_ts,
                    ts_uuid,
                    ..
                }) => Ok(Some((tablet_id.clone(), from_ts.clone(), ts_uuid.clone()))),
                other => Err(format!("unexpected action {other:?}")),
            }
        }

        /// `HandleRemoveReplicas`: (tablet, from) of the remove.
        pub(crate) fn remove_replicas(&mut self) -> Result<Option<(String, String)>> {
            let before = self.actions.len();
            if !self.handle_remove_replicas(0)? {
                return Ok(None);
            }
            match self.last_action(before) {
                Some(Action::RemoveReplica {
                    tablet_id, ts_uuid, ..
                }) => Ok(Some((tablet_id.clone(), ts_uuid.clone()))),
                Some(Action::MoveLeader {
                    tablet_id,
                    from_ts,
                    also_remove_replica: true,
                    ..
                }) => Ok(Some((tablet_id.clone(), from_ts.clone()))),
                other => Err(format!("unexpected action {other:?}")),
            }
        }

        /// `HandleLeaderMoves`: (tablet, from, to) of the move.
        pub(crate) fn leader_moves(&mut self) -> Result<Option<(String, String, String)>> {
            let before = self.actions.len();
            if !self.handle_leader_moves(0)? {
                return Ok(None);
            }
            match self.last_action(before) {
                Some(Action::MoveLeader {
                    tablet_id,
                    from_ts,
                    to_ts,
                    ..
                }) => Ok(Some((tablet_id.clone(), from_ts.clone(), to_ts.clone()))),
                other => Err(format!("unexpected action {other:?}")),
            }
        }

        /// `HandleOneAddIfMissingPlacement`: (tablet, to), failing on any warning.
        pub(crate) fn one_add_if_missing_placement(&mut self) -> Result<Option<(String, String)>> {
            let before = self.actions.len();
            let mut remaining = 1;
            self.process_under_replicated_tablets(&[0], &mut remaining);
            if !self.g.activity.warnings.is_empty() {
                return Err("ProcessUnderReplicatedTablets hit an error".to_string());
            }
            match self.last_action(before) {
                None => Ok(None),
                Some(Action::AddReplica {
                    tablet_id, ts_uuid, ..
                }) => Ok(Some((tablet_id.clone(), ts_uuid.clone()))),
                other => Err(format!("unexpected action {other:?}")),
            }
        }

        pub(crate) fn add_if_missing_placement(&mut self, tablet: &str) -> Result<Option<String>> {
            let tablet = self.tablet(tablet);
            Ok(self
                .handle_add_if_missing_placement(0, tablet)?
                .map(|ts| self.g.uuid(ts).to_string()))
        }

        /// `ClusterLoadBalancer::RemoveReplica`.
        pub(crate) fn remove(&mut self, tablet: &str, ts: &str) -> Result<()> {
            let (tablet, ts) = (self.tablet(tablet), self.ts(ts));
            self.remove_replica(0, tablet, ts, "Remove replica for move".to_string())
        }

        pub(crate) fn can_add(&self, tablet: &str, to: &str, from: &str) -> Result<bool> {
            let from = (!from.is_empty()).then(|| self.ts(from));
            self.state().can_add_tablet_to_tablet_server(
                self.tablet(tablet),
                self.ts(to),
                from,
                &self.g,
            )
        }
    }
}
