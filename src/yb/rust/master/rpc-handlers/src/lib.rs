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

pub mod leader;
mod list_masters;

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
use yb_proto::pb::master_client::{
    GetTableLocationsRequestPB, GetTableLocationsResponsePB, GetTabletLocationsRequestPB,
    GetTabletLocationsResponsePB, GetTransactionStatusTabletsRequestPB,
    GetTransactionStatusTabletsResponsePB,
};
use yb_proto::pb::master_cluster::{
    ChangeLoadBalancerStateRequestPB, ChangeLoadBalancerStateResponsePB,
    ChangeMasterClusterConfigRequestPB, ChangeMasterClusterConfigResponsePB,
    GetLoadBalancerStateResponsePB, GetMasterClusterConfigRequestPB,
    GetMasterClusterConfigResponsePB, GetMasterRegistrationResponsePB,
    GetMasterXClusterConfigRequestPB, GetMasterXClusterConfigResponsePB,
    IsMasterLeaderReadyRequestPB, IsMasterLeaderReadyResponsePB, ListLiveTabletServersRequestPB,
    ListLiveTabletServersResponsePB, ListMasterRaftPeersResponsePB, ListTabletServersRequestPB,
    ListTabletServersResponsePB, RemoveTabletServerRequestPB, RemoveTabletServerResponsePB,
    SetPreferredZonesRequestPB, SetPreferredZonesResponsePB,
};
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_rust_peer::{
    dump_catalog_response_pb, DumpCatalogResponsePB, PushLocationsRequestPB,
};
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB};
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
        let (token, snapshot) = match self.leader_ready(&leader) {
            Ok(ready) => ready,
            Err(error) => {
                resp.set_leader_master(false);
                resp.error = Some(error).into();
                return respond(call, &resp);
            }
        };
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
        match method.name {
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
                    leader.get_tablet_locations(&snapshot, &req, &mut resp);
                    Ok(())
                }
            ),
            "GetTableLocations" => serve!(
                GetTableLocationsRequestPB,
                GetTableLocationsResponsePB,
                |req, resp| { leader.get_table_locations(&snapshot, &req, &mut resp) }
            ),
            "GetTransactionStatusTablets" => serve!(
                GetTransactionStatusTabletsRequestPB,
                GetTransactionStatusTabletsResponsePB,
                |_req, resp| { leader.get_transaction_status_tablets(&snapshot, &mut resp) }
            ),
            "GetAutoFlagsConfig" | "ValidateAutoFlagsConfig" => {
                let info = self.info.clone();
                let result = tokio::task::spawn_blocking(move || {
                    let result = info.handle_on_host(method.name, call.request());
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

fn status_of(error: &MasterErrorPB) -> Status {
    Status::new(
        StatusCode::from_i32(error.status.code().value()),
        error.status.message().to_string(),
    )
}

#[cfg(test)]
mod tests;
