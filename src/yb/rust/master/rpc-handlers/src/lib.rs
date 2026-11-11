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

//! Master RPC handlers. Responses mirror the C++ handlers in src/yb/master, field for field.
//!
//! On every master: `GetMasterRegistration`, `ListMasters`, `ListMasterRaftPeers`,
//! `FlushSysCatalog`, `CompactSysCatalog`, the load balancer switch, and [`HOST_METHODS`], which
//! the C++ host serves with the C++ master's code. `TSHeartbeat` returns the master Raft config
//! when the tserver's is older.
//!
//! On a ready Rust leader (`TEST_rust_master_allow_leadership`), the core methods in
//! `MasterServiceHandler::leader_call` are served from the catalog; other leader methods answer
//! `NotSupported` naming the workstream that serves them. Elsewhere every leader method gets
//! `NOT_THE_LEADER`, which makes clients rerun leader discovery.
//!
//! Not served yet, though C++ serves them on every master: `DumpState` (yb-admin
//! dump_masters_state), and the test-only `TestRetry` and `AccessYsqlBackendsManagerTestRegister`.

mod balancer;
pub mod leader;
mod list_masters;
mod vtables;

pub use leader::LeaderServices;
pub use list_masters::list_masters_response;

use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Duration;

use protobuf::{Enum, Message};
use yb_host_api::rpc::{InboundCall, RpcClient, RpcHandler};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::ConsensusStatePB;
use yb_proto::pb::master_admin::{
    DeleteNotServingTabletRequestPB, DeleteNotServingTabletResponsePB,
    DisableTabletSplittingRequestPB, DisableTabletSplittingResponsePB, IsInitDbDoneRequestPB,
    IsInitDbDoneResponsePB, IsTabletSplittingCompleteRequestPB,
    IsTabletSplittingCompleteResponsePB, SplitTabletRequestPB, SplitTabletResponsePB,
};
use yb_proto::pb::master_admin::{
    WaitForYsqlBackendsCatalogVersionRequestPB, WaitForYsqlBackendsCatalogVersionResponsePB,
};
use yb_proto::pb::master_client::{
    GetTableLocationsRequestPB, GetTableLocationsResponsePB, GetTabletLocationsRequestPB,
    GetTabletLocationsResponsePB, GetTransactionStatusTabletsRequestPB,
    GetTransactionStatusTabletsResponsePB, GetYsqlCatalogConfigRequestPB,
    GetYsqlCatalogConfigResponsePB, ReservePgsqlOidsRequestPB, ReservePgsqlOidsResponsePB,
};
use yb_proto::pb::master_cluster::{
    AreLeadersOnPreferredOnlyRequestPB, AreLeadersOnPreferredOnlyResponsePB,
    ChangeLoadBalancerStateRequestPB, ChangeLoadBalancerStateResponsePB,
    ChangeMasterClusterConfigRequestPB, ChangeMasterClusterConfigResponsePB,
    GetLoadBalancerStateResponsePB, GetLoadMovePercentRequestPB, GetLoadMovePercentResponsePB,
    GetMasterClusterConfigRequestPB, GetMasterClusterConfigResponsePB,
    GetMasterRegistrationResponsePB, GetMasterXClusterConfigRequestPB,
    GetMasterXClusterConfigResponsePB, IsLoadBalancedRequestPB, IsLoadBalancedResponsePB,
    IsLoadBalancerIdleRequestPB, IsLoadBalancerIdleResponsePB, IsMasterLeaderReadyRequestPB,
    IsMasterLeaderReadyResponsePB, ListLiveTabletServersRequestPB, ListLiveTabletServersResponsePB,
    ListMasterRaftPeersResponsePB, ListTabletServersRequestPB, ListTabletServersResponsePB,
    RemoveTabletServerRequestPB, RemoveTabletServerResponsePB, SetPreferredZonesRequestPB,
    SetPreferredZonesResponsePB,
};
use yb_proto::pb::master_ddl::{
    AcquireObjectLocksGlobalRequestPB, AcquireObjectLocksGlobalResponsePB, AlterNamespaceRequestPB,
    AlterNamespaceResponsePB, AlterTableRequestPB, AlterTableResponsePB, BackfillIndexRequestPB,
    BackfillIndexResponsePB, CreateNamespaceRequestPB, CreateNamespaceResponsePB,
    CreateTableRequestPB, CreateTableResponsePB, DeleteNamespaceRequestPB,
    DeleteNamespaceResponsePB, DeleteTableRequestPB, DeleteTableResponsePB,
    GetBackfillStatusRequestPB, GetBackfillStatusResponsePB, GetNamespaceInfoRequestPB,
    GetNamespaceInfoResponsePB, GetTableSchemaRequestPB, GetTableSchemaResponsePB,
    IsAlterTableDoneRequestPB, IsAlterTableDoneResponsePB, IsCreateNamespaceDoneRequestPB,
    IsCreateNamespaceDoneResponsePB, IsCreateTableDoneRequestPB, IsCreateTableDoneResponsePB,
    IsDeleteNamespaceDoneRequestPB, IsDeleteNamespaceDoneResponsePB, IsDeleteTableDoneRequestPB,
    IsDeleteTableDoneResponsePB, IsTruncateTableDoneRequestPB, IsTruncateTableDoneResponsePB,
    IsYsqlDdlVerificationDoneRequestPB, IsYsqlDdlVerificationDoneResponsePB,
    ListNamespacesRequestPB, ListNamespacesResponsePB, ListTablesRequestPB, ListTablesResponsePB,
    ReleaseObjectLocksGlobalRequestPB, ReleaseObjectLocksGlobalResponsePB,
    ReportYsqlDdlTxnStatusRequestPB, ReportYsqlDdlTxnStatusResponsePB, TruncateTableRequestPB,
    TruncateTableResponsePB, WaitForLockersMultipleGlobalRequestPB,
    WaitForLockersMultipleGlobalResponsePB,
};
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_replication::{
    IsNamespacePartOfCDCSDKRequestPB, IsNamespacePartOfCDCSDKResponsePB,
};
use yb_proto::pb::master_rust_peer::{
    dump_catalog_response_pb, DumpCatalogResponsePB, PushLocationsRequestPB,
};
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB, NamespaceIdentifierPB};
use yb_proto::pb::master_ysql_lease::{
    RefreshYsqlLeaseRequestPB, RefreshYsqlLeaseResponsePB, RelinquishYsqlLeaseRequestPB,
    RelinquishYsqlLeaseResponsePB,
};
use yb_proto::pb::wire_protocol::{NodeInstancePB, ServerRegistrationPB};
use yb_proto::{MethodDesc, ServiceDesc};

/// What the handlers need from the master's host.
pub trait MasterInfo: Send + Sync + 'static {
    /// This master's node instance.
    fn instance(&self) -> NodeInstancePB;
    /// This master's RPC and HTTP addresses.
    fn registration(&self) -> Result<ServerRegistrationPB, Status>;
    /// This master's Raft role in the sys-catalog tablet.
    fn raft_role(&self) -> PeerRole;
    /// The committed Raft state of the sys-catalog tablet.
    fn committed_consensus_state(&self) -> Result<ConsensusStatePB, Status>;
    /// Whether the sys catalog has been opened, or shell mode entered.
    fn is_initialized(&self) -> bool;
    /// Whether this master waits, without a sys catalog, to be added to a quorum.
    fn is_shell_mode(&self) -> bool;
    /// Flushes the sys-catalog tablet; returns a serialized `FlushSysCatalogResponsePB`. Blocks.
    fn flush_sys_catalog(&self) -> Vec<u8>;
    /// Compacts the sys-catalog tablet; returns a serialized `CompactSysCatalogResponsePB`. Blocks.
    fn compact_sys_catalog(&self) -> Vec<u8>;
    /// Serves one of [`HOST_METHODS`] in C++: the serialized response to `request`. Blocks.
    fn handle_on_host(&self, method: &str, request: &[u8]) -> Result<Vec<u8>, Status>;
}

/// Settings, read when used so runtime flag changes take effect.
#[derive(Clone, Copy, Debug)]
pub struct HandlerOptions {
    /// `master_rpc_timeout_ms`: per-peer timeout of the `GetMasterRegistration` fan-out.
    pub master_rpc_timeout: fn() -> Duration,
    /// `master_list_raft_peers_check_is_leader`.
    pub list_raft_peers_check_is_leader: fn() -> bool,
}

/// Handles every method of one master service.
pub struct MasterServiceHandler {
    service: &'static ServiceDesc,
    shared: Arc<Shared>,
}

struct Shared {
    info: Arc<dyn MasterInfo>,
    rpc: Arc<dyn RpcClient>,
    runtime: tokio::runtime::Handle,
    options: HandlerOptions,
    leader: Option<Arc<LeaderServices>>,
}

impl MasterServiceHandler {
    /// Creates a handler for `service`. Blocking and asynchronous work runs on `runtime`.
    /// `leader` serves the leader methods; without it every leader method is `NOT_THE_LEADER`.
    pub fn new(
        service: &'static ServiceDesc,
        info: Arc<dyn MasterInfo>,
        rpc: Arc<dyn RpcClient>,
        runtime: tokio::runtime::Handle,
        options: HandlerOptions,
        leader: Option<Arc<LeaderServices>>,
    ) -> MasterServiceHandler {
        MasterServiceHandler {
            service,
            shared: Arc::new(Shared {
                info,
                rpc,
                runtime,
                options,
                leader,
            }),
        }
    }
}

impl RpcHandler for MasterServiceHandler {
    fn handle(&self, call: Box<dyn InboundCall>) {
        let Some(method) = self.service.methods.get(call.method_index()) else {
            call.respond_error(Status::new(
                StatusCode::InvalidArgument,
                "method index out of range",
            ));
            return;
        };
        // The host's service pool drops calls that expire in its queue; this catches the ones
        // that expired between that check and here.
        if call.time_to_deadline().is_zero() {
            return call.respond_error(expired_call());
        }
        let shared = self.shared.clone();
        match method.name {
            "GetMasterRegistration" => {
                let response = shared.get_master_registration();
                respond(call, &response);
            }
            "ListMasterRaftPeers" => {
                let response = shared.list_master_raft_peers();
                respond(call, &response);
            }
            "TSHeartbeat" => shared.ts_heartbeat(call),
            "ListMasters" => {
                let runtime = shared.runtime.clone();
                runtime.spawn(async move {
                    let response = list_masters::list_masters(&shared).await;
                    respond(call, &response);
                });
            }
            "FlushSysCatalog" | "CompactSysCatalog" => {
                let flush = method.name == "FlushSysCatalog";
                let runtime = shared.runtime.clone();
                runtime.spawn_blocking(move || {
                    if let Some(error) = shared.initialization_error() {
                        return respond_with_error(call, method.error_field, error);
                    }
                    let bytes = if flush {
                        shared.info.flush_sys_catalog()
                    } else {
                        shared.info.compact_sys_catalog()
                    };
                    call.respond(&bytes);
                });
            }
            name if host_method(name).is_some() => {
                let checks_initialized = host_method(name) == Some(true);
                let runtime = shared.runtime.clone();
                runtime.spawn_blocking(move || {
                    if checks_initialized {
                        if let Some(error) = shared.initialization_error() {
                            return respond_with_error(call, method.error_field, error);
                        }
                    }
                    match shared.info.handle_on_host(method.name, call.request()) {
                        Ok(bytes) => call.respond(&bytes),
                        Err(status) => call.respond_error(status),
                    }
                });
            }
            "ChangeLoadBalancerState" | "GetLoadBalancerState" if shared.leader.is_some() => {
                shared.load_balancer_state(call, method.name == "ChangeLoadBalancerState");
            }
            "PushLocations" if shared.leader.is_some() => shared.push_locations(call),
            "DumpCatalog" if shared.leader.is_some() => shared.dump_catalog(call),
            _ if shared.leader.is_some() => {
                let runtime = shared.runtime.clone();
                runtime.spawn(async move { shared.leader_call(call, method).await });
            }
            _ => {
                let error = shared.not_the_leader_error();
                if method.response_has_required_fields {
                    // Such a response cannot carry just `error`; fail the call instead, which
                    // clients also treat as "find the leader and retry".
                    call.respond_error(status_of(&error));
                } else {
                    respond_with_error(call, method.error_field, error);
                }
            }
        }
    }
}

/// Methods every C++ master serves, not only the leader, that the host answers with the C++
/// master's code, and whether C++ first checks that the catalog is initialized
/// (`CheckIsInitializedOrRespond`; the others are `MASTER_SERVICE_IMPL_ON_ALL_MASTERS`).
pub const HOST_METHODS: &[(&str, bool)] = &[
    ("RemovedMasterUpdate", true),
    ("DumpSysCatalogEntries", true),
    ("WriteSysCatalogEntry", true),
    ("CheckMasterTabletHealth", false),
    ("GetUniverseKeyRegistry", false),
    ("AddUniverseKeys", false),
    ("HasUniverseKeyInMemory", false),
];

/// The answer to a call whose deadline passed before it was handled: the caller has given up, so
/// it is not served late.
fn expired_call() -> Status {
    Status::new(
        StatusCode::TimedOut,
        "The call's deadline passed before the master handled it",
    )
}

fn host_method(name: &str) -> Option<bool> {
    HOST_METHODS
        .iter()
        .find(|(m, _)| *m == name)
        .map(|(_, checks)| *checks)
}

impl Shared {
    /// The error a C++ master returns when its catalog is not initialized yet.
    fn initialization_error(&self) -> Option<MasterErrorPB> {
        (!self.info.is_initialized()).then(|| {
            master_error(
                master_error_pb::Code::CATALOG_MANAGER_NOT_INITIALIZED,
                StatusCode::ServiceUnavailable,
                "Catalog manager is not initialized",
            )
        })
    }

    /// What `ScopedLeaderSharedLock` reports on a non-leader.
    fn not_the_leader_error(&self) -> MasterErrorPB {
        if let Some(error) = self.initialization_error() {
            return error;
        }
        let message = if self.info.is_shell_mode() {
            format!(
                "Catalog manager of {} is in shell mode, not the leader.",
                String::from_utf8_lossy(self.info.instance().permanent_uuid())
            )
        } else {
            "Not the leader".to_string()
        };
        master_error(
            master_error_pb::Code::NOT_THE_LEADER,
            StatusCode::IllegalState,
            &message,
        )
    }

    fn get_master_registration(&self) -> GetMasterRegistrationResponsePB {
        let mut resp = GetMasterRegistrationResponsePB::new();
        resp.instance_id = Some(self.info.instance()).into();
        if let Some(error) = self.initialization_error() {
            resp.error = Some(error).into();
            return resp;
        }
        match self.info.registration() {
            Ok(registration) => resp.registration = Some(registration).into(),
            Err(status) => {
                resp.error = Some(master_error_from(
                    master_error_pb::Code::UNKNOWN_ERROR,
                    &status,
                ))
                .into()
            }
        }
        // A leader that is not ready reports FOLLOWER so clients do not lock onto it.
        let ready = self.leader.as_ref().is_some_and(|l| l.ready().is_ok());
        let role = match self.info.raft_role() {
            PeerRole::LEADER if !ready => PeerRole::FOLLOWER,
            role => role,
        };
        resp.set_role(role);
        resp
    }

    fn list_master_raft_peers(&self) -> ListMasterRaftPeersResponsePB {
        let mut resp = ListMasterRaftPeersResponsePB::new();
        if (self.options.list_raft_peers_check_is_leader)() {
            resp.error = Some(self.not_the_leader_error()).into();
            return resp;
        }
        match self.info.committed_consensus_state() {
            Ok(state) if state.config.is_some() => {
                resp.masters = state.config.peers.clone();
            }
            Ok(_) => {
                resp.error = Some(master_error(
                    master_error_pb::Code::UNKNOWN_ERROR,
                    StatusCode::NotFound,
                    "No raft config found.",
                ))
                .into()
            }
            Err(status) => {
                resp.error = Some(master_error_from(
                    master_error_pb::Code::UNKNOWN_ERROR,
                    &status,
                ))
                .into()
            }
        }
        resp
    }

    /// The follower half of `MasterHeartbeatServiceImpl::TSHeartbeat`.
    fn ts_heartbeat(&self, call: Box<dyn InboundCall>) {
        let req = match TSHeartbeatRequestPB::parse_from_bytes(call.request()) {
            Ok(req) => req,
            Err(e) => {
                call.respond_error(Status::new(StatusCode::InvalidArgument, e.to_string()));
                return;
            }
        };
        let mut resp = TSHeartbeatResponsePB::new();
        if let Ok(state) = self.info.committed_consensus_state() {
            if let Some(config) = state.config.as_ref() {
                if config.committed_op_index() > req.config_index() {
                    resp.master_config = Some(config.clone()).into();
                }
            }
        }
        if req.common.ts_instance.permanent_uuid().is_empty() {
            // Same message as the C++ master, misspelling included.
            call.respond_error(Status::new(
                StatusCode::InvalidArgument,
                format!(
                    "Recevied Empty UUID from instance: {}",
                    protobuf::text_format::print_to_string(&*req.common.ts_instance)
                ),
            ));
            return;
        }
        let Some(leader) = self.leader.clone() else {
            resp.set_leader_master(false);
            resp.error = Some(self.not_the_leader_error()).into();
            return respond(call, &resp);
        };
        if let Err(error) = self.leader_ready(&leader) {
            resp.set_leader_master(false);
            resp.error = Some(error).into();
            return respond(call, &resp);
        }
        let master_config = resp.master_config.take();
        let instance = self.info.instance();
        // Taken here, on the RPC thread, so heartbeats of one tserver are processed in the order
        // they arrived (K6).
        let ticket = leader
            .ts_manager
            .heartbeat_ticket(req.common.ts_instance.permanent_uuid());
        self.runtime.clone().spawn(async move {
            if let Err(status) = ticket.wait(call.time_to_deadline()).await {
                return call.respond_error(status);
            }
            // The turn can come after the deadline, which the wait does not check when the turn
            // is already free.
            if call.time_to_deadline().is_zero() {
                return call.respond_error(expired_call());
            }
            // Read once the turn comes, so the catalog includes what the heartbeats before this
            // one wrote, and the token is that of the current leadership.
            let (token, snapshot) = match leader.ready() {
                Ok(ready) => ready,
                Err(error) => {
                    let mut resp = TSHeartbeatResponsePB::new();
                    resp.master_config = master_config.into();
                    resp.set_leader_master(false);
                    resp.error = Some(error).into();
                    return respond(call, &resp);
                }
            };
            let ctx = master_ts_manager::HeartbeatContext {
                token,
                writer: &leader.writer,
                snapshot,
                contributors: &leader.contributors,
                master_instance: instance,
                time_to_deadline: call.time_to_deadline(),
            };
            match leader.ts_manager.heartbeat(ctx, &req).await {
                Ok(mut resp) => {
                    resp.master_config = master_config.into();
                    // The leadership may have moved while the report was processed.
                    if let Err(status) = leader.leadership.check(&token) {
                        resp.set_leader_master(false);
                        resp.error = Some(master_error_from(
                            master_error_pb::Code::NOT_THE_LEADER,
                            &status,
                        ))
                        .into();
                    }
                    respond(call, &resp);
                }
                Err(status) => call.respond_error(status),
            }
            drop(ticket);
        });
    }

    /// The readiness gate on a master with leader services: `ScopedLeaderSharedLock`.
    fn leader_ready(
        &self,
        leader: &LeaderServices,
    ) -> Result<
        (
            master_leadership::LeaderToken,
            Arc<master_catalog_state::CatalogSnapshot>,
        ),
        MasterErrorPB,
    > {
        if let Some(error) = self.initialization_error() {
            return Err(error);
        }
        if self.info.is_shell_mode() {
            return Err(self.not_the_leader_error());
        }
        leader.ready()
    }

    /// `ChangeLoadBalancerState` and `GetLoadBalancerState`: in memory on every master.
    fn load_balancer_state(&self, call: Box<dyn InboundCall>, change: bool) {
        let leader = self.leader.as_ref().expect("leader services");
        if change {
            match ChangeLoadBalancerStateRequestPB::parse_from_bytes(call.request()) {
                Ok(req) => {
                    if req.has_is_enabled() {
                        leader
                            .load_balancer_enabled
                            .store(req.is_enabled(), Ordering::Relaxed);
                    }
                    respond(call, &ChangeLoadBalancerStateResponsePB::new());
                }
                Err(e) => {
                    call.respond_error(Status::new(StatusCode::InvalidArgument, e.to_string()))
                }
            }
        } else {
            let mut resp = GetLoadBalancerStateResponsePB::new();
            resp.set_is_enabled(leader.load_balancer_enabled.load(Ordering::Relaxed));
            respond(call, &resp);
        }
    }

    /// `MasterRustPeer.PushLocations`, on a follower.
    fn push_locations(&self, call: Box<dyn InboundCall>) {
        let leader = self.leader.as_ref().expect("leader services");
        match PushLocationsRequestPB::parse_from_bytes(call.request()) {
            Ok(req) => {
                let raft = leader.leadership.raft_state();
                let leading = raft.role == PeerRole::LEADER
                    || leader.leadership.phase() != master_leadership::Phase::Follower;
                match master_ts_manager::apply_push(
                    &leader.ts_manager,
                    &leader.follower_stream,
                    &req,
                    leading,
                    raft.term,
                ) {
                    Ok(resp) => respond(call, &resp),
                    Err(status) => call.respond_error(status),
                }
            }
            Err(e) => call.respond_error(Status::new(StatusCode::InvalidArgument, e.to_string())),
        }
    }

    /// `MasterRustPeer.DumpCatalog`: the in-memory catalog, for tests.
    fn dump_catalog(&self, call: Box<dyn InboundCall>) {
        let leader = self.leader.as_ref().expect("leader services");
        let snapshot = leader.catalog.snapshot();
        let mut resp = DumpCatalogResponsePB::new();
        if !snapshot.loaded {
            resp.error = Some(master_error_from(
                master_error_pb::Code::CATALOG_MANAGER_NOT_INITIALIZED,
                &master_catalog_state::not_loaded_status(),
            ))
            .into();
            return respond(call, &resp);
        }
        for (entry_type, id, data) in snapshot.rows() {
            let mut row = dump_catalog_response_pb::Row::new();
            row.entry_type = Some(protobuf::EnumOrUnknown::from_i32(entry_type));
            row.set_id(id.to_vec());
            row.set_data(data.to_vec());
            resp.rows.push(row);
        }
        resp.set_as_of(snapshot.as_of.0);
        resp.set_generation(snapshot.generation);
        let ts_manager = &leader.ts_manager;
        let locations = ts_manager.locations.all();
        resp.set_num_tablet_locations(locations.len() as u64);
        resp.set_num_tablet_replicas(locations.iter().map(|(_, r)| r.replicas.len() as u64).sum());
        resp.set_num_live_tservers(ts_manager.live().len() as u64);
        resp.set_num_tservers(ts_manager.all().len() as u64);
        resp.set_live_tservers_unchanged_responses(
            ts_manager.unchanged_responses.load(Ordering::Relaxed),
        );
        resp.set_self_check_mismatches(
            leader
                .catalog
                .stats
                .self_check_mismatches
                .load(Ordering::Relaxed),
        );
        respond(call, &resp);
    }

    /// Answers `call` with what the host's C++ code returns for `request`.
    async fn serve_on_host(
        &self,
        call: Box<dyn InboundCall>,
        method: &'static MethodDesc,
        request: Vec<u8>,
    ) {
        let info = self.info.clone();
        let result = tokio::task::spawn_blocking(move || {
            let result = info.handle_on_host(method.name, &request);
            (call, result)
        })
        .await;
        if let Ok((call, result)) = result {
            match result {
                Ok(bytes) => call.respond(&bytes),
                Err(status) => call.respond_error(status),
            }
        }
    }

    /// A leader method on a master with leader services.
    async fn leader_call(self: Arc<Self>, call: Box<dyn InboundCall>, method: &'static MethodDesc) {
        // It may have waited for a runtime worker.
        if call.time_to_deadline().is_zero() {
            return call.respond_error(expired_call());
        }
        let leader = self.leader.clone().expect("leader services");
        let (token, snapshot) = match self.leader_ready(&leader) {
            Ok(ready) => ready,
            Err(error) => {
                if method.name == "ListTabletServers" {
                    // C++ sets master_uuid before the leader check.
                    let mut resp = ListTabletServersResponsePB::new();
                    resp.set_master_uuid(
                        String::from_utf8_lossy(self.info.instance().permanent_uuid()).into(),
                    );
                    resp.error = Some(error).into();
                    return respond(call, &resp);
                }
                return respond_or_fail(call, method, error);
            }
        };
        macro_rules! serve {
            ($req_ty:ty, $resp_ty:ty, |$req:ident, $resp:ident| $body:expr) => {{
                match <$req_ty>::parse_from_bytes(call.request()) {
                    Err(e) => {
                        call.respond_error(Status::new(StatusCode::InvalidArgument, e.to_string()))
                    }
                    Ok($req) => {
                        let mut $resp = <$resp_ty>::new();
                        let result: Result<(), Status> = $body;
                        if let Err(status) = result {
                            if $resp.error.is_none() {
                                $resp.error = Some(master_error_from(
                                    master_error_pb::Code::UNKNOWN_ERROR,
                                    &status,
                                ))
                                .into();
                            }
                        }
                        respond(call, &$resp);
                    }
                }
            }};
        }
        // A DDL answer: the handler's MasterErrorPB goes in `error` as is.
        macro_rules! serve_ddl {
            ($req_ty:ty, $resp_ty:ty, |$req:ident, $resp:ident| $body:expr) => {{
                match <$req_ty>::parse_from_bytes(call.request()) {
                    Err(e) => {
                        call.respond_error(Status::new(StatusCode::InvalidArgument, e.to_string()))
                    }
                    Ok($req) => {
                        let mut $resp = <$resp_ty>::new();
                        let result: Result<(), MasterErrorPB> = $body;
                        if let Err(error) = result {
                            $resp.error = Some(error).into();
                        }
                        respond(call, &$resp);
                    }
                }
            }};
        }
        let tables = leader.tables.clone();
        let deadline = tokio::time::Instant::now() + call.time_to_deadline();
        match method.name {
            "CreateNamespace" => serve_ddl!(
                CreateNamespaceRequestPB,
                CreateNamespaceResponsePB,
                |req, resp| tables
                    .create_namespace(&token, deadline, &req, &mut resp)
                    .await
            ),
            "AlterNamespace" => serve_ddl!(
                AlterNamespaceRequestPB,
                AlterNamespaceResponsePB,
                |req, resp| {
                    let _ = &mut resp;
                    tables.alter_namespace(&token, deadline, &req).await
                }
            ),
            "IsCreateNamespaceDone" => serve_ddl!(
                IsCreateNamespaceDoneRequestPB,
                IsCreateNamespaceDoneResponsePB,
                |req, resp| tables.is_create_namespace_done(&snapshot, &req, &mut resp)
            ),
            "DeleteNamespace" => serve_ddl!(
                DeleteNamespaceRequestPB,
                DeleteNamespaceResponsePB,
                |req, _resp| tables.delete_namespace(&token, deadline, &req).await
            ),
            "IsDeleteNamespaceDone" => serve_ddl!(
                IsDeleteNamespaceDoneRequestPB,
                IsDeleteNamespaceDoneResponsePB,
                |req, resp| tables.is_delete_namespace_done(&snapshot, &req, &mut resp)
            ),
            "ListNamespaces" => serve_ddl!(
                ListNamespacesRequestPB,
                ListNamespacesResponsePB,
                |req, resp| {
                    tables.list_namespaces(&snapshot, &req, &mut resp);
                    Ok(())
                }
            ),
            "GetNamespaceInfo" => serve_ddl!(
                GetNamespaceInfoRequestPB,
                GetNamespaceInfoResponsePB,
                |req, resp| tables.get_namespace_info(&snapshot, &req, &mut resp)
            ),
            "ListTables" => serve_ddl!(ListTablesRequestPB, ListTablesResponsePB, |req, resp| {
                tables.list_tables(&snapshot, &req, &mut resp)
            }),
            "GetTableSchema" => serve_ddl!(
                GetTableSchemaRequestPB,
                GetTableSchemaResponsePB,
                |req, resp| tables.get_table_schema(&snapshot, &req, &mut resp)
            ),
            "CreateTable" => {
                serve_ddl!(CreateTableRequestPB, CreateTableResponsePB, |req, resp| {
                    tables.create_table(&token, deadline, &req, &mut resp).await
                })
            }
            "AlterTable" => serve_ddl!(AlterTableRequestPB, AlterTableResponsePB, |req, resp| {
                tables.alter_table(&token, deadline, &req, &mut resp).await
            }),
            "IsAlterTableDone" => serve_ddl!(
                IsAlterTableDoneRequestPB,
                IsAlterTableDoneResponsePB,
                |req, resp| tables.is_alter_table_done(&snapshot, &req, &mut resp)
            ),
            "DeleteTable" => {
                serve_ddl!(DeleteTableRequestPB, DeleteTableResponsePB, |req, resp| {
                    tables.delete_table(&token, deadline, &req, &mut resp).await
                })
            }
            "IsDeleteTableDone" => serve_ddl!(
                IsDeleteTableDoneRequestPB,
                IsDeleteTableDoneResponsePB,
                |req, resp| tables.is_delete_table_done(&snapshot, &req, &mut resp)
            ),
            "IsCreateTableDone" => serve_ddl!(
                IsCreateTableDoneRequestPB,
                IsCreateTableDoneResponsePB,
                |req, resp| tables.is_create_table_done(&snapshot, &req, &mut resp)
            ),
            "ListTabletServers" => serve!(
                ListTabletServersRequestPB,
                ListTabletServersResponsePB,
                |req, resp| {
                    resp.set_master_uuid(
                        String::from_utf8_lossy(self.info.instance().permanent_uuid()).into(),
                    );
                    leader.list_tablet_servers(&snapshot, &req, &mut resp)
                }
            ),
            "ListLiveTabletServers" => serve!(
                ListLiveTabletServersRequestPB,
                ListLiveTabletServersResponsePB,
                |_req, resp| { leader.list_live_tablet_servers(&snapshot, &mut resp) }
            ),
            "RemoveTabletServer" => serve!(
                RemoveTabletServerRequestPB,
                RemoveTabletServerResponsePB,
                |req, resp| {
                    leader
                        .remove_tablet_server(&token, &snapshot, &req, &mut resp)
                        .await
                }
            ),
            "IsMasterLeaderServiceReady" => serve!(
                IsMasterLeaderReadyRequestPB,
                IsMasterLeaderReadyResponsePB,
                |_req, _resp| Ok(())
            ),
            "IsLoadBalancerIdle" => serve!(
                IsLoadBalancerIdleRequestPB,
                IsLoadBalancerIdleResponsePB,
                |_req, resp| {
                    leader.is_load_balancer_idle(&mut resp);
                    Ok(())
                }
            ),
            "IsLoadBalanced" => serve!(
                IsLoadBalancedRequestPB,
                IsLoadBalancedResponsePB,
                |req, resp| {
                    leader.is_load_balanced(&req, &mut resp);
                    Ok(())
                }
            ),
            "AreLeadersOnPreferredOnly" => serve!(
                AreLeadersOnPreferredOnlyRequestPB,
                AreLeadersOnPreferredOnlyResponsePB,
                |_req, resp| {
                    leader.are_leaders_on_preferred_only(&snapshot, &mut resp);
                    Ok(())
                }
            ),
            "GetLoadMoveCompletion" | "GetLeaderBlacklistCompletion" => serve!(
                GetLoadMovePercentRequestPB,
                GetLoadMovePercentResponsePB,
                |_req, resp| {
                    leader
                        .load_move_completion(
                            &snapshot,
                            method.name == "GetLeaderBlacklistCompletion",
                            (leader.blacklist_progress_initial_delay)(),
                            deadline,
                            &mut resp,
                        )
                        .await;
                    Ok(())
                }
            ),
            "GetMasterClusterConfig" => serve!(
                GetMasterClusterConfigRequestPB,
                GetMasterClusterConfigResponsePB,
                |_req, resp| { leader.get_cluster_config(&snapshot, &mut resp) }
            ),
            "GetMasterXClusterConfig" => serve!(
                GetMasterXClusterConfigRequestPB,
                GetMasterXClusterConfigResponsePB,
                |_req, resp| { leader.get_xcluster_config(&snapshot, &mut resp) }
            ),
            "ChangeMasterClusterConfig" => serve!(
                ChangeMasterClusterConfigRequestPB,
                ChangeMasterClusterConfigResponsePB,
                |req, resp| { leader.change_cluster_config(&token, &req, &mut resp).await }
            ),
            "SetPreferredZones" => serve!(
                SetPreferredZonesRequestPB,
                SetPreferredZonesResponsePB,
                |req, resp| { leader.set_preferred_zones(&token, &req, &mut resp).await }
            ),
            "GetTabletLocations" => serve!(
                GetTabletLocationsRequestPB,
                GetTabletLocationsResponsePB,
                |req, resp| {
                    let masters = self.info.committed_consensus_state().ok();
                    leader.get_tablet_locations(&snapshot, masters.as_ref(), &req, &mut resp);
                    Ok(())
                }
            ),
            "GetTableLocations" => serve!(
                GetTableLocationsRequestPB,
                GetTableLocationsResponsePB,
                |req, resp| {
                    let masters = self.info.committed_consensus_state().ok();
                    leader.get_table_locations(&snapshot, masters.as_ref(), &req, &mut resp)
                }
            ),
            "GetTransactionStatusTablets" => serve!(
                GetTransactionStatusTabletsRequestPB,
                GetTransactionStatusTabletsResponsePB,
                |_req, resp| { leader.get_transaction_status_tablets(&snapshot, &mut resp) }
            ),
            "SplitTablet" => {
                serve_ddl!(SplitTabletRequestPB, SplitTabletResponsePB, |req, resp| {
                    let _ = &mut resp;
                    leader.splits.split_tablet(token, &req)
                })
            }
            "DeleteNotServingTablet" => serve_ddl!(
                DeleteNotServingTabletRequestPB,
                DeleteNotServingTabletResponsePB,
                |req, resp| {
                    let _ = &mut resp;
                    leader
                        .splits
                        .delete_not_serving_tablet(token, deadline, &req)
                        .await
                }
            ),
            "RefreshYsqlLease" => match RefreshYsqlLeaseRequestPB::parse_from_bytes(call.request())
            {
                Err(e) => {
                    call.respond_error(Status::new(StatusCode::InvalidArgument, e.to_string()))
                }
                Ok(req) => {
                    let mut resp = RefreshYsqlLeaseResponsePB::new();
                    match leader.leases.refresh(&token, &req).await {
                        Ok(info) => resp.set_info(info),
                        Err(status) => resp.set_error(master_error_from(
                            master_error_pb::Code::UNKNOWN_ERROR,
                            &status,
                        )),
                    }
                    respond(call, &resp);
                }
            },
            "RelinquishYsqlLease" => serve!(
                RelinquishYsqlLeaseRequestPB,
                RelinquishYsqlLeaseResponsePB,
                |req, resp| {
                    let _ = &mut resp;
                    leader.leases.relinquish(&token, &req).await
                }
            ),
            "AcquireObjectLocksGlobal" => serve!(
                AcquireObjectLocksGlobalRequestPB,
                AcquireObjectLocksGlobalResponsePB,
                |req, resp| {
                    let result = leader.locks.acquire_global(&token, &req, deadline).await;
                    lock_error(result, &mut resp.error)
                }
            ),
            "ReleaseObjectLocksGlobal" => serve!(
                ReleaseObjectLocksGlobalRequestPB,
                ReleaseObjectLocksGlobalResponsePB,
                |req, resp| {
                    let result = leader.locks.release_global(&token, &req, deadline).await;
                    lock_error(result, &mut resp.error)
                }
            ),
            "WaitForLockersMultipleGlobal" => serve!(
                WaitForLockersMultipleGlobalRequestPB,
                WaitForLockersMultipleGlobalResponsePB,
                |req, resp| {
                    let result = leader
                        .locks
                        .wait_for_lockers_global(&token, &req, deadline)
                        .await;
                    lock_error(result, &mut resp.error)
                }
            ),
            "IsInitDbDone" => serve!(
                IsInitDbDoneRequestPB,
                IsInitDbDoneResponsePB,
                |_req, resp| {
                    master_ysql_manager::initdb::is_initdb_done(&snapshot, &mut resp);
                    Ok(())
                }
            ),
            "DisableTabletSplitting" => serve!(
                DisableTabletSplittingRequestPB,
                DisableTabletSplittingResponsePB,
                |req, resp| {
                    let _ = &mut resp;
                    leader.splits.disable_splitting_for(
                        std::time::Duration::from_millis(req.disable_duration_ms()),
                        req.feature_name(),
                    );
                    Ok(())
                }
            ),
            "IsTabletSplittingComplete" => serve!(
                IsTabletSplittingCompleteRequestPB,
                IsTabletSplittingCompleteResponsePB,
                |req, resp| {
                    resp.set_is_tablet_splitting_complete(
                        leader
                            .splits
                            .is_tablet_splitting_complete(req.wait_for_parent_deletion()),
                    );
                    Ok(())
                }
            ),
            "IsNamespacePartOfCDCSDK" => serve!(
                IsNamespacePartOfCDCSDKRequestPB,
                IsNamespacePartOfCDCSDKResponsePB,
                |req, resp| {
                    if req.namespace_id().is_empty() {
                        Err(Status::new(
                            StatusCode::InvalidArgument,
                            "namespace_id must not be empty",
                        ))
                    } else {
                        resp.set_is_namespace_part_of_cdcsdk(
                            LeaderServices::namespace_has_cdc_stream(
                                &snapshot,
                                req.namespace_id().as_bytes(),
                            ),
                        );
                        Ok(())
                    }
                }
            ),
            "TruncateTable" => serve_ddl!(
                TruncateTableRequestPB,
                TruncateTableResponsePB,
                |req, resp| leader.truncates.truncate_table(&token, &req, &mut resp)
            ),
            "IsTruncateTableDone" => serve_ddl!(
                IsTruncateTableDoneRequestPB,
                IsTruncateTableDoneResponsePB,
                |req, resp| leader
                    .truncates
                    .is_truncate_table_done(&snapshot, &req, &mut resp)
            ),
            "GetBackfillStatus" => serve_ddl!(
                GetBackfillStatusRequestPB,
                GetBackfillStatusResponsePB,
                |req, resp| tables.get_backfill_status(&snapshot, &req, &mut resp)
            ),
            "WaitForYsqlBackendsCatalogVersion" => serve!(
                WaitForYsqlBackendsCatalogVersionRequestPB,
                WaitForYsqlBackendsCatalogVersionResponsePB,
                |req, resp| {
                    let lagging = leader.backends.wait(&token, &req, deadline).await;
                    lagging.map(|n| resp.set_num_lagging_backends(n))
                }
            ),
            "BackfillIndex" => serve_ddl!(
                BackfillIndexRequestPB,
                BackfillIndexResponsePB,
                |req, resp| {
                    tables
                        .backfill_index(&token, deadline, &req, &mut resp)
                        .await
                }
            ),
            "ReportYsqlDdlTxnStatus" => serve_ddl!(
                ReportYsqlDdlTxnStatusRequestPB,
                ReportYsqlDdlTxnStatusResponsePB,
                |req, resp| {
                    let _ = &mut resp;
                    tables
                        .report_ysql_ddl_txn_status(&token, deadline, &req)
                        .await
                }
            ),
            "IsYsqlDdlVerificationDone" => serve_ddl!(
                IsYsqlDdlVerificationDoneRequestPB,
                IsYsqlDdlVerificationDoneResponsePB,
                |req, resp| tables.is_ysql_ddl_verification_done(&snapshot, &req, &mut resp)
            ),
            "ReservePgsqlOids" => serve_ddl!(
                ReservePgsqlOidsRequestPB,
                ReservePgsqlOidsResponsePB,
                |req, resp| tables
                    .reserve_pgsql_oids(&token, deadline, &req, &mut resp)
                    .await
            ),
            // The host reads the database's version from pg_yb_catalog_version; it gets the
            // request with the namespace resolved to its id, as `FindNamespace` resolves it.
            "GetYsqlCatalogConfig" => {
                let request = GetYsqlCatalogConfigRequestPB::parse_from_bytes(call.request())
                    .map_err(|e| Status::new(StatusCode::InvalidArgument, e.to_string()))
                    .and_then(|mut req| {
                        if req.namespace.is_none() {
                            return Ok(req);
                        }
                        let namespace = master_table_lifecycle::lookup::find_namespace(
                            &snapshot,
                            &req.namespace,
                        )
                        .map_err(|e| status_of(&e))?;
                        let mut resolved = NamespaceIdentifierPB::new();
                        resolved.set_id(namespace.key.id.to_vec());
                        req.namespace = Some(resolved).into();
                        Ok(req)
                    })
                    .and_then(|req| {
                        req.write_to_bytes()
                            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))
                    });
                match request {
                    Ok(request) => self.serve_on_host(call, method, request).await,
                    Err(status) => {
                        let mut resp = GetYsqlCatalogConfigResponsePB::new();
                        resp.error = Some(master_error_from(
                            master_error_pb::Code::NAMESPACE_NOT_FOUND,
                            &status,
                        ))
                        .into();
                        respond(call, &resp)
                    }
                }
            }
            "GetAutoFlagsConfig" | "ValidateAutoFlagsConfig" | "GetYsqlYbSystemTableInfo" => {
                let request = call.request().to_vec();
                self.serve_on_host(call, method, request).await
            }
            // CatalogManager answers these from the cluster config's encryption info with its
            // EncryptionManager, so the host gets that instead of the (empty) request.
            "GetFullUniverseKeyRegistry" | "IsEncryptionEnabled" => {
                let encryption_info =
                    LeaderServices::cluster_config(&snapshot).and_then(|config| {
                        config
                            .encryption_info
                            .as_ref()
                            .map(|info| info.write_to_bytes())
                            .transpose()
                            .map_err(|e| Status::new(StatusCode::InternalError, e.to_string()))
                    });
                match encryption_info {
                    Ok(info) => {
                        self.serve_on_host(call, method, info.unwrap_or_default())
                            .await
                    }
                    Err(status) => respond_or_fail(
                        call,
                        method,
                        master_error_from(master_error_pb::Code::UNKNOWN_ERROR, &status),
                    ),
                }
            }
            "TSHeartbeat" => unreachable!("handled above"),
            other => {
                let status = Status::new(
                    StatusCode::NotSupported,
                    format!(
                        "{other} is not served by a layer-2 Rust master leader; it belongs to {}",
                        leader::owner(other)
                    ),
                );
                respond_or_fail(
                    call,
                    method,
                    master_error_from(master_error_pb::Code::UNKNOWN_ERROR, &status),
                )
            }
        }
    }
}

/// `FillErrorIfRequired` of the object lock RPCs: a lost leadership is NOT_THE_LEADER, so the
/// tserver retries on the new leader.
fn lock_error(
    result: Result<(), Status>,
    error: &mut protobuf::MessageField<MasterErrorPB>,
) -> Result<(), Status> {
    if let Err(status) = &result {
        let code = if status.code == StatusCode::IllegalState
            && status.message == "Master is not the leader"
        {
            master_error_pb::Code::NOT_THE_LEADER
        } else {
            master_error_pb::Code::UNKNOWN_ERROR
        };
        *error = Some(master_error_from(code, status)).into();
    }
    result
}

/// Answers with only `error`, or fails the call when the response has required fields.
fn respond_or_fail(call: Box<dyn InboundCall>, method: &MethodDesc, error: MasterErrorPB) {
    if method.response_has_required_fields {
        call.respond_error(status_of(&error));
    } else {
        respond_with_error(call, method.error_field, error);
    }
}

fn respond(call: Box<dyn InboundCall>, message: &dyn protobuf::MessageDyn) {
    match message.write_to_bytes_dyn() {
        Ok(bytes) => call.respond(&bytes),
        Err(e) => call.respond_error(Status::new(StatusCode::InternalError, e.to_string())),
    }
}

/// Answers with a response whose only field is `error`, without knowing the response type.
fn respond_with_error(
    call: Box<dyn InboundCall>,
    error_field: Option<(i32, &'static str)>,
    error: MasterErrorPB,
) {
    match error_field {
        Some((number, "yb.master.MasterErrorPB")) => {
            let mut bytes = Vec::new();
            let mut os = protobuf::CodedOutputStream::vec(&mut bytes);
            let written = os
                .write_message(number as u32, &error)
                .and_then(|()| os.flush());
            drop(os);
            match written {
                Ok(()) => call.respond(&bytes),
                Err(e) => call.respond_error(Status::new(StatusCode::InternalError, e.to_string())),
            }
        }
        _ => call.respond_error(status_of(&error)),
    }
}

pub(crate) use master_ts_manager::master_error;

pub(crate) fn master_error_from(code: master_error_pb::Code, status: &Status) -> MasterErrorPB {
    master_error(code, status.code, &status.message)
}

pub(crate) fn status_of(error: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(error.status.code().value()),
        error.status.message().to_string(),
    )
}

#[cfg(test)]
mod tests;
