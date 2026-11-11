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

//! YSQL index backfill (`BackfillTable` and `MultiStageAlterTable` for YSQL).
//!
//! A YSQL index is created at WRITE_AND_DELETE. PG's `BackfillIndex` moves it to DO_BACKFILL, an
//! alter of the indexed table; once every tablet has that version, the alterer runs [`run`]: it
//! records a backfill job on the indexed table, picks a read time no tablet's safe time is past,
//! has each tablet's leader backfill the index in chunks (persisting how far each tablet got, so
//! a new leader resumes), and then moves the index to READ_WRITE_AND_DELETE, or, if a tablet
//! failed, records the error and moves it to WRITE_AND_DELETE_WHILE_REMOVING. PG polls the indexed
//! table's schema for either.

use std::collections::{BTreeMap, HashSet};
use std::sync::Arc;
use std::time::Duration;

use master_catalog_state::{CatalogSnapshot, Mutation};
use master_leadership::LeaderToken;
use master_task_runtime::{Attempt, RetryPolicy, TsRpcExecutor};
use protobuf::{Enum, EnumOrUnknown, Message};
use tokio::task::JoinSet;
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    backfill_job_pb, sys_tables_entry_pb, BackfillJobPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common::IndexInfoPB;
use yb_proto::pb::common_types::{IndexPermissions, TableType};
use yb_proto::pb::master_ddl::{
    BackfillIndexRequestPB, BackfillIndexResponsePB, GetBackfillStatusRequestPB,
    GetBackfillStatusResponsePB, IndexStatusPB,
};
use yb_proto::pb::master_rust_peer::{HybridTimeNowRequestPB, HybridTimeNowResponsePB};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};
use yb_proto::pb::operations::ChangeMetadataRequestPB;
use yb_proto::pb::tserver_admin::{
    BackfillIndexRequestPB as TsBackfillIndexRequestPB,
    BackfillIndexResponsePB as TsBackfillIndexResponsePB, ChangeMetadataResponsePB,
    GetSafeTimeRequestPB, GetSafeTimeResponsePB,
};
use yb_proto::pb::tserver_types::tablet_server_error_pb;
use yb_proto::pb::wire_protocol::AppStatusPB;

use crate::lookup::find_table;
use crate::time::local_time_string;
use crate::{error, TableLifecycle};

use sys_tables_entry_pb::State as TableState;

const ADMIN_SERVICE: &str = "yb.tserver.TabletServerAdminService";

pub(crate) fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

pub(crate) fn ts_status(status: &AppStatusPB) -> Status {
    Status::new(
        StatusCode::from_i32(status.code().value()),
        status.message().to_string(),
    )
}

/// Tserver errors a backfill or safe-time request is not retried on.
pub(crate) fn permanent(code: tablet_server_error_pb::Code) -> bool {
    use tablet_server_error_pb::Code::*;
    matches!(
        code,
        TABLET_NOT_FOUND | MISMATCHED_SCHEMA | TABLET_HAS_A_NEWER_SCHEMA | OPERATION_NOT_SUPPORTED
    )
}

/// `UpdateIndexPermission`'s change of the row: the given indexes move to their permission, as
/// an alter that only changes index permissions.
fn set_index_permissions(pb: &mut SysTablesEntryPB, perms: &BTreeMap<Vec<u8>, IndexPermissions>) {
    crate::alter::copy_to_fully_applied(pb);
    let mut changed = false;
    for index in &mut pb.indexes {
        if let Some(perm) = perms.get(index.table_id()) {
            if index.index_permissions().value() < perm.value() {
                index.set_index_permissions(*perm);
                changed = true;
            }
        }
    }
    if changed {
        pb.set_version(pb.version() + 1);
        pb.set_updates_only_index_permissions(true);
    }
    pb.state = Some(EnumOrUnknown::new(TableState::ALTERING));
    pb.set_state_msg(
        format!(
            "Update index permission version={} ts={}",
            pb.version(),
            local_time_string()
        )
        .into_bytes(),
    );
}

impl TableLifecycle {
    /// `BackfillIndex`: PG asks for its index's backfill, which moves it to DO_BACKFILL. The
    /// alterer starts the backfill once every tablet has that.
    pub async fn backfill_index(
        &self,
        token: &LeaderToken,
        deadline: Instant,
        req: &BackfillIndexRequestPB,
        resp: &mut BackfillIndexResponsePB,
    ) -> Result<(), MasterErrorPB> {
        let snapshot = self.catalog.snapshot();
        let index = find_table(&snapshot, &req.index_identifier, false)?;
        let index_pb: SysTablesEntryPB = index
            .parse()
            .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
        if index_pb.table_type() != TableType::PGSQL_TABLE_TYPE {
            return Err(error(
                Code::UNKNOWN_ERROR,
                StatusCode::InvalidArgument,
                "Unexpected non-YSQL table",
            ));
        }
        let indexed_id = if index_pb.index_info.is_some() {
            index_pb.index_info.indexed_table_id().to_vec()
        } else {
            index_pb.indexed_table_id().to_vec()
        };
        resp.table_identifier
            .mut_or_insert_default()
            .set_table_id(indexed_id.clone());
        let index_id = index.key.id.to_vec();
        self.write(token, deadline, |snapshot| {
            let indexed = snapshot
                .get(SysRowEntryType::TABLE, &indexed_id)
                .ok_or_else(|| {
                    error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::InvalidArgument,
                        "Empty indexed table",
                    )
                })?;
            let mut pb: SysTablesEntryPB = indexed
                .parse()
                .map_err(|e| error(Code::UNKNOWN_ERROR, StatusCode::Corruption, e.to_string()))?;
            let Some(perm) = pb
                .indexes
                .iter()
                .find(|i| i.table_id() == index_id.as_slice())
                .map(|i| i.index_permissions())
            else {
                return Err(error(
                    Code::INVALID_SCHEMA,
                    StatusCode::InvalidArgument,
                    format!(
                        "Index {} is not an index of {}",
                        String::from_utf8_lossy(&index_id),
                        String::from_utf8_lossy(&indexed_id)
                    ),
                ));
            };
            // A resent request after the move succeeded.
            if perm.value() >= IndexPermissions::INDEX_PERM_DO_BACKFILL.value() {
                return Ok((Mutation::new(), ()));
            }
            if perm != IndexPermissions::INDEX_PERM_WRITE_AND_DELETE {
                return Err(error(
                    Code::INVALID_SCHEMA,
                    StatusCode::InvalidArgument,
                    format!("Expected WRITE_AND_DELETE perm, got {perm:?}"),
                ));
            }
            if !matches!(pb.state(), TableState::RUNNING | TableState::ALTERING) {
                return Err(error(
                    Code::UNKNOWN_ERROR,
                    StatusCode::IllegalState,
                    format!(
                        "Table is not in ALTERING or RUNNING state: {:?}",
                        pb.state()
                    ),
                ));
            }
            let mut perms = BTreeMap::new();
            perms.insert(index_id.clone(), IndexPermissions::INDEX_PERM_DO_BACKFILL);
            set_index_permissions(&mut pb, &perms);
            let mutation = Mutation::new()
                .read(indexed)
                .upsert_message(SysRowEntryType::TABLE, &indexed_id, &pb)
                .map_err(|s| error(Code::UNKNOWN_ERROR, s.code, s.message))?;
            Ok((mutation, ()))
        })
        .await
    }
}

impl TableLifecycle {
    /// `GetBackfillStatus`: BACKFILL_SUCCESS for an index at READ_WRITE_AND_DELETE, with its
    /// birth time; an error for one that is gone or is not an index.
    pub fn get_backfill_status(
        &self,
        snapshot: &CatalogSnapshot,
        req: &GetBackfillStatusRequestPB,
        resp: &mut GetBackfillStatusResponsePB,
    ) -> Result<(), MasterErrorPB> {
        use yb_proto::pb::master_ddl::index_status_pb::BackfillStatus;
        let status_pb = |status: Status| {
            let mut app = AppStatusPB::new();
            app.set_code(
                yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::from_i32(status.code as i32)
                    .unwrap_or(
                        yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::INTERNAL_ERROR,
                    ),
            );
            app.set_message(status.message);
            app
        };
        for ident in &req.index_tables {
            let mut out = IndexStatusPB::new();
            out.index_table = Some(ident.clone()).into();
            let found = find_table(snapshot, ident, false)
                .map_err(|e| ts_status(&e.status))
                .and_then(|index| {
                    let pb: SysTablesEntryPB = index.parse().map_err(corrupt)?;
                    let indexed_id = if pb.index_info.is_some() {
                        pb.index_info.indexed_table_id().to_vec()
                    } else {
                        pb.indexed_table_id().to_vec()
                    };
                    if indexed_id.is_empty() {
                        return Err(Status::new(
                            StatusCode::InvalidArgument,
                            "The table is not an index table",
                        ));
                    }
                    let indexed: SysTablesEntryPB = snapshot
                        .get(SysRowEntryType::TABLE, &indexed_id)
                        .ok_or_else(|| {
                            Status::new(StatusCode::NotFound, "Indexed table is not found")
                        })?
                        .parse()
                        .map_err(corrupt)?;
                    let info = indexed
                        .indexes
                        .iter()
                        .find(|i| i.table_id() == index.key.id.as_ref())
                        .ok_or_else(|| {
                            Status::new(StatusCode::NotFound, "Index table is not found")
                        })?;
                    let birth_time = pb.index_info.birth_time();
                    Ok((info.index_permissions(), birth_time))
                });
            match found {
                Ok((perm, birth_time)) => {
                    out.set_backfill_status(
                        if perm == IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE {
                            BackfillStatus::BACKFILL_SUCCESS
                        } else {
                            BackfillStatus::BACKFILL_UNKNOWN
                        },
                    );
                    if birth_time != 0 {
                        out.set_birth_time(birth_time);
                    }
                }
                Err(status) => out.error = Some(status_pb(status)).into(),
            }
            resp.index_status.push(out);
        }
        Ok(())
    }
}

/// What a backfill job needs to send its RPCs.
pub(crate) struct BackfillContext {
    pub(crate) lifecycle: Arc<TableLifecycle>,
    pub(crate) rpc: Arc<dyn RpcClient>,
    pub(crate) executor: Arc<TsRpcExecutor>,
    pub(crate) rpc_timeout: Duration,
}

/// The job's state, read from the indexed table's row.
struct Job {
    indexes: Vec<IndexInfoPB>,
    namespace_name: String,
    schema_version: u32,
    read_time: Option<u64>,
    using_table_locks: bool,
}

/// Runs the backfill of the indexed table's DO_BACKFILL indexes, to its end.
pub(crate) async fn run(
    ctx: &BackfillContext,
    token: LeaderToken,
    table_id: &[u8],
) -> Result<(), Status> {
    let Some(job) = start(ctx, &token, table_id).await? else {
        return Ok(());
    };
    let index_ids: HashSet<Vec<u8>> = job.indexes.iter().map(|i| i.table_id().to_vec()).collect();
    tracing::info!(
        "Backfilling indexes {:?} of table {}",
        index_ids
            .iter()
            .map(|i| String::from_utf8_lossy(i).into_owned())
            .collect::<Vec<_>>(),
        String::from_utf8_lossy(table_id)
    );
    let read_time = match job.read_time {
        Some(time) => time,
        None => {
            let time = safe_time(ctx, &token, table_id, job.using_table_locks).await?;
            persist(ctx, &token, table_id, |pb| {
                if let Some(job) = pb.backfill_jobs.first_mut() {
                    job.set_backfilling_timestamp(time);
                }
            })
            .await?;
            time
        }
    };
    let snapshot = ctx.lifecycle.catalog.snapshot();
    let mut tablets = JoinSet::new();
    for tablet in snapshot.active_tablets_of(table_id) {
        let tablet_id = tablet.key.id.to_vec();
        let pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
        // The least far any of the indexes got on this tablet; absent is from the start.
        let mut start_key: Option<Vec<u8>> = None;
        let mut done = true;
        for index in &index_ids {
            match pb
                .backfilled_until
                .get(&String::from_utf8_lossy(index).into_owned())
            {
                Some(key) if key.is_empty() => {}
                Some(key) => {
                    done = false;
                    start_key = Some(match start_key {
                        Some(k) if k.as_slice() < key.as_slice() => k,
                        _ => key.clone(),
                    });
                }
                None => {
                    done = false;
                    start_key = Some(Vec::new());
                }
            }
        }
        if done {
            continue;
        }
        let lifecycle = ctx.lifecycle.clone();
        let rpc = ctx.rpc.clone();
        let executor = ctx.executor.clone();
        let rpc_timeout = ctx.rpc_timeout;
        let indexes = job.indexes.clone();
        let namespace_name = job.namespace_name.clone();
        let table_id = table_id.to_vec();
        let schema_version = job.schema_version;
        let index_ids = index_ids.clone();
        tablets.spawn(async move {
            let ctx = BackfillContext {
                lifecycle,
                rpc,
                executor,
                rpc_timeout,
            };
            backfill_tablet(
                &ctx,
                &token,
                &table_id,
                &tablet_id,
                start_key.unwrap_or_default(),
                read_time,
                schema_version,
                &namespace_name,
                &indexes,
                &index_ids,
            )
            .await
        });
    }
    let mut failure = None;
    while let Some(result) = tablets.join_next().await {
        if let Err(status) = result.unwrap_or_else(|e| Err(corrupt(e))) {
            failure.get_or_insert(status);
        }
    }
    ctx.lifecycle
        .leadership
        .check(&token)
        .map_err(|s| Status::new(StatusCode::IllegalState, s.message))?;
    match failure {
        None => succeed(ctx, &token, table_id, &index_ids, read_time).await,
        Some(status) => fail(ctx, &token, table_id, &index_ids, &status).await,
    }
}

/// Records the job (once) and stays ALTERING with the fully applied schema cleared
/// (`StartBackfillingData`); `None` when nothing is at DO_BACKFILL.
async fn start(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
) -> Result<Option<Job>, Status> {
    let (job, _) = ctx
        .lifecycle
        .writer
        .mutate_with(token, Default::default(), |snapshot| {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                return Ok((Mutation::new(), None));
            };
            let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            let ready: Vec<IndexInfoPB> = pb
                .indexes
                .iter()
                .filter(|i| i.index_permissions() == IndexPermissions::INDEX_PERM_DO_BACKFILL)
                .cloned()
                .collect();
            if ready.is_empty() || pb.state() != TableState::ALTERING {
                return Ok((Mutation::new(), None));
            }
            if pb.backfill_jobs.is_empty() {
                // One index at a time, as C++ does for YSQL.
                let mut job = BackfillJobPB::new();
                job.indexes.push(ready[0].clone());
                job.backfill_state.insert(
                    String::from_utf8_lossy(ready[0].table_id()).into_owned(),
                    EnumOrUnknown::new(backfill_job_pb::State::IN_PROGRESS),
                );
                pb.backfill_jobs.push(job);
            }
            pb.fully_applied_schema.clear();
            pb.clear_fully_applied_schema_version();
            pb.fully_applied_indexes.clear();
            pb.fully_applied_index_info.clear();
            let job = &pb.backfill_jobs[0];
            let using_table_locks = pb
                .transaction
                .as_ref()
                .is_some_and(|t| t.using_table_locks());
            let namespace_name = snapshot
                .get(SysRowEntryType::NAMESPACE, pb.namespace_id())
                .and_then(|n| n.namespace().map(|n| n.name.clone()))
                .unwrap_or_default();
            let found = Job {
                // The current IndexInfoPBs, at DO_BACKFILL.
                indexes: job
                    .indexes
                    .iter()
                    .filter_map(|j| {
                        pb.indexes
                            .iter()
                            .find(|i| i.table_id() == j.table_id())
                            .cloned()
                    })
                    .collect(),
                namespace_name,
                schema_version: pb.version(),
                read_time: job
                    .has_backfilling_timestamp()
                    .then(|| job.backfilling_timestamp()),
                using_table_locks,
            };
            let mutation = Mutation::new().read(table).upsert(
                SysRowEntryType::TABLE,
                table_id,
                pb.write_to_bytes().map_err(corrupt)?,
            );
            Ok((mutation, Some(found)))
        })
        .await?;
    Ok(job)
}

/// Changes the indexed table's row.
async fn persist(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
    change: impl Fn(&mut SysTablesEntryPB),
) -> Result<(), Status> {
    ctx.lifecycle
        .writer
        .mutate(token, |snapshot| {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                return Ok((Mutation::new(), ()));
            };
            let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            change(&mut pb);
            Ok((
                Mutation::new().read(table).upsert(
                    SysRowEntryType::TABLE,
                    table_id,
                    pb.write_to_bytes().map_err(corrupt)?,
                ),
                (),
            ))
        })
        .await
        .map(|_| ())
}

pub(crate) fn hybrid_time_now(lifecycle: &TableLifecycle) -> Result<u64, Status> {
    let bytes = lifecycle.host.call(
        "HybridTimeNow",
        &HybridTimeNowRequestPB::new()
            .write_to_bytes()
            .map_err(corrupt)?,
    )?;
    Ok(HybridTimeNowResponsePB::parse_from_bytes(&bytes)
        .map_err(corrupt)?
        .hybrid_time())
}

fn leader_of(lifecycle: &TableLifecycle, tablet_id: &[u8]) -> Option<Vec<u8>> {
    lifecycle
        .ts_manager
        .locations
        .get(tablet_id)
        .and_then(|r| r.leader().map(|l| l.ts_uuid.to_vec()))
}

/// `LaunchComputeSafeTimeForRead`: the latest safe time of the table's tablets, each at least
/// the master's time now.
async fn safe_time(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
    using_table_locks: bool,
) -> Result<u64, Status> {
    let min_cutoff = hybrid_time_now(&ctx.lifecycle)?;
    let snapshot = ctx.lifecycle.catalog.snapshot();
    let mut read_time = 0u64;
    for tablet in snapshot.active_tablets_of(table_id) {
        let tablet_id = tablet.key.id.to_vec();
        let time = call_leader(
            ctx,
            token,
            &tablet_id,
            "GetSafeTime",
            |leader| {
                let mut req = GetSafeTimeRequestPB::new();
                req.set_dest_uuid(leader.to_vec());
                req.set_tablet_id(tablet_id.clone());
                req.set_min_hybrid_time_for_backfill(min_cutoff);
                req.set_propagated_hybrid_time(min_cutoff);
                req.set_only_abort_txns_not_using_table_locks(using_table_locks);
                req.write_to_bytes().map_err(corrupt)
            },
            |bytes| {
                let resp = GetSafeTimeResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
                Ok(match resp.error.as_ref() {
                    Some(e) => Err((ts_status(&e.status), permanent(e.code()))),
                    None => Ok(resp.safe_time()),
                })
            },
            RetryPolicy::default(),
        )
        .await?;
        read_time = read_time.max(time);
    }
    Ok(read_time)
}

/// Sends an RPC to a tablet's leader, picked again on every attempt.
#[allow(clippy::too_many_arguments)]
async fn call_leader<T>(
    ctx: &BackfillContext,
    token: &LeaderToken,
    tablet_id: &[u8],
    method: &'static str,
    request: impl Fn(&[u8]) -> Result<Vec<u8>, Status>,
    response: impl Fn(&[u8]) -> Result<Result<T, (Status, bool)>, Status>,
    policy: RetryPolicy,
) -> Result<T, Status> {
    call_tablet_leader(
        ctx,
        token,
        tablet_id,
        ADMIN_SERVICE,
        method,
        request,
        response,
        policy,
    )
    .await
}

/// Calls `service.method` on the tablet's leader, retrying as `policy` allows while this master
/// leads. `response` gives the value, or the tserver's error and whether it is permanent.
#[allow(clippy::too_many_arguments)]
pub(crate) async fn call_tablet_leader<T>(
    ctx: &BackfillContext,
    token: &LeaderToken,
    tablet_id: &[u8],
    service: &'static str,
    method: &'static str,
    request: impl Fn(&[u8]) -> Result<Vec<u8>, Status>,
    response: impl Fn(&[u8]) -> Result<Result<T, (Status, bool)>, Status>,
    policy: RetryPolicy,
) -> Result<T, Status> {
    let lifecycle = &ctx.lifecycle;
    let leader_key = leader_of(lifecycle, tablet_id).unwrap_or_default();
    ctx.executor
        .run(&leader_key, method, &policy, |_| {
            let current = lifecycle.leadership.check(token);
            let leader = leader_of(lifecycle, tablet_id);
            let address = leader
                .as_ref()
                .and_then(|l| lifecycle.ts_manager.rpc_address(l));
            let bytes = leader.as_ref().map(|l| request(l));
            let rpc = ctx.rpc.clone();
            let timeout = ctx.rpc_timeout;
            let response = &response;
            async move {
                current.map_err(Attempt::Fail)?;
                let (host, port) = address.ok_or_else(|| {
                    Attempt::Retry(Status::new(
                        StatusCode::ServiceUnavailable,
                        "No leader for the tablet",
                    ))
                })?;
                let bytes = bytes.expect("a leader").map_err(Attempt::Fail)?;
                let resp = rpc
                    .call(&host, port, service, method, &bytes, timeout)
                    .await
                    .map_err(Attempt::Retry)?;
                match response(&resp).map_err(Attempt::Retry)? {
                    Ok(value) => Ok(value),
                    Err((status, true)) => Err(Attempt::Fail(status)),
                    Err((status, false)) => Err(Attempt::Retry(status)),
                }
            }
        })
        .await
}

/// `BackfillTablet`: chunks from `start_key` until the tablet says it is done.
#[allow(clippy::too_many_arguments)]
async fn backfill_tablet(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
    tablet_id: &[u8],
    mut start_key: Vec<u8>,
    read_time: u64,
    schema_version: u32,
    namespace_name: &str,
    indexes: &[IndexInfoPB],
    index_ids: &HashSet<Vec<u8>>,
) -> Result<(), Status> {
    loop {
        let key = start_key.clone();
        let backfilled_until = call_leader(
            ctx,
            token,
            tablet_id,
            "BackfillIndex",
            |leader| {
                let mut req = TsBackfillIndexRequestPB::new();
                req.set_dest_uuid(leader.to_vec());
                req.set_tablet_id(tablet_id.to_vec());
                req.set_read_at_hybrid_time(read_time);
                req.set_schema_version(schema_version);
                req.set_start_key(key.clone());
                req.set_indexed_table_id(table_id.to_vec());
                req.set_namespace_name(namespace_name.to_string());
                req.indexes = indexes.to_vec();
                req.write_to_bytes().map_err(corrupt)
            },
            |bytes| {
                let resp = TsBackfillIndexResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
                Ok(match resp.error.as_ref() {
                    Some(e) => Err((ts_status(&e.status), permanent(e.code()))),
                    None => Ok(resp.backfilled_until().to_vec()),
                })
            },
            RetryPolicy {
                max_attempts: 150,
                max_delay: Duration::from_secs(600),
                deadline: Duration::from_secs(24 * 3600),
                ..RetryPolicy::default()
            },
        )
        .await?;
        // Persist how far the tablet got, so a new leader resumes there.
        ctx.lifecycle
            .writer
            .mutate(token, |snapshot| {
                let Some(tablet) = snapshot.get(SysRowEntryType::TABLET, tablet_id) else {
                    return Ok((Mutation::new(), ()));
                };
                let mut pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                for index in index_ids {
                    pb.backfilled_until.insert(
                        String::from_utf8_lossy(index).into_owned(),
                        backfilled_until.clone(),
                    );
                }
                Ok((
                    Mutation::new().read(tablet).upsert(
                        SysRowEntryType::TABLET,
                        tablet_id,
                        pb.write_to_bytes().map_err(corrupt)?,
                    ),
                    (),
                ))
            })
            .await?;
        if backfilled_until.is_empty() {
            tracing::info!(
                "Done backfilling tablet {}",
                String::from_utf8_lossy(tablet_id)
            );
            return Ok(());
        }
        start_key = backfilled_until;
    }
}

/// `MarkAllIndexesAsSuccess`, `AllowCompactionsToGCDeleteMarkers` and the permission update to
/// READ_WRITE_AND_DELETE.
async fn succeed(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
    index_ids: &HashSet<Vec<u8>>,
    read_time: u64,
) -> Result<(), Status> {
    for index_id in index_ids {
        allow_gc_of_delete_markers(ctx, token, index_id, read_time).await?;
    }
    finish(ctx, token, table_id, index_ids, None).await
}

/// `MarkIndexesAsFailed` and the permission update to WRITE_AND_DELETE_WHILE_REMOVING.
async fn fail(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
    index_ids: &HashSet<Vec<u8>>,
    status: &Status,
) -> Result<(), Status> {
    tracing::warn!(
        "Backfill of table {} failed: {status}",
        String::from_utf8_lossy(table_id)
    );
    finish(ctx, token, table_id, index_ids, Some(status)).await
}

/// Records the outcome on the index infos, moves their permissions, clears the job and the
/// tablets' progress.
async fn finish(
    ctx: &BackfillContext,
    token: &LeaderToken,
    table_id: &[u8],
    index_ids: &HashSet<Vec<u8>>,
    failure: Option<&Status>,
) -> Result<(), Status> {
    let perm = if failure.is_some() {
        IndexPermissions::INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING
    } else {
        IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE
    };
    ctx.lifecycle
        .writer
        .mutate(token, |snapshot: &CatalogSnapshot| {
            let Some(table) = snapshot.get(SysRowEntryType::TABLE, table_id) else {
                return Ok((Mutation::new(), ()));
            };
            let mut pb: SysTablesEntryPB = table.parse().map_err(corrupt)?;
            for index in &mut pb.indexes {
                if !index_ids.contains(index.table_id()) {
                    continue;
                }
                match failure {
                    Some(status) => {
                        index.set_backfill_error_message(status.message.clone());
                        let mut app = AppStatusPB::new();
                        app.set_code(
                            yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::from_i32(
                                status.code as i32,
                            )
                            .unwrap_or(
                                yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::INTERNAL_ERROR,
                            ),
                        );
                        app.set_message(status.message.clone());
                        index.backfill_status = Some(app).into();
                    }
                    None => {
                        index.clear_backfill_error_message();
                        index.backfill_status.clear();
                    }
                }
                index.clear_is_backfill_deferred();
            }
            let perms = index_ids.iter().map(|id| (id.clone(), perm)).collect();
            set_index_permissions(&mut pb, &perms);
            pb.backfill_jobs.clear();
            let mut mutation = Mutation::new().read(table).upsert(
                SysRowEntryType::TABLE,
                table_id,
                pb.write_to_bytes().map_err(corrupt)?,
            );
            // ClearCheckpointStateInTablets.
            for tablet in snapshot.tablets_of(table_id) {
                let mut tablet_pb: SysTabletsEntryPB = tablet.parse().map_err(corrupt)?;
                let before = tablet_pb.backfilled_until.len();
                for index in index_ids {
                    tablet_pb
                        .backfilled_until
                        .remove(&String::from_utf8_lossy(index).into_owned());
                }
                if tablet_pb.backfilled_until.len() != before {
                    mutation = mutation.read(tablet).upsert(
                        SysRowEntryType::TABLET,
                        &tablet.key.id,
                        tablet_pb.write_to_bytes().map_err(corrupt)?,
                    );
                }
            }
            Ok((mutation, ()))
        })
        .await?;
    tracing::info!(
        "Done backfill on table {}: indexes at {perm:?}",
        String::from_utf8_lossy(table_id)
    );
    Ok(())
}

/// `AllowCompactionsToGCDeleteMarkers`: once the index table is RUNNING, it stops retaining
/// delete markers, records its birth time, and its tablets learn the backfill is done.
async fn allow_gc_of_delete_markers(
    ctx: &BackfillContext,
    token: &LeaderToken,
    index_id: &[u8],
    read_time: u64,
) -> Result<(), Status> {
    loop {
        let snapshot = ctx.lifecycle.catalog.snapshot();
        let Some(state) = snapshot
            .get(SysRowEntryType::TABLE, index_id)
            .and_then(|t| t.table().map(|t| t.state))
        else {
            // A dropped index.
            return Ok(());
        };
        if state == TableState::RUNNING as i32 {
            break;
        }
        if state != TableState::ALTERING as i32 && state != TableState::PREPARING as i32 {
            return Ok(());
        }
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
    persist(ctx, token, index_id, |pb| {
        if let Some(props) = pb.schema.as_mut().and_then(|s| s.table_properties.as_mut()) {
            props.set_retain_delete_markers(false);
        }
        if pb.index_info.is_some() {
            pb.index_info
                .mut_or_insert_default()
                .set_birth_time(read_time);
        }
    })
    .await?;
    let snapshot = ctx.lifecycle.catalog.snapshot();
    let mut calls = JoinSet::new();
    for tablet in snapshot.active_tablets_of(index_id) {
        let tablet_id = tablet.key.id.to_vec();
        let index_id = index_id.to_vec();
        let ctx = BackfillContext {
            lifecycle: ctx.lifecycle.clone(),
            rpc: ctx.rpc.clone(),
            executor: ctx.executor.clone(),
            rpc_timeout: ctx.rpc_timeout,
        };
        let token = *token;
        calls.spawn(async move {
            call_leader(
                &ctx,
                &token,
                &tablet_id,
                "BackfillDone",
                |leader| {
                    let mut req = ChangeMetadataRequestPB::new();
                    req.set_dest_uuid(leader.to_vec());
                    req.set_tablet_id(tablet_id.clone());
                    req.set_backfill_done_table_id(index_id.clone());
                    req.set_mark_backfill_done(true);
                    req.set_birth_time(read_time);
                    req.write_to_bytes().map_err(corrupt)
                },
                |bytes| {
                    let resp =
                        ChangeMetadataResponsePB::parse_from_bytes(bytes).map_err(corrupt)?;
                    Ok(match resp.error.as_ref() {
                        Some(e) => Err((ts_status(&e.status), permanent(e.code()))),
                        None => Ok(()),
                    })
                },
                RetryPolicy::default(),
            )
            .await
        });
    }
    while let Some(result) = calls.join_next().await {
        if let Err(status) = result.unwrap_or_else(|e| Err(corrupt(e))) {
            tracing::warn!("Telling an index tablet its backfill is done failed: {status}");
        }
    }
    Ok(())
}
