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

//! `/api/v1/cluster-config`, `/api/v1/stateful-services` and `/api/v1/xcluster`.

use protobuf::Enum;
use yb_proto::pb::catalog_entity_info::{
    SysCDCStreamEntryPB, SysUniverseReplicationEntryPB, SysXClusterConfigEntryPB,
    SysXClusterOutboundReplicationGroupEntryPB, XClusterSafeTimePB,
};
use yb_proto::pb::common_types::StatefulServiceKind;
use yb_proto::pb::master_replication::{
    GetReplicationStatusRequestPB, GetReplicationStatusResponsePB,
};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::classify::TableRow;
use crate::json::{enum_name, JsonWriter, Mode};
use crate::tables::rows_in_key_order;
use crate::tservers::cluster_config;
use crate::{Args, View};

/// `MasterPathHandlers::HandleGetClusterConfigJSON`.
pub(crate) fn cluster_config_json(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    match cluster_config(&view.snapshot) {
        Ok(config) => jw.protobuf(&config),
        Err(e) => {
            jw.start_object();
            jw.field("error", e.to_string());
            jw.end_object();
        }
    }
    jw.into_bytes()
}

/// A stateful service, the table hosting it, its first tablet and that tablet's leader
/// (`CatalogManager::GetStatefulServicesStatus`).
pub(crate) struct StatefulService {
    pub(crate) name: String,
    pub(crate) table_id: Vec<u8>,
    pub(crate) tablet_id: Vec<u8>,
    pub(crate) hosting_server: Vec<u8>,
}

pub(crate) fn stateful_services(view: &View) -> Vec<StatefulService> {
    let snapshot = &*view.snapshot;
    let mut services = Vec::new();
    for entity in rows_in_key_order(snapshot, SysRowEntryType::TABLE) {
        let Some(table) = TableRow::new(entity) else {
            continue;
        };
        for kind in &table.pb.hosted_stateful_services {
            let name = match StatefulServiceKind::from_i32(kind.value()) {
                Some(kind) => enum_name(kind),
                None => continue,
            };
            let tablet = table
                .active_tablets(snapshot)
                .first()
                .map(|t| t.key.id.to_vec());
            let hosting_server = tablet
                .as_ref()
                .and_then(|id| view.locations.get(id))
                .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
                .unwrap_or_default();
            services.push(StatefulService {
                name,
                table_id: table.id().to_vec(),
                tablet_id: tablet.unwrap_or_default(),
                hosting_server,
            });
        }
    }
    services
}

/// `MasterPathHandlers::HandleStatefulServicesJson`.
pub(crate) fn stateful_services_json(view: &View, _args: &Args) -> Vec<u8> {
    let mut jw = JsonWriter::new(Mode::Compact);
    jw.start_object();
    jw.string("stateful_services");
    jw.start_array();
    for service in stateful_services(view) {
        jw.start_object();
        jw.field("service_name", &service.name);
        jw.field("hosting_server", &service.hosting_server);
        jw.field("table_id", &service.table_id);
        jw.field("tablet_id", &service.tablet_id);
        jw.end_object();
    }
    jw.end_array();
    jw.end_object();
    jw.into_bytes()
}

/// `HybridTime::ToString`, not pretty.
pub(crate) fn hybrid_time_string(ht: u64) -> String {
    const LOGICAL_BITS: u32 = 12;
    match ht {
        u64::MAX => "<invalid>".into(),
        0xFFFF_FFFF_FFFF_FFFE => "<max>".into(),
        0 => "<min>".into(),
        1 => "<initial>".into(),
        _ => {
            let physical = ht >> LOGICAL_BITS;
            let logical = ht & ((1 << LOGICAL_BITS) - 1);
            if logical != 0 {
                format!("{{ physical: {physical} logical: {logical} }}")
            } else {
                format!("{{ physical: {physical} }}")
            }
        }
    }
}

/// `MasterPathHandlers::HandleGetXClusterJSON` (`XClusterManager::PopulateXClusterStatusJson`).
pub(crate) fn xcluster_json(view: &View, _args: &Args) -> Vec<u8> {
    xcluster(view, Mode::Compact, |req, resp| {
        (view.replication_status)(req, resp)
    })
}

/// The xCluster status in `mode`, with the target's replication status from `status`.
pub(crate) fn xcluster(
    view: &View,
    mode: Mode,
    status: impl Fn(&GetReplicationStatusRequestPB, &mut GetReplicationStatusResponsePB) -> bool,
) -> Vec<u8> {
    let snapshot = &*view.snapshot;
    let mut jw = JsonWriter::new(mode);
    jw.start_object();

    let xcluster_config: SysXClusterConfigEntryPB = snapshot
        .get(SysRowEntryType::XCLUSTER_CONFIG, b"")
        .and_then(|e| e.parse().ok())
        .unwrap_or_default();
    jw.string("xcluster_config");
    jw.protobuf(&xcluster_config);

    // XClusterSourceManager::PopulateXClusterStatusJson.
    jw.string("outbound_replication_groups");
    jw.start_array();
    for entity in rows_in_key_order(
        snapshot,
        SysRowEntryType::XCLUSTER_OUTBOUND_REPLICATION_GROUP,
    ) {
        let Ok(metadata) = entity.parse::<SysXClusterOutboundReplicationGroupEntryPB>() else {
            continue;
        };
        jw.start_object();
        jw.field("replication_group_id", &*entity.key.id);
        jw.string("metadata");
        jw.protobuf(&metadata);
        jw.end_object();
    }
    jw.end_array();

    jw.string("outbound_streams");
    jw.start_array();
    let mut streams: Vec<(Vec<u8>, Vec<u8>, SysCDCStreamEntryPB)> = Vec::new();
    for entity in snapshot.iter(SysRowEntryType::CDC_STREAM) {
        let Ok(stream) = entity.parse::<SysCDCStreamEntryPB>() else {
            continue;
        };
        // xCluster streams are per table; CDCSDK streams name a namespace.
        if !stream.namespace_id().is_empty() {
            continue;
        }
        for table_id in &stream.table_id {
            streams.push((
                table_id.as_bytes().to_vec(),
                entity.key.id.to_vec(),
                stream.clone(),
            ));
        }
    }
    streams.sort_by(|a, b| (&a.0, &a.1).cmp(&(&b.0, &b.1)));
    for (_, stream_id, stream) in &streams {
        jw.start_object();
        jw.field("stream_id", stream_id);
        jw.string("metadata");
        jw.protobuf(stream);
        jw.end_object();
    }
    jw.end_array();

    // XClusterTargetManager::PopulateXClusterStatusJson.
    let mut replication_status = GetReplicationStatusResponsePB::new();
    if !status(
        &GetReplicationStatusRequestPB::new(),
        &mut replication_status,
    ) {
        jw.field("error", "Unable to get the replication status");
        jw.end_object();
        return jw.into_bytes();
    }
    let cluster = match cluster_config(snapshot) {
        Ok(config) => config,
        Err(e) => {
            jw.field("error", e.to_string());
            jw.end_object();
            return jw.into_bytes();
        }
    };
    jw.string("replication_status");
    jw.protobuf(&replication_status);

    jw.string("replication_infos");
    jw.start_array();
    for entity in rows_in_key_order(snapshot, SysRowEntryType::UNIVERSE_REPLICATION) {
        if let Ok(info) = entity.parse::<SysUniverseReplicationEntryPB>() {
            jw.protobuf(&info);
        }
    }
    jw.end_array();

    let safe_times: XClusterSafeTimePB = snapshot
        .get(SysRowEntryType::XCLUSTER_SAFE_TIME, b"")
        .and_then(|e| e.parse().ok())
        .unwrap_or_default();
    if !safe_times.safe_time_map.is_empty() {
        let mut safe_times: Vec<_> = safe_times.safe_time_map.iter().collect();
        safe_times.sort();
        jw.string("safe_time_map");
        jw.start_array();
        for (namespace_id, ht) in safe_times {
            jw.start_object();
            jw.field("consumer_namespace_id", namespace_id);
            jw.field("safe_time", hybrid_time_string(*ht));
            jw.end_object();
        }
        jw.end_array();
    }

    jw.string("consumer_registry");
    jw.protobuf(cluster.consumer_registry.get_or_default());

    jw.end_object();
    jw.into_bytes()
}
