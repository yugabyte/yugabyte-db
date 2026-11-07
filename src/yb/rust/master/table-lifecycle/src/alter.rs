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
use yb_proto::pb::common_types::TableType;
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
use crate::placement::{encode, replication_info_set};
use crate::time::local_time_string;
use crate::{error, TableLifecycle};

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
        // A rename takes a name; hold the name lock across the check and the write.
        let _names = self.names.lock().await;
        let version = self
            .write(token, deadline, |snapshot| {
                let table = find_table(snapshot, &req.table, false)?;
                let pb: SysTablesEntryPB = table.parse().map_err(corrupt_error)?;
                if pb.table_type() != TableType::YQL_TABLE_TYPE {
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
                        error(
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
                if req.has_new_table_name() {
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
                    return Err(error(
                        Code::TABLE_NOT_RUNNING,
                        StatusCode::TryAgain,
                        format!(
                            "Table {} is not ready yet",
                            String::from_utf8_lossy(pb.name())
                        ),
                    ));
                }
                let mut new = pb.clone();
                let mut changed = false;
                if let Some((schema, next_column_id)) =
                    self.alter_schema(&table.key.id, &pb, req)?
                {
                    if new.fully_applied_schema.is_none() {
                        copy_to_fully_applied(&mut new);
                    }
                    new.schema = MessageField::some(schema);
                    new.set_next_column_id(next_column_id);
                    changed = true;
                }
                if req.has_new_table_name() {
                    new.set_name(req.new_table_name().as_bytes().to_vec());
                    changed = true;
                }
                if let Some(info) = req.replication_info.as_ref() {
                    // Validated as `ValidateTableReplicationInfo` does for placement fields.
                    if replication_info_set(info) {
                        if let Some(live) = info.live_replicas.as_ref() {
                            master_ts_manager::placement::validate_placement(live)
                                .map_err(|s| error(Code::INVALID_REQUEST, s.code, s.message))?;
                        }
                    }
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
                    return Ok((Mutation::new(), pb.version()));
                }
                if !req.has_wal_retention_secs() {
                    new.set_version(pb.version() + 1);
                    new.set_updates_only_index_permissions(false);
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
                let mutation = Mutation::new().read(&table).upsert(
                    SysRowEntryType::TABLE,
                    &table.key.id,
                    encode(&new)?,
                );
                Ok((mutation, new.version()))
            })
            .await?;
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
    ) -> Result<Option<(yb_proto::pb::common::SchemaPB, i32)>, MasterErrorPB> {
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
        Ok(resp
            .schema
            .into_option()
            .map(|schema| (schema, resp.next_column_id.unwrap_or(0))))
    }
}

/// `MultiStageAlterTable::CopySchemaDetailsToFullyApplied`: clients of non-YCQL tables keep seeing
/// the old schema until every tablet has the new one.
fn copy_to_fully_applied(pb: &mut SysTablesEntryPB) {
    pb.fully_applied_schema = pb.schema.clone();
    pb.set_fully_applied_schema_version(pb.version());
    pb.fully_applied_indexes = pb.indexes.clone();
    if pb.index_info.is_some() {
        pb.fully_applied_index_info = pb.index_info.clone();
    }
}

/// Sends `AlterSchema` for ALTERING tables and returns them to RUNNING.
pub struct TableAlterer(Arc<Alterer>);

struct Alterer {
    lifecycle: Arc<TableLifecycle>,
    rpc: Arc<dyn RpcClient>,
    executor: Arc<TsRpcExecutor>,
    retry: RetryPolicy,
    rpc_timeout: fn() -> Duration,
    interval: fn() -> Duration,
    /// Per tablet: the schema version an `AlterSchema` this term was sent for, and whether the
    /// tablet acknowledged it.
    tablets: Mutex<HashMap<Vec<u8>, (u32, bool)>>,
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
            let version = pb.version();
            let tablets = snapshot.active_tablets_of(&table.key.id);
            let mut all_done = !tablets.is_empty();
            for tablet in tablets {
                let state = self.tablets.lock().unwrap().get(&*tablet.key.id).copied();
                match state {
                    Some((v, true)) if v >= version => {}
                    Some((v, false)) if v == version => all_done = false,
                    _ => {
                        all_done = false;
                        self.send(scope, &token, &table.key.id, &pb, &tablet.key.id);
                    }
                }
            }
            if all_done {
                if let Err(status) = self.finish(&token, &table.key.id, version).await {
                    tracing::warn!(
                        "Finishing the alter of table {} failed, will retry: {status}",
                        String::from_utf8_lossy(&table.key.id)
                    );
                }
            }
        }
    }

    fn send(
        self: &Arc<Self>,
        scope: &TermScope,
        token: &LeaderToken,
        table_id: &[u8],
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
        req.set_first_provisional_column_id(table.next_column_id());
        self.tablets
            .lock()
            .unwrap()
            .insert(tablet_id.to_vec(), (version, false));
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
                this.tablets.lock().unwrap().remove(&tablet_id);
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
            let mut tablets = this.tablets.lock().unwrap();
            match result {
                Ok(()) => {
                    tablets.insert(tablet_id, (version, true));
                }
                Err(status) => {
                    tracing::warn!(
                        "AlterSchema of tablet {} failed: {status}",
                        String::from_utf8_lossy(&tablet_id)
                    );
                    tablets.remove(&tablet_id);
                }
            }
        });
    }

    /// `ClearFullyAppliedAndUpdateState`: RUNNING again, if the version is still the one every
    /// tablet acknowledged.
    async fn finish(
        &self,
        token: &LeaderToken,
        table_id: &[u8],
        version: u32,
    ) -> Result<(), Status> {
        self.lifecycle
            .writer
            .mutate(token, |snapshot| {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                    return Ok((Mutation::new(), ()));
                };
                let mut pb: SysTablesEntryPB = table
                    .parse()
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                if pb.state() != TableState::ALTERING || pb.version() != version {
                    return Ok((Mutation::new(), ()));
                }
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
                    (),
                ))
            })
            .await
            .map(|_| ())
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

/// One `AlterSchema` call. A tablet that already has this or a newer schema counts as done.
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
    match error.code() {
        TsCode::TABLET_NOT_FOUND
        | TsCode::MISMATCHED_SCHEMA
        | TsCode::TABLET_HAS_A_NEWER_SCHEMA => Ok(()),
        _ => Err(Attempt::Retry(Status::new(
            StatusCode::from_i32(error.status.code().value()),
            error.status.message().to_string(),
        ))),
    }
}
