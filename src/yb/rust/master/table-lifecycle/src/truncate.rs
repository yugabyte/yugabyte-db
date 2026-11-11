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

//! `TruncateTable` and `IsTruncateTableDone`.
//!
//! As in C++, a truncate is not persisted: each tablet's leader gets `Truncate`, and the table is
//! done when no truncate of it runs on this master. A new leader therefore reports a truncate the
//! old one had not finished as done, as C++ does.

use std::collections::HashMap;
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use master_catalog_state::CatalogSnapshot;
use master_leadership::LeaderToken;
use master_task_runtime::{RetryPolicy, TsRpcExecutor};
use protobuf::Message;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb, SysCDCStreamEntryPB, SysTablesEntryPB,
};

use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_ddl::{
    IsTruncateTableDoneRequestPB, IsTruncateTableDoneResponsePB, TruncateTableRequestPB,
    TruncateTableResponsePB,
};
use yb_proto::pb::master_types::{
    master_error_pb::Code, MasterErrorPB, SysRowEntryType, TableIdentifierPB,
};
use yb_proto::pb::tserver::{TruncateRequestPB, TruncateResponsePB};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

use crate::backfill::{call_tablet_leader, corrupt, hybrid_time_now, ts_status, BackfillContext};
use crate::drop::xcluster_replicated;
use crate::lookup::{check_visible, find_table, is_colocation_parent_table_id};
use crate::schedules::{covering_schedules, schedule_list};
use crate::{error, TableLifecycle};

const TS_SERVICE: &str = "yb.tserver.TabletServerService";

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct TruncateOptions {
    /// `disable_truncate_table`.
    pub disable_truncate_table: fn() -> bool,
    /// `enable_truncate_on_pitr_table`.
    pub enable_truncate_on_pitr_table: fn() -> bool,
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
    /// How a tablet's truncate is retried (`AsyncTabletLeaderTask`).
    pub retry: fn() -> RetryPolicy,
}

/// The leader's truncates.
pub struct Truncates {
    lifecycle: Arc<TableLifecycle>,
    executor: Arc<TsRpcExecutor>,
    rpc: OnceLock<Arc<dyn RpcClient>>,
    options: TruncateOptions,
    /// Per table, the tablets whose truncate still runs (C++'s `kTruncateTablet` tasks).
    pending: Mutex<HashMap<Vec<u8>, usize>>,
}

impl Truncates {
    /// The truncates of this master.
    pub fn new(
        lifecycle: Arc<TableLifecycle>,
        executor: Arc<TsRpcExecutor>,
        options: TruncateOptions,
    ) -> Arc<Truncates> {
        Arc::new(Truncates {
            lifecycle,
            executor,
            rpc: OnceLock::new(),
            options,
            pending: Mutex::new(HashMap::new()),
        })
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    /// `TruncateTable`: truncates each table's tablets, and a YCQL table's indexes. PG sends
    /// each YSQL index its own truncate.
    pub fn truncate_table(
        self: &Arc<Self>,
        token: &LeaderToken,
        req: &TruncateTableRequestPB,
        _resp: &mut TruncateTableResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.lifecycle.catalog.snapshot();
        for table_id in &req.table_ids {
            self.truncate_one(token, &snapshot, table_id)?;
        }
        Ok(())
    }

    fn truncate_one(
        self: &Arc<Self>,
        token: &LeaderToken,
        snapshot: &CatalogSnapshot,
        table_id: &[u8],
    ) -> Result<(), MasterErrorPB> {
        let mut ident = TableIdentifierPB::new();
        ident.set_table_id(table_id.to_vec());
        let table = find_table(snapshot, &ident, false)?;
        check_visible(&table, false)?;
        let pb: SysTablesEntryPB = table
            .parse()
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        if pb.colocated() && !is_colocation_parent_table_id(table_id) {
            // PG truncates a colocated table with a table-level tombstone.
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InternalError,
                format!(
                    "Cannot truncate colocated table {} on master",
                    String::from_utf8_lossy(pb.name())
                ),
            ));
        }
        if (self.options.disable_truncate_table)() {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::NotSupported,
                format!(
                    "TRUNCATE table is disallowed: {}",
                    String::from_utf8_lossy(table_id)
                ),
            ));
        }
        if !(self.options.enable_truncate_on_pitr_table)() {
            let schedules = covering_schedules(snapshot, SysRowEntryType::TABLE, table_id);
            if !schedules.is_empty() {
                return Err(error(
                    Code::INVALID_REQUEST,
                    StatusCode::NotSupported,
                    format!(
                        "Cannot truncate table {} which has schedule: {}",
                        String::from_utf8_lossy(pb.name()),
                        schedule_list(&schedules)
                    ),
                ));
            }
        }
        if !(self
            .lifecycle
            .options
            .enable_delete_truncate_xcluster_replicated_table)()
            && xcluster_replicated(snapshot, table_id)
        {
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::NotSupported,
                format!(
                    "Cannot truncate table {} that is in xCluster replication",
                    String::from_utf8_lossy(table_id)
                ),
            ));
        }
        if cdcsdk_streamed(snapshot, pb.namespace_id(), table_id) {
            // `CDCSDKAllowTableRewrite` comes with the CDC service (W11).
            return Err(error(
                Code::INVALID_REQUEST,
                StatusCode::NotSupported,
                "Truncating a table in a CDC stream is not served by a Rust master leader yet",
            ));
        }
        let rpc = self.rpc.get().cloned().ok_or_else(|| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::ServiceUnavailable,
                "The tserver RPC client is not set",
            )
        })?;
        let tablets: Vec<Vec<u8>> = snapshot
            .active_tablets_of(table_id)
            .iter()
            .map(|t| t.key.id.to_vec())
            .collect();
        tracing::info!(
            "Truncating table {} ({} tablets)",
            String::from_utf8_lossy(table_id),
            tablets.len()
        );
        if !tablets.is_empty() {
            *self
                .pending
                .lock()
                .unwrap()
                .entry(table_id.to_vec())
                .or_default() += tablets.len();
        }
        for tablet_id in tablets {
            let this = self.clone();
            let token = *token;
            let table_id = table_id.to_vec();
            let ctx = BackfillContext {
                lifecycle: self.lifecycle.clone(),
                rpc: rpc.clone(),
                executor: self.executor.clone(),
                rpc_timeout: (self.options.rpc_timeout)(),
            };
            tokio::spawn(async move {
                if let Err(status) = truncate_tablet(&ctx, &token, &tablet_id, this.options).await {
                    tracing::warn!(
                        "Truncate of tablet {} failed: {status}",
                        String::from_utf8_lossy(&tablet_id)
                    );
                }
                let mut pending = this.pending.lock().unwrap();
                if let Some(count) = pending.get_mut(&table_id) {
                    *count -= 1;
                    if *count == 0 {
                        pending.remove(&table_id);
                    }
                }
            });
        }
        if pb.table_type() != TableType::PGSQL_TABLE_TYPE {
            for index in &pb.indexes {
                self.truncate_one(token, snapshot, index.table_id())?;
            }
        }
        Ok(())
    }

    /// `IsTruncateTableDone`: no truncate of the table, or of a YCQL table's indexes, runs.
    pub fn is_truncate_table_done(
        &self,
        snapshot: &CatalogSnapshot,
        req: &IsTruncateTableDoneRequestPB,
        resp: &mut IsTruncateTableDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let mut ident = TableIdentifierPB::new();
        ident.set_table_id(req.table_id().to_vec());
        let table = find_table(snapshot, &ident, false)?;
        check_visible(&table, false)?;
        let pb: SysTablesEntryPB = table
            .parse()
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        let mut ids = vec![req.table_id().to_vec()];
        if pb.table_type() != TableType::PGSQL_TABLE_TYPE {
            for index in &pb.indexes {
                let mut ident = TableIdentifierPB::new();
                ident.set_table_id(index.table_id().to_vec());
                check_visible(&*find_table(snapshot, &ident, false)?, false)?;
                ids.push(index.table_id().to_vec());
            }
        }
        let pending = self.pending.lock().unwrap();
        resp.set_done(ids.iter().all(|id| !pending.contains_key(id)));
        Ok(())
    }
}

/// `AsyncTruncate`: `Truncate` on the tablet's leader, retried until it succeeds or the tablet is
/// gone.
async fn truncate_tablet(
    ctx: &BackfillContext,
    token: &LeaderToken,
    tablet_id: &[u8],
    options: TruncateOptions,
) -> Result<(), Status> {
    let now = hybrid_time_now(&ctx.lifecycle)?;
    call_tablet_leader(
        ctx,
        token,
        tablet_id,
        TS_SERVICE,
        "Truncate",
        |_leader| {
            let mut req = TruncateRequestPB::new();
            req.set_tablet_id(tablet_id.to_vec());
            req.set_propagated_hybrid_time(now);
            req.write_to_bytes().map_err(corrupt)
        },
        |bytes| {
            let resp = TruncateResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
            Ok(match resp.error.as_ref() {
                Some(e) => Err((
                    ts_status(&e.status),
                    e.code() == tablet_server_error_pb::Code::TABLET_NOT_FOUND,
                )),
                None => Ok(()),
            })
        },
        (options.retry)(),
    )
    .await
}

/// `IsTablePartOfCDCSDK`: a live CDCSDK stream of the namespace includes the table.
fn cdcsdk_streamed(snapshot: &CatalogSnapshot, namespace_id: &[u8], table_id: &[u8]) -> bool {
    snapshot.iter(SysRowEntryType::CDC_STREAM).any(|stream| {
        stream.parse::<SysCDCStreamEntryPB>().is_ok_and(|pb| {
            !pb.namespace_id().is_empty()
                && pb.namespace_id().as_bytes() == namespace_id
                && pb.table_id.iter().any(|t| t.as_bytes() == table_id)
                && !matches!(
                    pb.state(),
                    sys_cdcstream_entry_pb::State::DELETING
                        | sys_cdcstream_entry_pb::State::DELETED
                )
        })
    })
}
