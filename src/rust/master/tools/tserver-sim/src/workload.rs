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

//! Runs the fake tservers' heartbeats against a master and drives a DDL workload through it,
//! timing what the master does.

use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use protobuf::Message;
use tokio::task::JoinSet;
use yb_host_api::rpc::RpcClient;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common::{partition_schema_pb, ColumnSchemaPB, PartitionSchemaPB};
use yb_proto::pb::common_types::{PeerRole, TableType, YQLDatabase};
use yb_proto::pb::master_cluster::{
    GetMasterRegistrationRequestPB, GetMasterRegistrationResponsePB,
};
use yb_proto::pb::master_ddl::{
    CreateNamespaceRequestPB, CreateNamespaceResponsePB, CreateTableRequestPB,
    CreateTableResponsePB, IsCreateTableDoneRequestPB, IsCreateTableDoneResponsePB,
};
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB};
use yb_proto::pb::value::PersistentDataType;

use crate::service::SimCluster;
use crate::stats::Latencies;

const MASTER_SERVICE: &str = "yb.master.MasterService";

/// What to run.
#[derive(Clone, Debug)]
pub struct WorkloadConfig {
    /// RPC addresses of the masters.
    pub master_addresses: Vec<(String, u16)>,
    /// Time between a tserver's heartbeats, as `heartbeat_interval_ms`.
    pub heartbeat_interval: Duration,
    /// Timeout of each RPC.
    pub rpc_timeout: Duration,
    /// How long to measure heartbeats with no DDL running.
    pub steady_state: Duration,
    /// YCQL tables to create, one after another.
    pub num_tables: usize,
    /// Tablets per table.
    pub tablets_per_table: i32,
    /// Prefix of the keyspace name; the run appends a timestamp so runs do not collide.
    pub keyspace_prefix: String,
}

/// What a run measured.
#[derive(Debug)]
pub struct Report {
    /// Tservers simulated.
    pub num_tservers: usize,
    /// From the first heartbeat until every tserver was registered with its full report
    /// acknowledged.
    pub registration: Duration,
    /// Heartbeat round trips with no DDL running.
    pub steady_heartbeats: Latencies,
    /// Heartbeat round trips while tables were being created.
    pub ddl_heartbeats: Latencies,
    /// `CreateTable` until `IsCreateTableDone`, per table.
    pub create_table: Latencies,
    /// Tablet replicas hosted at the end.
    pub tablet_replicas: usize,
    /// Calls answered without modelling them, by `Service.Method`.
    pub unmodeled_calls: Vec<(String, u64)>,
}

impl Report {
    /// The report as a JSON object.
    pub fn to_json(&self) -> String {
        let unmodeled = self
            .unmodeled_calls
            .iter()
            .map(|(name, count)| format!("\"{name}\": {count}"))
            .collect::<Vec<_>>()
            .join(", ");
        format!(
            "{{\n  \"num_tservers\": {},\n  \"registration_ms\": {:.1},\n  \
             \"steady_heartbeat_us\": {},\n  \"ddl_heartbeat_us\": {},\n  \
             \"create_table_ms\": {},\n  \"tablet_replicas\": {},\n  \
             \"unmodeled_calls\": {{{}}}\n}}\n",
            self.num_tservers,
            self.registration.as_secs_f64() * 1e3,
            self.steady_heartbeats.to_json(1e6),
            self.ddl_heartbeats.to_json(1e6),
            self.create_table.to_json(1e3),
            self.tablet_replicas,
            unmodeled,
        )
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
enum Phase {
    Registering = 0,
    Steady = 1,
    Ddl = 2,
    Done = 3,
}

struct Shared {
    rpc: Arc<dyn RpcClient>,
    config: WorkloadConfig,
    leader: Mutex<Option<(String, u16)>>,
    phase: AtomicU8,
    steady: Mutex<Latencies>,
    ddl: Mutex<Latencies>,
}

fn status_of(error: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(protobuf::Enum::value(&error.status.code())),
        error.status.message().to_string(),
    )
}

fn corruption(e: protobuf::Error) -> Status {
    Status::new(StatusCode::Corruption, e.to_string())
}

impl Shared {
    fn phase(&self) -> Phase {
        match self.phase.load(Ordering::Acquire) {
            0 => Phase::Registering,
            1 => Phase::Steady,
            2 => Phase::Ddl,
            _ => Phase::Done,
        }
    }

    async fn find_leader(&self) -> Result<(String, u16), Status> {
        let request = GetMasterRegistrationRequestPB::new()
            .write_to_bytes()
            .map_err(corruption)?;
        for (host, port) in &self.config.master_addresses {
            let Ok(bytes) = self
                .rpc
                .call(
                    host,
                    *port,
                    MASTER_SERVICE,
                    "GetMasterRegistration",
                    &request,
                    self.config.rpc_timeout,
                )
                .await
            else {
                continue;
            };
            let resp =
                GetMasterRegistrationResponsePB::parse_from_bytes(&bytes).map_err(corruption)?;
            if resp.error.is_none() && resp.role() == PeerRole::LEADER {
                return Ok((host.clone(), *port));
            }
        }
        Err(Status::new(
            StatusCode::NotFound,
            "no master reports itself as leader",
        ))
    }

    async fn leader(&self) -> Result<(String, u16), Status> {
        if let Some(leader) = self.leader.lock().unwrap().clone() {
            return Ok(leader);
        }
        let leader = self.find_leader().await?;
        *self.leader.lock().unwrap() = Some(leader.clone());
        Ok(leader)
    }

    /// Calls a master leader method; a NOT_THE_LEADER answer forgets the leader and retries.
    async fn call_leader<Req: Message, Resp: Message>(
        &self,
        method: &str,
        req: &Req,
        error: fn(&Resp) -> Option<&MasterErrorPB>,
    ) -> Result<Resp, Status> {
        let request = req.write_to_bytes().map_err(corruption)?;
        loop {
            let (host, port) = self.leader().await?;
            let bytes = self
                .rpc
                .call(
                    &host,
                    port,
                    MASTER_SERVICE,
                    method,
                    &request,
                    self.config.rpc_timeout,
                )
                .await?;
            let resp = Resp::parse_from_bytes(&bytes).map_err(corruption)?;
            match error(&resp) {
                Some(e) if e.code() == master_error_pb::Code::NOT_THE_LEADER => {
                    *self.leader.lock().unwrap() = None;
                }
                Some(e) => return Err(status_of(e)),
                None => return Ok(resp),
            }
        }
    }

    async fn heartbeat_loop(self: Arc<Self>, cluster: Arc<SimCluster>, index: usize) {
        let ts = cluster.tservers()[index].clone();
        let interval = self.config.heartbeat_interval;
        let num_tservers = cluster.tservers().len().max(1) as u32;
        // Spread the first heartbeats over one interval, as tservers started at different times.
        tokio::time::sleep(interval * index as u32 / num_tservers).await;
        while self.phase() != Phase::Done {
            let (req, sent) = ts.lock().unwrap().heartbeat_request();
            let phase = self.phase();
            let start = Instant::now();
            // Errors are handled here rather than by call_leader: the tserver needs the response.
            let result = self
                .call_leader::<TSHeartbeatRequestPB, TSHeartbeatResponsePB>(
                    "TSHeartbeat",
                    &req,
                    |_| None,
                )
                .await;
            let elapsed = start.elapsed();
            let again_soon = match result {
                Ok(resp) => {
                    ts.lock().unwrap().handle_heartbeat_response(&sent, &resp);
                    match resp.error.as_ref() {
                        Some(e) => {
                            if e.code() == master_error_pb::Code::NOT_THE_LEADER {
                                *self.leader.lock().unwrap() = None;
                            } else {
                                tracing::warn!("Heartbeat failed: {}", status_of(e));
                            }
                            false
                        }
                        None => {
                            match phase {
                                Phase::Steady => self.steady.lock().unwrap().record(elapsed),
                                Phase::Ddl => self.ddl.lock().unwrap().record(elapsed),
                                _ => {}
                            }
                            !ts.lock().unwrap().is_registered()
                        }
                    }
                }
                Err(status) => {
                    tracing::warn!("Heartbeat failed: {status}");
                    *self.leader.lock().unwrap() = None;
                    false
                }
            };
            // A tserver that still has to register or report heartbeats again at once, as the C++
            // heartbeater does after a TryAgain.
            if !again_soon {
                tokio::time::sleep(interval).await;
            }
        }
    }

    async fn create_table(&self, keyspace: &str, index: usize) -> Result<Duration, Status> {
        let mut req = CreateTableRequestPB::new();
        req.set_name(format!("t{index}"));
        req.set_table_type(TableType::YQL_TABLE_TYPE);
        req.set_num_tablets(self.config.tablets_per_table);
        req.namespace
            .mut_or_insert_default()
            .set_name(keyspace.to_string());
        req.namespace
            .mut_or_insert_default()
            .set_database_type(YQLDatabase::YQL_DATABASE_CQL);
        let schema = req.schema.mut_or_insert_default();
        schema
            .columns
            .push(column("k", PersistentDataType::INT32, true));
        schema
            .columns
            .push(column("v", PersistentDataType::STRING, false));
        let mut partition = PartitionSchemaPB::new();
        partition.set_hash_schema(partition_schema_pb::HashSchema::MULTI_COLUMN_HASH_SCHEMA);
        req.partition_schema = Some(partition).into();

        let start = Instant::now();
        let resp: CreateTableResponsePB = self
            .call_leader("CreateTable", &req, |r: &CreateTableResponsePB| {
                r.error.as_ref()
            })
            .await?;
        let mut done = IsCreateTableDoneRequestPB::new();
        done.table
            .mut_or_insert_default()
            .set_table_id(resp.table_id().to_vec());
        loop {
            let resp: IsCreateTableDoneResponsePB = self
                .call_leader(
                    "IsCreateTableDone",
                    &done,
                    |r: &IsCreateTableDoneResponsePB| r.error.as_ref(),
                )
                .await?;
            if resp.done() {
                return Ok(start.elapsed());
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
    }
}

fn column(name: &str, data_type: PersistentDataType, hash_key: bool) -> ColumnSchemaPB {
    let mut column = ColumnSchemaPB::new();
    column.set_name(name.to_string());
    column.type_.mut_or_insert_default().set_main(data_type);
    column.set_is_key(hash_key);
    column.set_is_hash_key(hash_key);
    column.set_is_nullable(!hash_key);
    column
}

/// Runs the workload. The tservers' RPC services must already be serving.
pub async fn run(
    cluster: Arc<SimCluster>,
    rpc: Arc<dyn RpcClient>,
    config: WorkloadConfig,
) -> Result<Report, Status> {
    let shared = Arc::new(Shared {
        rpc,
        config: config.clone(),
        leader: Mutex::new(None),
        phase: AtomicU8::new(Phase::Registering as u8),
        steady: Mutex::default(),
        ddl: Mutex::default(),
    });
    let num_tservers = cluster.tservers().len();
    let start = Instant::now();
    let mut heartbeats = JoinSet::new();
    for index in 0..num_tservers {
        heartbeats.spawn(shared.clone().heartbeat_loop(cluster.clone(), index));
    }

    let result = async {
        while !cluster
            .tservers()
            .iter()
            .all(|ts| ts.lock().unwrap().is_registered())
        {
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
        let registration = start.elapsed();
        tracing::info!("{num_tservers} tservers registered in {registration:?}");

        shared.phase.store(Phase::Steady as u8, Ordering::Release);
        tokio::time::sleep(config.steady_state).await;

        shared.phase.store(Phase::Ddl as u8, Ordering::Release);
        let keyspace = format!(
            "{}_{}",
            config.keyspace_prefix,
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .map(|d| d.as_secs())
                .unwrap_or_default()
        );
        let mut ns = CreateNamespaceRequestPB::new();
        ns.set_name(keyspace.clone());
        ns.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
        shared
            .call_leader("CreateNamespace", &ns, |r: &CreateNamespaceResponsePB| {
                r.error.as_ref()
            })
            .await?;
        let mut create_table = Latencies::default();
        for index in 0..config.num_tables {
            create_table.record(shared.create_table(&keyspace, index).await?);
        }
        Ok((registration, create_table))
    }
    .await;

    shared.phase.store(Phase::Done as u8, Ordering::Release);
    heartbeats.shutdown().await;
    let (registration, create_table) = result?;

    let steady_heartbeats = std::mem::take(&mut *shared.steady.lock().unwrap());
    let ddl_heartbeats = std::mem::take(&mut *shared.ddl.lock().unwrap());
    Ok(Report {
        num_tservers,
        registration,
        steady_heartbeats,
        ddl_heartbeats,
        create_table,
        tablet_replicas: cluster
            .tservers()
            .iter()
            .map(|ts| ts.lock().unwrap().num_tablets())
            .sum(),
        unmodeled_calls: cluster.unmodeled_calls().into_iter().collect(),
    })
}
