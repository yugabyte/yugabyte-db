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
use yb_proto::pb::master_backup::ListSnapshotsResponsePB;
use yb_proto::pb::master_cluster::{ListMastersResponsePB, RemovedMasterUpdateResponsePB};
use yb_proto::pb::master_heartbeat::TSToMasterCommonPB;
use yb_proto::{find_method, SERVICES};

use super::*;
use master_table_lifecycle::{LifecycleOptions, SchemaHost, TableLifecycle};

struct FakeInfo {
    initialized: AtomicBool,
    shell_mode: AtomicBool,
    role: AtomicI32,
    config: Mutex<Option<RaftConfigPB>>,
    host_calls: Mutex<Vec<String>>,
    last_host_request: Mutex<Vec<u8>>,
}

impl FakeInfo {
    fn new() -> Arc<FakeInfo> {
        Arc::new(FakeInfo {
            initialized: AtomicBool::new(true),
            shell_mode: AtomicBool::new(false),
            role: AtomicI32::new(PeerRole::FOLLOWER as i32),
            config: Mutex::new(Some(config(&[("peer-a", 7100), ("peer-b", 7101)], 12))),
            host_calls: Mutex::new(vec![]),
            last_host_request: Mutex::new(vec![]),
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

    fn handle_on_host(&self, method: &str, request: &[u8]) -> Result<Vec<u8>, Status> {
        self.host_calls.lock().unwrap().push(method.to_string());
        *self.last_host_request.lock().unwrap() = request.to_vec();
        Ok(b"host".to_vec())
    }
}

/// The handler tests send no table DDL that reaches the host.
struct NoSchemaHost;

impl SchemaHost for NoSchemaHost {
    fn call(&self, method: &'static str, _request: &[u8]) -> Result<Vec<u8>, Status> {
        Err(Status::new(StatusCode::NotSupported, method))
    }
}

struct Harness {
    runtime: tokio::runtime::Runtime,
    info: Arc<FakeInfo>,
    rpc: Arc<FakeRpcClient>,
    leader: Option<Arc<LeaderServices>>,
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
            leader: None,
        }
    }

    /// Calls `method` and waits for the answer.
    fn call(&self, method: &str, request: Vec<u8>) -> FakeResponse {
        self.call_with_timeout(method, request, Duration::from_secs(60))
    }

    /// Calls `method` with `timeout` left to the call's deadline and waits for the answer.
    fn call_with_timeout(&self, method: &str, request: Vec<u8>, timeout: Duration) -> FakeResponse {
        let (service, index, _) = find_method("yb.master.MasterService", method)
            .or_else(|| find_method("yb.master.MasterBackup", method))
            .or_else(|| find_method("yb.master.MasterRustPeer", method))
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
            self.leader.clone(),
        );
        let (call, outcome) = FakeInboundCall::new(index, request);
        handler.handle(call.with_timeout(timeout));
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
fn expired_calls_are_not_served() {
    let h = Harness::new();
    match h.call_with_timeout("GetMasterRegistration", vec![], Duration::ZERO) {
        FakeResponse::Error(status) => assert_eq!(status.code, StatusCode::TimedOut),
        other => panic!("expected TimedOut, got {other:?}"),
    }
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
        yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::ILLEGAL_STATE
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

mod leader_tests {
    use std::sync::atomic::AtomicBool;

    use master_catalog_state::{run_materializer, Catalog, CatalogWriter, WriterOptions};
    use master_leadership::{FakeLeaderState, LeaderStatus, Leadership};
    use master_sys_catalog::{FakeSysCatalog, RowChange};
    use master_ts_manager::{TsManager, TsManagerOptions};
    use protobuf::EnumOrUnknown;
    use yb_proto::pb::catalog_entity_info::{
        sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB,
        SysNamespaceEntryPB, SysPersistenceBlockMapEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
        SysXClusterConfigEntryPB,
    };
    use yb_proto::pb::common_types::{TableType, YQLDatabase};
    use yb_proto::pb::master_client::{
        GetTableLocationsRequestPB, GetTableLocationsResponsePB, GetTabletLocationsRequestPB,
        GetTabletLocationsResponsePB,
    };
    use yb_proto::pb::master_cluster::{
        ChangeMasterClusterConfigRequestPB, ChangeMasterClusterConfigResponsePB,
        GetMasterClusterConfigResponsePB, GetMasterRegistrationResponsePB,
        ListTabletServersResponsePB,
    };
    use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
    use yb_proto::pb::master_types::{master_error_pb, SysRowEntryType, TSRegistrationPB};

    use super::*;

    fn upsert(entry_type: SysRowEntryType, id: &str, message: &impl Message) -> RowChange {
        RowChange::Upsert {
            entry_type,
            id: id.as_bytes().to_vec(),
            data: message.write_to_bytes().unwrap(),
        }
    }

    /// The master's lock manager, unused by these tests.
    struct NoLocalLocks;

    impl master_ysql_manager::locks::LocalLockManager for NoLocalLocks {
        fn acquire(
            &self,
            _req: &yb_proto::pb::tserver::AcquireObjectLockRequestPB,
            _timeout: std::time::Duration,
        ) -> master_task_runtime::BoxFuture<'static, Result<(), Status>> {
            Box::pin(async { Ok(()) })
        }
        fn release(
            &self,
            _req: &yb_proto::pb::tserver::ReleaseObjectLockRequestPB,
            _timeout: std::time::Duration,
        ) -> Result<(), Status> {
            Ok(())
        }
        fn reset(&self, _entries: &yb_proto::pb::tserver::DdlLockEntriesPB) -> Result<(), Status> {
            Ok(())
        }
    }

    fn with_leader(lead: bool) -> (Harness, Arc<FakeLeaderState>) {
        let mut harness = Harness::new();
        harness
            .info
            .role
            .store(PeerRole::LEADER as i32, Ordering::SeqCst);
        let source = FakeLeaderState::new();
        let leader = harness.runtime.block_on(async {
            let (fake, events) = FakeSysCatalog::new();
            let mut config = SysClusterConfigEntryPB::new();
            config.set_version(1);
            config.set_cluster_uuid("cluster".into());
            let mut ns = SysNamespaceEntryPB::new();
            ns.set_name(b"ks".to_vec());
            ns.set_state(sys_namespace_entry_pb::State::RUNNING);
            let mut table = SysTablesEntryPB::new();
            table.set_name(b"t".to_vec());
            table.set_namespace_id(b"ns1".to_vec());
            table.set_version(0);
            table.schema = Some(Default::default()).into();
            table.set_state(sys_tables_entry_pb::State::RUNNING);
            let mut xcluster = SysXClusterConfigEntryPB::new();
            xcluster.set_version(1);
            let mut persistence = SysPersistenceBlockMapEntryPB::new();
            persistence.set_version(0);
            persistence.persistence_block_map = Some(Default::default()).into();
            config.set_oid_cache_invalidations_count(4);
            let mut changes = vec![
                upsert(SysRowEntryType::CLUSTER_CONFIG, "", &config),
                upsert(SysRowEntryType::XCLUSTER_CONFIG, "", &xcluster),
                upsert(SysRowEntryType::PERSISTENCE_BLOCK_MAP, "", &persistence),
                upsert(SysRowEntryType::NAMESPACE, "ns1", &ns),
                upsert(SysRowEntryType::TABLE, "table1", &table),
            ];
            // A YCQL keyspace with a table, and the system_schema.tables virtual table.
            for (id, name) in [
                ("00000000000000000000000000000002", "system_schema"),
                ("cqlns", "cqlks"),
            ] {
                let mut ns = SysNamespaceEntryPB::new();
                ns.set_name(name.as_bytes().to_vec());
                ns.set_database_type(YQLDatabase::YQL_DATABASE_CQL);
                ns.set_state(sys_namespace_entry_pb::State::RUNNING);
                changes.push(upsert(SysRowEntryType::NAMESPACE, id, &ns));
            }
            for (id, namespace_id, name, tablet_id) in [
                (
                    "vtable-tables",
                    "00000000000000000000000000000002",
                    "tables",
                    "vtable-tablet",
                ),
                ("ycqltable", "cqlns", "ct", "ycql-tablet"),
            ] {
                let mut table = SysTablesEntryPB::new();
                table.set_name(name.as_bytes().to_vec());
                table.set_namespace_id(namespace_id.as_bytes().to_vec());
                table.set_table_type(TableType::YQL_TABLE_TYPE);
                table.set_version(0);
                table.schema = Some(Default::default()).into();
                table.set_state(sys_tables_entry_pb::State::RUNNING);
                changes.push(upsert(SysRowEntryType::TABLE, id, &table));
                let mut tablet = SysTabletsEntryPB::new();
                tablet.set_table_id(id.as_bytes().to_vec());
                tablet.set_state(sys_tablets_entry_pb::State::RUNNING);
                changes.push(upsert(SysRowEntryType::TABLET, tablet_id, &tablet));
            }
            for (id, start) in [("tablet-a", ""), ("tablet-b", "m")] {
                let mut tablet = SysTabletsEntryPB::new();
                tablet.set_table_id(b"table1".to_vec());
                tablet.set_state(sys_tablets_entry_pb::State::RUNNING);
                tablet
                    .partition
                    .mut_or_insert_default()
                    .set_partition_key_start(start.as_bytes().to_vec());
                let cstate = tablet.committed_consensus_state.mut_or_insert_default();
                cstate.set_current_term(1);
                cstate.set_leader_uuid("ts1".into());
                let mut peer = RaftPeerPB::new();
                peer.set_permanent_uuid(b"ts1".to_vec());
                cstate.config.mut_or_insert_default().peers.push(peer);
                changes.push(upsert(SysRowEntryType::TABLET, id, &tablet));
            }
            fake.apply(changes);
            fake.set_term(7);
            let catalog = Catalog::new(fake.sender().clone());
            tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
            catalog.barrier().await;
            let leadership = Leadership::new(source.clone(), || true, Arc::default());
            if lead {
                source.lead(7);
                leadership.step();
                let mut phases = leadership.subscribe();
                while !matches!(
                    *phases.borrow_and_update(),
                    master_leadership::Phase::Ready { .. }
                ) {
                    phases.changed().await.unwrap();
                }
            }
            let writer = CatalogWriter::start(
                catalog.clone(),
                fake.clone(),
                leadership.clone(),
                WriterOptions::default(),
            );
            let ts_manager = TsManager::new(Arc::default(), TsManagerOptions::default());
            let tables = TableLifecycle::new(
                catalog.clone(),
                writer.clone(),
                leadership.clone(),
                ts_manager.clone(),
                Arc::new(NoSchemaHost),
                LifecycleOptions::default(),
            );
            let splits = master_tablet_split_manager::TabletSplitManager::new(
                catalog.clone(),
                writer.clone(),
                leadership.clone(),
                ts_manager.clone(),
                Arc::new(NoSchemaHost),
                master_task_runtime::TsRpcExecutor::new(|_| 1),
                Default::default(),
                Default::default(),
            );
            let leases = master_ysql_manager::lease::YsqlLeases::new(
                writer.clone(),
                leadership.clone(),
                ts_manager.clone(),
                Default::default(),
            );
            let locks = master_ysql_manager::locks::ObjectLocks::new(
                catalog.clone(),
                writer.clone(),
                leadership.clone(),
                ts_manager.clone(),
                Arc::new(NoLocalLocks),
                master_task_runtime::TsRpcExecutor::new(|_| 1),
                Default::default(),
            );
            let backends = master_ysql_manager::backends::YsqlBackends::new(
                catalog.clone(),
                leadership.clone(),
                ts_manager.clone(),
                Arc::new(NoSchemaHost),
                master_task_runtime::TsRpcExecutor::new(|_| 1),
                Default::default(),
            );
            let truncates = master_table_lifecycle::Truncates::new(
                tables.clone(),
                master_task_runtime::TsRpcExecutor::new(|_| 1),
                master_table_lifecycle::TruncateOptions {
                    disable_truncate_table: || false,
                    enable_truncate_on_pitr_table: || false,
                    rpc_timeout: || std::time::Duration::from_secs(1),
                    retry: Default::default,
                },
            );
            let flushes = master_table_lifecycle::Flushes::new(
                tables.clone(),
                master_task_runtime::TsRpcExecutor::new(|_| 1),
                master_table_lifecycle::FlushOptions {
                    rpc_timeout: || std::time::Duration::from_secs(1),
                    retry: Default::default,
                },
            );
            Arc::new(LeaderServices {
                catalog,
                writer,
                leadership,
                tables,
                ts_manager,
                contributors: vec![],
                follower_stream: Default::default(),
                load_balancer_enabled: AtomicBool::new(true),
                replication_factor: || 3,
                balancer: Arc::new(master_cluster_balancer::controller::BalancerStatus::new(5)),
                load_balancing_flag: || true,
                transaction_tables_use_preferred_zones: || false,
                splits,
                leases,
                locks,
                backends,
                truncates,
                flushes,
                blacklist_progress_initial_delay: || std::time::Duration::ZERO,
                delay_leader_blacklist_completion: || false,
                ysql_major_version: Some(15),
                tablespace_based_transaction_placement: || true,
            })
        });
        harness.leader = Some(leader);
        (harness, source)
    }

    /// `system_schema.tables` lists the YCQL tables visible to clients, the virtual tables
    /// among them, and a tablet of a real table is not a virtual table's.
    #[test]
    fn ycql_virtual_tables_list_the_catalog() {
        let (harness, _source) = with_leader(true);
        let leader = harness.leader.as_ref().unwrap();
        let input = leader
            .ycql_virtual_table_input(b"vtable-tablet", None)
            .unwrap()
            .unwrap();
        assert_eq!(input.namespace_name(), "system_schema");
        assert_eq!(input.table_name(), "tables");
        let mut tables: Vec<_> = input
            .tables
            .iter()
            .map(|t| (t.namespace_name().to_string(), t.table.name().to_vec()))
            .collect();
        tables.sort();
        assert_eq!(
            tables,
            // `ks.t` has no table type, which is YCQL's (`DEFAULT_TABLE_TYPE`).
            vec![
                ("cqlks".to_string(), b"ct".to_vec()),
                ("ks".to_string(), b"t".to_vec()),
                ("system_schema".to_string(), b"tables".to_vec()),
            ]
        );
        assert!(leader
            .ycql_virtual_table_input(b"ycql-tablet", None)
            .unwrap()
            .is_none());
    }

    #[test]
    fn ready_leader_serves_core_rpcs() {
        let (harness, _source) = with_leader(true);
        let registration = GetMasterRegistrationResponsePB::parse_from_bytes(&success(
            harness.call("GetMasterRegistration", vec![]),
        ))
        .unwrap();
        assert_eq!(registration.role(), PeerRole::LEADER);

        // A tserver registers through the leader.
        let mut req = TSHeartbeatRequestPB::new();
        let instance = req
            .common
            .mut_or_insert_default()
            .ts_instance
            .mut_or_insert_default();
        instance.set_permanent_uuid(b"ts1".to_vec());
        instance.set_instance_seqno(1);
        req.registration = Some(TSRegistrationPB::new()).into();
        let resp = TSHeartbeatResponsePB::parse_from_bytes(&success(
            harness.call("TSHeartbeat", req.write_to_bytes().unwrap()),
        ))
        .unwrap();
        assert!(resp.leader_master(), "{resp:?}");
        assert!(resp.error.is_none());
        assert_eq!(resp.tservers.len(), 1);
        // It holds an xCluster-guarded lease, which PG needs to allocate OIDs.
        assert_eq!(
            resp.xcluster_guarded_info.oid_cache_invalidations_count(),
            4
        );
        assert_eq!(
            resp.xcluster_guarded_info
                .xcluster_guarded_info_version
                .term(),
            7
        );
        assert!(resp.xcluster_guarded_lease_duration_ms() > 0);

        // The registered tserver gets a YSQL lease.
        let mut lease_req = yb_proto::pb::master_ysql_lease::RefreshYsqlLeaseRequestPB::new();
        let lease_instance = lease_req.instance.mut_or_insert_default();
        lease_instance.set_permanent_uuid(b"ts1".to_vec());
        lease_instance.set_instance_seqno(1);
        lease_req.set_local_request_send_time_ms(1);
        let lease = yb_proto::pb::master_ysql_lease::RefreshYsqlLeaseResponsePB::parse_from_bytes(
            &success(harness.call("RefreshYsqlLease", lease_req.write_to_bytes().unwrap())),
        )
        .unwrap();
        assert!(lease.info().new_lease(), "{lease:?}");
        assert_eq!(lease.info().lease_epoch(), 1);

        let servers = ListTabletServersResponsePB::parse_from_bytes(&success(
            harness.call("ListTabletServers", vec![]),
        ))
        .unwrap();
        assert_eq!(servers.servers.len(), 1);
        assert!(servers.servers[0].alive());
        assert_eq!(servers.master_uuid(), "self-uuid");

        // Table locations by name, from the committed config until reports arrive.
        let mut req = GetTableLocationsRequestPB::new();
        let table = req.table.mut_or_insert_default();
        table.set_table_name("t".into());
        table
            .namespace
            .mut_or_insert_default()
            .set_name("ks".into());
        req.set_partition_key_start(b"n".to_vec());
        let resp = GetTableLocationsResponsePB::parse_from_bytes(&success(
            harness.call("GetTableLocations", req.write_to_bytes().unwrap()),
        ))
        .unwrap();
        assert!(resp.error.is_none(), "{resp:?}");
        assert_eq!(resp.tablet_locations.len(), 1);
        assert_eq!(resp.tablet_locations[0].tablet_id(), b"tablet-b");
        assert_eq!(
            resp.tablet_locations[0].replicas[0].role(),
            PeerRole::LEADER
        );
        assert_eq!(resp.tablet_locations[0].expected_live_replicas(), 3);

        let mut req = GetTabletLocationsRequestPB::new();
        req.tablet_ids = vec![b"tablet-a".to_vec(), b"nope".to_vec()];
        let resp = GetTabletLocationsResponsePB::parse_from_bytes(&success(
            harness.call("GetTabletLocations", req.write_to_bytes().unwrap()),
        ))
        .unwrap();
        assert_eq!(resp.tablet_locations.len(), 1);
        assert_eq!(resp.errors.len(), 1);

        // Cluster config: a stale version is refused, the current one bumps it.
        let config = GetMasterClusterConfigResponsePB::parse_from_bytes(&success(
            harness.call("GetMasterClusterConfig", vec![]),
        ))
        .unwrap()
        .cluster_config
        .unwrap();
        // C++ refuses a placement whose minimums exceed num_replicas.
        let mut invalid = config.clone();
        let live = invalid
            .replication_info
            .mut_or_insert_default()
            .live_replicas
            .mut_or_insert_default();
        live.set_num_replicas(3);
        for zone in ["z1", "z2"] {
            let mut block = yb_proto::pb::common_net::PlacementBlockPB::new();
            let ci = block.cloud_info.mut_or_insert_default();
            ci.set_placement_cloud("aws".into());
            ci.set_placement_region("r1".into());
            ci.set_placement_zone(zone.into());
            block.set_min_num_replicas(2);
            live.placement_blocks.push(block);
        }
        let mut req = ChangeMasterClusterConfigRequestPB::new();
        req.cluster_config = Some(invalid).into();
        let resp = ChangeMasterClusterConfigResponsePB::parse_from_bytes(&success(
            harness.call("ChangeMasterClusterConfig", req.write_to_bytes().unwrap()),
        ))
        .unwrap();
        assert_eq!(
            resp.error.code(),
            master_error_pb::Code::INVALID_CLUSTER_CONFIG
        );

        let mut stale = config.clone();
        stale.set_version(0);
        let mut req = ChangeMasterClusterConfigRequestPB::new();
        req.cluster_config = Some(stale).into();
        let resp = ChangeMasterClusterConfigResponsePB::parse_from_bytes(&success(
            harness.call("ChangeMasterClusterConfig", req.write_to_bytes().unwrap()),
        ))
        .unwrap();
        assert_eq!(
            resp.error.code(),
            master_error_pb::Code::CONFIG_VERSION_MISMATCH
        );
        req.cluster_config = Some(config).into();
        let resp = ChangeMasterClusterConfigResponsePB::parse_from_bytes(&success(
            harness.call("ChangeMasterClusterConfig", req.write_to_bytes().unwrap()),
        ))
        .unwrap();
        assert!(resp.error.is_none(), "{resp:?}");
        let config = GetMasterClusterConfigResponsePB::parse_from_bytes(&success(
            harness.call("GetMasterClusterConfig", vec![]),
        ))
        .unwrap();
        assert_eq!(config.cluster_config.version(), 2);

        // A leader method of a later workstream (snapshots, W10).
        let resp = ListSnapshotsResponsePB::parse_from_bytes(&success(
            harness.call("ListSnapshots", vec![]),
        ))
        .unwrap();
        assert_eq!(
            resp.error.status.code().value(),
            StatusCode::NotSupported as i32
        );
    }

    #[test]
    fn encryption_methods_get_the_cluster_configs_encryption_info() {
        let (harness, _source) = with_leader(true);
        for method in ["GetFullUniverseKeyRegistry", "IsEncryptionEnabled"] {
            *harness.info.last_host_request.lock().unwrap() = b"stale".to_vec();
            assert_eq!(success(harness.call(method, b"ignored".to_vec())), b"host");
            assert_eq!(
                harness.info.host_calls.lock().unwrap().last().unwrap(),
                method
            );
            // The cluster config has no encryption info.
            assert!(harness.info.last_host_request.lock().unwrap().is_empty());
        }
    }

    #[test]
    fn not_ready_leader_answers_not_the_leader() {
        let (harness, source) = with_leader(false);
        source.set(PeerRole::LEADER, 7, LeaderStatus::NoMajorityReplicatedLease);
        let registration = GetMasterRegistrationResponsePB::parse_from_bytes(&success(
            harness.call("GetMasterRegistration", vec![]),
        ))
        .unwrap();
        assert_eq!(registration.role(), PeerRole::FOLLOWER);
        let resp = ListTabletServersResponsePB::parse_from_bytes(&success(
            harness.call("ListTabletServers", vec![]),
        ))
        .unwrap();
        assert_eq!(resp.error.code(), master_error_pb::Code::NOT_THE_LEADER);
        assert_eq!(resp.master_uuid(), "self-uuid");
        let _ = EnumOrUnknown::new(PeerRole::LEADER);
    }
}
