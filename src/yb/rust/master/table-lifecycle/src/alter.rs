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

//! `AlterTable` and `IsAlterTableDone` for YCQL tables, and the `table-alters` controller.
//!
//! The alter is one mutation: the new schema, version + 1 and ALTERING. The controller sends
//! `AlterSchema` to each active tablet's leader once per (tablet, version) in a term, and when
//! every tablet has the version, writes RUNNING and clears the fully applied schema. A new leader
//! sends again; a tablet that has the version or a newer one answers with an error that counts as
//! done, as in C++.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{Attempt, BoxFuture, Controller, RetryPolicy, TermScope, TsRpcExecutor};
use protobuf::{Enum, EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, SysTablesEntryPB,
};
use yb_proto::pb::common_types::{IndexPermissions, TableType};
use yb_proto::pb::master_ddl::{
    AlterTableRequestPB, AlterTableResponsePB, IsAlterTableDoneRequestPB,
    IsAlterTableDoneResponsePB,
};
use yb_proto::pb::master_rust_peer::{AlterTableSchemaRequestPB, AlterTableSchemaResponsePB};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::operations::ChangeMetadataRequestPB;
use yb_proto::pb::tserver_admin::ChangeMetadataResponsePB;
use yb_proto::pb::tserver_types::tablet_server_error_pb;

use crate::lookup::{check_visible, find_table};
use crate::placement::{cluster_config, encode, validate_table_replication_info};
use crate::time::local_time_string;
use crate::{coded_error, error, tagged_error, TableLifecycle};

use sys_tables_entry_pb::State as TableState;

fn corrupt_error(e: impl std::fmt::Display) -> MasterErrorPB {
    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
}

impl TableLifecycle {
    /// `AlterTable` for a YCQL table.
    pub async fn alter_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &AlterTableRequestPB,
        resp: &mut AlterTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if self.ysql_major_upgrade_in_progress(&self.catalog.snapshot()) {
            // Only pg_upgrade's restore can alter during the upgrade, and only PG metadata.
            tracing::info!("Ignoring alter table request during a major YSQL upgrade");
            return Ok(());
        }
        // A rename takes a name; hold the name lock across the check and the write.
        let _names = self.names.lock().await;
        let version = self
            .write(token, deadline, |snapshot| {
                let table = find_table(snapshot, &req.table, false)?;
                let pb: SysTablesEntryPB = table.parse().map_err(corrupt_error)?;
                let is_pg = pb.table_type() == TableType::PGSQL_TABLE_TYPE;
                if pb.table_type() != TableType::YQL_TABLE_TYPE && !is_pg {
                    return Err(error(
                        Code::INVALID_REQUEST,
                        StatusCode::NotSupported,
                        format!(
                            "ALTER TABLE of {:?} is not served by a Rust master leader yet",
                            pb.table_type()
                        ),
                    ));
                }
                let namespace = snapshot
                    .get(SysRowEntryType::NAMESPACE, pb.namespace_id())
                    .ok_or_else(|| {
                        coded_error(
                            Code::NAMESPACE_NOT_FOUND,
                            StatusCode::NotFound,
                            format!(
                                "Keyspace identifier not found: {}",
                                String::from_utf8_lossy(pb.namespace_id())
                            ),
                        )
                    })?;
                let ns = namespace.namespace().expect("namespace row");
                if ns.state != sys_namespace_entry_pb::State::RUNNING as i32 {
                    use sys_namespace_entry_pb::State as NsState;
                    let state = NsState::from_i32(ns.state).unwrap_or(NsState::UNKNOWN);
                    let code = match state {
                        NsState::PREPARING | NsState::DELETING => Code::IN_TRANSITION_CAN_RETRY,
                        _ => Code::INTERNAL_ERROR,
                    };
                    return Err(error(
                        code,
                        StatusCode::TryAgain,
                        format!(
                            "Namespace not running (State={state:?}). Cannot alter table {}.{}",
                            ns.name,
                            String::from_utf8_lossy(pb.name())
                        ),
                    ));
                }
                // YSQL names are pg_class's, not unique here.
                if req.has_new_table_name() && !is_pg {
                    if let Some(other) =
                        snapshot.table_by_name(&namespace.key.id, req.new_table_name())
                    {
                        return Err(error(
                            Code::OBJECT_ALREADY_PRESENT,
                            StatusCode::AlreadyPresent,
                            format!(
                                "Object '{}.{}' already exists",
                                ns.name,
                                other.table().map_or("", |t| t.name.as_str())
                            ),
                        ));
                    }
                }
                check_visible(&table, false)?;
                if pb.state() == TableState::PREPARING {
                    return Err(tagged_error(
                        Code::TABLE_NOT_RUNNING,
                        StatusCode::TryAgain,
                        format!(
                            "Table {} is not ready yet",
                            String::from_utf8_lossy(pb.name())
                        ),
                    ));
                }
                if req.ysql_yb_ddl_rollback_enabled() {
                    crate::ysql::check_no_other_ddl_txn(&pb, req.transaction.transaction_id())?;
                }
                if is_pg && req.replication_info.is_some() {
                    return Err(error(
                        Code::INVALID_REQUEST,
                        StatusCode::InvalidArgument,
                        "Placement policy cannot be altered for YSQL tables, use Tablespaces",
                    ));
                }
                let mut new = pb.clone();
                let mut changed = false;
                let mut ddl_log = Vec::new();
                if let Some(altered) = self.alter_schema(&table.key.id, &pb, req)? {
                    if new.fully_applied_schema.is_none() {
                        copy_to_fully_applied(&mut new);
                    }
                    new.schema = MessageField::some(altered.schema);
                    new.set_next_column_id(altered.next_column_id);
                    ddl_log = altered.ddl_log;
                    changed = true;
                }
                if req.has_new_table_name() {
                    new.set_name(req.new_table_name().as_bytes().to_vec());
                    changed = true;
                }
                if let Some(info) = req.replication_info.as_ref() {
                    validate_table_replication_info(info, &cluster_config(snapshot))
                        .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                    new.replication_info = req.replication_info.clone();
                    changed = true;
                }
                if req.increment_schema_version() {
                    changed = true;
                }
                if req.has_wal_retention_secs() {
                    if changed {
                        return Err(error(
                            Code::INVALID_REQUEST,
                            StatusCode::InvalidArgument,
                            "wal_retention_secs cannot be altered concurrently with other \
                             properties",
                        ));
                    }
                    new.set_wal_retention_secs(req.wal_retention_secs());
                    changed = true;
                }
                if !changed {
                    return Ok((Mutation::new(), (pb.version(), None)));
                }
                if !req.has_wal_retention_secs() {
                    new.set_version(pb.version() + 1);
                    new.set_updates_only_index_permissions(false);
                    crate::ysql::record_alter_in_ddl_txn(&pb, &mut new, req, is_pg);
                }
                new.state = Some(EnumOrUnknown::new(TableState::ALTERING));
                new.set_state_msg(
                    format!(
                        "Alter table version={} ts={}",
                        new.version(),
                        local_time_string()
                    )
                    .into_bytes(),
                );
                let mut mutation = Mutation::new().read(&table).upsert(
                    SysRowEntryType::TABLE,
                    &table.key.id,
                    encode(&new)?,
                );
                for row in &ddl_log {
                    mutation = crate::ddl_log::add(mutation, row)?;
                }
                let txn = (is_pg && !req.transaction.transaction_id().is_empty()).then(|| {
                    (
                        table.key.id.to_vec(),
                        req.transaction.transaction_id().to_vec(),
                    )
                });
                Ok((mutation, (new.version(), txn)))
            })
            .await?;
        let (version, txn) = version;
        if let Some((table_id, txn)) = txn {
            self.alter_txns
                .lock()
                .unwrap()
                .insert(table_id, (version, txn));
        }
        resp.set_schema_version(version);
        Ok(())
    }

    /// `IsAlterTableDone`.
    pub fn is_alter_table_done(
        &self,
        snapshot: &CatalogSnapshot,
        req: &IsAlterTableDoneRequestPB,
        resp: &mut IsAlterTableDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let table = find_table(snapshot, &req.table, false)?;
        check_visible(&table, false)?;
        let hot = table.table().expect("table row");
        resp.set_schema_version(hot.schema_version);
        resp.set_done(hot.state != TableState::ALTERING as i32);
        Ok(())
    }

    /// The host's `ApplyAlterSteps`: the new schema and next column id, if the request changes
    /// the schema.
    fn alter_schema(
        &self,
        table_id: &[u8],
        table: &SysTablesEntryPB,
        req: &AlterTableRequestPB,
    ) -> Result<Option<AlteredSchema>, MasterErrorPB> {
        if req.alter_schema_steps.is_empty()
            && req.alter_properties.is_none()
            && !req.has_pgschema_name()
        {
            return Ok(None);
        }
        let mut alter = AlterTableSchemaRequestPB::new();
        alter.set_table_id(table_id.to_vec());
        alter.table = MessageField::some(table.clone());
        alter.request = MessageField::some(req.clone());
        let bytes = self
            .host
            .call("AlterTableSchema", &encode(&alter)?)
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        let resp = AlterTableSchemaResponsePB::parse_from_bytes(&bytes).map_err(corrupt_error)?;
        if let Some(e) = resp.error.into_option() {
            return Err(e);
        }
        let ddl_log = resp
            .ddl_log_entry_ids
            .into_iter()
            .zip(resp.ddl_log_entries)
            .collect();
        Ok(resp.schema.into_option().map(|schema| AlteredSchema {
            schema,
            next_column_id: resp.next_column_id.unwrap_or(0),
            ddl_log,
        }))
    }
}

/// The host's `ApplyAlterSteps`: the new schema, next column id and DDL log entry.
struct AlteredSchema {
    schema: yb_proto::pb::common::SchemaPB,
    next_column_id: i32,
    ddl_log: Vec<crate::ddl_log::DdlLogRow>,
}

/// `SendAlterTableRequest` after a direct change of a RUNNING table's row: ALTERING, with the
/// schema the tablets have as fully applied, so the alterer sends them the row's schema.
pub fn start_schema_push(current: &SysTablesEntryPB, updated: &mut SysTablesEntryPB) {
    defer_schema_push(current, updated);
    updated.state = Some(EnumOrUnknown::new(TableState::ALTERING));
}

/// [`start_schema_push`] for a table whose tablets are still being created: the push starts when
/// the table would become RUNNING (`SendAlterTableRequest` to a clone's tablets, which C++ retries
/// until they exist).
pub fn defer_schema_push(current: &SysTablesEntryPB, updated: &mut SysTablesEntryPB) {
    let mut applied = current.clone();
    copy_to_fully_applied(&mut applied);
    updated.set_fully_applied_schema_version(applied.fully_applied_schema_version());
    updated.fully_applied_schema = applied.fully_applied_schema;
    updated.fully_applied_indexes = applied.fully_applied_indexes;
    updated.fully_applied_index_info = applied.fully_applied_index_info;
}

/// `MultiStageAlterTable::CopySchemaDetailsToFullyApplied`: clients of non-YCQL tables keep seeing
/// the old schema until every tablet has the new one.
pub(crate) fn copy_to_fully_applied(pb: &mut SysTablesEntryPB) {
    pb.fully_applied_schema = pb.schema.clone();
    pb.set_fully_applied_schema_version(pb.version());
    pb.fully_applied_indexes = pb.indexes.clone();
    if pb.index_info.is_some() {
        pb.fully_applied_index_info = pb.index_info.clone();
    }
}

/// A (table id, tablet id) pair.
type TableTablet = (Vec<u8>, Vec<u8>);

/// Sends `AlterSchema` for ALTERING tables and returns them to RUNNING.
pub struct TableAlterer(Arc<Alterer>);

struct Alterer {
    lifecycle: Arc<TableLifecycle>,
    rpc: Arc<dyn RpcClient>,
    executor: Arc<TsRpcExecutor>,
    retry: RetryPolicy,
    rpc_timeout: fn() -> Duration,
    interval: fn() -> Duration,
    /// Per (table, tablet): the version of the table row (`Entity::version`) an `AlterSchema`
    /// this term was sent for, and whether the tablet acknowledged it. The row version, not the
    /// schema version, since a `wal_retention_secs` alter keeps the schema version. Keyed by table
    /// too, since colocated tables share their tablet.
    tablets: Mutex<HashMap<TableTablet, (u64, bool)>>,
    /// Indexed tables whose backfill runs in this term.
    backfills: Mutex<std::collections::HashSet<Vec<u8>>>,
}

impl TableAlterer {
    /// An alterer for one term.
    pub fn new(
        lifecycle: Arc<TableLifecycle>,
        rpc: Arc<dyn RpcClient>,
        executor: Arc<TsRpcExecutor>,
        retry: RetryPolicy,
        rpc_timeout: fn() -> Duration,
        interval: fn() -> Duration,
    ) -> Arc<TableAlterer> {
        Arc::new(TableAlterer(Arc::new(Alterer {
            lifecycle,
            rpc,
            executor,
            retry,
            rpc_timeout,
            interval,
            tablets: Mutex::default(),
            backfills: Mutex::default(),
        })))
    }
}

impl Alterer {
    async fn pass(self: &Arc<Self>, scope: &TermScope) {
        let Ok(token) = self.lifecycle.leadership.token() else {
            return;
        };
        let snapshot = self.lifecycle.catalog.snapshot();
        let altering: Vec<_> = snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| {
                t.table()
                    .is_some_and(|h| h.state == TableState::ALTERING as i32)
            })
            .cloned()
            .collect();
        for table in altering {
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            let row_version = table.version;
            let tablets = snapshot.hosting_tablets_of(&table.key.id);
            let mut all_done = !tablets.is_empty();
            for tablet in tablets {
                let key = (table.key.id.to_vec(), tablet.key.id.to_vec());
                let state = self.tablets.lock().unwrap().get(&key).copied();
                match state {
                    Some((v, true)) if v == row_version => {}
                    Some((v, false)) if v == row_version => all_done = false,
                    _ => {
                        all_done = false;
                        self.send(
                            scope,
                            &token,
                            &table.key.id,
                            row_version,
                            &pb,
                            &tablet.key.id,
                        );
                    }
                }
            }
            if all_done && awaits_backfill(&pb) {
                self.start_backfill(scope, &token, &table.key.id, None);
                continue;
            }
            if all_done && !single_stage_alter(&pb) {
                if let Err(status) = self
                    .advance_ycql_indexes(scope, &token, &table.key.id, row_version, &pb)
                    .await
                {
                    tracing::warn!(
                        "Advancing the indexes of table {} failed, will retry: {status}",
                        String::from_utf8_lossy(&table.key.id)
                    );
                }
                continue;
            }
            if all_done {
                if let Err(status) = self.finish(&token, &table.key.id, row_version).await {
                    tracing::warn!(
                        "Finishing the alter of table {} failed, will retry: {status}",
                        String::from_utf8_lossy(&table.key.id)
                    );
                }
            }
        }
    }

    /// Runs the table's backfill (`StartBackfillingData`) unless it already runs in this term:
    /// of `indexes`, or of the first YSQL index at DO_BACKFILL; a recorded job is resumed as is.
    fn start_backfill(
        self: &Arc<Self>,
        scope: &TermScope,
        token: &LeaderToken,
        table_id: &[u8],
        indexes: Option<Vec<Vec<u8>>>,
    ) {
        if !self.backfills.lock().unwrap().insert(table_id.to_vec()) {
            return;
        }
        let this = self.clone();
        let token = *token;
        let table_id = table_id.to_vec();
        let options = &self.lifecycle.options;
        let rpc_timeout = if indexes.is_some() {
            (options.index_backfill_rpc_timeout)()
        } else {
            (options.ysql_index_backfill_rpc_timeout)()
        };
        scope.entity().spawn("index-backfill", async move {
            let ctx = crate::backfill::BackfillContext {
                lifecycle: this.lifecycle.clone(),
                rpc: this.rpc.clone(),
                executor: this.executor.clone(),
                rpc_timeout,
            };
            if let Err(status) = crate::backfill::run(&ctx, token, &table_id, indexes).await {
                tracing::warn!(
                    "Backfill of table {} failed, will retry: {status}",
                    String::from_utf8_lossy(&table_id)
                );
            }
            this.backfills.lock().unwrap().remove(&table_id);
        });
    }

    /// `HandleSchemaVersionReported` of a YCQL table once every tablet has the version: the next
    /// index permission (`ClassifyIndexes`, `ApplyYcqlActions`), the delete of an INDEX_UNUSED
    /// index, or the backfill of the indexes at DO_BACKFILL; else the alter is done.
    async fn advance_ycql_indexes(
        self: &Arc<Self>,
        scope: &TermScope,
        token: &LeaderToken,
        table_id: &[u8],
        row_version: u64,
        pb: &SysTablesEntryPB,
    ) -> Result<(), Status> {
        let plan = classify_ycql_indexes(
            pb,
            self.backfills.lock().unwrap().contains(table_id),
            &self.lifecycle.options,
            false,
        );
        match plan {
            YcqlStep::Done => self.finish(token, table_id, row_version).await,
            YcqlStep::Permissions(perms) => self
                .lifecycle
                .writer
                .mutate(token, |snapshot| {
                    let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                        return Ok((Mutation::new(), ()));
                    };
                    if table.version != row_version {
                        return Ok((Mutation::new(), ()));
                    }
                    let mut pb: SysTablesEntryPB = table
                        .parse()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                    crate::backfill::set_index_permissions(&mut pb, &perms);
                    let bytes = pb
                        .write_to_bytes()
                        .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
                    Ok((
                        Mutation::new()
                            .read(table)
                            .upsert(SysRowEntryType::TABLE, table_id, bytes),
                        (),
                    ))
                })
                .await
                .map(|_| ()),
            // `DeleteTableInternal`: the index goes, and leaves its table's `indexes`.
            YcqlStep::Delete(index_id) => self
                .lifecycle
                .mark_deleting(token, Instant::now() + (self.rpc_timeout)(), &index_id)
                .await
                .map_err(|e| crate::backfill::ts_status(&e.status)),
            YcqlStep::Backfill(indexes) => {
                self.start_backfill(scope, token, table_id, Some(indexes));
                Ok(())
            }
        }
    }

    fn send(
        self: &Arc<Self>,
        scope: &TermScope,
        token: &LeaderToken,
        table_id: &[u8],
        row_version: u64,
        table: &SysTablesEntryPB,
        tablet_id: &[u8],
    ) {
        let version = table.version();
        let mut req = ChangeMetadataRequestPB::new();
        req.set_tablet_id(tablet_id.to_vec());
        req.schema = table.schema.clone();
        req.indexes = table.indexes.clone();
        req.set_schema_version(version);
        req.set_new_table_name(String::from_utf8_lossy(table.name()).into_owned());
        req.set_alter_table_id(table_id.to_vec());
        if table.has_wal_retention_secs() {
            req.set_wal_retention_secs(table.wal_retention_secs());
        }
        // A rollback moves next_column_id back to the first DDL state's.
        req.set_first_provisional_column_id(
            table
                .ysql_ddl_txn_verifier_state
                .iter()
                .find(|s| s.has_previous_next_column_id())
                .map_or(table.next_column_id(), |s| s.previous_next_column_id()),
        );
        if table.transaction.using_table_locks() {
            req.set_only_abort_txns_not_using_table_locks(true);
        }
        // The alter a YSQL `AlterTable` made in a DDL transaction: the tablet aborts the other
        // transactions on it, or with table locks only those not using them.
        let txn = self
            .lifecycle
            .alter_txns
            .lock()
            .unwrap()
            .get(table_id)
            .cloned();
        if let Some((_, txn)) = txn.filter(|(v, _)| *v == version) {
            req.set_should_abort_active_txns(true);
            req.set_transaction_id(crate::ysql::uuid(&txn).into_bytes());
        }
        let key = (table_id.to_vec(), tablet_id.to_vec());
        self.tablets
            .lock()
            .unwrap()
            .insert(key.clone(), (row_version, false));
        let this = self.clone();
        let token = *token;
        let tablet_id = tablet_id.to_vec();
        scope.entity().spawn("alter-schema", async move {
            let lifecycle = &this.lifecycle;
            // The tablet's leader, as `AsyncTabletLeaderTask` sends it; no leader yet means the
            // next pass tries again.
            let Some(leader) = lifecycle
                .ts_manager
                .locations
                .get(&tablet_id)
                .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
            else {
                this.tablets.lock().unwrap().remove(&key);
                return;
            };
            req.set_dest_uuid(leader.clone());
            let rpc_timeout = (this.rpc_timeout)();
            let result = this
                .executor
                .run(&leader, "AlterSchema", &this.retry, |_| {
                    let current = lifecycle.leadership.check(&token);
                    let address = lifecycle.ts_manager.rpc_address(&leader);
                    let (rpc, req) = (this.rpc.clone(), req.clone());
                    async move {
                        current.map_err(Attempt::Fail)?;
                        let (host, port) = address.ok_or_else(|| {
                            Attempt::Retry(Status::new(
                                StatusCode::NotFound,
                                "tserver is not registered",
                            ))
                        })?;
                        send_alter(&*rpc, &host, port, &req, rpc_timeout).await
                    }
                })
                .await;
            let acked = {
                let mut tablets = this.tablets.lock().unwrap();
                match result {
                    Ok(()) => {
                        tablets.insert(key.clone(), (row_version, true));
                        true
                    }
                    Err(status) => {
                        tracing::warn!(
                            "AlterSchema of tablet {} failed: {status}",
                            String::from_utf8_lossy(&tablet_id)
                        );
                        tablets.remove(&key);
                        false
                    }
                }
            };
            if acked {
                this.finish_if_acked(&token, &key.0, row_version).await;
            }
        });
    }

    /// Finishes the alter once the last tablet acknowledges it, as C++ does from the response to
    /// its last `AlterSchema`: until then `GetTableSchema` serves the previous schema, which the
    /// tablets no longer accept for reads. An alter waiting for a backfill is left to the pass.
    async fn finish_if_acked(&self, token: &LeaderToken, table_id: &[u8], row_version: u64) {
        let snapshot = self.lifecycle.catalog.snapshot();
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return;
        };
        if table.version != row_version {
            return;
        }
        let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
            return;
        };
        if pb.state() != TableState::ALTERING || !single_stage_alter(&pb) || awaits_backfill(&pb) {
            return;
        }
        let tablets = snapshot.hosting_tablets_of(table_id);
        let all_acked = !tablets.is_empty() && {
            let acks = self.tablets.lock().unwrap();
            tablets.iter().all(|t| {
                acks.get(&(table_id.to_vec(), t.key.id.to_vec())) == Some(&(row_version, true))
            })
        };
        if all_acked {
            if let Err(status) = self.finish(token, table_id, row_version).await {
                tracing::warn!(
                    "Finishing the alter of table {} failed, will retry: {status}",
                    String::from_utf8_lossy(table_id)
                );
            }
        }
    }

    /// `ClearFullyAppliedAndUpdateState`: RUNNING again, if the row is still the one every tablet
    /// acknowledged.
    async fn finish(
        &self,
        token: &LeaderToken,
        table_id: &[u8],
        row_version: u64,
    ) -> Result<(), Status> {
        let (finished, _) = self
            .lifecycle
            .writer
            .mutate(token, |snapshot| {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                    return Ok((Mutation::new(), None));
                };
                let mut pb: SysTablesEntryPB = table
                    .parse()
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                if table.version != row_version || pb.state() != TableState::ALTERING {
                    return Ok((Mutation::new(), None));
                }
                let version = pb.version();
                pb.fully_applied_schema.clear();
                pb.clear_fully_applied_schema_version();
                pb.fully_applied_indexes.clear();
                pb.fully_applied_index_info.clear();
                pb.state = Some(EnumOrUnknown::new(TableState::RUNNING));
                pb.set_state_msg(format!("Current schema version={version}").into_bytes());
                let bytes = pb
                    .write_to_bytes()
                    .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))?;
                Ok((
                    Mutation::new()
                        .read(table)
                        .upsert(SysRowEntryType::TABLE, table_id, bytes),
                    Some(version),
                ))
            })
            .await?;
        if let Some(version) = finished {
            let mut txns = self.lifecycle.alter_txns.lock().unwrap();
            if txns.get(table_id).is_some_and(|(v, _)| *v <= version) {
                txns.remove(table_id);
            }
        }
        Ok(())
    }
}

impl Controller for TableAlterer {
    fn name(&self) -> &'static str {
        "table-alters"
    }

    fn interval(&self) -> Duration {
        (self.0.interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(self.0.pass(scope))
    }
}

/// An alter this controller completes: of a YSQL table, or of a YCQL table with no index
/// permission change in flight. The multi-stage index permission alters of a YCQL backfill
/// (`MultiStageAlterTable`), which a C++ leader may have started, are not served yet and stay
/// ALTERING.
fn single_stage_alter(pb: &SysTablesEntryPB) -> bool {
    let settled = |p: IndexPermissions| p == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE;
    // A YSQL index's permissions move only on PG's request or at the end of its backfill, so
    // every YSQL alter completes here (`ClassifyIndexes` for a YSQL table).
    pb.table_type() == TableType::PGSQL_TABLE_TYPE
        || pb.table_type() == TableType::YQL_TABLE_TYPE
            && pb.indexes.iter().all(|i| settled(i.index_permissions()))
            && pb
                .index_info
                .as_ref()
                .is_none_or(|i| settled(i.index_permissions()))
}

/// A YSQL table with an index at DO_BACKFILL: its alter ends with the backfill, not RUNNING.
fn awaits_backfill(pb: &SysTablesEntryPB) -> bool {
    pb.table_type() == TableType::PGSQL_TABLE_TYPE
        && pb
            .indexes
            .iter()
            .any(|i| i.index_permissions() == IndexPermissions::INDEX_PERM_DO_BACKFILL)
}

/// What a YCQL table's indexes need next.
pub(crate) enum YcqlStep {
    /// Nothing: the alter is done.
    Done,
    /// The next permission of each transient index.
    Permissions(std::collections::BTreeMap<Vec<u8>, IndexPermissions>),
    /// An index at INDEX_UNUSED is deleted.
    Delete(Vec<u8>),
    /// These indexes are backfilled.
    Backfill(Vec<Vec<u8>>),
}

/// `NextPermission`.
fn next_permission(perm: IndexPermissions) -> Option<IndexPermissions> {
    use IndexPermissions::*;
    Some(match perm {
        INDEX_PERM_DELETE_ONLY => INDEX_PERM_WRITE_AND_DELETE,
        INDEX_PERM_WRITE_AND_DELETE => INDEX_PERM_DO_BACKFILL,
        INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING => INDEX_PERM_DELETE_ONLY_WHILE_REMOVING,
        INDEX_PERM_DELETE_ONLY_WHILE_REMOVING => INDEX_PERM_INDEX_UNUSED,
        _ => return None,
    })
}

/// `ClassifyIndexes` and `ApplyIndexStateMachineActions` of a YCQL table. `launch_deferred` is
/// `LaunchBackfillIndexForTable`, which backfills deferred indexes too.
pub(crate) fn classify_ycql_indexes(
    pb: &SysTablesEntryPB,
    is_backfilling: bool,
    options: &crate::LifecycleOptions,
    launch_deferred: bool,
) -> YcqlStep {
    let defer = (options.defer_index_backfill)();
    let mut ready = Vec::new();
    let mut deferred = Vec::new();
    let mut to_delete = Vec::new();
    let mut perms = std::collections::BTreeMap::new();
    for index in &pb.indexes {
        if !index.has_index_permissions() {
            continue;
        }
        let id = index.table_id().to_vec();
        match index.index_permissions() {
            IndexPermissions::INDEX_PERM_DO_BACKFILL => {
                if !launch_deferred && (defer || index.is_backfill_deferred()) {
                    deferred.push(id);
                } else {
                    ready.push(id);
                }
            }
            IndexPermissions::INDEX_PERM_INDEX_UNUSED => to_delete.push(id),
            perm => {
                if let Some(next) = next_permission(perm) {
                    perms.insert(id, next);
                }
            }
        }
    }
    let mut to_backfill = ready;
    // A job a former leader started is resumed with its indexes only: the read time it chose
    // may not suit an index added since.
    if !is_backfilling {
        if let Some(job) = pb.backfill_jobs.first() {
            to_backfill = job.indexes.iter().map(|i| i.table_id().to_vec()).collect();
            deferred.clear();
        }
    }
    if to_backfill.len() > 1 && !(options.allow_batching_non_deferred_indexes)() {
        to_backfill.truncate(1);
    }
    if perms.is_empty() && to_delete.is_empty() && (is_backfilling || to_backfill.is_empty()) {
        return YcqlStep::Done;
    }
    if !perms.is_empty() {
        return YcqlStep::Permissions(perms);
    }
    if let Some(id) = to_delete.into_iter().next() {
        return YcqlStep::Delete(id);
    }
    if to_backfill.is_empty() || is_backfilling {
        return YcqlStep::Done;
    }
    to_backfill.extend(deferred);
    YcqlStep::Backfill(to_backfill)
}

/// One `AlterSchema` call. A tablet that already has this or a newer schema counts as done. A
/// target that does not have the tablet, or no longer leads it, ends the call: the next pass sends
/// to the leader the locations then name. (C++ counts TABLET_NOT_FOUND as done and re-sends from
/// the tablet's reports; the Rust leader finishes an alter from the acknowledgements alone.)
async fn send_alter(
    rpc: &dyn RpcClient,
    host: &str,
    port: u16,
    req: &ChangeMetadataRequestPB,
    timeout: Duration,
) -> Result<(), Attempt> {
    let bytes = req
        .write_to_bytes()
        .map_err(|e| Attempt::Fail(Status::new(StatusCode::InternalError, e.to_string())))?;
    let response = rpc
        .call(
            host,
            port,
            "yb.tserver.TabletServerAdminService",
            "AlterSchema",
            &bytes,
            timeout,
        )
        .await
        .map_err(Attempt::Retry)?;
    let response = ChangeMetadataResponsePB::parse_from_bytes(&response)
        .map_err(|e| Attempt::Retry(Status::new(StatusCode::Corruption, e.to_string())))?;
    let Some(error) = response.error.as_ref() else {
        return Ok(());
    };
    use tablet_server_error_pb::Code as TsCode;
    let status = || {
        Status::new(
            StatusCode::from_i32(error.status.code().value()),
            error.status.message().to_string(),
        )
    };
    match error.code() {
        TsCode::MISMATCHED_SCHEMA | TsCode::TABLET_HAS_A_NEWER_SCHEMA => Ok(()),
        TsCode::TABLET_NOT_FOUND | TsCode::NOT_THE_LEADER | TsCode::LEADER_NOT_READY_TO_SERVE => {
            Err(Attempt::Fail(status()))
        }
        _ => Err(Attempt::Retry(status())),
    }
}

#[cfg(test)]
mod ycql_index_tests {
    use super::*;
    use yb_proto::pb::catalog_entity_info::BackfillJobPB;
    use yb_proto::pb::common::IndexInfoPB;
    use IndexPermissions::*;

    fn table(indexes: &[(&str, IndexPermissions, bool)]) -> SysTablesEntryPB {
        let mut pb = SysTablesEntryPB::new();
        pb.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
        for (id, perm, deferred) in indexes {
            let mut info = IndexInfoPB::new();
            info.set_table_id(id.as_bytes().to_vec());
            info.set_index_permissions(*perm);
            info.set_is_backfill_deferred(*deferred);
            pb.indexes.push(info);
        }
        pb
    }

    fn step(pb: &SysTablesEntryPB, backfilling: bool, launch_deferred: bool) -> YcqlStep {
        classify_ycql_indexes(
            pb,
            backfilling,
            &crate::LifecycleOptions::default(),
            launch_deferred,
        )
    }

    #[test]
    fn walks_permissions_then_backfills_then_deletes() {
        let pb = table(&[
            ("a", INDEX_PERM_DELETE_ONLY, false),
            ("b", INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING, false),
        ]);
        let YcqlStep::Permissions(perms) = step(&pb, false, false) else {
            panic!()
        };
        assert_eq!(perms[&b"a".to_vec()], INDEX_PERM_WRITE_AND_DELETE);
        assert_eq!(perms[&b"b".to_vec()], INDEX_PERM_DELETE_ONLY_WHILE_REMOVING);

        let pb = table(&[
            ("a", INDEX_PERM_DO_BACKFILL, false),
            ("b", INDEX_PERM_DO_BACKFILL, false),
        ]);
        let YcqlStep::Backfill(ids) = step(&pb, false, false) else {
            panic!()
        };
        assert_eq!(ids, vec![b"a".to_vec(), b"b".to_vec()]);
        assert!(matches!(step(&pb, true, false), YcqlStep::Done));

        let pb = table(&[("a", INDEX_PERM_INDEX_UNUSED, false)]);
        assert!(matches!(step(&pb, false, false), YcqlStep::Delete(id) if id == b"a"));

        let pb = table(&[("a", INDEX_PERM_READ_WRITE_AND_DELETE, false)]);
        assert!(matches!(step(&pb, false, false), YcqlStep::Done));
    }

    #[test]
    fn deferred_indexes_wait_for_launch_or_a_batch() {
        let pb = table(&[("a", INDEX_PERM_DO_BACKFILL, true)]);
        assert!(matches!(step(&pb, false, false), YcqlStep::Done));
        let YcqlStep::Backfill(ids) = step(&pb, false, true) else {
            panic!()
        };
        assert_eq!(ids, vec![b"a".to_vec()]);
        // A deferred index joins a backfill that runs anyway.
        let pb = table(&[
            ("a", INDEX_PERM_DO_BACKFILL, true),
            ("b", INDEX_PERM_DO_BACKFILL, false),
        ]);
        let YcqlStep::Backfill(ids) = step(&pb, false, false) else {
            panic!()
        };
        assert_eq!(ids, vec![b"b".to_vec(), b"a".to_vec()]);
    }

    #[test]
    fn a_job_from_a_former_leader_is_resumed_alone() {
        let mut pb = table(&[
            ("a", INDEX_PERM_DO_BACKFILL, false),
            ("b", INDEX_PERM_DO_BACKFILL, false),
        ]);
        let mut job = BackfillJobPB::new();
        job.indexes.push(pb.indexes[1].clone());
        pb.backfill_jobs.push(job);
        let YcqlStep::Backfill(ids) = step(&pb, false, false) else {
            panic!()
        };
        assert_eq!(ids, vec![b"b".to_vec()]);
    }
}
