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

//! `CloneStateManager`: a namespace cloned as of a time from its snapshot schedule.
//!
//! CLONE_SCHEMA_STARTED: the clone row is written with the source namespace's next
//! `clone_request_seq_no`; for YSQL a tserver runs `ClonePgSchema` (ysql_dump of the source as of
//! the time into the target database). The snapshot's objects are then imported under the target
//! name, an imported snapshot of the new tables is created, and every source tablet's leader gets
//! `CloneTablet` (CREATING). Once the target tablets run, the target snapshot is restored to the
//! clone time (RESTORING). YSQL then clears the tservers' metacaches and enables connections to
//! the new database (RESTORED, then COMPLETE); YCQL is COMPLETE.
//!
//! What a clone needs between steps (its tablets and snapshots) is kept for the term only; a clone
//! not finished when its term ends is aborted, as C++ aborts it on load.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::sync::Arc;
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation, WriteOptions};
use master_leadership::LeaderToken;
use master_table_lifecycle::lookup::{find_namespace, is_colocation_parent_table_id};
use master_table_lifecycle::{pg_database_oid, TableLifecycle};
use master_task_runtime::{Attempt, TermScope};
use protobuf::{Message, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_clone_state_pb::State as CloneState, sys_snapshot_entry_pb::State as SnapshotState,
    sys_tables_entry_pb, sys_tablets_entry_pb, ysql_major_catalog_upgrade_info_pb, SysCloneStatePB,
    SysConfigEntryPB, SysNamespaceEntryPB, SysRowEntry, SysTablesEntryPB, SysTabletsEntryPB,
    YsqlMajorCatalogUpgradeInfoPB,
};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_backup::{
    BackupRowEntryPB, CloneNamespaceRequestPB, CloneNamespaceResponsePB, CreateSnapshotRequestPB,
    CreateSnapshotResponsePB, ListClonesRequestPB, ListClonesResponsePB,
    ListSnapshotRestorationsRequestPB, ListSnapshotRestorationsResponsePB, ListSnapshotsResponsePB,
    SnapshotInfoPB,
};
use yb_proto::pb::master_rust_peer::{
    ClosestLiveTserverRequestPB, ClosestLiveTserverResponsePB, CountPgYbMigrationRowsRequestPB,
    CountPgYbMigrationRowsResponsePB, ScanSnapshotSysCatalogRequestPB,
    ScanSnapshotSysCatalogResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::operations::CloneTabletRequestPB;
use yb_proto::pb::tablet_metadata::TableInfoPB;
use yb_proto::pb::tserver::{ClearMetacacheRequestPB, ClearMetacacheResponsePB};
use yb_proto::pb::tserver_admin::{
    ClonePgSchemaRequestPB, ClonePgSchemaResponsePB, CloneTabletResponsePB, EnableDbConnsRequestPB,
    EnableDbConnsResponsePB,
};
use yb_proto::pb::tserver_types::TabletServerErrorPB;

use crate::import::{Imported, ImportedTable};
use crate::restore::ht_string;
use crate::schedules::{find_schedule, schedule_rows};
use crate::state::schedule_deleted;
use crate::{corrupt, error, ids, internal, status_of, unknown, SnapshotCoordinator};

const ADMIN_SERVICE: &str = "yb.tserver.TabletServerAdminService";
const TSERVER_SERVICE: &str = "yb.tserver.TabletServerService";
const YSQL_CATALOG_CONFIG_ID: &[u8] = b"ysql_catalog_config";

/// Flags of the clone flow, read when used.
#[derive(Clone, Copy, Debug)]
pub struct CloneOptions {
    /// The `enable_db_clone` AutoFlag.
    pub enabled: fn() -> bool,
    /// `ysql_clone_pg_schema_rpc_timeout_ms`.
    pub pg_schema_rpc_timeout: fn() -> Duration,
    /// `TEST_fail_clone_pg_schema`.
    pub fail_pg_schema: fn() -> bool,
    /// `TEST_fail_clone_tablets`.
    pub fail_tablets: fn() -> bool,
}

impl Default for CloneOptions {
    fn default() -> Self {
        CloneOptions {
            enabled: || true,
            pg_schema_rpc_timeout: || Duration::from_secs(600),
            fail_pg_schema: || false,
            fail_tablets: || false,
        }
    }
}

/// What a clone started this term needs in its later steps (`CloneStateInfo`'s in-memory part).
#[derive(Clone, Debug, Default)]
pub(crate) struct CloneRun {
    /// (source tablet, target tablet).
    tablets: Vec<(Vec<u8>, Vec<u8>)>,
    target_snapshot_id: Vec<u8>,
    restoration_id: Vec<u8>,
    /// A step is running.
    busy: bool,
}

/// A clone row.
#[derive(Clone, Debug)]
struct CloneRow {
    id: Vec<u8>,
    pb: SysCloneStatePB,
}

fn clone_rows(snapshot: &CatalogSnapshot) -> Vec<CloneRow> {
    let mut rows: Vec<CloneRow> = snapshot
        .iter(SysRowEntryType::CLONE_STATE)
        .filter_map(|e| {
            Some(CloneRow {
                id: e.key.id.to_vec(),
                pb: e.parse().ok()?,
            })
        })
        .collect();
    rows.sort_by(|a, b| {
        (a.pb.source_namespace_id(), a.pb.clone_request_seq_no())
            .cmp(&(b.pb.source_namespace_id(), b.pb.clone_request_seq_no()))
    });
    rows
}

/// `CloneStateInfoHelpers::IsDone`.
fn is_done(pb: &SysCloneStatePB) -> bool {
    matches!(
        pb.aggregate_state(),
        CloneState::ABORTED | CloneState::COMPLETE
    )
}

fn failed(code: StatusCode, message: impl Into<String>) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, code, message)
}

/// The tserver error of a response as a status.
fn ts_status(e: &TabletServerErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
        e.status.message().to_string(),
    )
}

/// The snapshot a clone imports and what its tablets are cloned from.
struct CloneSnapshotInfo {
    info: SnapshotInfoPB,
    /// Tablets running at the clone time that the snapshot does not hold: hidden or split before
    /// it was taken, so they are cloned from their active RocksDB.
    not_snapshotted: HashSet<Vec<u8>>,
}

impl SnapshotCoordinator {
    /// `ListClones`.
    pub fn list_clones(&self, req: &ListClonesRequestPB, resp: &mut ListClonesResponsePB) {
        for row in clone_rows(&self.catalog.snapshot()) {
            if req.has_source_namespace_id()
                && (row.pb.source_namespace_id() != req.source_namespace_id()
                    || (req.has_seq_no() && row.pb.clone_request_seq_no() != req.seq_no()))
            {
                continue;
            }
            resp.entries.push(row.pb);
        }
    }

    /// `CloneNamespace`. A clone that fails after its row is written is marked ABORTED, and the
    /// call still answers with its source namespace and sequence number.
    pub async fn clone_namespace(
        self: &Arc<Self>,
        tables: Arc<TableLifecycle>,
        token: &LeaderToken,
        deadline: Instant,
        req: &CloneNamespaceRequestPB,
        resp: &mut CloneNamespaceResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if req.target_namespace_name().is_empty() {
            return Err(failed(
                StatusCode::InvalidArgument,
                "Got empty target namespace name",
            ));
        }
        tracing::info!("Servicing CloneNamespace request: {:?}", req);
        if !(self.options.clone.enabled)() {
            return Err(failed(
                StatusCode::ConfigurationError,
                "FLAGS_enable_db_clone is disabled",
            ));
        }
        let source = req.source_namespace.clone().unwrap_or_default();
        if !source.has_database_type() || !source.has_name() {
            return Err(failed(
                StatusCode::InvalidArgument,
                format!(
                    "Expected source namespace identifier to have database type and name. Got: \
                     {source:?}"
                ),
            ));
        }
        let snapshot = self.catalog.snapshot();
        let namespace = find_namespace(&snapshot, &source)?;
        let source_id = namespace.key.id.to_vec();
        let schedule_id = schedule_rows(&snapshot)
            .into_iter()
            .filter(|s| !schedule_deleted(&s.options))
            .find(|s| {
                s.tables()
                    .first()
                    .is_some_and(|t| t.namespace.id() == source_id.as_slice())
            })
            .map(|s| s.id)
            .ok_or_else(|| {
                failed(
                    StatusCode::InvalidArgument,
                    format!(
                        "Could not find snapshot schedule for namespace {}",
                        source.name()
                    ),
                )
            })?;
        let restore_time = req.restore_ht();
        self.forward_restore_check(&snapshot, &schedule_id, restore_time)?;
        let (clone_id, seq_no) = self
            .create_clone_state(token, deadline, &source_id, source.database_type(), req)
            .await?;
        let tried = self
            .try_clone(
                tables,
                token,
                deadline,
                &clone_id,
                &schedule_id,
                &source_id,
                namespace
                    .namespace()
                    .map(|n| n.name.clone())
                    .unwrap_or_default(),
                req,
            )
            .await;
        if let Err(e) = tried {
            self.mark_aborted(token, &clone_id, &status_of(&e).to_string())
                .await;
        }
        tracing::info!(
            "source_namespace_id: {}, seq_no: {seq_no}",
            String::from_utf8_lossy(&source_id)
        );
        resp.set_source_namespace_id(String::from_utf8_lossy(&source_id).into_owned());
        resp.set_seq_no(seq_no);
        Ok(())
    }

    /// `CreateCloneState`: the namespace's next sequence number and the clone row, in one write.
    async fn create_clone_state(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        source_id: &[u8],
        database_type: YQLDatabase,
        req: &CloneNamespaceRequestPB,
    ) -> Result<(Vec<u8>, u32), MasterErrorPB> {
        let clone_id = master_table_lifecycle::generate_object_id();
        // Registered first: a clone row this term does not know is aborted by the controller.
        self.state(token.term())
            .clones
            .insert(clone_id.clone(), CloneRun::default());
        let written = self
            .writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let mut mutation = Mutation::new();
                    let ongoing = clone_rows(snapshot)
                        .into_iter()
                        .filter(|r| r.pb.source_namespace_id().as_bytes() == source_id)
                        .inspect(|r| {
                            if let Some(e) = snapshot.get(SysRowEntryType::CLONE_STATE, &r.id) {
                                mutation = std::mem::take(&mut mutation).read(e);
                            }
                        })
                        .last()
                        .filter(|r| !is_done(&r.pb));
                    if let Some(r) = ongoing {
                        return Err(Status::new(
                            StatusCode::AlreadyPresent,
                            format!(
                                "Cannot create new clone state because there is already an \
                                 ongoing clone for source namespace {} in state {}",
                                String::from_utf8_lossy(source_id),
                                r.pb.aggregate_state() as i32
                            ),
                        ));
                    }
                    let entity = snapshot
                        .get(SysRowEntryType::NAMESPACE, source_id)
                        .ok_or_else(|| Status::new(StatusCode::NotFound, "namespace is gone"))?;
                    let mut ns: SysNamespaceEntryPB = entity.parse().map_err(corrupt)?;
                    let seq_no = ns.clone_request_seq_no() + 1;
                    ns.set_clone_request_seq_no(seq_no);
                    let mut pb = SysCloneStatePB::new();
                    pb.set_aggregate_state(CloneState::CLONE_SCHEMA_STARTED);
                    pb.set_clone_request_seq_no(seq_no);
                    pb.set_source_namespace_id(String::from_utf8_lossy(source_id).into_owned());
                    pb.set_source_namespace_name(String::from_utf8_lossy(ns.name()).into_owned());
                    pb.set_restore_time(req.restore_ht());
                    pb.set_target_namespace_name(req.target_namespace_name().to_string());
                    pb.set_database_type(database_type);
                    let mutation = mutation
                        .read(entity)
                        .upsert_message(SysRowEntryType::NAMESPACE, source_id, &ns)?
                        .read_absent(SysRowEntryType::CLONE_STATE, &clone_id)
                        .upsert_message(SysRowEntryType::CLONE_STATE, &clone_id, &pb)?;
                    Ok((mutation, seq_no))
                },
            )
            .await;
        match written {
            Ok((seq_no, _)) => Ok((clone_id, seq_no)),
            Err(s) => {
                self.state(token.term()).clones.remove(&clone_id);
                Err(if s.code == StatusCode::AlreadyPresent {
                    failed(s.code, s.message)
                } else {
                    unknown(&s)
                })
            }
        }
    }

    /// `TryCloneNamespace`.
    #[allow(clippy::too_many_arguments)]
    async fn try_clone(
        self: &Arc<Self>,
        tables: Arc<TableLifecycle>,
        token: &LeaderToken,
        deadline: Instant,
        clone_id: &[u8],
        schedule_id: &[u8],
        source_id: &[u8],
        source_name: String,
        req: &CloneNamespaceRequestPB,
    ) -> Result<(), MasterErrorPB> {
        let restore_time = req.restore_ht();
        let info = self
            .clone_snapshot_info(token, schedule_id, restore_time, deadline)
            .await?;
        let target = req.target_namespace_name().to_string();
        if req.source_namespace.database_type() != YQLDatabase::YQL_DATABASE_PGSQL {
            return self
                .import_and_start_cloning(
                    &tables, token, deadline, clone_id, source_id, &target, info,
                )
                .await;
        }
        self.validate_ysql_upgrade_states(&tables, source_id, restore_time)
            .await?;
        if (self.options.clone.fail_pg_schema)() {
            return Err(failed(
                StatusCode::RuntimeError,
                "Failing clone due to test flag fail_clone_pg_schema",
            ));
        }
        let ts = self.closest_live_tserver().await?;
        // Tablets are cloned once ysql_dump created the target database: the rest runs in the
        // background, as C++ runs it in the ClonePgSchema callback.
        let timeout = (self.options.clone.pg_schema_rpc_timeout)();
        let mut pg = ClonePgSchemaRequestPB::new();
        pg.set_source_db_name(source_name.into_bytes());
        pg.set_target_db_name(target.clone().into_bytes());
        pg.set_target_owner(req.pg_target_owner().as_bytes().to_vec());
        pg.set_restore_ht(restore_time);
        let (this, token, clone_id, source_id) =
            (self.clone(), *token, clone_id.to_vec(), source_id.to_vec());
        tokio::spawn(async move {
            let deadline = Instant::now() + timeout;
            let mut status = this
                .ts_call(&ts, ADMIN_SERVICE, "ClonePgSchema", &pg, deadline, timeout)
                .await
                .and_then(|r: ClonePgSchemaResponsePB| {
                    r.error.as_ref().map_or(Ok(()), |e| Err(ts_status(e)))
                });
            tracing::info!("Done ClonePgSchema: {status:?}");
            if status.is_ok() {
                status = this
                    .import_and_start_cloning(
                        &tables, &token, deadline, &clone_id, &source_id, &target, info,
                    )
                    .await
                    .map_err(|e| status_of(&e));
            }
            if let Err(s) = status {
                this.mark_aborted(&token, &clone_id, &s.to_string()).await;
            }
        });
        Ok(())
    }

    /// `ValidateYSQLUpgradeStates`: no YSQL upgrade runs now, ran at the clone time, or happened
    /// since.
    async fn validate_ysql_upgrade_states(
        &self,
        tables: &TableLifecycle,
        source_id: &[u8],
        restore_time: u64,
    ) -> Result<(), MasterErrorPB> {
        let not_supported = |m: String| failed(StatusCode::NotSupported, m);
        let snapshot = self.catalog.snapshot();
        if tables.ysql_major_upgrade_in_progress(&snapshot) {
            return Err(not_supported(
                "Cannot clone database: YSQL major catalog upgrade is in progress".into(),
            ));
        }
        let info_of = |data: &[u8]| -> Option<YsqlMajorCatalogUpgradeInfoPB> {
            let config = SysConfigEntryPB::parse_from_bytes(data).ok()?;
            config
                .ysql_catalog_config()
                .ysql_major_catalog_upgrade_info
                .clone()
                .into_option()
        };
        let now_info = snapshot
            .get(SysRowEntryType::SYS_CONFIG, YSQL_CATALOG_CONFIG_ID)
            .and_then(|e| info_of(&e.raw));
        let host = self.host.clone();
        let rows = tokio::task::spawn_blocking(move || {
            host.scan_at(
                SysRowEntryType::SYS_CONFIG,
                restore_time,
                Duration::from_secs(30),
            )
        })
        .await
        .map_err(|e| unknown(&internal(e)))?
        .map_err(|s| unknown(&s))?;
        let then_info = rows
            .iter()
            .find(|(id, _)| id == YSQL_CATALOG_CONFIG_ID)
            .and_then(|(_, data)| info_of(data));
        if let Some(then) = then_info.as_ref() {
            if then.state() != ysql_major_catalog_upgrade_info_pb::State::DONE {
                return Err(not_supported(format!(
                    "Cannot clone database: YSQL major catalog upgrade was in state {:?} at the \
                     restore time",
                    then.state()
                )));
            }
        }
        if then_info.is_some() != now_info.is_some()
            || now_info
                .as_ref()
                .zip(then_info.as_ref())
                .is_some_and(|(n, t)| n.catalog_version() != t.catalog_version())
        {
            return Err(not_supported(
                "Cannot clone database: YSQL major catalog upgrade occurred between the restore \
                 time and now"
                    .into(),
            ));
        }
        let db_oid = pg_database_oid(source_id).ok_or_else(|| {
            failed(
                StatusCode::InvalidArgument,
                format!(
                    "Invalid YSQL namespace id {}",
                    String::from_utf8_lossy(source_id)
                ),
            )
        })?;
        let count = |read_time: Option<u64>| {
            let mut req = CountPgYbMigrationRowsRequestPB::new();
            req.set_database_oid(db_oid);
            if let Some(t) = read_time {
                req.set_read_time(t);
            }
            self.host_call::<_, CountPgYbMigrationRowsResponsePB>("CountPgYbMigrationRows", req)
        };
        let current = count(None).await?.count();
        let then = count(Some(restore_time)).await?.count();
        if current != then {
            return Err(not_supported(
                "Cannot clone database: YSQL upgrade was performed since the restore time".into(),
            ));
        }
        Ok(())
    }

    /// `GenerateSnapshotInfoFromScheduleForClone`: a schedule snapshot covering the clone time,
    /// with the namespace, tables and tablets that ran at that time as its entries.
    async fn clone_snapshot_info(
        &self,
        token: &LeaderToken,
        schedule_id: &[u8],
        read_time: u64,
        deadline: Instant,
    ) -> Result<CloneSnapshotInfo, MasterErrorPB> {
        tracing::info!(
            "Servicing GenerateSnapshotInfoFromScheduleForClone for snapshot_schedule_id: {} and \
             read_time: {}",
            ids::to_string(schedule_id),
            ht_string(read_time)
        );
        let snapshot_id = self
            .suitable_snapshot(token, schedule_id, read_time, deadline)
            .await?;
        tracing::info!(
            "Found suitable snapshot for restore: {}",
            ids::to_string(&snapshot_id)
        );
        let snapshot = self.catalog.snapshot();
        let schedule = find_schedule(&snapshot, schedule_id)?;
        let source_id = schedule
            .tables()
            .first()
            .map(|t| t.namespace.id().to_vec())
            .filter(|id| !id.is_empty())
            .ok_or_else(|| {
                failed(
                    StatusCode::IllegalState,
                    format!(
                        "No namespace found in filter for schedule id {}",
                        ids::to_string(schedule_id)
                    ),
                )
            })?;
        // `GetSnapshotInfoForClone`.
        let row = Self::find_snapshot(&snapshot, &snapshot_id)?;
        let mut resp = ListSnapshotsResponsePB::new();
        resp.snapshots
            .push(self.snapshot_info(token.term(), &row, &Default::default())?);
        self.repack_for_backup(&snapshot, &mut resp, false, true)
            .await?;
        let mut info = resp.snapshots.pop().unwrap_or_default();
        let snapshotted: HashSet<Vec<u8>> = info
            .backup_entries
            .iter()
            .filter(|b| b.entry.type_() == SysRowEntryType::TABLET)
            .map(|b| b.entry.id().as_bytes().to_vec())
            .collect();
        info.backup_entries.clear();
        // Not a PITR restore at the target: the snapshot's schema packings replace the current.
        let entry = info.entry.mut_or_insert_default();
        entry.clear_schedule_id();
        entry.clear_previous_snapshot_hybrid_time();
        info.backup_entries = self
            .backup_entries_as_of(&snapshot_id, &source_id, read_time)
            .await?;
        let not_snapshotted = info
            .backup_entries
            .iter()
            .filter(|b| b.entry.type_() == SysRowEntryType::TABLET)
            .map(|b| b.entry.id().as_bytes().to_vec())
            .filter(|id| !snapshotted.contains(id))
            .collect();
        Ok(CloneSnapshotInfo {
            info,
            not_snapshotted,
        })
    }

    /// `GetBackupEntriesAsOfTime`: from the sys catalog as of `read_time` in the snapshot's files,
    /// the namespace, its running visible user tables (colocation parents with children first),
    /// each followed by its active tablets in partition order.
    async fn backup_entries_as_of(
        &self,
        snapshot_id: &[u8],
        source_id: &[u8],
        read_time: u64,
    ) -> Result<Vec<BackupRowEntryPB>, MasterErrorPB> {
        let mut req = ScanSnapshotSysCatalogRequestPB::new();
        req.set_snapshot_id(snapshot_id.to_vec());
        req.set_read_time(read_time);
        for t in [
            SysRowEntryType::NAMESPACE,
            SysRowEntryType::TABLE,
            SysRowEntryType::TABLET,
        ] {
            req.entry_types.push(t as i32);
        }
        let scanned: ScanSnapshotSysCatalogResponsePB =
            self.host_call("ScanSnapshotSysCatalog", req).await?;
        let entry = |id: &[u8], type_: SysRowEntryType, data: Vec<u8>| {
            let mut e = SysRowEntry::new();
            e.set_id(String::from_utf8_lossy(id).into_owned());
            e.set_type(type_);
            e.set_data(data);
            let mut b = BackupRowEntryPB::new();
            b.entry = MessageField::some(e);
            b
        };
        let mut out = Vec::new();
        let mut tables: BTreeMap<Vec<u8>, TableWithTablets> = BTreeMap::new();
        let mut parents: BTreeMap<Vec<u8>, bool> = BTreeMap::new();
        for row in &scanned.rows {
            let id = row.id().as_bytes();
            match row.type_() {
                SysRowEntryType::NAMESPACE if id == source_id => {
                    out.push(entry(id, SysRowEntryType::NAMESPACE, row.data().to_vec()));
                }
                SysRowEntryType::TABLE => {
                    let pb = SysTablesEntryPB::parse_from_bytes(row.data())
                        .map_err(|e| unknown(&corrupt(e)))?;
                    let running = matches!(
                        pb.state(),
                        sys_tables_entry_pb::State::RUNNING | sys_tables_entry_pb::State::ALTERING
                    );
                    if pb.namespace_id() != source_id
                        || !running
                        || pb.hide_state() != sys_tables_entry_pb::HideState::VISIBLE
                        || pb.schema.table_properties.is_ysql_catalog_table()
                    {
                        continue;
                    }
                    if pb.state() == sys_tables_entry_pb::State::ALTERING {
                        return Err(failed(
                            StatusCode::IllegalState,
                            format!(
                                "Table {} is in altering state (cloning to a time when a DDL is \
                                 in progress is not supported yet. See GitHub issue #28814.)",
                                String::from_utf8_lossy(id)
                            ),
                        ));
                    }
                    if pb.colocated() {
                        if is_colocation_parent_table_id(id) {
                            parents.entry(id.to_vec()).or_insert(false);
                        } else {
                            parents.insert(pb.parent_table_id().to_vec(), true);
                        }
                    }
                    tables.insert(id.to_vec(), (pb, Vec::new()));
                }
                _ => {}
            }
        }
        if out.is_empty() {
            return Err(failed(
                StatusCode::IllegalState,
                format!(
                    "Did not find backup entry for namespace {}",
                    String::from_utf8_lossy(source_id)
                ),
            ));
        }
        for row in scanned
            .rows
            .iter()
            .filter(|r| r.type_() == SysRowEntryType::TABLET)
        {
            let pb = SysTabletsEntryPB::parse_from_bytes(row.data())
                .map_err(|e| unknown(&corrupt(e)))?;
            // The active children as of the snapshot: deleted or replaced tablets would conflict
            // on partitions in the import.
            if pb.split_tablet_ids.is_empty()
                && !matches!(
                    pb.state(),
                    sys_tablets_entry_pb::State::DELETED | sys_tablets_entry_pb::State::REPLACED
                )
            {
                if let Some((_, tablets)) = tables.get_mut(pb.table_id()) {
                    tablets.push((row.id().as_bytes().to_vec(), pb));
                }
            }
        }
        let mut add = |id: &[u8], (pb, mut tablets): (SysTablesEntryPB, Vec<_>)| {
            tablets.sort_by(|a: &(Vec<u8>, SysTabletsEntryPB), b| {
                a.1.partition
                    .partition_key_start()
                    .cmp(b.1.partition.partition_key_start())
            });
            let mut table = entry(
                id,
                SysRowEntryType::TABLE,
                pb.write_to_bytes().unwrap_or_default(),
            );
            if !pb.schema.DEPRECATED_pgschema_name().is_empty() {
                table.set_pg_schema_name(pb.schema.DEPRECATED_pgschema_name().as_bytes().to_vec());
            }
            out.push(table);
            for (tablet_id, tablet) in tablets {
                out.push(entry(
                    &tablet_id,
                    SysRowEntryType::TABLET,
                    tablet.write_to_bytes().unwrap_or_default(),
                ));
            }
        };
        // An import needs a colocation parent before its children; one without them is left out.
        for (parent, has_child) in parents {
            if let Some(t) = tables.remove(&parent) {
                if has_child {
                    add(&parent, t);
                }
            }
        }
        for (id, t) in tables {
            add(&id, t);
        }
        Ok(out)
    }

    /// `ImportSnapshotAndStartTabletsCloning`.
    #[allow(clippy::too_many_arguments)]
    async fn import_and_start_cloning(
        &self,
        tables: &Arc<TableLifecycle>,
        token: &LeaderToken,
        deadline: Instant,
        clone_id: &[u8],
        source_id: &[u8],
        target: &str,
        info: CloneSnapshotInfo,
    ) -> Result<(), MasterErrorPB> {
        if (self.options.clone.fail_tablets)() {
            return Err(failed(
                StatusCode::RuntimeError,
                "Failing clone due to test flag fail_clone_tablets",
            ));
        }
        let imported = self
            .import(
                tables.clone(),
                token,
                deadline,
                &info.info,
                Some(target.to_string()),
            )
            .await?;
        if imported.namespaces.len() != 1 {
            return Err(failed(
                StatusCode::IllegalState,
                format!("Expected 1 namespace, got {}", imported.namespaces.len()),
            ));
        }
        let target_id = imported
            .namespaces
            .get(source_id)
            .cloned()
            .unwrap_or_default();
        self.update_clone(token, clone_id, |pb| {
            pb.set_target_namespace_id(String::from_utf8_lossy(&target_id).into_owned());
        })
        .await
        .map_err(|s| unknown(&s))?;
        // Imported snapshots are created complete, so the clone ops may follow at once.
        let mut create = CreateSnapshotRequestPB::new();
        for t in &imported.tables {
            let mut id = yb_proto::pb::master_types::TableIdentifierPB::new();
            id.set_table_id(t.new_id.clone());
            create.tables.push(id);
        }
        create.set_add_indexes(false);
        create.set_imported(true);
        let mut created = CreateSnapshotResponsePB::new();
        self.create_snapshot(token, deadline, &create, &mut created)
            .await?;
        let target_snapshot_id = created.snapshot_id().to_vec();
        let source_snapshot_id = info.info.id().to_vec();
        let tablets = clone_tablets(&imported)?;
        let pairs: Vec<(Vec<u8>, Vec<u8>)> = tablets
            .iter()
            .map(|t| (t.source.clone(), t.target.clone()))
            .collect();
        self.seed_target_peers(token, deadline, &pairs).await?;
        let seq_no = clone_rows(&self.catalog.snapshot())
            .into_iter()
            .find(|r| r.id == clone_id)
            .map_or(0, |r| r.pb.clone_request_seq_no());
        let snapshot = self.catalog.snapshot();
        for tablet in tablets {
            let req = clone_tablet_request(
                &snapshot,
                &tablet,
                &source_snapshot_id,
                &target_snapshot_id,
                seq_no,
                info.not_snapshotted.contains(&tablet.source),
            )?;
            let this_token = *token;
            let rpc = self.rpc().map_err(|s| unknown(&s))?;
            let executor = self.executor.clone();
            let retry = self.retry;
            let lane = self.leader_of(&tablet.source).unwrap_or_default();
            let (catalog_ts, locations, leadership) = (
                self.ts_manager.clone(),
                self.ts_manager.locations.clone(),
                self.leadership.clone(),
            );
            let rpc_timeout = (self.options.rpc_timeout)();
            let now = self.host.now().unwrap_or_default();
            tokio::spawn(async move {
                let source = tablet.source.clone();
                let result = executor
                    .run(&lane, "CloneTablet", &retry, |_| {
                        let current = leadership.check(&this_token);
                        let leader = locations
                            .get(&source)
                            .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()));
                        let address = leader.as_ref().and_then(|l| catalog_ts.rpc_address(l));
                        let (rpc, mut req) = (rpc.clone(), req.clone());
                        async move {
                            current.map_err(Attempt::Fail)?;
                            let (Some(leader), Some((host, port))) = (leader, address) else {
                                return Err(Attempt::Retry(Status::new(
                                    StatusCode::ServiceUnavailable,
                                    "No leader for the tablet",
                                )));
                            };
                            req.set_dest_uuid(leader);
                            req.set_propagated_hybrid_time(now);
                            let bytes = req
                                .write_to_bytes()
                                .map_err(|e| Attempt::Fail(internal(e)))?;
                            let resp = rpc
                                .call(
                                    &host,
                                    port,
                                    ADMIN_SERVICE,
                                    "CloneTablet",
                                    &bytes,
                                    rpc_timeout,
                                )
                                .await
                                .map_err(Attempt::Retry)?;
                            let resp = CloneTabletResponsePB::parse_from_bytes(&resp)
                                .map_err(|e| Attempt::Retry(corrupt(e)))?;
                            match resp.error.as_ref() {
                                // `AsyncCloneTablet::HandleResponse` leaves the task to retry.
                                Some(e) => Err(Attempt::Retry(ts_status(e))),
                                None => Ok(()),
                            }
                        }
                    })
                    .await;
                if let Err(s) = result {
                    tracing::warn!(
                        "CloneTablet for tablet {} failed: {s}",
                        String::from_utf8_lossy(&source)
                    );
                }
            });
        }
        if let Some(run) = self.state(token.term()).clones.get_mut(clone_id) {
            run.tablets = pairs;
            run.target_snapshot_id = target_snapshot_id;
        }
        self.update_clone(token, clone_id, |pb| {
            pb.set_aggregate_state(CloneState::CREATING);
        })
        .await
        .map_err(|s| unknown(&s))
    }

    /// The target tablets get their source tablets' peers: the cloned replicas are made where the
    /// source's are, and a report from a peer not in the config would be tombstoned.
    async fn seed_target_peers(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        pairs: &[(Vec<u8>, Vec<u8>)],
    ) -> Result<(), MasterErrorPB> {
        self.writer
            .mutate_with(
                token,
                WriteOptions {
                    deadline: Some(deadline),
                    ..Default::default()
                },
                |snapshot| {
                    let mut mutation = Mutation::new();
                    for (source, target) in pairs {
                        let (Some(s), Some(t)) = (
                            snapshot.get(SysRowEntryType::TABLET, source),
                            snapshot.get(SysRowEntryType::TABLET, target),
                        ) else {
                            continue;
                        };
                        let mut tpb: SysTabletsEntryPB = t.parse().map_err(corrupt)?;
                        if !tpb.committed_consensus_state.config.peers.is_empty() {
                            continue;
                        }
                        let spb: SysTabletsEntryPB = s.parse().map_err(corrupt)?;
                        let peers = spb.committed_consensus_state.config.peers.clone();
                        if peers.is_empty() {
                            tracing::warn!(
                                "Source tablet {} has no peers at clone scheduling time; cloned \
                                 tablet {} is not seeded",
                                String::from_utf8_lossy(source),
                                String::from_utf8_lossy(target)
                            );
                            continue;
                        }
                        let cstate = tpb.committed_consensus_state.mut_or_insert_default();
                        if !cstate.has_current_term() {
                            cstate.set_current_term(0);
                        }
                        cstate.config.mut_or_insert_default().peers = peers;
                        mutation = mutation.read(t).upsert_message(
                            SysRowEntryType::TABLET,
                            target,
                            &tpb,
                        )?;
                    }
                    Ok((mutation, ()))
                },
            )
            .await
            .map(|_| ())
            .map_err(|s| unknown(&s))
    }

    /// Writes the clone row with `f` applied.
    async fn update_clone(
        &self,
        token: &LeaderToken,
        clone_id: &[u8],
        f: impl Fn(&mut SysCloneStatePB),
    ) -> Result<(), Status> {
        self.writer
            .mutate_with(token, WriteOptions::default(), |snapshot| {
                let entity = snapshot
                    .get(SysRowEntryType::CLONE_STATE, clone_id)
                    .ok_or_else(|| Status::new(StatusCode::NotFound, "clone state is gone"))?;
                let mut pb: SysCloneStatePB = entity.parse().map_err(corrupt)?;
                f(&mut pb);
                let mutation = Mutation::new().read(entity).upsert_message(
                    SysRowEntryType::CLONE_STATE,
                    clone_id,
                    &pb,
                )?;
                Ok((mutation, ()))
            })
            .await
            .map(|_| ())
    }

    /// `MarkCloneAborted`.
    async fn mark_aborted(&self, token: &LeaderToken, clone_id: &[u8], reason: &str) {
        let written = self
            .update_clone(token, clone_id, |pb| {
                tracing::info!(
                    "Aborted clone for source namespace {} because: {reason}.\nseq_no: {}, target \
                     namespace: {}, restore time: {}",
                    pb.source_namespace_id(),
                    pb.clone_request_seq_no(),
                    pb.target_namespace_name(),
                    pb.restore_time()
                );
                pb.set_abort_message(reason.to_string());
                pb.set_aggregate_state(CloneState::ABORTED);
            })
            .await;
        if let Err(s) = written {
            tracing::warn!("Failed to mark clone aborted: {s}");
        }
    }

    /// The controller's clone step (`CloneStateManager::Run`), and aborting clones of earlier
    /// terms (`LoadCloneState`).
    pub(crate) fn poll_clones(
        self: &Arc<Self>,
        scope: &TermScope,
        token: LeaderToken,
        snapshot: &CatalogSnapshot,
    ) {
        let rows = clone_rows(snapshot);
        let mut latest: HashMap<String, CloneRow> = HashMap::new();
        for row in rows {
            let known = self.state(token.term()).clones.contains_key(&row.id);
            if !is_done(&row.pb) && !known {
                if self.state(token.term()).writing.insert(row.id.clone()) {
                    let (this, id) = (self.clone(), row.id.clone());
                    scope.entity().spawn("clone-abort", async move {
                        this.mark_aborted(&token, &id, "aborted by master failover")
                            .await;
                        this.state(token.term()).writing.remove(&id);
                    });
                }
                continue;
            }
            latest.insert(row.pb.source_namespace_id().to_string(), row);
        }
        for row in latest.into_values() {
            let run = {
                let mut state = self.state(token.term());
                let Some(run) = state.clones.get_mut(&row.id) else {
                    continue;
                };
                let step = matches!(
                    row.pb.aggregate_state(),
                    CloneState::CREATING | CloneState::RESTORING
                );
                if run.busy || !step {
                    continue;
                }
                if row.pb.aggregate_state() == CloneState::CREATING
                    && !run.tablets.iter().all(|(_, target)| {
                        snapshot
                            .get(SysRowEntryType::TABLET, target)
                            .and_then(|t| t.tablet())
                            .is_some_and(|t| t.state == sys_tablets_entry_pb::State::RUNNING as i32)
                    })
                {
                    continue;
                }
                run.busy = true;
                run.clone()
            };
            let this = self.clone();
            scope.entity().spawn("clone-step", async move {
                let result = match row.pb.aggregate_state() {
                    CloneState::CREATING => this.restore_clone(&token, &row, &run).await,
                    _ => this.finish_clone(&token, &row, &run).await,
                };
                if let Err(s) = result {
                    this.mark_aborted(&token, &row.id, &s.to_string()).await;
                }
                if let Some(run) = this.state(token.term()).clones.get_mut(&row.id) {
                    run.busy = false;
                }
                this.wake();
            });
        }
    }

    /// `HandleCreatingState` once every target tablet runs: the target snapshot is restored to
    /// the clone time.
    async fn restore_clone(
        &self,
        token: &LeaderToken,
        row: &CloneRow,
        run: &CloneRun,
    ) -> Result<(), Status> {
        tracing::info!(
            "All tablets for cloned namespace {} with seq_no {} are running. Triggering restore.",
            row.pb.source_namespace_id(),
            row.pb.clone_request_seq_no()
        );
        let deadline = Instant::now() + Duration::from_secs(60);
        let restoration_id = self
            .restore(
                token,
                &run.target_snapshot_id,
                row.pb.restore_time(),
                deadline,
            )
            .await
            .map_err(|e| status_of(&e))?;
        if let Some(r) = self.state(token.term()).clones.get_mut(&row.id) {
            r.restoration_id = restoration_id;
        }
        self.update_clone(token, &row.id, |pb| {
            pb.set_aggregate_state(CloneState::RESTORING);
        })
        .await
    }

    /// `HandleRestoringState` once the restoration is done; for YSQL, `ClearMetaCaches` and
    /// `EnableDbConnections`.
    async fn finish_clone(
        &self,
        token: &LeaderToken,
        row: &CloneRow,
        run: &CloneRun,
    ) -> Result<(), Status> {
        let mut req = ListSnapshotRestorationsRequestPB::new();
        req.set_restoration_id(run.restoration_id.clone());
        let mut resp = ListSnapshotRestorationsResponsePB::new();
        self.list_restorations(token, &req, &mut resp)
            .map_err(|e| status_of(&e))?;
        if resp.restorations.len() != 1 {
            return Err(Status::new(
                StatusCode::IllegalState,
                "Unexpected number of restorations.",
            ));
        }
        if resp.restorations[0].entry.state() != SnapshotState::RESTORED {
            return Ok(());
        }
        if row.pb.database_type() != YQLDatabase::YQL_DATABASE_PGSQL {
            return self
                .update_clone(token, &row.id, |pb| {
                    pb.set_aggregate_state(CloneState::COMPLETE);
                })
                .await;
        }
        self.update_clone(token, &row.id, |pb| {
            pb.set_aggregate_state(CloneState::RESTORED);
        })
        .await?;
        // A tserver that kept its metacache could route to the source's tablets through it.
        let target_name = row.pb.target_namespace_name().to_string();
        let target_id = master_table_lifecycle::lookup::find_namespace_by_name(
            &self.catalog.snapshot(),
            &target_name,
            YQLDatabase::YQL_DATABASE_PGSQL as i32,
        )
        .map(|n| n.key.id.to_vec())
        .ok_or_else(|| {
            Status::new(
                StatusCode::NotFound,
                format!("YSQL keyspace name not found: {target_name}"),
            )
        })?;
        let timeout = (self.options.rpc_timeout)();
        for ts in self.ts_manager.live() {
            tracing::info!(
                "Scheduling clear metacache entries task for namespace: {} and tserver with \
                 UUID: {}",
                String::from_utf8_lossy(&target_id),
                String::from_utf8_lossy(&ts.uuid)
            );
            let mut req = ClearMetacacheRequestPB::new();
            req.set_namespace_id(String::from_utf8_lossy(&target_id).into_owned());
            let r: ClearMetacacheResponsePB = self
                .ts_call(
                    &ts.uuid,
                    TSERVER_SERVICE,
                    "ClearMetacache",
                    &req,
                    Instant::now() + Duration::from_secs(60),
                    timeout,
                )
                .await?;
            if let Some(e) = r.error.as_ref() {
                return Err(ts_status(e));
            }
        }
        let ts = self
            .closest_live_tserver()
            .await
            .map_err(|e| status_of(&e))?;
        tracing::info!("Scheduling enable DB Connections Task for database:{target_name} ");
        let mut enable = EnableDbConnsRequestPB::new();
        enable.set_target_db_name(target_name.into_bytes());
        let r: EnableDbConnsResponsePB = self
            .ts_call(
                &ts,
                ADMIN_SERVICE,
                "EnableDbConns",
                &enable,
                Instant::now() + Duration::from_secs(60),
                timeout,
            )
            .await?;
        if let Some(e) = r.error.as_ref() {
            return Err(ts_status(e));
        }
        tracing::info!(
            "Marking clone as complete for source namespace {} with seq_no {}",
            row.pb.source_namespace_id(),
            row.pb.clone_request_seq_no()
        );
        self.update_clone(token, &row.id, |pb| {
            pb.set_aggregate_state(CloneState::COMPLETE);
        })
        .await
    }

    /// `GetClosestLiveTserver`.
    async fn closest_live_tserver(&self) -> Result<Vec<u8>, MasterErrorPB> {
        let mut req = ClosestLiveTserverRequestPB::new();
        for ts in self.ts_manager.live() {
            req.live_tservers.push(ts.info());
        }
        let resp: ClosestLiveTserverResponsePB = self.host_call("ClosestLiveTserver", req).await?;
        Ok(resp.permanent_uuid().to_vec())
    }

    /// One of the host's C++ steps, off the async threads.
    async fn host_call<Req: Message, Resp: protobuf::MessageFull>(
        &self,
        method: &'static str,
        req: Req,
    ) -> Result<Resp, MasterErrorPB> {
        let bytes = req.write_to_bytes().map_err(|e| unknown(&internal(e)))?;
        let host = self.host.clone();
        let out = tokio::task::spawn_blocking(move || host.call(method, &bytes))
            .await
            .map_err(|e| unknown(&internal(e)))?
            .map_err(|s| unknown(&s))?;
        let resp = Resp::parse_from_bytes(&out).map_err(|e| unknown(&corrupt(e)))?;
        if let Some(e) = crate::host_error(&resp) {
            return Err(e);
        }
        Ok(resp)
    }

    /// `RetrySpecificTSRpcTask`: a call to one tserver, retried on transport errors until
    /// `deadline`.
    async fn ts_call<Req: Message, Resp: Message>(
        &self,
        uuid: &[u8],
        service: &str,
        method: &str,
        req: &Req,
        deadline: Instant,
        rpc_timeout: Duration,
    ) -> Result<Resp, Status> {
        let rpc = self.rpc()?;
        let bytes = req.write_to_bytes().map_err(internal)?;
        let mut delay = Duration::from_millis(100);
        loop {
            let last = match self.ts_manager.rpc_address(uuid) {
                Some((host, port)) => {
                    let timeout =
                        rpc_timeout.min(deadline.saturating_duration_since(Instant::now()));
                    match rpc
                        .call(&host, port, service, method, &bytes, timeout)
                        .await
                    {
                        Ok(out) => return Resp::parse_from_bytes(&out).map_err(corrupt),
                        Err(s) => s,
                    }
                }
                None => Status::new(
                    StatusCode::NotFound,
                    format!("Unknown tserver {}", String::from_utf8_lossy(uuid)),
                ),
            };
            if Instant::now() + delay >= deadline {
                return Err(last);
            }
            tracing::warn!(
                "{method} to tserver {} failed, will be retried: {last}",
                String::from_utf8_lossy(uuid)
            );
            tokio::time::sleep(delay).await;
            delay = (delay * 2).min(Duration::from_secs(5));
        }
    }
}

/// A table's row with its tablets (`TableWithTabletsEntries`).
type TableWithTablets = (SysTablesEntryPB, Vec<(Vec<u8>, SysTabletsEntryPB)>);

/// An id of the source's tables mapped to the target's.
type Remap<'a> = dyn Fn(&[u8], &str) -> Result<Vec<u8>, MasterErrorPB> + 'a;

fn lossy(bytes: &[u8]) -> String {
    String::from_utf8_lossy(bytes).into_owned()
}

/// One tablet of a clone (`CloneStateInfo::TabletData`).
struct CloneTablet {
    source: Vec<u8>,
    target: Vec<u8>,
    /// For a colocation parent's tablet: the colocated tables, as the target names them.
    colocated: Vec<TableInfoPB>,
}

/// `UpdateCloneStateWithSnapshotInfo`: each imported tablet, a colocation parent's with the
/// target's colocated tables, whose ids and schemas cannot be copied from the source tablet.
fn clone_tablets(imported: &Imported) -> Result<Vec<CloneTablet>, MasterErrorPB> {
    let new_id_of: HashMap<&[u8], &[u8]> = imported
        .tables
        .iter()
        .map(|t| (t.old_id.as_slice(), t.new_id.as_slice()))
        .collect();
    let remap = |id: &[u8], what: &str| -> Result<Vec<u8>, MasterErrorPB> {
        new_id_of.get(id).map(|n| n.to_vec()).ok_or_else(|| {
            failed(
                StatusCode::NotFound,
                format!("Did not find {what} {}", String::from_utf8_lossy(id)),
            )
        })
    };
    let mut colocated: HashMap<Vec<u8>, Vec<TableInfoPB>> = HashMap::new();
    for t in &imported.tables {
        if !t.pb.colocated() || is_colocation_parent_table_id(&t.new_id) {
            continue;
        }
        colocated
            .entry(t.pb.parent_table_id().to_vec())
            .or_default()
            .push(colocated_table_info(t, &remap)?);
    }
    let mut out = Vec::new();
    for t in &imported.tables {
        let tables = colocated.get(&t.old_id).cloned().unwrap_or_default();
        for ids in &t.meta.tablets_ids {
            out.push(CloneTablet {
                source: ids.old_id().to_vec(),
                target: ids.new_id().to_vec(),
                colocated: tables.clone(),
            });
        }
    }
    Ok(out)
}

/// `FillTableInfoPB` of a colocated table with its index references remapped to the target's
/// tables.
fn colocated_table_info(t: &ImportedTable, remap: &Remap) -> Result<TableInfoPB, MasterErrorPB> {
    let mut pb = TableInfoPB::new();
    pb.set_table_id(t.new_id.clone());
    pb.set_table_name(String::from_utf8_lossy(t.pb.name()).into_owned());
    pb.set_table_type(TableType::PGSQL_TABLE_TYPE);
    pb.schema = t.pb.schema.clone();
    pb.set_schema_version(t.new_schema_version);
    pb.partition_schema = t.pb.partition_schema.clone();
    if let Some(index_info) = t.pb.index_info.as_ref() {
        let mut index_info = index_info.clone();
        index_info.set_table_id(t.new_id.clone());
        index_info.set_indexed_table_id(remap(index_info.indexed_table_id(), "indexed table")?);
        pb.index_info = MessageField::some(index_info);
    }
    for index in &t.pb.indexes {
        let mut index = index.clone();
        index.set_table_id(remap(index.table_id(), "index table")?);
        index.set_indexed_table_id(remap(index.indexed_table_id(), "indexed table")?);
        pb.indexes.push(index);
    }
    Ok(pb)
}

/// `ScheduleCloneOps`' request for one tablet.
fn clone_tablet_request(
    snapshot: &CatalogSnapshot,
    tablet: &CloneTablet,
    source_snapshot_id: &[u8],
    target_snapshot_id: &[u8],
    seq_no: u32,
    from_active_rocksdb: bool,
) -> Result<CloneTabletRequestPB, MasterErrorPB> {
    let target: SysTabletsEntryPB = snapshot
        .get(SysRowEntryType::TABLET, &tablet.target)
        .ok_or_else(|| {
            failed(
                StatusCode::NotFound,
                format!(
                    "Tablet {} not found",
                    String::from_utf8_lossy(&tablet.target)
                ),
            )
        })?
        .parse()
        .map_err(|e| unknown(&corrupt(e)))?;
    let table: SysTablesEntryPB = snapshot
        .get(SysRowEntryType::TABLE, target.table_id())
        .ok_or_else(|| {
            failed(
                StatusCode::NotFound,
                format!(
                    "Table {} not found",
                    String::from_utf8_lossy(target.table_id())
                ),
            )
        })?
        .parse()
        .map_err(|e| unknown(&corrupt(e)))?;
    let mut req = CloneTabletRequestPB::new();
    if from_active_rocksdb {
        tracing::debug!(
            "Cloning tablet {} from active rocksdb since it was deleted or split before snapshot",
            String::from_utf8_lossy(&tablet.source)
        );
        req.set_clone_from_active_rocksdb(true);
    }
    req.set_tablet_id(lossy(&tablet.source));
    req.set_target_tablet_id(lossy(&tablet.target));
    req.set_source_snapshot_id(source_snapshot_id.to_vec());
    req.set_target_snapshot_id(target_snapshot_id.to_vec());
    req.set_target_table_id(lossy(target.table_id()));
    req.set_target_namespace_name(lossy(table.namespace_name()));
    req.set_target_namespace_id(lossy(table.namespace_id()));
    req.set_clone_request_seq_no(seq_no);
    req.set_target_pg_table_id(lossy(table.pg_table_id()));
    if table.index_info.is_some() {
        req.target_index_info = table.index_info.clone();
    }
    req.target_schema = table.schema.clone();
    req.target_partition_schema = table.partition_schema.clone();
    req.target_indexes = table.indexes.clone();
    req.colocated_tables = tablet.colocated.clone();
    Ok(req)
}
