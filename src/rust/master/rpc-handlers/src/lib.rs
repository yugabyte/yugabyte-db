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

//! Master RPC handlers.
//!
//! Layer 1's master is a follower that never leads. It answers what a C++ follower answers:
//! `GetMasterRegistration`, `ListMasters`, `ListMasterRaftPeers`, `FlushSysCatalog` and
//! `CompactSysCatalog` are served; `TSHeartbeat` gets the follower reply (the master Raft config
//! when the tserver's is older); every other method gets `NOT_THE_LEADER`, which makes clients
//! rerun leader discovery. Responses mirror the C++ handlers in src/yb/master, field for field.

mod list_masters;

use std::sync::Arc;
use std::time::Duration;

use protobuf::{Enum, Message};
use yb_host_api::rpc::{InboundCall, RpcClient, RpcHandler};
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::ConsensusStatePB;
use yb_proto::pb::master_cluster::{
    GetMasterRegistrationResponsePB, ListMasterRaftPeersResponsePB,
};
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::master_types::{master_error_pb, MasterErrorPB};
use yb_proto::pb::wire_protocol::{
    app_status_pb, AppStatusPB, NodeInstancePB, ServerRegistrationPB,
};
use yb_proto::ServiceDesc;

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
}

impl MasterServiceHandler {
    /// Creates a handler for `service`. Blocking and asynchronous work runs on `runtime`.
    pub fn new(
        service: &'static ServiceDesc,
        info: Arc<dyn MasterInfo>,
        rpc: Arc<dyn RpcClient>,
        runtime: tokio::runtime::Handle,
        options: HandlerOptions,
    ) -> MasterServiceHandler {
        MasterServiceHandler {
            service,
            shared: Arc::new(Shared {
                info,
                rpc,
                runtime,
                options,
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
        // A leader that is not ready reports FOLLOWER so clients do not lock onto it. This master
        // is never ready to lead.
        let role = match self.info.raft_role() {
            PeerRole::LEADER => PeerRole::FOLLOWER,
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
        resp.set_leader_master(false);
        resp.error = Some(self.not_the_leader_error()).into();
        respond(call, &resp);
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

fn master_error(code: master_error_pb::Code, status: StatusCode, message: &str) -> MasterErrorPB {
    let mut error = MasterErrorPB::new();
    error.set_code(code);
    let mut app_status = AppStatusPB::new();
    // AppStatusPB::ErrorCode uses the same numbers as yb::Status::Code.
    app_status.set_code(
        app_status_pb::ErrorCode::from_i32(status as i32)
            .unwrap_or(app_status_pb::ErrorCode::UNKNOWN_ERROR),
    );
    app_status.set_message(message.to_string());
    error.status = Some(app_status).into();
    error
}

fn master_error_from(code: master_error_pb::Code, status: &Status) -> MasterErrorPB {
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
