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

//! The JSON of the pages the C++ master only renders as HTML: home (`/api/v1/overview`),
//! `/api/v1/load-distribution`, `/api/v1/tasks` and `/api/v1/tablet-server-clocks`. A Rust master
//! alone serves these; their shapes are the UI's (`ui/src/api.ts`), not a C++ contract.

use std::collections::{BTreeMap, HashSet};

use master_cluster_balancer::Action;
use master_ts_manager::TsDescriptor;
use yb_proto::pb::catalog_entity_info::{SysClusterConfigEntryPB, SysObjectLockEntryPB};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::classify::{namespace_name, TableRow};
use crate::json::{JsonWriter, Mode};
use crate::tservers::{
    cluster_config, http_host_port, replication_factor, tablet_map, time_since_heartbeat,
    TabletCounts,
};
use crate::{Args, View};

/// The tservers whose RPC address is on the server blacklist (`TSDescriptor::IsBlacklisted`).
fn blacklisted(config: &SysClusterConfigEntryPB, desc: &TsDescriptor) -> bool {
    let blacklist: HashSet<(String, u32)> =
        master_ts_manager::placement::blacklist_addresses(config);
    let reg = desc.pb.registration.get_or_default();
    reg.private_rpc_addresses
        .iter()
        .chain(reg.broadcast_addresses.iter())
        .any(|hp| blacklist.contains(&(hp.host().to_string(), hp.port())))
}

/// `ShouldHideTserverNodeFromDisplay`.
fn hidden_from_display(view: &View, desc: &TsDescriptor) -> bool {
    let threshold = (view.options.hide_dead_node_threshold_mins)();
    threshold > 0
        && !desc.is_live()
        && time_since_heartbeat(view, desc).as_secs() / 60 > threshold as u64
}

/// `PlacementClusterTabletCounts`.
#[derive(Default)]
struct PlacementCounts {
    counts: TabletCounts,
    live: u32,
    blacklisted: u32,
    dead: u32,
    active_tablet_peers: i64,
    limit: Option<i64>,
}

/// `ComputeAggregatedClusterInfo` and `ComputeTabletReplicaLimit`: None for no limit.
fn tablet_replica_limit(
    view: &View,
    config: &SysClusterConfigEntryPB,
    placement: &[u8],
) -> Option<i64> {
    let mut cores: Option<i64> = Some(0);
    let mut memory: Option<i64> = Some(0);
    for desc in &view.tservers {
        if !desc.is_live() || blacklisted(config, desc) || desc.placement_uuid() != placement {
            continue;
        }
        let resources = desc.pb.resources.get_or_default();
        cores = cores.zip(resources.core_count).map(|(a, b)| a + b);
        memory = memory
            .zip(resources.tablet_overhead_ram_in_bytes)
            .map(|(a, b)| a + b);
    }
    let per_core = i64::from((view.options.tablet_replicas_per_core_limit)());
    let per_gib = i64::from((view.options.tablet_replicas_per_gib_limit)());
    let mut limit = match cores {
        Some(cores) if per_core > 0 => per_core * cores,
        _ => i64::MAX,
    };
    if let Some(memory) = memory.filter(|_| per_gib > 0) {
        let by_memory = (per_gib as f64 * (memory as f64 / (1u64 << 30) as f64)).trunc();
        limit = limit.min(by_memory.round() as i64);
    }
    // ToUnsignedOrNullopt.
    (limit != i64::MAX && limit >= 0).then_some(limit)
}

/// `MasterPathHandlers::CalculateUniverseTabletCounts`, by placement uuid.
fn placement_counts(
    view: &View,
    config: &SysClusterConfigEntryPB,
) -> BTreeMap<Vec<u8>, PlacementCounts> {
    let tablets = tablet_map(view);
    let mut placements: BTreeMap<Vec<u8>, PlacementCounts> = BTreeMap::new();
    for desc in &view.tservers {
        if hidden_from_display(view, desc) {
            continue;
        }
        let counts = placements
            .entry(desc.placement_uuid().to_vec())
            .or_default();
        if let Some(c) = tablets.get(&*desc.uuid) {
            counts.counts.user_tablet_leaders += c.user_tablet_leaders;
            counts.counts.user_tablet_followers += c.user_tablet_followers;
            counts.counts.system_tablet_leaders += c.system_tablet_leaders;
            counts.counts.system_tablet_followers += c.system_tablet_followers;
            counts.counts.hidden_tablet_peers += c.hidden_tablet_peers;
        }
        if blacklisted(config, desc) {
            counts.blacklisted += 1;
        } else if desc.is_live() {
            counts.live += 1;
        } else {
            counts.dead += 1;
        }
        counts.active_tablet_peers += i64::from(desc.num_live_replicas);
    }
    for (placement, counts) in placements.iter_mut() {
        counts.limit = tablet_replica_limit(view, config, placement);
    }
    placements
}

fn optional_int(jw: &mut JsonWriter, value: Option<i64>) {
    match value {
        Some(v) => jw.int(v),
        None => jw.null(),
    }
}

/// `/api/v1/overview`: what the home page and the tablet servers' universe summary show.
pub(crate) fn overview(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    let config = match cluster_config(&view.snapshot) {
        Ok(config) => config,
        Err(e) => {
            jw.start_object();
            jw.field("error", e.to_string());
            jw.end_object();
            return jw.into_bytes();
        }
    };
    let live_placement = config.replication_info.live_replicas.placement_uuid();
    let placements = placement_counts(view, &config);

    jw.start_object();
    jw.field("universe_uuid", config.universe_uuid());
    jw.field("cluster_uuid", config.cluster_uuid());
    jw.string("cluster_config_version");
    jw.int(config.version().into());
    jw.string("replication_factor");
    jw.int(replication_factor(view, &config).into());
    let live = view.tservers.iter().filter(|d| d.is_live()).count();
    jw.string("live_tservers");
    jw.uint(live as u64);
    jw.string("dead_tservers");
    jw.uint((view.tservers.len() - live) as u64);
    jw.string("load_balancer_enabled");
    jw.bool(view.balancer.enabled);
    jw.string("is_load_balanced");
    if view.balancer.enabled {
        jw.bool(view.balancer.idle);
    } else {
        jw.null();
    }
    let encryption = config.encryption_info.get_or_default();
    jw.field(
        "encryption_at_rest",
        if encryption.encryption_enabled() {
            format!("Enabled with key: {}", encryption.latest_version_id())
        } else {
            "Disabled".to_string()
        },
    );

    let primary = placements.get(live_placement);
    jw.string("tablet_limits");
    jw.start_object();
    jw.string("tablet_peer_limit");
    optional_int(&mut jw, primary.and_then(|p| p.limit));
    jw.string("tablet_peers");
    jw.int(primary.map_or(0, |p| p.active_tablet_peers));
    jw.string("enforced");
    jw.bool((view.options.enforce_tablet_replica_limits)());
    jw.end_object();

    // MasterPathHandlers::DisplayUniverseSummary.
    jw.string("placements");
    jw.start_array();
    for (placement, counts) in &placements {
        jw.start_object();
        jw.field("placement_uuid", placement);
        jw.string("is_primary");
        jw.bool(placement.as_slice() == live_placement);
        jw.string("live_tservers");
        jw.uint(counts.live.into());
        jw.string("blacklisted_tservers");
        jw.uint(counts.blacklisted.into());
        jw.string("dead_tservers");
        jw.uint(counts.dead.into());
        let c = &counts.counts;
        jw.string("user_tablet_peers");
        jw.uint((c.user_tablet_leaders + c.user_tablet_followers).into());
        jw.string("system_tablet_peers");
        jw.uint((c.system_tablet_leaders + c.system_tablet_followers).into());
        jw.string("hidden_tablet_peers");
        jw.uint(c.hidden_tablet_peers.into());
        jw.string("active_tablet_peers");
        jw.int(counts.active_tablet_peers);
        jw.string("tablet_peer_limit");
        optional_int(&mut jw, counts.limit);
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}

/// The `namespace.table` of a table id, as the C++ pages name it.
fn table_label(view: &View, table_id: &[u8]) -> (String, String) {
    view.snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(TableRow::new)
        .map(|t| {
            (
                namespace_name(&view.snapshot, t.pb.namespace_id()),
                String::from_utf8_lossy(t.pb.name()).into_owned(),
            )
        })
        .unwrap_or_default()
}

/// A balancer action as the C++ task it becomes describes itself (`AsyncTryStepDown`,
/// `AsyncAddServerTask`, `AsyncRemoveServerTask`).
fn action_description(view: &View, action: &Action) -> (&'static str, String) {
    let table_name = |table_id: &str| {
        let (_, name) = table_label(view, table_id.as_bytes());
        format!("{name} [id={table_id}]")
    };
    match action {
        Action::AddReplica {
            table_id,
            tablet_id,
            ts_uuid,
            reason,
            ..
        } => (
            "AddServer ChangeConfig",
            format!(
                "AddServer ChangeConfig RPC for tablet {tablet_id} ({}) on peer {ts_uuid}. \
                 Reason: {reason}",
                table_name(table_id)
            ),
        ),
        Action::RemoveReplica {
            table_id,
            tablet_id,
            ts_uuid,
            reason,
        } => (
            "RemoveServer ChangeConfig",
            format!(
                "RemoveServer ChangeConfig RPC for tablet {tablet_id} ({}) on peer {ts_uuid}. \
                 Reason: {reason}",
                table_name(table_id)
            ),
        ),
        Action::MoveLeader {
            table_id,
            tablet_id,
            from_ts,
            reason,
            ..
        } => (
            "Stepdown Leader",
            format!(
                "Stepdown Leader RPC for tablet {tablet_id} ({}) on peer {from_ts}. Reason: \
                 {reason}",
                table_name(table_id)
            ),
        ),
    }
}

/// `/api/v1/load-distribution`: the balancer's last run and the replicas of every table on every
/// tserver (`MasterPathHandlers::HandleLoadBalancer`).
pub(crate) fn load_distribution(view: &View, _args: &Args) -> Vec<u8> {
    let snapshot = &*view.snapshot;
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();

    // Fetching remote bootstraps from every tserver is not done yet.
    jw.string("ongoing_remote_bootstraps");
    jw.start_array();
    jw.end_array();

    let activities = &view.balancer.activities;
    // ClusterBalancerActivityInfo::GetWarningsSummary: by type, over the run's placements.
    let mut warnings: BTreeMap<String, (String, usize)> = BTreeMap::new();
    for activity in activities {
        for (warning, count) in &activity.warnings {
            let entry = warnings
                .entry(format!("{warning:?}"))
                .or_insert_with(|| (count.example_message.clone(), 0));
            entry.1 += count.count;
        }
    }
    jw.string("warnings_summary");
    jw.start_array();
    for (example, count) in warnings.values() {
        jw.start_object();
        jw.field("example_message", example);
        jw.string("count");
        jw.uint(*count as u64);
        jw.end_object();
    }
    jw.end_array();

    // GetTasksSummary: by type. The balancer records what it sent, not how it ended.
    let mut tasks: BTreeMap<&'static str, (String, usize)> = BTreeMap::new();
    for action in activities.iter().flat_map(|a| a.actions.iter()) {
        let (kind, description) = action_description(view, action);
        tasks.entry(kind).or_insert((description, 0)).1 += 1;
    }
    jw.string("tasks_summary");
    jw.start_array();
    for (description, count) in tasks.values() {
        jw.start_object();
        jw.field("example_description", description);
        jw.field("state", "kScheduled");
        jw.string("count");
        jw.uint(*count as u64);
        jw.field("example_status", "");
        jw.end_object();
    }
    jw.end_array();

    jw.string("tablet_distribution");
    jw.start_object();
    jw.string("tservers");
    jw.start_array();
    for desc in &view.tservers {
        let reg = desc.pb.registration.get_or_default();
        let cloud = reg.cloud_info.get_or_default();
        jw.start_object();
        jw.field("uuid", &*desc.uuid);
        jw.field("host_port", http_host_port(reg));
        jw.field("cloud", cloud.placement_cloud());
        jw.field("region", cloud.placement_region());
        jw.field("zone", cloud.placement_zone());
        jw.field("placement_uuid", desc.placement_uuid());
        jw.string("alive");
        jw.bool(desc.is_live());
        jw.end_object();
    }
    jw.end_array();

    jw.string("tables");
    jw.start_array();
    // MasterPathHandlers::CalculateTServerTree and RenderLoadBalancerViewPanel: running user
    // tables and indexes, their tablets not deleted (hidden split parents included).
    let mut tables: Vec<TableRow> = snapshot
        .iter(SysRowEntryType::TABLE)
        .filter_map(TableRow::new)
        .filter(|t| t.is_running() && !t.is_secondary() && t.is_user_created(snapshot))
        .filter(|t| {
            matches!(
                crate::classify::table_category(snapshot, t),
                crate::classify::TableCategory::User | crate::classify::TableCategory::Index
            )
        })
        .collect();
    tables.sort_by(|a, b| a.id().cmp(b.id()));
    for table in &tables {
        let mut counts: BTreeMap<Vec<u8>, (u32, u32)> = BTreeMap::new();
        let mut tablet_count = 0u64;
        for tablet in table.tablets(snapshot) {
            if tablet.tablet().is_none_or(|h| {
                h.state
                    == yb_proto::pb::catalog_entity_info::sys_tablets_entry_pb::State::DELETED
                        as i32
            }) {
                continue;
            }
            let replicas = view
                .locations
                .get(&tablet.key.id)
                .map(|r| r.replicas)
                .unwrap_or_default();
            if !replicas.is_empty() {
                tablet_count += 1;
            }
            for replica in replicas {
                let entry = counts.entry(replica.ts_uuid.to_vec()).or_default();
                entry.0 += 1;
                if replica.role == yb_proto::pb::common_types::PeerRole::LEADER {
                    entry.1 += 1;
                }
            }
        }
        jw.start_object();
        jw.field("table_id", table.id());
        jw.field(
            "keyspace",
            namespace_name(snapshot, table.pb.namespace_id()),
        );
        jw.field("table_name", table.pb.name());
        jw.string("tablet_count");
        jw.uint(tablet_count);
        jw.string("counts");
        jw.start_array();
        for (uuid, (replicas, leaders)) in &counts {
            jw.start_object();
            jw.field("uuid", uuid);
            jw.string("replicas");
            jw.uint((*replicas).into());
            jw.string("leaders");
            jw.uint((*leaders).into());
            jw.end_object();
        }
        jw.end_array();
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();

    jw.end_object();
    jw.into_bytes()
}

/// `/api/v1/tasks`: the leader's running tasks by name.
pub(crate) fn tasks(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.string("running");
    jw.start_array();
    for (name, count) in &view.running_tasks {
        jw.start_object();
        jw.field("name", name);
        jw.string("count");
        jw.uint(*count as u64);
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}

/// The YSQL lease column of the C++ tablet servers page: `state` is `N/A` (leases off),
/// `NO LEASE` (none for this instance), `RELINQUISHED`, `EXPIRED` or `LIVE`; `seconds` is the time
/// to the deadline, or since it for an expired lease.
fn ysql_lease(view: &View, desc: &TsDescriptor, jw: &mut JsonWriter) {
    jw.string("ysql_lease");
    jw.start_object();
    if !(view.options.enable_ysql)() || !(view.options.ysql_lease_enabled)() {
        jw.field("state", "N/A");
        jw.end_object();
        return;
    }
    let lease = view
        .snapshot
        .get(SysRowEntryType::OBJECT_LOCK_ENTRY, &desc.uuid)
        .and_then(|row| row.parse::<SysObjectLockEntryPB>().ok())
        .map(|entry| entry.lease_info.unwrap_or_default())
        .filter(|lease| lease.instance_seqno() == desc.pb.instance_seqno());
    let Some(lease) = lease else {
        jw.field("state", "NO LEASE");
        jw.end_object();
        return;
    };
    let deadline = (view.lease_deadline)(&desc.uuid);
    let (state, seconds) = if lease.lease_relinquished() {
        ("RELINQUISHED", 0.0)
    } else if !lease.live_lease() {
        (
            "EXPIRED",
            view.now.saturating_duration_since(deadline).as_secs_f64(),
        )
    } else {
        (
            "LIVE",
            deadline.saturating_duration_since(view.now).as_secs_f64(),
        )
    };
    jw.field("state", state);
    jw.string("epoch");
    jw.uint(lease.lease_epoch());
    jw.string("seconds");
    jw.double(seconds);
    jw.end_object();
}

/// `/api/v1/tablet-server-clocks`: when each tserver last heartbeated, and its YSQL lease.
pub(crate) fn tablet_server_clocks(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.string("tservers");
    jw.start_array();
    for desc in &view.tservers {
        jw.start_object();
        jw.field("uuid", &*desc.uuid);
        jw.field(
            "host_port",
            http_host_port(desc.pb.registration.get_or_default()),
        );
        jw.string("time_since_hb_sec");
        let since = time_since_heartbeat(view, desc);
        jw.double(if since == std::time::Duration::MAX {
            -1.0
        } else {
            since.as_secs_f64()
        });
        ysql_lease(view, desc, &mut jw);
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}
