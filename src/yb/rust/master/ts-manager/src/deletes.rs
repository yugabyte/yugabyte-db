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

use std::collections::{BTreeSet, HashMap, HashSet};
use std::sync::Mutex;
use std::time::Duration;

use master_catalog_entity::EntityId;
use master_leadership::LeaderToken;
use master_task_runtime::Attempt;
use protobuf::{Enum, EnumOrUnknown, Message};
use tokio::sync::Notify;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::tablet_types::TabletDataState;
use yb_proto::pb::tserver_admin::{DeleteTabletRequestPB, DeleteTabletResponsePB};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

/// One `DeleteTablet` the leader owes a tserver.
#[derive(Clone, Debug, PartialEq)]
pub struct DeleteReplica {
    /// The leadership it was decided under; sent only while that token is current, since a later
    /// term or a restore may have changed what it was based on.
    pub token: LeaderToken,
    /// The tserver.
    pub ts_uuid: EntityId,
    /// The tablet.
    pub tablet_id: Vec<u8>,
    /// DELETED or TOMBSTONED.
    pub delete_type: TabletDataState,
    /// Only delete if the replica's committed config index is at most this.
    pub cas_config_index: Option<i64>,
    /// Hide rather than delete (a hidden tablet that the tserver still shows).
    pub hide_only: bool,
    /// Abort the tablet's active transactions: set for a dropped table, as `AsyncDeleteReplica`
    /// sets it once the table started deleting.
    pub abort_active_txns: bool,
    /// For the tserver's log.
    pub reason: String,
}

impl DeleteReplica {
    /// The request to send.
    pub fn request(&self) -> DeleteTabletRequestPB {
        let mut req = DeleteTabletRequestPB::new();
        req.set_dest_uuid(self.ts_uuid.to_vec());
        req.set_tablet_id(self.tablet_id.clone());
        req.set_reason(self.reason.clone());
        req.delete_type = Some(EnumOrUnknown::new(self.delete_type));
        if let Some(index) = self.cas_config_index {
            req.set_cas_config_opid_index_less_or_equal(index);
        }
        if self.abort_active_txns {
            req.set_should_abort_active_txns(true);
        }
        if self.hide_only {
            req.set_hide_only(true);
        }
        req
    }

    /// Sends it to the tserver at `host:port`. Like C++ `AsyncDeleteReplica`, a CAS failure or a
    /// wrong tserver uuid is final: the replica changed since the decision, and while a delete is
    /// being sent [`DeleteReplicaQueue`] holds back newer decisions for the same replica.
    pub async fn send(
        &self,
        rpc: &dyn RpcClient,
        host: &str,
        port: u16,
        timeout: Duration,
    ) -> Result<(), Attempt> {
        let bytes = self
            .request()
            .write_to_bytes()
            .map_err(|e| Attempt::Fail(Status::new(StatusCode::InternalError, e.to_string())))?;
        let response = rpc
            .call(
                host,
                port,
                "yb.tserver.TabletServerAdminService",
                "DeleteTablet",
                &bytes,
                timeout,
            )
            .await
            .map_err(Attempt::Retry)?;
        let response = DeleteTabletResponsePB::parse_from_bytes(&response)
            .map_err(|e| Attempt::Retry(Status::new(StatusCode::Corruption, e.to_string())))?;
        let Some(error) = response.error.as_ref() else {
            return Ok(());
        };
        let status = Status::new(
            StatusCode::from_i32(error.status.code().value()),
            error.status.message().to_string(),
        );
        match error.code() {
            // Already gone is what was asked for.
            tablet_server_error_pb::Code::TABLET_NOT_FOUND => Ok(()),
            tablet_server_error_pb::Code::CAS_FAILED
            | tablet_server_error_pb::Code::WRONG_SERVER_UUID => Err(Attempt::Fail(status)),
            _ => Err(Attempt::Retry(status)),
        }
    }
}

/// Deletes waiting to be sent, deduplicated by tserver and tablet against both the queue and the
/// deletes being sent: after a large drop every report repeats the same deletes (CE-1629).
#[derive(Default)]
pub struct DeleteReplicaQueue {
    state: Mutex<QueueState>,
    added: Notify,
}

/// A replica: tserver and tablet.
type ReplicaKey = (EntityId, Vec<u8>);

#[derive(Default)]
struct QueueState {
    queue: Vec<DeleteReplica>,
    /// Keys of `queue`, so a push after a large drop is O(1) rather than a scan.
    queued: HashSet<ReplicaKey>,
    in_flight: HashSet<ReplicaKey>,
}

fn key(delete: &DeleteReplica) -> ReplicaKey {
    (delete.ts_uuid.clone(), delete.tablet_id.clone())
}

impl DeleteReplicaQueue {
    /// Queues a delete unless one for the same replica is queued or being sent. A queued one is
    /// made to abort active transactions if the new one does.
    pub fn push(&self, delete: DeleteReplica) {
        let mut state = self.state.lock().unwrap();
        let key = key(&delete);
        if state.in_flight.contains(&key) {
            return;
        }
        if !state.queued.insert(key) {
            if delete.abort_active_txns {
                if let Some(queued) = state
                    .queue
                    .iter_mut()
                    .find(|q| q.ts_uuid == delete.ts_uuid && q.tablet_id == delete.tablet_id)
                {
                    queued.abort_active_txns = true;
                }
            }
            return;
        }
        tracing::info!(
            "Queueing {:?} of tablet {} on tserver {}: {}",
            delete.delete_type,
            String::from_utf8_lossy(&delete.tablet_id),
            String::from_utf8_lossy(&delete.ts_uuid),
            delete.reason
        );
        state.queue.push(delete);
        self.added.notify_one();
    }

    /// The deletes queued or being sent, by tserver: `TabletsPendingDeletion`, which the balancer
    /// avoids as destinations.
    pub fn pending(&self) -> HashMap<EntityId, BTreeSet<Vec<u8>>> {
        let state = self.state.lock().unwrap();
        let mut pending: HashMap<EntityId, BTreeSet<Vec<u8>>> = HashMap::new();
        for (ts, tablet) in state.queued.iter().chain(state.in_flight.iter()) {
            pending
                .entry(ts.clone())
                .or_default()
                .insert(tablet.clone());
        }
        pending
    }

    /// Takes everything queued. Each one counts as being sent until [`DeleteReplicaQueue::sent`].
    pub fn drain(&self) -> Vec<DeleteReplica> {
        let mut state = self.state.lock().unwrap();
        let drained = std::mem::take(&mut state.queue);
        let queued = std::mem::take(&mut state.queued);
        state.in_flight.extend(queued);
        drained
    }

    /// A drained delete was sent or given up on: a later report may queue it again.
    pub fn sent(&self, delete: &DeleteReplica) {
        self.state.lock().unwrap().in_flight.remove(&key(delete));
    }

    /// Drops everything queued: a new term decides again from reports.
    pub fn clear(&self) {
        let mut state = self.state.lock().unwrap();
        state.queue.clear();
        state.queued.clear();
    }

    /// Resolves when something is queued.
    pub async fn wait(&self) {
        self.added.notified().await
    }

    /// How many are queued.
    pub fn len(&self) -> usize {
        self.state.lock().unwrap().queue.len()
    }

    /// Whether none are queued.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}
