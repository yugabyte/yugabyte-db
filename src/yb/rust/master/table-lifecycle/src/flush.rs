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

//! `FlushTables` and `IsFlushTablesDone` (C++'s `FlushManager`).
//!
//! As in C++, a flush is not persisted: each tserver with a replica of the tables' tablets gets
//! one `FlushTablets`, and the request is done once every tserver answered or gave up. A new
//! leader does not know an old leader's request, and answers NotFound for it, as C++ does.

use std::collections::{BTreeMap, HashMap, HashSet};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{Attempt, RetryPolicy, TsRpcExecutor};
use protobuf::{EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::sys_tables_entry_pb::State as TableState;
use yb_proto::pb::catalog_entity_info::SysTablesEntryPB;
use yb_proto::pb::master_admin::{
    FlushTablesRequestPB, FlushTablesResponsePB, GetCompactionStatusRequestPB,
    GetCompactionStatusResponsePB, IsFlushTablesDoneRequestPB, IsFlushTablesDoneResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::tablet_types::FlushCompactFlags;
use yb_proto::pb::tserver_admin::{
    flush_tablets_request_pb::Operation, FlushTabletsRequestPB, FlushTabletsResponsePB,
};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

use crate::backfill::{hybrid_time_now, ts_status};
use crate::lookup::find_table;
use crate::{coded_error, error, TableLifecycle};

const ADMIN_SERVICE: &str = "yb.tserver.TabletServerAdminService";

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct FlushOptions {
    /// `master_ts_rpc_timeout_ms`.
    pub rpc_timeout: fn() -> Duration,
    /// How a tserver's `FlushTablets` is retried (`RetryingTSRpcTask`).
    pub retry: fn() -> RetryPolicy,
}

#[derive(Default)]
struct Request {
    flushing: HashSet<Vec<u8>>,
    failed: HashSet<Vec<u8>>,
}

/// The leader's flushes.
pub struct Flushes {
    lifecycle: Arc<TableLifecycle>,
    executor: Arc<TsRpcExecutor>,
    rpc: OnceLock<Arc<dyn RpcClient>>,
    options: FlushOptions,
    requests: Mutex<HashMap<Vec<u8>, Request>>,
}

impl Flushes {
    /// The flushes of this master.
    pub fn new(
        lifecycle: Arc<TableLifecycle>,
        executor: Arc<TsRpcExecutor>,
        options: FlushOptions,
    ) -> Arc<Flushes> {
        Arc::new(Flushes {
            lifecycle,
            executor,
            rpc: OnceLock::new(),
            options,
            requests: Mutex::default(),
        })
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    /// `FlushManager::FlushTables`: flushes or compacts the tables (and their indexes with
    /// `add_indexes`) on every tserver that has a replica of one of their tablets.
    pub async fn flush_tables(
        self: &Arc<Self>,
        token: &LeaderToken,
        deadline: Instant,
        req: &FlushTablesRequestPB,
        resp: &mut FlushTablesResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let rpc = self.rpc.get().cloned().ok_or_else(|| {
            error(
                Code::UNKNOWN_ERROR,
                StatusCode::ServiceUnavailable,
                "The tserver RPC client is not set",
            )
        })?;
        let snapshot = self.lifecycle.catalog.snapshot();
        let tables = collect_tables(&snapshot, req)?;
        let mut ts_tablets: BTreeMap<Vec<u8>, Vec<Vec<u8>>> = BTreeMap::new();
        for table_id in &tables {
            for tablet in snapshot.hosting_tablets_of(table_id) {
                let Some(observed) = self.lifecycle.ts_manager.locations.get(&tablet.key.id) else {
                    continue;
                };
                for replica in &observed.replicas {
                    let tablets = ts_tablets.entry(replica.ts_uuid.to_vec()).or_default();
                    if !tablets.contains(&tablet.key.id.to_vec()) {
                        tablets.push(tablet.key.id.to_vec());
                    }
                }
            }
        }
        // A new leader knows no locations until tservers report; flushing nothing would look like
        // success.
        if ts_tablets.is_empty() {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::LeaderNotReadyToServe,
                "Master leader has no tablet replica locations for the requested tables, either it \
                 just became leader or there is a network partition",
            ));
        }
        if req.is_compaction() {
            self.record_compaction_request(token, deadline, &tables)
                .await?;
        }
        // `AsyncFlushTablets` of the last table: a vector index has only the vector indexes
        // flushed or compacted.
        let vector_index = tables.last().filter(|id| {
            snapshot
                .get(SysRowEntryType::TABLE, id)
                .and_then(|t| t.parse::<SysTablesEntryPB>().ok())
                .is_some_and(|pb| pb.index_info.vector_idx_options.is_some())
        });
        let vector_index = vector_index.cloned();
        let flush_id = uuid_hex();
        {
            let mut requests = self.requests.lock().unwrap();
            requests.retain(|_, r| !r.flushing.is_empty());
            requests.insert(
                flush_id.clone(),
                Request {
                    flushing: ts_tablets.keys().cloned().collect(),
                    failed: HashSet::new(),
                },
            );
        }
        let operation = if req.is_compaction() {
            Operation::COMPACT
        } else {
            Operation::FLUSH
        };
        let mut policy = (self.options.retry)();
        policy.deadline = policy
            .deadline
            .min(deadline.saturating_duration_since(Instant::now()));
        for (ts_uuid, tablet_ids) in ts_tablets {
            let this = self.clone();
            let rpc = rpc.clone();
            let token = *token;
            let flush_id = flush_id.clone();
            let (flags, vector_index) = match &vector_index {
                Some(id) => (
                    Some(EnumOrUnknown::new(
                        FlushCompactFlags::FLUSH_COMPACT_VECTOR_INDEX_ONLY,
                    )),
                    vec![id.clone()],
                ),
                None => (req.flags, Vec::new()),
            };
            tokio::spawn(async move {
                let result = this
                    .flush_on(
                        &rpc,
                        &token,
                        &ts_uuid,
                        tablet_ids,
                        operation,
                        flags,
                        vector_index,
                        &policy,
                    )
                    .await;
                if let Err(status) = &result {
                    tracing::warn!(
                        "FlushTablets on tserver {} failed: {status}",
                        String::from_utf8_lossy(&ts_uuid)
                    );
                }
                let mut requests = this.requests.lock().unwrap();
                if let Some(request) = requests.get_mut(&flush_id) {
                    if request.flushing.remove(&ts_uuid) && result.is_err() {
                        request.failed.insert(ts_uuid);
                    }
                }
            });
        }
        tracing::info!(
            "Started flush request {}",
            String::from_utf8_lossy(&flush_id)
        );
        resp.set_flush_request_id(flush_id);
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    async fn flush_on(
        &self,
        rpc: &Arc<dyn RpcClient>,
        token: &LeaderToken,
        ts_uuid: &[u8],
        tablet_ids: Vec<Vec<u8>>,
        operation: Operation,
        flags: Option<EnumOrUnknown<yb_proto::pb::tablet_types::FlushCompactFlags>>,
        vector_index_ids: Vec<Vec<u8>>,
        policy: &RetryPolicy,
    ) -> Result<(), Status> {
        let lifecycle = &self.lifecycle;
        let timeout = (self.options.rpc_timeout)();
        self.executor
            .run(ts_uuid, "FlushTablets", policy, |_| {
                let current = lifecycle.leadership.check(token);
                let address = lifecycle.ts_manager.rpc_address(ts_uuid);
                let now = hybrid_time_now(lifecycle);
                let mut req = FlushTabletsRequestPB::new();
                req.set_dest_uuid(ts_uuid.to_vec());
                req.tablet_ids = tablet_ids.clone();
                req.operation = Some(EnumOrUnknown::new(operation));
                req.flags = flags;
                req.vector_index_ids = vector_index_ids.clone();
                let rpc = rpc.clone();
                async move {
                    current.map_err(Attempt::Fail)?;
                    req.set_propagated_hybrid_time(now.map_err(Attempt::Fail)?);
                    let (host, port) = address.ok_or_else(|| {
                        Attempt::Retry(Status::new(
                            StatusCode::NotFound,
                            "tserver is not registered",
                        ))
                    })?;
                    let bytes = req.write_to_bytes().map_err(|e| {
                        Attempt::Fail(Status::new(StatusCode::Corruption, e.to_string()))
                    })?;
                    let resp = rpc
                        .call(&host, port, ADMIN_SERVICE, "FlushTablets", &bytes, timeout)
                        .await
                        .map_err(Attempt::Retry)?;
                    let resp = FlushTabletsResponsePB::parse_from_bytes(&resp).map_err(|e| {
                        Attempt::Retry(Status::new(StatusCode::Corruption, e.to_string()))
                    })?;
                    match resp.error.as_ref() {
                        None => Ok(()),
                        Some(e) if e.code() == tablet_server_error_pb::Code::TABLET_NOT_FOUND => {
                            Err(Attempt::Fail(ts_status(&e.status)))
                        }
                        Some(e) => Err(Attempt::Retry(ts_status(&e.status))),
                    }
                }
            })
            .await
    }

    /// `UpdateLastFullCompactionRequestTime` for each table.
    async fn record_compaction_request(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        tables: &[Vec<u8>],
    ) -> Result<(), MasterErrorPB> {
        let now = hybrid_time_now(&self.lifecycle)
            .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
        self.lifecycle
            .write(token, deadline, |snapshot| {
                let mut mutation = Mutation::new();
                for table_id in tables {
                    let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                        continue;
                    };
                    let mut pb: SysTablesEntryPB = table.parse().map_err(|e| {
                        error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string())
                    })?;
                    pb.set_last_full_compaction_request_time(now);
                    mutation = mutation
                        .read(table)
                        .upsert_message(SysRowEntryType::TABLE, table_id, &pb)
                        .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
                }
                Ok((mutation, ()))
            })
            .await
    }

    /// `FlushManager::IsFlushTablesDone`.
    pub fn is_flush_tables_done(
        &self,
        req: &IsFlushTablesDoneRequestPB,
        resp: &mut IsFlushTablesDoneResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let requests = self.requests.lock().unwrap();
        let request = requests.get(req.flush_request_id()).ok_or_else(|| {
            coded_error(
                Code::INVALID_REQUEST,
                StatusCode::NotFound,
                format!(
                    "The flush request was not found: {}",
                    String::from_utf8_lossy(req.flush_request_id())
                ),
            )
        })?;
        resp.set_done(request.flushing.is_empty());
        resp.set_success(request.failed.is_empty());
        Ok(())
    }
}

/// `CollectTables` for a flush: the named tables, and their indexes with `add_indexes`.
fn collect_tables(
    snapshot: &CatalogSnapshot,
    req: &FlushTablesRequestPB,
) -> Result<Vec<Vec<u8>>, MasterErrorPB> {
    let mut tables: Vec<Vec<u8>> = Vec::new();
    for ident in &req.tables {
        let table = find_table(snapshot, ident, false)?;
        if !tables.contains(&table.key.id.to_vec()) {
            tables.push(table.key.id.to_vec());
        }
        if req.add_indexes() {
            let pb: SysTablesEntryPB = table
                .parse()
                .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
            for index in &pb.indexes {
                if !tables.contains(&index.table_id().to_vec()) {
                    tables.push(index.table_id().to_vec());
                }
            }
        }
    }
    Ok(tables)
}

/// `GenerateId`: a random UUID in hex.
fn uuid_hex() -> Vec<u8> {
    use rand::RngCore;
    let mut bytes = [0u8; 16];
    rand::rng().fill_bytes(&mut bytes);
    bytes
        .iter()
        .map(|b| format!("{b:02x}"))
        .collect::<String>()
        .into_bytes()
}

impl TableLifecycle {
    /// `GetCompactionStatus`: the table's last full compaction request, and its replicas' full
    /// compaction states as heartbeats report them: UNKNOWN if any replica's is, else COMPACTING
    /// if any is; the table's last full compaction is the earliest replica's.
    pub fn get_compaction_status(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetCompactionStatusRequestPB,
        resp: &mut GetCompactionStatusResponsePB,
    ) -> Result<(), MasterErrorPB> {
        use yb_proto::pb::tablet_types::FullCompactionState;
        let table = snapshot
            .get(SysRowEntryType::TABLE, req.table.table_id())
            .filter(|t| t.table().map(|h| h.state) != Some(TableState::DELETED as i32))
            .ok_or_else(|| {
                crate::tagged_error(
                    Code::OBJECT_NOT_FOUND,
                    StatusCode::NotFound,
                    format!(
                        "Table with id {} does not exist",
                        String::from_utf8_lossy(req.table.table_id())
                    ),
                )
            })?;
        let pb: SysTablesEntryPB = table
            .parse()
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        let tablets = snapshot.hosting_tablets_of(&table.key.id);
        let unknown = FullCompactionState::FULL_COMPACTION_STATE_UNKNOWN as i32;
        let mut state = if tablets.is_empty() {
            unknown
        } else {
            FullCompactionState::IDLE as i32
        };
        let mut last_time = 0u64;
        'tablets: for tablet in &tablets {
            if state == unknown {
                break;
            }
            let observed = self.lifecycle_locations(&tablet.key.id);
            for replica in &observed {
                let status = replica.full_compaction;
                if status.state == unknown {
                    state = unknown;
                    break 'tablets;
                }
                if status.state == FullCompactionState::COMPACTING as i32 {
                    state = status.state;
                }
                last_time = if last_time == 0 {
                    status.last_time
                } else {
                    last_time.min(status.last_time)
                };
            }
        }
        resp.set_last_request_time(pb.last_full_compaction_request_time());
        resp.set_last_full_compaction_time(last_time);
        resp.full_compaction_state = Some(EnumOrUnknown::from_i32(state));
        if req.show_tablets() {
            for tablet in &tablets {
                for replica in self.lifecycle_locations(&tablet.key.id) {
                    let mut out =
                        yb_proto::pb::master_admin::TabletReplicaFullCompactionStatus::new();
                    out.set_ts_id(replica.ts_uuid.to_vec());
                    out.set_tablet_id(tablet.key.id.to_vec());
                    out.full_compaction_state =
                        Some(EnumOrUnknown::from_i32(replica.full_compaction.state));
                    out.set_last_full_compaction_time(replica.full_compaction.last_time);
                    resp.replica_statuses.push(out);
                }
            }
        }
        Ok(())
    }

    fn lifecycle_locations(&self, tablet_id: &[u8]) -> Vec<master_tablet_locations::Replica> {
        self.ts_manager
            .locations
            .get(tablet_id)
            .map(|r| r.replicas)
            .unwrap_or_default()
    }
}
