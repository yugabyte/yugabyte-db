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

//! `WaitForYsqlBackendsCatalogVersion` (C++'s `YsqlBackendsManager`): how many PG backends in the
//! cluster still run on a catalog version of a database older than a given one. A DDL that must
//! not proceed while older backends exist (an online CREATE INDEX between its steps) asks until
//! the answer is 0.
//!
//! One job per (database, version) asks each tserver with a live YSQL lease, again while it has
//! lagging backends; callers wait on the job until the client's deadline less a margin, and get
//! the total, or -1 while some tserver has not answered. A version found reached is remembered
//! for the term.

use std::collections::{BTreeMap, HashMap};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Duration;

use master_catalog_state::Catalog;
use master_leadership::{LeaderToken, Leadership};
use master_table_lifecycle::SchemaHost;
use master_task_runtime::{Attempt, RetryPolicy, TsRpcExecutor};
use master_ts_manager::TsManager;
use protobuf::{Enum, Message};
use tokio::sync::watch;
use tokio::time::Instant;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::SysObjectLockEntryPB;
use yb_proto::pb::master_admin::WaitForYsqlBackendsCatalogVersionRequestPB;
use yb_proto::pb::master_client::{GetYsqlCatalogConfigRequestPB, GetYsqlCatalogConfigResponsePB};
use yb_proto::pb::master_types::SysRowEntryType;
use yb_proto::pb::tserver_admin::{
    WaitForYsqlBackendsCatalogVersionRequestPB as TsRequestPB,
    WaitForYsqlBackendsCatalogVersionResponsePB as TsResponsePB,
};
use yb_proto::pb::tserver_types::tablet_server_error_pb;

/// Flags, read when used.
#[derive(Clone, Copy, Debug)]
pub struct BackendsOptions {
    /// `wait_for_ysql_backends_catalog_version_master_tserver_rpc_timeout_ms`.
    pub tserver_rpc_timeout: fn() -> Duration,
    /// `wait_for_ysql_backends_catalog_version_client_master_rpc_margin_ms`.
    pub client_margin: fn() -> Duration,
}

impl Default for BackendsOptions {
    fn default() -> BackendsOptions {
        BackendsOptions {
            tserver_rpc_timeout: || Duration::from_secs(60),
            client_margin: || Duration::from_secs(5),
        }
    }
}

fn corrupt(e: impl std::fmt::Display) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

/// `GetPgsqlNamespaceId`.
pub fn pg_namespace_id(database_oid: u32) -> Vec<u8> {
    format!("{database_oid:08x}000030008000000000000000").into_bytes()
}

#[derive(Clone, Debug, PartialEq)]
enum JobState {
    Running,
    Complete,
    Failed(String),
}

struct Job {
    /// Lagging backends per tserver; -1 until it answers.
    lagging: Mutex<BTreeMap<Vec<u8>, i32>>,
    state: watch::Sender<JobState>,
}

impl Job {
    fn total(&self) -> i32 {
        let lagging = self.lagging.lock().unwrap();
        if lagging.is_empty() || lagging.values().any(|n| *n < 0) {
            return -1;
        }
        lagging.values().sum()
    }
}

#[derive(Default)]
struct Jobs {
    term: i64,
    /// The newest version known reached, per database.
    reached: HashMap<u32, u64>,
    running: HashMap<(u32, u64), Arc<Job>>,
}

/// The backends manager of one master.
pub struct YsqlBackends {
    catalog: Arc<Catalog>,
    leadership: Arc<Leadership>,
    ts_manager: Arc<TsManager>,
    host: Arc<dyn SchemaHost>,
    executor: Arc<TsRpcExecutor>,
    rpc: OnceLock<Arc<dyn RpcClient>>,
    options: BackendsOptions,
    jobs: Mutex<Jobs>,
}

impl YsqlBackends {
    /// The backends manager of this master.
    pub fn new(
        catalog: Arc<Catalog>,
        leadership: Arc<Leadership>,
        ts_manager: Arc<TsManager>,
        host: Arc<dyn SchemaHost>,
        executor: Arc<TsRpcExecutor>,
        options: BackendsOptions,
    ) -> Arc<YsqlBackends> {
        Arc::new(YsqlBackends {
            catalog,
            leadership,
            ts_manager,
            host,
            executor,
            rpc: OnceLock::new(),
            options,
            jobs: Mutex::default(),
        })
    }

    /// The RPC client tservers are called with; set once, before any leader term.
    pub fn set_rpc(&self, rpc: Arc<dyn RpcClient>) {
        let _ = self.rpc.set(rpc);
    }

    /// The database's catalog version, as the host reads it from `pg_yb_catalog_version`.
    fn master_version(&self, database_oid: u32) -> Result<u64, Status> {
        let mut req = GetYsqlCatalogConfigRequestPB::new();
        req.namespace
            .mut_or_insert_default()
            .set_id(pg_namespace_id(database_oid));
        let bytes = self.host.call(
            "GetYsqlCatalogConfig",
            &req.write_to_bytes().map_err(corrupt)?,
        )?;
        let resp = GetYsqlCatalogConfigResponsePB::parse_from_bytes(&bytes).map_err(corrupt)?;
        if let Some(error) = resp.error.as_ref() {
            return Err(Status::new(
                StatusCode::from_i32(error.status.code().value()),
                error.status.message().to_string(),
            ));
        }
        Ok(resp.version())
    }

    /// `WaitForYsqlBackendsCatalogVersion`: the number of lagging backends, or -1 if not known
    /// by the deadline.
    pub async fn wait(
        self: &Arc<Self>,
        token: &LeaderToken,
        req: &WaitForYsqlBackendsCatalogVersionRequestPB,
        deadline: Instant,
    ) -> Result<i32, Status> {
        let db = req.database_oid();
        let version = req.catalog_version();
        let master_version = {
            let this = self.clone();
            tokio::task::spawn_blocking(move || this.master_version(db))
                .await
                .map_err(corrupt)??
        };
        if master_version < version {
            return Err(Status::new(
                StatusCode::InvalidArgument,
                format!(
                    "Requested catalog version is too high: req version {version}, master \
                     version {master_version}"
                ),
            ));
        }
        let job = {
            let mut jobs = self.jobs.lock().unwrap();
            if jobs.term != token.term() {
                *jobs = Jobs {
                    term: token.term(),
                    ..Jobs::default()
                };
            }
            if jobs.reached.get(&db).is_some_and(|v| *v >= version) {
                return Ok(0);
            }
            match jobs.running.get(&(db, version)) {
                Some(job) => job.clone(),
                None => {
                    let job = Arc::new(Job {
                        lagging: Mutex::default(),
                        state: watch::channel(JobState::Running).0,
                    });
                    jobs.running.insert((db, version), job.clone());
                    self.launch(token, &job, req);
                    job
                }
            }
        };
        let wait_until = deadline
            .checked_sub((self.options.client_margin)())
            .unwrap_or(deadline);
        let mut state = job.state.subscribe();
        let _ =
            tokio::time::timeout_at(wait_until, state.wait_for(|s| *s != JobState::Running)).await;
        let current = state.borrow().clone();
        match current {
            JobState::Complete => Ok(0),
            JobState::Failed(message) => {
                self.jobs.lock().unwrap().running.remove(&(db, version));
                Err(Status::new(
                    StatusCode::RuntimeError,
                    format!("job is failed, statuses: {message}"),
                ))
            }
            JobState::Running => Ok(job.total()),
        }
    }

    /// `BackendsCatalogVersionJob::Launch`: one task per tserver with a live YSQL lease.
    fn launch(
        self: &Arc<Self>,
        token: &LeaderToken,
        job: &Arc<Job>,
        req: &WaitForYsqlBackendsCatalogVersionRequestPB,
    ) {
        let live = live_leases(&self.catalog);
        if live.is_empty() {
            let _ = job.state.send(JobState::Complete);
            self.reached(req.database_oid(), req.catalog_version());
            return;
        }
        {
            let mut lagging = job.lagging.lock().unwrap();
            for uuid in &live {
                lagging.insert(uuid.clone(), -1);
            }
        }
        for uuid in live {
            let this = self.clone();
            let job = job.clone();
            let token = *token;
            let req = req.clone();
            tokio::spawn(async move {
                let result = this.ask_tserver(&token, &job, &uuid, &req).await;
                match result {
                    Ok(()) => {
                        if job.total() == 0
                            && job.state.send_if_modified(|s| {
                                let running = *s == JobState::Running;
                                if running {
                                    *s = JobState::Complete;
                                }
                                running
                            })
                        {
                            this.reached(req.database_oid(), req.catalog_version());
                        }
                    }
                    Err(status) => {
                        tracing::warn!(
                            "Waiting for the backends of tserver {} failed: {status}",
                            String::from_utf8_lossy(&uuid)
                        );
                        job.state.send_if_modified(|s| {
                            let running = *s == JobState::Running;
                            if running {
                                *s = JobState::Failed(status.to_string());
                            }
                            running
                        });
                    }
                }
            });
        }
    }

    fn reached(&self, db: u32, version: u64) {
        let mut jobs = self.jobs.lock().unwrap();
        let reached = jobs.reached.entry(db).or_default();
        *reached = (*reached).max(version);
        jobs.running.remove(&(db, version));
    }

    /// `BackendsCatalogVersionTS`: asks again while the tserver has lagging backends. A tserver
    /// whose lease ends counts as caught up: its backends are gone.
    async fn ask_tserver(
        &self,
        token: &LeaderToken,
        job: &Job,
        uuid: &[u8],
        req: &WaitForYsqlBackendsCatalogVersionRequestPB,
    ) -> Result<(), Status> {
        let rpc = self
            .rpc
            .get()
            .cloned()
            .ok_or_else(|| Status::new(StatusCode::ServiceUnavailable, "No RPC client yet"))?;
        let mut prev = -1;
        loop {
            self.leadership.check(token)?;
            if !live_leases(&self.catalog).contains(&uuid.to_vec()) {
                job.lagging.lock().unwrap().insert(uuid.to_vec(), 0);
                return Ok(());
            }
            let mut ts_req = TsRequestPB::new();
            ts_req.set_database_oid(req.database_oid());
            ts_req.set_catalog_version(req.catalog_version());
            if req.requestor_ts_uuid() == uuid {
                ts_req.set_requestor_pg_backend_pid(req.requestor_pg_backend_pid());
            }
            ts_req.set_prev_num_lagging_backends(prev);
            let bytes = ts_req.write_to_bytes().map_err(corrupt)?;
            let timeout = (self.options.tserver_rpc_timeout)();
            let address = self.ts_manager.rpc_address(uuid);
            let lagging = self
                .executor
                .run(
                    uuid,
                    "WaitForYsqlBackendsCatalogVersion",
                    &RetryPolicy::default(),
                    |_| {
                        let (rpc, bytes, address) = (rpc.clone(), bytes.clone(), address.clone());
                        async move {
                            let (host, port) = address.ok_or_else(|| {
                                Attempt::Retry(Status::new(
                                    StatusCode::NotFound,
                                    "tserver is not registered",
                                ))
                            })?;
                            let resp = rpc
                                .call(
                                    &host,
                                    port,
                                    "yb.tserver.TabletServerAdminService",
                                    "WaitForYsqlBackendsCatalogVersion",
                                    &bytes,
                                    timeout,
                                )
                                .await
                                .map_err(Attempt::Retry)?;
                            let resp = TsResponsePB::parse_from_bytes(&resp)
                                .map_err(|e| Attempt::Retry(corrupt(e)))?;
                            match resp.error.as_ref() {
                                None => Ok(resp.num_lagging_backends()),
                                Some(e) => {
                                    let status = Status::new(
                                        StatusCode::from_i32(e.status.code().value()),
                                        e.status.message().to_string(),
                                    );
                                    if e.code()
                                        == tablet_server_error_pb::Code::OPERATION_NOT_SUPPORTED
                                        || status.message.contains("sorry, too many clients")
                                    {
                                        Err(Attempt::Fail(status))
                                    } else {
                                        Err(Attempt::Retry(status))
                                    }
                                }
                            }
                        }
                    },
                )
                .await?;
            job.lagging.lock().unwrap().insert(uuid.to_vec(), lagging);
            if lagging <= 0 {
                return Ok(());
            }
            prev = lagging;
        }
    }
}

/// The tservers with a live YSQL lease.
fn live_leases(catalog: &Catalog) -> Vec<Vec<u8>> {
    catalog
        .snapshot()
        .iter(SysRowEntryType::OBJECT_LOCK_ENTRY)
        .filter(|row| {
            row.parse::<SysObjectLockEntryPB>()
                .is_ok_and(|e| e.lease_info.as_ref().is_some_and(|l| l.live_lease()))
        })
        .map(|row| row.key.id.to_vec())
        .collect()
}
