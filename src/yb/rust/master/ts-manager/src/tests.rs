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

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use master_catalog_state::{run_materializer, Catalog, CatalogWriter, WriterOptions};
use master_leadership::{ready_for_tests, Leadership};
use master_sys_catalog::{FakeSysCatalog, RowChange};
use master_tablet_locations::TabletLocations;
use protobuf::{EnumOrUnknown, Message};
use yb_proto::pb::catalog_entity_info::{
    sys_tables_entry_pb, sys_tablets_entry_pb, SysClusterConfigEntryPB, SysTablesEntryPB,
    SysTabletsEntryPB,
};
use yb_proto::pb::common_net::HostPortPB;
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::{ConsensusStatePB, PeerMemberType, RaftPeerPB};
use yb_proto::pb::master_heartbeat::{
    ReportedTabletPB, TSHeartbeatRequestPB, TSHeartbeatResponsePB, TabletReportPB,
};
use yb_proto::pb::master_rust_peer::PushLocationsRequestPB;
use yb_proto::pb::master_types::{SysRowEntryType, TSRegistrationPB};
use yb_proto::pb::tablet_types::RaftGroupStatePB;

use super::*;
use crate::peer::FollowerStreamState;

struct Setup {
    fake: Arc<FakeSysCatalog>,
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    leadership: Arc<Leadership>,
    ts: Arc<TsManager>,
}

fn upsert(entry_type: SysRowEntryType, id: &str, data: Vec<u8>) -> RowChange {
    RowChange::Upsert {
        entry_type,
        id: id.as_bytes().to_vec(),
        data,
    }
}

fn cstate(term: i64, index: i64, leader: &str, peers: &[&str]) -> ConsensusStatePB {
    let mut cstate = ConsensusStatePB::new();
    cstate.set_current_term(term);
    cstate.set_leader_uuid(leader.to_string());
    cstate
        .config
        .mut_or_insert_default()
        .set_committed_op_index(index);
    for peer in peers {
        let mut p = RaftPeerPB::new();
        p.set_permanent_uuid(peer.as_bytes().to_vec());
        p.member_type = Some(EnumOrUnknown::new(PeerMemberType::VOTER));
        cstate.config.mut_or_insert_default().peers.push(p);
    }
    cstate
}

async fn setup(persist: bool) -> Setup {
    let (fake, events) = FakeSysCatalog::new();
    let mut config = SysClusterConfigEntryPB::new();
    config.set_version(3);
    config.set_cluster_uuid("cluster-1".into());
    let mut table = SysTablesEntryPB::new();
    table.set_name(b"t".to_vec());
    table.set_version(0);
    table.schema = Some(Default::default()).into();
    table.set_state(sys_tables_entry_pb::State::RUNNING);
    let mut tablet = SysTabletsEntryPB::new();
    tablet.set_table_id(b"table".to_vec());
    tablet.set_state(sys_tablets_entry_pb::State::CREATING);
    fake.apply(vec![
        upsert(
            SysRowEntryType::CLUSTER_CONFIG,
            "",
            config.write_to_bytes().unwrap(),
        ),
        upsert(
            SysRowEntryType::TABLE,
            "table",
            table.write_to_bytes().unwrap(),
        ),
        upsert(
            SysRowEntryType::TABLET,
            "tablet",
            tablet.write_to_bytes().unwrap(),
        ),
    ]);
    let catalog = Catalog::new(fake.sender().clone());
    tokio::spawn(run_materializer(catalog.clone(), fake.clone(), events));
    let (leadership, _source) = ready_for_tests(1).await;
    let writer = CatalogWriter::start(
        catalog.clone(),
        fake.clone(),
        leadership.clone(),
        WriterOptions::default(),
    );
    catalog.barrier().await;
    let options = TsManagerOptions {
        persist_registry: if persist { || true } else { || false },
        unresponsive_timeout: || Duration::from_millis(50),
        ..Default::default()
    };
    let ts = TsManager::new(Arc::new(TabletLocations::default()), options);
    ts.on_leader_ready(&catalog.snapshot());
    Setup {
        fake,
        catalog,
        writer,
        leadership,
        ts,
    }
}

fn request(uuid: &str, seqno: i64, port: u32) -> TSHeartbeatRequestPB {
    let mut req = TSHeartbeatRequestPB::new();
    let instance = req
        .common
        .mut_or_insert_default()
        .ts_instance
        .mut_or_insert_default();
    instance.set_permanent_uuid(uuid.as_bytes().to_vec());
    instance.set_instance_seqno(seqno);
    let _ = port;
    req
}

fn registration(port: u32) -> TSRegistrationPB {
    let mut reg = TSRegistrationPB::new();
    let mut hp = HostPortPB::new();
    hp.set_host("127.0.0.1".into());
    hp.set_port(port);
    reg.common
        .mut_or_insert_default()
        .private_rpc_addresses
        .push(hp);
    reg
}

impl Setup {
    async fn heartbeat(&self, req: &TSHeartbeatRequestPB) -> Result<TSHeartbeatResponsePB, Status> {
        self.heartbeat_with_time_left(req, Duration::from_secs(10))
            .await
    }

    async fn heartbeat_with_time_left(
        &self,
        req: &TSHeartbeatRequestPB,
        time_to_deadline: Duration,
    ) -> Result<TSHeartbeatResponsePB, Status> {
        let ctx = HeartbeatContext {
            token: self.leadership.token().unwrap(),
            writer: &self.writer,
            snapshot: self.catalog.snapshot(),
            contributors: &[],
            master_instance: NodeInstancePB::new(),
            time_to_deadline,
        };
        self.ts.heartbeat(ctx, req).await
    }
}

#[tokio::test]
async fn register_then_report_then_live_list_versions() {
    let s = setup(false).await;
    // Unknown tserver without registration.
    let resp = s.heartbeat(&request("ts1", 1, 1)).await.unwrap();
    assert!(resp.needs_reregister());
    assert!(resp.needs_full_tablet_report());

    let mut req = request("ts1", 1, 1);
    req.registration = Some(registration(9100)).into();
    let resp = s.heartbeat(&req).await.unwrap();
    assert!(!resp.needs_reregister());
    assert_eq!(resp.cluster_uuid(), "cluster-1");
    assert_eq!(resp.cluster_config_version(), 3);
    assert!(resp.needs_full_tablet_report());
    assert_eq!(resp.tservers.len(), 1);
    let version = resp.live_tservers_version.clone().unwrap();
    assert_eq!(version.term(), 1);

    // A full report with the leader replica: locations, and the tablet turns RUNNING.
    let mut req = request("ts1", 1, 1);
    let mut report = TabletReportPB::new();
    report.set_is_incremental(false);
    report.set_sequence_number(0);
    report.set_full_report_seq_no(0);
    report.set_remaining_tablet_count(0);
    let mut tablet = ReportedTabletPB::new();
    tablet.set_tablet_id(b"tablet".to_vec());
    tablet.state = Some(EnumOrUnknown::new(RaftGroupStatePB::RUNNING));
    tablet.committed_consensus_state = Some(cstate(1, 5, "ts1", &["ts1"])).into();
    report.updated_tablets.push(tablet);
    req.tablet_report = Some(report).into();
    req.live_tservers_version = Some(version.clone()).into();
    let resp = s.heartbeat(&req).await.unwrap();
    assert!(!resp.needs_full_tablet_report(), "{resp:?}");
    assert_eq!(resp.tablet_report.tablets.len(), 1);
    // Echoed version: no list.
    assert!(resp.live_tservers_unchanged());
    assert!(resp.tservers.is_empty());
    let replicas = s.ts.locations.get(b"tablet").unwrap();
    assert!(replicas.confirmed);
    assert_eq!(replicas.leader().unwrap().ts_uuid.as_ref(), b"ts1");
    let tablet = s
        .catalog
        .snapshot()
        .get(SysRowEntryType::TABLET, b"tablet")
        .unwrap()
        .clone();
    assert_eq!(
        tablet.tablet().unwrap().state,
        sys_tablets_entry_pb::State::RUNNING as i32
    );
    let pb: SysTabletsEntryPB = tablet.parse().unwrap();
    assert_eq!(pb.committed_consensus_state.config.committed_op_index(), 5);
    assert!(s.ts.get(b"ts1").unwrap().has_tablet_report);

    // A second tserver changes membership: the old version gets the full list.
    let mut req = request("ts2", 1, 2);
    req.registration = Some(registration(9101)).into();
    s.heartbeat(&req).await.unwrap();
    let mut req = request("ts1", 1, 1);
    req.live_tservers_version = Some(version).into();
    let resp = s.heartbeat(&req).await.unwrap();
    assert!(!resp.live_tservers_unchanged());
    assert_eq!(resp.tservers.len(), 2);
}

#[tokio::test]
async fn host_port_collision_and_restart() {
    let s = setup(false).await;
    let mut req = request("ts1", 5, 1);
    req.registration = Some(registration(9100)).into();
    s.heartbeat(&req).await.unwrap();
    // Another uuid on the same address with an older seqno is refused.
    let mut req = request("other", 4, 1);
    req.registration = Some(registration(9100)).into();
    assert!(s.heartbeat(&req).await.unwrap().needs_reregister());
    // A newer one replaces it.
    let mut req = request("other", 6, 1);
    req.registration = Some(registration(9100)).into();
    assert!(!s.heartbeat(&req).await.unwrap().needs_reregister());
    assert_eq!(s.ts.get(b"ts1").unwrap().state(), TsState::REPLACED);
    // A restarted tserver (new seqno) must re-register.
    assert!(s
        .heartbeat(&request("other", 7, 1))
        .await
        .unwrap()
        .needs_reregister());
}

#[tokio::test]
async fn unresponsive_tserver_needs_a_full_report_and_is_persisted() {
    let s = setup(true).await;
    let mut req = request("ts1", 1, 1);
    req.registration = Some(registration(9100)).into();
    s.heartbeat(&req).await.unwrap();
    let row = s
        .catalog
        .snapshot()
        .get(SysRowEntryType::TSERVER_REGISTRATION, b"ts1")
        .cloned()
        .unwrap();
    let pb: SysTabletServerEntryPB = row.parse().unwrap();
    assert!(pb.persisted());
    assert_eq!(pb.state(), TsState::LIVE);

    let changed =
        s.ts.mark_unresponsive(Instant::now() + Duration::from_secs(1));
    assert_eq!(changed.len(), 1);
    assert!(s.ts.live().is_empty());
    assert!(!s.ts.get(b"ts1").unwrap().has_tablet_report);
    s.ts.persist(&s.writer, &s.leadership.token().unwrap(), &changed)
        .await
        .unwrap();
    let row = s
        .catalog
        .snapshot()
        .get(SysRowEntryType::TSERVER_REGISTRATION, b"ts1")
        .cloned()
        .unwrap();
    assert_eq!(
        row.parse::<SysTabletServerEntryPB>().unwrap().state(),
        TsState::UNRESPONSIVE
    );
    // A new leader loads the persisted registry.
    let other = TsManager::new(Arc::new(TabletLocations::default()), *s.ts.options());
    other.on_leader_ready(&s.catalog.snapshot());
    assert!(other.get(b"ts1").is_some());
    let _ = &s.fake;
}

#[tokio::test]
async fn deleted_table_replicas_are_deleted() {
    let s = setup(false).await;
    let mut table = SysTablesEntryPB::new();
    table.set_name(b"t".to_vec());
    table.set_version(0);
    table.schema = Some(Default::default()).into();
    table.set_state(sys_tables_entry_pb::State::DELETED);
    s.fake.apply(vec![upsert(
        SysRowEntryType::TABLE,
        "table",
        table.write_to_bytes().unwrap(),
    )]);
    s.catalog.barrier().await;
    let mut req = request("ts1", 1, 1);
    req.registration = Some(registration(9100)).into();
    s.heartbeat(&req).await.unwrap();
    let mut req = request("ts1", 1, 1);
    let mut report = TabletReportPB::new();
    report.set_is_incremental(false);
    report.set_sequence_number(0);
    let mut tablet = ReportedTabletPB::new();
    tablet.set_tablet_id(b"tablet".to_vec());
    report.updated_tablets.push(tablet);
    req.tablet_report = Some(report).into();
    s.heartbeat(&req).await.unwrap();
    let deletes = s.ts.deletes.drain();
    assert_eq!(deletes.len(), 1);
    assert_eq!(deletes[0].tablet_id, b"tablet");
}

#[test]
fn location_stream_gap_asks_for_full() {
    let leader = TsManager::new(Arc::default(), TsManagerOptions::default());
    leader.locations.set(
        b"t1",
        master_tablet_locations::TabletReplicas {
            replicas: vec![master_tablet_locations::Replica::new(
                b"ts1".as_slice().into(),
                PeerRole::LEADER,
                PeerMemberType::VOTER,
            )],
            confirmed: true,
        },
    );
    let follower = TsManager::new(Arc::default(), TsManagerOptions::default());
    let state = Mutex::new(FollowerStreamState::default());
    let mut delta = full_push(&leader, b"leader", 3);
    delta.set_full(false);
    delta.set_sequence(5);
    let push = |req: &PushLocationsRequestPB| apply_push(&follower, &state, req, false, 3).unwrap();
    assert!(push(&delta).needs_full());
    let mut full = full_push(&leader, b"leader", 3);
    full.set_sequence(1);
    assert!(!push(&full).needs_full());
    assert!(!follower.locations.get(b"t1").unwrap().confirmed);
    delta.set_sequence(2);
    assert!(!push(&delta).needs_full());
    // An older term is ignored.
    let mut old = full_push(&leader, b"old", 2);
    old.set_sequence(1);
    old.tablets.clear();
    push(&old);
    assert!(follower.locations.get(b"t1").is_some());
}

#[test]
fn a_deposed_leader_cannot_push_over_a_newer_term() {
    let leader = TsManager::new(Arc::default(), TsManagerOptions::default());
    let follower = TsManager::new(Arc::default(), TsManagerOptions::default());
    follower.locations.set(
        b"t1",
        master_tablet_locations::TabletReplicas {
            replicas: vec![],
            confirmed: true,
        },
    );
    let state = Mutex::new(FollowerStreamState::default());
    let mut stale = full_push(&leader, b"old-leader", 4);
    stale.set_sequence(1);
    // Never applied pushes, but this master's Raft term is already 5.
    apply_push(&follower, &state, &stale, false, 5).unwrap();
    assert!(follower.locations.get(b"t1").is_some());
    // A leader refuses pushes.
    assert!(apply_push(&follower, &state, &stale, true, 4).is_err());
    assert!(follower.locations.get(b"t1").is_some());
}

impl Setup {
    async fn register(&self, uuid: &str, seqno: i64, ports: &[u32]) -> TSHeartbeatResponsePB {
        let mut req = request(uuid, seqno, 0);
        let mut reg = registration(ports[0]);
        for port in &ports[1..] {
            let mut hp = HostPortPB::new();
            hp.set_host("127.0.0.1".into());
            hp.set_port(*port);
            reg.common
                .mut_or_insert_default()
                .broadcast_addresses
                .push(hp);
        }
        req.registration = Some(reg).into();
        self.heartbeat(&req).await.unwrap()
    }

    async fn report(&self, uuid: &str, tablets: Vec<ReportedTabletPB>) -> TSHeartbeatResponsePB {
        let mut req = request(uuid, 1, 0);
        let mut report = TabletReportPB::new();
        report.set_is_incremental(false);
        report.set_sequence_number(0);
        report.set_full_report_seq_no(0);
        report.set_remaining_tablet_count(0);
        report.updated_tablets = tablets;
        req.tablet_report = Some(report).into();
        self.heartbeat(&req).await.unwrap()
    }

    async fn set_tablet(
        &self,
        state: sys_tablets_entry_pb::State,
        cstate: Option<ConsensusStatePB>,
    ) {
        let mut tablet = SysTabletsEntryPB::new();
        tablet.set_table_id(b"table".to_vec());
        tablet.set_state(state);
        tablet.committed_consensus_state = cstate.into();
        self.fake.apply(vec![upsert(
            SysRowEntryType::TABLET,
            "tablet",
            tablet.write_to_bytes().unwrap(),
        )]);
        self.catalog.barrier().await;
    }
}

fn reported(cstate: Option<ConsensusStatePB>) -> ReportedTabletPB {
    let mut tablet = ReportedTabletPB::new();
    tablet.set_tablet_id(b"tablet".to_vec());
    tablet.state = Some(EnumOrUnknown::new(RaftGroupStatePB::RUNNING));
    tablet.committed_consensus_state = cstate.into();
    tablet
}

#[tokio::test]
async fn a_replaced_tablet_is_deleted_and_not_revived() {
    let s = setup(false).await;
    s.set_tablet(sys_tablets_entry_pb::State::REPLACED, None)
        .await;
    s.register("ts1", 1, &[9100]).await;
    s.report("ts1", vec![reported(Some(cstate(1, 5, "ts1", &["ts1"])))])
        .await;
    let deletes = s.ts.deletes.drain();
    assert_eq!(deletes.len(), 1);
    assert_eq!(deletes[0].token, s.leadership.token().unwrap());
    let snapshot = s.catalog.snapshot();
    let tablet = snapshot.get(SysRowEntryType::TABLET, b"tablet").unwrap();
    assert_eq!(
        tablet.tablet().unwrap().state,
        sys_tablets_entry_pb::State::REPLACED as i32
    );
}

#[tokio::test]
async fn a_reported_config_registers_unknown_peers_and_fills_the_replica_map() {
    let s = setup(false).await;
    s.set_tablet(
        sys_tablets_entry_pb::State::RUNNING,
        Some(cstate(1, 5, "ts1", &["ts1", "ts2", "ts3"])),
    )
    .await;
    s.register("ts1", 1, &[9100]).await;
    // The same config as stored: C++ rebuilds the map because it lacks ts2 and ts3.
    s.report(
        "ts1",
        vec![reported(Some(cstate(1, 5, "ts1", &["ts1", "ts2", "ts3"])))],
    )
    .await;
    let replicas = s.ts.locations.get(b"tablet").unwrap().replicas;
    assert_eq!(replicas.len(), 3);
    assert_eq!(s.ts.get(b"ts2").unwrap().state(), TsState::UNRESPONSIVE);
    assert_eq!(
        s.ts.get(b"ts2").unwrap().pb.xcluster_guarded_lease_state(),
        XClusterGuardedLeaseState::DEFINITELY_NO_LEASE
    );
    assert_eq!(s.ts.live().len(), 1);
}

#[tokio::test]
async fn an_evicted_replica_at_the_committed_index_is_tombstoned() {
    let s = setup(false).await;
    s.set_tablet(
        sys_tablets_entry_pb::State::RUNNING,
        Some(cstate(2, 5, "ts2", &["ts2", "ts3"])),
    )
    .await;
    s.register("ts1", 1, &[9100]).await;
    // Removed at index 5 while bootstrapping, it reports index 5 without itself.
    s.report(
        "ts1",
        vec![reported(Some(cstate(2, 5, "ts2", &["ts2", "ts3"])))],
    )
    .await;
    let deletes = s.ts.deletes.drain();
    assert_eq!(deletes.len(), 1, "{deletes:?}");
    assert_eq!(deletes[0].cas_config_index, Some(5));
    // A replica whose config change may still be pending in the current term is left alone.
    let mut pending = reported(Some(cstate(2, 5, "ts2", &["ts2", "ts3"])));
    let op_id = pending.pending_config_op_id.mut_or_insert_default();
    op_id.set_term(2);
    op_id.set_index(6);
    s.report("ts1", vec![pending]).await;
    assert!(s.ts.deletes.drain().is_empty());
}

#[tokio::test]
async fn a_refused_registration_changes_nothing() {
    let s = setup(false).await;
    s.register("ts1", 5, &[9100]).await;
    s.register("ts2", 9, &[9101]).await;
    // Collides with ts1 (older) and ts2 (newer): refused, and ts1 is not replaced.
    let resp = s.register("other", 7, &[9100, 9101]).await;
    assert!(resp.needs_reregister());
    assert_eq!(s.ts.get(b"ts1").unwrap().state(), TsState::LIVE);
    assert!(s.ts.get(b"other").is_none());
}

#[tokio::test]
async fn deleted_tablets_do_not_block_removing_a_tserver() {
    let s = setup(false).await;
    s.set_tablet(sys_tablets_entry_pb::State::RUNNING, None)
        .await;
    s.ts.locations.set(
        b"tablet",
        master_tablet_locations::TabletReplicas {
            replicas: vec![master_tablet_locations::Replica::new(
                b"ts1".as_slice().into(),
                PeerRole::FOLLOWER,
                PeerMemberType::VOTER,
            )],
            confirmed: true,
        },
    );
    s.register("ts1", 1, &[9100]).await;
    s.ts.mark_unresponsive(Instant::now() + Duration::from_secs(1));
    s.ts.update_descriptor(b"ts1", |d| {
        d.pb.xcluster_guarded_lease_state = Some(EnumOrUnknown::new(
            XClusterGuardedLeaseState::DEFINITELY_NO_LEASE,
        ));
    });
    assert!(s.ts.remove(&s.catalog.snapshot(), b"ts1", true).is_err());
    s.set_tablet(sys_tablets_entry_pb::State::DELETED, None)
        .await;
    s.ts.remove(&s.catalog.snapshot(), b"ts1", true).unwrap();
}

#[tokio::test]
async fn leader_leases_from_heartbeats_feed_the_leaderless_check() {
    use master_tablet_locations::LeaderlessCheck;
    use yb_proto::pb::consensus_types::LeaderLeaseStatus;
    use yb_proto::pb::master_heartbeat::{TabletDriveStorageMetadataPB, TabletLeaderMetricsPB};

    let s = setup(false).await;
    s.set_tablet(
        sys_tablets_entry_pb::State::RUNNING,
        Some(cstate(1, 5, "ts1", &["ts1"])),
    )
    .await;
    s.register("ts1", 1, &[9100]).await;
    s.report("ts1", vec![reported(Some(cstate(1, 5, "ts1", &["ts1"])))])
        .await;
    let term_start = std::time::Instant::now();
    let delay = Duration::from_millis(20);
    let leaderless = |now| {
        s.ts.locations.leaderless(
            [&b"tablet"[..]],
            &LeaderlessCheck {
                now,
                alert_delay: delay,
                observable_from: term_start,
                last_lease_report: &|uuid| s.ts.live_lease_report_time(uuid),
            },
        )
    };
    // An incremental heartbeat with storage metadata and ts1's lease of the tablet.
    let lease_report = |status| {
        let mut req = request("ts1", 1, 0);
        let mut metadata = TabletDriveStorageMetadataPB::new();
        metadata.set_tablet_id(b"tablet".to_vec());
        req.storage_metadata.push(metadata);
        let mut info = TabletLeaderMetricsPB::new();
        info.set_tablet_id(b"tablet".to_vec());
        info.set_leader_lease_status(status);
        info.set_ht_lease_expiration(
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_micros() as u64,
        );
        req.leader_info.push(info);
        req
    };

    // ts1 leads, and the leases recorded after the alert delay show none valid.
    tokio::time::sleep(delay * 2).await;
    s.heartbeat(&lease_report(
        LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE,
    ))
    .await
    .unwrap();
    assert_eq!(leaderless(std::time::Instant::now()).len(), 1);

    // A valid lease: whatever the clock says later, nothing observed contradicts it.
    s.heartbeat(&lease_report(LeaderLeaseStatus::HAS_LEASE))
        .await
        .unwrap();
    let later = std::time::Instant::now() + Duration::from_secs(60);
    assert!(leaderless(later).is_empty());

    // A heartbeat with too little time left keeps the tserver live but skips its leases, so it is
    // no evidence of a lost leader either.
    tokio::time::sleep(delay * 2).await;
    s.heartbeat_with_time_left(
        &lease_report(LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE),
        Duration::from_secs(1),
    )
    .await
    .unwrap();
    assert!(leaderless(later).is_empty());
}

#[tokio::test]
async fn a_faulty_drive_is_kept_until_the_tserver_registers_again() {
    let s = setup(false).await;
    let mut req = request("ts1", 5, 1);
    req.registration = Some(registration(9100)).into();
    s.heartbeat(&req).await.unwrap();
    assert!(!s.ts.get(b"ts1").unwrap().has_faulty_drive);
    let mut req = request("ts1", 5, 1);
    req.set_faulty_drive(true);
    s.heartbeat(&req).await.unwrap();
    assert!(s.ts.get(b"ts1").unwrap().has_faulty_drive);
    // A heartbeat without the field does not clear it.
    s.heartbeat(&request("ts1", 5, 1)).await.unwrap();
    assert!(s.ts.get(b"ts1").unwrap().has_faulty_drive);
    // A restarted tserver registers again with its drives.
    let mut req = request("ts1", 6, 1);
    req.registration = Some(registration(9100)).into();
    s.heartbeat(&req).await.unwrap();
    assert!(!s.ts.get(b"ts1").unwrap().has_faulty_drive);
}

#[tokio::test]
async fn a_rebuilt_replica_map_keeps_each_peers_own_state() {
    let s = setup(false).await;
    let peers = ["ts1", "ts2", "ts3"];
    s.set_tablet(
        sys_tablets_entry_pb::State::RUNNING,
        Some(cstate(1, 5, "ts1", &peers)),
    )
    .await;
    s.register("ts1", 1, &[9100]).await;
    s.register("ts2", 1, &[9101]).await;
    s.report("ts1", vec![reported(Some(cstate(1, 5, "ts1", &peers)))])
        .await;
    // ts2 won an election and reports first: the map is rebuilt from its config, and ts1, which
    // may not report the tablet again, keeps the state it reported.
    s.report("ts2", vec![reported(Some(cstate(2, 5, "ts2", &peers)))])
        .await;
    let replicas = s.ts.locations.get(b"tablet").unwrap().replicas;
    let state = |uuid: &str| {
        replicas
            .iter()
            .find(|r| &*r.ts_uuid == uuid.as_bytes())
            .unwrap()
            .state
    };
    assert_eq!(state("ts1"), RaftGroupStatePB::RUNNING);
    assert_eq!(state("ts2"), RaftGroupStatePB::RUNNING);
    assert_eq!(state("ts3"), RaftGroupStatePB::UNKNOWN);
}
