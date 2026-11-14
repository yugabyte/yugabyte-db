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

//! Tests of the stream RPCs against a fake sys catalog.

use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::{run_materializer, Catalog, CatalogWriter, WriterOptions};
use master_leadership::{FakeLeaderState, LeaderToken, Leadership, Phase};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_table_lifecycle::SchemaHost;
use protobuf::{EnumOrUnknown, Message};
use tokio::time::Instant;
use yb_host_api::Status;
use yb_proto::pb::catalog_entity_info::{
    sys_cdcstream_entry_pb::State as StreamState, CDCStreamOptionsPB, SysCDCStreamEntryPB,
    SysClusterConfigEntryPB, SysTablesEntryPB,
};
use yb_proto::pb::master_replication::{
    update_cdcstream_request_pb::StreamUpdateInfo, GetCDCDBStreamInfoRequestPB,
    GetCDCDBStreamInfoResponsePB, GetCDCStreamRequestPB, GetCDCStreamResponsePB, IdTypePB,
    ListCDCStreamsRequestPB, ListCDCStreamsResponsePB, UpdateCDCStreamRequestPB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, SysRowEntryType};

use master_tablet_locations::Replica;
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysNamespaceEntryPB,
    SysTabletsEntryPB,
};
use yb_proto::pb::common::{ColumnSchemaPB, PartitionPB};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::{CDCSDKSnapshotOption, PeerRole, TableType, YQLDatabase};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_heartbeat::TSInformationPB;
use yb_proto::pb::master_rust_peer::{
    CdcStateEntryPB, CdcStateReadRequestPB, CdcStateReadResponsePB, CdcStateUpsertRequestPB,
    HybridTimeNowResponsePB,
};
use yb_proto::pb::operations::ChangeMetadataRequestPB;
use yb_proto::pb::tserver_admin::ChangeMetadataResponsePB;

use crate::{Xrepl, XreplOptions};

/// A YSQL database with one user table on one tablet, which ts0 leads.
const YSQL_DB: &str = "00004000000030008000000000000000";
const YSQL_TABLE: &str = "00004000000030008000000000004000";
const YSQL_TABLET: &str = "tab-ysql";

fn ysql_rows() -> Vec<RowChange> {
    let mut ns = SysNamespaceEntryPB::new();
    ns.set_name("db1".into());
    ns.database_type = Some(EnumOrUnknown::new(YQLDatabase::YQL_DATABASE_PGSQL));
    ns.state = Some(EnumOrUnknown::new(sys_namespace_entry_pb::State::RUNNING));
    let mut t = SysTablesEntryPB::new();
    t.set_name("users".into());
    t.set_namespace_id(YSQL_DB.as_bytes().to_vec());
    t.table_type = Some(EnumOrUnknown::new(TableType::PGSQL_TABLE_TYPE));
    t.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    t.set_version(3);
    let mut key = ColumnSchemaPB::new();
    key.set_name("id".into());
    key.set_is_key(true);
    key.set_order(1);
    let mut ty = yb_proto::pb::common::QLTypePB::new();
    ty.main = Some(EnumOrUnknown::new(
        yb_proto::pb::value::PersistentDataType::INT32,
    ));
    key.type_ = protobuf::MessageField::some(ty);
    t.schema.mut_or_insert_default().columns.push(key);
    t.partition_schema = protobuf::MessageField::some(Default::default());
    let mut tablet = SysTabletsEntryPB::new();
    tablet.set_table_id(YSQL_TABLE.as_bytes().to_vec());
    tablet.partition = protobuf::MessageField::some(PartitionPB::new());
    tablet.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
    let mut cdc_state = SysTablesEntryPB::new();
    cdc_state.set_name("cdc_state".into());
    cdc_state.set_namespace_id(b"00000000000000000000000000000001".to_vec());
    cdc_state.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
    cdc_state.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
    cdc_state.set_version(0);
    cdc_state.schema = protobuf::MessageField::some(Default::default());
    cdc_state.partition_schema = protobuf::MessageField::some(Default::default());
    let row = |entry_type, id: &str, data: Vec<u8>| RowChange::Upsert {
        entry_type,
        id: id.as_bytes().to_vec(),
        data,
    };
    vec![
        row(
            SysRowEntryType::NAMESPACE,
            YSQL_DB,
            ns.write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLE,
            YSQL_TABLE,
            t.write_to_bytes().unwrap(),
        ),
        row(
            SysRowEntryType::TABLET,
            YSQL_TABLET,
            tablet.write_to_bytes().unwrap(),
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

const XCLUSTER: &str = "11111111111141118111111111111111";
const CDCSDK: &str = "22222222222242228222222222222222";
const DELETED: &str = "33333333333343338333333333333333";

fn stream(tables: &[&str], namespace: Option<&str>, state: StreamState) -> Vec<u8> {
    let mut pb = SysCDCStreamEntryPB::new();
    pb.table_id = tables.iter().map(|t| t.to_string()).collect();
    if let Some(ns) = namespace {
        pb.set_namespace_id(ns.to_string());
        let mut o = CDCStreamOptionsPB::new();
        o.set_key("id_type".into());
        o.set_value(b"NAMESPACEID".to_vec());
        pb.options.push(o);
        pb.set_cdcsdk_ysql_replication_slot_name("slot1".into());
    }
    pb.state = Some(EnumOrUnknown::new(state));
    pb.write_to_bytes().unwrap()
}

fn table() -> Vec<u8> {
    let mut t = SysTablesEntryPB::new();
    t.set_name("t".into());
    t.set_namespace_id(b"ns1".to_vec());
    t.set_version(1);
    t.schema = protobuf::MessageField::some(Default::default());
    t.partition_schema = protobuf::MessageField::some(Default::default());
    t.write_to_bytes().unwrap()
}

/// Records the host calls and keeps the `cdc_state` rows they write.
#[derive(Default)]
struct FakeHost {
    calls: Mutex<Vec<(String, Vec<u8>)>>,
    cdc_state: Mutex<Vec<CdcStateEntryPB>>,
}

impl SchemaHost for FakeHost {
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status> {
        self.calls
            .lock()
            .unwrap()
            .push((method.to_string(), request.to_vec()));
        match method {
            "CdcStateUpsert" => {
                let req = CdcStateUpsertRequestPB::parse_from_bytes(request).unwrap();
                let mut rows = self.cdc_state.lock().unwrap();
                for e in req.entries {
                    let key = |r: &CdcStateEntryPB| {
                        (
                            r.tablet_id().to_vec(),
                            r.stream_id().to_vec(),
                            r.colocated_table_id().to_vec(),
                        )
                    };
                    match rows.iter_mut().find(|r| key(r) == key(&e)) {
                        Some(row) => {
                            let mut merged = row.clone();
                            merged
                                .merge_from_bytes(&e.write_to_bytes().unwrap())
                                .unwrap();
                            *row = merged;
                        }
                        None => rows.push(e),
                    }
                }
                Ok(Vec::new())
            }
            "CdcStateRead" => {
                let req = CdcStateReadRequestPB::parse_from_bytes(request).unwrap();
                let mut resp = CdcStateReadResponsePB::new();
                resp.entries = self
                    .cdc_state
                    .lock()
                    .unwrap()
                    .iter()
                    .filter(|r| r.stream_id() == req.stream_id())
                    .cloned()
                    .collect();
                Ok(resp.write_to_bytes().unwrap())
            }
            "HybridTimeNow" => {
                let mut resp = HybridTimeNowResponsePB::new();
                resp.set_hybrid_time(5_000 << 12);
                Ok(resp.write_to_bytes().unwrap())
            }
            _ => Ok(Vec::new()),
        }
    }
}

struct Harness {
    xrepl: Arc<Xrepl>,
    host: Arc<FakeHost>,
    alters: Arc<Mutex<Vec<ChangeMetadataRequestPB>>>,
    catalog: Arc<Catalog>,
    token: LeaderToken,
}

impl Harness {
    async fn new() -> Harness {
        let (fake, events) = FakeSysCatalog::new();
        let row = |entry_type, id: &str, data| RowChange::Upsert {
            entry_type,
            id: id.as_bytes().to_vec(),
            data,
        };
        fake.apply(ysql_rows());
        fake.apply(vec![
            RowChange::Upsert {
                entry_type: SysRowEntryType::CLUSTER_CONFIG,
                id: Vec::new(),
                data: SysClusterConfigEntryPB::new().write_to_bytes().unwrap(),
            },
            row(SysRowEntryType::TABLE, "t1", table()),
            row(
                SysRowEntryType::CDC_STREAM,
                XCLUSTER,
                stream(&["t1"], None, StreamState::ACTIVE),
            ),
            row(
                SysRowEntryType::CDC_STREAM,
                CDCSDK,
                stream(&["t1", "t2"], Some("ns1"), StreamState::ACTIVE),
            ),
            row(
                SysRowEntryType::CDC_STREAM,
                DELETED,
                stream(&["t1"], None, StreamState::DELETING),
            ),
        ]);
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
        ts_manager.locations.update_replica(
            YSQL_TABLET.as_bytes(),
            Replica::new(
                b"ts0".as_slice().into(),
                PeerRole::LEADER,
                PeerMemberType::VOTER,
            ),
        );
        let alters: Arc<Mutex<Vec<ChangeMetadataRequestPB>>> = Arc::default();
        let record = alters.clone();
        let rpc = Arc::new(yb_host_api::rpc::FakeRpcClient::new(
            move |_, _, service, method, req| {
                assert_eq!(service, "yb.tserver.TabletServerAdminService");
                assert_eq!(method, "AlterSchema");
                let req = ChangeMetadataRequestPB::parse_from_bytes(req).unwrap();
                record.lock().unwrap().push(req.clone());
                let mut resp = ChangeMetadataResponsePB::new();
                // A retention barrier answers with the op id the snapshot is safe from.
                if req.has_retention_requester_id() {
                    let safe = resp.cdc_sdk_snapshot_safe_op_id.mut_or_insert_default();
                    safe.set_term(1);
                    safe.set_index(7);
                    resp.set_propagated_hybrid_time(4_000 << 12);
                }
                Ok(resp.write_to_bytes().unwrap())
            },
        ));
        let xrepl = Xrepl::new(
            catalog.clone(),
            writer,
            leadership.clone(),
            ts_manager,
            host.clone(),
            master_task_runtime::TsRpcExecutor::new(|_| usize::MAX),
            Default::default(),
            XreplOptions::default(),
        );
        xrepl.set_rpc(rpc);
        Harness {
            xrepl,
            host,
            catalog,
            token,
            alters,
        }
    }

    fn deadline(&self) -> Instant {
        Instant::now() + Duration::from_secs(30)
    }
}

#[tokio::test]
async fn get_finds_a_stream_by_id_or_slot_and_not_a_deleting_one() {
    let h = Harness::new().await;
    let get = |req: GetCDCStreamRequestPB| {
        let mut resp = GetCDCStreamResponsePB::new();
        h.xrepl.get_cdc_stream(&req, &mut resp).map(|_| resp)
    };
    let mut req = GetCDCStreamRequestPB::new();
    // The id in upper case names the same stream.
    req.set_stream_id(CDCSDK.to_uppercase().into_bytes());
    let resp = get(req).unwrap();
    assert_eq!(resp.stream.stream_id(), CDCSDK.as_bytes());
    assert_eq!(resp.stream.namespace_id(), b"ns1");
    assert_eq!(resp.stream.table_id.len(), 2);
    assert!(resp
        .stream
        .options
        .iter()
        .any(|o| o.key() == "state" && o.value() == b"ACTIVE"));
    // A slot's stream has its LSN type and ordering mode, with their defaults.
    assert!(resp.stream.cdc_stream_info_options.is_some());

    let mut req = GetCDCStreamRequestPB::new();
    req.set_cdcsdk_ysql_replication_slot_name("slot1".into());
    assert_eq!(get(req).unwrap().stream.stream_id(), CDCSDK.as_bytes());

    let mut req = GetCDCStreamRequestPB::new();
    req.set_stream_id(DELETED.as_bytes().to_vec());
    assert_eq!(get(req).unwrap_err().code(), Code::OBJECT_NOT_FOUND);

    let mut req = GetCDCStreamRequestPB::new();
    req.set_stream_id(b"not-a-uuid".to_vec());
    assert!(get(req).is_err());

    assert_eq!(
        get(GetCDCStreamRequestPB::new()).unwrap_err().code(),
        Code::INVALID_REQUEST
    );
}

#[tokio::test]
async fn list_filters_by_id_type_namespace_and_first_table() {
    let h = Harness::new().await;
    let list = |req: ListCDCStreamsRequestPB| {
        let mut resp = ListCDCStreamsResponsePB::new();
        h.xrepl.list_cdc_streams(&req, &mut resp).map(|_| {
            resp.streams
                .iter()
                .map(|s| String::from_utf8(s.stream_id().to_vec()).unwrap())
                .collect::<Vec<_>>()
        })
    };
    // TABLE_ID by default: the xCluster stream; the deleting one is left out.
    assert_eq!(
        list(ListCDCStreamsRequestPB::new()).unwrap(),
        vec![XCLUSTER]
    );
    let mut req = ListCDCStreamsRequestPB::new();
    req.set_id_type(IdTypePB::NAMESPACE_ID);
    assert_eq!(list(req.clone()).unwrap(), vec![CDCSDK]);
    req.set_namespace_id("other".into());
    assert!(list(req).unwrap().is_empty());
    let mut req = ListCDCStreamsRequestPB::new();
    req.set_table_id("missing".into());
    assert_eq!(list(req).unwrap_err().code(), Code::OBJECT_NOT_FOUND);
}

#[tokio::test]
async fn db_stream_info_lists_the_streams_tables() {
    let h = Harness::new().await;
    let mut req = GetCDCDBStreamInfoRequestPB::new();
    req.set_db_stream_id(CDCSDK.as_bytes().to_vec());
    let mut resp = GetCDCDBStreamInfoResponsePB::new();
    h.xrepl.get_cdc_db_stream_info(&req, &mut resp).unwrap();
    assert_eq!(resp.namespace_id(), b"ns1");
    assert_eq!(resp.table_info.len(), 2);
    assert_eq!(resp.table_info[1].table_id(), b"t2");
}

#[tokio::test]
async fn update_replaces_entries_but_keeps_the_xcluster_flags() {
    let h = Harness::new().await;
    // An update cannot set the xCluster flags: the stored ones are kept.
    let mut flagged =
        SysCDCStreamEntryPB::parse_from_bytes(&stream(&["t1"], None, StreamState::ACTIVE)).unwrap();
    flagged.set_xcluster_use_target_applied_filter(true);
    let mut req = UpdateCDCStreamRequestPB::new();
    let mut info = StreamUpdateInfo::new();
    info.set_stream_id(XCLUSTER.as_bytes().to_vec());
    info.entry = Some(flagged).into();
    req.streams.push(info);
    h.xrepl
        .update_cdc_stream(&h.token, h.deadline(), &req)
        .await
        .unwrap();

    let mut entry =
        SysCDCStreamEntryPB::parse_from_bytes(&stream(&["t1"], None, StreamState::INITIATED))
            .unwrap();
    let mut state = CDCStreamOptionsPB::new();
    state.set_key("state".into());
    state.set_value(b"ACTIVE".to_vec());
    entry.options.push(state);
    let mut req = UpdateCDCStreamRequestPB::new();
    req.set_stream_id(XCLUSTER.as_bytes().to_vec());
    req.entry = Some(entry).into();
    h.xrepl
        .update_cdc_stream(&h.token, h.deadline(), &req)
        .await
        .unwrap();
    h.catalog.barrier().await;
    let row: SysCDCStreamEntryPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::CDC_STREAM, XCLUSTER.as_bytes())
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(row.state(), StreamState::INITIATED);
    assert!(!row.xcluster_use_target_applied_filter());
    assert!(row.options.iter().all(|o| o.key() != "state"));

    let mut req = UpdateCDCStreamRequestPB::new();
    req.set_stream_id(DELETED.as_bytes().to_vec());
    req.entry = Some(SysCDCStreamEntryPB::new()).into();
    assert_eq!(
        h.xrepl
            .update_cdc_stream(&h.token, h.deadline(), &req)
            .await
            .unwrap_err()
            .code(),
        Code::OBJECT_NOT_FOUND
    );
}

#[tokio::test]
async fn deleted_streams_lose_their_state_rows_then_their_rows() {
    use yb_proto::pb::master_replication::{DeleteCDCStreamRequestPB, DeleteCDCStreamResponsePB};
    use yb_proto::pb::master_rust_peer::CdcStateDeleteStreamsRequestPB;
    let h = Harness::new().await;
    let mut req = DeleteCDCStreamRequestPB::new();
    req.stream_id.push(XCLUSTER.as_bytes().to_vec());
    req.stream_id
        .push(b"44444444444444448444444444444444".to_vec());
    let mut resp = DeleteCDCStreamResponsePB::new();
    // A missing stream fails the request unless errors are ignored.
    assert_eq!(
        h.xrepl
            .delete_cdc_stream(&h.token, h.deadline(), &req, &mut resp)
            .await
            .unwrap_err()
            .code(),
        Code::OBJECT_NOT_FOUND
    );
    req.set_ignore_errors(true);
    let mut resp = DeleteCDCStreamResponsePB::new();
    h.xrepl
        .delete_cdc_stream(&h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert_eq!(resp.not_found_stream_ids.len(), 1);
    h.catalog.barrier().await;
    let state = |id: &str| {
        h.catalog
            .snapshot()
            .get(SysRowEntryType::CDC_STREAM, id.as_bytes())
            .map(|e| e.parse::<SysCDCStreamEntryPB>().unwrap().state())
    };
    assert_eq!(state(XCLUSTER), Some(StreamState::DELETING));

    h.xrepl.clean_up_deleted_streams(&h.token).await.unwrap();
    h.catalog.barrier().await;
    // Both streams that were DELETING are gone, after their cdc_state rows.
    assert_eq!(state(XCLUSTER), None);
    assert_eq!(state(DELETED), None);
    assert_eq!(state(CDCSDK), Some(StreamState::ACTIVE));
    let calls = h.host.calls.lock().unwrap();
    assert_eq!(calls.len(), 1);
    assert_eq!(calls[0].0, "CdcStateDeleteStreams");
    let sent = CdcStateDeleteStreamsRequestPB::parse_from_bytes(&calls[0].1).unwrap();
    assert_eq!(sent.stream_ids.len(), 2);
}

/// Never asked: the harness has `system.cdc_state`.
struct NoTables;

impl crate::CreatesTables for NoTables {
    fn create<'a>(
        &'a self,
        _: &'a LeaderToken,
        _: Instant,
        _: &'a yb_proto::pb::master_ddl::CreateTableRequestPB,
        _: &'a mut yb_proto::pb::master_ddl::CreateTableResponsePB,
    ) -> master_task_runtime::BoxFuture<'a, Result<(), yb_proto::pb::master_types::MasterErrorPB>>
    {
        panic!("cdc_state exists")
    }
}

fn cdcsdk_request() -> yb_proto::pb::master_replication::CreateCDCStreamRequestPB {
    let mut req = yb_proto::pb::master_replication::CreateCDCStreamRequestPB::new();
    req.set_namespace_id(YSQL_DB.into());
    for (k, v) in [
        ("source_type", "CDCSDK"),
        ("id_type", "NAMESPACEID"),
        ("record_type", "CHANGE"),
    ] {
        let mut o = CDCStreamOptionsPB::new();
        o.set_key(k.into());
        o.set_value(v.as_bytes().to_vec());
        req.options.push(o);
    }
    req
}

#[tokio::test]
async fn a_cdcsdk_stream_gets_its_tables_state_rows_and_wal_retention() {
    use yb_proto::pb::master_replication::CreateCDCStreamResponsePB;
    let h = Harness::new().await;
    let mut resp = CreateCDCStreamResponsePB::new();
    h.xrepl
        .create_cdc_stream(
            &NoTables,
            &h.token,
            h.deadline(),
            &cdcsdk_request(),
            &mut resp,
        )
        .await
        .unwrap();
    h.catalog.barrier().await;
    let id = String::from_utf8(resp.stream_id().to_vec()).unwrap();
    let snapshot = h.catalog.snapshot();
    let row: SysCDCStreamEntryPB = snapshot
        .get(SysRowEntryType::CDC_STREAM, id.as_bytes())
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(row.state(), StreamState::ACTIVE);
    assert_eq!(row.table_id, vec![YSQL_TABLE.to_string()]);
    assert_eq!(row.namespace_id(), YSQL_DB);
    // The table keeps WAL for the stream; its tablet was told so.
    let table: SysTablesEntryPB = snapshot
        .get(SysRowEntryType::TABLE, YSQL_TABLE.as_bytes())
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(table.wal_retention_secs(), 4 * 3600);
    let alters = h.alters.lock().unwrap();
    assert_eq!(alters.len(), 1);
    assert_eq!(alters[0].tablet_id(), YSQL_TABLET.as_bytes());
    assert_eq!(alters[0].schema_version(), 3);
    assert!(!alters[0].has_retention_requester_id());
    // One row per tablet, with an invalid checkpoint until the tablet reports.
    let rows = h.host.cdc_state.lock().unwrap();
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0].checkpoint.term(), -1);
}

#[tokio::test]
async fn a_consistent_snapshot_stream_records_each_tablets_safe_op_id_and_one_snapshot_time() {
    use yb_proto::pb::master_replication::CreateCDCStreamResponsePB;
    let h = Harness::new().await;
    let mut req = cdcsdk_request();
    req.cdcsdk_consistent_snapshot_option =
        Some(EnumOrUnknown::new(CDCSDKSnapshotOption::USE_SNAPSHOT));
    let mut resp = CreateCDCStreamResponsePB::new();
    h.xrepl
        .create_cdc_stream(&NoTables, &h.token, h.deadline(), &req, &mut resp)
        .await
        .unwrap();
    h.catalog.barrier().await;
    let id = String::from_utf8(resp.stream_id().to_vec()).unwrap();
    assert_eq!(resp.cdcsdk_consistent_snapshot_time(), 5_000 << 12);
    let row: SysCDCStreamEntryPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::CDC_STREAM, id.as_bytes())
        .unwrap()
        .parse()
        .unwrap();
    // INITIATED until the snapshot time is set.
    assert_eq!(row.state(), StreamState::ACTIVE);
    assert_eq!(row.cdcsdk_stream_metadata.snapshot_time(), 5_000 << 12);
    let alters = h.alters.lock().unwrap();
    assert_eq!(alters[0].retention_requester_id(), id.as_bytes());
    assert!(alters[0].cdc_sdk_require_history_cutoff());
    let rows = h.host.cdc_state.lock().unwrap();
    assert_eq!(rows.len(), 1);
    assert_eq!(rows[0].checkpoint.index(), 7);
    assert_eq!(rows[0].cdc_sdk_safe_time(), 5_000 << 12);
    assert!(rows[0].has_snapshot_key());
}

#[tokio::test]
async fn a_cdcsdk_request_is_validated() {
    use yb_proto::pb::master_replication::CreateCDCStreamResponsePB;
    let h = Harness::new().await;
    let mut req = cdcsdk_request();
    req.options.retain(|o| o.key() != "id_type");
    let mut o = CDCStreamOptionsPB::new();
    o.set_key("id_type".into());
    o.set_value(b"TABLEID".to_vec());
    req.options.push(o);
    req.set_table_id("t1".into());
    // A table stream is xCluster's.
    let mut resp = CreateCDCStreamResponsePB::new();
    assert!(h
        .xrepl
        .create_cdc_stream(&NoTables, &h.token, h.deadline(), &req, &mut resp)
        .await
        .is_err());
    let mut req = cdcsdk_request();
    req.clear_namespace_id();
    assert_eq!(
        h.xrepl
            .create_cdc_stream(&NoTables, &h.token, h.deadline(), &req, &mut resp)
            .await
            .unwrap_err()
            .code(),
        Code::INVALID_REQUEST
    );
}

#[tokio::test]
async fn dropped_tables_leave_cdcsdk_streams() {
    let h = Harness::new().await;
    // t2 has no row: it was dropped.
    h.xrepl
        .clean_up_cdcsdk_streams_metadata(&h.token)
        .await
        .unwrap();
    h.catalog.barrier().await;
    let row: SysCDCStreamEntryPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::CDC_STREAM, CDCSDK.as_bytes())
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(row.table_id, vec!["t1".to_string()]);
    assert!(row.dropped_table_id.is_empty());
    // The xCluster stream is not a CDCSDK stream and keeps its table.
    let row: SysCDCStreamEntryPB = h
        .catalog
        .snapshot()
        .get(SysRowEntryType::CDC_STREAM, XCLUSTER.as_bytes())
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(row.table_id, vec!["t1".to_string()]);
}
