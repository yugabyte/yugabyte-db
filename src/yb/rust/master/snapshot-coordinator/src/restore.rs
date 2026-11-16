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

//! Restores: `RestoreSnapshot`, `RestoreSnapshotSchedule` (PITR), `AbortSnapshotRestore`,
//! `ListSnapshotRestorations`, and finishing a restoration (`FinishRestoration`).
//!
//! A PITR first restores the sys catalog: the `RESTORE_SYS_CATALOG` operation, which every master
//! applies with the C++ code and which writes the restoration row with the tablets to restore.
//! The controller then restores the tablets from that row, as C++ does after reloading its
//! catalog. A restore of a snapshot without a schedule writes the restoration row itself.

use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::MessageField;
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::backup::tablet_snapshot_op_request_pb::Operation;
use yb_proto::pb::backup::{TableMetadataPB, TabletSnapshotOpRequestPB};
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State;
use yb_proto::pb::catalog_entity_info::SysTablesEntryPB;
use yb_proto::pb::common_types::YQLDatabase;
use yb_proto::pb::master_backup::{
    AbortSnapshotRestoreRequestPB, ListSnapshotRestorationsRequestPB,
    ListSnapshotRestorationsResponsePB, RestorationInfoPB, RestoreSnapshotRequestPB,
    RestoreSnapshotResponsePB, RestoreSnapshotScheduleRequestPB, RestoreSnapshotScheduleResponsePB,
    SnapshotScheduleOptionsPB, SysRestorationEntryPB, TabletRestorationPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::schedules::{find_schedule, schedule_snapshots};
use crate::snapshots::{snapshot_rows, SnapshotRow};
use crate::state::{
    aggregated_state, all_done, merged, min_restore_time, restoration_initial_state,
    restoration_tablets,
};
use crate::tablet_ops::TabletOp;
use crate::{coded, error, ids, tagged, unknown, SnapshotCoordinator};

/// The feature name splitting is disabled under while a PITR runs.
pub const PITR_FEATURE_NAME: &str = "PITR";

/// `HybridTime::ToString`.
pub(crate) fn ht_string(ht: u64) -> String {
    let (physical, logical) = (ht >> 12, ht & 0xfff);
    if logical != 0 {
        format!("{{ physical: {physical} logical: {logical} }}")
    } else {
        format!("{{ physical: {physical} }}")
    }
}

/// A restoration row.
#[derive(Clone, Debug)]
pub(crate) struct RestorationRow {
    pub(crate) id: Vec<u8>,
    pub(crate) pb: SysRestorationEntryPB,
}

impl RestorationRow {
    pub(crate) fn schedule_id(&self) -> &[u8] {
        ids::try_decode(self.pb.schedule_id()).unwrap_or_default()
    }
}

pub(crate) fn restoration_rows(snapshot: &CatalogSnapshot) -> Vec<RestorationRow> {
    snapshot
        .iter(SysRowEntryType::SNAPSHOT_RESTORATION)
        .filter(|e| ids::try_decode(&e.key.id).is_some())
        .filter_map(|e| {
            e.parse::<SysRestorationEntryPB>()
                .ok()
                .map(|pb| RestorationRow {
                    id: e.key.id.to_vec(),
                    pb,
                })
        })
        .collect()
}

/// `GetPgsqlDatabaseOid`: the first 8 hex digits of a YSQL namespace id.
pub(crate) fn database_oid(namespace_id: &[u8]) -> Option<i64> {
    let s = std::str::from_utf8(namespace_id).ok()?;
    if s.len() != 32 {
        return None;
    }
    i64::from_str_radix(&s[0..8], 16).ok()
}

impl SnapshotCoordinator {
    /// The tablets of `row` with this term's outcomes applied.
    pub(crate) fn restoration_current_tablets(
        &self,
        term: i64,
        row: &RestorationRow,
    ) -> Vec<(Vec<u8>, State)> {
        let state = self.state(term);
        merged(restoration_tablets(&row.pb), state.outcomes.get(&row.id))
    }

    /// `FindInProgressRestorationUsingSnapshot`.
    pub(crate) fn in_progress_restoration_using(
        &self,
        snapshot: &CatalogSnapshot,
        term: i64,
        snapshot_id: &[u8],
    ) -> Option<Vec<u8>> {
        restoration_rows(snapshot)
            .into_iter()
            .filter(|r| r.pb.snapshot_id() == snapshot_id)
            .find(|r| {
                !all_done(
                    restoration_initial_state(&r.pb),
                    &self.restoration_current_tablets(term, r),
                )
            })
            .map(|r| r.id)
    }

    /// `ListSnapshotRestorations`.
    pub fn list_restorations(
        &self,
        token: &LeaderToken,
        req: &ListSnapshotRestorationsRequestPB,
        resp: &mut ListSnapshotRestorationsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let decode = |bytes: &[u8], what: &str| -> Result<Option<Vec<u8>>, MasterErrorPB> {
            if bytes.is_empty() {
                return Ok(None);
            }
            ids::try_decode(bytes)
                .map(|id| Some(id.to_vec()))
                .ok_or_else(|| {
                    error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::InvalidArgument,
                        format!("Invalid {what} id"),
                    )
                })
        };
        let restoration_id = decode(req.restoration_id(), "restoration")?;
        let snapshot_id = decode(req.snapshot_id(), "snapshot")?;
        let snapshot = self.catalog.snapshot();
        let rows = restoration_rows(&snapshot);
        match restoration_id {
            None => {
                for row in rows {
                    if snapshot_id
                        .as_deref()
                        .is_none_or(|s| row.pb.snapshot_id() == s)
                    {
                        resp.restorations
                            .push(self.restoration_info(token.term(), &row)?);
                    }
                }
            }
            Some(id) => {
                let row = rows.into_iter().find(|r| r.id == id).ok_or_else(|| {
                    coded(
                        Code::OBJECT_NOT_FOUND,
                        StatusCode::NotFound,
                        format!("Could not find restoration: {}", ids::to_string(&id)),
                    )
                })?;
                resp.restorations
                    .push(self.restoration_info(token.term(), &row)?);
            }
        }
        Ok(())
    }

    /// `RestorationState::ToPB` for a client.
    fn restoration_info(
        &self,
        term: i64,
        row: &RestorationRow,
    ) -> Result<RestorationInfoPB, MasterErrorPB> {
        let tablets = self.restoration_current_tablets(term, row);
        let mut entry = row.pb.clone();
        entry.set_state(
            aggregated_state(restoration_initial_state(&row.pb), &tablets, false)
                .map_err(|s| unknown(&s))?,
        );
        entry.tablet_restorations = tablets
            .into_iter()
            .map(|(id, state)| {
                let mut t = TabletRestorationPB::new();
                t.set_id(id);
                t.set_state(state);
                t
            })
            .collect();
        let mut info = RestorationInfoPB::new();
        info.set_id(row.id.clone());
        info.entry = MessageField::some(entry);
        Ok(info)
    }

    /// `RestoreSnapshot`.
    pub async fn restore_snapshot(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &RestoreSnapshotRequestPB,
        resp: &mut RestoreSnapshotResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = ids::try_decode(req.snapshot_id()).ok_or_else(|| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "Invalid snapshot id",
            )
        })?;
        let restore_at = if req.has_restore_ht() {
            req.restore_ht()
        } else {
            0
        };
        tracing::info!(
            "Servicing RestoreSnapshot request: {}{}",
            ids::to_string(id),
            if restore_at != 0 {
                format!(" to restore to time {} ", ht_string(restore_at))
            } else {
                String::new()
            }
        );
        let restoration = self.restore(token, id, restore_at, deadline).await?;
        resp.set_restoration_id(restoration);
        Ok(())
    }

    /// `Restore` and `DoRestore(kInitial)`.
    pub(crate) async fn restore(
        &self,
        token: &LeaderToken,
        snapshot_id: &[u8],
        restore_at: u64,
        deadline: Instant,
    ) -> Result<Vec<u8>, MasterErrorPB> {
        let restoration_id = ids::random_id();
        let snapshot = self.catalog.snapshot();
        let row = Self::find_snapshot(&snapshot, snapshot_id)?;
        if self.state(token.term()).deleting.contains(snapshot_id) {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                "The snapshot has started deleting",
            ));
        }
        // The replicated state: the host's coordinator checks it when applying the restore.
        if self.persisted(&row).map_err(|s| unknown(&s))? != State::COMPLETE {
            return Err(coded(
                Code::SNAPSHOT_IS_NOT_READY,
                StatusCode::IllegalState,
                format!(
                    "The snapshot state is not complete: {}",
                    ids::to_string(snapshot_id)
                ),
            ));
        }
        if restore_at != 0 && restore_at > row.pb.snapshot_hybrid_time() {
            return Err(tagged(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                format!(
                    "Snapshot {} contains data only up to {}, cannot restore it to the later time \
                     {}",
                    ids::to_string(snapshot_id),
                    ht_string(row.pb.snapshot_hybrid_time()),
                    ht_string(restore_at)
                ),
            ));
        }
        let mut restore_sys_catalog = !row.schedule_id().is_empty();
        if restore_sys_catalog {
            // An automatic-mode xCluster target rolls DDLs back by replication: its sys catalog is
            // not restored.
            if let Some(namespace_id) = row.namespace_id() {
                let target = self
                    .hooks
                    .get()
                    .is_some_and(|h| h.is_automatic_xcluster_target(&namespace_id));
                if (self.options.skip_sys_catalog_tablet_restore)() || target {
                    restore_sys_catalog = false;
                }
            }
        }
        if restore_sys_catalog {
            self.forward_restore_check(&snapshot, row.schedule_id(), restore_at)?;
            let mut req = TabletSnapshotOpRequestPB::new();
            req.operation = Operation::RESTORE_SYS_CATALOG.into();
            req.snapshot_id = snapshot_id.to_vec();
            req.snapshot_hybrid_time = restore_at;
            req.restoration_id = restoration_id.clone();
            self.submit_op(token, req, deadline).await?;
            self.wake();
            return Ok(restoration_id);
        }
        // A restore without the sys catalog step: its row is written here, with the snapshot's
        // complete tablets, so a new leader carries on with it.
        let mut pb = SysRestorationEntryPB::new();
        pb.set_state(State::RESTORING);
        for (tablet, state) in self.current_tablets(token.term(), &row) {
            if state == State::COMPLETE {
                let mut t = TabletRestorationPB::new();
                t.set_id(tablet);
                t.set_state(State::RESTORING);
                pb.tablet_restorations.push(t);
            }
        }
        pb.set_snapshot_id(snapshot_id.to_vec());
        pb.set_schedule_id(row.schedule_id().to_vec());
        pb.set_is_sys_catalog_restored(true);
        pb.set_restore_at_ht(restore_at);
        pb.set_version(2);
        tracing::info!(
            "Writing restoration {} entry to sys catalog",
            ids::to_string(&restoration_id)
        );
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |_| {
                    let mutation = Mutation::new()
                        .read_absent(SysRowEntryType::SNAPSHOT_RESTORATION, &restoration_id)
                        .upsert_message(
                            SysRowEntryType::SNAPSHOT_RESTORATION,
                            &restoration_id,
                            &pb,
                        )?;
                    Ok((mutation, ()))
                },
            )
            .await
            .map_err(|s| unknown(&s))?;
        self.wake();
        Ok(restoration_id)
    }

    /// `ForwardRestoreCheck`: restoring into a time a previous restoration rolled back is refused.
    pub(crate) fn forward_restore_check(
        &self,
        snapshot: &CatalogSnapshot,
        schedule_id: &[u8],
        restore_at: u64,
    ) -> Result<(), MasterErrorPB> {
        for r in restoration_rows(snapshot) {
            if r.schedule_id() != schedule_id {
                continue;
            }
            let complete = r.pb.complete_time_ht();
            if r.pb.restore_at_ht() <= restore_at && complete != 0 && complete >= restore_at {
                let message = format!(
                    "Cannot perform a forward restore. Existing restoration {} was restored to {} \
                     and completed at {}, while the requested restoration for {} is in between.",
                    ids::to_string(&r.id),
                    ht_string(r.pb.restore_at_ht()),
                    ht_string(complete),
                    ht_string(restore_at)
                );
                tracing::warn!("{message}");
                return Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::NotSupported,
                    message,
                ));
            }
        }
        Ok(())
    }

    /// `RestoreSnapshotSchedule` (PITR): splitting is paused, a snapshot covering `restore_ht` is
    /// found or taken, and restored to that time.
    pub async fn restore_schedule(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &RestoreSnapshotScheduleRequestPB,
        resp: &mut RestoreSnapshotScheduleResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let schedule_id = ids::try_decode(req.snapshot_schedule_id())
            .unwrap_or_default()
            .to_vec();
        let restore_at = req.restore_ht();
        if let Some(hooks) = self.hooks.get() {
            hooks.disable_splitting(
                deadline.saturating_duration_since(Instant::now()) + Duration::from_secs(60),
                PITR_FEATURE_NAME,
            );
        }
        tracing::info!(
            "Servicing RestoreSnapshotSchedule request. id: {} restore_ht: {}",
            ids::to_string(&schedule_id),
            ht_string(restore_at)
        );
        let snapshot_id = self
            .suitable_snapshot(token, &schedule_id, restore_at, deadline)
            .await?;
        let restoration_id = self
            .restore(token, &snapshot_id, restore_at, deadline)
            .await?;
        resp.set_snapshot_id(snapshot_id);
        resp.set_restoration_id(restoration_id);
        Ok(())
    }

    /// `GetSuitableSnapshotForRestore`.
    pub(crate) async fn suitable_snapshot(
        &self,
        token: &LeaderToken,
        schedule_id: &[u8],
        restore_at: u64,
        deadline: Instant,
    ) -> Result<Vec<u8>, MasterErrorPB> {
        let now = self.host.max_global_now().map_err(|s| unknown(&s))?;
        if restore_at > now {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                format!(
                    "Cannot restore to {} since it is in the future",
                    ht_string(restore_at)
                ),
            ));
        }
        while Instant::now() < deadline {
            let found = match self.suitable_existing(token.term(), schedule_id, restore_at) {
                Ok(id) => Ok(id),
                Err(e)
                    if e.status.code()
                        == yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::NOT_FOUND =>
                {
                    self.create_for_schedule(token, schedule_id, deadline).await
                }
                Err(e) => Err(e),
            };
            match found {
                Ok(id) => {
                    self.wait_for_complete(&id, deadline).await?;
                    return Ok(id);
                }
                Err(e) if e.code() == Code::PARALLEL_SNAPSHOT_OPERATION => {
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
                Err(e) => return Err(e),
            }
        }
        Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::TimedOut,
            format!(
                "Timed out trying to find or create a snapshot for schedule {} that covers time {}",
                ids::to_string(schedule_id),
                ht_string(restore_at)
            ),
        ))
    }

    /// `FindSnapshotSuitableForRestoreAt`.
    fn suitable_existing(
        &self,
        term: i64,
        schedule_id: &[u8],
        restore_at: u64,
    ) -> Result<Vec<u8>, MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let rows = snapshot_rows(&snapshot);
        let snapshots: Vec<&SnapshotRow> = schedule_snapshots(&rows, schedule_id).collect();
        let Some(first) = snapshots.first() else {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                format!(
                    "No snapshots have been created for schedule {}",
                    ids::to_string(schedule_id)
                ),
            ));
        };
        if restore_at < min_restore_time(&first.pb) {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::IllegalState,
                format!(
                    "Trying to restore to {}, which is earlier than the minimum allowed restore \
                     time {}.",
                    ht_string(restore_at),
                    ht_string(min_restore_time(&first.pb))
                ),
            ));
        }
        for row in snapshots {
            let state = self.aggregated(term, row).map_err(|s| unknown(&s))?;
            if matches!(state, State::COMPLETE | State::CREATING)
                && restore_at <= row.pb.snapshot_hybrid_time()
                && restore_at >= min_restore_time(&row.pb)
            {
                return Ok(row.id.clone());
            }
        }
        Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::NotFound,
            format!(
                "The schedule does not have any valid snapshots created after the restore time \
                 ({}).",
                ht_string(restore_at)
            ),
        ))
    }

    /// `WaitForSnapshotToComplete`, for a caller outside the coordinator (xCluster failover).
    pub async fn wait_for_snapshot(
        &self,
        _token: &LeaderToken,
        id: &[u8],
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        self.wait_for_complete(id, deadline).await
    }

    /// `Restore` of a snapshot at `restore_at`: the restoration id.
    pub async fn restore_at(
        &self,
        token: &LeaderToken,
        snapshot_id: &[u8],
        restore_at: u64,
        deadline: Instant,
    ) -> Result<Vec<u8>, MasterErrorPB> {
        self.restore(token, snapshot_id, restore_at, deadline).await
    }

    /// `WaitForSnapshotToComplete`.
    async fn wait_for_complete(&self, id: &[u8], deadline: Instant) -> Result<(), MasterErrorPB> {
        while Instant::now() < deadline {
            let snapshot = self.catalog.snapshot();
            let row = Self::find_snapshot(&snapshot, id)?;
            // Not the in-memory progress: a restore proposed before the write that completes the
            // snapshot blocks that write, and the host then fails to apply it.
            if self.persisted(&row).map_err(|s| unknown(&s))? == State::COMPLETE {
                return Ok(());
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
        Err(error(
            Code::UNKNOWN_ERROR,
            StatusCode::TimedOut,
            format!(
                "Timed out waiting for snapshot {} to complete",
                ids::to_string(id)
            ),
        ))
    }

    /// `AbortSnapshotRestore`: every tablet FAILED.
    pub async fn abort_restore(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &AbortSnapshotRestoreRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let Some(id) = ids::try_decode(req.restoration_id()) else {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::NotSupported,
                format!(
                    "Invalid restoration id: {}",
                    String::from_utf8_lossy(req.restoration_id())
                ),
            ));
        };
        tracing::info!(
            "Servicing AbortSnapshotRestore request. restoration id: {}",
            ids::to_string(id)
        );
        let term = token.term();
        let mut not_found = None;
        let mut complete = false;
        let result = self
            .writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let Some(entity) = snapshot.get(SysRowEntryType::SNAPSHOT_RESTORATION, id)
                    else {
                        not_found = Some(coded(
                            Code::OBJECT_NOT_FOUND,
                            StatusCode::NotFound,
                            format!("Could not find restoration: {}", ids::to_string(id)),
                        ));
                        return Err(Status::new(StatusCode::NotFound, "restoration"));
                    };
                    let mut pb: SysRestorationEntryPB = entity
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    let row = RestorationRow {
                        id: id.to_vec(),
                        pb: pb.clone(),
                    };
                    let tablets = self.restoration_current_tablets(term, &row);
                    if aggregated_state(restoration_initial_state(&pb), &tablets, false)?
                        == State::RESTORED
                    {
                        complete = true;
                        return Err(Status::new(
                            StatusCode::IllegalState,
                            "Cannot abort completed restoration",
                        ));
                    }
                    for t in pb.tablet_restorations.iter_mut() {
                        t.set_state(State::FAILED);
                    }
                    pb.set_version(pb.version() + 1);
                    tracing::info!(
                        "Marking restoration {} as FAILED in sys catalog",
                        ids::to_string(id)
                    );
                    let mutation = Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::SNAPSHOT_RESTORATION,
                        id,
                        &pb,
                    )?;
                    Ok((mutation, ()))
                },
            )
            .await;
        match result {
            Ok(_) => {
                self.state(term).outcomes.remove(id);
                Ok(())
            }
            Err(_) if not_found.is_some() => Err(not_found.unwrap()),
            Err(s) if complete => Err(error(
                Code::UNKNOWN_ERROR,
                s.code,
                format!("Failed to abort tasks: {}", s.message),
            )),
            Err(s) => Err(unknown(&s)),
        }
    }

    /// The RESTORE_ON_TABLET operations of a restoration whose sys catalog phase is done.
    pub(crate) fn restore_ops(
        &self,
        snapshot: &CatalogSnapshot,
        row: &RestorationRow,
        snapshot_row: &SnapshotRow,
        tablets: &[Vec<u8>],
    ) -> Vec<TabletOp> {
        let in_snapshot: std::collections::HashSet<Vec<u8>> =
            snapshot_row.tablet_ids().into_iter().collect();
        let pitr = !row.schedule_id().is_empty();
        let db_oid = pitr
            .then(|| snapshot_row.namespace_id().and_then(|n| database_oid(&n)))
            .flatten();
        let mut ops = Vec::new();
        for tablet_id in tablets {
            let mut extras = None;
            let tablet = snapshot.get(SysRowEntryType::TABLET, tablet_id);
            let table_id = tablet
                .and_then(|t| t.tablet())
                .map(|t| t.table_id.clone())
                .unwrap_or_default();
            if pitr {
                let mut req = TabletSnapshotOpRequestPB::new();
                if let Some(table) = snapshot
                    .get(SysRowEntryType::TABLE, &table_id)
                    .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                {
                    req.schema_version = table.version();
                    req.schema = table.schema.clone();
                    req.indexes = table.indexes.clone();
                    if table.colocated() {
                        let ids = tablet
                            .map(|t| snapshot.hosted_table_ids(t))
                            .unwrap_or_default();
                        for id in ids {
                            let Some(colocated) = snapshot.get(SysRowEntryType::TABLE, &id) else {
                                tracing::warn!(
                                    "Table {} does not exist",
                                    String::from_utf8_lossy(&id)
                                );
                                continue;
                            };
                            if !master_table_lifecycle::lookup::is_visible_to_client(colocated) {
                                continue;
                            }
                            let Ok(pb) = colocated.parse::<SysTablesEntryPB>() else {
                                continue;
                            };
                            let mut m = TableMetadataPB::new();
                            m.schema_version = pb.version();
                            m.schema = pb.schema.clone();
                            m.indexes = pb.indexes.clone();
                            m.table_id = id.clone();
                            req.colocated_tables_metadata.push(m);
                        }
                    }
                }
                extras = Some(req);
            }
            ops.push(TabletOp {
                operation: Operation::RESTORE_ON_TABLET,
                tablet_id: tablet_id.clone(),
                snapshot_id: if in_snapshot.contains(tablet_id) {
                    row.pb.snapshot_id().to_vec()
                } else {
                    Vec::new()
                },
                schedule_id: row.schedule_id().to_vec(),
                restoration_id: row.id.clone(),
                snapshot_hybrid_time: row.pb.restore_at_ht(),
                restoration_hybrid_time: 0,
                request_extras: extras,
                db_oid: (table_id == crate::SEQUENCES_DATA_TABLE_ID)
                    .then_some(db_oid)
                    .flatten(),
            });
        }
        ops
    }

    /// `FinishRestoration` once every tablet is done: the complete time on the restoration and,
    /// for a PITR, on its schedule, in one write; then `RESTORE_FINISHED` to the restored
    /// tablets and the follow-ups.
    pub(crate) async fn finish_restoration(
        &self,
        token: LeaderToken,
        row: RestorationRow,
    ) -> Result<(), Status> {
        let id = row.id.clone();
        let outcomes = {
            let state = self.state(token.term());
            state.outcomes.get(&id).cloned().unwrap_or_default()
        };
        let complete_time = self.host.now()?;
        let schedule_id = row.schedule_id().to_vec();
        // C++'s words; tests wait for this line.
        tracing::info!(
            "Marking restoration {} as complete in sys catalog",
            ids::to_string(&id)
        );
        let (pb, _) = self
            .writer
            .mutate_with(
                &token,
                WriteOptions::with_timeout(Duration::from_secs(30)),
                |snapshot| {
                    let Some(entity) = snapshot.get(SysRowEntryType::SNAPSHOT_RESTORATION, &id)
                    else {
                        return Err(Status::new(StatusCode::NotFound, "restoration is gone"));
                    };
                    let mut pb: SysRestorationEntryPB = entity
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    if pb.complete_time_ht() != 0 {
                        return Ok((Mutation::new(), pb));
                    }
                    for t in pb.tablet_restorations.iter_mut() {
                        if let Some(o) = outcomes.get(t.id()).filter(|o| o.from == t.state()) {
                            t.set_state(o.state);
                        }
                    }
                    pb.set_complete_time_ht(complete_time);
                    pb.set_version(pb.version() + 1);
                    let mut mutation = Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::SNAPSHOT_RESTORATION,
                        &id,
                        &pb,
                    )?;
                    if let Some(schedule) = (!schedule_id.is_empty())
                        .then(|| snapshot.get(SysRowEntryType::SNAPSHOT_SCHEDULE, &schedule_id))
                        .flatten()
                    {
                        let mut options: SnapshotScheduleOptionsPB = schedule
                            .parse()
                            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                        options.restoration_times.push(complete_time);
                        mutation = mutation.read(schedule).upsert_message(
                            SysRowEntryType::SNAPSHOT_SCHEDULE,
                            &schedule_id,
                            &options,
                        )?;
                    }
                    Ok((mutation, pb))
                },
            )
            .await?;
        self.state(token.term()).outcomes.remove(&id);
        tracing::info!(
            "Marked restoration {} as complete at {}",
            ids::to_string(&id),
            ht_string(pb.complete_time_ht())
        );
        if !self.state(token.term()).finished.insert(id.clone()) {
            return Ok(());
        }
        if !(self.options.skip_sending_restore_finished)() {
            let restored: Vec<Vec<u8>> = restoration_tablets(&pb)
                .filter(|(_, s)| *s == State::RESTORED)
                .map(|(id, _)| id)
                .collect();
            for tablet_id in restored {
                let op = TabletOp {
                    operation: Operation::RESTORE_FINISHED,
                    tablet_id,
                    snapshot_id: Vec::new(),
                    schedule_id: Vec::new(),
                    restoration_id: id.clone(),
                    snapshot_hybrid_time: 0,
                    restoration_hybrid_time: pb.complete_time_ht(),
                    request_extras: None,
                    db_oid: None,
                };
                if let Err(e) = self.send_tablet_op(token, &op, &|| false).await {
                    tracing::warn!(
                        "RESTORE_FINISHED of tablet {} failed: {}",
                        String::from_utf8_lossy(&op.tablet_id),
                        e.status
                    );
                }
            }
        }
        if schedule_id.is_empty() {
            return Ok(());
        }
        let Some(hooks) = self.hooks.get() else {
            return Ok(());
        };
        for m in &pb.master_metadata {
            if m.type_() == SysRowEntryType::TABLE {
                hooks.resume_backfill(m.id());
            }
        }
        hooks.reenable_splitting(PITR_FEATURE_NAME);
        // Hidden tables kept for PITR or splits can hold OIDs above the restored counters.
        let snapshot = self.catalog.snapshot();
        if let Ok(schedule) = find_schedule(&snapshot, &schedule_id) {
            for table in schedule.tables() {
                let Some(ns) = table.namespace.as_ref() else {
                    continue;
                };
                if ns.has_id() && ns.database_type() == YQLDatabase::YQL_DATABASE_PGSQL {
                    if let Err(s) = hooks.advance_oid_counters(ns.id().to_vec()).await {
                        tracing::warn!(
                            "Failed to advance OID counters for namespace {} after restore. \
                             Post-restore OID collisions may occur: {s}",
                            String::from_utf8_lossy(ns.id())
                        );
                    }
                }
            }
        } else {
            tracing::warn!(
                "PITR: Skipping OID counter advance for restoration {}: snapshot schedule {} \
                 could not be found. Post-restore OID collisions may occur.",
                ids::to_string(&id),
                ids::to_string(&schedule_id)
            );
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use protobuf::Message;

    #[test]
    fn database_oids() {
        assert_eq!(
            database_oid(b"000033e8000030008000000000000000"),
            Some(0x33e8)
        );
        assert_eq!(database_oid(b"short"), None);
        assert_eq!(ht_string(5 << 12), "{ physical: 5 }");
        assert_eq!(ht_string((5 << 12) | 3), "{ physical: 5 logical: 3 }");
    }

    #[test]
    fn message_round_trip() {
        let mut pb = SysRestorationEntryPB::new();
        pb.set_state(State::RESTORING);
        let bytes = pb.write_to_bytes().unwrap();
        assert_eq!(
            SysRestorationEntryPB::parse_from_bytes(&bytes)
                .unwrap()
                .state(),
            State::RESTORING
        );
    }
}
