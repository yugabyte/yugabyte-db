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

//! `DeleteTable` and `IsDeleteTableDone`, and the `table-deletes` controller (K1).
//!
//! The drop is one mutation: the table and its indexes become DELETING. The controller then, from
//! the rows alone, marks their tablets DELETED in bounded batches, queues `DeleteTablet` for each
//! replica through the paced executor (K2), and marks the table DELETED once no tablet is left.
//! C++ instead writes DELETING, the tablets and DELETED in separate steps with in-memory
//! bookkeeping between them (#33349).

use std::sync::Arc;
use std::time::Duration;

use master_catalog_entity::Entity;
use master_catalog_state::{is_inactive_state, CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use master_ts_manager::DeleteReplica;
use protobuf::{Enum, EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysCDCStreamEntryPB,
    SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_ddl::{
    DeleteTableRequestPB, DeleteTableResponsePB, IsDeleteTableDoneRequestPB,
    IsDeleteTableDoneResponsePB,
};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, SysRowEntryType, TableIdentifierPB,
};
use yb_proto::pb::tablet_types::TabletDataState;

use crate::lookup::{find_table, is_hidden};
use crate::namespaces::short_debug;
use crate::placement::cluster_config;
use crate::schedules::covering_schedules;
use crate::time::local_time_string;
use crate::{error, TableLifecycle};

use sys_tables_entry_pb::State as TableState;
use sys_tablets_entry_pb::State as TabletState;

/// Tablets marked DELETED per write, so a drop of a table with many tablets does not make one
/// huge Raft write.
const TABLETS_PER_WRITE: usize = 1000;

fn is_index(pb: &SysTablesEntryPB) -> bool {
    !pb.indexed_table_id().is_empty() || pb.index_info.is_some()
}

impl TableLifecycle {
    /// `DeleteTable` for a YCQL table and its indexes. Dropping an index on its own, a table a
    /// snapshot schedule covers (which C++ hides), and YSQL tables are not served yet.
    pub async fn delete_table(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteTableRequestPB,
        resp: &mut DeleteTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let table = find_table(&snapshot, &req.table, false)?;
        resp.set_table_id(table.key.id.to_vec());
        if table
            .table()
            .is_some_and(|t| t.table_type == TableType::PGSQL_TABLE_TYPE.value())
            && !is_hidden(&table)
        {
            return self.delete_ysql_table(token, deadline, req, resp).await;
        }
        if is_hidden(&table) {
            // IgnoreHideRequest.
            return Ok(());
        }
        if !(self
            .options
            .enable_delete_truncate_xcluster_replicated_table)()
            && table
                .table()
                .is_some_and(|t| t.table_type == TableType::YQL_TABLE_TYPE.value())
            && xcluster_replicated(&snapshot, &table.key.id)
        {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::NotSupported,
                format!(
                    "Cannot delete a table in replication.: {}",
                    short_debug(req)
                ),
            ));
        }
        if !covering_schedules(&snapshot, SysRowEntryType::TABLE, &table.key.id).is_empty() {
            return Err(not_yet(
                "Dropping a table a snapshot schedule covers (it is hidden, W10)",
            ));
        }
        // The name is released by the write: hold the name lock so a create cannot interleave.
        let _names = self.names.lock().await;
        let deleted = self
            .write(token, deadline, |snapshot| {
                let table = find_table(snapshot, &req.table, false)?;
                let pb: SysTablesEntryPB = table.parse().map_err(|e| {
                    error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                })?;
                if is_hidden(&table) {
                    // IgnoreHideRequest: already hidden, nothing to do.
                    return Ok((Mutation::new(), Vec::new()));
                }
                if req.is_index_table() != is_index(&pb) {
                    return Err(error(
                        Code::OBJECT_NOT_FOUND,
                        StatusCode::NotFound,
                        "The object does not exist",
                    ));
                }
                if pb.state() == TableState::DELETING || pb.state() == TableState::DELETED {
                    return Err(error(
                        Code::OBJECT_NOT_FOUND,
                        StatusCode::NotFound,
                        format!(
                            "The object was deleted: {}",
                            String::from_utf8_lossy(pb.state_msg())
                        ),
                    ));
                }
                match pb.table_type() {
                    TableType::YQL_TABLE_TYPE | TableType::DEFAULT_TABLE_TYPE => {}
                    TableType::PGSQL_TABLE_TYPE => return Err(not_yet("YSQL DROP TABLE (W7)")),
                    other => return Err(not_yet(&format!("DROP TABLE of {other:?}"))),
                }
                if is_index(&pb) {
                    return Err(not_yet("DROP INDEX (W7)"));
                }
                // The table and its indexes; the dying table's index list is left as is, as C++
                // leaves it.
                let message = format!("Started deleting at {}", local_time_string());
                let mut mutation = Mutation::new();
                let mut deleted = Vec::new();
                for index in &pb.indexes {
                    let Some(index_table) = snapshot.get(SysRowEntryType::TABLE, index.table_id())
                    else {
                        continue;
                    };
                    let mut index_pb: SysTablesEntryPB = index_table.parse().map_err(|e| {
                        error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                    })?;
                    if index_pb.state() == TableState::DELETING
                        || index_pb.state() == TableState::DELETED
                    {
                        continue;
                    }
                    index_pb.state = Some(EnumOrUnknown::new(TableState::DELETING));
                    index_pb.set_state_msg(message.clone().into_bytes());
                    mutation = mutation
                        .read(index_table)
                        .upsert_message(SysRowEntryType::TABLE, &index_table.key.id, &index_pb)
                        .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                    deleted.push(index_table.key.id.to_vec());
                }
                let mut pb = pb;
                pb.state = Some(EnumOrUnknown::new(TableState::DELETING));
                pb.set_state_msg(message.into_bytes());
                mutation = mutation
                    .read(&table)
                    .upsert_message(SysRowEntryType::TABLE, &table.key.id, &pb)
                    .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                deleted.push(table.key.id.to_vec());
                Ok((mutation, deleted))
            })
            .await?;
        resp.deleted_table_ids = deleted;
        Ok(())
    }

    /// `IsDeleteTableDone`.
    pub fn is_delete_table_done(
        &self,
        snapshot: &CatalogSnapshot,
        req: &IsDeleteTableDoneRequestPB,
        resp: &mut IsDeleteTableDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let mut ident = TableIdentifierPB::new();
        ident.set_table_id(req.table_id().to_vec());
        let Ok(table) = find_table(snapshot, &ident, true) else {
            resp.set_done(true);
            return Ok(());
        };
        let state = table.table().map_or(0, |t| t.state);
        if state == TableState::DELETED as i32 || is_hidden(&table) {
            resp.set_done(true);
            return Ok(());
        }
        if state != TableState::DELETING as i32 {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::IllegalState,
                "The object was NOT deleted",
            ));
        }
        resp.set_done(false);
        Ok(())
    }
}

/// A request the Rust leader does not serve yet.
fn not_yet(what: &str) -> MasterErrorPB {
    error(
        Code::INVALID_REQUEST,
        StatusCode::NotSupported,
        format!("{what} is not served by a Rust master leader yet"),
    )
}

/// Finishes drops: marks the tablets of DELETING tables DELETED, sends their `DeleteTablet`s and
/// marks the tables DELETED.
pub struct TableDeleter {
    lifecycle: Arc<TableLifecycle>,
    /// `catalog_manager_bg_task_wait_ms`.
    interval: fn() -> Duration,
}

impl TableDeleter {
    /// A deleter for one term.
    pub fn new(lifecycle: Arc<TableLifecycle>, interval: fn() -> Duration) -> Arc<TableDeleter> {
        Arc::new(TableDeleter {
            lifecycle,
            interval,
        })
    }

    pub(crate) async fn pass(&self) {
        let Ok(token) = self.lifecycle.leadership.token() else {
            return;
        };
        let snapshot = self.lifecycle.catalog.snapshot();
        let deleting: Vec<Vec<u8>> = snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| {
                t.table()
                    .is_some_and(|h| h.state == TableState::DELETING as i32 && !h.hidden)
            })
            .map(|t| t.key.id.to_vec())
            .collect();
        for table_id in deleting {
            if let Err(status) = self.finish(&token, &table_id).await {
                tracing::warn!(
                    "Deleting table {} failed, will retry: {status}",
                    String::from_utf8_lossy(&table_id)
                );
            }
        }
    }

    /// Marks the table's remaining tablets DELETED, a batch per write, queueing their replicas'
    /// deletes after each write; then the table.
    async fn finish(&self, token: &LeaderToken, table_id: &[u8]) -> Result<(), Status> {
        loop {
            let (marked, _) = self
                .lifecycle
                .writer
                .mutate(token, |snapshot| {
                    let message = format!("Table deleted at {}", local_time_string());
                    let mut mutation = Mutation::new();
                    let mut marked = Vec::new();
                    for tablet in snapshot.tablets_of(table_id) {
                        if marked.len() >= TABLETS_PER_WRITE {
                            break;
                        }
                        let Some(hot) = tablet.tablet() else { continue };
                        // Only tablets this table owns; a colocated tablet is the parent's.
                        if hot.table_id != table_id || is_inactive_state(hot.state) {
                            continue;
                        }
                        let mut pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                        pb.state = Some(EnumOrUnknown::new(TabletState::DELETED));
                        pb.set_state_msg(message.clone().into_bytes());
                        mutation = mutation.read(tablet).upsert(
                            SysRowEntryType::TABLET,
                            &tablet.key.id,
                            pb.write_to_bytes().map_err(corrupt)?,
                        );
                        marked.push(tablet.clone());
                    }
                    Ok((mutation, marked))
                })
                .await?;
            self.queue_deletes(token, &marked);
            if marked.len() < TABLETS_PER_WRITE {
                break;
            }
        }
        self.lifecycle
            .writer
            .mutate(token, |snapshot| {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                    return Ok((Mutation::new(), ()));
                };
                if table.table().map(|t| t.state) != Some(TableState::DELETING as i32) {
                    return Ok((Mutation::new(), ()));
                }
                let remaining = snapshot.tablets_of(table_id).into_iter().any(|t| {
                    t.tablet()
                        .is_some_and(|h| h.table_id == table_id && !is_inactive_state(h.state))
                });
                if remaining {
                    return Ok((Mutation::new(), ()));
                }
                let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
                pb.state = Some(EnumOrUnknown::new(TableState::DELETED));
                pb.set_state_msg(
                    format!("Deleted with tablets at {}", local_time_string()).into_bytes(),
                );
                let mutation = Mutation::new().read(table).upsert(
                    SysRowEntryType::TABLE,
                    table_id,
                    pb.write_to_bytes().map_err(corrupt)?,
                );
                Ok((mutation, ()))
            })
            .await
            .map(|_| ())
    }

    /// `DeleteTablet` for every replica of the newly deleted tablets: the committed config's
    /// peers and any replica the locations know of. Reports from replicas missed here get the
    /// delete from report processing.
    fn queue_deletes(&self, token: &LeaderToken, tablets: &[Arc<Entity>]) {
        let reason = format!("Table deleted at {}", local_time_string());
        let locations = &self.lifecycle.ts_manager.locations;
        for tablet in tablets {
            let mut replicas: Vec<Vec<u8>> = tablet
                .parse::<SysTabletsEntryPB>()
                .map(|pb| {
                    pb.committed_consensus_state
                        .config
                        .peers
                        .iter()
                        .map(|p| p.permanent_uuid().to_vec())
                        .collect()
                })
                .unwrap_or_default();
            if let Some(observed) = locations.get(&tablet.key.id) {
                replicas.extend(observed.replicas.iter().map(|r| r.ts_uuid.to_vec()));
            }
            replicas.sort();
            replicas.dedup();
            for ts_uuid in replicas {
                self.lifecycle.ts_manager.deletes.push(DeleteReplica {
                    token: *token,
                    ts_uuid: ts_uuid.into(),
                    tablet_id: tablet.key.id.to_vec(),
                    delete_type: TabletDataState::TABLET_DATA_DELETED,
                    cas_config_index: None,
                    hide_only: false,
                    abort_active_txns: true,
                    reason: reason.clone(),
                });
            }
        }
    }
}

impl Controller for TableDeleter {
    fn name(&self) -> &'static str {
        "table-deletes"
    }

    fn interval(&self) -> Duration {
        (self.interval)()
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(self.pass())
    }
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

/// `XClusterManager::IsTableReplicated`: an xCluster stream out of the table that is not being
/// deleted (a CDC stream with no namespace, whose first table is this one), or a stream into it in
/// the cluster config's consumer registry.
pub(crate) fn xcluster_replicated(snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
    let outbound = snapshot.iter(SysRowEntryType::CDC_STREAM).any(|stream| {
        stream.parse::<SysCDCStreamEntryPB>().is_ok_and(|pb| {
            pb.namespace_id().is_empty()
                && pb
                    .table_id
                    .first()
                    .is_some_and(|t| t.as_bytes() == table_id)
                && !matches!(
                    pb.state(),
                    sys_cdcstream_entry_pb::State::DELETING
                        | sys_cdcstream_entry_pb::State::DELETED
                )
        })
    });
    outbound
        || cluster_config(snapshot)
            .consumer_registry
            .producer_map
            .values()
            .flat_map(|producer| producer.stream_map.values())
            .any(|stream| stream.consumer_table_id.as_bytes() == table_id)
}
