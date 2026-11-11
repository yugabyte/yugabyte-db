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

//! YSQL colocation: tablegroups and the tables they hold.
//!
//! A tablegroup is its parent table (`<tablegroup id>.tablegroup.parent.uuid`, or
//! `.colocation.parent.uuid` in a colocated database), which owns the group's one tablet. A
//! colocated table names the parent as `parent_table_id` and owns no tablet. It is created
//! PREPARING; the `colocated-tables` controller has the tablet's leader add it
//! (`AddTableToTablet`) and then marks it RUNNING, so a new leader finishes what the last one
//! started. A dropped colocated table leaves the tablet (`RemoveTableFromTablet`) before the
//! table-deletes controller marks it DELETED.

use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{BoxFuture, Controller, RetryPolicy, TermScope, TsRpcExecutor};
use protobuf::{EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, SysTablesEntryPB};
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_ddl::{
    CreateTableRequestPB, CreateTableResponsePB, CreateTablegroupRequestPB,
    CreateTablegroupResponsePB, DeleteTableRequestPB, DeleteTableResponsePB,
    DeleteTablegroupRequestPB, DeleteTablegroupResponsePB, ListTablegroupsRequestPB,
    ListTablegroupsResponsePB, TablegroupIdentifierPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::tablet_metadata::TableInfoPB;
use yb_proto::pb::tserver_admin::{
    AddTableToTabletRequestPB, AddTableToTabletResponsePB, RemoveTableFromTabletRequestPB,
    RemoveTableFromTabletResponsePB,
};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

use crate::backfill::{call_tablet_leader, corrupt, ts_status, BackfillContext};
use crate::ysql::{is_tablegroup_parent_table_id, tablegroup_parent_of};
use crate::{error, TableLifecycle};

use sys_tables_entry_pb::State as TableState;

const ADMIN_SERVICE: &str = "yb.tserver.TabletServerAdminService";

/// A colocated table that is not a parent: it names a parent and owns no tablet.
pub(crate) fn is_colocated_child(snapshot: &CatalogSnapshot, table_id: &[u8]) -> bool {
    snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(|t| t.table())
        .is_some_and(|h| {
            !h.parent_table_id.is_empty()
                && h.parent_table_id != table_id
                && snapshot.tablets_of(table_id).is_empty()
        })
}

/// What a call to a tablet's leader needs.
#[derive(Clone)]
pub struct TabletRpc {
    /// Tserver RPCs.
    pub rpc: Arc<dyn RpcClient>,
    /// Paces them per tserver.
    pub executor: Arc<TsRpcExecutor>,
    /// Each attempt's timeout.
    pub rpc_timeout: fn() -> Duration,
    /// Retries.
    pub retry: RetryPolicy,
}

impl TabletRpc {
    fn context(&self, lifecycle: &Arc<TableLifecycle>) -> BackfillContext {
        BackfillContext {
            lifecycle: lifecycle.clone(),
            rpc: self.rpc.clone(),
            executor: self.executor.clone(),
            rpc_timeout: (self.rpc_timeout)(),
        }
    }
}

/// `AsyncRemoveTableFromTablet` for a dropped colocated table: each of its parent's serving
/// tablets drops the table from its metadata. Skipped while the parent is being dropped too,
/// since its tablets are deleted (`DeleteYsqlDBTables` sends none either).
pub(crate) async fn remove_from_tablets(
    lifecycle: &Arc<TableLifecycle>,
    rpc: &TabletRpc,
    token: &LeaderToken,
    table_id: &[u8],
) -> Result<(), Status> {
    let snapshot = lifecycle.catalog.snapshot();
    let Some(parent_id) = snapshot
        .get(SysRowEntryType::TABLE, table_id)
        .and_then(|t| t.table())
        .map(|h| h.parent_table_id.clone())
    else {
        return Ok(());
    };
    let parent_live = snapshot
        .get(SysRowEntryType::TABLE, &parent_id)
        .and_then(|t| t.table())
        .is_some_and(|h| {
            h.state != TableState::DELETING as i32 && h.state != TableState::DELETED as i32
        });
    if !parent_live {
        return Ok(());
    }
    let ctx = rpc.context(lifecycle);
    for tablet in snapshot.active_tablets_of(&parent_id) {
        let tablet_id = tablet.key.id.to_vec();
        call_tablet_leader(
            &ctx,
            token,
            &tablet_id,
            ADMIN_SERVICE,
            "RemoveTableFromTablet",
            |_leader| {
                let mut req = RemoveTableFromTabletRequestPB::new();
                req.set_tablet_id(tablet_id.clone());
                req.set_remove_table_id(table_id.to_vec());
                req.write_to_bytes().map_err(corrupt)
            },
            |bytes| {
                let resp =
                    RemoveTableFromTabletResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
                Ok(match resp.error.as_ref() {
                    // The tablet is gone, so the table is too.
                    Some(e) if e.code() == tablet_server_error_pb::Code::TABLET_NOT_FOUND => Ok(()),
                    Some(e) => Err((ts_status(&e.status), false)),
                    None => Ok(()),
                })
            },
            rpc.retry,
        )
        .await?;
    }
    Ok(())
}

/// Adds PREPARING colocated tables to their group's tablet and marks them RUNNING.
pub struct ColocatedTableAdder(Arc<Adder>);

struct Adder {
    lifecycle: Arc<TableLifecycle>,
    rpc: TabletRpc,
    /// `ysql_yb_enable_alter_table_rewrite`: the tablet skips its tombstone check for the table.
    skip_table_tombstone_check: fn() -> bool,
    interval: fn() -> Duration,
    /// Tables whose add runs in this term, by row version.
    running: Mutex<HashMap<Vec<u8>, u64>>,
}

impl ColocatedTableAdder {
    /// An adder for one term.
    pub fn new(
        lifecycle: Arc<TableLifecycle>,
        rpc: TabletRpc,
        skip_table_tombstone_check: fn() -> bool,
        interval: fn() -> Duration,
    ) -> Arc<ColocatedTableAdder> {
        Arc::new(ColocatedTableAdder(Arc::new(Adder {
            lifecycle,
            rpc,
            skip_table_tombstone_check,
            interval,
            running: Mutex::default(),
        })))
    }
}

impl Adder {
    async fn pass(self: &Arc<Self>, scope: &TermScope) {
        let Ok(token) = self.lifecycle.leadership.token() else {
            return;
        };
        let snapshot = self.lifecycle.catalog.snapshot();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else { continue };
            if hot.state != TableState::PREPARING as i32
                || !is_colocated_child(&snapshot, &table.key.id)
            {
                continue;
            }
            // The group's tablet must run first; the next pass after it does sends the add.
            let tablets = snapshot.active_tablets_of(&hot.parent_table_id);
            if tablets.is_empty()
                || !tablets.iter().all(|t| {
                    t.tablet().is_some_and(|h| {
                        h.state
                        == yb_proto::pb::catalog_entity_info::sys_tablets_entry_pb::State::RUNNING
                            as i32
                    })
                })
            {
                continue;
            }
            {
                let mut running = self.running.lock().unwrap();
                if running.get(&*table.key.id) == Some(&table.version) {
                    continue;
                }
                running.insert(table.key.id.to_vec(), table.version);
            }
            let this = self.clone();
            let table_id = table.key.id.to_vec();
            let version = table.version;
            scope.entity().spawn("add-table-to-tablet", async move {
                if let Err(status) = this.add(&token, &table_id).await {
                    tracing::warn!(
                        "Adding colocated table {} to its tablet failed, will retry: {status}",
                        String::from_utf8_lossy(&table_id)
                    );
                }
                let mut running = this.running.lock().unwrap();
                if running.get(&table_id) == Some(&version) {
                    running.remove(&table_id);
                }
            });
        }
    }

    /// `AsyncAddTableToTablet`, then `PromoteTableToRunningState`.
    async fn add(&self, token: &LeaderToken, table_id: &[u8]) -> Result<(), Status> {
        let snapshot = self.lifecycle.catalog.snapshot();
        let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
            return Ok(());
        };
        let pb: SysTablesEntryPB = table
            .parse()
            .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
        let mut info = TableInfoPB::new();
        info.set_table_id(table_id.to_vec());
        info.set_table_name(String::from_utf8_lossy(pb.name()).into_owned());
        info.table_type = Some(EnumOrUnknown::new(pb.table_type()));
        info.set_schema_version(pb.version());
        info.schema = pb.schema.clone();
        info.partition_schema = pb.partition_schema.clone();
        if pb.index_info.is_some() {
            info.index_info = pb.index_info.clone();
        }
        info.old_schema_packings = pb
            .xcluster_table_info
            .xcluster_colocated_old_schema_packings
            .clone();
        info.set_pg_table_id(String::from_utf8_lossy(pb.pg_table_id()).into_owned());
        info.set_skip_table_tombstone_check((self.skip_table_tombstone_check)());
        let ctx = self.rpc.context(&self.lifecycle);
        for tablet in snapshot.active_tablets_of(pb.parent_table_id()) {
            let tablet_id = tablet.key.id.to_vec();
            call_tablet_leader(
                &ctx,
                token,
                &tablet_id,
                ADMIN_SERVICE,
                "AddTableToTablet",
                |_leader| {
                    let mut req = AddTableToTabletRequestPB::new();
                    req.set_tablet_id(tablet_id.clone());
                    req.add_table = Some(info.clone()).into();
                    req.write_to_bytes().map_err(corrupt)
                },
                |bytes| {
                    let resp =
                        AddTableToTabletResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
                    Ok(match resp.error.as_ref() {
                        Some(e) => Err((
                            ts_status(&e.status),
                            e.code() == tablet_server_error_pb::Code::TABLET_NOT_FOUND,
                        )),
                        None => Ok(()),
                    })
                },
                self.rpc.retry,
            )
            .await?;
        }
        let row_version = table.version;
        self.lifecycle
            .writer
            .mutate(token, |snapshot| {
                let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                    return Ok((Mutation::new(), ()));
                };
                if table.version != row_version
                    || table.table().map(|h| h.state) != Some(TableState::PREPARING as i32)
                {
                    return Ok((Mutation::new(), ()));
                }
                let mut pb: SysTablesEntryPB = table
                    .parse()
                    .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?;
                pb.state = Some(EnumOrUnknown::new(TableState::RUNNING));
                let mutation = Mutation::new().read(table).upsert(
                    SysRowEntryType::TABLE,
                    table_id,
                    pb.write_to_bytes()
                        .map_err(|e| Status::new(StatusCode::Corruption, e.to_string()))?,
                );
                Ok((mutation, ()))
            })
            .await
            .map(|_| ())
    }
}

impl Controller for ColocatedTableAdder {
    fn name(&self) -> &'static str {
        "colocated-tables"
    }

    fn interval(&self) -> Duration {
        (self.0.interval)()
    }

    fn reconcile<'a>(&'a self, scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(self.0.pass(scope))
    }
}

/// `GetTablegroupParentTableId` or `GetColocationParentTableId`, and the parent's name.
fn parent_table_of(tablegroup_id: &[u8], colocated_database: bool) -> (Vec<u8>, String) {
    let id = String::from_utf8_lossy(tablegroup_id);
    if colocated_database {
        (
            format!("{id}.colocation.parent.uuid").into_bytes(),
            format!("{id}.colocation.parent.tablename"),
        )
    } else {
        (
            format!("{id}.tablegroup.parent.uuid").into_bytes(),
            format!("{id}.tablegroup.parent.tablename"),
        )
    }
}

impl TableLifecycle {
    /// `CreateTablegroup`: creates the parent table, which owns the group's tablet.
    pub async fn create_tablegroup(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &CreateTablegroupRequestPB,
        resp: &mut CreateTablegroupResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if !req.has_id() || !req.has_namespace_id() || !req.has_namespace_name() {
            return Err(error(
                Code::INVALID_SCHEMA,
                StatusCode::InvalidArgument,
                "Improper CREATE TABLEGROUP request (missing fields).",
            ));
        }
        let colocated_database = {
            let snapshot = self.catalog.snapshot();
            let namespace = snapshot
                .get(SysRowEntryType::NAMESPACE, req.namespace_id())
                .ok_or_else(|| {
                    error(
                        Code::NAMESPACE_NOT_FOUND,
                        StatusCode::NotFound,
                        format!(
                            "Keyspace identifier not found: {}",
                            String::from_utf8_lossy(req.namespace_id())
                        ),
                    )
                })?;
            namespace
                .parse::<yb_proto::pb::catalog_entity_info::SysNamespaceEntryPB>()
                .is_ok_and(|n| n.colocated())
        };
        let (parent_id, parent_name) = parent_table_of(req.id(), colocated_database);
        let mut create = CreateTableRequestPB::new();
        if req.transaction.is_some() {
            create.transaction = req.transaction.clone();
            create.set_ysql_yb_ddl_rollback_enabled(req.ysql_yb_ddl_rollback_enabled());
            if req.has_sub_transaction_id() {
                create.set_sub_transaction_id(req.sub_transaction_id());
            }
        }
        create.set_name(parent_name.clone());
        create.set_table_id(parent_id);
        let ns = create.namespace.mut_or_insert_default();
        ns.set_name(req.namespace_name().to_string());
        ns.set_id(req.namespace_id().to_vec());
        create.set_table_type(TableType::PGSQL_TABLE_TYPE);
        create.set_tablegroup_id(req.id().to_vec());
        create.set_tablespace_id(req.tablespace_id().to_vec());
        let mut created = CreateTableResponsePB::new();
        let result = self
            .create_ysql_table_as(token, deadline, &create, &mut created, true)
            .await;
        resp.set_parent_table_id(created.table_id().to_vec());
        resp.set_parent_table_name(parent_name.into_bytes());
        result
    }

    /// `DeleteTablegroup`: drops the parent table, once the group holds no table that is not
    /// being dropped by the same DDL transaction.
    pub async fn delete_tablegroup(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &DeleteTablegroupRequestPB,
        resp: &mut DeleteTablegroupResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if !req.has_id() {
            return Err(error(
                Code::INVALID_SCHEMA,
                StatusCode::InvalidArgument,
                "Improper DELETE TABLEGROUP request (missing fields).",
            ));
        }
        let (parent_id, parent_name) = {
            let snapshot = self.catalog.snapshot();
            let tablegroup = String::from_utf8_lossy(req.id()).into_owned();
            let parent = tablegroup_parent_of(&snapshot, req.id()).ok_or_else(|| {
                error(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::InvalidArgument,
                    format!("Tablegroup with ID {tablegroup} does not exist"),
                )
            })?;
            let txn_id = req.transaction.transaction_id().to_vec();
            for child in snapshot.children_of(&parent.key.id) {
                let Some(hot) = child.table() else { continue };
                if hot.state == TableState::DELETING as i32
                    || hot.state == TableState::DELETED as i32
                {
                    continue;
                }
                if !req.has_ysql_yb_ddl_rollback_enabled() {
                    return Err(error(
                        Code::INVALID_REQUEST,
                        StatusCode::InvalidArgument,
                        "Cannot delete tablegroup, it still has tables in it",
                    ));
                }
                let dropped_by_txn = child.parse::<SysTablesEntryPB>().is_ok_and(|pb| {
                    pb.transaction.transaction_id() == txn_id.as_slice()
                        && pb
                            .ysql_ddl_txn_verifier_state
                            .iter()
                            .any(|s| s.contains_drop_table_op())
                });
                if !dropped_by_txn {
                    return Err(error(
                        Code::INVALID_REQUEST,
                        StatusCode::InvalidArgument,
                        format!(
                            "Cannot delete non-empty tablegroup, table {} is not deleted",
                            String::from_utf8_lossy(&child.key.id)
                        ),
                    ));
                }
            }
            let name = parent.table().map(|h| h.name.clone()).unwrap_or_default();
            (parent.key.id.to_vec(), name)
        };
        let mut drop = DeleteTableRequestPB::new();
        let table = drop.table.mut_or_insert_default();
        table.set_table_name(parent_name);
        table.set_table_id(parent_id);
        drop.set_is_index_table(false);
        drop.transaction = req.transaction.clone();
        drop.set_ysql_yb_ddl_rollback_enabled(req.ysql_yb_ddl_rollback_enabled());
        if req.has_sub_transaction_id() {
            drop.set_sub_transaction_id(req.sub_transaction_id());
        }
        let mut dropped = DeleteTableResponsePB::new();
        let result = self
            .delete_ysql_table(token, deadline, &drop, &mut dropped)
            .await;
        resp.set_parent_table_id(dropped.table_id().to_vec());
        result
    }

    /// `ListTablegroups`: the database's tablegroups, by parent table id.
    pub fn list_tablegroups(
        &self,
        snapshot: &CatalogSnapshot,
        req: &ListTablegroupsRequestPB,
        resp: &mut ListTablegroupsResponsePB,
    ) -> Result<(), MasterErrorPB> {
        if !req.has_namespace_id() {
            return Err(error(
                Code::INVALID_SCHEMA,
                StatusCode::InvalidArgument,
                "Improper ListTablegroups request (missing fields).",
            ));
        }
        let mut parents: Vec<_> = snapshot
            .iter(SysRowEntryType::TABLE)
            .filter(|t| {
                is_tablegroup_parent_table_id(&t.key.id)
                    && t.table().is_some_and(|h| {
                        h.namespace_id == req.namespace_id()
                            && h.state != TableState::DELETING as i32
                            && h.state != TableState::DELETED as i32
                    })
            })
            .collect();
        parents.sort_by(|a, b| a.key.id.cmp(&b.key.id));
        for parent in parents {
            let mut tablegroup = TablegroupIdentifierPB::new();
            tablegroup.set_id(parent.key.id[..32].to_vec());
            tablegroup.set_namespace_id(req.namespace_id().to_vec());
            resp.tablegroups.push(tablegroup);
        }
        Ok(())
    }
}
