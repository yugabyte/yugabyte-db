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

//! `/api/v1/tablet-servers` and `/api/v1/health-check`.

use std::collections::HashMap;
use std::time::Duration;

use master_catalog_state::CatalogSnapshot;
use master_ts_manager::TsDescriptor;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tablets_entry_pb, SysClusterConfigEntryPB, SysObjectLockEntryPB,
};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::{PeerRole, TableType};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::wire_protocol::ServerRegistrationPB;

use crate::classify::{TableRow, SYSTEM_NAMESPACE_ID};
use crate::json::{fixed_point, humanize_bytes, JsonWriter, Mode};
use crate::{Args, View};

/// `kTserverAlive`, `kTserverDead`.
pub(crate) const ALIVE: &str = "ALIVE";
pub(crate) const DEAD: &str = "DEAD";

/// `CatalogManager::GetClusterConfig`.
pub(crate) fn cluster_config(
    snapshot: &CatalogSnapshot,
) -> Result<SysClusterConfigEntryPB, Status> {
    snapshot
        .cluster_config()
        .ok_or_else(|| Status::new(StatusCode::IllegalState, "No cluster config"))?
        .parse()
        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))
}

/// `CatalogManager::GetReplicationFactor`.
pub(crate) fn replication_factor(view: &View, config: &SysClusterConfigEntryPB) -> i32 {
    let configured = config.replication_info.live_replicas.num_replicas();
    if configured > 0 {
        configured
    } else {
        (view.options.replication_factor)()
    }
}

/// `GetPublicHttpHostPort`: the first HTTP address, on the first broadcast host if there is one.
pub(crate) fn public_http_host_port(reg: &ServerRegistrationPB) -> Option<HostPortPB> {
    let http = reg.http_addresses.first()?;
    let mut hp = http.clone();
    if let Some(broadcast) = reg.broadcast_addresses.first() {
        hp.set_host(broadcast.host().to_string());
    }
    Some(hp)
}

/// `HostPortPBToString`.
pub(crate) fn host_port_string(hp: &HostPortPB) -> String {
    if hp.host().contains(':') {
        format!("[{}]:{}", hp.host(), hp.port())
    } else {
        format!("{}:{}", hp.host(), hp.port())
    }
}

/// `GetHttpHostPortFromServerRegistration`.
pub(crate) fn http_host_port(reg: &ServerRegistrationPB) -> String {
    public_http_host_port(reg)
        .map(|hp| host_port_string(&hp))
        .unwrap_or_default()
}

/// `TSDescriptor::TimeSinceHeartbeat`. One never heard from counts from the epoch of `Instant`
/// in C++ (`MonoTime::kMin`); here, from when it was loaded, which is as meaningless.
pub(crate) fn time_since_heartbeat(view: &View, desc: &TsDescriptor) -> Duration {
    desc.last_heartbeat
        .map(|t| view.now.saturating_duration_since(t))
        .unwrap_or(Duration::MAX)
}

fn seconds(d: Duration) -> f64 {
    if d == Duration::MAX {
        // MonoTime::Now() - MonoTime::kMin.
        i64::MAX as f64 / 1e9
    } else {
        d.as_secs_f64()
    }
}

/// `TabletCounts` per tserver uuid.
#[derive(Clone, Copy, Debug, Default)]
pub(crate) struct TabletCounts {
    pub(crate) user_tablet_leaders: u32,
    pub(crate) user_tablet_followers: u32,
    pub(crate) system_tablet_leaders: u32,
    pub(crate) system_tablet_followers: u32,
    pub(crate) hidden_tablet_peers: u32,
}

/// `MasterPathHandlers::CalculateTabletMap`.
pub(crate) fn tablet_map(view: &View) -> HashMap<Vec<u8>, TabletCounts> {
    let snapshot = &*view.snapshot;
    let mut map: HashMap<Vec<u8>, TabletCounts> = HashMap::new();
    for entity in snapshot.iter(SysRowEntryType::TABLE) {
        let Some(table) = TableRow::new(entity) else {
            continue;
        };
        if !table.is_running() || table.is_secondary() {
            continue;
        }
        let is_user = table.is_user_created(snapshot) || table.is_colocation_parent();
        for tablet in table.tablets(snapshot) {
            let Some(hot) = tablet.tablet() else {
                continue;
            };
            if hot.state == sys_tablets_entry_pb::State::DELETED as i32 {
                continue;
            }
            let Some(replicas) = view.locations.get(&tablet.key.id) else {
                continue;
            };
            for replica in &replicas.replicas {
                let counts = map.entry(replica.ts_uuid.to_vec()).or_default();
                if hot.hidden {
                    counts.hidden_tablet_peers += 1;
                }
                let leader = replica.role == PeerRole::LEADER;
                match (is_user, leader) {
                    (true, true) => counts.user_tablet_leaders += 1,
                    (true, false) => counts.user_tablet_followers += 1,
                    (false, true) => counts.system_tablet_leaders += 1,
                    (false, false) => counts.system_tablet_followers += 1,
                }
            }
        }
    }
    map
}

fn error_object(jw: &mut JsonWriter, message: impl AsRef<[u8]>) {
    jw.start_object();
    jw.field("error", message);
}

/// `MasterPathHandlers::HandleGetTserverStatus`.
pub(crate) fn tablet_servers(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    let config = match cluster_config(&view.snapshot) {
        Ok(config) => config,
        Err(e) => {
            error_object(&mut jw, e.to_string());
            return jw.into_bytes();
        }
    };
    let tablet_map = tablet_map(view);
    let leases = (view.options.enable_ysql)() && (view.options.ysql_lease_enabled)();

    let mut placements: Vec<&[u8]> = vec![config.replication_info.live_replicas.placement_uuid()];
    for desc in &view.tservers {
        if !placements.contains(&desc.placement_uuid()) {
            placements.push(desc.placement_uuid());
        }
    }

    jw.start_object();
    for placement in placements {
        jw.string(placement);
        jw.start_object();
        for desc in view
            .tservers
            .iter()
            .filter(|d| d.placement_uuid() == placement)
        {
            tserver_status(view, desc, &tablet_map, leases, &mut jw);
        }
        jw.end_object();
    }
    jw.end_object();
    jw.into_bytes()
}

fn tserver_status(
    view: &View,
    desc: &TsDescriptor,
    tablet_map: &HashMap<Vec<u8>, TabletCounts>,
    leases: bool,
    jw: &mut JsonWriter,
) {
    let reg = desc.pb.registration.get_or_default();
    let metrics = desc.metrics.clone().unwrap_or_default();
    jw.string(http_host_port(reg));
    jw.start_object();

    let since = seconds(time_since_heartbeat(view, desc));
    jw.field("time_since_hb", format!("{}s", fixed_point(since, 1)));
    jw.string("time_since_hb_sec");
    jw.double(since);

    jw.field("status", if desc.is_live() { ALIVE } else { DEAD });
    jw.string("uptime_seconds");
    jw.uint(if desc.is_live() {
        metrics.uptime_seconds()
    } else {
        0
    });

    jw.string("has_faulty_drive");
    jw.bool(desc.has_faulty_drive);
    jw.string("start_time_us");
    jw.uint(desc.pb.start_time_us());

    let ram = metrics.total_ram_usage() as u64;
    jw.field("ram_used", humanize_bytes(ram));
    jw.string("ram_used_bytes");
    jw.uint(ram);
    jw.string("num_sst_files");
    jw.uint(metrics.num_sst_files());
    let sst = metrics.total_sst_file_size() as u64;
    jw.field("total_sst_file_size", humanize_bytes(sst));
    jw.string("total_sst_file_size_bytes");
    jw.uint(sst);
    let uncompressed = metrics.uncompressed_sst_file_size() as u64;
    jw.field("uncompressed_sst_file_size", humanize_bytes(uncompressed));
    jw.string("uncompressed_sst_file_size_bytes");
    jw.uint(uncompressed);

    jw.string("path_metrics");
    jw.start_array();
    for path in &metrics.path_metrics {
        jw.start_object();
        jw.field("path", path.path_id());
        jw.string("space_used");
        jw.uint(path.used_space());
        jw.string("total_space_size");
        jw.uint(path.total_space());
        if !path.storage_tier().is_empty() {
            jw.field("storage_tier", path.storage_tier());
        }
        jw.end_object();
    }
    jw.end_array();

    jw.string("read_ops_per_sec");
    jw.double(metrics.read_ops_per_sec());
    jw.string("write_ops_per_sec");
    jw.double(metrics.write_ops_per_sec());

    let counts = tablet_map.get(&*desc.uuid);
    let c = counts.copied().unwrap_or_default();
    jw.string("user_tablets_total");
    jw.uint((c.user_tablet_leaders + c.user_tablet_followers).into());
    jw.string("user_tablets_leaders");
    jw.uint(c.user_tablet_leaders.into());
    jw.string("system_tablets_total");
    jw.uint((c.system_tablet_leaders + c.system_tablet_followers).into());
    jw.string("system_tablets_leaders");
    jw.uint(c.system_tablet_leaders.into());
    jw.string("active_tablets");
    jw.int(if counts.is_some() {
        desc.num_live_replicas.into()
    } else {
        0
    });

    let cloud = reg.cloud_info.get_or_default();
    jw.field("cloud", cloud.placement_cloud());
    jw.field("region", cloud.placement_region());
    jw.field("zone", cloud.placement_zone());
    jw.field("permanent_uuid", &*desc.uuid);

    if leases {
        lease_info(view, desc, jw);
    }
    jw.end_object();
}

/// The `lease_info` of a tserver whose lease row is for its current instance.
fn lease_info(view: &View, desc: &TsDescriptor, jw: &mut JsonWriter) {
    let Some(row) = view
        .snapshot
        .get(SysRowEntryType::OBJECT_LOCK_ENTRY, &desc.uuid)
    else {
        return;
    };
    let Ok(entry) = row.parse::<SysObjectLockEntryPB>() else {
        return;
    };
    let lease = entry.lease_info.get_or_default();
    if lease.instance_seqno() != desc.pb.instance_seqno() {
        return;
    }
    let deadline = (view.lease_deadline)(&desc.uuid);
    jw.string("lease_info");
    jw.start_object();
    jw.string("is_live");
    jw.bool(lease.live_lease());
    jw.string("lease_expiry_sec");
    jw.double(deadline.saturating_duration_since(view.now).as_secs_f64());
    jw.string("lease_epoch");
    jw.uint(lease.lease_epoch());
    jw.end_object();
}

const METRICS_SNAPSHOTS_TABLE: &str = "metrics";

/// `MasterPathHandlers::HandleHealthCheck`.
pub(crate) fn health_check(view: &View, args: &Args) -> Vec<u8> {
    let snapshot = &*view.snapshot;
    let mut jw = JsonWriter::new(Mode::Compact);
    let config = match cluster_config(snapshot) {
        Ok(config) => config,
        Err(e) => {
            error_object(&mut jw, e.to_string());
            return jw.into_bytes();
        }
    };
    let replication_factor = replication_factor(view, &config);
    let live_placement = config.replication_info.live_replicas.placement_uuid();

    let mut dead_nodes: Vec<&TsDescriptor> = Vec::new();
    let mut most_recent_uptime = u64::MAX;
    for desc in view
        .tservers
        .iter()
        .filter(|d| d.placement_uuid() == live_placement)
    {
        if !desc.is_live() {
            dead_nodes.push(desc);
        } else {
            let uptime = desc.metrics.as_ref().map_or(0, |m| m.uptime_seconds());
            most_recent_uptime = most_recent_uptime.min(uptime);
        }
    }

    jw.start_object();
    jw.string("dead_nodes");
    jw.start_array();
    for desc in &dead_nodes {
        jw.string(&*desc.uuid);
    }
    jw.end_array();
    jw.string("most_recent_uptime");
    jw.uint(most_recent_uptime);

    // atoi.
    let death_interval_ms: i64 = args
        .get("tserver_death_interval_msecs")
        .map(atoi)
        .unwrap_or(0);

    jw.string("under_replicated_tablets");
    jw.start_array();
    let mut tables: Vec<TableRow> = snapshot
        .iter(SysRowEntryType::TABLE)
        .filter_map(TableRow::new)
        .filter(TableRow::is_running)
        .collect();
    tables.sort_by(|a, b| a.id().cmp(b.id()));
    for table in &tables {
        let table_type = table.pb.table_type();
        let counted = table.is_user_table(snapshot)
            || table_type == TableType::REDIS_TABLE_TYPE
            || table_type == TableType::TRANSACTION_STATUS_TABLE_TYPE
            || (table.pb.namespace_id() == SYSTEM_NAMESPACE_ID
                && table.pb.name() == METRICS_SNAPSHOTS_TABLE.as_bytes());
        if !counted {
            continue;
        }
        for tablet in table.active_tablets(snapshot) {
            let replicas = view
                .locations
                .get(&tablet.key.id)
                .map(|r| r.replicas)
                .unwrap_or_default();
            if (replicas.len() as i64) < i64::from(replication_factor) {
                jw.string(&*tablet.key.id);
                continue;
            }
            if dead_nodes.is_empty() {
                continue;
            }
            let recent = replicas
                .iter()
                .filter(|r| {
                    !dead_nodes.iter().any(|d| {
                        d.uuid == r.ts_uuid
                            && i64::try_from(time_since_heartbeat(view, d).as_millis())
                                .unwrap_or(i64::MAX)
                                > death_interval_ms
                    })
                })
                .count();
            if (recent as i64) < i64::from(replication_factor) {
                jw.string(&*tablet.key.id);
            }
        }
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}

/// C `atoi`: leading whitespace, a sign, then digits; 0 when there are none.
pub(crate) fn atoi(s: &str) -> i64 {
    let s = s.trim_start();
    let (negative, digits) = match s.as_bytes().first() {
        Some(b'-') => (true, &s[1..]),
        Some(b'+') => (false, &s[1..]),
        _ => (false, s),
    };
    let value = digits
        .bytes()
        .take_while(u8::is_ascii_digit)
        .fold(0i64, |acc, d| {
            acc.wrapping_mul(10).wrapping_add(i64::from(d - b'0'))
        });
    if negative {
        -value
    } else {
        value
    }
}
