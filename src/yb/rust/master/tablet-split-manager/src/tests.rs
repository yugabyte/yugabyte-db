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
    run_materializer, Catalog, CatalogSnapshot, CatalogWriter, Mutation, WriterOptions,
};
use master_leadership::{FakeLeaderState, LeaderToken, Leadership, Phase};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_table_lifecycle::SchemaHost;
use master_task_runtime::TsRpcExecutor;
use master_ts_manager::{TsManager, TsManagerOptions};
use protobuf::{EnumOrUnknown, Message, MessageField};
use tokio::time::Instant;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB,
};
use yb_proto::pb::common::PartitionPB;
use yb_proto::pb::common_types::TableType;
use yb_proto::pb::master_admin::DeleteNotServingTabletRequestPB;
use yb_proto::pb::master_types::SysRowEntryType;

use crate::{child_partitions, validate_split_candidate, TabletSplitManager};

struct NoHost;

impl SchemaHost for NoHost {
    fn call(&self, method: &'static str, _: &[u8]) -> Result<Vec<u8>, Status> {
        Err(Status::new(StatusCode::NotSupported, method))
    }
}

struct Harness {
    splits: Arc<TabletSplitManager>,
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    ts_manager: Arc<TsManager>,
    token: LeaderToken,
}

impl Harness {
    async fn new() -> Harness {
        let (fake, events) = FakeSysCatalog::new();
        fake.apply(vec![RowChange::Upsert {
            entry_type: SysRowEntryType::CLUSTER_CONFIG,
            id: Vec::new(),
            data: SysClusterConfigEntryPB::new().write_to_bytes().unwrap(),
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
        let splits = TabletSplitManager::new(
            catalog.clone(),
            writer.clone(),
            leadership,
            ts_manager.clone(),
            Arc::new(NoHost),
            TsRpcExecutor::new(|_| 1),
            Default::default(),
            Default::default(),
        );
        let h = Harness {
            splits,
            catalog,
            writer,
            ts_manager,
            token,
        };
        // A YCQL table with one RUNNING tablet on three peers.
        let mut table = SysTablesEntryPB::new();
        table.set_name(b"t".to_vec());
        table.set_namespace_id(b"ns".to_vec());
        table.table_type = Some(EnumOrUnknown::new(TableType::YQL_TABLE_TYPE));
        table.state = Some(EnumOrUnknown::new(sys_tables_entry_pb::State::RUNNING));
        table.set_version(0);
        table.schema = MessageField::some(Default::default());
        table.partition_schema = MessageField::some(Default::default());
        let mut tablet = SysTabletsEntryPB::new();
        tablet.set_table_id(b"t1".to_vec());
        tablet.partition = MessageField::some(PartitionPB::new());
        tablet.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
        let cstate = tablet.committed_consensus_state.mut_or_insert_default();
        cstate.set_current_term(1);
        let config = cstate.config.mut_or_insert_default();
        config.set_committed_op_index(5);
        for peer in ["ts0", "ts1", "ts2"] {
            let mut p = yb_proto::pb::consensus_metadata::RaftPeerPB::new();
            p.set_permanent_uuid(peer.as_bytes().to_vec());
            config.peers.push(p);
        }
        h.writer
            .submit(
                &h.token,
                Mutation::new()
                    .upsert(
                        SysRowEntryType::TABLE,
                        b"t1",
                        table.write_to_bytes().unwrap(),
                    )
                    .upsert(
                        SysRowEntryType::TABLET,
                        b"parent",
                        tablet.write_to_bytes().unwrap(),
                    ),
            )
            .await
            .unwrap();
        h
    }

    fn snapshot(&self) -> Arc<CatalogSnapshot> {
        self.catalog.snapshot()
    }

    fn tablet(&self, id: &[u8]) -> SysTabletsEntryPB {
        self.snapshot()
            .get(SysRowEntryType::TABLET, id)
            .unwrap()
            .parse()
            .unwrap()
    }

    async fn set_running(&self, id: &[u8]) {
        let mut pb = self.tablet(id);
        pb.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::RUNNING));
        self.writer
            .submit(
                &self.token,
                Mutation::new().upsert(SysRowEntryType::TABLET, id, pb.write_to_bytes().unwrap()),
            )
            .await
            .unwrap();
    }
}

#[tokio::test]
async fn a_split_registers_its_children_in_one_write() {
    let h = Harness::new().await;
    let key = vec![0x80, 0x00];
    let children = h
        .splits
        .do_split(h.token, b"parent", vec![key.clone()], true)
        .await
        .unwrap();
    assert_eq!(children.len(), 2);
    assert!(children[0] < children[1]);
    let parent = h.tablet(b"parent");
    assert_eq!(parent.split_tablet_ids, children);
    let bounds: Vec<(Vec<u8>, Vec<u8>)> = children
        .iter()
        .map(|c| {
            let pb = h.tablet(c);
            assert_eq!(pb.state(), sys_tablets_entry_pb::State::CREATING);
            assert_eq!(pb.split_parent_tablet_id(), b"parent");
            assert_eq!(pb.split_depth(), 1);
            assert_eq!(pb.committed_consensus_state.config.peers.len(), 3);
            (
                pb.partition.partition_key_start().to_vec(),
                pb.partition.partition_key_end().to_vec(),
            )
        })
        .collect();
    assert_eq!(bounds, vec![(vec![], key.clone()), (key.clone(), vec![])]);
    let table: SysTablesEntryPB = h
        .snapshot()
        .get(SysRowEntryType::TABLE, b"t1")
        .unwrap()
        .parse()
        .unwrap();
    assert_eq!(table.partition_list_version(), 1);

    // A retried split resumes with the registered children.
    let again = h
        .splits
        .do_split(h.token, b"parent", vec![key], true)
        .await
        .unwrap();
    assert_eq!(again, children);
    assert_eq!(h.snapshot().tablets_of(b"t1").len(), 3);
}

#[tokio::test]
async fn the_parent_goes_once_its_children_cover_it() {
    let h = Harness::new().await;
    let children = h
        .splits
        .do_split(h.token, b"parent", vec![vec![0x80, 0x00]], true)
        .await
        .unwrap();
    let mut req = DeleteNotServingTabletRequestPB::new();
    req.set_tablet_id(b"parent".to_vec());
    let deadline = Instant::now() + Duration::from_secs(10);
    // The children are still CREATING: the range has a gap.
    let e = h
        .splits
        .delete_not_serving_tablet(h.token, deadline, &req)
        .await
        .unwrap_err();
    assert!(
        e.status.message().contains("not covered by child tablets"),
        "{e:?}"
    );
    // A child of a live parent cannot be split yet.
    h.set_running(&children[0]).await;
    let snapshot = h.snapshot();
    let child = snapshot.get(SysRowEntryType::TABLET, &children[0]).unwrap();
    let e = validate_split_candidate(&snapshot, child, true).unwrap_err();
    assert!(
        e.status.message().contains("parent is not yet deleted"),
        "{e:?}"
    );

    h.set_running(&children[1]).await;
    h.splits
        .delete_not_serving_tablet(h.token, deadline, &req)
        .await
        .unwrap();
    assert_eq!(
        h.tablet(b"parent").state(),
        sys_tablets_entry_pb::State::DELETED
    );
    let pending = h.ts_manager.deletes.pending();
    assert_eq!(pending.len(), 3);
    let e = h
        .splits
        .delete_not_serving_tablet(h.token, deadline, &req)
        .await
        .unwrap_err();
    assert!(
        e.status.message().contains("has been already deleted"),
        "{e:?}"
    );
    // With the parent gone, a child can be split.
    let snapshot = h.snapshot();
    let child = snapshot.get(SysRowEntryType::TABLET, &children[0]).unwrap();
    validate_split_candidate(&snapshot, child, true).unwrap();
}

#[tokio::test]
async fn split_candidates_follow_cpp() {
    let h = Harness::new().await;
    // A transaction status table is never split.
    let mut table: SysTablesEntryPB = h
        .snapshot()
        .get(SysRowEntryType::TABLE, b"t1")
        .unwrap()
        .parse()
        .unwrap();
    table.table_type = Some(EnumOrUnknown::new(TableType::TRANSACTION_STATUS_TABLE_TYPE));
    h.writer
        .submit(
            &h.token,
            Mutation::new().upsert(
                SysRowEntryType::TABLE,
                b"t1",
                table.write_to_bytes().unwrap(),
            ),
        )
        .await
        .unwrap();
    let snapshot = h.snapshot();
    let tablet = snapshot.get(SysRowEntryType::TABLET, b"parent").unwrap();
    let e = validate_split_candidate(&snapshot, tablet, true).unwrap_err();
    assert!(
        e.status.message().contains("transaction status tables"),
        "{e:?}"
    );

    // Each child must be a nonempty range.
    let mut parent = PartitionPB::new();
    parent.set_partition_key_start(vec![0x10]);
    parent.set_partition_key_end(vec![0x90]);
    let parent = MessageField::some(parent);
    assert!(child_partitions(b"p", &parent, &[vec![0x10]]).is_err());
    assert!(child_partitions(b"p", &parent, &[vec![0x95]]).is_err());
    let parts = child_partitions(b"p", &parent, &[vec![0x50]]).unwrap();
    assert_eq!(parts[0].partition_key_end(), &[0x50]);
    assert_eq!(parts[1].partition_key_start(), &[0x50]);
    assert_eq!(parts[1].partition_key_end(), &[0x90]);
}
