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

//! `/api/v1/tablet-replication` and `/api/v1/tablet-under-replication`.

use master_tablet_locations::LeaderlessCheck;
use yb_proto::pb::catalog_entity_info::sys_tablets_entry_pb;
use yb_proto::pb::common_net::{CloudInfoPB, PlacementInfoPB, ReplicationInfoPB};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;

use crate::classify::TableRow;
use crate::json::{JsonWriter, Mode};
use crate::tservers::cluster_config;
use crate::{Args, View};

/// The running tables that are not system tables, by id (`GetTables(kRunning)`, `is_system`).
fn non_system_tables(view: &View) -> Vec<TableRow> {
    let snapshot = &*view.snapshot;
    let mut tables: Vec<TableRow> = snapshot
        .iter(SysRowEntryType::TABLE)
        .filter_map(TableRow::new)
        .filter(|t| t.is_running() && !t.is_system(snapshot))
        .collect();
    tables.sort_by(|a, b| a.id().cmp(b.id()));
    tables
}

/// `MasterPathHandlers::GetLeaderlessTablets`, counting only observable time
/// (`master_tablet_locations::LeaderlessCheck`).
pub(crate) fn leaderless_tablets(view: &View) -> Vec<(Vec<u8>, Vec<u8>, String)> {
    let snapshot = &*view.snapshot;
    let check = LeaderlessCheck {
        now: view.now,
        alert_delay: (view.options.leaderless_alert_delay)(),
        observable_from: view.observable_from,
        last_lease_report: &*view.last_lease_report,
    };
    let mut result = Vec::new();
    for table in non_system_tables(view) {
        let tablets: Vec<_> = table
            .tablets(snapshot)
            .into_iter()
            .filter(|t| {
                t.tablet()
                    .is_some_and(|h| h.state != sys_tablets_entry_pb::State::DELETED as i32)
            })
            .collect();
        let ids = tablets.iter().map(|t| &*t.key.id);
        for (tablet_id, gone) in view.locations.leaderless(ids, &check) {
            // The tablet's own table: a colocated table's tablets are its parent's.
            let owner = snapshot
                .get(SysRowEntryType::TABLET, &tablet_id)
                .and_then(|t| t.tablet().map(|h| h.table_id.clone()))
                .unwrap_or_else(|| table.id().to_vec());
            result.push((
                owner,
                tablet_id.to_vec(),
                format!(
                    "No valid leader reported for {} seconds",
                    crate::json::format_double(gone.as_secs_f64())
                ),
            ));
        }
    }
    result
}

/// `MasterPathHandlers::HandleGetReplicationStatus`.
pub(crate) fn tablet_replication(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.string("leaderless_tablets");
    jw.start_array();
    for (table_id, tablet_id, reason) in leaderless_tablets(view) {
        jw.start_object();
        jw.field("table_uuid", &table_id);
        jw.field("tablet_uuid", &tablet_id);
        jw.field("reason", &reason);
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}

/// `PlacementMissingReplicas`.
pub(crate) struct MissingReplicas {
    pub(crate) placement_uuid: Vec<u8>,
    pub(crate) is_live: bool,
    pub(crate) missing_replicas: i32,
    /// `FormatCloudInfoCompact` and how many replicas the block lacks.
    pub(crate) blocks: Vec<(String, i32)>,
}

/// An under-replicated tablet: its table, id, expected replicas and what each placement lacks.
pub(crate) struct UnderReplicated {
    pub(crate) table_id: Vec<u8>,
    pub(crate) tablet_id: Vec<u8>,
    pub(crate) expected: i32,
    pub(crate) missing: Vec<MissingReplicas>,
}

fn cloud_info_compact(ci: &CloudInfoPB) -> String {
    format!(
        "{}.{}.{}",
        ci.placement_cloud(),
        ci.placement_region(),
        ci.placement_zone()
    )
}

/// `GetTabletUnderReplicatedPlacements`.
fn under_replicated_placements(
    view: &View,
    tablet_id: &[u8],
    info: &ReplicationInfoPB,
) -> Vec<MissingReplicas> {
    let mut placements: Vec<PlacementInfoPB> = vec![info.live_replicas.clone().unwrap_or_default()];
    placements.extend(info.read_replicas.iter().cloned());
    let replicas = view
        .locations
        .get(tablet_id)
        .map(|r| r.replicas)
        .unwrap_or_default();
    for replica in replicas {
        // Config changes put replicas in UNKNOWN until they heartbeat; they count as running.
        if replica.state != RaftGroupStatePB::RUNNING && replica.state != RaftGroupStatePB::UNKNOWN
        {
            continue;
        }
        let Some(desc) = view.tservers.iter().find(|d| d.uuid == replica.ts_uuid) else {
            continue;
        };
        let ts_cloud = desc.pb.registration.cloud_info.get_or_default();
        for placement in placements
            .iter_mut()
            .filter(|p| p.placement_uuid() == desc.placement_uuid())
        {
            placement.set_num_replicas(placement.num_replicas() - 1);
            if let Some(block) = placement.placement_blocks.iter_mut().find(|b| {
                b.min_num_replicas() > 0
                    && master_ts_manager::placement::is_cloud_info_prefix(
                        b.cloud_info.get_or_default(),
                        ts_cloud,
                    )
            }) {
                block.set_min_num_replicas(block.min_num_replicas() - 1);
            }
        }
    }
    placements
        .iter()
        .enumerate()
        .filter_map(|(i, placement)| {
            let blocks: Vec<(String, i32)> = placement
                .placement_blocks
                .iter()
                .filter(|b| b.min_num_replicas() > 0)
                .map(|b| {
                    (
                        cloud_info_compact(b.cloud_info.get_or_default()),
                        b.min_num_replicas(),
                    )
                })
                .collect();
            (placement.num_replicas() > 0 || !blocks.is_empty()).then(|| MissingReplicas {
                placement_uuid: placement.placement_uuid().to_vec(),
                is_live: i == 0,
                missing_replicas: placement.num_replicas(),
                blocks,
            })
        })
        .collect()
}

/// `GetUnderReplicatedTablets`: tables that are not colocated children (they share their
/// parent's tablets and would follow the cluster's placement here).
pub(crate) fn under_replicated_tablets(view: &View) -> Vec<UnderReplicated> {
    let snapshot = &*view.snapshot;
    let config = cluster_config(snapshot).unwrap_or_default();
    let mut result = Vec::new();
    for table in non_system_tables(view) {
        if table.is_secondary() {
            continue;
        }
        let info = match view.tablespaces.as_ref() {
            Some(t) => t.table_replication_info_or_cluster(table.id(), &table.pb, &config),
            None => master_table_lifecycle::table_replication_info(
                table.pb.replication_info.as_ref(),
                &config,
            ),
        };
        let expected = info.live_replicas.num_replicas()
            + info
                .read_replicas
                .iter()
                .map(|r| r.num_replicas())
                .sum::<i32>();
        for tablet in table.active_tablets(snapshot) {
            let missing = under_replicated_placements(view, &tablet.key.id, &info);
            if !missing.is_empty() {
                result.push(UnderReplicated {
                    table_id: table.id().to_vec(),
                    tablet_id: tablet.key.id.to_vec(),
                    expected,
                    missing,
                });
            }
        }
    }
    result
}

/// `MasterPathHandlers::HandleGetUnderReplicationStatus`.
pub(crate) fn tablet_under_replication(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.string("underreplicated_tablets");
    jw.start_array();
    for tablet in under_replicated_tablets(view) {
        jw.start_object();
        jw.field("table_uuid", &tablet.table_id);
        jw.field("tablet_uuid", &tablet.tablet_id);
        jw.string("expected_num_replicas");
        jw.int(tablet.expected.into());
        jw.string("underreplicated_placements");
        jw.start_array();
        for placement in &tablet.missing {
            jw.string(&placement.placement_uuid);
        }
        jw.end_array();
        jw.string("missing_replicas");
        jw.start_array();
        for placement in &tablet.missing {
            jw.start_object();
            jw.field("placement_uuid", &placement.placement_uuid);
            jw.string("is_live");
            jw.bool(placement.is_live);
            jw.string("missing_replicas");
            jw.int(placement.missing_replicas.into());
            jw.string("placement_blocks");
            jw.start_array();
            for (cloud_info, missing) in &placement.blocks {
                jw.start_object();
                jw.field("cloud_info", cloud_info);
                jw.string("missing_replicas");
                jw.int((*missing).into());
                jw.end_object();
            }
            jw.end_array();
            jw.end_object();
        }
        jw.end_array();
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}
