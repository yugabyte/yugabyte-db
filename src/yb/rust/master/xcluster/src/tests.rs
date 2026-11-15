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

//! Tests of the outbound replication groups against a fake sys catalog.

use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::{run_materializer, Catalog, CatalogWriter, WriterOptions};
use master_leadership::{FakeLeaderState, LeaderToken, Leadership, Phase};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_table_lifecycle::SchemaHost;
use master_tablet_locations::Replica;
use master_task_runtime::BoxFuture;
use master_xrepl::{CreatesTables, Xrepl, XreplOptions};
use protobuf::{EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState,
    sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb,
    sys_xcluster_outbound_replication_group_entry_pb::{
        namespace_info_pb::State as NsState, target_universe_info_pb::State as TargetState,
    },
    SysCDCStreamEntryPB, SysClusterConfigEntryPB, SysNamespaceEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB, SysXClusterConfigEntryPB,
    SysXClusterOutboundReplicationGroupEntryPB as GroupPB,
};
use yb_proto::pb::cdc_service::{BootstrapProducerRequestPB, BootstrapProducerResponsePB};
use yb_proto::pb::common::PartitionPB;
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::{PeerRole, TableType, YQLDatabase};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_ddl::{CreateTableRequestPB, CreateTableResponsePB};
use yb_proto::pb::master_heartbeat::{
    TSHeartbeatRequestPB, TSHeartbeatResponsePB, TSInformationPB,
};
use yb_proto::pb::master_replication::*;
use yb_proto::pb::master_rust_peer::{
    CdcStateEntryPB, CdcStateUpsertRequestPB, HybridTimeNowResponsePB,
};
use yb_proto::pb::master_types::{MasterErrorPB, SysRowEntryType};

use crate::{RemoteMasters, XCluster, XClusterOptions};

const DB: &str = "00004000000030008000000000000000";
const TABLE: &str = "00004000000030008000000000004000";
const TABLET: &str = "tab-ysql";
const NEW_TABLE: &str = "00004000000030008000000000004001";
const NEW_TABLET: &str = "tab-new";
const CQL_NS: &str = "cql-ns";
const GROUP: &str = "rg1";

fn ysql_table(name: &str) -> SysTablesEntryPB {
    let mut t = SysTablesEntryPB::new();
    t.set_name(name.into());
    t.set_namespace_id(DB.as_bytes().to_vec());
    t.table_type = Some(EnumOrUnknown::new(TableType::PGSQL_TABLE_TYPE));
    t.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    t.set_version(1);
    t.schema = protobuf::MessageField::some(Default::default());
    t.schema
        .mut_or_insert_default()
        .set_DEPRECATED_pgschema_name("public".into());
    t.partition_schema = protobuf::MessageField::some(Default::default());
    t
}

fn tablet(table: &str) -> SysTabletsEntryPB {
    let mut t = SysTabletsEntryPB::new();
    t.set_table_id(table.as_bytes().to_vec());
    t.partition = protobuf::MessageField::some(PartitionPB::new());
    t.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
    t
}

fn rows() -> Vec<RowChange> {
    let row = |entry_type, id: &str, data: Vec<u8>| RowChange::Upsert {
        entry_type,
        id: id.as_bytes().to_vec(),
        data,
    };
    let mut ns = SysNamespaceEntryPB::new();
    ns.set_name("db1".into());
    ns.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_PGSQL));
    ns.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
    let mut cql = SysNamespaceEntryPB::new();
    cql.set_name("ks".into());
    cql.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_CQL));
    cql.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
    let mut cql_table = ysql_table("c");
    cql_table.set_namespace_id(CQL_NS.as_bytes().to_vec());
    cql_table.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
    let mut cdc_state = SysTablesEntryPB::new();
    cdc_state.set_name("cdc_state".into());
    cdc_state.set_namespace_id(b"00000000000000000000000000000001".to_vec());
    cdc_state.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
    cdc_state.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    cdc_state.set_version(0);
    cdc_state.schema = protobuf::MessageField::some(Default::default());
    cdc_state.partition_schema = protobuf::MessageField::some(Default::default());
    vec![
        RowChange::Upsert {
            entry_type: SysRowEntryType::CLUSTER_CONFIG,
            id: Vec::new(),
            data: SysClusterConfigEntryPB::new().write_to_bytes().unwrap(),
        },
        RowChange::Upsert {
            entry_type: SysRowEntryType::XCLUSTER_CONFIG,
            id: Vec::new(),
            data: SysXClusterConfigEntryPB::new().write_to_bytes().unwrap(),
        },
        row(SysRowEntryType::NAMESPACE, DB, ns.write_to_bytes().unwrap()),
        row(
            SysRowEntryType::NAMESPACE,
            CQL_NS,
            cql.write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLE,
            TABLE,
            ysql_table("users").write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLET,
            TABLET,
            tablet(TABLE).write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLE,
            "cql-table",
            cql_table.write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLE,
            "cdc-state-table",
            cdc_state.write_to_bytes().unwrap(),
        ),
    ]
}

fn tserver(uuid: &str) -> (TSInformationPB, bool) {
    let mut info = TSInformationPB::new();
    info.tserver_instance
        .mut_or_insert_default()
        .set_permanent_uuid(uuid.as_bytes().to_vec());
    let mut hp = HostPortPB::new();
    hp.set_host(uuid.to_string());
    hp.set_port(9100);
    info.registration
        .mut_or_insert_default()
        .common
        .mut_or_insert_default()
        .private_rpc_addresses
        .push(hp);
    (info, true)
}

#[derive(Default)]
struct FakeHost {
    cdc_state: Mutex<Vec<CdcStateEntryPB>>,
}

impl SchemaHost for FakeHost {
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status> {
        match method {
            "CdcStateUpsert" => {
                let req = CdcStateUpsertRequestPB::parse_from_bytes(request).unwrap();
                self.cdc_state.lock().unwrap().extend(req.entries);
                Ok(Vec::new())
            }
            "HybridTimeNow" => {
                let mut resp = HybridTimeNowResponsePB::new();
                resp.set_hybrid_time(100_000_000_000 << 12);
                Ok(resp.write_to_bytes().unwrap())
            }
            _ => Ok(Vec::new()),
        }
    }
}

/// `system.cdc_state` exists in the rows, so nothing is created.
struct NoTables;

impl CreatesTables for NoTables {
    fn create<'a>(
        &'a self,
        _token: &'a LeaderToken,
        _deadline: Instant,
        _req: &'a CreateTableRequestPB,
        _resp: &'a mut CreateTableResponsePB,
    ) -> BoxFuture<'a, Result<(), MasterErrorPB>> {
        panic!("no table is created")
    }
}

/// The target's masters: SetupUniverseReplication answers a universe uuid, and
/// IsSetupUniverseReplicationDone answers done once `done` is set.
#[derive(Default)]
struct FakeTarget {
    calls: Mutex<Vec<(String, Vec<u8>)>>,
    done: Mutex<bool>,
}

impl RemoteMasters for FakeTarget {
    #[allow(clippy::too_many_arguments)]
    fn call(
        &self,
        _id: &str,
        masters: &[HostPortPB],
        _service: &'static str,
        method: &'static str,
        request: Vec<u8>,
        _timeout: Duration,
        _refresh: bool,
    ) -> BoxFuture<'static, Result<Vec<u8>, Status>> {
        assert!(!masters.is_empty());
        self.calls
            .lock()
            .unwrap()
            .push((method.to_string(), request));
        let out = match method {
            "SetupUniverseReplication" => {
                let mut resp = SetupUniverseReplicationResponsePB::new();
                resp.set_universe_uuid("target-uuid".into());
                resp.write_to_bytes().unwrap()
            }
            "IsSetupUniverseReplicationDone" => {
                let mut resp = IsSetupUniverseReplicationDoneResponsePB::new();
                resp.set_done(*self.done.lock().unwrap());
                if resp.done() {
                    resp.replication_error.mut_or_insert_default().code =
                        Some(EnumOrUnknown::from_i32(0));
                }
                resp.write_to_bytes().unwrap()
            }
            _ => Vec::new(),
        };
        Box::pin(async move { Ok(out) })
    }
}

struct Harness {
    xcluster: Arc<XCluster>,
    xrepl: Arc<Xrepl>,
    host: Arc<FakeHost>,
    target: Arc<FakeTarget>,
    catalog: Arc<Catalog>,
    fake: Arc<FakeSysCatalog>,
    token: LeaderToken,
    bootstraps: Arc<Mutex<Vec<BootstrapProducerRequestPB>>>,
    bootstrap_required: Arc<Mutex<bool>>,
}

impl Harness {
    async fn new() -> Harness {
        let (fake, events) = FakeSysCatalog::new();
        fake.apply(rows());
        fake.set_term(3);
        let catalog = Catalog::new(fake.sender().clone());
        tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
        catalog.barrier().await;
        let source = FakeLeaderState::new();
        let leadership = Leadership::new(source.clone(), || true, Arc::default());
        source.lead(3);
        leadership.step();
        let mut phases = leadership.subscribe();
        while !matches!(*phases.borrow_and_update(), Phase::Ready { .. }) {
            phases.changed().await.unwrap();
        }
        let token = leadership.token().unwrap();
        let host = Arc::new(FakeHost::default());
        let writer = CatalogWriter::start(
            catalog.clone(),
            fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        let ts_manager = master_ts_manager::TsManager::new(Arc::default(), Default::default());
        ts_manager.apply_peer_tservers(&[tserver("ts0")], true);
        for tablet in [TABLET, NEW_TABLET] {
            ts_manager.locations.update_replica(
                tablet.as_bytes(),
                Replica::new(
                    b"ts0".as_slice().into(),
                    PeerRole::LEADER,
                    PeerMemberType::VOTER,
                ),
            );
        }
        let bootstraps: Arc<Mutex<Vec<BootstrapProducerRequestPB>>> = Arc::default();
        let bootstrap_required = Arc::new(Mutex::new(false));
        let (record, required) = (bootstraps.clone(), bootstrap_required.clone());
        let rpc = Arc::new(yb_host_api::rpc::FakeRpcClient::new(
            move |_, _, service, method, req| {
                assert_eq!(
                    (service, method),
                    ("yb.cdc.CDCService", "BootstrapProducer")
                );
                let req = BootstrapProducerRequestPB::parse_from_bytes(req).unwrap();
                record.lock().unwrap().push(req);
                let mut resp = BootstrapProducerResponsePB::new();
                resp.set_bootstrap_required(*required.lock().unwrap());
                Ok(resp.write_to_bytes().unwrap())
            },
        ));
        let xrepl = Xrepl::new(
            catalog.clone(),
            writer.clone(),
            leadership.clone(),
            ts_manager.clone(),
            host.clone(),
            master_task_runtime::TsRpcExecutor::new(|_| usize::MAX),
            Default::default(),
            XreplOptions::default(),
        );
        xrepl.set_rpc(rpc.clone());
        let target = Arc::new(FakeTarget::default());
        let xcluster = XCluster::new(
            catalog.clone(),
            writer,
            leadership,
            ts_manager,
            xrepl.clone(),
            Arc::new(NoTables),
            target.clone(),
            XClusterOptions::default(),
        );
        xcluster.set_rpc(rpc);
        xrepl.set_extension(xcluster.clone());
        Harness {
            xcluster,
            xrepl,
            host,
            target,
            catalog,
            fake,
            token,
            bootstraps,
            bootstrap_required,
        }
    }

    fn deadline(&self) -> Instant {
        Instant::now() + Duration::from_secs(30)
    }

    fn group(&self) -> Option<GroupPB> {
        self.catalog
            .snapshot()
            .get(
                SysRowEntryType::XCLUSTER_OUTBOUND_REPLICATION_GROUP,
                GROUP.as_bytes(),
            )
            .map(|e| e.parse().unwrap())
    }

    async fn create(&self) -> Result<(), MasterErrorPB> {
        let mut req = XClusterCreateOutboundReplicationGroupRequestPB::new();
        req.set_replication_group_id(GROUP.into());
        req.namespace_ids.push(DB.into());
        self.xcluster.create_outbound_group(&self.token, &req).await
    }

    /// Waits for the namespace to leave CHECKPOINTING.
    async fn ready(&self) -> GroupPB {
        for _ in 0..500 {
            let pb = self.group().unwrap();
            if pb.namespace_infos[DB].state() != NsState::CHECKPOINTING {
                return pb;
            }
            tokio::time::sleep(Duration::from_millis(10)).await;
        }
        panic!("namespace still checkpointing");
    }

    fn stream(&self, id: &[u8]) -> Option<SysCDCStreamEntryPB> {
        self.catalog
            .snapshot()
            .get(SysRowEntryType::CDC_STREAM, id)
            .map(|e| e.parse().unwrap())
    }
}

#[tokio::test]
async fn a_new_group_checkpoints_its_tables_and_becomes_ready() {
    let h = Harness::new().await;
    *h.bootstrap_required.lock().unwrap() = true;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let ns = &pb.namespace_infos[DB];
    assert_eq!(ns.state(), NsState::READY);
    assert!(ns.initial_bootstrap_required());
    // Only the YSQL user table: not cdc_state, not the YCQL table.
    assert_eq!(ns.table_infos.len(), 1);
    let t = &ns.table_infos[TABLE];
    assert!(t.is_part_of_initial_bootstrap() && !t.is_checkpointing());
    let stream = h.stream(t.stream_id()).unwrap();
    assert_eq!(stream.state(), StreamState::INITIATED);
    assert!(stream.transactional());
    assert_eq!(stream.table_id, vec![TABLE.to_string()]);
    // One tserver call bootstrapped the stream, checking whether a bootstrap is needed.
    let bootstraps = h.bootstraps.lock().unwrap().clone();
    assert_eq!(bootstraps.len(), 1);
    assert!(bootstraps[0].check_if_bootstrap_required());
    assert_eq!(bootstraps[0].table_ids, vec![TABLE.to_string()]);
    assert_eq!(bootstraps[0].xrepl_stream_ids, vec![t.stream_id().to_vec()]);

    let mut req = IsXClusterBootstrapRequiredRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_namespace_id(DB.into());
    let mut resp = IsXClusterBootstrapRequiredResponsePB::new();
    h.xcluster
        .is_xcluster_bootstrap_required(&req, &mut resp)
        .unwrap();
    assert!(!resp.not_ready() && resp.initial_bootstrap_required());

    let mut req = GetXClusterStreamsRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_namespace_id(DB.into());
    let mut info = get_xcluster_streams_request_pb::TableInfo::new();
    info.set_table_name("users".into());
    info.set_pg_schema_name("public".into());
    req.table_infos.push(info);
    let mut resp = GetXClusterStreamsResponsePB::new();
    h.xcluster
        .get_xcluster_streams(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert!(!resp.not_ready());
    assert_eq!(resp.table_infos.len(), 1);
    assert_eq!(resp.table_infos[0].table_id(), TABLE.as_bytes());
    assert_eq!(resp.table_infos[0].xrepl_stream_id(), t.stream_id());
    assert_eq!(resp.table_infos[0].pg_schema_name(), "public");

    let mut resp = GetXClusterOutboundReplicationGroupsResponsePB::new();
    h.xcluster
        .get_outbound_groups(&Default::default(), &mut resp)
        .unwrap();
    assert_eq!(resp.replication_group_ids, vec![GROUP.to_string()]);
    let mut req = GetXClusterOutboundReplicationGroupInfoRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    let mut resp = GetXClusterOutboundReplicationGroupInfoResponsePB::new();
    h.xcluster.get_outbound_group_info(&req, &mut resp).unwrap();
    assert_eq!(resp.namespace_infos.len(), 1);
    assert_eq!(
        resp.namespace_infos[0].table_streams[TABLE].as_bytes(),
        t.stream_id()
    );
}

#[tokio::test]
async fn creation_is_refused_for_duplicates_and_unsupported_namespaces() {
    let h = Harness::new().await;
    h.create().await.unwrap();
    let e = h.create().await.unwrap_err();
    assert!(e.status.message().contains("already exists"), "{e:?}");

    let mut req = XClusterCreateOutboundReplicationGroupRequestPB::new();
    req.set_replication_group_id("rg2".into());
    req.namespace_ids.push(CQL_NS.into());
    let e = h
        .xcluster
        .create_outbound_group(&h.token, &req)
        .await
        .unwrap_err();
    // A YCQL keyspace has no eligible table.
    assert!(e.status.message().contains("at least one table"), "{e:?}");

    let mut req = XClusterCreateOutboundReplicationGroupRequestPB::new();
    req.set_replication_group_id("rg3".into());
    let e = h
        .xcluster
        .create_outbound_group(&h.token, &req)
        .await
        .unwrap_err();
    assert_eq!(
        StatusCode::from_i32(protobuf::Enum::value(&e.status.code())),
        StatusCode::InvalidArgument
    );
}

#[tokio::test]
async fn a_new_table_gets_a_stream_checkpointed_at_op_zero_before_it_runs() {
    use master_xrepl::XreplExtension;
    let h = Harness::new().await;
    h.create().await.unwrap();
    h.ready().await;
    let mut t = ysql_table("orders");
    t.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::PREPARING));
    h.fake.apply(vec![
        RowChange::Upsert {
            entry_type: SysRowEntryType::TABLE,
            id: NEW_TABLE.as_bytes().to_vec(),
            data: t.write_to_bytes().unwrap(),
        },
        RowChange::Upsert {
            entry_type: SysRowEntryType::TABLET,
            id: NEW_TABLET.as_bytes().to_vec(),
            data: tablet(NEW_TABLE).write_to_bytes().unwrap(),
        },
    ]);
    h.catalog.barrier().await;
    h.xcluster
        .post_tablet_create(NEW_TABLE.as_bytes().to_vec())
        .await
        .unwrap();
    let pb = h.group().unwrap();
    let info = &pb.namespace_infos[DB].table_infos[NEW_TABLE];
    assert!(!info.is_part_of_initial_bootstrap() && !info.is_checkpointing());
    let rows = h.host.cdc_state.lock().unwrap().clone();
    let row = rows
        .iter()
        .find(|r| r.tablet_id() == NEW_TABLET.as_bytes())
        .unwrap();
    assert_eq!(row.stream_id(), info.stream_id());
    assert_eq!(row.checkpoint.index(), 0);
    // Again: nothing more.
    h.xcluster
        .post_tablet_create(NEW_TABLE.as_bytes().to_vec())
        .await
        .unwrap();
    assert_eq!(h.host.cdc_state.lock().unwrap().len(), rows.len());

    let mut req = GetXClusterStreamsRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_namespace_id(DB.into());
    req.source_table_ids.push(NEW_TABLE.as_bytes().to_vec());
    let mut resp = GetXClusterStreamsResponsePB::new();
    h.xcluster
        .get_xcluster_streams(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert_eq!(resp.table_infos[0].xrepl_stream_id(), info.stream_id());
}

#[tokio::test]
async fn deleting_a_group_deletes_its_streams_and_row() {
    let h = Harness::new().await;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let stream_id = pb.namespace_infos[DB].table_infos[TABLE]
        .stream_id()
        .to_vec();
    let mut req = XClusterDeleteOutboundReplicationGroupRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    h.xcluster
        .delete_outbound_group(&h.token, h.deadline(), &req)
        .await
        .unwrap();
    assert!(h.group().is_none());
    assert_eq!(h.stream(&stream_id).unwrap().state(), StreamState::DELETING);
    let e = h
        .xcluster
        .delete_outbound_group(&h.token, h.deadline(), &req)
        .await
        .unwrap_err();
    assert!(e.status.message().contains("not found"), "{e:?}");
}

#[tokio::test]
async fn removing_a_namespace_deletes_its_streams() {
    let h = Harness::new().await;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let stream_id = pb.namespace_infos[DB].table_infos[TABLE]
        .stream_id()
        .to_vec();
    let mut req = XClusterRemoveNamespaceFromOutboundReplicationGroupRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_namespace_id(DB.into());
    h.xcluster
        .remove_namespace_from_outbound_group(&h.token, h.deadline(), &req)
        .await
        .unwrap();
    assert!(h.group().unwrap().namespace_infos.is_empty());
    assert_eq!(h.stream(&stream_id).unwrap().state(), StreamState::DELETING);

    // Re-adding it checkpoints it again with a new stream.
    let mut req = XClusterAddNamespaceToOutboundReplicationGroupRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_namespace_id(DB.into());
    h.xcluster
        .add_namespace_to_outbound_group(&h.token, &req)
        .await
        .unwrap();
    let pb = h.ready().await;
    assert_ne!(
        pb.namespace_infos[DB].table_infos[TABLE].stream_id(),
        stream_id.as_slice()
    );
}

#[tokio::test]
async fn repair_removes_and_adds_back_a_table() {
    let h = Harness::new().await;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let stream_id = pb.namespace_infos[DB].table_infos[TABLE]
        .stream_id()
        .to_vec();
    let mut req = RepairOutboundXClusterReplicationGroupRemoveTableRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_table_id(TABLE.as_bytes().to_vec());
    h.xcluster
        .repair_remove_table(&h.token, h.deadline(), &req)
        .await
        .unwrap();
    assert!(h.group().unwrap().namespace_infos[DB]
        .table_infos
        .is_empty());
    assert!(h
        .xcluster
        .repair_remove_table(&h.token, h.deadline(), &req)
        .await
        .is_err());

    let mut req = RepairOutboundXClusterReplicationGroupAddTableRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.set_table_id(TABLE.as_bytes().to_vec());
    req.set_stream_id(stream_id.clone());
    h.xcluster.repair_add_table(&h.token, &req).await.unwrap();
    let pb = h.group().unwrap();
    let t = &pb.namespace_infos[DB].table_infos[TABLE];
    assert_eq!(t.stream_id(), stream_id.as_slice());
    assert!(!t.is_checkpointing() && !t.is_part_of_initial_bootstrap());
    let e = h
        .xcluster
        .repair_add_table(&h.token, &req)
        .await
        .unwrap_err();
    assert!(e.status.message().contains("already exists"), "{e:?}");
}

#[tokio::test]
async fn paused_streams_reach_tservers_with_an_older_config() {
    use master_ts_manager::{ContributorContext, HeartbeatContributor};
    let h = Harness::new().await;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let stream_id = String::from_utf8(
        pb.namespace_infos[DB].table_infos[TABLE]
            .stream_id()
            .to_vec(),
    )
    .unwrap();
    let mut req = PauseResumeXClusterProducerStreamsRequestPB::new();
    req.set_is_paused(true);
    req.stream_ids.push(stream_id.clone());
    h.xcluster
        .pause_resume_producer_streams(&h.token, &req)
        .await
        .unwrap();
    let snapshot = h.catalog.snapshot();
    let config = SysClusterConfigEntryPB::new();
    let ctx = ContributorContext {
        snapshot: &snapshot,
        cluster_config: &config,
        send_universe_key_registry: false,
        term: 3,
    };
    let mut hb = TSHeartbeatRequestPB::new();
    hb.set_xcluster_config_version(0);
    let mut resp = TSHeartbeatResponsePB::new();
    h.xcluster.contribute(&hb, &ctx, &mut resp).unwrap();
    assert_eq!(resp.xcluster_config_version(), 1);
    assert!(resp
        .xcluster_producer_registry
        .paused_producer_stream_ids
        .contains_key(&stream_id));
    // A tserver with this version gets nothing.
    hb.set_xcluster_config_version(1);
    let mut resp = TSHeartbeatResponsePB::new();
    h.xcluster.contribute(&hb, &ctx, &mut resp).unwrap();
    assert!(!resp.has_xcluster_config_version());

    let mut req = PauseResumeXClusterProducerStreamsRequestPB::new();
    req.set_is_paused(false);
    h.xcluster
        .pause_resume_producer_streams(&h.token, &req)
        .await
        .unwrap();
    let config: SysXClusterConfigEntryPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::XCLUSTER_CONFIG, b"")
        .unwrap()
        .parse()
        .unwrap();
    assert!(config
        .xcluster_producer_registry
        .paused_producer_stream_ids
        .is_empty());
    assert_eq!(config.version(), 2);
}

#[tokio::test]
async fn a_dropped_table_with_streams_is_retained_until_they_expire() {
    use master_xrepl::XreplExtension;
    let h = Harness::new().await;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let stream_id = pb.namespace_infos[DB].table_infos[TABLE]
        .stream_id()
        .to_vec();
    assert!(h
        .xcluster
        .retains_dropped_table(&h.catalog.snapshot(), TABLE.as_bytes()));
    // Hidden since the start of time: past cdc_wal_retention_time_secs.
    let mut t = ysql_table("users");
    t.set_hide_state(sys_tables_entry_pb::HideState::HIDDEN);
    t.set_hide_hybrid_time(1 << 12);
    h.fake.apply(vec![RowChange::Upsert {
        entry_type: SysRowEntryType::TABLE,
        id: TABLE.as_bytes().to_vec(),
        data: t.write_to_bytes().unwrap(),
    }]);
    h.catalog.barrier().await;
    assert!(h
        .xcluster
        .retains_hidden_tablet(&h.catalog.snapshot(), TABLET.as_bytes()));
    h.xcluster
        .drop_streams_of_old_hidden_tables()
        .await
        .unwrap();
    assert_eq!(h.stream(&stream_id).unwrap().state(), StreamState::DELETING);
    assert!(!h
        .xcluster
        .retains_hidden_tablet(&h.catalog.snapshot(), TABLET.as_bytes()));
}

#[tokio::test]
async fn the_target_sets_up_replication_from_the_ready_streams() {
    let h = Harness::new().await;
    h.create().await.unwrap();
    let pb = h.ready().await;
    let stream_id = pb.namespace_infos[DB].table_infos[TABLE]
        .stream_id()
        .to_vec();
    let mut target = HostPortPB::new();
    target.set_host("target".into());
    target.set_port(7100);
    let mut req = CreateXClusterReplicationRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.target_master_addresses.push(target.clone());
    h.xcluster
        .create_xcluster_replication(&h.token, h.deadline(), &req, vec![target.clone()])
        .await
        .unwrap();
    let calls = h.target.calls.lock().unwrap().clone();
    assert_eq!(calls[0].0, "SetupUniverseReplication");
    let setup = SetupUniverseReplicationRequestPB::parse_from_bytes(&calls[0].1).unwrap();
    assert!(setup.transactional());
    assert_eq!(setup.producer_table_ids, vec![TABLE.to_string()]);
    assert_eq!(
        setup.producer_bootstrap_ids[0].as_bytes(),
        stream_id.as_slice()
    );
    assert_eq!(setup.producer_namespaces[0].name(), "db1");
    let target_info = h.group().unwrap().target_universe_info.unwrap();
    assert_eq!(target_info.universe_uuid(), b"target-uuid");
    assert_eq!(target_info.state(), TargetState::CREATING_REPLICATION_GROUP);

    let mut req = IsCreateXClusterReplicationDoneRequestPB::new();
    req.set_replication_group_id(GROUP.into());
    req.target_master_addresses.push(target);
    let mut resp = IsCreateXClusterReplicationDoneResponsePB::new();
    h.xcluster
        .is_create_xcluster_replication_done(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert!(!resp.done());
    *h.target.done.lock().unwrap() = true;
    let mut resp = IsCreateXClusterReplicationDoneResponsePB::new();
    h.xcluster
        .is_create_xcluster_replication_done(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert!(resp.done());
    assert_eq!(
        h.group().unwrap().target_universe_info.state(),
        TargetState::REPLICATING
    );
    let _ = &h.xrepl;
}
