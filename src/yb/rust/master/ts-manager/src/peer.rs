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

//! The location stream (L9): the Rust leader pushes replica locations and tserver liveness to
//! Rust followers over `yb.master.MasterRustPeer`, so a Rust master elected later starts warm.
//! Best effort: a gap in the sequence, a failed push or a new term means a full push.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use master_task_runtime::TermScope;
use protobuf::Message;
use tokio::task::JoinSet;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::master_rust_peer::{
    PushLocationsRequestPB, PushLocationsResponsePB, TServerStatePB,
};

use crate::TsManager;

/// The service and method on the wire.
pub const SERVICE: &str = "yb.master.MasterRustPeer";
const METHOD: &str = "PushLocations";

/// Every location and tserver, replacing what the follower holds.
pub fn full_push(ts_manager: &TsManager, leader_uuid: &[u8], term: i64) -> PushLocationsRequestPB {
    let mut req = PushLocationsRequestPB::new();
    req.set_leader_uuid(leader_uuid.to_vec());
    req.set_term(term);
    req.set_full(true);
    for (tablet_id, replicas) in ts_manager.locations.all() {
        req.tablets
            .push(master_tablet_locations::TabletLocations::to_pb(
                &tablet_id,
                Some(&replicas),
            ));
    }
    req.tservers = tserver_states(ts_manager);
    req
}

fn tserver_states(ts_manager: &TsManager) -> Vec<TServerStatePB> {
    ts_manager
        .all()
        .iter()
        .map(|d| {
            let mut state = TServerStatePB::new();
            state.info = Some(d.info()).into();
            state.set_live(d.is_live());
            state
        })
        .collect()
}

/// What a follower last applied.
#[derive(Default)]
pub struct FollowerStreamState {
    term: i64,
    sequence: u64,
}

/// Applies a push on a follower. Asks for a full push on a gap; ignores a push from a term older
/// than the last one applied or than this master's own Raft term (`raft_term`): a deposed leader
/// that has not noticed yet. Refused while this master leads (`leading`), whose locations come
/// from its own heartbeats.
pub fn apply_push(
    ts_manager: &TsManager,
    state: &Mutex<FollowerStreamState>,
    req: &PushLocationsRequestPB,
    leading: bool,
    raft_term: i64,
) -> Result<PushLocationsResponsePB, Status> {
    if leading {
        return Err(Status::new(
            StatusCode::IllegalState,
            "This master is the leader and does not take pushed locations",
        ));
    }
    let resp = PushLocationsResponsePB::new();
    let mut state = state.lock().unwrap();
    if req.term() < state.term || req.term() < raft_term {
        return Ok(resp);
    }
    let mut resp = resp;
    if !req.full() && (req.term() != state.term || req.sequence() != state.sequence + 1) {
        resp.set_needs_full(true);
        return Ok(resp);
    }
    if req.full() {
        ts_manager.locations.clear();
    }
    for tablet in &req.tablets {
        ts_manager.locations.apply_pb(tablet);
    }
    if req.full() || !req.tservers.is_empty() {
        let infos: Vec<_> = req
            .tservers
            .iter()
            .map(|s| ((*s.info).clone(), s.live()))
            .collect();
        ts_manager.apply_peer_tservers(&infos, req.full());
    }
    state.term = req.term();
    state.sequence = req.sequence();
    Ok(resp)
}

/// A master peer: uuid and RPC address.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub struct Peer {
    /// Permanent uuid.
    pub uuid: Vec<u8>,
    /// RPC host.
    pub host: String,
    /// RPC port.
    pub port: u16,
}

#[derive(Default)]
struct PeerSendState {
    sequence: u64,
    needs_full: bool,
    /// A C++ master: it does not serve the stream.
    unsupported: bool,
    /// Pushes failed in a row, and when to try again.
    failures: u32,
    retry_at: Option<Instant>,
}

/// The longest wait before pushing to a peer whose pushes failed.
const MAX_BACKOFF: Duration = Duration::from_secs(30);

/// The leader side of the stream for one term.
pub struct LocationStream {
    ts_manager: Arc<TsManager>,
    rpc: Arc<dyn RpcClient>,
    leader_uuid: Vec<u8>,
    term: i64,
    peers: Mutex<HashMap<Vec<u8>, PeerSendState>>,
}

impl LocationStream {
    /// A stream for `term`.
    pub fn new(
        ts_manager: Arc<TsManager>,
        rpc: Arc<dyn RpcClient>,
        leader_uuid: Vec<u8>,
        term: i64,
    ) -> Arc<LocationStream> {
        Arc::new(LocationStream {
            ts_manager,
            rpc,
            leader_uuid,
            term,
            peers: Mutex::default(),
        })
    }

    /// Pushes every `interval` until the scope is cancelled, to every peer at once. `peers` lists
    /// the other masters. A peer whose push failed gets a full push, after a backoff.
    pub fn run(
        self: Arc<Self>,
        scope: &TermScope,
        peers: Arc<dyn Fn() -> Vec<Peer> + Send + Sync>,
        interval: fn() -> Duration,
        timeout: fn() -> Duration,
    ) {
        let mut membership = self.ts_manager.subscribe();
        scope.spawn("location-stream", async move {
            loop {
                let tservers_changed = membership.has_changed().unwrap_or(false);
                membership.borrow_and_update();
                let dirty = self.ts_manager.locations.take_dirty();
                let mut delta = PushLocationsRequestPB::new();
                delta.set_leader_uuid(self.leader_uuid.clone());
                delta.set_term(self.term);
                for (tablet_id, replicas) in &dirty {
                    delta
                        .tablets
                        .push(master_tablet_locations::TabletLocations::to_pb(
                            tablet_id,
                            replicas.as_ref(),
                        ));
                }
                if tservers_changed {
                    delta.tservers = tserver_states(&self.ts_manager);
                }
                let delta = Arc::new(delta);
                let mut full: Option<Arc<PushLocationsRequestPB>> = None;
                let mut pushes = JoinSet::new();
                for peer in peers() {
                    let Some((sequence, needs_full)) = self.next_push(&peer, &delta) else {
                        continue;
                    };
                    let payload = if needs_full {
                        full.get_or_insert_with(|| {
                            Arc::new(full_push(&self.ts_manager, &self.leader_uuid, self.term))
                        })
                        .clone()
                    } else {
                        delta.clone()
                    };
                    let this = self.clone();
                    let timeout = timeout();
                    pushes.spawn(async move {
                        this.push_to(&peer, &payload, sequence, timeout, interval())
                            .await
                    });
                }
                while pushes.join_next().await.is_some() {}
                tokio::time::sleep(interval()).await;
            }
        });
    }

    /// The sequence number and whether it must be a full push, or `None` to skip the peer now.
    fn next_push(&self, peer: &Peer, delta: &PushLocationsRequestPB) -> Option<(u64, bool)> {
        let mut peers = self.peers.lock().unwrap();
        let state = peers
            .entry(peer.uuid.clone())
            .or_insert_with(|| PeerSendState {
                needs_full: true,
                ..Default::default()
            });
        if state.unsupported || state.retry_at.is_some_and(|t| Instant::now() < t) {
            return None;
        }
        if !state.needs_full && delta.tablets.is_empty() && delta.tservers.is_empty() {
            return None;
        }
        state.sequence += 1;
        Some((state.sequence, state.needs_full))
    }

    async fn push_to(
        &self,
        peer: &Peer,
        payload: &PushLocationsRequestPB,
        sequence: u64,
        timeout: Duration,
        interval: Duration,
    ) {
        let mut req = payload.clone();
        req.set_sequence(sequence);
        let result = self.send(peer, &req, timeout).await;
        let mut peers = self.peers.lock().unwrap();
        let state = peers.entry(peer.uuid.clone()).or_default();
        match result {
            Ok(resp) => {
                state.needs_full = resp.needs_full();
                state.failures = 0;
                state.retry_at = None;
            }
            Err(status) if is_unknown_service(&status) => {
                tracing::info!(
                    "Master {} does not serve {SERVICE}; not pushing locations to it this term",
                    String::from_utf8_lossy(&peer.uuid)
                );
                state.unsupported = true;
            }
            Err(status) => {
                tracing::debug!("Pushing locations to {peer:?} failed: {status}");
                state.needs_full = true;
                let backoff = interval
                    .saturating_mul(1 << state.failures.min(6))
                    .min(MAX_BACKOFF);
                state.failures += 1;
                state.retry_at = Some(Instant::now() + backoff);
            }
        }
    }

    async fn send(
        &self,
        peer: &Peer,
        req: &PushLocationsRequestPB,
        timeout: Duration,
    ) -> Result<PushLocationsResponsePB, Status> {
        let bytes = req
            .write_to_bytes()
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
        let response = self
            .rpc
            .call(&peer.host, peer.port, SERVICE, METHOD, &bytes, timeout)
            .await?;
        PushLocationsResponsePB::parse_from_bytes(&response)
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))
    }
}

/// `ERROR_NO_SUCH_SERVICE` from a C++ master: "Service yb.master.MasterRustPeer not registered".
fn is_unknown_service(status: &Status) -> bool {
    status.message.contains(SERVICE) && status.message.contains("not registered")
}
