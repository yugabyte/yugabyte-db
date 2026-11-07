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

//! The leader half of `MasterHeartbeatServiceImpl::TSHeartbeat`: everything after the readiness
//! check. Field for field as C++, minus what later layers own (YSQL catalog versions and pins,
//! xCluster-guarded info, the leader-blacklist drain list).

use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use master_catalog_entity::EntityId;
use master_catalog_state::{CatalogSnapshot, CatalogWriter};
use master_leadership::LeaderToken;
use master_tablet_locations::LeaseReport;
use protobuf::EnumOrUnknown;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{SysClusterConfigEntryPB, SysConfigEntryPB};
use yb_proto::pb::consensus_metadata::RaftPeerPB;
use yb_proto::pb::consensus_types::LeaderLeaseStatus;
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::wire_protocol::{app_status_pb, AppStatusPB, NodeInstancePB};

use crate::{TsDescriptor, TsManager, TsState, XClusterGuardedLeaseState};

/// What a contributor may read.
pub struct ContributorContext<'a> {
    /// The catalog the response is built from.
    pub snapshot: &'a CatalogSnapshot,
    /// The parsed cluster config.
    pub cluster_config: &'a SysClusterConfigEntryPB,
    /// Whether to send the universe key registry (`ShouldResendRegistry`).
    pub send_universe_key_registry: bool,
}

/// Fills the response fields one component owns (L7). Contributors run before the tserver is
/// looked up, in registration order, as `CatalogManager::FillHeartbeatResponse` does.
pub trait HeartbeatContributor: Send + Sync {
    /// Fills its fields; an error fails the heartbeat.
    fn contribute(
        &self,
        req: &TSHeartbeatRequestPB,
        ctx: &ContributorContext<'_>,
        resp: &mut TSHeartbeatResponsePB,
    ) -> Result<(), Status>;
}

/// What a heartbeat needs from the leader.
pub struct HeartbeatContext<'a> {
    /// The leader's token, for catalog writes.
    pub token: LeaderToken,
    /// The catalog writer.
    pub writer: &'a CatalogWriter,
    /// The snapshot at the start of the heartbeat.
    pub snapshot: Arc<CatalogSnapshot>,
    /// Response parts owned elsewhere.
    pub contributors: &'a [Arc<dyn HeartbeatContributor>],
    /// This master's instance.
    pub master_instance: NodeInstancePB,
    /// Time left until the tserver's deadline.
    pub time_to_deadline: Duration,
}

impl TsManager {
    /// Handles a heartbeat on a ready leader. An `Err` fails the RPC, as `RespondFailure` does.
    pub async fn heartbeat(
        &self,
        ctx: HeartbeatContext<'_>,
        req: &TSHeartbeatRequestPB,
    ) -> Result<TSHeartbeatResponsePB, Status> {
        let deadline = Instant::now() + ctx.time_to_deadline;
        let mut resp = TSHeartbeatResponsePB::new();
        resp.master_instance = Some(ctx.master_instance.clone()).into();
        resp.set_leader_master(true);

        let cluster_config: SysClusterConfigEntryPB = ctx
            .snapshot
            .cluster_config()
            .ok_or_else(|| Status::new(StatusCode::IllegalState, "No cluster config"))?
            .parse()
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        if !self.check_universe(req, &cluster_config, &mut resp)? {
            return Ok(resp);
        }
        if req.registration.is_some() {
            resp.set_cluster_uuid(cluster_config.cluster_uuid().to_string());
        }

        let uuid = req.common.ts_instance.permanent_uuid();
        let send_universe_key_registry = cluster_config.encryption_info.is_some()
            && self.take_key_registry_send(uuid, req.registration.is_some());
        let contributor_ctx = ContributorContext {
            snapshot: &ctx.snapshot,
            cluster_config: &cluster_config,
            send_universe_key_registry,
        };
        for contributor in ctx.contributors {
            contributor.contribute(req, &contributor_ctx, &mut resp)?;
        }
        if self.cdc_enabled(&ctx.snapshot) {
            resp.set_xcluster_enabled_on_producer(true);
        }
        // FillHeartbeatResponseClusterConfig and the consumer registry part of
        // FillHeartbeatResponseCDC.
        if req.cluster_config_version() < cluster_config.version() {
            resp.set_cluster_config_version(cluster_config.version());
            resp.cluster_replication_info = cluster_config.replication_info.clone();
            if cluster_config.consumer_registry.is_some() {
                resp.consumer_registry = cluster_config.consumer_registry.clone();
            }
        }

        let mut changed: Vec<EntityId> = Vec::new();
        if let Some(registration) = req.registration.as_ref() {
            match self.register(&req.common.ts_instance, registration, true) {
                Ok(mut replaced) => changed.append(&mut replaced),
                Err(status) if status.code == StatusCode::AlreadyPresent => {
                    tracing::warn!("Tablet server registration failed: {status}");
                    resp.set_needs_reregister(true);
                    resp.set_needs_full_tablet_report(true);
                    return Ok(resp);
                }
                Err(status) => return Err(status),
            }
        }
        match self.update_from_heartbeat(req) {
            Ok(true) => changed.push(uuid.into()),
            Ok(false) => {}
            Err(status) if status.code == StatusCode::NotFound => {
                resp.set_needs_reregister(true);
                resp.set_needs_full_tablet_report(true);
                return Ok(resp);
            }
            Err(status) => return Err(status),
        }
        if req.registration.is_some() {
            changed.push(uuid.into());
        }
        self.persist(ctx.writer, &ctx.token, &changed).await?;

        resp.set_tablet_report_limit((self.options.tablet_report_limit)());
        let (updates, has_report) = self.process_report(&ctx, req, deadline).await?;
        if req.tablet_report.is_some() {
            resp.tablet_report = Some(updates).into();
        }
        if !has_report {
            resp.set_needs_full_tablet_report(true);
        }
        let incremental = req
            .tablet_report
            .as_ref()
            .is_none_or(|r| r.is_incremental());
        if incremental
            && deadline.saturating_duration_since(Instant::now())
                > (self.options.metadata_min_time_left)()
        {
            self.record_leader_leases(req);
        }

        let (version, list) = self.live_list(ctx.token.term());
        if req.live_tservers_version.as_ref() == Some(&version) {
            resp.set_live_tservers_unchanged(true);
            self.unchanged_responses
                .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        } else {
            resp.tservers = (*list).clone();
        }
        resp.live_tservers_version = Some(version).into();

        if let Some(config) = ctx.snapshot.get(
            SysRowEntryType::SYS_CONFIG,
            b"transaction-tables-configuration",
        ) {
            if let Ok(config) = config.parse::<SysConfigEntryPB>() {
                resp.set_transaction_tables_version(config.transaction_tables_config().version());
            }
        }
        Ok(resp)
    }

    /// The leader-lease half of `ProcessTabletMetadata`, for the tablets in `storage_metadata`
    /// whose leader is the reporting tserver.
    fn record_leader_leases(&self, req: &TSHeartbeatRequestPB) {
        if req.storage_metadata.is_empty() {
            return;
        }
        let uuid = req.common.ts_instance.permanent_uuid();
        let now = Instant::now();
        if let Some(descriptor) = self.descriptors.write().unwrap().get_mut(uuid) {
            descriptor.last_lease_report = Some(now);
        }
        if req.leader_info.is_empty() {
            return;
        }
        let leader_info: HashMap<&[u8], _> = req
            .leader_info
            .iter()
            .map(|info| (info.tablet_id(), info))
            .collect();
        let now_physical_us = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(0, |d| d.as_micros() as u64);
        let max_expired = (self.options.max_leader_lease_expired)();
        for metadata in &req.storage_metadata {
            let Some(info) = leader_info.get(metadata.tablet_id()) else {
                continue;
            };
            self.locations.record_leader_lease(
                LeaseReport {
                    tablet_id: metadata.tablet_id(),
                    leader: uuid,
                    has_lease: info.leader_lease_status() == LeaderLeaseStatus::HAS_LEASE,
                    ht_lease_expiration: info.ht_lease_expiration(),
                },
                now_physical_us,
                max_expired,
                now,
            );
        }
    }

    /// `ValidateTServerUniverseOrRespond`. `Ok(false)` means the response carries the error.
    fn check_universe(
        &self,
        req: &TSHeartbeatRequestPB,
        cluster_config: &SysClusterConfigEntryPB,
        resp: &mut TSHeartbeatResponsePB,
    ) -> Result<bool, Status> {
        if !(self.options.universe_uuid_check)() {
            return Ok(true);
        }
        let master_uuid = cluster_config.universe_uuid();
        let ts_uuid = req.universe_uuid();
        if master_uuid.is_empty() {
            resp.error = Some(master_error(
                master_error_pb::Code::INVALID_CLUSTER_CONFIG,
                StatusCode::TryAgain,
                "universe_uuid is not set in the cluster config",
            ))
            .into();
            return Ok(false);
        }
        if ts_uuid.is_empty() {
            resp.set_universe_uuid(master_uuid.to_string());
            resp.error = Some(master_error(
                master_error_pb::Code::INVALID_REQUEST,
                StatusCode::TryAgain,
                "universe_uuid needs to be set in the request",
            ))
            .into();
            return Ok(false);
        }
        if ts_uuid != master_uuid {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                format!("Received wrong universe_uuid {ts_uuid}, expected {master_uuid}"),
            ));
        }
        Ok(true)
    }

    /// `TSManager::RegisterInternal`: from a heartbeat, or, for a peer in a reported Raft config
    /// that never heartbeated, from that config (unresponsive, with no xCluster lease). Returns the
    /// uuids whose state changed (other than this one).
    fn register(
        &self,
        instance: &NodeInstancePB,
        registration: &yb_proto::pb::master_types::TSRegistrationPB,
        through_heartbeat: bool,
    ) -> Result<Vec<EntityId>, Status> {
        let uuid: EntityId = instance.permanent_uuid().into();
        let seqno = instance.instance_seqno();
        let mut descriptors = self.descriptors.write().unwrap();
        // Every check before any change, so a refused registration changes nothing.
        let mut replaced = Vec::new();
        for other in descriptors.values() {
            if other.uuid == uuid || other.state() == TsState::REPLACED {
                continue;
            }
            if same_host_port(&other.pb.registration, &registration.common) {
                if other.pb.instance_seqno() >= seqno {
                    return Err(Status::new(
                        StatusCode::AlreadyPresent,
                        format!(
                            "Tablet server {} has a host port collision with {}",
                            String::from_utf8_lossy(&uuid),
                            String::from_utf8_lossy(&other.uuid)
                        ),
                    ));
                }
                replaced.push(other.uuid.clone());
            }
        }
        if let Some(existing) = descriptors.get(&uuid) {
            if seqno < existing.pb.instance_seqno() {
                return Err(Status::new(
                    StatusCode::AlreadyPresent,
                    format!(
                        "Cannot register with sequence number {seqno}: Already have a \
                         registration from sequence number {}",
                        existing.pb.instance_seqno()
                    ),
                ));
            }
        }
        for other in &replaced {
            if let Some(other) = descriptors.get_mut(other) {
                other.set_state(TsState::REPLACED);
            }
        }
        let descriptor = descriptors
            .entry(uuid.clone())
            .or_insert_with(|| TsDescriptor::new(uuid.clone()));
        // TSDescriptor::UpdateRegistration.
        descriptor.has_tablet_report = false;
        descriptor.receiving_full_report_seq_no = None;
        descriptor.pb.set_instance_seqno(seqno);
        if instance.has_start_time_us() {
            descriptor.pb.set_start_time_us(instance.start_time_us());
        }
        descriptor.pb.registration = registration.common.clone();
        descriptor.pb.resources = registration.resources.clone();
        descriptor.pb.version_info = registration.version_info.clone();
        let (state, lease) = if through_heartbeat {
            (TsState::LIVE, XClusterGuardedLeaseState::MAYBE_HAS_LEASE)
        } else {
            (
                TsState::UNRESPONSIVE,
                XClusterGuardedLeaseState::DEFINITELY_NO_LEASE,
            )
        };
        descriptor.set_state(state);
        descriptor.pb.xcluster_guarded_lease_state = Some(EnumOrUnknown::new(lease));
        descriptor.latest_report_seqno = i32::MIN;
        tracing::info!(
            "Registered tablet server {} (instance seqno {seqno}){}",
            String::from_utf8_lossy(&uuid),
            if through_heartbeat {
                ""
            } else {
                " from a Raft config"
            }
        );
        drop(descriptors);
        self.sent_key_registry.lock().unwrap().remove(&uuid);
        self.bump_membership();
        Ok(replaced)
    }

    /// `CatalogManager::RegisterTsFromRaftConfig`.
    pub(crate) fn register_from_raft(
        &self,
        peer: &RaftPeerPB,
        snapshot: &CatalogSnapshot,
    ) -> Result<(), Status> {
        let mut instance = NodeInstancePB::new();
        instance.set_permanent_uuid(peer.permanent_uuid().to_vec());
        instance.set_instance_seqno(0);
        let mut registration = yb_proto::pb::master_types::TSRegistrationPB::new();
        let common = registration.common.mut_or_insert_default();
        common.private_rpc_addresses = peer.last_known_private_addr.clone();
        common.broadcast_addresses = peer.last_known_broadcast_addr.clone();
        common.cloud_info = peer.cloud_info.clone();
        let config: SysClusterConfigEntryPB = snapshot
            .cluster_config()
            .ok_or_else(|| Status::new(StatusCode::IllegalState, "No cluster config"))?
            .parse()
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        let placement_uuid = match config.replication_info.as_ref() {
            Some(info) => crate::placement::placement_uuid_of_peer(info, peer)?,
            None => Vec::new(),
        };
        common.set_placement_uuid(placement_uuid);
        self.register(&instance, &registration, false)?;
        Ok(())
    }

    /// `LookupAndUpdateTSFromHeartbeat`. Returns whether the persisted state changed.
    fn update_from_heartbeat(&self, req: &TSHeartbeatRequestPB) -> Result<bool, Status> {
        let instance = &req.common.ts_instance;
        let not_found = || {
            Status::new(
                StatusCode::NotFound,
                format!(
                    "Unknown tablet server {}",
                    String::from_utf8_lossy(instance.permanent_uuid())
                ),
            )
        };
        let mut became_live = false;
        let changed = self
            .update_descriptor(instance.permanent_uuid(), |d| {
                if d.state() == TsState::REPLACED
                    || d.pb.instance_seqno() != instance.instance_seqno()
                {
                    return Err(not_found());
                }
                if let Some(report) = req.tablet_report.as_ref() {
                    if report.sequence_number() < d.latest_report_seqno {
                        return Err(Status::new(
                            StatusCode::IllegalState,
                            format!(
                                "Stale tablet report {} (latest {})",
                                report.sequence_number(),
                                d.latest_report_seqno
                            ),
                        ));
                    }
                    d.latest_report_seqno = report.sequence_number();
                    if !report.is_incremental()
                        && report.full_report_seq_no() == report.sequence_number()
                    {
                        d.receiving_full_report_seq_no = Some(report.full_report_seq_no());
                    }
                }
                if d.state() == TsState::REMOVED {
                    return Err(Status::new(
                        StatusCode::IllegalState,
                        "Tablet server was removed",
                    ));
                }
                d.last_heartbeat = Some(Instant::now());
                d.num_live_replicas = req.num_live_tablets();
                d.leader_count = req.leader_count();
                if let Some(metrics) = req.metrics.as_ref() {
                    d.metrics = Some(metrics.clone());
                }
                let before = d.pb.clone();
                if !d.is_live() {
                    became_live = true;
                }
                d.set_state(TsState::LIVE);
                d.pb.xcluster_guarded_lease_state = Some(EnumOrUnknown::new(
                    XClusterGuardedLeaseState::MAYBE_HAS_LEASE,
                ));
                Ok(before != d.pb)
            })
            .ok_or_else(not_found)??;
        if became_live {
            self.bump_membership();
        }
        Ok(changed)
    }
}

/// Whether two registrations share an RPC address (the `master_register_ts_check_desired_host_port`
/// = false comparison; layer 2 does not compare per cloud).
fn same_host_port(
    a: &yb_proto::pb::wire_protocol::ServerRegistrationPB,
    b: &yb_proto::pb::wire_protocol::ServerRegistrationPB,
) -> bool {
    let addresses = |r: &yb_proto::pb::wire_protocol::ServerRegistrationPB| {
        r.private_rpc_addresses
            .iter()
            .chain(r.broadcast_addresses.iter())
            .map(|hp| (hp.host().to_string(), hp.port()))
            .collect::<Vec<_>>()
    };
    let a = addresses(a);
    addresses(b).iter().any(|hp| a.contains(hp))
}

/// A `MasterErrorPB` with a status.
pub fn master_error(
    code: master_error_pb::Code,
    status: StatusCode,
    message: &str,
) -> MasterErrorPB {
    let mut error = MasterErrorPB::new();
    error.set_code(code);
    let mut app_status = AppStatusPB::new();
    app_status.set_code(
        <app_status_pb::ErrorCode as protobuf::Enum>::from_i32(status as i32)
            .unwrap_or(app_status_pb::ErrorCode::UNKNOWN_ERROR),
    );
    app_status.set_message(message.to_string());
    error.status = Some(app_status).into();
    error
}
