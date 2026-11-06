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

//! `ListMasters`, as `Master::ListMasters` and `GetMasterEntryForHosts` do it in C++: ask every
//! Raft peer for its registration, each over all of its addresses at once, and take the first
//! answer per peer.

use protobuf::{Enum, Message};
use tokio::task::JoinSet;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::RaftPeerPB;
use yb_proto::pb::master_cluster::{
    GetMasterRegistrationRequestPB, GetMasterRegistrationResponsePB, ListMastersResponsePB,
};
use yb_proto::pb::master_types::master_error_pb;
use yb_proto::pb::wire_protocol::{app_status_pb, AppStatusPB, NodeInstancePB, ServerEntryPB};

use std::sync::Arc;
use std::time::Duration;

use yb_host_api::rpc::RpcClient;

use crate::{master_error_from, MasterInfo, Shared};

pub(crate) async fn list_masters(shared: &Shared) -> ListMastersResponsePB {
    list_masters_response(
        shared.info.as_ref(),
        shared.rpc.clone(),
        (shared.options.master_rpc_timeout)(),
    )
    .await
}

/// The `ListMasters` response. Also what the host's web pages show.
pub async fn list_masters_response(
    info: &dyn MasterInfo,
    rpc: Arc<dyn RpcClient>,
    timeout: Duration,
) -> ListMastersResponsePB {
    let mut resp = ListMastersResponsePB::new();
    match list_master_entries(info, rpc, timeout).await {
        Ok(entries) => resp.masters = entries,
        Err(status) => {
            resp.error = Some(master_error_from(
                master_error_pb::Code::UNKNOWN_ERROR,
                &status,
            ))
            .into()
        }
    }
    resp
}

/// The masters of the sys-catalog Raft config, each with its registration and role as it reports
/// them, or with an error when it cannot be reached. In shell mode, only this master.
async fn list_master_entries(
    info: &dyn MasterInfo,
    rpc: Arc<dyn RpcClient>,
    timeout: Duration,
) -> Result<Vec<ServerEntryPB>, Status> {
    if info.is_shell_mode() {
        let mut entry = ServerEntryPB::new();
        entry.instance_id = Some(info.instance()).into();
        entry.registration = Some(info.registration()?).into();
        entry.set_role(PeerRole::NON_PARTICIPANT);
        return Ok(vec![entry]);
    }
    let state = info.committed_consensus_state()?;
    if state.config.is_none() {
        return Err(Status::new(StatusCode::NotFound, "No raft config found."));
    }
    let mut peers = JoinSet::new();
    for (index, peer) in state.config.peers.iter().enumerate() {
        let peer = peer.clone();
        let rpc = rpc.clone();
        peers.spawn(async move { (index, peer_entry(rpc, peer, timeout).await) });
    }
    let mut entries: Vec<(usize, ServerEntryPB)> = peers.join_all().await;
    entries.sort_by_key(|(index, _)| *index);
    Ok(entries.into_iter().map(|(_, entry)| entry).collect())
}

fn addresses(peer: &RaftPeerPB) -> Vec<(String, u16)> {
    peer.last_known_private_addr
        .iter()
        .chain(peer.last_known_broadcast_addr.iter())
        .map(|hp| (hp.host().to_string(), hp.port() as u16))
        .collect()
}

async fn peer_entry(rpc: Arc<dyn RpcClient>, peer: RaftPeerPB, timeout: Duration) -> ServerEntryPB {
    let addrs = addresses(&peer);
    let request = GetMasterRegistrationRequestPB::new()
        .write_to_bytes()
        .expect("empty request serializes");
    let mut calls = JoinSet::new();
    for (host, port) in addrs.clone() {
        let rpc = rpc.clone();
        let request = request.clone();
        calls.spawn(async move {
            let bytes = rpc
                .call(
                    &host,
                    port,
                    "yb.master.MasterService",
                    "GetMasterRegistration",
                    &request,
                    timeout,
                )
                .await?;
            let resp = GetMasterRegistrationResponsePB::parse_from_bytes(&bytes)
                .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
            match resp.error.as_ref() {
                Some(error) => Err(Status::new(
                    StatusCode::from_i32(error.status.code().value()),
                    error.status.message().to_string(),
                )),
                None => Ok(resp),
            }
        });
    }

    let mut last_error = Status::new(StatusCode::InvalidArgument, "peer has no addresses");
    while let Some(result) = calls.join_next().await {
        match result {
            Ok(Ok(resp)) => {
                let mut entry = ServerEntryPB::new();
                entry.instance_id = resp.instance_id;
                entry.registration = resp.registration;
                entry.role = resp.role;
                return entry;
            }
            Ok(Err(status)) => last_error = status,
            Err(e) => last_error = Status::new(StatusCode::InternalError, e.to_string()),
        }
    }

    // As in C++: report what is known about an unreachable peer, plus the error.
    let addrs_text = addrs
        .iter()
        .map(|(host, port)| format!("{host}:{port}"))
        .collect::<Vec<_>>()
        .join(", ");
    let mut entry = ServerEntryPB::new();
    let mut error = AppStatusPB::new();
    error.set_code(
        app_status_pb::ErrorCode::from_i32(last_error.code as i32)
            .unwrap_or(app_status_pb::ErrorCode::UNKNOWN_ERROR),
    );
    error.set_message(format!(
        "Unable to get registration information for peer ([{addrs_text}]) id ({}): {}",
        String::from_utf8_lossy(peer.permanent_uuid()),
        last_error.message
    ));
    entry.error = Some(error).into();
    let mut instance = NodeInstancePB::new();
    instance.set_permanent_uuid(peer.permanent_uuid().to_vec());
    instance.set_instance_seqno(0);
    entry.instance_id = Some(instance).into();
    let registration = entry.registration.mut_or_insert_default();
    registration.private_rpc_addresses = peer.last_known_private_addr.clone();
    registration.broadcast_addresses = peer.last_known_broadcast_addr.clone();
    entry
}
