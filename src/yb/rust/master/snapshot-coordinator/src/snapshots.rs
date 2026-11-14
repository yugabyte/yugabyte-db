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

//! Snapshots: `CreateSnapshot`, `ListSnapshots`, `DeleteSnapshot`, and writing a snapshot's
//! tablet outcomes once they are all in (`UpdateSnapshot`).

use std::collections::HashSet;
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use protobuf::{Message, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::backup::tablet_snapshot_op_request_pb::Operation;
use yb_proto::pb::backup::TabletSnapshotOpRequestPB;
use yb_proto::pb::catalog_entity_info::sys_snapshot_entry_pb::{State, TabletSnapshotPB};
use yb_proto::pb::catalog_entity_info::{
    SysRowEntries, SysSnapshotEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_backup::{
    BackupRowEntryPB, CreateSnapshotRequestPB, CreateSnapshotResponsePB, DeleteSnapshotRequestPB,
    ListSnapshotsDetailOptionsPB, ListSnapshotsRequestPB, ListSnapshotsResponsePB, SnapshotInfoPB,
};
use yb_proto::pb::master_rust_peer::{
    get_backup_pg_table_info_request_pb, GetBackupPgTableInfoRequestPB,
    GetBackupPgTableInfoResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::collect::{collect_as_of, collect_entries, CollectFlags, SEQUENCES_DATA_TABLE_ID};
use crate::state::{aggregated_state, merged, snapshot_tablets, Outcome};
use crate::{coded, corrupt, error, ids, internal, unknown, SnapshotCoordinator};

/// `kUseBackupRowEntryFormatVersion`.
const BACKUP_ROW_ENTRY_FORMAT_VERSION: i64 = 2;
/// `kUseRelfilenodeFormatVersion`.
const RELFILENODE_FORMAT_VERSION: i64 = 3;

/// A snapshot row.
#[derive(Clone, Debug)]
pub(crate) struct SnapshotRow {
    pub(crate) id: Vec<u8>,
    pub(crate) pb: SysSnapshotEntryPB,
}

impl SnapshotRow {
    pub(crate) fn schedule_id(&self) -> &[u8] {
        ids::try_decode(self.pb.schedule_id()).unwrap_or_default()
    }

    /// `GetNamespaceId`.
    pub(crate) fn namespace_id(&self) -> Option<Vec<u8>> {
        self.pb
            .entries
            .iter()
            .find(|e| e.type_() == SysRowEntryType::NAMESPACE)
            .map(|e| e.id().as_bytes().to_vec())
    }

    pub(crate) fn tablet_ids(&self) -> Vec<Vec<u8>> {
        self.pb
            .tablet_snapshots
            .iter()
            .map(|t| t.id().to_vec())
            .collect()
    }
}

/// The snapshot rows, by schedule id (the nil id first) and time, as `snapshots_`' schedule index
/// orders them.
pub(crate) fn snapshot_rows(snapshot: &CatalogSnapshot) -> Vec<SnapshotRow> {
    let mut rows: Vec<SnapshotRow> = snapshot
        .iter(SysRowEntryType::SNAPSHOT)
        .filter(|e| ids::try_decode(&e.key.id).is_some())
        .filter_map(|e| {
            e.parse::<SysSnapshotEntryPB>().ok().map(|pb| SnapshotRow {
                id: e.key.id.to_vec(),
                pb,
            })
        })
        .collect();
    rows.sort_by(|a, b| {
        let key = |r: &SnapshotRow| {
            let mut schedule = [0u8; ids::ID_SIZE];
            if let Some(id) = ids::try_decode(r.pb.schedule_id()) {
                schedule.copy_from_slice(id);
            }
            (schedule, r.pb.snapshot_hybrid_time())
        };
        key(a).cmp(&key(b)).then_with(|| a.id.cmp(&b.id))
    });
    rows
}

/// `SnapshotState::HasExpired`.
pub(crate) fn has_expired(pb: &SysSnapshotEntryPB, now: u64, hours_as_millis: bool) -> bool {
    if ids::try_decode(pb.schedule_id()).is_some()
        || !pb.has_retention_duration_hours()
        || pb.retention_duration_hours() < 0
        || pb.snapshot_hybrid_time() == 0
    {
        return false;
    }
    let micros = if hours_as_millis {
        pb.retention_duration_hours() as u64 * 1000
    } else {
        pb.retention_duration_hours() as u64 * 3_600_000_000
    };
    now > pb.snapshot_hybrid_time().saturating_add(micros << 12)
}

/// A google.protobuf.Any holding `entries`, as `PackFrom` makes it.
fn pack(entries: &SysRowEntries) -> Result<protobuf::well_known_types::any::Any, Status> {
    let mut any = protobuf::well_known_types::any::Any::new();
    any.type_url = "type.googleapis.com/yb.master.SysRowEntries".to_string();
    any.value = entries.write_to_bytes().map_err(internal)?;
    Ok(any)
}

impl SnapshotCoordinator {
    /// The snapshot `id`, or `SNAPSHOT_NOT_FOUND`.
    pub(crate) fn find_snapshot(
        snapshot: &CatalogSnapshot,
        id: &[u8],
    ) -> Result<SnapshotRow, MasterErrorPB> {
        snapshot
            .get(SysRowEntryType::SNAPSHOT, id)
            .and_then(|e| e.parse::<SysSnapshotEntryPB>().ok())
            .map(|pb| SnapshotRow {
                id: id.to_vec(),
                pb,
            })
            .ok_or_else(|| {
                coded(
                    Code::SNAPSHOT_NOT_FOUND,
                    StatusCode::NotFound,
                    format!("Could not find snapshot: {}", ids::to_string(id)),
                )
            })
    }

    /// The tablets of `row` with this term's outcomes applied.
    pub(crate) fn current_tablets(&self, term: i64, row: &SnapshotRow) -> Vec<(Vec<u8>, State)> {
        let state = self.state(term);
        merged(snapshot_tablets(&row.pb), state.outcomes.get(&row.id))
    }

    /// The state a client sees.
    pub(crate) fn aggregated(&self, term: i64, row: &SnapshotRow) -> Result<State, Status> {
        aggregated_state(
            row.pb.state(),
            &self.current_tablets(term, row),
            (self.options.mark_snapshot_as_failed)(),
        )
    }

    /// `CreateSnapshot`.
    pub async fn create_snapshot(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateSnapshotRequestPB,
        resp: &mut CreateSnapshotResponsePB,
    ) -> Result<(), MasterErrorPB> {
        tracing::info!("Servicing CreateSnapshot request: {:?}", req);
        if req.has_schedule_id() {
            let schedule_id = ids::try_decode(req.schedule_id()).ok_or_else(|| {
                error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::InvalidArgument,
                    "Invalid snapshot schedule id",
                )
            })?;
            let id = self
                .create_for_schedule(token, schedule_id, deadline)
                .await?;
            resp.set_snapshot_id(id);
            return Ok(());
        }
        if req.has_retention_duration_hours() && req.retention_duration_hours() == 0 {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "Snapshot Ttl value must be non-zero. -1 to retain indefinitely",
            ));
        }
        let flags = CollectFlags {
            include_parent_colocated_table: true,
            add_indexes: req.add_indexes(),
            add_ud_types: req.add_ud_types(),
            succeed_if_create_in_progress: req.imported(),
            include_hidden_tables: false,
        };
        let retention = if req.has_retention_duration_hours() {
            req.retention_duration_hours()
        } else {
            (self.options.default_retention_hours)()
        };
        let snapshot = self.catalog.snapshot();
        let namespace_workflow = (self.options.namespace_snapshot_workflow)()
            && req.tables.len() == 1
            && req.tables[0].table_name().is_empty()
            && req.tables[0].table_id().is_empty()
            && req.tables[0].namespace.as_ref().is_some_and(|n| {
                n.has_id() && n.database_type() == YQLDatabase::YQL_DATABASE_PGSQL
            });
        let id = ids::random_id();
        // Released on every way out before the snapshot row exists; after, when it ends.
        let mut anchor = None;
        let (entries, time) = if namespace_workflow {
            let namespace_id = req.tables[0].namespace.id().to_vec();
            // Its tables are hidden, not deleted, until the snapshot ends.
            self.retain_namespace_for(&namespace_id, &id);
            anchor = Some(NamespaceAnchor {
                coordinator: self,
                snapshot_id: id.clone(),
            });
            let time = self.host.max_global_now().map_err(|s| unknown(&s))?;
            let timeout = deadline.saturating_duration_since(Instant::now());
            let host = self.host.clone();
            let catalog_snapshot = snapshot.clone();
            let collected = tokio::task::spawn_blocking(move || {
                collect_as_of(
                    &*host,
                    &catalog_snapshot,
                    &namespace_id,
                    flags,
                    time,
                    timeout,
                )
            })
            .await
            .map_err(|e| unknown(&internal(e)))?;
            let entries = collected?;
            tracing::info!(
                "Successfully collected entries as of time {time}, num entries: {}",
                entries.entries.len()
            );
            (entries, time)
        } else {
            let entries = collect_entries(&snapshot, &req.tables, flags)?;
            let time = self.host.max_global_now().map_err(|s| unknown(&s))?;
            (entries, time)
        };
        let submitted = self
            .submit_create(
                token,
                &id,
                &entries,
                time,
                req.imported(),
                &[],
                0,
                Some(retention),
                deadline,
            )
            .await;
        submitted?;
        // The snapshot row exists: the controller releases the anchor when it ends.
        std::mem::forget(anchor);
        resp.set_snapshot_id(id);
        Ok(())
    }

    fn retain_namespace_for(&self, namespace_id: &[u8], snapshot_id: &[u8]) {
        self.retained_namespaces
            .lock()
            .unwrap()
            .entry(namespace_id.to_vec())
            .or_default()
            .insert(snapshot_id.to_vec());
    }

    /// Drops the namespace anchor of a snapshot that reached a final state (or never started).
    pub(crate) fn release_namespace_for(&self, snapshot_id: &[u8]) {
        let mut retained = self.retained_namespaces.lock().unwrap();
        retained.retain(|_, snapshots| {
            snapshots.remove(snapshot_id);
            !snapshots.is_empty()
        });
    }

    /// `SubmitCreate`: the `CREATE_ON_MASTER` operation, waited for until `deadline`.
    #[allow(clippy::too_many_arguments)]
    pub(crate) async fn submit_create(
        &self,
        token: &LeaderToken,
        id: &[u8],
        entries: &SysRowEntries,
        time: u64,
        imported: bool,
        schedule_id: &[u8],
        previous_time: u64,
        retention_hours: Option<i32>,
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        let mut req = TabletSnapshotOpRequestPB::new();
        let mut tablets = HashSet::new();
        for entry in &entries.entries {
            if entry.type_() == SysRowEntryType::TABLET
                && tablets.insert(entry.id().as_bytes().to_vec())
            {
                req.tablet_id.push(entry.id().as_bytes().to_vec());
            }
        }
        req.snapshot_hybrid_time = time;
        req.operation = Operation::CREATE_ON_MASTER.into();
        req.snapshot_id = id.to_vec();
        req.imported = imported;
        req.schedule_id = schedule_id.to_vec();
        req.previous_snapshot_hybrid_time = previous_time;
        if (self.options.object_retention_due_to_snapshots)() {
            if let Some(hours) = retention_hours {
                req.retention_duration_hours = hours;
            }
        }
        req.extra_data = MessageField::some(pack(entries).map_err(|s| unknown(&s))?);
        self.submit_op(token, req, deadline).await
    }

    /// Submits a snapshot operation and waits for it until `deadline`.
    pub(crate) async fn submit_op(
        &self,
        token: &LeaderToken,
        req: TabletSnapshotOpRequestPB,
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        self.leadership
            .check(token)
            .map_err(|s| error(Code::NOT_THE_LEADER, s.code, s.message))?;
        let submitted = self.host.submit(token.term(), req);
        match tokio::time::timeout_at(deadline, submitted).await {
            Ok(result) => {
                result.map_err(|s| unknown(&s))?;
                // The operation's rows are in the catalog before the caller reads them.
                self.catalog.barrier().await;
                Ok(())
            }
            Err(_) => Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::TimedOut,
                "Timed out waiting for the snapshot operation",
            )),
        }
    }

    /// `ListSnapshots`.
    pub async fn list_snapshots(
        &self,
        token: &LeaderToken,
        req: &ListSnapshotsRequestPB,
        resp: &mut ListSnapshotsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let id = ids::try_decode(req.snapshot_id());
        if req.prepare_for_backup() && id.is_none() {
            return Err(coded(
                Code::SNAPSHOT_FAILED,
                StatusCode::InvalidArgument,
                format!(
                    "Request must have correct snapshot_id: {}",
                    if req.has_snapshot_id() {
                        String::from_utf8_lossy(req.snapshot_id()).into_owned()
                    } else {
                        "None".to_string()
                    }
                ),
            ));
        }
        let snapshot = self.catalog.snapshot();
        let options = req.detail_options.clone().unwrap_or_default();
        match id {
            None => {
                for row in snapshot_rows(&snapshot) {
                    if !req.list_deleted_snapshots()
                        && self
                            .aggregated(token.term(), &row)
                            .is_ok_and(|s| s == State::DELETED)
                    {
                        continue;
                    }
                    resp.snapshots
                        .push(self.snapshot_info(token.term(), &row, &options)?);
                }
            }
            Some(id) => {
                let row = Self::find_snapshot(&snapshot, id)?;
                resp.snapshots
                    .push(self.snapshot_info(token.term(), &row, &options)?);
            }
        }
        if req.prepare_for_backup() {
            self.repack_for_backup(&snapshot, resp, req.include_ddl_in_progress_tables(), false)
                .await?;
        }
        Ok(())
    }

    /// `SnapshotState::ToPB` for a client.
    pub(crate) fn snapshot_info(
        &self,
        term: i64,
        row: &SnapshotRow,
        options: &ListSnapshotsDetailOptionsPB,
    ) -> Result<SnapshotInfoPB, MasterErrorPB> {
        let mut info = SnapshotInfoPB::new();
        info.set_id(row.id.clone());
        let mut entry = SysSnapshotEntryPB::new();
        entry.set_state(self.aggregated(term, row).map_err(|s| unknown(&s))?);
        entry.set_snapshot_hybrid_time(row.pb.snapshot_hybrid_time());
        if row.pb.previous_snapshot_hybrid_time() != 0 {
            entry.set_previous_snapshot_hybrid_time(row.pb.previous_snapshot_hybrid_time());
        }
        for (id, state) in self.current_tablets(term, row) {
            let mut t = TabletSnapshotPB::new();
            t.set_id(id);
            t.set_state(state);
            entry.tablet_snapshots.push(t);
        }
        for e in &row.pb.entries {
            let show = match e.type_() {
                SysRowEntryType::NAMESPACE => options.show_namespace_details(),
                SysRowEntryType::UDTYPE => options.show_udtype_details(),
                SysRowEntryType::TABLE => options.show_table_details(),
                SysRowEntryType::TABLET => options.show_tablet_details(),
                _ => false,
            };
            if show {
                entry.entries.push(e.clone());
            }
        }
        if let Some(schedule) = ids::try_decode(row.pb.schedule_id()) {
            entry.set_schedule_id(schedule.to_vec());
        }
        if row.pb.has_retention_duration_hours() {
            entry.set_retention_duration_hours(row.pb.retention_duration_hours());
        }
        entry.set_version(row.pb.version());
        entry.set_imported(row.pb.imported());
        info.entry = MessageField::some(entry);
        Ok(info)
    }

    /// `RepackSnapshotsForBackup`: each snapshot's entries as `backup_entries`, with each YSQL
    /// table's PG schema name as of the snapshot, skipping tables PG had not committed then and
    /// invalid indexes.
    pub(crate) async fn repack_for_backup(
        &self,
        snapshot: &CatalogSnapshot,
        resp: &mut ListSnapshotsResponsePB,
        include_ddl_in_progress: bool,
        skip_missing_tables: bool,
    ) -> Result<(), MasterErrorPB> {
        for info in resp.snapshots.iter_mut() {
            info.set_format_version(
                if (self.options.export_snapshot_using_relfilenode)() && include_ddl_in_progress {
                    RELFILENODE_FORMAT_VERSION
                } else {
                    BACKUP_ROW_ENTRY_FORMAT_VERSION
                },
            );
            let entry = info.entry.mut_or_insert_default();
            let read_time = entry.snapshot_hybrid_time();
            // The YSQL tables' PG facts, asked of the host in one call.
            let mut pg_req = GetBackupPgTableInfoRequestPB::new();
            pg_req.set_read_time(read_time);
            for e in &entry.entries {
                let eid = e.id().as_bytes();
                if e.type_() != SysRowEntryType::TABLE || eid == SEQUENCES_DATA_TABLE_ID {
                    continue;
                }
                let Some(current) = snapshot.get(SysRowEntryType::TABLE, eid) else {
                    continue;
                };
                let pb: SysTablesEntryPB = current.parse().map_err(|e| unknown(&corrupt(e)))?;
                if pb.table_type() == TableType::PGSQL_TABLE_TYPE
                    && !master_table_lifecycle::lookup::is_colocation_parent_table_id(eid)
                {
                    let mut t = get_backup_pg_table_info_request_pb::Table::new();
                    t.set_table_id(eid.to_vec());
                    t.table = MessageField::some(pb);
                    pg_req.tables.push(t);
                }
            }
            let pg = if pg_req.tables.is_empty() {
                GetBackupPgTableInfoResponsePB::new()
            } else {
                let bytes = pg_req.write_to_bytes().map_err(|e| unknown(&internal(e)))?;
                let host = self.host.clone();
                let out =
                    tokio::task::spawn_blocking(move || host.call("GetBackupPgTableInfo", &bytes))
                        .await
                        .map_err(|e| unknown(&internal(e)))?
                        .map_err(|s| unknown(&s))?;
                let pg = GetBackupPgTableInfoResponsePB::parse_from_bytes(&out)
                    .map_err(|e| unknown(&corrupt(e)))?;
                if let Some(e) = pg.error.as_ref() {
                    return Err(e.clone());
                }
                pg
            };
            let pg_by_id: std::collections::HashMap<&[u8], _> = pg_req
                .tables
                .iter()
                .map(|t| t.table_id())
                .zip(pg.tables.iter())
                .collect();
            let mut skip: HashSet<Vec<u8>> = HashSet::new();
            let entries = std::mem::take(&mut entry.entries);
            for mut e in entries {
                let eid = e.id().as_bytes().to_vec();
                let mut schema_name = None;
                if e.type_() == SysRowEntryType::TABLE {
                    if eid == SEQUENCES_DATA_TABLE_ID {
                        skip.insert(eid.clone());
                        continue;
                    }
                    let Some(current) = snapshot.get(SysRowEntryType::TABLE, &eid) else {
                        if skip_missing_tables {
                            tracing::info!(
                                "While repacking a snapshot couldn't find snapshotted table {} \
                                 in memory, skipping",
                                String::from_utf8_lossy(&eid)
                            );
                            skip.insert(eid.clone());
                            continue;
                        }
                        return Err(coded(
                            Code::OBJECT_NOT_FOUND,
                            StatusCode::InvalidArgument,
                            format!("Table not found by ID: {}", String::from_utf8_lossy(&eid)),
                        ));
                    };
                    let pb: SysTablesEntryPB = current.parse().map_err(|e| unknown(&corrupt(e)))?;
                    if !include_ddl_in_progress && !pb.ysql_ddl_txn_verifier_state.is_empty() {
                        return Err(error(
                            Code::UNKNOWN_ERROR,
                            StatusCode::IllegalState,
                            format!(
                                "Table {} is undergoing DDL verification, retry later",
                                String::from_utf8_lossy(&eid)
                            ),
                        ));
                    }
                    if let Some(info) = pg_by_id.get(eid.as_slice()) {
                        if info.not_committed() {
                            tracing::warn!(
                                "Skipping backup of table {} : not committed at the snapshot",
                                String::from_utf8_lossy(&eid)
                            );
                            skip.insert(eid.clone());
                            continue;
                        }
                        if let Some(status) = info.status.as_ref() {
                            return Err(error(
                                Code::UNKNOWN_ERROR,
                                StatusCode::from_i32(protobuf::Enum::value(&status.code())),
                                status.message().to_string(),
                            ));
                        }
                        if !pb.indexed_table_id().is_empty() && !info.index_valid() {
                            tracing::info!(
                                "Skipping backup of invalid index table {}",
                                String::from_utf8_lossy(&eid)
                            );
                            skip.insert(eid.clone());
                            continue;
                        }
                        schema_name = Some(info.pg_schema_name().to_vec());
                        // An ALTER that raced the tablets' snapshots may have left the snapshot's
                        // schema version behind theirs; a restore needs it at least as large.
                        let mut snapshotted = SysTablesEntryPB::parse_from_bytes(e.data())
                            .map_err(|e| unknown(&corrupt(e)))?;
                        if pb.version() > snapshotted.version() {
                            tracing::info!(
                                "Overriding snapshotted schema version for table {} from {} to {} \
                                 during snapshot repack for backup",
                                String::from_utf8_lossy(&eid),
                                snapshotted.version(),
                                pb.version()
                            );
                            snapshotted.set_version(pb.version());
                            e.set_data(
                                snapshotted
                                    .write_to_bytes()
                                    .map_err(|e| unknown(&internal(e)))?,
                            );
                        }
                    }
                } else if !skip.is_empty() && e.type_() == SysRowEntryType::TABLET {
                    let tablet = SysTabletsEntryPB::parse_from_bytes(e.data())
                        .map_err(|e| unknown(&corrupt(e)))?;
                    if skip.contains(tablet.table_id()) {
                        tracing::warn!(
                            "Skipping backup of tablet {} since its table {} was skipped.",
                            String::from_utf8_lossy(&eid),
                            String::from_utf8_lossy(tablet.table_id())
                        );
                        continue;
                    }
                }
                let mut backup = BackupRowEntryPB::new();
                if let Some(name) = schema_name {
                    backup.set_pg_schema_name(name);
                }
                backup.entry = MessageField::some(e);
                info.backup_entries.push(backup);
            }
            entry.tablet_snapshots.clear();
        }
        Ok(())
    }

    /// `DeleteSnapshot`.
    pub async fn delete_snapshot(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteSnapshotRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let id = ids::try_decode(req.snapshot_id()).ok_or_else(|| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                format!(
                    "Invalid snapshot id: {}",
                    String::from_utf8_lossy(req.snapshot_id())
                ),
            )
        })?;
        tracing::info!(
            "Servicing DeleteSnapshot request. id: {}, request: {:?}",
            ids::to_string(id),
            req
        );
        let snapshot = self.catalog.snapshot();
        let row = Self::find_snapshot(&snapshot, id)?;
        if let Some(restoration) = self.in_progress_restoration_using(&snapshot, token.term(), id) {
            return Err(coded(
                Code::INVALID_REQUEST,
                StatusCode::InvalidArgument,
                format!(
                    "Cannot delete snapshot {0}, restoration {1} is in progress and using \
                     snapshot {0}",
                    ids::to_string(id),
                    ids::to_string(&restoration)
                ),
            ));
        }
        self.try_start_delete(token.term(), &row)?;
        let result = self.submit_delete(token, id, deadline).await;
        if result.is_err() {
            self.state(token.term()).deleting.remove(id);
        }
        result
    }

    /// `TryStartDelete`.
    pub(crate) fn try_start_delete(
        &self,
        term: i64,
        row: &SnapshotRow,
    ) -> Result<(), MasterErrorPB> {
        let tablets = self.current_tablets(term, row);
        let mut state = self.state(term);
        if row.pb.state() == State::DELETING || state.deleting.contains(&row.id) {
            let message = if tablets.iter().all(|(_, s)| *s == State::DELETED) {
                "The snapshot was deleted"
            } else {
                "The snapshot is being deleted"
            };
            return Err(coded(
                Code::SNAPSHOT_NOT_FOUND,
                StatusCode::NotFound,
                format!("{message}: {}", ids::to_string(&row.id)),
            ));
        }
        state.deleting.insert(row.id.clone());
        Ok(())
    }

    /// `SubmitDelete`: the `DELETE_ON_MASTER` operation.
    pub(crate) async fn submit_delete(
        &self,
        token: &LeaderToken,
        id: &[u8],
        deadline: Instant,
    ) -> Result<(), MasterErrorPB> {
        let _row = self.lock_object(token.term(), id).await;
        let mut req = TabletSnapshotOpRequestPB::new();
        req.operation = Operation::DELETE_ON_MASTER.into();
        req.snapshot_id = id.to_vec();
        self.submit_op(token, req, deadline).await
    }

    /// `UpdateSnapshot` once a snapshot's tablets are all done: its row with their states.
    pub(crate) async fn write_outcomes(
        &self,
        token: LeaderToken,
        entry_type: SysRowEntryType,
        id: Vec<u8>,
    ) -> Result<(), Status> {
        let outcomes = {
            let state = self.state(token.term());
            state.outcomes.get(&id).cloned().unwrap_or_default()
        };
        if outcomes.is_empty() {
            return Ok(());
        }
        let written = self
            .writer
            .mutate_with(
                &token,
                WriteOptions::with_timeout(Duration::from_secs(30)),
                |snapshot| {
                    let Some(row) = snapshot.get(entry_type, &id) else {
                        return Ok((Mutation::new(), ()));
                    };
                    let data = apply_outcomes(entry_type, &row.raw, &outcomes)?;
                    Ok((Mutation::new().read(row).upsert(entry_type, &id, data), ()))
                },
            )
            .await;
        if written.is_ok() {
            let mut state = self.state(token.term());
            if let Some(pending) = state.outcomes.get_mut(&id) {
                pending.retain(|tablet, o| outcomes.get(tablet) != Some(o));
                if pending.is_empty() {
                    state.outcomes.remove(&id);
                }
            }
        }
        written.map(|_| ())
    }
}

/// The row with its tablets' outcomes and the next version, as `StoreToWriteBatch` writes it.
fn apply_outcomes(
    entry_type: SysRowEntryType,
    raw: &[u8],
    outcomes: &std::collections::HashMap<Vec<u8>, Outcome>,
) -> Result<Vec<u8>, Status> {
    match entry_type {
        SysRowEntryType::SNAPSHOT => {
            let mut pb = SysSnapshotEntryPB::parse_from_bytes(raw).map_err(corrupt)?;
            for t in pb.tablet_snapshots.iter_mut() {
                if let Some(o) = outcomes.get(t.id()).filter(|o| o.from == t.state()) {
                    t.set_state(o.state);
                }
            }
            pb.set_version(pb.version() + 1);
            pb.write_to_bytes().map_err(internal)
        }
        SysRowEntryType::SNAPSHOT_RESTORATION => {
            let mut pb = yb_proto::pb::master_backup::SysRestorationEntryPB::parse_from_bytes(raw)
                .map_err(corrupt)?;
            for t in pb.tablet_restorations.iter_mut() {
                if let Some(o) = outcomes.get(t.id()).filter(|o| o.from == t.state()) {
                    t.set_state(o.state);
                }
            }
            pb.set_version(pb.version() + 1);
            pb.write_to_bytes().map_err(internal)
        }
        other => Err(Status::new(
            StatusCode::InvalidArgument,
            format!("No tablet states in {other:?} rows"),
        )),
    }
}

/// A namespace retained for a snapshot being created; released on drop.
struct NamespaceAnchor<'a> {
    coordinator: &'a SnapshotCoordinator,
    snapshot_id: Vec<u8>,
}

impl Drop for NamespaceAnchor<'_> {
    fn drop(&mut self) {
        self.coordinator.release_namespace_for(&self.snapshot_id);
    }
}
