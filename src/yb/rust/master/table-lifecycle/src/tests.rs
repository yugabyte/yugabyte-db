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
use protobuf::{Enum, EnumOrUnknown, Message, MessageField};
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
    PrepareTableSchemaResponsePB, SnapshotSchedulesResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, MasterErrorPB, SysRowEntryType};

use crate::lookup::{database_type, is_colocation_parent_table_id};
use crate::placement::{select_replicas, Load};
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
                resp.set_next_column_id(req.request.schema.columns.len() as i32);
                let n = if req.request.has_num_tablets() {
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
                Ok(resp.write_to_bytes().unwrap())
            }
            "SnapshotSchedules" => Ok(SnapshotSchedulesResponsePB::new().write_to_bytes().unwrap()),
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
    assert_eq!(database_type(&ident), YQLDatabase::YQL_DATABASE_REDIS);
    ident.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    assert_eq!(database_type(&ident), YQLDatabase::YQL_DATABASE_PGSQL);
    let id = "0123456789abcdef0123456789abcdef";
    assert!(is_colocation_parent_table_id(
        format!("{id}.tablegroup.parent.uuid").as_bytes()
    ));
    assert!(!is_colocation_parent_table_id(b"x.tablegroup.parent.uuid"));
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

    // YSQL databases wait for YSQL DDL.
    req.set_name("db".into());
    req.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
    let e = h
        .lifecycle
        .create_namespace(&h.token, Harness::deadline(), &req, &mut resp)
        .await
        .unwrap_err();
    assert_eq!(e.status.code().value(), StatusCode::NotSupported as i32);

    let mut list = ListNamespacesResponsePB::new();
    h.lifecycle
        .list_namespaces(&h.snapshot(), &ListNamespacesRequestPB::new(), &mut list);
    assert_eq!(list.namespaces.len(), 1);
    assert_eq!(list.namespaces[0].name(), "ks");

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
    assert!(select_replicas(&placement, &tservers[..2], 3, &mut Load::default()).is_err());
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

    let deleter = crate::TableDeleter::new(h.lifecycle.clone(), || Duration::from_secs(1));
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
