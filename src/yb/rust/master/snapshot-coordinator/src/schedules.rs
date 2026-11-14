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

//! Snapshot schedules: their RPCs and the snapshots they take (`SnapshotScheduleState`,
//! `PollSchedulesPrepare`, `CreateForSchedule`).

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use master_table_lifecycle::lookup::find_namespace;
use protobuf::MessageField;
use tokio::time::Instant;
use yb_host_api::StatusCode;
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::State;
use yb_proto::pb::master_backup::{
    snapshot_schedule_filter_pb, CreateSnapshotScheduleRequestPB, CreateSnapshotScheduleResponsePB,
    DeleteSnapshotScheduleRequestPB, EditSnapshotScheduleRequestPB, EditSnapshotScheduleResponsePB,
    ListSnapshotSchedulesRequestPB, ListSnapshotSchedulesResponsePB, ListSnapshotsDetailOptionsPB,
    SnapshotScheduleInfoPB, SnapshotScheduleOptionsPB,
};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, NamespaceIdentifierPB, SysRowEntryType, TableIdentifierPB,
};

use crate::collect::collect_for_snapshot;
use crate::snapshots::{snapshot_rows, SnapshotRow};
use crate::state::{schedule_deleted, validate_schedule_options};
use crate::{coded, error, ids, unknown, SnapshotCoordinator};

/// A schedule row.
#[derive(Clone, Debug)]
pub(crate) struct ScheduleRow {
    pub(crate) id: Vec<u8>,
    pub(crate) options: SnapshotScheduleOptionsPB,
}

impl ScheduleRow {
    pub(crate) fn tables(&self) -> Vec<TableIdentifierPB> {
        match self.options.filter.as_ref().and_then(|f| f.filter.clone()) {
            Some(snapshot_schedule_filter_pb::Filter::Tables(t)) => t.tables,
            _ => Vec::new(),
        }
    }
}

pub(crate) fn schedule_rows(snapshot: &CatalogSnapshot) -> Vec<ScheduleRow> {
    snapshot
        .iter(SysRowEntryType::SNAPSHOT_SCHEDULE)
        .filter(|e| ids::try_decode(&e.key.id).is_some())
        .filter_map(|e| {
            e.parse::<SnapshotScheduleOptionsPB>()
                .ok()
                .map(|options| ScheduleRow {
                    id: e.key.id.to_vec(),
                    options,
                })
        })
        .collect()
}

pub(crate) fn find_schedule(
    snapshot: &CatalogSnapshot,
    id: &[u8],
) -> Result<ScheduleRow, MasterErrorPB> {
    snapshot
        .get(SysRowEntryType::SNAPSHOT_SCHEDULE, id)
        .and_then(|e| e.parse::<SnapshotScheduleOptionsPB>().ok())
        .map(|options| ScheduleRow {
            id: id.to_vec(),
            options,
        })
        .ok_or_else(|| {
            coded(
                Code::SNAPSHOT_NOT_FOUND,
                StatusCode::NotFound,
                format!("Could not find snapshot schedule: {}", ids::to_string(id)),
            )
        })
}

/// The snapshots of `schedule_id`, oldest first.
pub(crate) fn schedule_snapshots<'a>(
    rows: &'a [SnapshotRow],
    schedule_id: &'a [u8],
) -> impl Iterator<Item = &'a SnapshotRow> + 'a {
    rows.iter().filter(move |r| r.schedule_id() == schedule_id)
}

impl SnapshotCoordinator {
    /// `CreateSnapshotSchedule`: one schedule per namespace, identified by id.
    pub async fn create_schedule(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateSnapshotScheduleRequestPB,
        resp: &mut CreateSnapshotScheduleResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing CreateSnapshotSchedule {:?}", req);
        let mut options = req.options.clone().unwrap_or_default();
        let tables = match options.filter.as_ref().and_then(|f| f.filter.clone()) {
            Some(snapshot_schedule_filter_pb::Filter::Tables(t)) => t.tables,
            _ => Vec::new(),
        };
        if tables.len() != 1 {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::NotSupported,
                "Only one filter can be set on a snapshot schedule",
            ));
        }
        let snapshot = self.catalog.snapshot();
        let filter_ns = tables[0].namespace.clone().unwrap_or_default();
        let ident = if filter_ns.has_id() {
            let mut i = NamespaceIdentifierPB::new();
            i.set_id(filter_ns.id().to_vec());
            i
        } else {
            let mut i = NamespaceIdentifierPB::new();
            i.set_database_type(filter_ns.database_type());
            i.set_name(filter_ns.name().to_string());
            i
        };
        let namespace = find_namespace(&snapshot, &ident)?;
        let ns = namespace.namespace().expect("namespace row");
        let mut table = tables[0].clone();
        let n = table.namespace.mut_or_insert_default();
        n.set_id(namespace.key.id.to_vec());
        n.database_type = Some(protobuf::EnumOrUnknown::from_i32(ns.database_type));
        n.clear_name();
        if let Some(snapshot_schedule_filter_pb::Filter::Tables(t)) =
            options.filter.as_mut().and_then(|f| f.filter.as_mut())
        {
            t.tables = vec![table];
        }
        // `ParseCreateSnapshotScheduleRequest` holds with the namespace id set above.
        let namespace_id = namespace.key.id.to_vec();
        validate_schedule_options(&options).map_err(|s| unknown(&s))?;
        let id = ids::random_id();
        let (_, _) = self
            .writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let mut mutation = Mutation::new();
                    for s in schedule_rows(snapshot) {
                        let entity = snapshot
                            .get(SysRowEntryType::SNAPSHOT_SCHEDULE, &s.id)
                            .expect("listed");
                        mutation = mutation.read(entity);
                        if schedule_deleted(&s.options) {
                            continue;
                        }
                        if s.tables()
                            .iter()
                            .any(|t| t.namespace.id() == namespace_id.as_slice())
                        {
                            return Err(crate::status_of(&coded(
                                Code::OBJECT_ALREADY_PRESENT,
                                StatusCode::AlreadyPresent,
                                format!(
                                    "Snapshot schedule {} already exists for the namespace {}",
                                    ids::to_string(&s.id),
                                    String::from_utf8_lossy(&namespace_id)
                                ),
                            )));
                        }
                    }
                    let mutation = mutation
                        .read_absent(SysRowEntryType::SNAPSHOT_SCHEDULE, &id)
                        .upsert_message(SysRowEntryType::SNAPSHOT_SCHEDULE, &id, &options)?;
                    Ok((mutation, ()))
                },
            )
            .await
            .map_err(|s| {
                if s.code == StatusCode::AlreadyPresent {
                    coded(Code::OBJECT_ALREADY_PRESENT, s.code, s.message)
                } else {
                    unknown(&s)
                }
            })?;
        resp.set_snapshot_schedule_id(id);
        Ok(())
    }

    /// `SnapshotScheduleInfoPB` with the schedule's snapshots, without details.
    fn schedule_info(
        &self,
        term: i64,
        snapshot: &CatalogSnapshot,
        rows: &[SnapshotRow],
        schedule: &ScheduleRow,
    ) -> Result<SnapshotScheduleInfoPB, MasterErrorPB> {
        let mut info = SnapshotScheduleInfoPB::new();
        info.set_id(schedule.id.clone());
        let mut options = schedule.options.clone();
        // The namespace may have been renamed since the schedule was made.
        if let Some(snapshot_schedule_filter_pb::Filter::Tables(t)) =
            options.filter.as_mut().and_then(|f| f.filter.as_mut())
        {
            for table in t.tables.iter_mut() {
                let Some(ns) = table.namespace.as_mut() else {
                    continue;
                };
                if ns.id().is_empty() {
                    continue;
                }
                if let Some(name) = snapshot
                    .get(SysRowEntryType::NAMESPACE, ns.id())
                    .and_then(|n| n.namespace().map(|n| n.name.clone()))
                {
                    ns.set_name(name);
                }
            }
        }
        info.options = MessageField::some(options);
        let mut no_details = ListSnapshotsDetailOptionsPB::new();
        no_details.set_show_namespace_details(false);
        no_details.set_show_udtype_details(false);
        no_details.set_show_table_details(false);
        no_details.set_show_tablet_details(false);
        for row in schedule_snapshots(rows, &schedule.id) {
            info.snapshots
                .push(self.snapshot_info(term, row, &no_details)?);
        }
        Ok(info)
    }

    /// `ListSnapshotSchedules`.
    pub fn list_schedules(
        &self,
        token: &LeaderToken,
        req: &ListSnapshotSchedulesRequestPB,
        resp: &mut ListSnapshotSchedulesResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let rows = snapshot_rows(&snapshot);
        match ids::try_decode(req.snapshot_schedule_id()) {
            None => {
                for schedule in schedule_rows(&snapshot) {
                    resp.schedules.push(self.schedule_info(
                        token.term(),
                        &snapshot,
                        &rows,
                        &schedule,
                    )?);
                }
            }
            Some(id) => {
                let schedule = find_schedule(&snapshot, id)?;
                resp.schedules.push(self.schedule_info(
                    token.term(),
                    &snapshot,
                    &rows,
                    &schedule,
                )?);
            }
        }
        Ok(())
    }

    /// `DeleteSnapshotSchedule`: sets the delete time; the schedule's snapshots are deleted and
    /// then the row by the controller.
    pub async fn delete_schedule(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteSnapshotScheduleRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let id = ids::try_decode(req.snapshot_schedule_id())
            .unwrap_or_default()
            .to_vec();
        let now = self.host.now().map_err(|s| unknown(&s))?;
        self.update_schedule(token, deadline, &id, |options| {
            options.set_delete_time(now);
            Ok(())
        })
        .await
        .map(|_| ())
    }

    /// `EditSnapshotSchedule`.
    pub async fn edit_schedule(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &EditSnapshotScheduleRequestPB,
        resp: &mut EditSnapshotScheduleResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = ids::try_decode(req.snapshot_schedule_id())
            .unwrap_or_default()
            .to_vec();
        self.update_schedule(token, deadline, &id, |options| {
            if req.has_interval_sec() {
                options.set_interval_sec(req.interval_sec());
            }
            if req.has_retention_duration_sec() {
                options.set_retention_duration_sec(req.retention_duration_sec());
            }
            validate_schedule_options(options)
        })
        .await?;
        let snapshot = self.catalog.snapshot();
        let schedule = find_schedule(&snapshot, &id)?;
        resp.schedule = MessageField::some(self.schedule_info(
            token.term(),
            &snapshot,
            &snapshot_rows(&snapshot),
            &schedule,
        )?);
        Ok(())
    }

    pub(crate) async fn update_schedule(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        id: &[u8],
        edit: impl Fn(&mut SnapshotScheduleOptionsPB) -> Result<(), yb_host_api::Status>,
    ) -> Result<SnapshotScheduleOptionsPB, MasterErrorPB> {
        let mut not_found = None;
        let result = self
            .writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let schedule = match find_schedule(snapshot, id) {
                        Ok(s) => s,
                        Err(e) => {
                            not_found = Some(e.clone());
                            return Err(crate::status_of(&e));
                        }
                    };
                    let mut options = schedule.options.clone();
                    edit(&mut options)?;
                    let entity = snapshot
                        .get(SysRowEntryType::SNAPSHOT_SCHEDULE, id)
                        .expect("found");
                    let mutation = Mutation::new().read(entity).upsert_message(
                        SysRowEntryType::SNAPSHOT_SCHEDULE,
                        id,
                        &options,
                    )?;
                    Ok((mutation, options))
                },
            )
            .await;
        match result {
            Ok((options, _)) => Ok(options),
            Err(_) if not_found.is_some() => Err(not_found.unwrap()),
            Err(s) => Err(unknown(&s)),
        }
    }

    /// `MaybeClearCreatingSnapshotMarker` and `ForceCreateSnapshot`: claims the schedule's next
    /// snapshot, or reports the one being created.
    pub(crate) fn claim_schedule_snapshot(
        &self,
        term: i64,
        snapshot: &CatalogSnapshot,
        schedule_id: &[u8],
    ) -> Result<Vec<u8>, MasterErrorPB> {
        let mut state = self.state(term);
        if let Some((creating, since)) = state.creating.get(schedule_id).cloned() {
            let finished = match snapshot
                .get(SysRowEntryType::SNAPSHOT, &creating)
                .and_then(|e| {
                    e.parse::<yb_proto::pb::catalog_entity_info::SysSnapshotEntryPB>()
                        .ok()
                }) {
                // Completed creates keep CREATING with terminal tablet states.
                Some(pb) => {
                    pb.state() != State::CREATING
                        || pb
                            .tablet_snapshots
                            .iter()
                            .all(|t| t.state() != State::CREATING)
                }
                None => since.elapsed() > (self.options.missing_creating_snapshot_grace)(),
            };
            if !finished {
                return Err(crate::tagged(
                    Code::PARALLEL_SNAPSHOT_OPERATION,
                    StatusCode::IllegalState,
                    format!(
                        "Creating snapshot in progress: {} (passed {:?})",
                        ids::to_string(&creating),
                        since.elapsed()
                    ),
                ));
            }
            tracing::warn!(
                "Clearing creating snapshot marker {} of schedule {}",
                ids::to_string(&creating),
                ids::to_string(schedule_id)
            );
        }
        let id = ids::random_id();
        state
            .creating
            .insert(schedule_id.to_vec(), (id.clone(), Instant::now()));
        Ok(id)
    }

    /// `SnapshotFinished`: the schedule may take its next snapshot.
    pub(crate) fn schedule_snapshot_finished(&self, term: i64, schedule_id: &[u8], id: &[u8]) {
        let mut state = self.state(term);
        if state
            .creating
            .get(schedule_id)
            .is_some_and(|(creating, _)| creating == id)
        {
            state.creating.remove(schedule_id);
        }
    }

    /// `ExecuteScheduleOperation`: takes the schedule's next snapshot.
    pub(crate) async fn create_schedule_snapshot(
        &self,
        token: &LeaderToken,
        schedule: &ScheduleRow,
        id: &[u8],
        previous_time: u64,
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let entries = match collect_for_snapshot(&snapshot, &schedule.tables(), false) {
            Ok(entries) => entries,
            Err(e) => {
                self.schedule_snapshot_finished(token.term(), &schedule.id, id);
                return Err(e);
            }
        };
        let time = match self.host.max_global_now() {
            Ok(time) => time,
            Err(status) => {
                self.schedule_snapshot_finished(token.term(), &schedule.id, id);
                return Err(unknown(&status));
            }
        };
        let result = self
            .submit_create(
                token,
                id,
                &entries,
                time,
                false,
                &schedule.id,
                previous_time,
                None,
                deadline,
            )
            .await;
        if result.is_err() {
            self.schedule_snapshot_finished(token.term(), &schedule.id, id);
        }
        result
    }

    /// `CreateForSchedule`: a snapshot of the schedule now (a client's `CreateSnapshot` with a
    /// schedule id, or a PITR that needs one).
    pub(crate) async fn create_for_schedule(
        &self,
        token: &LeaderToken,
        schedule_id: &[u8],
        deadline: Instant,
    ) -> Result<Vec<u8>, MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let schedule = find_schedule(&snapshot, schedule_id).map_err(|_| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::NotFound,
                format!("Unknown snapshot schedule: {}", ids::to_string(schedule_id)),
            )
        })?;
        let id = self.claim_schedule_snapshot(token.term(), &snapshot, schedule_id)?;
        let rows = snapshot_rows(&snapshot);
        let previous = schedule_snapshots(&rows, schedule_id)
            .last()
            .map_or(0, |r| r.pb.snapshot_hybrid_time());
        self.create_schedule_snapshot(token, &schedule, &id, previous, deadline)
            .await?;
        Ok(id)
    }
}
