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

use std::collections::HashSet;
use std::sync::Mutex;
use std::time::Duration;

use master_catalog_entity::EntityId;
use master_leadership::LeaderToken;
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
        if self.hide_only {
            req.set_hide_only(true);
        }
        req
    }

    /// Sends it to the tserver at `host:port`.
    pub async fn send(
        &self,
        rpc: &dyn RpcClient,
        host: &str,
        port: u16,
        timeout: Duration,
    ) -> Result<(), Status> {
        let bytes = self
            .request()
            .write_to_bytes()
            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
        let response = rpc
            .call(
                host,
                port,
                "yb.tserver.TabletServerAdminService",
                "DeleteTablet",
                &bytes,
                timeout,
            )
            .await?;
        let response = DeleteTabletResponsePB::parse_from_bytes(&response)
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        if let Some(error) = response.error.as_ref() {
            // Already gone is what was asked for.
            if error.code() != tablet_server_error_pb::Code::TABLET_NOT_FOUND {
                return Err(Status::new(
                    StatusCode::from_i32(error.status.code().value()),
                    error.status.message().to_string(),
                ));
            }
        }
        Ok(())
    }
}

/// Deletes waiting to be sent, deduplicated by tserver and tablet against both the queue and the
/// deletes being sent: after a large drop every report repeats the same deletes (CE-1629).
#[derive(Default)]
pub struct DeleteReplicaQueue {
    queue: Mutex<Vec<DeleteReplica>>,
    in_flight: Mutex<HashSet<(EntityId, Vec<u8>)>>,
    added: Notify,
}

impl DeleteReplicaQueue {
    /// Queues a delete unless one for the same replica is queued or being sent.
    pub fn push(&self, delete: DeleteReplica) {
        let mut queue = self.queue.lock().unwrap();
        if queue
            .iter()
            .any(|d| d.ts_uuid == delete.ts_uuid && d.tablet_id == delete.tablet_id)
            || self
                .in_flight
                .lock()
                .unwrap()
                .contains(&(delete.ts_uuid.clone(), delete.tablet_id.clone()))
        {
            return;
        }
        tracing::info!(
            "Queueing {:?} of tablet {} on tserver {}: {}",
            delete.delete_type,
            String::from_utf8_lossy(&delete.tablet_id),
            String::from_utf8_lossy(&delete.ts_uuid),
            delete.reason
        );
        queue.push(delete);
        self.added.notify_one();
    }

    /// Takes everything queued. Each one counts as being sent until [`DeleteReplicaQueue::sent`].
    pub fn drain(&self) -> Vec<DeleteReplica> {
        // Lock order as in push: queue, then in_flight.
        let mut queue = self.queue.lock().unwrap();
        let mut in_flight = self.in_flight.lock().unwrap();
        let drained = std::mem::take(&mut *queue);
        for delete in &drained {
            in_flight.insert((delete.ts_uuid.clone(), delete.tablet_id.clone()));
        }
        drained
    }

    /// A drained delete was sent or given up on: a later report may queue it again.
    pub fn sent(&self, delete: &DeleteReplica) {
        self.in_flight
            .lock()
            .unwrap()
            .remove(&(delete.ts_uuid.clone(), delete.tablet_id.clone()));
    }

    /// Drops everything queued: a new term decides again from reports.
    pub fn clear(&self) {
        self.queue.lock().unwrap().clear();
    }

    /// Resolves when something is queued.
    pub async fn wait(&self) {
        self.added.notified().await
    }

    /// How many are queued.
    pub fn len(&self) -> usize {
        self.queue.lock().unwrap().len()
    }

    /// Whether none are queued.
    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }
}
