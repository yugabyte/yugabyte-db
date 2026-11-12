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

use std::sync::Arc;
use std::time::Duration;

use master_catalog_state::{
    run_materializer, Catalog, CatalogSnapshot, CatalogWriter, WriterOptions,
};
use master_leadership::{FakeLeaderState, LeaderToken, Leadership, Phase};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_ts_manager::{TsManager, TsManagerOptions};
use protobuf::{EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_namespace_entry_pb, sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB,
    SysNamespaceEntryPB, SysTablesEntryPB, SysTabletsEntryPB,
};
use yb_proto::pb::common::{PartitionPB, SchemaPB};
use yb_proto::pb::common_net::{CloudInfoPB, HostPortPB, PlacementBlockPB, PlacementInfoPB};
use yb_proto::pb::common_types::{TableType, YQLDatabase};
use yb_proto::pb::master_ddl::{
    alter_table_request_pb, AlterTableRequestPB, AlterTableResponsePB, CreateNamespaceRequestPB,
    CreateNamespaceResponsePB, CreateTableRequestPB, CreateTableResponsePB,
    DeleteNamespaceRequestPB, DeleteTableRequestPB, DeleteTableResponsePB, GetTableSchemaRequestPB,
    GetTableSchemaResponsePB, IsAlterTableDoneRequestPB, IsAlterTableDoneResponsePB,
    IsDeleteTableDoneRequestPB, IsDeleteTableDoneResponsePB, ListNamespacesRequestPB,
    ListNamespacesResponsePB, ListTablesRequestPB, ListTablesResponsePB,
};
use yb_proto::pb::master_heartbeat::TSInformationPB;
use yb_proto::pb::master_rust_peer::{
    AlterTableSchemaRequestPB, AlterTableSchemaResponsePB, PrepareTableSchemaRequestPB,
    PrepareTableSchemaResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::lookup::{database_type, is_colocation_parent_table_id};
use crate::placement::{check_placement, select_replicas, Load};
use crate::{generate_object_id, LifecycleOptions, SchemaHost, TableLifecycle};

/// The host's schema steps, simplified: the request's schema as is, and `num_tablets` (default
/// one per tserver) equal partitions of a one-byte key.
struct FakeSchemaHost;

impl SchemaHost for FakeSchemaHost {
    fn call(&self, method: &'static str, request: &[u8]) -> Result<Vec<u8>, Status> {
        match method {
            "PrepareTableSchema" => {
                let req = PrepareTableSchemaRequestPB::parse_from_bytes(request).unwrap();
                let mut resp = PrepareTableSchemaResponsePB::new();
                resp.schema = req.request.schema.clone();
                if req.tablegroup_parent() {
                    // The host builds the parent's schema.
                    resp.schema = MessageField::some(Default::default());
                }
                resp.set_next_column_id(req.request.schema.columns.len() as i32);
                let n = if req.colocated() {
                    1
                } else if req.request.has_num_tablets() {
                    req.request.num_tablets()
                } else {
                    req.num_live_tservers() as i32
                };
                resp.set_num_tablets(n);
                if n <= 0 {
                    resp.partitions_error = MessageField::some(crate::error(
                        Code::UNKNOWN_ERROR,
                        StatusCode::InvalidArgument,
                        "num_tablets should be greater than 0",
                    ));
                }
                for i in 0..n.max(0) {
                    let mut p = PartitionPB::new();
                    if i > 0 {
                        p.set_partition_key_start(vec![(i * 256 / n) as u8]);
                    }
                    if i < n - 1 {
                        p.set_partition_key_end(vec![((i + 1) * 256 / n) as u8]);
                    }
                    resp.partitions.push(p);
                }
                if req.has_namespace_id() {
                    // FillNewTableEntry.
                    let table = resp.table.mut_or_insert_default();
                    table.set_name(req.request.name().as_bytes().to_vec());
                    table.table_type = req.request.table_type;
                    table.set_namespace_id(req.namespace_id().to_vec());
                    table.set_namespace_name(req.namespace_name().to_vec());
                    table.set_version(0);
                    table.schema = resp.schema.clone();
                    table.set_state(sys_tables_entry_pb::State::PREPARING);
                    table.set_colocated(req.colocated());
                    if req.request.index_info.is_some() {
                        table.index_info = req.request.index_info.clone();
                        table.set_indexed_table_id(
                            req.request.index_info.indexed_table_id().to_vec(),
                        );
                    }
                }
                Ok(resp.write_to_bytes().unwrap())
            }
            "AlterTableSchema" => {
                // Adds the request's columns, numbered from next_column_id.
                let req = AlterTableSchemaRequestPB::parse_from_bytes(request).unwrap();
                let mut resp = AlterTableSchemaResponsePB::new();
                let mut schema = req.table.schema.clone().unwrap_or_default();
                let mut next = req.table.next_column_id();
                for step in &req.request.alter_schema_steps {
                    let mut column = step.add_column.schema.clone().unwrap_or_default();
                    column.set_id(next as u32);
                    next += 1;
                    schema.columns.push(column);
                }
                resp.schema = MessageField::some(schema);
                resp.set_next_column_id(next);
                Ok(resp.write_to_bytes().unwrap())
            }
            _ => Err(Status::new(StatusCode::NotSupported, method)),
        }
    }
}

struct Harness {
    lifecycle: Arc<TableLifecycle>,
    catalog: Arc<Catalog>,
    token: LeaderToken,
    ts_manager: Arc<TsManager>,
}

impl Harness {
    async fn new(tservers: usize) -> Harness {
        let (fake, events) = FakeSysCatalog::new();
        let mut config = SysClusterConfigEntryPB::new();
        config.set_version(1);
        fake.apply(vec![RowChange::Upsert {
            entry_type: SysRowEntryType::CLUSTER_CONFIG,
            id: Vec::new(),
            data: config.write_to_bytes().unwrap(),
        }]);
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
        let writer = CatalogWriter::start(
            catalog.clone(),
            fake.clone(),
            leadership.clone(),
            WriterOptions::default(),
        );
        let ts_manager = TsManager::new(Arc::default(), TsManagerOptions::default());
        let infos: Vec<(TSInformationPB, bool)> = (0..tservers)
            .map(|i| (tserver(&format!("ts{i}"), "zone"), true))
            .collect();
        ts_manager.apply_peer_tservers(&infos, true);
        let lifecycle = TableLifecycle::new(
            catalog.clone(),
            writer,
            leadership,
            ts_manager.clone(),
            Arc::new(FakeSchemaHost),
            LifecycleOptions::default(),
        );
        Harness {
            lifecycle,
            catalog,
            token,
            ts_manager,
        }
    }

    fn deadline() -> Instant {
        Instant::now() + Duration::from_secs(30)
    }

    async fn create_namespace(&self, name: &str) -> Result<Vec<u8>, MasterErrorPB> {
        let mut req = CreateNamespaceRequestPB::new();
        req.set_name(name.into());
        let mut resp = CreateNamespaceResponsePB::new();
        self.lifecycle
            .create_namespace(&self.token, Self::deadline(), &req, &mut resp)
            .await
            .map(|()| resp.id().to_vec())
    }

    async fn create_table(
        &self,
        namespace: &str,
        name: &str,
        num_tablets: Option<i32>,
    ) -> (Result<(), MasterErrorPB>, CreateTableResponsePB) {
        let mut req = CreateTableRequestPB::new();
        req.set_name(name.into());
        req.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
        req.namespace
            .mut_or_insert_default()
            .set_name(namespace.into());
        req.schema = MessageField::some(SchemaPB::new());
        if let Some(n) = num_tablets {
            req.set_num_tablets(n);
        }
        let mut resp = CreateTableResponsePB::new();
        let result = self
            .lifecycle
            .create_table(&self.token, Self::deadline(), &req, &mut resp)
            .await;
        (result, resp)
    }

    fn snapshot(&self) -> Arc<CatalogSnapshot> {
        self.catalog.snapshot()
    }
}

fn tserver(uuid: &str, zone: &str) -> TSInformationPB {
    let mut info = TSInformationPB::new();
    info.tserver_instance
        .mut_or_insert_default()
        .set_permanent_uuid(uuid.as_bytes().to_vec());
    let common = info
        .registration
        .mut_or_insert_default()
        .common
        .mut_or_insert_default();
    let mut hp = HostPortPB::new();
    hp.set_host(format!("{uuid}.private"));
    hp.set_port(9100);
    common.private_rpc_addresses.push(hp.clone());
    hp.set_host(format!("{uuid}.public"));
    common.broadcast_addresses.push(hp);
    let ci = common.cloud_info.mut_or_insert_default();
    ci.set_placement_cloud("c".into());
    ci.set_placement_region("r".into());
    ci.set_placement_zone(zone.into());
    info
}

#[test]
fn object_ids_are_uuid_v4_hex() {
    let id = generate_object_id();
    assert_eq!(id.len(), 32);
    assert!(id
        .iter()
        .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase()));
    assert_eq!(id[12], b'4');
    assert_ne!(generate_object_id(), id);
}

#[test]
fn database_type_and_colocation_ids_follow_cpp() {
    let mut ident = yb_proto::pb::master_types::NamespaceIdentifierPB::new();
    ident.set_name("redis".into());
    assert_eq!(database_type(&ident), YQLDatabase::YQL_DATABASE_CQL);
    ident.set_name("system_redis".into());
    assert_eq!(database_type(&ident), YQLDatabase::YQL_DATABASE_REDIS);
    ident.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    assert_eq!(database_type(&ident), YQLDatabase::YQL_DATABASE_PGSQL);
    let id = "0123456789abcdef0123456789abcdef";
    assert!(is_colocation_parent_table_id(
        format!("{id}.tablegroup.parent.uuid").as_bytes()
    ));
    assert!(!is_colocation_parent_table_id(b"x.tablegroup.parent.uuid"));
}

/// `ReservePgsqlOids`: consecutive reservations do not overlap, a client's `next_oid` moves the
/// start forward, and the secondary space starts after the normal one.
#[tokio::test]
async fn pgsql_oids_are_reserved_in_order() {
    let h = Harness::new(3).await;
    let id = h.create_namespace("db").await.unwrap();
    let reserve = |next_oid: u32, count: u32, secondary: bool| {
        let mut req = yb_proto::pb::master_client::ReservePgsqlOidsRequestPB::new();
        req.set_namespace_id(id.clone());
        req.set_next_oid(next_oid);
        req.set_count(count);
        req.set_use_secondary_space(secondary);
        let lifecycle = h.lifecycle.clone();
        let token = h.token;
        async move {
            let mut resp = yb_proto::pb::master_client::ReservePgsqlOidsResponsePB::new();
            lifecycle
                .reserve_pgsql_oids(&token, Harness::deadline(), &req, &mut resp)
                .await
                .map(|()| (resp.begin_oid(), resp.end_oid()))
        }
    };
    assert_eq!(reserve(16384, 256, false).await.unwrap(), (16384, 16640));
    assert_eq!(reserve(16384, 256, false).await.unwrap(), (16640, 16896));
    assert_eq!(reserve(20000, 10, false).await.unwrap(), (20000, 20010));
    assert_eq!(
        reserve(0, 5, true).await.unwrap(),
        (2_200_000_000, 2_200_000_005)
    );
    // The normal space ends at kPgUpperBoundNormalObjectId.
    assert_eq!(
        reserve(2_199_999_990, 100, false).await.unwrap(),
        (2_199_999_990, 2_199_999_999)
    );
    assert!(reserve(0, 1, false).await.is_err());
}

#[tokio::test]
async fn keyspaces_are_created_listed_and_dropped() {
    let h = Harness::new(3).await;
    let id = h.create_namespace("ks").await.unwrap();
    let ns: SysNamespaceEntryPB = h
        .snapshot()
        .get(SysRowEntryType::NAMESPACE, &id)
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(ns.state(), sys_namespace_entry_pb::State::RUNNING);
    assert_eq!(ns.database_type(), YQLDatabase::YQL_DATABASE_CQL);

    // A duplicate name answers with the existing id.
    let mut req = CreateNamespaceRequestPB::new();
    req.set_name("ks".into());
    let mut resp = CreateNamespaceResponsePB::new();
    let e = h
        .lifecycle
        .create_namespace(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::NAMESPACE_ALREADY_PRESENT);
    assert_eq!(resp.id(), id);

    // A YSQL database without a template has no tables to copy: PREPARING, then RUNNING.
    req.set_name("db".into());
    req.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    req.set_namespace_id(b"00004000000030008000000000000000".to_vec());
    h.lifecycle
        .create_namespace(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap();
    let mut running = false;
    for _ in 0..100 {
        let ns = h.snapshot();
        let ns = ns
            .get(SysRowEntryType::NAMESPACE, resp.id())
            .and_then(|n| n.namespace().map(|n| n.state));
        if ns == Some(sys_namespace_entry_pb::State::RUNNING as i32) {
            running = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    assert!(running);
    req.clear_namespace_id();

    let mut list = ListNamespacesResponsePB::new();
    h.lifecycle
        .list_namespaces(&h.snapshot(), &ListNamespacesRequestPB::new(), &mut list);
    assert_eq!(list.namespaces.len(), 2);
    assert!(list.namespaces.iter().any(|n| n.name() == "ks"));

    // Not while it holds a table.
    let (result, _) = h.create_table("ks", "t", Some(1)).await;
    result.unwrap();
    let mut drop_req = DeleteNamespaceRequestPB::new();
    drop_req
        .namespace
        .mut_or_insert_default()
        .set_name("ks".into());
    let e = h
        .lifecycle
        .delete_namespace(&h.token, Harness::deadline(), &drop_req)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::NAMESPACE_IS_NOT_EMPTY);
    assert!(e.status.message().contains("which has table: t"), "{e:?}");

    h.create_namespace("empty").await.unwrap();
    drop_req
        .namespace
        .mut_or_insert_default()
        .set_name("empty".into());
    h.lifecycle
        .delete_namespace(&h.token, Harness::deadline(), &drop_req)
        .await
        .unwrap();
    let e = h
        .lifecycle
        .delete_namespace(&h.token, Harness::deadline(), &drop_req)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::NAMESPACE_NOT_FOUND);
}

#[tokio::test]
async fn create_table_writes_preparing_rows_once() {
    let h = Harness::new(3).await;
    h.create_namespace("ks").await.unwrap();
    let (result, resp) = h.create_table("ks", "t", None).await;
    result.unwrap();
    let snapshot = h.snapshot();
    let table = snapshot
        .get(SysRowEntryType::TABLE, resp.table_id())
        .unwrap();
    let pb: SysTablesEntryPB = table.parse().unwrap();
    assert_eq!(pb.state(), sys_tables_entry_pb::State::PREPARING);
    assert_eq!(pb.namespace_name(), b"ks");
    assert_eq!(pb.version(), 0);
    assert!(pb.has_no_custom_tablespace());
    // One tablet per tserver by default; ids sorted by partition, as C++ sorts them.
    let tablets = snapshot.tablets_of(resp.table_id());
    assert_eq!(tablets.len(), 3);
    let ids: Vec<_> = tablets.iter().map(|t| t.key.id.to_vec()).collect();
    let mut sorted = ids.clone();
    sorted.sort();
    assert_eq!(ids, sorted);
    for tablet in &tablets {
        let pb: SysTabletsEntryPB = tablet.parse().unwrap();
        assert_eq!(pb.state(), sys_tablets_entry_pb::State::PREPARING);
        assert!(pb.hosted_tables_mapped_by_parent_id());
        assert_eq!(pb.committed_consensus_state.config.committed_op_index(), -1);
    }

    // Again: the existing id comes back with OBJECT_ALREADY_PRESENT.
    let (again, again_resp) = h.create_table("ks", "t", None).await;
    assert_eq!(again.unwrap_err().code(), Code::OBJECT_ALREADY_PRESENT);
    assert_eq!(again_resp.table_id(), resp.table_id());

    let mut list = ListTablesResponsePB::new();
    h.lifecycle
        .list_tables(&snapshot, &ListTablesRequestPB::new(), &mut list)
        .unwrap();
    assert_eq!(list.tables.len(), 1);
    assert_eq!(
        list.tables[0].relation_type(),
        yb_proto::pb::master_types::RelationType::USER_TABLE_RELATION
    );

    let mut req = GetTableSchemaRequestPB::new();
    req.table
        .mut_or_insert_default()
        .set_table_id(resp.table_id().to_vec());
    let mut schema = GetTableSchemaResponsePB::new();
    h.lifecycle
        .get_table_schema(&snapshot, &req, &mut schema)
        .unwrap();
    assert!(!schema.create_table_done());
    assert_eq!(schema.identifier.namespace.name(), "ks");
}

#[tokio::test]
async fn create_table_checks_tablets_and_tservers_like_cpp() {
    let h = Harness::new(1).await;
    h.create_namespace("ks").await.unwrap();
    // RF 3 needs two live tservers for a quorum.
    let (result, _) = h.create_table("ks", "t", Some(1)).await;
    let e = result.unwrap_err();
    assert_eq!(e.code(), Code::UNKNOWN_ERROR);
    assert!(
        e.status
            .message()
            .starts_with("Not enough live tablet servers"),
        "{e:?}"
    );

    let h = Harness::new(3).await;
    h.create_namespace("ks").await.unwrap();
    let (result, _) = h.create_table("ks", "t", Some(151)).await;
    assert_eq!(result.unwrap_err().code(), Code::TOO_MANY_TABLETS);
    let (result, _) = h.create_table("ks", "t", Some(0)).await;
    assert!(result
        .unwrap_err()
        .status
        .message()
        .contains("num_tablets should be greater than 0"));
    let (result, _) = h.create_table("nope", "t", Some(1)).await;
    assert_eq!(result.unwrap_err().code(), Code::NAMESPACE_NOT_FOUND);
    assert!(h.ts_manager.live().len() == 3);
}

#[test]
fn replicas_go_to_the_least_loaded_tservers_and_fill_block_minimums() {
    let ts_manager = TsManager::new(Arc::default(), TsManagerOptions::default());
    ts_manager.apply_peer_tservers(
        &[
            (tserver("a", "z1"), true),
            (tserver("b", "z1"), true),
            (tserver("c", "z2"), true),
            (tserver("d", "z2"), true),
        ],
        true,
    );
    let tservers = ts_manager.live();
    let mut load = Load::default();
    load.global.insert(b"a".to_vec(), 5);
    let chosen = select_replicas(&PlacementInfoPB::new(), &tservers, 3, &mut load).unwrap();
    let uuids: Vec<_> = chosen.iter().map(|d| d.uuid.to_vec()).collect();
    assert_eq!(uuids, vec![b"b".to_vec(), b"c".to_vec(), b"d".to_vec()]);
    // The next tablet of the same table goes to the least loaded again.
    let next = select_replicas(&PlacementInfoPB::new(), &tservers, 1, &mut load).unwrap();
    assert_eq!(&*next[0].uuid, b"a");

    // Two replicas in z1 at least, three in all.
    let mut placement = PlacementInfoPB::new();
    placement.set_num_replicas(3);
    for (zone, min) in [("z1", 2), ("z2", 1)] {
        let mut block = PlacementBlockPB::new();
        let mut ci = CloudInfoPB::new();
        ci.set_placement_cloud("c".into());
        ci.set_placement_region("r".into());
        ci.set_placement_zone(zone.into());
        block.cloud_info = MessageField::some(ci);
        block.set_min_num_replicas(min);
        placement.placement_blocks.push(block);
    }
    let chosen = select_replicas(&placement, &tservers, 3, &mut Load::default()).unwrap();
    let in_z1 = chosen
        .iter()
        .filter(|d| d.pb.registration.cloud_info.placement_zone() == "z1")
        .count();
    assert_eq!(in_z1, 2);
    // Too few tservers: under-replicated, as in C++; none at all is an error.
    let short = select_replicas(&placement, &tservers[..2], 3, &mut Load::default()).unwrap();
    assert_eq!(short.len(), 2);
    assert!(select_replicas(&placement, &[], 3, &mut Load::default()).is_err());
}

/// `MaxNumReplicasIsAHardCap`: z0 has three tservers but a maximum of two, and z3 and z4 have
/// none, so no quorum of three fits until a tserver joins z3.
#[test]
fn block_maximums_cap_the_feasible_quorum() {
    let ts_manager = TsManager::new(Arc::default(), TsManagerOptions::default());
    let mut peers = vec![
        (tserver("a", "z0"), true),
        (tserver("b", "z0"), true),
        (tserver("c", "z0"), true),
        (tserver("d", "z1"), true),
    ];
    ts_manager.apply_peer_tservers(&peers, true);
    let mut placement = PlacementInfoPB::new();
    placement.set_num_replicas(5);
    for zone in ["z0", "z3", "z4"] {
        let mut block = PlacementBlockPB::new();
        let mut ci = CloudInfoPB::new();
        ci.set_placement_cloud("c".into());
        ci.set_placement_region("r".into());
        ci.set_placement_zone(zone.into());
        block.cloud_info = MessageField::some(ci);
        block.set_min_num_replicas(1);
        block.set_max_num_replicas(2);
        placement.placement_blocks.push(block);
    }
    let error = check_placement(&placement, &ts_manager.live(), 3, true).unwrap_err();
    assert!(
        error
            .status
            .message()
            .contains("Can only find 2 tablet servers for the replicas but need at least 3"),
        "{}",
        error.status.message()
    );
    peers.push((tserver("e", "z3"), true));
    ts_manager.apply_peer_tservers(&peers, true);
    check_placement(&placement, &ts_manager.live(), 3, true).unwrap();
}

impl Harness {
    /// Sets a table's state, as the controllers would.
    async fn set_table_state(&self, table_id: &[u8], state: sys_tables_entry_pb::State) {
        self.lifecycle
            .writer
            .mutate(&self.token, |snapshot| {
                let table = snapshot.get(SysRowEntryType::TABLE, table_id).unwrap();
                let mut pb: SysTablesEntryPB = table.parse().unwrap();
                pb.state = Some(EnumOrUnknown::new(state));
                let mutation = master_catalog_state::Mutation::new().read(table).upsert(
                    SysRowEntryType::TABLE,
                    table_id,
                    pb.write_to_bytes().unwrap(),
                );
                Ok((mutation, ()))
            })
            .await
            .unwrap();
    }

    fn table(&self, table_id: &[u8]) -> SysTablesEntryPB {
        self.snapshot()
            .get(SysRowEntryType::TABLE, table_id)
            .unwrap()
            .parse()
            .unwrap()
    }
}

#[tokio::test]
async fn alter_writes_the_new_schema_and_altering_once_running() {
    let h = Harness::new(3).await;
    h.create_namespace("ks").await.unwrap();
    let (result, created) = h.create_table("ks", "t", Some(2)).await;
    result.unwrap();
    let table_id = created.table_id().to_vec();
    h.create_table("ks", "other", Some(1)).await.0.unwrap();

    let mut req = AlterTableRequestPB::new();
    req.table
        .mut_or_insert_default()
        .set_table_id(table_id.clone());
    let mut step = alter_table_request_pb::Step::new();
    step.set_type(alter_table_request_pb::StepType::ADD_COLUMN);
    let column = step
        .add_column
        .mut_or_insert_default()
        .schema
        .mut_or_insert_default();
    column.set_name("c".into());
    column
        .type_
        .mut_or_insert_default()
        .set_main(yb_proto::pb::value::PersistentDataType::INT32);
    req.alter_schema_steps.push(step);
    let mut resp = AlterTableResponsePB::new();
    // Not while the table is being created.
    let e = h
        .lifecycle
        .alter_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::TABLE_NOT_RUNNING);

    h.set_table_state(&table_id, sys_tables_entry_pb::State::RUNNING)
        .await;
    h.lifecycle
        .alter_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert_eq!(resp.schema_version(), 1);
    let pb = h.table(&table_id);
    assert_eq!(pb.state(), sys_tables_entry_pb::State::ALTERING);
    assert_eq!(pb.version(), 1);
    assert_eq!(pb.schema.columns.len(), 1);
    assert!(pb.fully_applied_schema.is_some());
    assert_eq!(pb.fully_applied_schema_version(), 0);

    let mut done_req = IsAlterTableDoneRequestPB::new();
    done_req
        .table
        .mut_or_insert_default()
        .set_table_id(table_id.clone());
    let mut done = IsAlterTableDoneResponsePB::new();
    h.lifecycle
        .is_alter_table_done(&h.snapshot(), &done_req, &mut done)
        .unwrap();
    assert!(!done.done());
    assert_eq!(done.schema_version(), 1);

    // A rename to a taken name is refused.
    let mut rename = AlterTableRequestPB::new();
    rename
        .table
        .mut_or_insert_default()
        .set_table_id(table_id.clone());
    rename.set_new_table_name("other".into());
    let e = h
        .lifecycle
        .alter_table(&h.token, Harness::deadline(), &rename, &mut resp)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::OBJECT_ALREADY_PRESENT);
}

#[tokio::test]
async fn drop_marks_the_table_deleting_and_the_deleter_finishes_it() {
    let h = Harness::new(3).await;
    h.create_namespace("ks").await.unwrap();
    let (result, created) = h.create_table("ks", "t", Some(3)).await;
    result.unwrap();
    let table_id = created.table_id().to_vec();

    let mut req = DeleteTableRequestPB::new();
    req.table
        .mut_or_insert_default()
        .set_table_id(table_id.clone());
    // The table is not an index.
    req.set_is_index_table(true);
    let mut resp = DeleteTableResponsePB::new();
    let e = h
        .lifecycle
        .delete_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap_err();
    assert_eq!(e.code(), Code::OBJECT_NOT_FOUND);

    req.set_is_index_table(false);
    h.lifecycle
        .delete_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap();
    assert_eq!(resp.deleted_table_ids, vec![table_id.clone()]);
    assert_eq!(
        h.table(&table_id).state(),
        sys_tables_entry_pb::State::DELETING
    );
    // The name is free again, and a second drop is refused.
    assert!(h
        .snapshot()
        .table_by_name(h.table(&table_id).namespace_id(), "t")
        .is_none());
    let e = h
        .lifecycle
        .delete_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap_err();
    assert!(
        e.status.message().starts_with("The object was deleted"),
        "{e:?}"
    );

    let mut done_req = IsDeleteTableDoneRequestPB::new();
    done_req.set_table_id(table_id.clone());
    let mut done = IsDeleteTableDoneResponsePB::new();
    h.lifecycle
        .is_delete_table_done(&h.snapshot(), &done_req, &mut done)
        .unwrap();
    assert!(!done.done());

    let deleter = crate::TableDeleter::new(h.lifecycle.clone(), || Duration::from_secs(1), None);
    deleter.pass().await;
    for tablet in h.snapshot().tablets_of(&table_id) {
        let pb: SysTabletsEntryPB = tablet.parse().unwrap();
        assert_eq!(pb.state(), sys_tablets_entry_pb::State::DELETED);
    }
    assert_eq!(
        h.table(&table_id).state(),
        sys_tables_entry_pb::State::DELETED
    );
    h.lifecycle
        .is_delete_table_done(&h.snapshot(), &done_req, &mut done)
        .unwrap();
    assert!(done.done());

    // The keyspace can go now.
    let mut drop_req = DeleteNamespaceRequestPB::new();
    drop_req
        .namespace
        .mut_or_insert_default()
        .set_name("ks".into());
    h.lifecycle
        .delete_namespace(&h.token, Harness::deadline(), &drop_req)
        .await
        .unwrap();
}

#[tokio::test]
async fn a_transactional_table_creates_the_transaction_table_first() {
    let h = Harness::new(3).await;
    // The system keyspace and the transaction tables config, as default rows give them.
    let mut req = CreateNamespaceRequestPB::new();
    req.set_name("system".into());
    req.set_namespace_id(b"00000000000000000000000000000001".to_vec());
    h.lifecycle
        .create_namespace(
            &h.token,
            Harness::deadline(),
            &req,
            &mut CreateNamespaceResponsePB::new(),
        )
        .await
        .unwrap();
    let mut config = yb_proto::pb::catalog_entity_info::SysConfigEntryPB::new();
    config.mut_transaction_tables_config().set_version(0);
    h.lifecycle
        .writer
        .submit(
            &h.token,
            master_catalog_state::Mutation::new().upsert(
                SysRowEntryType::SYS_CONFIG,
                master_catalog_state::defaults::TRANSACTION_TABLES_CONFIG_ID,
                config.write_to_bytes().unwrap(),
            ),
        )
        .await
        .unwrap();
    h.create_namespace("ks").await.unwrap();

    let mut req = CreateTableRequestPB::new();
    req.set_name("t".into());
    req.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
    req.namespace.mut_or_insert_default().set_name("ks".into());
    req.schema
        .mut_or_insert_default()
        .table_properties
        .mut_or_insert_default()
        .set_is_transactional(true);
    req.set_num_tablets(1);
    let mut resp = CreateTableResponsePB::new();
    h.lifecycle
        .create_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap();

    let snapshot = h.snapshot();
    let txn = snapshot
        .table_by_name(b"00000000000000000000000000000001", "transactions")
        .expect("system.transactions");
    let txn_pb: SysTablesEntryPB = txn.parse().unwrap();
    assert_eq!(
        txn_pb.table_type(),
        TableType::TRANSACTION_STATUS_TABLE_TYPE
    );
    assert_eq!(txn_pb.schema.columns[0].name(), "key");
    let config: yb_proto::pb::catalog_entity_info::SysConfigEntryPB = snapshot
        .get(
            SysRowEntryType::SYS_CONFIG,
            master_catalog_state::defaults::TRANSACTION_TABLES_CONFIG_ID,
        )
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(config.transaction_tables_config().version(), 1);

    // Not done while the transaction table is being created, even once the table runs.
    h.set_table_state(resp.table_id(), sys_tables_entry_pb::State::RUNNING)
        .await;
    let mut done_req = yb_proto::pb::master_ddl::IsCreateTableDoneRequestPB::new();
    done_req
        .table
        .mut_or_insert_default()
        .set_table_id(resp.table_id().to_vec());
    let mut done = yb_proto::pb::master_ddl::IsCreateTableDoneResponsePB::new();
    h.lifecycle
        .is_create_table_done(&h.snapshot(), &done_req, &mut done)
        .unwrap();
    assert!(!done.done());
    h.set_table_state(&txn.key.id, sys_tables_entry_pb::State::RUNNING)
        .await;
    h.lifecycle
        .is_create_table_done(&h.snapshot(), &done_req, &mut done)
        .unwrap();
    assert!(done.done());
}

#[tokio::test]
async fn a_keyspace_schedule_refuses_drops_until_it_is_deleted() {
    use yb_proto::pb::master_backup::SnapshotScheduleOptionsPB;
    let h = Harness::new(3).await;
    h.create_namespace("ks").await.unwrap();
    let (result, created) = h.create_table("ks", "t", Some(1)).await;
    result.unwrap();
    let table_id = created.table_id().to_vec();

    let schedule_id = [0x12u8; 16];
    let write_schedule = |deleted: bool| {
        let mut options = SnapshotScheduleOptionsPB::new();
        let mut ident = yb_proto::pb::master_types::TableIdentifierPB::new();
        ident
            .namespace
            .mut_or_insert_default()
            .set_name("ks".into());
        options
            .filter
            .mut_or_insert_default()
            .mut_tables()
            .tables
            .push(ident);
        if deleted {
            options.set_delete_time(1);
        }
        master_catalog_state::Mutation::new().upsert(
            SysRowEntryType::SNAPSHOT_SCHEDULE,
            &schedule_id,
            options.write_to_bytes().unwrap(),
        )
    };
    h.lifecycle
        .writer
        .submit(&h.token, write_schedule(false))
        .await
        .unwrap();

    let mut req = DeleteTableRequestPB::new();
    req.table
        .mut_or_insert_default()
        .set_table_id(table_id.clone());
    let mut resp = DeleteTableResponsePB::new();
    let e = h
        .lifecycle
        .delete_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap_err();
    assert_eq!(
        e.status.code(),
        yb_proto::pb::wire_protocol::app_status_pb::ErrorCode::NOT_SUPPORTED,
        "{e:?}"
    );

    let mut drop_req = DeleteNamespaceRequestPB::new();
    drop_req
        .namespace
        .mut_or_insert_default()
        .set_name("ks".into());
    // The table is checked first, as in C++.
    let e = h
        .lifecycle
        .delete_namespace(&h.token, Harness::deadline(), &drop_req)
        .await
        .unwrap_err();
    assert!(e.status.message().contains("which has table"), "{e:?}");

    h.lifecycle
        .writer
        .submit(&h.token, write_schedule(true))
        .await
        .unwrap();
    h.lifecycle
        .delete_table(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap();
    h.lifecycle
        .writer
        .submit(&h.token, write_schedule(false))
        .await
        .unwrap();
    let e = h
        .lifecycle
        .delete_namespace(&h.token, Harness::deadline(), &drop_req)
        .await
        .unwrap_err();
    assert!(
        e.status.message().starts_with(
            "Cannot delete keyspace which has schedule: [12121212-1212-1212-1212-121212121212], \
             request: "
        ),
        "{e:?}"
    );
}

#[tokio::test]
async fn only_ycql_virtual_tables_and_sys_catalog_tables_are_system() {
    use crate::lookup::{is_system_table, SYS_CATALOG_TABLET_ID};
    const SYSTEM: &[u8] = b"00000000000000000000000000000001";
    let table = |id: &[u8], name: &str, namespace_id: &[u8], table_type: TableType| {
        let mut pb = SysTablesEntryPB::new();
        pb.set_name(name.into());
        pb.set_namespace_id(namespace_id.to_vec());
        pb.table_type = Some(EnumOrUnknown::new(table_type));
        pb.set_version(0);
        pb.schema = MessageField::some(SchemaPB::new());
        RowChange::Upsert {
            entry_type: SysRowEntryType::TABLE,
            id: id.to_vec(),
            data: pb.write_to_bytes().unwrap(),
        }
    };
    let tablet = |id: &[u8], table_id: &[u8]| {
        let mut pb = SysTabletsEntryPB::new();
        pb.set_table_id(table_id.to_vec());
        pb.table_ids.push(table_id.to_vec());
        pb.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
        RowChange::Upsert {
            entry_type: SysRowEntryType::TABLET,
            id: id.to_vec(),
            data: pb.write_to_bytes().unwrap(),
        }
    };
    let (fake, events) = FakeSysCatalog::new();
    fake.apply(vec![
        // A YCQL virtual table, on its own master-hosted tablet.
        table(b"peers", "peers", SYSTEM, TableType::YQL_TABLE_TYPE),
        tablet(b"peers-tablet", b"peers"),
        // A real table C++ creates in `system` through CreateTable, on tservers.
        table(b"cdc", "cdc_state", SYSTEM, TableType::YQL_TABLE_TYPE),
        tablet(b"cdc-tablet", b"cdc"),
        // A YSQL catalog table on the sys catalog tablet.
        table(b"pg", "pg_class", b"db", TableType::PGSQL_TABLE_TYPE),
        tablet(SYS_CATALOG_TABLET_ID, b"pg"),
    ]);
    let catalog = Catalog::new(fake.sender().clone());
    tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
    catalog.barrier().await;
    let snapshot = catalog.snapshot();
    let is_system = |id: &[u8]| {
        is_system_table(
            &snapshot,
            snapshot.get(SysRowEntryType::TABLE, id).expect("the table"),
        )
    };
    assert!(is_system(b"peers"));
    assert!(!is_system(b"cdc"));
    assert!(is_system(b"pg"));
}

mod ysql {
    use super::*;
    use yb_proto::pb::catalog_entity_info::{
        sys_namespace_entry_pb, SysNamespaceEntryPB, SysTablesEntryPB,
    };
    use yb_proto::pb::common::TransactionMetadataPB;
    use yb_proto::pb::common_types::{TableType, YQLDatabase};
    use yb_proto::pb::master_ddl::{
        AlterTableRequestPB, AlterTableResponsePB, DeleteTableRequestPB, DeleteTableResponsePB,
        IsYsqlDdlVerificationDoneRequestPB, IsYsqlDdlVerificationDoneResponsePB,
        ReportYsqlDdlTxnStatusRequestPB,
    };

    const DB_ID: &[u8] = b"000033e8000030008000000000000000";
    const TABLE_ID: &[u8] = b"000033e8000030008000000000004000";

    async fn ysql_harness() -> Harness {
        let h = Harness::new(3).await;
        let mut ns = SysNamespaceEntryPB::new();
        ns.set_name(b"yugabyte".to_vec());
        ns.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
        ns.set_state(sys_namespace_entry_pb::State::RUNNING);
        h.lifecycle
            .write(&h.token, Harness::deadline(), |_| {
                Ok((
                    master_catalog_state::Mutation::new()
                        .upsert_message(SysRowEntryType::NAMESPACE, DB_ID, &ns)
                        .unwrap(),
                    (),
                ))
            })
            .await
            .unwrap();
        h
    }

    fn txn(id: u8) -> TransactionMetadataPB {
        let mut txn = TransactionMetadataPB::new();
        txn.set_transaction_id(vec![id; 16]);
        txn.set_status_tablet("status".into());
        txn
    }

    impl Harness {
        async fn create_ysql(
            &self,
            txn: Option<TransactionMetadataPB>,
        ) -> Result<(), MasterErrorPB> {
            let mut req = CreateTableRequestPB::new();
            req.set_name("t".into());
            req.set_table_type(TableType::PGSQL_TABLE_TYPE);
            req.set_table_id(TABLE_ID.to_vec());
            req.schema = Some(Default::default()).into();
            req.namespace.mut_or_insert_default().set_id(DB_ID.to_vec());
            req.set_num_tablets(2);
            if let Some(txn) = txn {
                req.transaction = Some(txn).into();
                req.set_ysql_yb_ddl_rollback_enabled(true);
            }
            let mut resp = CreateTableResponsePB::new();
            self.lifecycle
                .create_table(&self.token, Self::deadline(), &req, &mut resp)
                .await
        }

        async fn drop_ysql(&self, txn: Option<TransactionMetadataPB>) {
            let mut req = DeleteTableRequestPB::new();
            req.table
                .mut_or_insert_default()
                .set_table_id(TABLE_ID.to_vec());
            if let Some(txn) = txn {
                req.transaction = Some(txn).into();
                req.set_ysql_yb_ddl_rollback_enabled(true);
            }
            let mut resp = DeleteTableResponsePB::new();
            self.lifecycle
                .delete_table(&self.token, Self::deadline(), &req, &mut resp)
                .await
                .unwrap();
        }

        async fn report(&self, id: u8, committed: bool) {
            let mut req = ReportYsqlDdlTxnStatusRequestPB::new();
            req.set_transaction_id(vec![id; 16]);
            req.set_is_committed(committed);
            self.lifecycle
                .report_ysql_ddl_txn_status(&self.token, Self::deadline(), &req)
                .await
                .unwrap();
        }

        fn verified(&self, id: u8) -> bool {
            let mut req = IsYsqlDdlVerificationDoneRequestPB::new();
            req.transaction
                .mut_or_insert_default()
                .set_transaction_id(vec![id; 16]);
            let mut resp = IsYsqlDdlVerificationDoneResponsePB::new();
            self.lifecycle
                .is_ysql_ddl_verification_done(&self.catalog.snapshot(), &req, &mut resp)
                .unwrap();
            resp.done()
        }

        fn table_row(&self) -> SysTablesEntryPB {
            self.catalog
                .snapshot()
                .get(SysRowEntryType::TABLE, TABLE_ID)
                .unwrap()
                .parse()
                .unwrap()
        }
    }

    /// A table a committed DDL transaction created stays, without the transaction's state.
    #[tokio::test]
    async fn a_committed_create_stays() {
        let h = ysql_harness().await;
        h.create_ysql(Some(txn(1))).await.unwrap();
        let row = h.table_row();
        assert_eq!(row.table_type(), TableType::PGSQL_TABLE_TYPE);
        assert!(row.ysql_ddl_txn_verifier_state[0].contains_create_table_op());
        assert_eq!(h.catalog.snapshot().tablets_of(TABLE_ID).len(), 2);
        assert!(!h.verified(1));
        h.report(1, true).await;
        assert!(h.verified(1));
        let row = h.table_row();
        assert!(row.ysql_ddl_txn_verifier_state.is_empty());
        assert!(row.transaction.is_none());
        // The same id again is a duplicate.
        assert_eq!(
            h.create_ysql(None).await.unwrap_err().code(),
            Code::OBJECT_ALREADY_PRESENT
        );
    }

    /// A table an aborted DDL transaction created is dropped.
    #[tokio::test]
    async fn an_aborted_create_is_dropped() {
        let h = ysql_harness().await;
        h.create_ysql(Some(txn(2))).await.unwrap();
        h.report(2, false).await;
        assert_eq!(h.table_row().state(), sys_tables_entry_pb::State::DELETING);
        assert!(h.verified(2));
    }

    /// An aborted ALTER restores the schema and name as they were, and the transaction is
    /// verified once the tablets have that.
    #[tokio::test]
    async fn an_aborted_alter_is_rolled_back() {
        let h = ysql_harness().await;
        h.create_ysql(None).await.unwrap();
        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        let before = h.table_row();
        let mut req = AlterTableRequestPB::new();
        req.table
            .mut_or_insert_default()
            .set_table_id(TABLE_ID.to_vec());
        req.set_new_table_name("renamed".into());
        req.transaction = Some(txn(5)).into();
        req.set_ysql_yb_ddl_rollback_enabled(true);
        let mut resp = AlterTableResponsePB::new();
        h.lifecycle
            .alter_table(&h.token, Harness::deadline(), &req, &mut resp)
            .await
            .unwrap();
        let altered = h.table_row();
        assert_eq!(altered.name(), b"renamed");
        assert!(altered.ysql_ddl_txn_verifier_state[0].contains_alter_table_op());
        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        h.report(5, false).await;
        let rolled_back = h.table_row();
        assert_eq!(rolled_back.name(), before.name());
        assert_eq!(rolled_back.version(), before.version() + 2);
        assert_eq!(rolled_back.state(), sys_tables_entry_pb::State::ALTERING);
        assert!(!h.verified(5));
        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        assert!(h.verified(5));
    }

    const INDEX_ID: &[u8] = b"000033e8000030008000000000004001";

    /// A tablegroup is its parent table, which owns one colocated tablet; a table in it owns
    /// none, names the parent and gets a colocation id unique in the group. The group cannot be
    /// dropped while it holds a table.
    #[tokio::test]
    async fn a_tablegroup_holds_colocated_tables() {
        use yb_proto::pb::master_ddl::{
            CreateTablegroupRequestPB, CreateTablegroupResponsePB, DeleteTablegroupRequestPB,
            DeleteTablegroupResponsePB, ListTablegroupsRequestPB, ListTablegroupsResponsePB,
        };
        const TG: &[u8] = b"000033e800003000800000000000a000";
        let h = ysql_harness().await;
        let mut req = CreateTablegroupRequestPB::new();
        req.set_id(TG.to_vec());
        req.set_namespace_id(DB_ID.to_vec());
        req.set_namespace_name("yugabyte".into());
        let mut resp = CreateTablegroupResponsePB::new();
        h.lifecycle
            .create_tablegroup(&h.token, Harness::deadline(), &req, &mut resp)
            .await
            .unwrap();
        let parent_id = [TG, b".tablegroup.parent.uuid"].concat();
        assert_eq!(resp.parent_table_id(), parent_id.as_slice());
        let tablets = h.snapshot().tablets_of(&parent_id).len();
        assert_eq!(tablets, 1);
        let tablet: SysTabletsEntryPB = h.snapshot().tablets_of(&parent_id)[0].parse().unwrap();
        assert!(tablet.colocated());
        // Again: the tablegroup exists.
        let err = h
            .lifecycle
            .create_tablegroup(&h.token, Harness::deadline(), &req, &mut resp)
            .await
            .unwrap_err();
        assert_eq!(err.code(), Code::OBJECT_ALREADY_PRESENT);

        let create = |id: &[u8], colocation_id: Option<u32>| {
            let mut req = CreateTableRequestPB::new();
            req.set_name("c".into());
            req.set_table_type(TableType::PGSQL_TABLE_TYPE);
            req.set_table_id(id.to_vec());
            req.schema = Some(Default::default()).into();
            req.namespace.mut_or_insert_default().set_id(DB_ID.to_vec());
            req.set_tablegroup_id(TG.to_vec());
            if let Some(c) = colocation_id {
                req.set_colocation_id(c);
            }
            req
        };
        let child = b"000033e8000030008000000000004010";
        h.lifecycle
            .create_table(
                &h.token,
                Harness::deadline(),
                &create(child, Some(20000)),
                &mut CreateTableResponsePB::new(),
            )
            .await
            .unwrap();
        assert!(h.snapshot().tablets_of(child).is_empty());
        let pb = h.table(child);
        assert_eq!(pb.parent_table_id(), parent_id.as_slice());
        assert_eq!(pb.schema.colocated_table_id.colocation_id(), 20000);
        assert_eq!(pb.state(), sys_tables_entry_pb::State::PREPARING);
        assert_eq!(h.snapshot().hosting_tablets_of(child).len(), 1);
        let err = h
            .lifecycle
            .create_table(
                &h.token,
                Harness::deadline(),
                &create(b"000033e8000030008000000000004011", Some(20000)),
                &mut CreateTableResponsePB::new(),
            )
            .await
            .unwrap_err();
        assert!(err.status.message().contains("already contains"), "{err:?}");

        let mut list = ListTablegroupsResponsePB::new();
        let mut list_req = ListTablegroupsRequestPB::new();
        list_req.set_namespace_id(DB_ID.to_vec());
        h.lifecycle
            .list_tablegroups(&h.snapshot(), &list_req, &mut list)
            .unwrap();
        assert_eq!(list.tablegroups.len(), 1);
        assert_eq!(list.tablegroups[0].id(), TG);

        let mut drop = DeleteTablegroupRequestPB::new();
        drop.set_id(TG.to_vec());
        let err = h
            .lifecycle
            .delete_tablegroup(
                &h.token,
                Harness::deadline(),
                &drop,
                &mut DeleteTablegroupResponsePB::new(),
            )
            .await
            .unwrap_err();
        assert!(err.status.message().contains("still has tables"), "{err:?}");
    }

    /// DROP DATABASE writes the database DELETING; the table-deletes controller then deletes its
    /// tables and marks it DELETED, after which it is no longer found.
    #[tokio::test]
    async fn a_dropped_database_takes_its_tables_with_it() {
        use yb_proto::pb::master_ddl::{
            IsDeleteNamespaceDoneRequestPB, IsDeleteNamespaceDoneResponsePB,
        };
        let h = ysql_harness().await;
        h.create_ysql(None).await.unwrap();
        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        let mut req = DeleteNamespaceRequestPB::new();
        req.namespace.mut_or_insert_default().set_id(DB_ID.to_vec());
        req.namespace
            .mut_or_insert_default()
            .set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
        h.lifecycle
            .delete_namespace(&h.token, Harness::deadline(), &req)
            .await
            .unwrap();
        let ns_state = |h: &Harness| {
            h.snapshot()
                .get(SysRowEntryType::NAMESPACE, DB_ID)
                .unwrap()
                .parse::<SysNamespaceEntryPB>()
                .unwrap()
                .state()
        };
        assert_eq!(ns_state(&h), sys_namespace_entry_pb::State::DELETING);
        let mut done_req = IsDeleteNamespaceDoneRequestPB::new();
        done_req.namespace = req.namespace.clone();
        let mut done = IsDeleteNamespaceDoneResponsePB::new();
        h.lifecycle
            .is_delete_namespace_done(&h.snapshot(), &done_req, &mut done)
            .unwrap();
        assert!(!done.done());

        crate::TableDeleter::new(h.lifecycle.clone(), || Duration::from_secs(1), None)
            .pass()
            .await;
        assert_eq!(ns_state(&h), sys_namespace_entry_pb::State::DELETED);
        assert_eq!(
            h.table(TABLE_ID).state(),
            sys_tables_entry_pb::State::DELETED
        );
        h.lifecycle
            .is_delete_namespace_done(&h.snapshot(), &done_req, &mut done)
            .unwrap();
        assert!(done.done());
    }

    #[tokio::test]
    async fn a_new_leader_drops_a_database_whose_creation_it_did_not_start() {
        let h = ysql_harness().await;
        let state = |h: &Harness, id: &[u8]| {
            h.snapshot()
                .get(SysRowEntryType::NAMESPACE, id)
                .unwrap()
                .parse::<SysNamespaceEntryPB>()
                .unwrap()
                .state()
        };
        let creating: &[u8] = b"000033e9000030008000000000000000";
        let interrupted: &[u8] = b"000033ea000030008000000000000000";
        let upgrading: &[u8] = b"000033eb000030008000000000000000";
        h.lifecycle
            .write(&h.token, Harness::deadline(), |_| {
                let mut mutation = master_catalog_state::Mutation::new();
                for id in [creating, interrupted, upgrading] {
                    let mut ns = SysNamespaceEntryPB::new();
                    ns.set_name(id.to_vec());
                    ns.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
                    ns.set_state(sys_namespace_entry_pb::State::PREPARING);
                    if id == upgrading {
                        ns.set_ysql_next_major_version_state(
                            sys_namespace_entry_pb::YsqlNextMajorVersionState::NEXT_VER_PREPARING,
                        );
                    }
                    mutation = mutation
                        .upsert_message(SysRowEntryType::NAMESPACE, id, &ns)
                        .unwrap();
                }
                Ok((mutation, ()))
            })
            .await
            .unwrap();
        h.lifecycle
            .creating_databases
            .lock()
            .unwrap()
            .insert((h.token.term(), creating.to_vec()));
        crate::TableDeleter::new(h.lifecycle.clone(), || Duration::from_secs(1), None)
            .pass()
            .await;
        assert_eq!(
            state(&h, creating),
            sys_namespace_entry_pb::State::PREPARING
        );
        assert_eq!(
            state(&h, interrupted),
            sys_namespace_entry_pb::State::DELETED
        );
        assert_eq!(
            state(&h, upgrading),
            sys_namespace_entry_pb::State::PREPARING
        );
        assert_eq!(state(&h, DB_ID), sys_namespace_entry_pb::State::RUNNING);
    }

    #[tokio::test]
    async fn truncate_is_refused_while_disabled_and_done_with_nothing_running() {
        use crate::{TruncateOptions, Truncates};
        use yb_proto::pb::master_ddl::{
            IsTruncateTableDoneRequestPB, IsTruncateTableDoneResponsePB, TruncateTableRequestPB,
            TruncateTableResponsePB,
        };
        let h = ysql_harness().await;
        h.create_ysql(None).await.unwrap();
        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        let options = |disabled: fn() -> bool| TruncateOptions {
            disable_truncate_table: disabled,
            enable_truncate_on_pitr_table: || false,
            rpc_timeout: || std::time::Duration::from_secs(1),
            retry: Default::default,
        };
        let executor = master_task_runtime::TsRpcExecutor::new(|_| 1);
        let mut req = TruncateTableRequestPB::new();
        req.table_ids.push(TABLE_ID.to_vec());
        let truncates = Truncates::new(h.lifecycle.clone(), executor.clone(), options(|| true));
        let err = truncates
            .truncate_table(&h.token, &req, &mut TruncateTableResponsePB::new())
            .unwrap_err();
        assert_eq!(err.code(), Code::INVALID_REQUEST);
        assert!(err
            .status
            .message()
            .contains("TRUNCATE table is disallowed"));

        let mut done = IsTruncateTableDoneRequestPB::new();
        done.set_table_id(TABLE_ID.to_vec());
        let mut resp = IsTruncateTableDoneResponsePB::new();
        truncates
            .is_truncate_table_done(&h.catalog.snapshot(), &done, &mut resp)
            .unwrap();
        assert!(resp.done());

        let mut missing = IsTruncateTableDoneRequestPB::new();
        missing.set_table_id(b"0000000000000000000000000000dead".to_vec());
        let err = truncates
            .is_truncate_table_done(&h.catalog.snapshot(), &missing, &mut resp)
            .unwrap_err();
        assert_eq!(err.code(), Code::OBJECT_NOT_FOUND);
    }

    /// An index joins its table at WRITE_AND_DELETE, keeping delete markers; `BackfillIndex`
    /// moves it to DO_BACKFILL; `GetBackfillStatus` reports success only once it is
    /// READ_WRITE_AND_DELETE; a drop takes it out of its table.
    #[tokio::test]
    async fn an_index_moves_through_its_backfill_states() {
        let h = ysql_harness().await;
        h.create_ysql(None).await.unwrap();
        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        let mut req = CreateTableRequestPB::new();
        req.set_name("t_idx".into());
        req.set_table_type(TableType::PGSQL_TABLE_TYPE);
        req.set_table_id(INDEX_ID.to_vec());
        req.schema = Some(Default::default()).into();
        req.namespace.mut_or_insert_default().set_id(DB_ID.to_vec());
        req.set_num_tablets(1);
        req.index_info
            .mut_or_insert_default()
            .set_indexed_table_id(TABLE_ID.to_vec());
        let mut resp = CreateTableResponsePB::new();
        h.lifecycle
            .create_table(&h.token, Harness::deadline(), &req, &mut resp)
            .await
            .unwrap();
        let table = h.table_row();
        assert_eq!(table.state(), sys_tables_entry_pb::State::ALTERING);
        assert_eq!(table.indexes.len(), 1);
        assert_eq!(table.indexes[0].table_id(), INDEX_ID);
        assert_eq!(
            table.indexes[0].index_permissions(),
            yb_proto::pb::common_types::IndexPermissions::INDEX_PERM_WRITE_AND_DELETE
        );
        let index: SysTablesEntryPB = h
            .catalog
            .snapshot()
            .get(SysRowEntryType::TABLE, INDEX_ID)
            .unwrap()
            .parse()
            .unwrap();
        assert!(index.schema.table_properties.retain_delete_markers());

        let mut status_req = yb_proto::pb::master_ddl::GetBackfillStatusRequestPB::new();
        let mut ident = yb_proto::pb::master_types::TableIdentifierPB::new();
        ident.set_table_id(INDEX_ID.to_vec());
        status_req.index_tables.push(ident.clone());
        let status = |h: &Harness| {
            let mut resp = yb_proto::pb::master_ddl::GetBackfillStatusResponsePB::new();
            h.lifecycle
                .get_backfill_status(&h.catalog.snapshot(), &status_req, &mut resp)
                .unwrap();
            resp.index_status[0].backfill_status()
        };
        use yb_proto::pb::master_ddl::index_status_pb::BackfillStatus;
        assert_eq!(status(&h), BackfillStatus::BACKFILL_UNKNOWN);

        h.set_table_state(TABLE_ID, sys_tables_entry_pb::State::RUNNING)
            .await;
        let version = h.table_row().version();
        let mut backfill = yb_proto::pb::master_ddl::BackfillIndexRequestPB::new();
        backfill.index_identifier = Some(ident).into();
        let mut backfill_resp = yb_proto::pb::master_ddl::BackfillIndexResponsePB::new();
        h.lifecycle
            .backfill_index(&h.token, Harness::deadline(), &backfill, &mut backfill_resp)
            .await
            .unwrap();
        assert_eq!(backfill_resp.table_identifier.table_id(), TABLE_ID);
        let table = h.table_row();
        assert_eq!(table.version(), version + 1);
        assert!(table.updates_only_index_permissions());
        assert_eq!(
            table.indexes[0].index_permissions(),
            yb_proto::pb::common_types::IndexPermissions::INDEX_PERM_DO_BACKFILL
        );
        // Asked again: already done.
        h.lifecycle
            .backfill_index(&h.token, Harness::deadline(), &backfill, &mut backfill_resp)
            .await
            .unwrap();
        assert_eq!(h.table_row().version(), version + 1);

        // DROP INDEX outside a transaction: the index goes, and leaves its table.
        let mut drop = DeleteTableRequestPB::new();
        drop.table
            .mut_or_insert_default()
            .set_table_id(INDEX_ID.to_vec());
        drop.set_is_index_table(true);
        let mut drop_resp = DeleteTableResponsePB::new();
        h.lifecycle
            .delete_table(&h.token, Harness::deadline(), &drop, &mut drop_resp)
            .await
            .unwrap();
        assert_eq!(drop_resp.indexed_table.table_id(), TABLE_ID);
        assert!(h.table_row().indexes.is_empty());
    }

    /// A drop in a DDL transaction waits for the commit, and an abort undoes it.
    #[tokio::test]
    async fn a_drop_waits_for_the_commit() {
        let h = ysql_harness().await;
        h.create_ysql(None).await.unwrap();
        h.drop_ysql(Some(txn(3))).await;
        let row = h.table_row();
        assert_ne!(row.state(), sys_tables_entry_pb::State::DELETING);
        assert!(row.ysql_ddl_txn_verifier_state[0].contains_drop_table_op());
        h.report(3, false).await;
        let row = h.table_row();
        assert_ne!(row.state(), sys_tables_entry_pb::State::DELETING);
        assert!(row.ysql_ddl_txn_verifier_state.is_empty());

        h.drop_ysql(Some(txn(4))).await;
        h.report(4, true).await;
        assert_eq!(h.table_row().state(), sys_tables_entry_pb::State::DELETING);

        // Without a transaction, a drop is at once.
        let h = ysql_harness().await;
        h.create_ysql(None).await.unwrap();
        h.drop_ysql(None).await;
        assert_eq!(h.table_row().state(), sys_tables_entry_pb::State::DELETING);
    }
}
