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

use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;
use std::time::Duration;

use protobuf::Message;
use yb_host_api::rpc::{FakeInboundCall, FakeResponse, FakeRpcClient, RpcHandler};
use yb_host_api::StatusCode;
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::RaftPeerPB;
use yb_proto::pb::master_cluster::GetMasterRegistrationResponsePB;
use yb_proto::pb::master_ddl::{
    CreateNamespaceResponsePB, CreateTableResponsePB, IsCreateTableDoneResponsePB,
};
use yb_proto::pb::master_heartbeat::{TSHeartbeatRequestPB, TSHeartbeatResponsePB};
use yb_proto::pb::tserver_admin::CreateTabletRequestPB;
use yb_proto::{find_method, SERVICES};

use super::*;

fn create_tablet_request(dest: &[u8], tablet: &str, peers: &[&[u8]]) -> CreateTabletRequestPB {
    let mut req = CreateTabletRequestPB::new();
    req.set_dest_uuid(dest.to_vec());
    req.set_tablet_id(tablet.as_bytes().to_vec());
    req.set_table_id(b"table".to_vec());
    req.set_table_name("t".into());
    req.schema.mut_or_insert_default();
    let config = req.config.mut_or_insert_default();
    for peer in peers {
        let mut p = RaftPeerPB::new();
        p.set_permanent_uuid(peer.to_vec());
        config.peers.push(p);
    }
    req
}

fn tserver(index: usize) -> SimTserver {
    SimTserver::new(index, format!("127.1.0.{}", index + 1), 9100, 1)
}

#[test]
fn registers_then_reports_in_full_then_incrementally() {
    let mut ts = tserver(0);
    let (req, sent) = ts.heartbeat_request();
    assert!(req.registration.is_some());
    assert!(req.tablet_report.is_incremental());
    assert!(!ts.is_registered());

    // The master asks for a full report after registration, as a C++ master does.
    let mut resp = TSHeartbeatResponsePB::new();
    resp.set_needs_full_tablet_report(true);
    ts.handle_heartbeat_response(&sent, &resp);
    let (req, sent) = ts.heartbeat_request();
    assert!(req.registration.is_none());
    assert!(!req.tablet_report.is_incremental());
    ts.handle_heartbeat_response(&sent, &TSHeartbeatResponsePB::new());
    assert!(ts.is_registered());

    let uuid = ts.uuid().to_vec();
    ts.create_tablet(&create_tablet_request(&uuid, "tablet1", &[b"zz", &uuid]));
    let (req, sent) = ts.heartbeat_request();
    let report = &req.tablet_report;
    assert!(report.is_incremental());
    assert_eq!(report.updated_tablets.len(), 1);
    let tablet = &report.updated_tablets[0];
    // The lowest UUID leads.
    assert_eq!(
        tablet.committed_consensus_state.leader_uuid().as_bytes(),
        &uuid[..]
    );
    assert_eq!(req.leader_count(), 1);
    req.write_to_bytes().expect("every required field is set");

    // Acknowledged: not reported again.
    ts.handle_heartbeat_response(&sent, &TSHeartbeatResponsePB::new());
    assert!(ts
        .heartbeat_request()
        .0
        .tablet_report
        .updated_tablets
        .is_empty());

    ts.delete_tablet(b"tablet1");
    let (req, _) = ts.heartbeat_request();
    assert_eq!(
        req.tablet_report.removed_tablet_ids,
        vec![b"tablet1".to_vec()]
    );
}

#[test]
fn truncated_report_is_sent_again() {
    let mut ts = tserver(0);
    let uuid = ts.uuid().to_vec();
    ts.create_tablet(&create_tablet_request(&uuid, "tablet1", &[&uuid]));
    let (_, sent) = ts.heartbeat_request();
    let mut resp = TSHeartbeatResponsePB::new();
    resp.tablet_report
        .mut_or_insert_default()
        .set_processing_truncated(true);
    ts.handle_heartbeat_response(&sent, &resp);
    assert_eq!(
        ts.heartbeat_request().0.tablet_report.updated_tablets.len(),
        1
    );
}

fn admin_service() -> &'static yb_proto::ServiceDesc {
    SERVICES
        .iter()
        .find(|s| s.proto_name == "yb.tserver.TabletServerAdminService")
        .unwrap()
}

fn call(service: &SimService, method: &str, request: Vec<u8>) -> FakeResponse {
    let index = admin_service()
        .methods
        .iter()
        .position(|m| m.name == method)
        .unwrap();
    let (call, outcome) = FakeInboundCall::new(index, request);
    service.handle(call);
    let response = outcome.lock().unwrap().clone().unwrap();
    response
}

#[test]
fn routes_tablet_rpcs_by_dest_uuid() {
    let cluster = Arc::new(SimCluster::new(vec![tserver(0), tserver(1)]));
    let service = SimService::new(admin_service(), cluster.clone());
    let second = cluster.tservers()[1].lock().unwrap().uuid().to_vec();

    let req = create_tablet_request(&second, "tablet1", &[&second]);
    assert!(matches!(
        call(&service, "CreateTablet", req.write_to_bytes().unwrap()),
        FakeResponse::Success(_)
    ));
    assert_eq!(cluster.tservers()[0].lock().unwrap().num_tablets(), 0);
    assert_eq!(cluster.tservers()[1].lock().unwrap().num_tablets(), 1);

    let req = create_tablet_request(b"nobody", "tablet2", &[&second]);
    match call(&service, "CreateTablet", req.write_to_bytes().unwrap()) {
        FakeResponse::Error(status) => assert_eq!(status.code, StatusCode::InvalidArgument),
        other => panic!("{other:?}"),
    }

    // Calls the simulator does not model succeed with nothing to report, and are counted.
    assert_eq!(
        call(&service, "UpdateTransactionTablesVersion", vec![]),
        FakeResponse::Success(vec![])
    );
    assert_eq!(
        cluster.unmodeled_calls().into_iter().collect::<Vec<_>>(),
        vec![(
            "yb.tserver.TabletServerAdminService.UpdateTransactionTablesVersion".to_string(),
            1
        )]
    );
}

#[test]
fn percentiles() {
    let mut l = Latencies::default();
    for ms in 1..=100 {
        l.record(Duration::from_millis(ms));
    }
    assert_eq!(l.percentile(50.0), Duration::from_millis(50));
    assert_eq!(l.percentile(99.0), Duration::from_millis(99));
    assert_eq!(l.percentile(100.0), Duration::from_millis(100));
    assert_eq!(Latencies::default().percentile(50.0), Duration::ZERO);
}

/// A master that registers tservers like a C++ leader and creates tables at once.
fn fake_master() -> FakeRpcClient {
    let heartbeats = Arc::new(AtomicUsize::new(0));
    FakeRpcClient::new(move |_, _, service, method, request| {
        assert_eq!(service, "yb.master.MasterService");
        assert!(find_method(service, method).is_some(), "{method}");
        let bytes = match method {
            "GetMasterRegistration" => {
                let mut resp = GetMasterRegistrationResponsePB::new();
                let instance = resp.instance_id.mut_or_insert_default();
                instance.set_permanent_uuid(b"master".to_vec());
                instance.set_instance_seqno(1);
                resp.set_role(PeerRole::LEADER);
                resp.write_to_bytes()
            }
            "TSHeartbeat" => {
                heartbeats.fetch_add(1, Ordering::Relaxed);
                let req = TSHeartbeatRequestPB::parse_from_bytes(request).unwrap();
                let mut resp = TSHeartbeatResponsePB::new();
                resp.set_leader_master(true);
                if req.registration.is_some() {
                    resp.set_needs_full_tablet_report(true);
                }
                resp.write_to_bytes()
            }
            "CreateNamespace" => CreateNamespaceResponsePB::new().write_to_bytes(),
            "CreateTable" => {
                let mut resp = CreateTableResponsePB::new();
                resp.set_table_id(b"table-id".to_vec());
                resp.write_to_bytes()
            }
            "IsCreateTableDone" => {
                let mut resp = IsCreateTableDoneResponsePB::new();
                resp.set_done(true);
                resp.write_to_bytes()
            }
            other => panic!("unexpected {other}"),
        };
        Ok(bytes.unwrap())
    })
}

#[tokio::test]
async fn workload_runs_to_a_report() {
    let cluster = Arc::new(SimCluster::new((0..3).map(tserver).collect()));
    let config = WorkloadConfig {
        master_addresses: vec![("127.0.0.1".into(), 7100)],
        heartbeat_interval: Duration::from_millis(5),
        rpc_timeout: Duration::from_secs(1),
        steady_state: Duration::from_millis(50),
        num_tables: 2,
        tablets_per_table: 1,
        keyspace_prefix: "sim".into(),
    };
    let report = run(cluster, Arc::new(fake_master()), config).await.unwrap();
    assert_eq!(report.num_tservers, 3);
    assert_eq!(report.create_table.count(), 2);
    assert!(report.steady_heartbeats.count() > 0);
    let json = report.to_json();
    assert!(json.contains("\"num_tservers\": 3"), "{json}");
    assert!(
        json.contains("\"create_table_ms\": {\"count\": 2"),
        "{json}"
    );
}
