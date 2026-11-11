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
async fn the_parent_of_a_streamed_table_is_kept() {
    let h = Harness::new().await;
    let children = h
        .splits
        .do_split(h.token, b"parent", vec![vec![0x80, 0x00]], true)
        .await
        .unwrap();
    h.set_running(&children[0]).await;
    h.set_running(&children[1]).await;
    // A C++ leader split a table with an outbound stream, whose pollers still read the parent.
    let mut stream = yb_proto::pb::catalog_entity_info::SysCDCStreamEntryPB::new();
    stream.table_id.push("t1".to_string());
    h.writer
        .submit(
            &h.token,
            Mutation::new().upsert(
                SysRowEntryType::CDC_STREAM,
                b"stream",
                stream.write_to_bytes().unwrap(),
            ),
        )
        .await
        .unwrap();
    let mut req = DeleteNotServingTabletRequestPB::new();
    req.set_tablet_id(b"parent".to_vec());
    let deadline = Instant::now() + Duration::from_secs(10);
    let e = h
        .splits
        .delete_not_serving_tablet(h.token, deadline, &req)
        .await
        .unwrap_err();
    assert!(
        e.status.message().contains("xCluster or CDC stream"),
        "{e:?}"
    );
    assert_eq!(
        h.tablet(b"parent").state(),
        sys_tablets_entry_pb::State::RUNNING
    );
    assert!(h.ts_manager.deletes.pending().is_empty());
}

#[tokio::test]
async fn a_parent_with_a_single_registered_child_is_not_sent() {
    let h = Harness::new().await;
    // As a C++ master older than #19954 could leave it: one child registered, CREATING.
    let mut child = h.tablet(b"parent");
    child.state = Some(EnumOrUnknown::new(sys_tablets_entry_pb::State::CREATING));
    child.set_split_parent_tablet_id(b"parent".to_vec());
    let mut parent = h.tablet(b"parent");
    parent.split_tablet_ids.push(b"child".to_vec());
    h.writer
        .submit(
            &h.token,
            Mutation::new()
                .upsert(
                    SysRowEntryType::TABLET,
                    b"child",
                    child.write_to_bytes().unwrap(),
                )
                .upsert(
                    SysRowEntryType::TABLET,
                    b"parent",
                    parent.write_to_bytes().unwrap(),
                ),
        )
        .await
        .unwrap();
    let snapshot = h.snapshot();
    let entity = snapshot.get(SysRowEntryType::TABLET, b"parent").unwrap();
    let e = crate::send_split(&h.splits, h.token, &snapshot, entity)
        .await
        .unwrap_err();
    assert_eq!(e.code, StatusCode::IllegalState, "{e:?}");
    // A pass skips it rather than sending.
    let scope = master_task_runtime::TermScope::new(3, Arc::default());
    let controller = crate::SplitController::new(h.splits.clone());
    controller.pass(&scope);
    assert_eq!(
        controller.sent.lock().unwrap().get(b"parent".as_slice()),
        Some(&false)
    );
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

mod automatic {
    use std::collections::{HashMap, HashSet};

    use master_tablet_locations::{DriveInfo, Replica, TabletReplicas};
    use yb_proto::pb::common_net::CloudInfoPB;
    use yb_proto::pb::common_types::PeerRole;
    use yb_proto::pb::consensus_metadata::PeerMemberType;
    use yb_proto::pb::tablet_types::RaftGroupStatePB;

    use super::*;
    use crate::auto::{plan, AutoSplitOptions, Cluster, Disabled, Plan};

    const PEERS: [&str; 3] = ["ts0", "ts1", "ts2"];

    fn options() -> AutoSplitOptions {
        AutoSplitOptions {
            enabled: || true,
            low_phase_size_threshold_bytes: || 1000,
            outstanding_limit_per_tserver: || 1,
            ..AutoSplitOptions::default()
        }
    }

    fn cluster() -> Cluster {
        Cluster {
            num_live: 3,
            valid: vec![CloudInfoPB::new(); 3],
            ..Cluster::default()
        }
    }

    /// Three running voters, ts0 leading, the leader's SST files `size` bytes.
    fn replicas(size: u64) -> TabletReplicas {
        TabletReplicas {
            replicas: PEERS
                .iter()
                .enumerate()
                .map(|(i, ts)| {
                    let role = if i == 0 {
                        PeerRole::LEADER
                    } else {
                        PeerRole::FOLLOWER
                    };
                    let mut r = Replica::new(Arc::from(ts.as_bytes()), role, PeerMemberType::VOTER);
                    r.state = RaftGroupStatePB::RUNNING;
                    r.drive = DriveInfo {
                        sst_files_size: size,
                        may_have_orphaned_post_split_data: false,
                        ..DriveInfo::default()
                    };
                    r
                })
                .collect(),
            confirmed: true,
        }
    }

    /// A RUNNING tablet of t1 from `start`, with a config of the three peers.
    async fn add_tablet(h: &Harness, id: &[u8], start: &[u8], state: sys_tablets_entry_pb::State) {
        let mut pb = h.tablet(b"parent");
        pb.partition
            .mut_or_insert_default()
            .set_partition_key_start(start.to_vec());
        pb.state = Some(EnumOrUnknown::new(state));
        pb.split_tablet_ids.clear();
        h.writer
            .submit(
                &h.token,
                Mutation::new().upsert(SysRowEntryType::TABLET, id, pb.write_to_bytes().unwrap()),
            )
            .await
            .unwrap();
    }

    fn run(
        h: &Harness,
        sizes: &HashMap<Vec<u8>, TabletReplicas>,
        cluster: &Cluster,
        disabled: &mut Disabled,
        options: &AutoSplitOptions,
    ) -> Plan {
        plan(
            &h.snapshot(),
            cluster,
            &|id| sizes.get(id).cloned(),
            &HashSet::new(),
            disabled,
            options,
            Instant::now(),
        )
    }

    fn scheduled(plan: &Plan) -> Vec<String> {
        plan.schedule
            .iter()
            .map(|(id, _)| String::from_utf8_lossy(id).into_owned())
            .collect()
    }

    #[tokio::test]
    async fn the_largest_tablet_over_its_phase_threshold_is_split_first() {
        let h = Harness::new().await;
        add_tablet(&h, b"second", &[0x80], sys_tablets_entry_pb::State::RUNNING).await;
        add_tablet(&h, b"third", &[0xc0], sys_tablets_entry_pb::State::RUNNING).await;
        // 3 partitions on 3 tservers: 1 per tserver, the high phase (10 GB by default).
        let mut sizes = HashMap::from([
            (b"parent".to_vec(), replicas(5000)),
            (b"second".to_vec(), replicas(20 << 30)),
            (b"third".to_vec(), replicas(10)),
        ]);
        let mut disabled = Disabled::default();
        let p = run(&h, &sizes, &cluster(), &mut disabled, &options());
        assert_eq!(scheduled(&p), vec!["second"]);
        assert_eq!((p.candidates, p.outstanding), (1, 1));

        // With 4 tservers the table is in the low phase: both large tablets qualify, but with one
        // split per tserver only the largest starts, and the smaller one is below 0.8 of it.
        let four = Cluster {
            num_live: 4,
            valid: vec![CloudInfoPB::new(); 4],
            ..Cluster::default()
        };
        let p = run(&h, &sizes, &four, &mut disabled, &options());
        assert_eq!(scheduled(&p), vec!["second"]);
        assert_eq!(p.candidates, 2);
        // Comparable sizes on separate tservers: both start, largest first.
        sizes.insert(b"parent".to_vec(), replicas(19 << 30));
        let mut other = replicas(19 << 30);
        for (r, ts) in other.replicas.iter_mut().zip(["ts3", "ts4", "ts5"]) {
            r.ts_uuid = Arc::from(ts.as_bytes());
        }
        sizes.insert(b"parent".to_vec(), other);
        let p = run(&h, &sizes, &four, &mut disabled, &options());
        assert_eq!(scheduled(&p), vec!["second", "parent"]);
    }

    #[tokio::test]
    async fn a_tablet_with_a_bootstrapping_replica_waits() {
        let h = Harness::new().await;
        let mut r = replicas(5000);
        r.replicas[2].state = RaftGroupStatePB::BOOTSTRAPPING;
        let sizes = HashMap::from([(b"parent".to_vec(), r)]);
        let p = run(&h, &sizes, &cluster(), &mut Disabled::default(), &options());
        assert!(p.schedule.is_empty());
        let mut r = replicas(5000);
        r.replicas[2].member_type = PeerMemberType::PRE_VOTER;
        let sizes = HashMap::from([(b"parent".to_vec(), r)]);
        let p = run(&h, &sizes, &cluster(), &mut Disabled::default(), &options());
        assert!(p.schedule.is_empty());
    }

    #[tokio::test]
    async fn registered_splits_count_against_the_limits() {
        let h = Harness::new().await;
        let children = h
            .splits
            .do_split(h.token, b"parent", vec![vec![0x80, 0x00]], true)
            .await
            .unwrap();
        add_tablet(&h, b"other", &[0xf0], sys_tablets_entry_pb::State::RUNNING).await;
        // The children replace the parent as partitions: 3 on 3 tservers, the high phase.
        let mut sizes = HashMap::from([
            (b"parent".to_vec(), replicas(5000)),
            (b"other".to_vec(), replicas(20 << 30)),
        ]);
        // A child not running: the split restarts, and holds the slot on the tservers of
        // `other`.
        let p = run(&h, &sizes, &cluster(), &mut Disabled::default(), &options());
        assert!(p.schedule.is_empty(), "{:?}", scheduled(&p));
        assert_eq!((p.candidates, p.outstanding), (1, 1));

        // Children running but compacting: still outstanding.
        h.set_running(&children[0]).await;
        h.set_running(&children[1]).await;
        for c in &children {
            let mut r = replicas(10);
            r.replicas[1].drive.may_have_orphaned_post_split_data = true;
            sizes.insert(c.clone(), r);
        }
        let p = run(&h, &sizes, &cluster(), &mut Disabled::default(), &options());
        assert!(p.schedule.is_empty());
        assert_eq!(p.outstanding, 1);

        // Compacted: `other` may split; the children are too small.
        for c in &children {
            sizes.insert(c.clone(), replicas(10));
        }
        let p = run(&h, &sizes, &cluster(), &mut Disabled::default(), &options());
        assert_eq!(scheduled(&p), vec!["other"]);
    }

    #[tokio::test]
    async fn a_ttl_table_is_held_off() {
        let h = Harness::new().await;
        let mut table: SysTablesEntryPB = h
            .snapshot()
            .get(SysRowEntryType::TABLE, b"t1")
            .unwrap()
            .parse()
            .unwrap();
        table
            .schema
            .mut_or_insert_default()
            .table_properties
            .mut_or_insert_default()
            .set_default_time_to_live(60_000);
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
        let sizes = HashMap::from([(b"parent".to_vec(), replicas(5000))]);
        let mut c = cluster();
        c.ttl_split_disabled.insert(b"ts0".to_vec());
        let mut disabled = Disabled::default();
        let p = run(&h, &sizes, &c, &mut disabled, &options());
        assert!(p.schedule.is_empty());
        assert!(disabled.ttl_tables.contains_key(b"t1".as_slice()));
        // Held off even once the tserver no longer asks.
        let p = run(&h, &sizes, &cluster(), &mut disabled, &options());
        assert!(p.schedule.is_empty());
    }
}

mod split_key {
    use std::sync::atomic::{AtomicUsize, Ordering};

    use crate::{GetSplitKeyRequestPB, GetSplitKeyResponsePB};
    use master_tablet_locations::Replica;
    use yb_host_api::rpc::FakeRpcClient;
    use yb_proto::pb::common_net::HostPortPB;
    use yb_proto::pb::common_types::PeerRole;
    use yb_proto::pb::consensus_metadata::PeerMemberType;
    use yb_proto::pb::master_heartbeat::TSInformationPB;
    use yb_proto::pb::tserver_types::tablet_server_error_pb::Code as TsCode;
    use yb_proto::pb::wire_protocol::app_status_pb::ErrorCode;

    use super::*;

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

    fn lead(h: &Harness, uuid: &str) {
        h.ts_manager.locations.update_replica(
            b"parent",
            Replica::new(
                uuid.as_bytes().into(),
                PeerRole::LEADER,
                PeerMemberType::VOTER,
            ),
        );
        for other in ["ts0", "ts1"].into_iter().filter(|o| *o != uuid) {
            h.ts_manager.locations.update_replica(
                b"parent",
                Replica::new(
                    other.as_bytes().into(),
                    PeerRole::FOLLOWER,
                    PeerMemberType::VOTER,
                ),
            );
        }
    }

    fn error(code: TsCode, status: ErrorCode) -> Vec<u8> {
        let mut resp = GetSplitKeyResponsePB::new();
        let e = resp.error.mut_or_insert_default();
        e.set_code(code);
        e.status.mut_or_insert_default().set_code(status);
        resp.write_to_bytes().unwrap()
    }

    async fn harness(
        handler: impl Fn(&str) -> Vec<u8> + Send + Sync + 'static,
    ) -> (Harness, Arc<AtomicUsize>) {
        let h = Harness::new().await;
        h.ts_manager
            .apply_peer_tservers(&[tserver("ts0"), tserver("ts1")], true);
        lead(&h, "ts0");
        let calls = Arc::new(AtomicUsize::new(0));
        let counter = calls.clone();
        h.splits.set_rpc(Arc::new(FakeRpcClient::new(
            move |host, _, _, method, req| {
                assert_eq!(method, "GetSplitKey");
                GetSplitKeyRequestPB::parse_from_bytes(req).unwrap();
                counter.fetch_add(1, Ordering::SeqCst);
                Ok(handler(host))
            },
        )));
        (h, calls)
    }

    #[tokio::test(start_paused = true)]
    async fn a_moved_leader_is_asked_next() {
        let moved = Arc::new(std::sync::Mutex::new(None::<Arc<TsManager>>));
        let to_move = moved.clone();
        let (h, _) = harness(move |host| {
            if host == "ts0" {
                // ts0 lost leadership to ts1 since the split started.
                if let Some(ts_manager) = to_move.lock().unwrap().as_ref() {
                    ts_manager.locations.update_replica(
                        b"parent",
                        Replica::new(
                            b"ts1".as_slice().into(),
                            PeerRole::LEADER,
                            PeerMemberType::VOTER,
                        ),
                    );
                    ts_manager.locations.update_replica(
                        b"parent",
                        Replica::new(
                            b"ts0".as_slice().into(),
                            PeerRole::FOLLOWER,
                            PeerMemberType::VOTER,
                        ),
                    );
                }
                return error(TsCode::NOT_THE_LEADER, ErrorCode::ILLEGAL_STATE);
            }
            let mut resp = GetSplitKeyResponsePB::new();
            resp.split_partition_keys.push(vec![0x80, 0x00]);
            resp.write_to_bytes().unwrap()
        })
        .await;
        *moved.lock().unwrap() = Some(h.ts_manager.clone());
        let keys = h
            .splits
            .split_key(h.token, b"parent", 2, true)
            .await
            .unwrap();
        assert_eq!(keys, vec![vec![0x80, 0x00]]);
    }

    #[tokio::test(start_paused = true)]
    async fn a_missing_tablet_is_not_retried() {
        let (h, calls) = harness(|_| error(TsCode::TABLET_NOT_FOUND, ErrorCode::NOT_FOUND)).await;
        let e = h
            .splits
            .split_key(h.token, b"parent", 2, true)
            .await
            .unwrap_err();
        assert_eq!(e.code, StatusCode::NotFound, "{e:?}");
        assert_eq!(calls.load(Ordering::SeqCst), 1);
    }
}
