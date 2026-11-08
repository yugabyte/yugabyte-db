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

//! `ProcessTabletReport`: observed state goes to tablet-locations at once; catalog consequences
//! (a committed Raft config change, a tablet turning RUNNING) become one mutation per report,
//! group-committed with every other mutation (L6).

use std::time::Instant;

use master_catalog_entity::{Entity, EntityId};
use master_catalog_state::defaults::TRANSACTION_TABLES_CONFIG_ID;
use master_catalog_state::{is_inactive_state, CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use master_tablet_locations::{Replica, TabletReplicas};
use protobuf::{Enum, EnumOrUnknown, Message};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysConfigEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_types::{PeerRole, TableType};
use yb_proto::pb::consensus_metadata::{ConsensusStatePB, PeerMemberType};
use yb_proto::pb::master_heartbeat::{
    ReportedTabletPB, ReportedTabletUpdatesPB, TSHeartbeatRequestPB, TabletReportUpdatesPB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tablet_types::{RaftGroupStatePB, TabletDataState};

use crate::{DeleteReplica, HeartbeatContext, TsManager};

impl TsManager {
    /// Returns the updates for the response and whether the tserver's report state is current
    /// (false asks for a full report).
    pub(crate) async fn process_report(
        &self,
        ctx: &HeartbeatContext<'_>,
        req: &TSHeartbeatRequestPB,
        deadline: Instant,
    ) -> Result<(TabletReportUpdatesPB, bool), Status> {
        let uuid = req.common.ts_instance.permanent_uuid();
        let mut updates = TabletReportUpdatesPB::new();
        let Some(descriptor) = self.get(uuid) else {
            return Ok((updates, false));
        };
        let Some(report) = req.tablet_report.as_ref() else {
            return Ok((updates, descriptor.has_tablet_report));
        };
        // TSDescriptor::PreCheckTabletReport.
        if !descriptor.has_tablet_report
            && (report.is_incremental()
                || (report.has_full_report_seq_no()
                    && descriptor.receiving_full_report_seq_no
                        != Some(report.full_report_seq_no())))
        {
            return Ok((updates, false));
        }

        let safe_deadline = deadline
            .checked_sub((self.options.report_safety_margin)())
            .unwrap_or(deadline);
        let mut reported: Vec<&ReportedTabletPB> = report.updated_tablets.iter().collect();
        reported.sort_by(|a, b| a.tablet_id().cmp(b.tablet_id()));
        let mut catalog_updates = Vec::new();
        let mut registered = Vec::new();
        let mut truncated = false;
        let report_ctx = ReportContext {
            token: ctx.token,
            snapshot: &ctx.snapshot,
            uuid,
            incremental: report.is_incremental(),
        };
        for (i, tablet) in reported.iter().enumerate() {
            if i > 0 && Instant::now() > safe_deadline {
                truncated = true;
                break;
            }
            let mut update = ReportedTabletUpdatesPB::new();
            update.set_tablet_id(tablet.tablet_id().to_vec());
            if let Some(state_msg) = self.process_tablet(&report_ctx, tablet, &mut registered) {
                update.set_state_msg(state_msg);
            }
            updates.tablets.push(update);
            if next_tablet_pb(&ctx.snapshot, uuid, tablet).is_some() {
                catalog_updates.push(*tablet);
            }
        }
        if truncated {
            updates.set_processing_truncated(true);
        }
        self.persist(ctx.writer, &ctx.token, &registered).await?;

        if !catalog_updates.is_empty() {
            ctx.writer
                .mutate_with(
                    &ctx.token,
                    // Not after the tserver gives up: it resends the report in its next heartbeat.
                    WriteOptions::with_timeout(deadline.saturating_duration_since(Instant::now())),
                    |snapshot| {
                        let mut mutation = Mutation::new();
                        let mut status_tablet_running = false;
                        for tablet in &catalog_updates {
                            let Some((entity, pb)) = next_tablet_pb(snapshot, uuid, tablet) else {
                                continue;
                            };
                            status_tablet_running |=
                                becomes_running_status_tablet(snapshot, &entity, &pb);
                            mutation = mutation.read(&entity).upsert_message(
                                SysRowEntryType::TABLET,
                                tablet.tablet_id(),
                                &pb,
                            )?;
                        }
                        if status_tablet_running {
                            mutation = bump_transaction_tables_version(snapshot, mutation)?;
                        }
                        Ok((mutation, ()))
                    },
                )
                .await?;
        }

        if !report.is_incremental() && report.remaining_tablet_count() == 0 && !truncated {
            let seqno = req.common.ts_instance.instance_seqno();
            self.update_descriptor(uuid, |d| {
                // CompleteFullReport: only for the instance that sent it.
                if d.pb.instance_seqno() == seqno {
                    d.has_tablet_report = true;
                    d.receiving_full_report_seq_no = None;
                }
            });
        }
        Ok((updates, true))
    }

    /// The observed-state half of `ProcessTabletReportBatch` for one tablet: locations and replica
    /// deletes. Returns the state message for the response. Tservers registered from a Raft config
    /// are added to `registered`.
    fn process_tablet(
        &self,
        ctx: &ReportContext<'_>,
        reported: &ReportedTabletPB,
        registered: &mut Vec<EntityId>,
    ) -> Option<String> {
        let uuid = ctx.uuid;
        let tablet_id = reported.tablet_id();
        let Some(tablet) = ctx.snapshot.get(SysRowEntryType::TABLET, tablet_id) else {
            // master_enable_deletion_check_for_orphaned_tablets: a replica of a tablet the
            // catalog never had is left alone.
            tracing::warn!(
                "Tablet server {} reported unknown tablet {}",
                String::from_utf8_lossy(uuid),
                String::from_utf8_lossy(tablet_id)
            );
            return None;
        };
        let hot = tablet.tablet()?;
        let table = ctx
            .snapshot
            .get(SysRowEntryType::TABLE, &hot.table_id)
            .and_then(|t| t.table());
        let tablet_pb: SysTabletsEntryPB = tablet.parse().ok()?;
        if is_inactive_state(hot.state) || table.is_none_or(|t| started_deleting(t.state)) {
            let message = String::from_utf8_lossy(tablet_pb.state_msg()).into_owned();
            self.delete_replica(
                ctx,
                uuid,
                tablet_id,
                TabletDataState::TABLET_DATA_DELETED,
                None,
                false,
                message.clone(),
            );
            return Some(message);
        }
        if !table.is_some_and(|t| table_is_running(t.state)) {
            return Some(String::from_utf8_lossy(tablet_pb.state_msg()).into_owned());
        }

        let previous = &*tablet_pb.committed_consensus_state;
        let previous_index = previous.config.committed_op_index();
        let reported_index = committed_index(reported);
        if self.should_delete_non_member(reported, previous, previous_index, reported_index)
            && !is_member(previous, uuid)
        {
            let message = if reported_index == INVALID_OP_INDEX {
                "Replica has no consensus available".to_string()
            } else {
                format!("Replica with old config index {reported_index}")
            };
            self.delete_replica(
                ctx,
                uuid,
                tablet_id,
                TabletDataState::TABLET_DATA_TOMBSTONED,
                Some(previous_index),
                false,
                format!("{message} (current committed config index is {previous_index})"),
            );
            return None;
        }
        if reported.error.is_some() {
            return None;
        }
        let table_hidden = table.is_some_and(|t| t.hidden);
        if (hot.hidden || table_hidden) && reported.has_is_hidden() && !reported.is_hidden() {
            self.delete_replica(
                ctx,
                uuid,
                tablet_id,
                TabletDataState::TABLET_DATA_DELETED,
                None,
                true,
                String::from_utf8_lossy(tablet_pb.state_msg()).into_owned(),
            );
        }

        match reported.committed_consensus_state.as_ref() {
            Some(cstate) => {
                self.update_locations(ctx, tablet, &tablet_pb, reported, cstate, registered)
            }
            None => {
                let state = reported.state();
                if ctx.incremental
                    && (state == RaftGroupStatePB::NOT_STARTED
                        || state == RaftGroupStatePB::BOOTSTRAPPING)
                {
                    self.locations.update_replica(
                        tablet_id,
                        reported_replica(
                            uuid,
                            PeerRole::NON_PARTICIPANT,
                            PeerMemberType::UNKNOWN_MEMBER_TYPE,
                            reported,
                        ),
                    );
                }
            }
        }
        None
    }

    /// `ShouldDeleteNonMember`: whether a replica that is not in the committed config is evicted
    /// rather than still joining. The caller checks membership.
    fn should_delete_non_member(
        &self,
        reported: &ReportedTabletPB,
        previous: &ConsensusStatePB,
        previous_index: i64,
        reported_index: i64,
    ) -> bool {
        let data_state = reported.tablet_data_state();
        if !(self.options.tombstone_evicted_replicas)()
            || data_state == TabletDataState::TABLET_DATA_TOMBSTONED
            || data_state == TabletDataState::TABLET_DATA_DELETED
        {
            return false;
        }
        if !(self.options.use_pending_config_op_id)() {
            return reported_index < previous_index;
        }
        let pending_term = match reported.pending_config_op_id.as_ref() {
            None => None,
            Some(op_id) if op_id.term() > 0 && op_id.index() > 0 => Some(op_id.term()),
            Some(op_id) => {
                tracing::warn!(
                    "Unexpected pending_config_op_id {}.{} in the report of tablet {}",
                    op_id.term(),
                    op_id.index(),
                    String::from_utf8_lossy(reported.tablet_id())
                );
                return reported_index < previous_index;
            }
        };
        let may_still_be_pending = pending_term.is_some_and(|t| t >= previous.current_term());
        reported_index <= previous_index && !may_still_be_pending
    }

    /// The locations half of `ProcessCommittedConsensusState`.
    fn update_locations(
        &self,
        ctx: &ReportContext<'_>,
        tablet: &Entity,
        tablet_pb: &SysTabletsEntryPB,
        reported: &ReportedTabletPB,
        cstate: &ConsensusStatePB,
        registered: &mut Vec<EntityId>,
    ) {
        let uuid = ctx.uuid;
        let tablet_id = &tablet.key.id;
        let Some(cstate) = effective_cstate(tablet_pb, uuid, cstate) else {
            return;
        };
        let previous = &*tablet_pb.committed_consensus_state;
        if config_or_leader_changed(previous, &cstate) {
            if (self.options.tombstone_evicted_replicas)() {
                for peer in &previous.config.peers {
                    if !is_member(&cstate, peer.permanent_uuid())
                        && self.get(peer.permanent_uuid()).is_some()
                    {
                        self.delete_replica(
                            ctx,
                            peer.permanent_uuid(),
                            tablet_id,
                            TabletDataState::TABLET_DATA_TOMBSTONED,
                            Some(previous.config.committed_op_index()),
                            false,
                            format!(
                                "TS {} not found in new config with opid_index {}",
                                String::from_utf8_lossy(peer.permanent_uuid()),
                                cstate.config.committed_op_index()
                            ),
                        );
                    }
                }
            }
            self.set_replicas_from_config(ctx, tablet_id, &cstate, reported, registered);
        } else if (self.options.register_from_raft)()
            && self.replicas_differ_from_config(tablet_id, &cstate)
        {
            self.set_replicas_from_config(ctx, tablet_id, &cstate, reported, registered);
        } else {
            let member_type = cstate
                .config
                .peers
                .iter()
                .find(|p| p.permanent_uuid() == uuid)
                .map_or(PeerMemberType::UNKNOWN_MEMBER_TYPE, |p| p.member_type());
            self.locations.update_replica(
                tablet_id,
                reported_replica(uuid, consensus_role(&cstate, uuid), member_type, reported),
            );
        }
    }

    /// `ReplicaMapDiffersFromConsensusState`.
    fn replicas_differ_from_config(&self, tablet_id: &[u8], cstate: &ConsensusStatePB) -> bool {
        let replicas = self.locations.get(tablet_id).unwrap_or_default().replicas;
        replicas.len() != cstate.config.peers.len()
            || cstate
                .config
                .peers
                .iter()
                .any(|p| !replicas.iter().any(|r| *r.ts_uuid == *p.permanent_uuid()))
    }

    /// `UpdateTabletReplicasAfterConfigChange`: the replica map becomes the config's peers,
    /// registering peers never heard from when `enable_register_ts_from_raft` is on.
    fn set_replicas_from_config(
        &self,
        ctx: &ReportContext<'_>,
        tablet_id: &[u8],
        cstate: &ConsensusStatePB,
        reported: &ReportedTabletPB,
        registered: &mut Vec<EntityId>,
    ) {
        let snapshot = ctx.snapshot;
        let existing = self.locations.get(tablet_id).unwrap_or_default();
        let mut replicas = Vec::new();
        for peer in &cstate.config.peers {
            let peer_uuid = peer.permanent_uuid();
            if peer_uuid.is_empty() {
                continue;
            }
            if self.get(peer_uuid).is_none() {
                if !(self.options.register_from_raft)() {
                    continue;
                }
                if let Err(status) = self.register_from_raft(peer, snapshot) {
                    tracing::warn!(
                        "Could not register tserver {} from the Raft config of tablet {}: {status}",
                        String::from_utf8_lossy(peer_uuid),
                        String::from_utf8_lossy(tablet_id)
                    );
                    continue;
                }
                registered.push(peer_uuid.into());
            }
            let role = consensus_role(cstate, peer_uuid);
            // The report's state, directory and flag are the sender's own replica's; a peer keeps
            // what its own reports said. (C++ gives every peer the sender's directory.)
            replicas.push(if peer_uuid == ctx.uuid {
                reported_replica(peer_uuid, role, peer.member_type(), reported)
            } else {
                let mut replica = Replica::new(peer_uuid.into(), role, peer.member_type());
                if let Some(old) = existing.replicas.iter().find(|r| &*r.ts_uuid == peer_uuid) {
                    replica.fs_data_dir = old.fs_data_dir.clone();
                    replica.should_disable_lb_move = old.should_disable_lb_move;
                }
                replica
            });
        }
        self.locations.set(
            tablet_id,
            TabletReplicas {
                replicas,
                confirmed: true,
            },
        );
    }

    #[allow(clippy::too_many_arguments)]
    fn delete_replica(
        &self,
        ctx: &ReportContext<'_>,
        uuid: &[u8],
        tablet_id: &[u8],
        delete_type: TabletDataState,
        cas_config_index: Option<i64>,
        hide_only: bool,
        reason: String,
    ) {
        // `AsyncDeleteReplica`: abort the replica's transactions unless its table is live.
        let started_deleting = |state: i32| {
            state == sys_tables_entry_pb::State::DELETING.value()
                || state == sys_tables_entry_pb::State::DELETED.value()
        };
        let abort_active_txns = ctx
            .snapshot
            .get(SysRowEntryType::TABLET, tablet_id)
            .and_then(|t| t.tablet())
            .and_then(|t| ctx.snapshot.get(SysRowEntryType::TABLE, &t.table_id))
            .and_then(|t| t.table())
            .is_none_or(|t| started_deleting(t.state));
        self.deletes.push(DeleteReplica {
            token: ctx.token,
            ts_uuid: uuid.into(),
            tablet_id: tablet_id.to_vec(),
            delete_type,
            cas_config_index,
            hide_only,
            abort_active_txns,
            reason,
        });
    }
}

/// What processing one report needs.
pub(crate) struct ReportContext<'a> {
    token: LeaderToken,
    snapshot: &'a CatalogSnapshot,
    uuid: &'a [u8],
    incremental: bool,
}

/// `consensus::kInvalidOpIdIndex`.
const INVALID_OP_INDEX: i64 = -1;

/// `GetCommittedConsensusStateOpIdIndex`.
fn committed_index(reported: &ReportedTabletPB) -> i64 {
    match reported.committed_consensus_state.as_ref() {
        Some(cstate) if cstate.config.has_committed_op_index() => {
            cstate.config.committed_op_index()
        }
        _ => INVALID_OP_INDEX,
    }
}

/// `TableInfo::started_deleting`.
fn started_deleting(state: i32) -> bool {
    state == sys_tables_entry_pb::State::DELETING.value()
        || state == sys_tables_entry_pb::State::DELETED.value()
}

/// `TableInfo::is_running`: PREPARING counts, as in C++.
fn table_is_running(state: i32) -> bool {
    state == sys_tables_entry_pb::State::PREPARING.value()
        || state == sys_tables_entry_pb::State::RUNNING.value()
        || state == sys_tables_entry_pb::State::ALTERING.value()
}

/// The reported cstate as the master would store it, or `None` when the report is ignored: no
/// committed index, stale (`master_ignore_stale_cstate`), or from a non-member.
fn effective_cstate(
    tablet_pb: &SysTabletsEntryPB,
    uuid: &[u8],
    reported: &ConsensusStatePB,
) -> Option<ConsensusStatePB> {
    if !reported.config.has_committed_op_index() {
        return None;
    }
    let previous = &*tablet_pb.committed_consensus_state;
    if reported.current_term() < previous.current_term()
        || reported.config.committed_op_index() < previous.config.committed_op_index()
    {
        return None;
    }
    if !is_member(reported, uuid) {
        return None;
    }
    let mut cstate = reported.clone();
    if !cstate.leader_uuid().is_empty() && !is_member(&cstate, cstate.leader_uuid().as_bytes()) {
        cstate.leader_uuid = None;
    }
    if cstate.current_term() == previous.current_term()
        && cstate.leader_uuid().is_empty()
        && !previous.leader_uuid().is_empty()
    {
        cstate.leader_uuid = previous.leader_uuid.clone();
    }
    Some(cstate)
}

fn config_or_leader_changed(previous: &ConsensusStatePB, cstate: &ConsensusStatePB) -> bool {
    cstate.config.committed_op_index() > previous.config.committed_op_index()
        || (!cstate.leader_uuid().is_empty()
            && (previous.leader_uuid().is_empty()
                || cstate.current_term() > previous.current_term()))
}

/// The tablet row after the report, when it changes: the new committed config and leader, and
/// the RUNNING transition. Pure, so a conflicting mutation can recompute it.
fn next_tablet_pb(
    snapshot: &CatalogSnapshot,
    uuid: &[u8],
    reported: &ReportedTabletPB,
) -> Option<(std::sync::Arc<Entity>, SysTabletsEntryPB)> {
    let entity = snapshot
        .get(SysRowEntryType::TABLET, reported.tablet_id())?
        .clone();
    let hot = entity.tablet()?;
    if is_inactive_state(hot.state) || reported.error.is_some() {
        return None;
    }
    let table_running = snapshot
        .get(SysRowEntryType::TABLE, &hot.table_id)
        .and_then(|t| t.table())
        .is_some_and(|t| table_is_running(t.state));
    if !table_running {
        return None;
    }
    let original: SysTabletsEntryPB = entity.parse().ok()?;
    let cstate = effective_cstate(
        &original,
        uuid,
        reported.committed_consensus_state.as_ref()?,
    )?;
    let mut pb = original.clone();
    if pb.state() != sys_tablets_entry_pb::State::RUNNING
        && reported.state() == RaftGroupStatePB::RUNNING
        && !cstate.leader_uuid().is_empty()
    {
        pb.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
        pb.set_state_msg(b"Tablet reported with an active leader".to_vec());
    }
    if config_or_leader_changed(&original.committed_consensus_state, &cstate) {
        pb.committed_consensus_state = Some(cstate).into();
    }
    if pb == original {
        return None;
    }
    // Byte-compare against what is stored: the writer skips unchanged rows anyway.
    let bytes = pb.write_to_bytes().ok()?;
    if *entity.raw == bytes[..] {
        return None;
    }
    Some((entity, pb))
}

/// A transaction status tablet turning RUNNING: tservers refresh their usable status tablets
/// only when the transaction tables version changes, so it is bumped, as the C++ report path
/// does. The bump when the table is created comes too early, before its tablets run.
fn becomes_running_status_tablet(
    snapshot: &CatalogSnapshot,
    entity: &Entity,
    pb: &SysTabletsEntryPB,
) -> bool {
    let running = sys_tablets_entry_pb::State::RUNNING.value();
    pb.state() == sys_tablets_entry_pb::State::RUNNING
        && entity.tablet().is_some_and(|t| t.state != running)
        && snapshot
            .get(SysRowEntryType::TABLE, pb.table_id())
            .and_then(|t| t.table())
            .is_some_and(|t| t.table_type == TableType::TRANSACTION_STATUS_TABLE_TYPE.value())
}

/// `IncrementTransactionTablesVersion`, in `mutation`.
fn bump_transaction_tables_version(
    snapshot: &CatalogSnapshot,
    mutation: Mutation,
) -> Result<Mutation, Status> {
    let Some(row) = snapshot.get(SysRowEntryType::SYS_CONFIG, TRANSACTION_TABLES_CONFIG_ID) else {
        return Ok(mutation);
    };
    let mut pb: SysConfigEntryPB = row
        .parse()
        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
    let config = pb.mut_transaction_tables_config();
    config.set_version(config.version() + 1);
    mutation.read(row).upsert_message(
        SysRowEntryType::SYS_CONFIG,
        TRANSACTION_TABLES_CONFIG_ID,
        &pb,
    )
}

fn is_member(cstate: &ConsensusStatePB, uuid: &[u8]) -> bool {
    cstate
        .config
        .peers
        .iter()
        .any(|p| p.permanent_uuid() == uuid)
}

/// `GetConsensusRole`.
pub fn consensus_role(cstate: &ConsensusStatePB, uuid: &[u8]) -> PeerRole {
    if cstate.leader_uuid().as_bytes() == uuid {
        return PeerRole::LEADER;
    }
    match cstate
        .config
        .peers
        .iter()
        .find(|p| p.permanent_uuid() == uuid)
    {
        None => PeerRole::NON_PARTICIPANT,
        Some(peer) => match peer.member_type() {
            PeerMemberType::VOTER => PeerRole::FOLLOWER,
            PeerMemberType::PRE_VOTER | PeerMemberType::PRE_OBSERVER => PeerRole::LEARNER,
            PeerMemberType::OBSERVER => PeerRole::READ_REPLICA,
            _ => PeerRole::UNKNOWN_ROLE,
        },
    }
}

/// The reporting tserver's own replica (`CreateNewReplicaForLocalMemory`).
fn reported_replica(
    uuid: &[u8],
    role: PeerRole,
    member_type: PeerMemberType,
    reported: &ReportedTabletPB,
) -> Replica {
    let mut replica = Replica::new(uuid.into(), role, member_type);
    replica.state = reported.state();
    replica.should_disable_lb_move = reported.should_disable_lb_move();
    replica.fs_data_dir =
        std::sync::Arc::from(String::from_utf8_lossy(reported.fs_data_dir()).as_ref());
    replica
}
