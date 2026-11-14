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

//! `AreNodesSafeToTakeDown` (C++'s `TabletHealthManager`): every tablet with a replica on a node
//! being taken down must keep a majority of healthy replicas (leaders, and followers lagging less
//! than the bound) on the other nodes, which are asked for their replicas' health.

use std::collections::{BTreeMap, BTreeSet, HashMap};

use master_catalog_state::CatalogSnapshot;
use protobuf::Message;
use tokio::task::JoinSet;
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, SysTablesEntryPB};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::RaftPeerPB;
use yb_proto::pb::master_admin::AreNodesSafeToTakeDownRequestPB;
use yb_proto::pb::master_cluster::{
    CheckMasterTabletHealthRequestPB, CheckMasterTabletHealthResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::tserver::{
    CheckTserverTabletHealthRequestPB, CheckTserverTabletHealthResponsePB,
};

use crate::placement::{cluster_config, num_replicas};
use crate::{error, TableLifecycle};

/// `kSysCatalogTabletId`.
const SYS_CATALOG_TABLET_ID: &[u8] = b"00000000000000000000000000000000";

/// Which replicas each answering node reports healthy.
type Healthy = (Vec<u8>, Vec<Vec<u8>>);

impl TableLifecycle {
    /// `AreNodesSafeToTakeDown`. `masters` is the sys catalog's Raft config; `deadline` is the
    /// caller's, which the answer comes a buffer ahead of so the caller gets a readable error.
    pub async fn are_nodes_safe_to_take_down(
        &self,
        snapshot: &CatalogSnapshot,
        masters: &[RaftPeerPB],
        req: &AreNodesSafeToTakeDownRequestPB,
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        let buffer = (self.options.nodes_safe_to_take_down_buffer)();
        tracing::info!(
            "Processing AreNodesSafeToTakeDown call: {}",
            crate::namespaces::short_debug(req)
        );
        let deadline = deadline.checked_sub(buffer).unwrap_or(deadline);
        if Instant::now() > deadline {
            return Err(error(
                Code::INTERNAL_ERROR,
                StatusCode::TimedOut,
                "Adjusted deadline (deadline minus buffer time) has already been exceeded  before \
                 starting AreNodesSafeToTakeDown check.",
            ));
        }
        let rpc = self
            .tablet_rpc
            .get()
            .map(|r| r.rpc.clone())
            .ok_or_else(|| {
                error(
                    Code::INTERNAL_ERROR,
                    StatusCode::ServiceUnavailable,
                    "The tserver RPC client is not set",
                )
            })?;
        let config = cluster_config(snapshot);
        let replication_factor = (self.options.replication_factor)();
        // Healthy replicas each tablet still needs.
        let mut required: HashMap<Vec<u8>, usize> = HashMap::new();

        // `FindMastersToContact`. C++ takes the majority of the cluster's replication factor, not
        // of the master count.
        let mut masters_to_contact = Vec::new();
        if !req.master_uuids.is_empty() {
            let rf = num_replicas(
                &config
                    .replication_info
                    .live_replicas
                    .clone()
                    .unwrap_or_default(),
                replication_factor,
            );
            required.insert(SYS_CATALOG_TABLET_ID.to_vec(), rf / 2 + 1);
            masters_to_contact = masters
                .iter()
                .filter(|p| !req.master_uuids.contains(&p.permanent_uuid().to_vec()))
                .cloned()
                .collect();
        }

        // `FindTserversToContact`: the running tables that own tablets.
        let down: BTreeSet<&[u8]> = req.tserver_uuids.iter().map(|u| u.as_slice()).collect();
        let mut to_check: BTreeMap<Vec<u8>, Vec<Vec<u8>>> = BTreeMap::new();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else { continue };
            use sys_tables_entry_pb::State;
            if hot.state != State::RUNNING as i32 && hot.state != State::ALTERING as i32 {
                continue;
            }
            let tablets = snapshot.active_tablets_of(&table.key.id);
            let mut min_replicas = None;
            for tablet in tablets {
                let replicas = self
                    .ts_manager
                    .locations
                    .get(&tablet.key.id)
                    .map(|r| r.replicas)
                    .unwrap_or_default();
                if !replicas.iter().any(|r| down.contains(&*r.ts_uuid)) {
                    continue;
                }
                let min = match min_replicas {
                    Some(min) => min,
                    None => {
                        let pb: SysTablesEntryPB = table.parse().map_err(|e| {
                            error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                        })?;
                        let info = self.tablespaces.table_replication_info_or_cluster(
                            &table.key.id,
                            &pb,
                            &config,
                        );
                        let rf = num_replicas(
                            &info.live_replicas.clone().unwrap_or_default(),
                            replication_factor,
                        );
                        *min_replicas.insert(rf / 2 + 1)
                    }
                };
                required.insert(tablet.key.id.to_vec(), min);
                for replica in &replicas {
                    if !down.contains(&*replica.ts_uuid) {
                        to_check
                            .entry(replica.ts_uuid.to_vec())
                            .or_default()
                            .push(tablet.key.id.to_vec());
                    }
                }
            }
        }

        let lag_bound = req.follower_lag_bound_ms();
        let healthy = move |role: PeerRole, lag: i64| {
            role == PeerRole::LEADER || (role == PeerRole::FOLLOWER && lag < lag_bound)
        };
        let mut calls: JoinSet<Option<Healthy>> = JoinSet::new();
        let outstanding = masters_to_contact.len() + to_check.len();
        for (ts_uuid, tablets) in to_check {
            let Some((host, port)) = self.ts_manager.rpc_address(&ts_uuid) else {
                continue;
            };
            let rpc = rpc.clone();
            calls.spawn(async move {
                let mut req = CheckTserverTabletHealthRequestPB::new();
                req.tablet_ids = tablets;
                let bytes = req.write_to_bytes().ok()?;
                let resp = call_until(deadline, || {
                    rpc.call(
                        &host,
                        port,
                        "yb.tserver.TabletServerService",
                        "CheckTserverTabletHealth",
                        &bytes,
                        deadline.saturating_duration_since(Instant::now()),
                    )
                })
                .await;
                let resp = CheckTserverTabletHealthResponsePB::parse_from_bytes(&resp).ok()?;
                if resp.error.is_some() {
                    return Some((ts_uuid, Vec::new()));
                }
                let ok = resp
                    .tablet_healths
                    .iter()
                    .filter(|t| healthy(t.role(), t.follower_lag_ms()))
                    .map(|t| t.tablet_id().to_vec())
                    .collect();
                Some((ts_uuid, ok))
            });
        }
        for master in masters_to_contact {
            let rpc = rpc.clone();
            calls.spawn(async move {
                let addr = master
                    .last_known_private_addr
                    .first()
                    .or(master.last_known_broadcast_addr.first())?;
                let bytes = CheckMasterTabletHealthRequestPB::new()
                    .write_to_bytes()
                    .ok()?;
                let resp = call_until(deadline, || {
                    rpc.call(
                        addr.host(),
                        addr.port() as u16,
                        "yb.master.MasterService",
                        "CheckMasterTabletHealth",
                        &bytes,
                        deadline.saturating_duration_since(Instant::now()),
                    )
                })
                .await;
                let resp = CheckMasterTabletHealthResponsePB::parse_from_bytes(&resp).ok()?;
                let ok = (resp.error.is_none() && healthy(resp.role(), resp.follower_lag_ms()))
                    .then(|| SYS_CATALOG_TABLET_ID.to_vec())
                    .into_iter()
                    .collect();
                Some((master.permanent_uuid().to_vec(), ok))
            });
        }
        // Until every node answered, every tablet has its majority, or the deadline.
        let mut answered = 0;
        let mut finished = outstanding == 0 || required.is_empty();
        while !finished {
            match tokio::time::timeout_at(deadline, calls.join_next()).await {
                Ok(Some(result)) => {
                    answered += 1;
                    if let Ok(Some((_, tablets))) = result {
                        for tablet in tablets {
                            if let Some(n) = required.get_mut(&tablet) {
                                *n -= 1;
                                if *n == 0 {
                                    required.remove(&tablet);
                                }
                            }
                        }
                    }
                    finished = answered == outstanding || required.is_empty();
                }
                Ok(None) => finished = true,
                Err(_) => break,
            }
        }
        calls.abort_all();
        if let Some((tablet_id, missing)) = required.iter().next() {
            let mut message = if finished {
                String::new()
            } else {
                "Some nodes did not respond (they may already be down) and were treated as \
                 unavailable. "
                    .to_string()
            };
            message.push_str(&format!(
                "Taking down the requested node(s) is unsafe because {} tablet(s) would not have \
                 enough replicas for quorum. For example, tablet {} will have {missing} fewer {} \
                 than needed for quorum.",
                required.len(),
                String::from_utf8_lossy(tablet_id),
                if *missing == 1 { "replica" } else { "replicas" }
            ));
            return Err(error(
                Code::INTERNAL_ERROR,
                StatusCode::IllegalState,
                message,
            ));
        }
        Ok(())
    }
}

/// The call's response, retried as `RetryingRpcTask` does until the deadline. A node that never
/// answers is never counted as answering, so the check waits for the deadline and says some
/// nodes did not respond.
async fn call_until<F>(deadline: Instant, call: impl Fn() -> F) -> Vec<u8>
where
    F: std::future::Future<Output = Result<Vec<u8>, yb_host_api::Status>>,
{
    let mut backoff = std::time::Duration::from_millis(50);
    loop {
        if let Ok(bytes) = call().await {
            return bytes;
        }
        if Instant::now() + backoff >= deadline {
            return std::future::pending().await;
        }
        tokio::time::sleep(backoff).await;
        backoff = (backoff * 2).min(std::time::Duration::from_secs(1));
    }
}
