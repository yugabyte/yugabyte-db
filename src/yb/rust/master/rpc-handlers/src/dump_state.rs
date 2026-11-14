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

//! `DumpState` (yb-admin dump_masters_state), served on every master: `CatalogManager::DumpState`
//! from this master's catalog snapshot, with the leader also collecting its peers' dumps
//! (`PeerStateDump`). Protobufs are printed in text format, as C++'s `ShortDebugString`.

use std::collections::BTreeSet;
use std::fmt::Write;

use master_catalog_state::CatalogSnapshot;
use protobuf::Message;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    SysClusterConfigEntryPB, SysNamespaceEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::master_cluster::{DumpMasterStateRequestPB, DumpMasterStateResponsePB};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::Shared;

fn short<M: protobuf::MessageFull>(pb: &M) -> String {
    protobuf::text_format::print_to_string(pb)
}

/// The text `CatalogManager::DumpState` writes.
pub(crate) fn dump(
    snapshot: &CatalogSnapshot,
    raft_config: Option<String>,
    on_disk: bool,
) -> String {
    let mut out = String::from("Dumping current state of master.\nNamespaces:\n");
    for ns in snapshot.iter(SysRowEntryType::NAMESPACE) {
        let Ok(pb) = ns.parse::<SysNamespaceEntryPB>() else {
            continue;
        };
        let _ = writeln!(out, "{}:", String::from_utf8_lossy(&ns.key.id));
        let _ = writeln!(
            out,
            "  name: \"{}\"",
            String::from_utf8_lossy(pb.name()).escape_default()
        );
        let _ = writeln!(out, "  metadata: {}", short(&pb));
    }
    out.push_str("Tables:\n");
    let mut listed = BTreeSet::new();
    for table in snapshot.iter(SysRowEntryType::TABLE) {
        let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
            continue;
        };
        let _ = writeln!(out, "{}:", String::from_utf8_lossy(&table.key.id));
        let ns_id = String::from_utf8_lossy(pb.namespace_id()).into_owned();
        let _ = writeln!(out, "  namespace id: \"{}\"", ns_id.escape_default());
        if let Some(ns) = snapshot
            .get(SysRowEntryType::NAMESPACE, pb.namespace_id())
            .and_then(|n| n.namespace())
        {
            let _ = writeln!(out, "  namespace name: \"{}\"", ns.name.escape_default());
        }
        let _ = writeln!(
            out,
            "  name: \"{}\"",
            String::from_utf8_lossy(pb.name()).escape_default()
        );
        let _ = writeln!(out, "  metadata: {}", short(&pb));
        out.push_str("  tablets:\n");
        for tablet in snapshot.hosting_tablets_of(&table.key.id) {
            let Ok(tablet_pb) = tablet.parse::<SysTabletsEntryPB>() else {
                continue;
            };
            let _ = writeln!(
                out,
                "    {}: {}",
                String::from_utf8_lossy(&tablet.key.id),
                short(&tablet_pb)
            );
            listed.insert(tablet.key.id.to_vec());
        }
    }
    let orphans: Vec<_> = snapshot
        .iter(SysRowEntryType::TABLET)
        .filter(|t| !listed.contains(t.key.id.as_ref()))
        .filter(|t| {
            // Inactive tablets are not in C++'s per-table lists either, but are in its tablet map.
            t.tablet()
                .is_some_and(|h| snapshot.get(SysRowEntryType::TABLE, &h.table_id).is_none())
        })
        .collect();
    if !orphans.is_empty() {
        out.push_str("Orphaned tablets (not referenced by any table):\n");
        for tablet in orphans {
            let Ok(pb) = tablet.parse::<SysTabletsEntryPB>() else {
                continue;
            };
            let _ = writeln!(
                out,
                "    {}: {}",
                String::from_utf8_lossy(&tablet.key.id),
                short(&pb)
            );
        }
    }
    if on_disk {
        if let Some(config) = raft_config {
            let _ = writeln!(out, "Current raft config: {config}");
        }
        if let Some(config) = snapshot
            .cluster_config()
            .and_then(|c| c.parse::<SysClusterConfigEntryPB>().ok())
        {
            let _ = writeln!(out, "Cluster config: {}", short(&config));
        }
    }
    out
}

/// `MasterClusterService::DumpState`.
pub(crate) async fn dump_state(
    shared: &Shared,
    req: &DumpMasterStateRequestPB,
) -> Result<DumpMasterStateResponsePB, Status> {
    let leader = shared
        .leader
        .as_ref()
        .ok_or_else(|| Status::new(StatusCode::IllegalState, "No catalog on this master"))?;
    let snapshot = leader.catalog.snapshot();
    let consensus = shared.info.committed_consensus_state().ok();
    let uuid = String::from_utf8_lossy(shared.info.instance().permanent_uuid()).into_owned();
    let role = if req.peers_also() {
        "Leader"
    } else {
        "Follower"
    };
    let title = format!("{role} Master {uuid}");
    let text = dump(&snapshot, consensus.as_ref().map(short), req.on_disk());
    let mut resp = DumpMasterStateResponsePB::new();
    if req.return_dump_as_string() {
        resp.set_dump(format!("{title}:\n{text}"));
    } else {
        tracing::info!("{title}");
        for line in text.lines() {
            tracing::info!("{line}");
        }
    }
    if req.peers_also() {
        // `PeerStateDump`: the other masters' dumps, appended in Raft config order.
        let mut peer_req = DumpMasterStateRequestPB::new();
        peer_req.set_on_disk(req.on_disk());
        peer_req.set_return_dump_as_string(req.return_dump_as_string());
        let bytes = peer_req
            .write_to_bytes()
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
        let peers = consensus
            .map(|c| c.config.peers.clone())
            .unwrap_or_default();
        tracing::info!(
            "Sending dump command to {} peers.",
            peers.len().saturating_sub(1)
        );
        let mut appended = String::new();
        for peer in peers
            .iter()
            .filter(|p| p.permanent_uuid() != uuid.as_bytes())
        {
            let Some(addr) = peer
                .last_known_private_addr
                .first()
                .or(peer.last_known_broadcast_addr.first())
            else {
                continue;
            };
            let response = shared
                .rpc
                .call(
                    addr.host(),
                    addr.port() as u16,
                    "yb.master.MasterService",
                    "DumpState",
                    &bytes,
                    (shared.options.master_rpc_timeout)(),
                )
                .await?;
            let peer_resp = DumpMasterStateResponsePB::parse_from_bytes(&response)
                .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
            if let Some(error) = peer_resp.error.as_ref() {
                return Err(Status::new(
                    StatusCode::from_i32(protobuf::Enum::value(&error.status.code())),
                    error.status.message().to_string(),
                ));
            }
            appended.push_str(peer_resp.dump());
        }
        if req.return_dump_as_string() {
            let dump = format!("{}{appended}", resp.dump());
            resp.set_dump(dump);
        }
    }
    Ok(resp)
}
