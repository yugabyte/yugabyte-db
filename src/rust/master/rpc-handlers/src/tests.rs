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

use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use protobuf::{Enum, Message};
use yb_host_api::rpc::{FakeInboundCall, FakeResponse, FakeRpcClient};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::consensus_metadata::{RaftConfigPB, RaftPeerPB};
use yb_proto::pb::master_cluster::{ListMastersResponsePB, RemovedMasterUpdateResponsePB};
use yb_proto::pb::master_ddl::CreateTableResponsePB;
use yb_proto::pb::master_heartbeat::TSToMasterCommonPB;
use yb_proto::{find_method, SERVICES};

use super::*;

struct FakeInfo {
    initialized: AtomicBool,
    shell_mode: AtomicBool,
    role: AtomicI32,
    config: Mutex<Option<RaftConfigPB>>,
    host_calls: Mutex<Vec<String>>,
}

impl FakeInfo {
    fn new() -> Arc<FakeInfo> {
        Arc::new(FakeInfo {
            initialized: AtomicBool::new(true),
            shell_mode: AtomicBool::new(false),
            role: AtomicI32::new(PeerRole::FOLLOWER as i32),
            config: Mutex::new(Some(config(&[("peer-a", 7100), ("peer-b", 7101)], 12))),
            host_calls: Mutex::new(vec![]),
        })
    }
}

fn config(peers: &[(&str, i32)], committed_op_index: i64) -> RaftConfigPB {
    let mut config = RaftConfigPB::new();
    config.set_committed_op_index(committed_op_index);
    for (uuid, port) in peers {
        let mut peer = RaftPeerPB::new();
        peer.set_permanent_uuid(uuid.as_bytes().to_vec());
        let mut hp = HostPortPB::new();
        hp.set_host("127.0.0.81".into());
        hp.set_port(*port as u32);
        peer.last_known_private_addr.push(hp);
        config.peers.push(peer);
    }
    config
}

impl MasterInfo for FakeInfo {
    fn instance(&self) -> NodeInstancePB {
        let mut instance = NodeInstancePB::new();
        instance.set_permanent_uuid(b"self-uuid".to_vec());
        instance.set_instance_seqno(42);
        instance
    }

    fn registration(&self) -> Result<ServerRegistrationPB, Status> {
        Ok(ServerRegistrationPB::new())
    }

    fn raft_role(&self) -> PeerRole {
        PeerRole::from_i32(self.role.load(Ordering::SeqCst)).unwrap()
    }

    fn committed_consensus_state(&self) -> Result<ConsensusStatePB, Status> {
        let mut state = ConsensusStatePB::new();
        state.set_current_term(3);
        state.config = self.config.lock().unwrap().clone().into();
        Ok(state)
    }

    fn is_initialized(&self) -> bool {
        self.initialized.load(Ordering::SeqCst)
    }

    fn is_shell_mode(&self) -> bool {
        self.shell_mode.load(Ordering::SeqCst)
    }

    fn flush_sys_catalog(&self) -> Vec<u8> {
        vec![]
    }

    fn compact_sys_catalog(&self) -> Vec<u8> {
        vec![]
    }

    fn handle_on_host(&self, method: &str, _request: &[u8]) -> Result<Vec<u8>, Status> {
        self.host_calls.lock().unwrap().push(method.to_string());
        Ok(b"host".to_vec())
    }
}

struct Harness {
    runtime: tokio::runtime::Runtime,
    info: Arc<FakeInfo>,
    rpc: Arc<FakeRpcClient>,
}

impl Harness {
    fn new() -> Harness {
        Harness::with_rpc(FakeRpcClient::new(|_, _, _, _, _| {
            Err(Status::new(StatusCode::NetworkError, "unreachable"))
        }))
    }

    fn with_rpc(rpc: FakeRpcClient) -> Harness {
        Harness {
            runtime: tokio::runtime::Builder::new_multi_thread()
                .worker_threads(2)
                .enable_all()
                .build()
                .unwrap(),
            info: FakeInfo::new(),
            rpc: Arc::new(rpc),
        }
    }

    /// Calls `method` and waits for the answer.
    fn call(&self, method: &str, request: Vec<u8>) -> FakeResponse {
        let (service, index, _) = find_method("yb.master.MasterService", method)
            .or_else(|| find_method("yb.master.MasterBackup", method))
            .unwrap();
        let handler = MasterServiceHandler::new(
            service,
            self.info.clone(),
            self.rpc.clone(),
            self.runtime.handle().clone(),
            HandlerOptions {
                master_rpc_timeout: || Duration::from_secs(1),
                list_raft_peers_check_is_leader: || false,
            },
        );
        let (call, outcome) = FakeInboundCall::new(index, request);
        handler.handle(call);
        for _ in 0..500 {
            if let Some(response) = outcome.lock().unwrap().clone() {
                return response;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
        panic!("{method} was not answered");
    }
}

fn success(response: FakeResponse) -> Vec<u8> {
    match response {
        FakeResponse::Success(bytes) => bytes,
        other => panic!("expected a response, got {other:?}"),
    }
}

#[test]
fn registration_reports_follower_even_when_leader() {
    let h = Harness::new();
    h.info.role.store(PeerRole::LEADER as i32, Ordering::SeqCst);
    let resp = GetMasterRegistrationResponsePB::parse_from_bytes(&success(
        h.call("GetMasterRegistration", vec![]),
    ))
    .unwrap();
    assert_eq!(resp.role(), PeerRole::FOLLOWER);
    assert_eq!(resp.instance_id.instance_seqno(), 42);
    assert!(resp.error.is_none());
}

#[test]
fn registration_before_initialization() {
    let h = Harness::new();
    h.info.initialized.store(false, Ordering::SeqCst);
    let resp = GetMasterRegistrationResponsePB::parse_from_bytes(&success(
        h.call("GetMasterRegistration", vec![]),
    ))
    .unwrap();
    // instance_id is required and always set.
    assert_eq!(resp.instance_id.permanent_uuid(), b"self-uuid");
    assert_eq!(
        resp.error.code(),
        master_error_pb::Code::CATALOG_MANAGER_NOT_INITIALIZED
    );
}

#[test]
fn host_methods_are_served_by_the_host() {
    let h = Harness::new();
    for (method, _) in HOST_METHODS {
        assert_eq!(
            h.call(method, vec![]),
            FakeResponse::Success(b"host".to_vec())
        );
    }
    assert_eq!(
        *h.info.host_calls.lock().unwrap(),
        HOST_METHODS
            .iter()
            .map(|(m, _)| m.to_string())
            .collect::<Vec<_>>()
    );

    // Only the CheckIsInitializedOrRespond ones wait for the catalog.
    h.info.host_calls.lock().unwrap().clear();
    h.info.initialized.store(false, Ordering::SeqCst);
    let resp = RemovedMasterUpdateResponsePB::parse_from_bytes(&success(
        h.call("RemovedMasterUpdate", vec![]),
    ))
    .unwrap();
    assert_eq!(
        resp.error.code(),
        master_error_pb::Code::CATALOG_MANAGER_NOT_INITIALIZED
    );
    assert_eq!(
        h.call("CheckMasterTabletHealth", vec![]),
        FakeResponse::Success(b"host".to_vec())
    );
    assert_eq!(
        *h.info.host_calls.lock().unwrap(),
        vec!["CheckMasterTabletHealth".to_string()]
    );
}

#[test]
fn leader_only_methods_answer_not_the_leader() {
    let h = Harness::new();
    let resp =
        CreateTableResponsePB::parse_from_bytes(&success(h.call("CreateTable", vec![]))).unwrap();
    assert_eq!(resp.error.code(), master_error_pb::Code::NOT_THE_LEADER);
    assert_eq!(
        resp.error.status.code(),
        app_status_pb::ErrorCode::ILLEGAL_STATE
    );
    assert_eq!(resp.error.status.message(), "Not the leader");

    h.info.shell_mode.store(true, Ordering::SeqCst);
    let resp =
        CreateTableResponsePB::parse_from_bytes(&success(h.call("CreateTable", vec![]))).unwrap();
    assert!(resp.error.status.message().contains("shell mode"));
}

#[test]
fn every_method_gets_an_answer() {
    let h = Harness::new();
    for service in SERVICES
        .iter()
        .filter(|s| s.proto_name.starts_with("yb.master."))
    {
        for method in service.methods {
            match h.call(method.name, vec![]) {
                FakeResponse::Success(_) | FakeResponse::Error(_) => {}
                FakeResponse::Dropped => panic!("{} was dropped", method.name),
            }
        }
    }
}

#[test]
fn heartbeat_follower_reply() {
    let h = Harness::new();
    let mut req = TSHeartbeatRequestPB::new();
    let mut common = TSToMasterCommonPB::new();
    common
        .ts_instance
        .mut_or_insert_default()
        .set_permanent_uuid(b"ts-1".to_vec());
    common
        .ts_instance
        .mut_or_insert_default()
        .set_instance_seqno(1);
    req.common = Some(common).into();
    req.set_config_index(5);
    let resp = TSHeartbeatResponsePB::parse_from_bytes(&success(
        h.call("TSHeartbeat", req.write_to_bytes().unwrap()),
    ))
    .unwrap();
    assert!(!resp.leader_master());
    assert_eq!(resp.error.code(), master_error_pb::Code::NOT_THE_LEADER);
    // The tserver's config (index 5) is older than the committed one (12).
    assert_eq!(resp.master_config.committed_op_index(), 12);

    req.set_config_index(12);
    let resp = TSHeartbeatResponsePB::parse_from_bytes(&success(
        h.call("TSHeartbeat", req.write_to_bytes().unwrap()),
    ))
    .unwrap();
    assert!(resp.master_config.is_none());

    req.common
        .mut_or_insert_default()
        .ts_instance
        .mut_or_insert_default()
        .set_permanent_uuid(vec![]);
    assert!(matches!(
        h.call("TSHeartbeat", req.write_to_bytes().unwrap()),
        FakeResponse::Error(_)
    ));
}

#[test]
fn list_raft_peers() {
    let h = Harness::new();
    let resp = ListMasterRaftPeersResponsePB::parse_from_bytes(&success(
        h.call("ListMasterRaftPeers", vec![]),
    ))
    .unwrap();
    assert_eq!(resp.masters.len(), 2);
    assert!(resp.error.is_none());
}

#[test]
fn list_masters_mixes_answers_and_errors() {
    let h = Harness::with_rpc(FakeRpcClient::new(|_, port, service, method, _| {
        assert_eq!(
            (service, method),
            ("yb.master.MasterService", "GetMasterRegistration")
        );
        if port != 7100 {
            return Err(Status::new(StatusCode::NetworkError, "connection refused"));
        }
        let mut resp = GetMasterRegistrationResponsePB::new();
        resp.instance_id
            .mut_or_insert_default()
            .set_permanent_uuid(b"peer-a".to_vec());
        resp.instance_id
            .mut_or_insert_default()
            .set_instance_seqno(7);
        resp.set_role(PeerRole::LEADER);
        Ok(resp.write_to_bytes().unwrap())
    }));
    let resp =
        ListMastersResponsePB::parse_from_bytes(&success(h.call("ListMasters", vec![]))).unwrap();
    assert_eq!(resp.masters.len(), 2);
    assert_eq!(resp.masters[0].role(), PeerRole::LEADER);
    assert_eq!(resp.masters[0].instance_id.instance_seqno(), 7);
    let unreachable = &resp.masters[1];
    assert!(unreachable.error.message().contains("peer-b"));
    assert!(unreachable.error.message().contains("connection refused"));
    assert_eq!(unreachable.instance_id.permanent_uuid(), b"peer-b");
    assert_eq!(unreachable.registration.private_rpc_addresses.len(), 1);
}

#[test]
fn list_masters_in_shell_mode() {
    let h = Harness::new();
    h.info.shell_mode.store(true, Ordering::SeqCst);
    let resp =
        ListMastersResponsePB::parse_from_bytes(&success(h.call("ListMasters", vec![]))).unwrap();
    assert_eq!(resp.masters.len(), 1);
    assert_eq!(resp.masters[0].role(), PeerRole::NON_PARTICIPANT);
}
