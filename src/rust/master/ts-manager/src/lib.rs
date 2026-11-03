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

//! The tserver registry and the leader's side of `TSHeartbeat`. See docs/ts-manager.md.
//!
//! Registration, liveness and replica locations are observed state: they live in memory, and are
//! written to the sys catalog only where the C++ master persists them (`persist_tserver_registry`).
//! The heartbeat response is assembled from parts with one owner each (L7): this crate's own
//! fields plus [`HeartbeatContributor`]s.

mod deletes;
mod heartbeat;
mod peer;
pub mod placement;
mod report;

pub use deletes::{DeleteReplica, DeleteReplicaQueue};
pub use heartbeat::{master_error, ContributorContext, HeartbeatContext, HeartbeatContributor};
pub use peer::{
    apply_push, full_push, FollowerStreamState, LocationStream, Peer, SERVICE as PEER_SERVICE,
};
pub use report::consensus_role;

use std::collections::{BTreeMap, HashSet};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::time::{Duration, Instant};

use master_catalog_entity::EntityId;
use master_catalog_state::{is_inactive_state, CatalogSnapshot, CatalogWriter, Mutation};
use master_leadership::LeaderToken;
use master_tablet_locations::TabletLocations;
use protobuf::{EnumOrUnknown, Message};
use tokio::sync::watch;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{sys_tablet_server_entry_pb, SysTabletServerEntryPB};
use yb_proto::pb::master_heartbeat::{LiveTServersVersionPB, TSInformationPB};
use yb_proto::pb::master_types::{SysRowEntryType, TSRegistrationPB, TServerMetricsPB};
use yb_proto::pb::wire_protocol::NodeInstancePB;

pub use sys_tablet_server_entry_pb::{State as TsState, XClusterGuardedLeaseState};

/// Settings, read when used so runtime flag changes take effect.
#[derive(Clone, Copy)]
pub struct TsManagerOptions {
    /// `tserver_unresponsive_timeout_ms`.
    pub unresponsive_timeout: fn() -> Duration,
    /// `tablet_report_limit`.
    pub tablet_report_limit: fn() -> i32,
    /// `heartbeat_rpc_timeout_ms` x `heartbeat_safe_deadline_ratio`: report processing stops this
    /// long before the caller's deadline.
    pub report_safety_margin: fn() -> Duration,
    /// The `persist_tserver_registry` AutoFlag.
    pub persist_registry: fn() -> bool,
    /// The `master_enable_universe_uuid_heartbeat_check` AutoFlag.
    pub universe_uuid_check: fn() -> bool,
    /// `xcluster_guarded_lease_duration_ms` plus twice `max_clock_skew_usec`.
    pub guarded_lease_expiry: fn() -> Duration,
    /// `master_tombstone_evicted_tablet_replicas`.
    pub tombstone_evicted_replicas: fn() -> bool,
    /// The `use_tablet_report_pending_config_op_id` AutoFlag.
    pub use_pending_config_op_id: fn() -> bool,
    /// `enable_register_ts_from_raft`.
    pub register_from_raft: fn() -> bool,
}

impl Default for TsManagerOptions {
    fn default() -> Self {
        TsManagerOptions {
            unresponsive_timeout: || Duration::from_secs(60),
            tablet_report_limit: || 1000,
            report_safety_margin: || Duration::from_millis(3000),
            persist_registry: || false,
            universe_uuid_check: || false,
            guarded_lease_expiry: || Duration::from_secs(121),
            tombstone_evicted_replicas: || true,
            use_pending_config_op_id: || true,
            register_from_raft: || true,
        }
    }
}

/// What the master knows about one tserver.
#[derive(Clone, Debug)]
pub struct TsDescriptor {
    /// The permanent uuid.
    pub uuid: EntityId,
    /// The persistent part, as `SysTabletServerEntryPB` (also when not persisted).
    pub pb: SysTabletServerEntryPB,
    /// The last heartbeat; `None` for one loaded from the catalog and not heard from yet.
    pub last_heartbeat: Option<Instant>,
    /// A full report was completed since registration (or since the term began).
    pub has_tablet_report: bool,
    /// The sequence number of the full report in progress.
    pub receiving_full_report_seq_no: Option<i32>,
    /// The highest report sequence number seen.
    pub latest_report_seqno: i32,
    /// From the last heartbeat.
    pub num_live_replicas: i32,
    /// From the last heartbeat.
    pub leader_count: i32,
    /// From the last heartbeat.
    pub metrics: Option<TServerMetricsPB>,
    /// The row last written for it, to skip unchanged writes.
    persisted_row: Option<Vec<u8>>,
}

impl TsDescriptor {
    fn new(uuid: EntityId) -> TsDescriptor {
        TsDescriptor {
            uuid,
            pb: SysTabletServerEntryPB::new(),
            last_heartbeat: None,
            has_tablet_report: false,
            receiving_full_report_seq_no: None,
            latest_report_seqno: i32::MIN,
            num_live_replicas: 0,
            leader_count: 0,
            metrics: None,
            persisted_row: None,
        }
    }

    /// The state.
    pub fn state(&self) -> TsState {
        self.pb.state()
    }

    /// `TSDescriptor::IsLive`.
    pub fn is_live(&self) -> bool {
        self.state() == TsState::LIVE
    }

    /// `{permanent_uuid, instance_seqno}`.
    pub fn instance(&self) -> NodeInstancePB {
        let mut instance = NodeInstancePB::new();
        instance.set_permanent_uuid(self.uuid.to_vec());
        instance.set_instance_seqno(self.pb.instance_seqno());
        instance
    }

    /// `{common, resources}`.
    pub fn registration(&self) -> TSRegistrationPB {
        let mut registration = TSRegistrationPB::new();
        registration.common = self.pb.registration.clone();
        registration.resources = self.pb.resources.clone();
        registration
    }

    /// `TSDescriptor::GetTSInformationPB`.
    pub fn info(&self) -> TSInformationPB {
        let mut info = TSInformationPB::new();
        info.registration = Some(self.registration()).into();
        info.tserver_instance = Some(self.instance()).into();
        info
    }

    /// The placement uuid it registered with.
    pub fn placement_uuid(&self) -> &[u8] {
        self.pb.registration.placement_uuid()
    }

    fn set_state(&mut self, state: TsState) {
        self.pb.state = Some(EnumOrUnknown::new(state));
    }
}

/// The tserver registry.
pub struct TsManager {
    descriptors: RwLock<BTreeMap<EntityId, TsDescriptor>>,
    /// Bumped whenever the live list or a registration in it changes.
    membership: AtomicU64,
    membership_changed: watch::Sender<u64>,
    /// Tservers sent the universe key registry since they registered.
    sent_key_registry: Mutex<HashSet<EntityId>>,
    live_cache: Mutex<Option<(u64, Arc<Vec<TSInformationPB>>)>>,
    /// Heartbeats answered with `live_tservers_unchanged`.
    pub unchanged_responses: AtomicU64,
    /// Observed replica locations.
    pub locations: Arc<TabletLocations>,
    /// Replica deletes the report processing asks for.
    pub deletes: DeleteReplicaQueue,
    /// Tservers whose row could not be written yet; the liveness controller retries them.
    unpersisted: Mutex<HashSet<EntityId>>,
    /// CDC streams were seen: `CatalogManager::cdc_enabled_`, which stays set.
    cdc_enabled: AtomicBool,
    options: TsManagerOptions,
}

impl TsManager {
    /// An empty registry.
    pub fn new(locations: Arc<TabletLocations>, options: TsManagerOptions) -> Arc<TsManager> {
        Arc::new(TsManager {
            descriptors: RwLock::default(),
            membership: AtomicU64::new(1),
            membership_changed: watch::channel(1).0,
            sent_key_registry: Mutex::default(),
            live_cache: Mutex::default(),
            unchanged_responses: AtomicU64::new(0),
            locations,
            deletes: DeleteReplicaQueue::default(),
            unpersisted: Mutex::default(),
            cdc_enabled: AtomicBool::new(false),
            options,
        })
    }

    /// The settings.
    pub fn options(&self) -> &TsManagerOptions {
        &self.options
    }

    /// One tserver.
    pub fn get(&self, uuid: &[u8]) -> Option<TsDescriptor> {
        self.descriptors.read().unwrap().get(uuid).cloned()
    }

    /// Every tserver not replaced, in uuid order (`GetAllDescriptors`).
    pub fn all(&self) -> Vec<TsDescriptor> {
        self.descriptors
            .read()
            .unwrap()
            .values()
            .filter(|d| d.state() != TsState::REPLACED)
            .cloned()
            .collect()
    }

    /// Every live tserver, in uuid order (`GetAllLiveDescriptors`).
    pub fn live(&self) -> Vec<TsDescriptor> {
        self.descriptors
            .read()
            .unwrap()
            .values()
            .filter(|d| d.is_live())
            .cloned()
            .collect()
    }

    /// Changes whenever the live list changes.
    pub fn subscribe(&self) -> watch::Receiver<u64> {
        self.membership_changed.subscribe()
    }

    /// The live list and its version in `term` (L8). Rebuilt only when membership changed.
    pub fn live_list(&self, term: i64) -> (LiveTServersVersionPB, Arc<Vec<TSInformationPB>>) {
        let counter = self.membership.load(Ordering::Acquire);
        let mut version = LiveTServersVersionPB::new();
        version.set_term(term);
        version.set_counter(counter);
        let mut cache = self.live_cache.lock().unwrap();
        if let Some((cached, list)) = cache.as_ref() {
            if *cached == counter {
                return (version, list.clone());
            }
        }
        let list = Arc::new(
            self.live()
                .iter()
                .map(TsDescriptor::info)
                .collect::<Vec<_>>(),
        );
        *cache = Some((counter, list.clone()));
        (version, list)
    }

    fn bump_membership(&self) {
        let v = self.membership.fetch_add(1, Ordering::AcqRel) + 1;
        self.membership_changed.send_replace(v);
    }

    /// Whether the tserver should get the universe key registry now (`ShouldResendRegistry`).
    pub fn take_key_registry_send(&self, uuid: &[u8], registering: bool) -> bool {
        let mut sent = self.sent_key_registry.lock().unwrap();
        let first = sent.insert(uuid.into());
        first || registering
    }

    /// A new term: forget report state so every tserver sends a full report, give every
    /// registered tserver a heartbeat grace period, load persisted registrations, drop locations of
    /// tablets no longer active and deletes decided in an earlier term.
    pub fn on_leader_ready(&self, snapshot: &CatalogSnapshot) {
        self.deletes.clear();
        for (tablet_id, _) in self.locations.all() {
            let active = snapshot
                .get(SysRowEntryType::TABLET, &tablet_id)
                .and_then(|t| t.tablet())
                .is_some_and(|t| !is_inactive_state(t.state));
            if !active {
                self.locations.remove(&tablet_id);
            }
        }
        let now = Instant::now();
        let mut descriptors = self.descriptors.write().unwrap();
        if (self.options.persist_registry)() {
            for entity in snapshot.iter(SysRowEntryType::TSERVER_REGISTRATION) {
                let Ok(pb) = entity.parse::<SysTabletServerEntryPB>() else {
                    continue;
                };
                let descriptor = descriptors
                    .entry(entity.key.id.clone())
                    .or_insert_with(|| TsDescriptor::new(entity.key.id.clone()));
                if descriptor.pb.instance_seqno() <= pb.instance_seqno() {
                    descriptor.persisted_row = Some(entity.raw.to_vec());
                    descriptor.pb = pb;
                }
            }
        }
        for descriptor in descriptors.values_mut() {
            descriptor.has_tablet_report = false;
            descriptor.receiving_full_report_seq_no = None;
            descriptor.last_heartbeat = Some(now);
        }
        drop(descriptors);
        self.sent_key_registry.lock().unwrap().clear();
        self.locations.mark_all_unconfirmed();
        self.bump_membership();
    }

    /// Whether CDC streams exist or existed while this process led (`xcluster_enabled_on_producer`).
    pub fn cdc_enabled(&self, snapshot: &CatalogSnapshot) -> bool {
        if !self.cdc_enabled.load(Ordering::Relaxed)
            && snapshot.count(SysRowEntryType::CDC_STREAM) > 0
        {
            self.cdc_enabled.store(true, Ordering::Relaxed);
        }
        self.cdc_enabled.load(Ordering::Relaxed)
    }

    /// `MarkUnresponsiveTServers`: LIVE tservers not heard from in the timeout become
    /// UNRESPONSIVE and must send a full report when they come back. Returns the changed uuids.
    pub fn mark_unresponsive(&self, now: Instant) -> Vec<EntityId> {
        let timeout = (self.options.unresponsive_timeout)();
        let lease_expiry = (self.options.guarded_lease_expiry)();
        let mut changed = Vec::new();
        let mut descriptors = self.descriptors.write().unwrap();
        let mut membership = false;
        for descriptor in descriptors.values_mut() {
            let Some(last) = descriptor.last_heartbeat else {
                continue;
            };
            let since = now.saturating_duration_since(last);
            if descriptor.is_live() && since > timeout {
                tracing::info!(
                    "Tablet server {} unresponsive for {since:?}",
                    String::from_utf8_lossy(&descriptor.uuid)
                );
                descriptor.set_state(TsState::UNRESPONSIVE);
                descriptor.has_tablet_report = false;
                changed.push(descriptor.uuid.clone());
                membership = true;
            }
            if descriptor.pb.xcluster_guarded_lease_state()
                != XClusterGuardedLeaseState::DEFINITELY_NO_LEASE
                && since > lease_expiry
            {
                descriptor.pb.xcluster_guarded_lease_state = Some(EnumOrUnknown::new(
                    XClusterGuardedLeaseState::DEFINITELY_NO_LEASE,
                ));
                changed.push(descriptor.uuid.clone());
            }
        }
        drop(descriptors);
        if membership {
            self.bump_membership();
        }
        changed
    }

    /// Writes the rows of `uuids` when the registry is persisted and they changed. Rows that fail
    /// to write are retried by [`TsManager::persist_unpersisted`].
    pub async fn persist(
        &self,
        writer: &CatalogWriter,
        token: &LeaderToken,
        uuids: &[EntityId],
    ) -> Result<(), Status> {
        let result = self.persist_rows(writer, token, uuids).await;
        if result.is_err() {
            self.defer_persist(uuids);
        }
        result
    }

    /// Leaves `uuids` for [`TsManager::persist_unpersisted`].
    pub fn defer_persist(&self, uuids: &[EntityId]) {
        self.unpersisted
            .lock()
            .unwrap()
            .extend(uuids.iter().cloned());
    }

    /// Writes the rows an earlier [`TsManager::persist`] failed to write.
    pub async fn persist_unpersisted(
        &self,
        writer: &CatalogWriter,
        token: &LeaderToken,
    ) -> Result<(), Status> {
        let uuids: Vec<EntityId> = self.unpersisted.lock().unwrap().drain().collect();
        self.persist(writer, token, &uuids).await
    }

    async fn persist_rows(
        &self,
        writer: &CatalogWriter,
        token: &LeaderToken,
        uuids: &[EntityId],
    ) -> Result<(), Status> {
        if !(self.options.persist_registry)() || uuids.is_empty() {
            return Ok(());
        }
        let mut rows = Vec::new();
        {
            let descriptors = self.descriptors.read().unwrap();
            for uuid in uuids {
                let Some(descriptor) = descriptors.get(uuid) else {
                    rows.push((uuid.clone(), None));
                    continue;
                };
                let mut pb = descriptor.pb.clone();
                pb.set_persisted(true);
                let bytes = pb
                    .write_to_bytes()
                    .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
                if descriptor.persisted_row.as_deref() != Some(&bytes[..]) {
                    rows.push((uuid.clone(), Some(bytes)));
                }
            }
        }
        if rows.is_empty() {
            return Ok(());
        }
        let rows_for_write = rows.clone();
        writer
            .mutate(token, move |_| {
                let mut mutation = Mutation::new();
                for (uuid, bytes) in &rows_for_write {
                    mutation = match bytes {
                        Some(bytes) => mutation.upsert(
                            SysRowEntryType::TSERVER_REGISTRATION,
                            uuid,
                            bytes.clone(),
                        ),
                        None => mutation.delete(SysRowEntryType::TSERVER_REGISTRATION, uuid),
                    };
                }
                Ok((mutation, ()))
            })
            .await?;
        let mut descriptors = self.descriptors.write().unwrap();
        for (uuid, bytes) in rows {
            if let Some(descriptor) = descriptors.get_mut(&uuid) {
                descriptor.persisted_row = bytes;
            }
        }
        Ok(())
    }

    /// `TSManager::RemoveTabletServer`'s checks and effect; `blacklisted` says whether the
    /// tserver is in the server blacklist. The caller persists the removal.
    pub fn remove(
        &self,
        snapshot: &CatalogSnapshot,
        uuid: &[u8],
        blacklisted: bool,
    ) -> Result<(), (Status, bool)> {
        let not_found = |m: String| Err((Status::new(StatusCode::NotFound, m), true));
        let invalid = |m: String| Err((Status::new(StatusCode::InvalidArgument, m), false));
        let name = String::from_utf8_lossy(uuid).into_owned();
        let mut descriptors = self.descriptors.write().unwrap();
        let Some(descriptor) = descriptors.get(uuid) else {
            return not_found(format!("Couldn't find tserver with uuid {name}"));
        };
        if descriptor.is_live() {
            return invalid(format!(
                "Cannot remove tablet server {name} because it is live"
            ));
        }
        if !blacklisted {
            return invalid(format!(
                "Cannot remove tablet server {name} because it is not blacklisted"
            ));
        }
        // Replicas of deleted tablets do not count, as in C++.
        let hosted = self
            .locations
            .tablets_on(uuid)
            .into_iter()
            .find(|tablet_id| {
                snapshot
                    .get(SysRowEntryType::TABLET, tablet_id)
                    .and_then(|t| t.tablet())
                    .is_some_and(|t| !is_inactive_state(t.state))
            });
        if let Some(tablet) = hosted {
            return invalid(format!(
                "Cannot remove tablet server {name} because it is hosting tablet {}",
                String::from_utf8_lossy(&tablet)
            ));
        }
        if descriptor.pb.xcluster_guarded_lease_state()
            != XClusterGuardedLeaseState::DEFINITELY_NO_LEASE
        {
            return invalid(format!(
                "Cannot remove tablet server {name} because it may still hold a xCluster-guarded \
                 information lease"
            ));
        }
        descriptors.remove(uuid);
        drop(descriptors);
        self.bump_membership();
        Ok(())
    }

    /// Follower: replaces or updates the registry from the leader's location stream.
    pub fn apply_peer_tservers(&self, infos: &[(TSInformationPB, bool)], full: bool) {
        let mut descriptors = self.descriptors.write().unwrap();
        if full {
            descriptors.clear();
        }
        for (info, live) in infos {
            let uuid: EntityId = info.tserver_instance.permanent_uuid().into();
            let descriptor = descriptors
                .entry(uuid.clone())
                .or_insert_with(|| TsDescriptor::new(uuid));
            descriptor
                .pb
                .set_instance_seqno(info.tserver_instance.instance_seqno());
            descriptor.pb.registration = info.registration.common.clone();
            descriptor.pb.resources = info.registration.resources.clone();
            descriptor.set_state(if *live {
                TsState::LIVE
            } else {
                TsState::UNRESPONSIVE
            });
        }
        drop(descriptors);
        self.bump_membership();
    }

    fn update_descriptor<T>(
        &self,
        uuid: &[u8],
        f: impl FnOnce(&mut TsDescriptor) -> T,
    ) -> Option<T> {
        self.descriptors.write().unwrap().get_mut(uuid).map(f)
    }
}

#[cfg(test)]
mod tests;
