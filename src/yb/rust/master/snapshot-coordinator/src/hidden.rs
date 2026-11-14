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

//! Hidden objects: what keeps a dropped table's or retired tablet's data
//! (`GetDeleteRetainerInfoForTableDrop`, `GetDeleteRetainerInfoForTabletDrop`), and
//! `CleanupHiddenObjects`, which deletes hidden tablets and tables once no schedule can restore to
//! a time they were live and no snapshot holds them.

use std::collections::{HashMap, HashSet};
use std::sync::Arc;

use master_catalog_entity::Entity;
use master_catalog_state::{is_inactive_state, CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_table_lifecycle::{covering_schedules, local_time_string, Retainer, Retention};
use master_task_runtime::{Attempt, TermScope};
use master_ts_manager::DeleteReplica;
use protobuf::{EnumOrUnknown, Message};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State;
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tablet_types::TabletDataState;
use yb_proto::pb::tserver_admin::{
    RemoveTableFromTabletRequestPB, RemoveTableFromTabletResponsePB,
};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

use crate::schedules::{schedule_rows, schedule_snapshots};
use crate::snapshots::SnapshotRow;
use crate::state::{min_restore_time, schedule_deleted};
use crate::{corrupt, internal, SnapshotCoordinator};

/// Tablets of hidden tables retired per write.
const TABLETS_PER_WRITE: usize = 1000;

/// `ShouldBlockObjectCleanup` and `ShouldAddToCoveringMap`: a complete snapshot with a TTL, not of
/// a schedule and not imported, keeps its tablets.
fn covering_tablets(snapshot: &CatalogSnapshot) -> HashSet<Vec<u8>> {
    let mut out = HashSet::new();
    for row in snapshot.iter(SysRowEntryType::SNAPSHOT) {
        let Ok(pb) = row.parse::<yb_proto::pb::catalog_entity_info::SysSnapshotEntryPB>() else {
            continue;
        };
        if !pb.has_retention_duration_hours()
            || crate::ids::try_decode(pb.schedule_id()).is_some()
            || pb.imported()
            || !pb
                .tablet_snapshots
                .iter()
                .all(|t| t.state() == State::COMPLETE)
        {
            continue;
        }
        out.extend(pb.tablet_snapshots.iter().map(|t| t.id().to_vec()));
    }
    out
}

impl Retention for SnapshotCoordinator {
    fn table_drop(&self, snapshot: &CatalogSnapshot, table_id: &[u8]) -> Retainer {
        let namespace_id = snapshot
            .get(SysRowEntryType::TABLE, table_id)
            .and_then(|t| t.table().map(|t| t.namespace_id.clone()))
            .unwrap_or_default();
        let covered = covering_tablets(snapshot);
        Retainer {
            schedules: covering_schedules(snapshot, SysRowEntryType::TABLE, table_id),
            active_snapshot: self.is_namespace_retained(&namespace_id)
                || snapshot
                    .tablets_of(table_id)
                    .iter()
                    .any(|t| covered.contains(&*t.key.id)),
        }
    }

    fn tablet_drop(&self, snapshot: &CatalogSnapshot, tablet_id: &[u8]) -> Retainer {
        let table_id = snapshot
            .get(SysRowEntryType::TABLET, tablet_id)
            .and_then(|t| t.tablet().map(|t| t.table_id.clone()))
            .unwrap_or_default();
        let namespace_id = snapshot
            .get(SysRowEntryType::TABLE, &table_id)
            .and_then(|t| t.table().map(|t| t.namespace_id.clone()))
            .unwrap_or_default();
        Retainer {
            schedules: covering_schedules(snapshot, SysRowEntryType::TABLE, &table_id),
            active_snapshot: covering_tablets(snapshot).contains(tablet_id)
                || self.is_namespace_retained(&namespace_id),
        }
    }
}

/// For each live schedule with snapshots, the earliest time it can restore to (the first snapshot
/// that is not being deleted, or the last one).
pub(crate) fn schedule_min_restore_times(
    snapshot: &CatalogSnapshot,
    rows: &[SnapshotRow],
) -> HashMap<Vec<u8>, u64> {
    let mut out = HashMap::new();
    for schedule in schedule_rows(snapshot) {
        if schedule_deleted(&schedule.options) {
            continue;
        }
        let snapshots: Vec<&SnapshotRow> = schedule_snapshots(rows, &schedule.id).collect();
        let Some(last) = snapshots.last() else {
            continue;
        };
        let first = snapshots
            .iter()
            .find(|s| s.pb.state() != State::DELETING)
            .unwrap_or(last);
        out.insert(schedule.id.clone(), min_restore_time(&first.pb));
    }
    out
}

impl SnapshotCoordinator {
    /// `ShouldRetain`: a schedule that retained the object can still restore to a time before it
    /// was hidden, or a snapshot holds the tablet.
    fn should_retain(
        &self,
        snapshot: &CatalogSnapshot,
        tablet: &Entity,
        tablet_pb: &SysTabletsEntryPB,
        hide_time: u64,
        min_times: &HashMap<Vec<u8>, u64>,
        covered: &HashSet<Vec<u8>>,
    ) -> bool {
        if tablet_pb
            .retained_by_snapshot_schedules
            .iter()
            .any(|s| min_times.get(s).is_some_and(|min| *min <= hide_time))
        {
            return true;
        }
        if covered.contains(&*tablet.key.id) {
            return true;
        }
        let namespace_id = tablet
            .tablet()
            .and_then(|t| snapshot.get(SysRowEntryType::TABLE, &t.table_id))
            .and_then(|t| t.table().map(|t| t.namespace_id.clone()))
            .unwrap_or_default();
        self.is_namespace_retained(&namespace_id)
    }

    /// `CleanupHiddenObjects`.
    pub(crate) fn cleanup_hidden(
        self: &Arc<Self>,
        scope: &TermScope,
        token: LeaderToken,
        snapshot: &CatalogSnapshot,
        rows: &[SnapshotRow],
    ) {
        let min_times = schedule_min_restore_times(snapshot, rows);
        let covered = covering_tablets(snapshot);
        // `CleanupHiddenTablets`.
        let expired: Vec<Vec<u8>> = snapshot
            .iter(SysRowEntryType::TABLET)
            .filter(|t| {
                t.tablet()
                    .is_some_and(|h| h.hidden && !is_inactive_state(h.state))
            })
            .filter(|t| {
                t.parse::<SysTabletsEntryPB>().is_ok_and(|pb| {
                    !self.should_retain(
                        snapshot,
                        t,
                        &pb,
                        pb.hide_hybrid_time(),
                        &min_times,
                        &covered,
                    )
                })
            })
            .map(|t| t.key.id.to_vec())
            .take(TABLETS_PER_WRITE)
            .collect();
        if !expired.is_empty() && self.state(token.term()).writing.insert(b"hidden".to_vec()) {
            let this = self.clone();
            scope.entity().spawn("cleanup-hidden-tablets", async move {
                if let Err(status) = this.delete_hidden_tablets(token, &expired).await {
                    tracing::warn!("Failed to cleanup hidden tablets: {status}");
                }
                this.state(token.term())
                    .writing
                    .remove(b"hidden".as_slice());
            });
        }
        // `CleanupHiddenTables`.
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else { continue };
            if !hot.hidden
                || hot.state == sys_tables_entry_pb::State::DELETING as i32
                || hot.state == sys_tables_entry_pb::State::DELETED as i32
                || master_table_lifecycle::lookup::is_system_table(snapshot, table)
            {
                continue;
            }
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            if pb.hide_state() != sys_tables_entry_pb::HideState::HIDDEN {
                continue;
            }
            let colocated = !hot.parent_table_id.is_empty() && hot.parent_table_id != *table.key.id;
            let id = table.key.id.to_vec();
            if colocated {
                // Out of its hosting tablets' metadata first, unless they are retained.
                let hosting: Vec<Vec<u8>> = snapshot
                    .hosting_tablets_of(&id)
                    .into_iter()
                    .filter(|t| {
                        t.parse::<SysTabletsEntryPB>().is_ok_and(|tpb| {
                            !self.should_retain(
                                snapshot,
                                t,
                                &tpb,
                                pb.hide_hybrid_time(),
                                &min_times,
                                &covered,
                            )
                        })
                    })
                    .map(|t| t.key.id.to_vec())
                    .collect();
                if hosting.len() != snapshot.hosting_tablets_of(&id).len() {
                    continue;
                }
                if !self.state(token.term()).writing.insert(id.clone()) {
                    continue;
                }
                let this = self.clone();
                scope.entity().spawn("cleanup-hidden-table", async move {
                    let mut removed = true;
                    for tablet in &hosting {
                        if let Err(status) = this.remove_table_from_tablet(token, tablet, &id).await
                        {
                            tracing::warn!(
                                "Failed to remove hidden table {} from tablet {}: {status}",
                                String::from_utf8_lossy(&id),
                                String::from_utf8_lossy(tablet)
                            );
                            removed = false;
                        }
                    }
                    if removed {
                        if let Err(status) = this.delete_hidden_table(token, &id).await {
                            tracing::warn!("Failed to mark tables as deleted: {status}");
                        }
                    }
                    this.state(token.term()).writing.remove(&id);
                });
                continue;
            }
            let remaining = snapshot.tablets_of(&id).into_iter().any(|t| {
                t.tablet()
                    .is_some_and(|h| h.table_id == id && !is_inactive_state(h.state))
            });
            if remaining || !self.state(token.term()).writing.insert(id.clone()) {
                continue;
            }
            let this = self.clone();
            scope.entity().spawn("cleanup-hidden-table", async move {
                if let Err(status) = this.delete_hidden_table(token, &id).await {
                    tracing::warn!("Failed to mark tables as deleted: {status}");
                }
                this.state(token.term()).writing.remove(&id);
            });
        }
    }

    /// Hidden tablets nothing retains: DELETED, and their replicas deleted.
    async fn delete_hidden_tablets(
        &self,
        token: LeaderToken,
        ids: &[Vec<u8>],
    ) -> Result<(), Status> {
        tracing::info!(
            "Cleanup hidden tablets: {:?}",
            ids.iter()
                .map(|i| String::from_utf8_lossy(i).into_owned())
                .collect::<Vec<_>>()
        );
        let reason = "Cleanup hidden tablets".to_string();
        let (deleted, _) = self
            .writer
            .mutate(&token, |snapshot| {
                let mut mutation = Mutation::new();
                let mut deleted = Vec::new();
                for id in ids {
                    let Some(tablet) = snapshot.get(SysRowEntryType::TABLET, id) else {
                        continue;
                    };
                    let Some(hot) = tablet.tablet() else { continue };
                    if !hot.hidden || is_inactive_state(hot.state) {
                        continue;
                    }
                    let mut pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                    pb.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::DELETED));
                    pb.set_state_msg(reason.clone().into_bytes());
                    let peers: Vec<Vec<u8>> = pb
                        .committed_consensus_state
                        .config
                        .peers
                        .iter()
                        .map(|p| p.permanent_uuid().to_vec())
                        .collect();
                    mutation = mutation.read(tablet).upsert(
                        SysRowEntryType::TABLET,
                        id,
                        pb.write_to_bytes().map_err(internal)?,
                    );
                    deleted.push((id.clone(), peers));
                }
                Ok((mutation, deleted))
            })
            .await?;
        for (tablet_id, peers) in deleted {
            let mut replicas: Vec<Vec<u8>> = peers;
            if let Some(observed) = self.ts_manager.locations.get(&tablet_id) {
                replicas.extend(observed.replicas.iter().map(|r| r.ts_uuid.to_vec()));
            }
            replicas.sort();
            replicas.dedup();
            for ts_uuid in replicas {
                self.ts_manager.deletes.push(DeleteReplica {
                    token,
                    ts_uuid: ts_uuid.into(),
                    tablet_id: tablet_id.clone(),
                    delete_type: TabletDataState::TABLET_DATA_DELETED,
                    cas_config_index: None,
                    hide_only: false,
                    abort_active_txns: true,
                    exclude_txn: None,
                    reason: reason.clone(),
                });
            }
        }
        Ok(())
    }

    /// A hidden table whose tablets are gone: DELETED, skipping DELETING.
    async fn delete_hidden_table(&self, token: LeaderToken, id: &[u8]) -> Result<(), Status> {
        self.writer
            .mutate(&token, |snapshot| {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, id) else {
                    return Ok((Mutation::new(), ()));
                };
                let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                if matches!(
                    pb.state(),
                    sys_tables_entry_pb::State::DELETING | sys_tables_entry_pb::State::DELETED
                ) {
                    return Ok((Mutation::new(), ()));
                }
                tracing::info!(
                    "Cleaning up hidden table {}: {}",
                    String::from_utf8_lossy(pb.name()),
                    String::from_utf8_lossy(id)
                );
                pb.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::DELETED));
                pb.set_state_msg(
                    format!("Cleanup hidden table at {}", local_time_string()).into_bytes(),
                );
                Ok((
                    Mutation::new().read(table).upsert(
                        SysRowEntryType::TABLE,
                        id,
                        pb.write_to_bytes().map_err(internal)?,
                    ),
                    (),
                ))
            })
            .await
            .map(|_| ())
    }

    /// `AsyncRemoveTableFromTablet` to the tablet's leader.
    async fn remove_table_from_tablet(
        &self,
        token: LeaderToken,
        tablet_id: &[u8],
        table_id: &[u8],
    ) -> Result<(), Status> {
        let rpc = self.rpc()?;
        let lane = self.leader_uuid(tablet_id).unwrap_or_default();
        let timeout = (self.options.rpc_timeout)();
        self.executor
            .run(&lane, "RemoveTableFromTablet", &self.retry, |_| {
                let current = self.leadership.check(&token);
                let leader = self.leader_uuid(tablet_id);
                let address = leader.as_ref().and_then(|l| self.ts_manager.rpc_address(l));
                let rpc = rpc.clone();
                async move {
                    current.map_err(Attempt::Fail)?;
                    let (host, port) = address.ok_or_else(|| {
                        Attempt::Retry(Status::new(
                            StatusCode::ServiceUnavailable,
                            "No leader for the tablet",
                        ))
                    })?;
                    let mut req = RemoveTableFromTabletRequestPB::new();
                    req.set_tablet_id(tablet_id.to_vec());
                    req.set_remove_table_id(table_id.to_vec());
                    let bytes = req
                        .write_to_bytes()
                        .map_err(|e| Attempt::Fail(internal(e)))?;
                    let resp = rpc
                        .call(
                            &host,
                            port,
                            "yb.tserver.TabletServerAdminService",
                            "RemoveTableFromTablet",
                            &bytes,
                            timeout,
                        )
                        .await
                        .map_err(Attempt::Retry)?;
                    let resp = RemoveTableFromTabletResponsePB::parse_from_bytes(&resp)
                        .map_err(|e| Attempt::Retry(corrupt(e)))?;
                    match resp.error.as_ref() {
                        None => Ok(()),
                        Some(e) if e.code() == tablet_server_error_pb::Code::TABLET_NOT_FOUND => {
                            Ok(())
                        }
                        Some(e) => Err(Attempt::Retry(Status::new(
                            StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
                            e.status.message().to_string(),
                        ))),
                    }
                }
            })
            .await
    }

    pub(crate) fn leader_uuid(&self, tablet_id: &[u8]) -> Option<Vec<u8>> {
        self.ts_manager
            .locations
            .get(tablet_id)
            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
    }
}
