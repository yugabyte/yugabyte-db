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

//! `load_balancer_mocked-test.cc`, ported scenario by scenario (K8): the harness mirrors
//! `LoadBalancerMockedBase`, with tservers "0000", "1111", ... and one table "my_table_id" whose
//! tablets each have a replica on every tserver, the leader of tablet i on tserver i % n.

use std::collections::{BTreeSet, HashMap};

use yb_proto::pb::common_net::{CloudInfoPB, PlacementBlockPB, ReplicationInfoPB};
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::tablet_types::RaftGroupStatePB;

use crate::distribution::TsTableLoadMap;
use crate::planner::testing::analyze;
use crate::state::RunMode;
use crate::{
    calculate_optimal_load_distribution, calculate_table_load_difference, ClusterBalancer,
    ClusterView, Options, ReplicaInfo, TableInfo, TabletInfo, TsInfo,
};

const MB: u64 = 1 << 20;

fn cloud(az: &str) -> CloudInfoPB {
    let mut ci = CloudInfoPB::new();
    ci.set_placement_cloud("aws".into());
    ci.set_placement_region("us-west-1".into());
    ci.set_placement_zone(az.into());
    ci
}

/// `SetupTS`.
fn ts(uuid: &str, az: &str) -> TsInfo {
    TsInfo {
        uuid: uuid.into(),
        cloud_info: cloud(az),
        live_and_reported: true,
        ..TsInfo::default()
    }
}

/// `SetupClusterConfig`: one block per zone with a minimum of 1.
fn cluster_config(azs: &[&str], num_replicas: i32) -> ReplicationInfoPB {
    let mut ri = ReplicationInfoPB::new();
    let live = ri.live_replicas.mut_or_insert_default();
    live.set_num_replicas(num_replicas);
    for az in azs {
        let mut block = PlacementBlockPB::new();
        block.cloud_info = Some(cloud(az)).into();
        block.set_min_num_replicas(1);
        live.placement_blocks.push(block);
    }
    ri
}

/// `CreateTable`: tablets "tablet--0", "tablet-0-1", ..., "tablet-<n-2>-".
fn tablet_ids(n: usize) -> Vec<String> {
    (0..n)
        .map(|i| {
            let start = if i == 0 {
                String::new()
            } else {
                (i - 1).to_string()
            };
            let end = if i == n - 1 {
                String::new()
            } else {
                i.to_string()
            };
            format!("tablet-{start}-{end}")
        })
        .collect()
}

struct Harness {
    view: ClusterView,
    options: Options,
    /// `options` as a run normalizes them.
    normalized: Options,
    balancer: ClusterBalancer,
    mode: RunMode,
}

impl Harness {
    fn new(num_tablets: usize) -> Harness {
        let table = TableInfo {
            id: "my_table_id".into(),
            tablets: tablet_ids(num_tablets)
                .into_iter()
                .map(|id| TabletInfo {
                    id,
                    running: true,
                    ..TabletInfo::default()
                })
                .collect(),
            ..TableInfo::default()
        };
        Harness {
            view: ClusterView {
                tables: vec![table],
                ..ClusterView::default()
            },
            options: Options {
                check_ts_liveness: false,
                ..Options::default()
            },
            normalized: Options::default(),
            balancer: ClusterBalancer::new(),
            mode: RunMode {
                read_only: false,
                placement_uuid: String::new(),
                live_placement_uuid: String::new(),
            },
        }
    }

    /// `PrepareTestState`: a replica of every tablet on every tserver.
    fn prepare(&mut self, tservers: Vec<TsInfo>) {
        self.view.tservers = tservers;
        let n = self.view.tservers.len();
        let uuids: Vec<String> = self.view.tservers.iter().map(|t| t.uuid.clone()).collect();
        let table = &mut self.view.tables[0];
        table.replication_info = ReplicationInfoPB::new();
        table.pending = Default::default();
        for (i, tablet) in table.tablets.iter_mut().enumerate() {
            tablet.replicas = uuids
                .iter()
                .enumerate()
                .map(|(j, uuid)| {
                    (
                        uuid.clone(),
                        ReplicaInfo {
                            is_leader: i % n == j,
                            ..ReplicaInfo::default()
                        },
                    )
                })
                .collect();
        }
    }

    /// `PrepareTestStateSingleAz` / `PrepareTestStateMultiAz` with `n` tservers.
    fn prepare_az(&mut self, n: usize, multi_az: bool) {
        let tservers = (0..n)
            .map(|i| {
                let uuid = char::from(b'0' + i as u8).to_string().repeat(4);
                let az = if multi_az {
                    char::from(b'a' + i as u8).to_string()
                } else {
                    "a".into()
                };
                ts(&uuid, &az)
            })
            .collect();
        self.prepare(tservers);
    }

    fn table(&mut self) -> &mut TableInfo {
        &mut self.view.tables[0]
    }

    fn ts_mut(&mut self, uuid: &str) -> &mut TsInfo {
        self.view
            .tservers
            .iter_mut()
            .find(|t| t.uuid == uuid)
            .unwrap()
    }

    fn remove_replica(&mut self, tablet: usize, uuid: &str) {
        assert!(self.table().tablets[tablet].replicas.remove(uuid).is_some());
    }

    fn add_running_replica(&mut self, tablet: usize, uuid: &str) {
        let replicas = &mut self.table().tablets[tablet].replicas;
        assert!(replicas
            .insert(uuid.into(), ReplicaInfo::default())
            .is_none());
    }

    fn move_tablet_leader(&mut self, tablet: usize, uuid: &str) {
        for (ts, replica) in &mut self.table().tablets[tablet].replicas {
            replica.is_leader = ts == uuid;
        }
    }

    fn keep_only_replicas_on(&mut self, tablet: usize, keep: &[&str]) {
        self.table().tablets[tablet]
            .replicas
            .retain(|ts, _| keep.contains(&ts.as_str()));
    }

    fn set_sizes(&mut self, tablet: usize, size: u64) {
        for replica in self.table().tablets[tablet].replicas.values_mut() {
            replica.total_size = size;
        }
    }

    fn set_all_sizes(&mut self, size: u64) {
        for i in 0..self.view.tables[0].tablets.len() {
            self.set_sizes(i, size);
        }
    }

    fn set_disk(&mut self, uuid: &str, used: u64, total: u64) {
        let ts = self.ts_mut(uuid);
        ts.disk_used_bytes = used;
        ts.disk_capacity_bytes = total;
    }

    fn leader_of(&self, tablet_id: &str) -> Option<String> {
        self.view.tables[0]
            .tablets
            .iter()
            .find(|t| t.id == tablet_id)?
            .replicas
            .iter()
            .find(|(_, r)| r.is_leader)
            .map(|(ts, _)| ts.clone())
    }
}

/// `ResetLoadBalancerAndAnalyzeTablets`.
macro_rules! analyze {
    ($h:expr) => {{
        $h.normalized = $h.options.normalized();
        analyze(&mut $h.balancer, &$h.view, &$h.normalized, &$h.mode).unwrap()
    }};
}

/// `TestAddLoad`: a missing-placement add, else a regular add; a move's source is then removed.
fn add_load(
    run: &mut crate::planner::Run<'_, '_>,
    tablet: &str,
    from: &str,
    to: &str,
) -> Result<(String, String, String), String> {
    let over_at_start = run.state().tablets_over_replicated.len();
    let (t, f, d) = match run.one_add_if_missing_placement()? {
        Some((t, d)) => (t, String::new(), d),
        None => run.add_replicas()?.ok_or("Failed to add replica")?,
    };
    for (expected, actual, what) in [
        (tablet, &t, "Tablet id"),
        (from, &f, "From ts"),
        (to, &d, "To ts"),
    ] {
        if !expected.is_empty() && expected != actual {
            return Err(format!(
                "{what} mismatch: expected {expected}, got {actual}"
            ));
        }
    }
    if !f.is_empty() {
        if run.state().tablets_over_replicated.len() != over_at_start + 1 {
            return Err("Overreplication count mismatch".into());
        }
        run.remove(&t, &f)?;
    }
    Ok((t, f, d))
}

fn starting_tablet_replica() -> ReplicaInfo {
    ReplicaInfo {
        state: RaftGroupStatePB::BOOTSTRAPPING,
        ..ReplicaInfo::default()
    }
}

#[test]
fn starting_tablet() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    let first = h.view.tservers[0].uuid.clone();
    h.table().tablets[0]
        .replicas
        .insert(first, starting_tablet_replica());
    let run = analyze!(h);
    assert_eq!(run.state().tablets_over_replicated.len(), 0);
    assert_eq!(run.total_starting_tablets(), 1);
    assert_eq!(run.state().total_running, 12 - 1);
}

#[test]
fn pending_stepdown() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    let tablet_id = h.view.tables[0].tablets[0].id.clone();
    let old_leader = h.leader_of(&tablet_id).unwrap();
    let new_leader = h.view.tservers[1].uuid.clone();
    h.table()
        .pending
        .stepdowns
        .insert(tablet_id.clone(), new_leader.clone());
    let run = analyze!(h);
    assert_eq!(
        run.leader_of(&tablet_id).as_deref(),
        Some(new_leader.as_str())
    );
    assert!(!run.leads(&old_leader, &tablet_id));
    assert!(run.leads(&new_leader, &tablet_id));
}

#[test]
fn no_placement() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table()
        .replication_info
        .live_replicas
        .mut_or_insert_default()
        .set_num_replicas(3);
    {
        let run = analyze!(h);
        assert_eq!(run.state().tablets_over_replicated.len(), 0);
        assert_eq!(run.total_starting_tablets(), 0);
        assert_eq!(run.state().total_running, 12);
    }
    h.view.tservers.push(ts("3333", "a"));
    let mut run = analyze!(h);
    // Equal loads: the largest uuid first, then the next.
    add_load(&mut run, "", "2222", "3333").unwrap();
    add_load(&mut run, "", "1111", "3333").unwrap();
    add_load(&mut run, "", "0000", "3333").unwrap();
    assert_eq!(run.state().total_running, 12 - 3);
    assert_eq!(run.total_starting_tablets(), 3);
    assert_eq!(run.state().tablets_over_replicated.len(), 0);
}

#[test]
fn with_placement() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table().replication_info = cluster_config(&["a", "b", "c"], 3);
    h.view
        .tservers
        .extend([ts("3333", "WRONG"), ts("4444", "a"), ts("5555", "a")]);
    let mut run = analyze!(h);
    assert_eq!(run.state().total_running, 12);
    // Only from ts0, to the matching zone's least loaded, lowest uuid tserver first.
    add_load(&mut run, "", "0000", "4444").unwrap();
    add_load(&mut run, "", "0000", "5555").unwrap();
}

#[test]
fn with_missing_placement() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table().replication_info = cluster_config(&["a", "b", "c"], 3);
    for i in 0..4 {
        h.remove_replica(i, "2222");
        h.move_tablet_leader(i, "0000");
    }
    h.view.tservers.pop();
    h.view.tservers.extend([ts("2new", "c"), ts("3new", "c")]);
    let mut run = analyze!(h);
    add_load(&mut run, "", "", "2new").unwrap();
    add_load(&mut run, "", "", "3new").unwrap();
    add_load(&mut run, "", "", "2new").unwrap();
    add_load(&mut run, "", "", "3new").unwrap();
    // 4,4,2,2: zone a and b load cannot go to zone c.
    assert!(add_load(&mut run, "", "", "").is_err());
}

#[test]
fn over_replication() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table().replication_info = cluster_config(&["a"], 3);
    for i in 0..4 {
        h.remove_replica(i, "1111");
        h.remove_replica(i, "2222");
    }
    h.view.tservers.truncate(1);
    h.view.tservers.push(ts("1111", "a"));
    for i in 0..4 {
        h.add_running_replica(i, "1111");
    }
    h.view.tservers.extend([
        ts("2222", "WRONG"),
        ts("3333", "a"),
        ts("4444", "a"),
        ts("5555", "a"),
    ]);
    // Tablet 0: over-replicated with a wrongly placed replica; tablet 1: over-replicated; tablet 2:
    // at RF with a wrongly placed replica; tablet 3: at RF.
    h.add_running_replica(0, "2222");
    h.add_running_replica(0, "3333");
    h.add_running_replica(1, "3333");
    h.add_running_replica(1, "4444");
    h.add_running_replica(2, "2222");
    h.add_running_replica(3, "4444");

    // Every tablet has an add in flight: nothing to add.
    for i in 0..4 {
        let id = h.view.tables[0].tablets[i].id.clone();
        h.table().pending.adds.insert(id, "1111".into());
    }
    {
        let mut run = analyze!(h);
        assert!(add_load(&mut run, "", "", "").is_err());
    }
    h.table().pending.adds.clear();
    {
        let mut run = analyze!(h);
        let t2 = h.view.tables[0].tablets[2].id.clone();
        add_load(&mut run, &t2, "2222", "5555").unwrap();
    }
    for i in 0..4 {
        let id = h.view.tables[0].tablets[i].id.clone();
        h.table().pending.removes.insert(id, "1111".into());
    }
    {
        let mut run = analyze!(h);
        assert_eq!(run.remove_replicas().unwrap(), None);
    }
    h.table().pending.removes.clear();
    let mut run = analyze!(h);
    // The wrongly placed replica goes first; then, at equal load, the highest uuid.
    assert_eq!(
        run.remove_replicas().unwrap(),
        Some((h.view.tables[0].tablets[0].id.clone(), "2222".into()))
    );
    assert_eq!(
        run.remove_replicas().unwrap(),
        Some((h.view.tables[0].tablets[1].id.clone(), "1111".into()))
    );
    assert_eq!(run.remove_replicas().unwrap(), None);
}

#[test]
fn with_blacklist() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table().replication_info = cluster_config(&["a", "b", "c"], 3);
    h.ts_mut("0000").blacklisted = true;
    h.view
        .tservers
        .extend([ts("3333", "b"), ts("4444", "a"), ts("5555", "a")]);
    h.ts_mut("4444").blacklisted = true;
    {
        let mut run = analyze!(h);
        // Off the blacklisted ts0, to ts5 in the same zone (ts4 is blacklisted too).
        for i in 0..4 {
            add_load(
                &mut run,
                &h.view.tables[0].tablets[i].id.clone(),
                "0000",
                "5555",
            )
            .unwrap();
        }
        // Each tablet moves at most once per run.
        assert!(add_load(&mut run, "", "", "").is_err());
    }
    for i in 0..4 {
        h.remove_replica(i, "0000");
        h.add_running_replica(i, "5555");
    }
    let mut run = analyze!(h);
    // ts1:4, ts2:4, ts3:0, ts5:4; only ts1 and ts3 share a zone.
    add_load(&mut run, "", "1111", "3333").unwrap();
    add_load(&mut run, "", "1111", "3333").unwrap();
    assert!(add_load(&mut run, "", "", "").is_err());
}

#[test]
fn add_replica_to_ts_with_pending_delete() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.remove_replica(0, "2222");
    h.move_tablet_leader(0, "0000");
    let other = h.view.tables[0].tablets[1].id.clone();
    h.ts_mut("2222").pending_deletes.insert(other);
    let mut run = analyze!(h);
    // A pending delete of another tablet does not block the add.
    add_load(
        &mut run,
        &h.view.tables[0].tablets[0].id.clone(),
        "",
        "2222",
    )
    .unwrap();
}

#[test]
fn add_replica_to_ts_with_pending_delete_for_same_tablet() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.remove_replica(0, "2222");
    let id = h.view.tables[0].tablets[0].id.clone();
    h.ts_mut("2222").pending_deletes.insert(id);
    let mut run = analyze!(h);
    assert_eq!(run.add_replicas().unwrap(), None);
}

fn choose_tablet_in_same_zone(ignore_cloud_info_similarity: bool) {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.options.ignore_cloud_info_similarity = ignore_cloud_info_similarity;
    h.table()
        .replication_info
        .live_replicas
        .mut_or_insert_default()
        .set_num_replicas(3);
    h.view
        .tservers
        .extend([ts("3333", "a"), ts("4444", "b"), ts("5555", "c")]);
    // ts0 keeps 4 tablets, ts1..4 get 2, ts5 none.
    for (tablet, from, to) in [
        (0, "1111", "3333"),
        (1, "1111", "3333"),
        (0, "2222", "4444"),
        (1, "2222", "4444"),
    ] {
        h.remove_replica(tablet, from);
        h.add_running_replica(tablet, to);
    }
    h.move_tablet_leader(1, "4444");
    let mut run = analyze!(h);
    let (tablet, _, _) = add_load(&mut run, "", "0000", "5555").unwrap();
    let leader = h.leader_of(&tablet).unwrap();
    let zone = h
        .view
        .tservers
        .iter()
        .find(|t| t.uuid == leader)
        .unwrap()
        .cloud_info
        .placement_zone();
    // Without the similarity rule the first movable tablet wins; with it, the one led from zone c.
    assert_eq!(zone == "c", !ignore_cloud_info_similarity);
}

#[test]
fn choose_tablet_in_same_zone_by_similarity() {
    choose_tablet_in_same_zone(false);
}

#[test]
fn choose_tablet_ignoring_similarity() {
    choose_tablet_in_same_zone(true);
}

#[test]
fn moving_multiple_tablets_from_same_server() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table()
        .replication_info
        .live_replicas
        .mut_or_insert_default()
        .set_num_replicas(3);
    h.view
        .tservers
        .extend([ts("3333", "a"), ts("4444", "a"), ts("5555", "a")]);
    for (tablet, from, to) in [
        (0, "1111", "3333"),
        (1, "1111", "3333"),
        (0, "2222", "4444"),
        (1, "2222", "4444"),
    ] {
        h.remove_replica(tablet, from);
        h.add_running_replica(tablet, to);
    }
    h.move_tablet_leader(1, "4444");
    let mut run = analyze!(h);
    let (t1, _, _) = add_load(&mut run, "", "0000", "5555").unwrap();
    let (t2, _, _) = add_load(&mut run, "", "0000", "5555").unwrap();
    assert_ne!(t1, t2);
}

#[test]
fn with_missing_placement_and_load_imbalance() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table().replication_info = cluster_config(&["a", "b", "c"], 3);
    for i in 0..4 {
        h.remove_replica(i, "2222");
        h.move_tablet_leader(i, "0000");
    }
    h.view.tservers.pop();
    h.view.tservers.push(ts("1new", "c"));
    {
        let mut run = analyze!(h);
        for _ in 0..4 {
            add_load(&mut run, "", "", "1new").unwrap();
        }
    }
    h.view.tservers.push(ts("2new", "c"));
    for i in 0..4 {
        h.add_running_replica(i, "1new");
    }
    let mut run = analyze!(h);
    add_load(&mut run, "", "", "2new").unwrap();
    add_load(&mut run, "", "", "2new").unwrap();
    assert!(add_load(&mut run, "", "", "").is_err());
}

#[test]
fn balancing_leaders() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    for i in 0..4 {
        h.move_tablet_leader(i, "0000");
    }
    {
        // 4 0 0: two leaders move off ts0, to ts1 then ts2.
        let mut run = analyze!(h);
        let (t1, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("0000", "1111"));
        let (t2, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("0000", "2222"));
        assert_ne!(t1, t2);
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(0, "1111");
    {
        // 3 1 0.
        let mut run = analyze!(h);
        let (_, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("0000", "2222"));
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(1, "1111");
    h.ts_mut("0000").blacklisted = true;
    {
        // 2 2 0, ts0 blacklisted: no replica can go anywhere, but a leader moves to ts2.
        let mut run = analyze!(h);
        assert!(add_load(&mut run, "", "", "").is_err());
        assert_eq!(run.remove_replicas().unwrap(), None);
        let (_, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("1111", "2222"));
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.ts_mut("0000").blacklisted = false;
    {
        let mut run = analyze!(h);
        let (_, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("1111", "2222"));
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(1, "2222");
    // 2 1 1: as balanced as it gets.
    let mut run = analyze!(h);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn missing_placement_single_az() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.table().replication_info = cluster_config(&["a"], 3);
    for i in 0..3 {
        let uuid = h.view.tservers[i].uuid.clone();
        let next = h.view.tservers[(i + 1) % 3].uuid.clone();
        h.remove_replica(i, &uuid);
        h.move_tablet_leader(i, &next);
    }
    let mut run = analyze!(h);
    for i in 0..3 {
        add_load(
            &mut run,
            &h.view.tables[0].tablets[i].id.clone(),
            "",
            &h.view.tservers[i].uuid.clone(),
        )
        .unwrap();
    }
    assert!(add_load(&mut run, "", "", "").is_err());
}

#[test]
fn balancing_leaders_with_threshold() {
    let mut h = Harness::new(4);
    h.options.leader_balance_threshold = 2;
    h.prepare_az(3, true);
    for i in 0..4 {
        h.move_tablet_leader(i, "0000");
    }
    {
        let mut run = analyze!(h);
        let (t1, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("0000", "1111"));
        let (t2, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("0000", "2222"));
        assert_ne!(t1, t2);
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(0, "1111");
    {
        let mut run = analyze!(h);
        let (_, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("0000", "2222"));
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(1, "1111");
    h.ts_mut("0000").blacklisted = true;
    {
        // 2 2 0: everyone is at or below the threshold.
        let mut run = analyze!(h);
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.ts_mut("0000").blacklisted = false;
    {
        let mut run = analyze!(h);
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(1, "2222");
    let mut run = analyze!(h);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn leader_over_replication() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.table()
        .replication_info
        .live_replicas
        .mut_or_insert_default()
        .set_num_replicas(3);
    h.view.tservers.push(ts("3333", "a"));
    h.add_running_replica(0, "3333");
    h.move_tablet_leader(0, "2222");
    let mut run = analyze!(h);
    let (tablet, _) = run.remove_replicas().unwrap().unwrap();
    assert_eq!(tablet, h.view.tables[0].tablets[0].id.clone());
}

#[test]
fn limit_rbs_per_tserver() {
    let mut h = Harness::new(4);
    h.options.max_inbound_remote_bootstraps_per_ts = 1;
    h.prepare_az(3, true);
    for i in 0..4 {
        h.remove_replica(i, "2222");
    }
    h.view.tservers.push(ts("3333", "c"));
    let mut run = analyze!(h);
    add_load(&mut run, "", "", "2222").unwrap();
    add_load(&mut run, "", "", "3333").unwrap();
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn over_replication_limit_does_not_block_under_replication_repair() {
    let mut h = Harness::new(4);
    h.options.max_over_replicated_tablets_per_table = 1;
    h.prepare_az(3, true);
    h.table().replication_info = cluster_config(&["a", "b", "c"], 3);
    h.view.tservers.extend([ts("3333", "a"), ts("4444", "c")]);
    // Tablet 0 over-replicated (the table's limit); tablet 1 under-replicated; tablet 2 at RF
    // with zone c's replica moved to zone a.
    h.add_running_replica(0, "4444");
    h.remove_replica(1, "2222");
    h.remove_replica(2, "2222");
    h.add_running_replica(2, "3333");
    h.move_tablet_leader(2, "0000");
    let mut run = analyze!(h);
    assert_eq!(run.state().tablets_over_replicated.len(), 1);
    // Restoring RF is not limited.
    assert!(run
        .add_if_missing_placement(&h.view.tables[0].tablets[1].id.clone())
        .unwrap()
        .is_some());
    assert_eq!(run.state().tablets_over_replicated.len(), 1);
    // A block below its minimum at RF over-replicates the tablet, so the limit blocks it, as it
    // blocks a move.
    let e = run
        .add_if_missing_placement(&h.view.tables[0].tablets[2].id.clone())
        .unwrap_err();
    assert!(e.contains("overreplication"), "{e}");
    assert!(run.add_replicas().is_err());
}

#[test]
fn exclude_over_replication_in_source_load() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    h.view.tservers.push(ts("3333", "c"));
    h.add_running_replica(0, "3333");
    h.add_running_replica(1, "3333");
    let mut run = analyze!(h);
    // Less the over-replicated replicas, ts2 has 2 tablets, as many as ts3.
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn leader_blacklist() {
    let mut h = Harness::new(4);
    h.prepare_az(3, true);
    for i in 0..4 {
        let uuid = h.view.tservers[i % 3].uuid.clone();
        h.move_tablet_leader(i, &uuid);
    }
    h.ts_mut("2222").leader_blacklisted = true;
    {
        // 2 1 1, ts2 leader-blacklisted: its leader goes to ts1.
        let mut run = analyze!(h);
        let (tablet, from, to) = run.leader_moves().unwrap().unwrap();
        assert_eq!((from.as_str(), to.as_str()), ("2222", "1111"));
        assert_eq!(tablet, h.view.tables[0].tablets[2].id.clone());
        assert!(run.leader_moves().unwrap().is_none());
    }
    h.move_tablet_leader(2, "1111");
    h.ts_mut("2222").leader_blacklisted = false;
    // 2 2 0: a leader comes back to ts2.
    let mut run = analyze!(h);
    let (_, _, to) = run.leader_moves().unwrap().unwrap();
    assert_eq!(to, "2222");
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn size_aware_balancing() {
    for (max_bytes, expected_moves) in [(100 * MB, 2), (120 * MB, 3)] {
        let mut h = Harness::new(40);
        h.prepare_az(3, false);
        h.view.tservers.push(ts("3333", "a"));
        // 40 MB tablets against the byte budget, with a minimum of one bootstrap.
        h.options.max_inbound_bytes_per_ts = max_bytes;
        h.options.min_inbound_remote_bootstraps_per_ts = 1;
        h.set_all_sizes(40 * MB);
        let mut run = analyze!(h);
        for _ in 0..expected_moves {
            let (_, _, to) = run.add_replicas().unwrap().unwrap();
            assert_eq!(to, "3333");
        }
        assert_eq!(run.add_replicas().unwrap(), None);
    }
}

#[test]
fn size_aware_balancing_min_rbs() {
    let mut h = Harness::new(40);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_all_sizes(101 * MB);
    h.options.max_inbound_bytes_per_ts = 100 * MB;
    h.options.min_inbound_remote_bootstraps_per_ts = 2;
    let mut run = analyze!(h);
    // Each tablet is above the byte budget, yet the minimum number of bootstraps is scheduled.
    for _ in 0..2 {
        assert_eq!(run.add_replicas().unwrap().unwrap().2, "3333");
    }
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn size_aware_balancing_no_size_data() {
    let mut h = Harness::new(40);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.options.max_inbound_bytes_per_ts = 100 * MB;
    h.options.min_inbound_remote_bootstraps_per_ts = 1;
    let mut run = analyze!(h);
    // An unknown size counts as 1 GB.
    assert_eq!(run.add_replicas().unwrap().unwrap().2, "3333");
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn disk_full_accounts_for_starting_replicas() {
    let mut h = Harness::new(40);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_all_sizes(10 * MB);
    h.options.max_inbound_bytes_per_ts = 10 << 30;
    // 95 MB usable: nine 10 MB replicas fit.
    h.set_disk("3333", 0, 100 * MB);
    let mut run = analyze!(h);
    for _ in 0..9 {
        assert_eq!(run.add_replicas().unwrap().unwrap().2, "3333");
    }
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn size_aware_balancing_many_tablets() {
    let mut h = Harness::new(40);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_all_sizes(MB);
    let mut run = analyze!(h);
    // 20 moves, then the over-replicated replicas even out the source's load.
    for _ in 0..20 {
        assert_eq!(run.add_replicas().unwrap().unwrap().2, "3333");
    }
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn disk_full_tserver_rejected_as_destination() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_all_sizes(10 * MB);
    h.set_disk("3333", 96 * MB, 100 * MB);
    let mut run = analyze!(h);
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn disk_full_tserver_accepted_when_check_disabled() {
    let mut h = Harness::new(4);
    h.options.min_free_disk_space_pct = 0;
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_all_sizes(10 * MB);
    h.set_disk("3333", 96 * MB, 100 * MB);
    let mut run = analyze!(h);
    assert_eq!(run.add_replicas().unwrap().unwrap().2, "3333");
}

#[test]
fn unknown_disk_capacity_tserver_accepted_as_destination() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_all_sizes(10 * MB);
    let mut run = analyze!(h);
    assert_eq!(run.add_replicas().unwrap().unwrap().2, "3333");
}

#[test]
fn disk_full_tserver_does_not_stop_balancing() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.view.tservers.extend([ts("3333", "a"), ts("4444", "a")]);
    h.remove_replica(0, "2222");
    h.add_running_replica(0, "4444");
    h.set_all_sizes(10 * MB);
    h.set_disk("3333", 96 * MB, 100 * MB);
    h.set_disk("4444", 0, 100 * MB);
    let mut run = analyze!(h);
    for i in 0..4 {
        assert!(!run
            .can_add(&h.view.tables[0].tablets[i].id.clone(), "3333", "1111")
            .unwrap());
    }
    assert!(run
        .can_add(&h.view.tables[0].tablets[1].id.clone(), "4444", "1111")
        .unwrap());
    // 3333 is tried first and rejected; the balancer goes on to 4444.
    let mut moves = 0;
    while let Some((_, from, to)) = run.add_replicas().unwrap() {
        moves += 1;
        assert!(moves <= 4);
        assert_eq!(to, "4444");
        assert!(from == "0000" || from == "1111", "{from}");
    }
    assert!(moves >= 1);
}

#[test]
fn disk_check_skips_oversized_tablet_on_same_source() {
    let mut h = Harness::new(4);
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    h.set_sizes(0, 50 * MB);
    for i in 1..4 {
        h.set_sizes(i, 10 * MB);
    }
    h.set_disk("3333", 60 * MB, 100 * MB);
    let mut run = analyze!(h);
    let big = h.view.tables[0].tablets[0].id.clone();
    assert!(!run.can_add(&big, "3333", "2222").unwrap());
    assert!(run
        .can_add(&h.view.tables[0].tablets[1].id.clone(), "3333", "2222")
        .unwrap());
    let mut moves = 0;
    while let Some((tablet, _, to)) = run.add_replicas().unwrap() {
        moves += 1;
        assert!(moves <= 4);
        assert_eq!(to, "3333");
        assert_ne!(tablet, big);
    }
    assert!(moves >= 1);
}

#[test]
fn disk_check_tries_next_source_for_same_destination() {
    let mut h = Harness::new(5);
    h.prepare(
        ["0000", "1111", "2222", "3333", "4444"]
            .iter()
            .map(|u| ts(u, "a"))
            .collect(),
    );
    let layouts: [&[&str]; 5] = [
        &["0000", "1111", "2222"],
        &["0000", "1111", "3333"],
        &["0000", "2222", "4444"],
        &["0000", "3333", "4444"],
        &["1111", "2222", "4444"],
    ];
    for (i, layout) in layouts.iter().enumerate() {
        h.keep_only_replicas_on(i, layout);
    }
    for i in 0..4 {
        h.set_sizes(i, 50 * MB);
    }
    h.set_sizes(4, 10 * MB);
    h.view.tservers.push(ts("5555", "a"));
    h.set_disk("5555", 60 * MB, 100 * MB);
    let mut run = analyze!(h);
    assert!(!run
        .can_add(&h.view.tables[0].tablets[0].id.clone(), "5555", "0000")
        .unwrap());
    assert!(run
        .can_add(&h.view.tables[0].tablets[4].id.clone(), "5555", "4444")
        .unwrap());
    // 0000 has nothing that fits; the next source for the same destination is tried.
    let (tablet, from, to) = run.add_replicas().unwrap().unwrap();
    assert_eq!(tablet, h.view.tables[0].tablets[4].id.clone());
    assert_eq!(to, "5555");
    assert_ne!(from, "0000");
}

fn max_replicas(h: &mut Harness, max: i32) {
    for block in &mut h
        .table()
        .replication_info
        .live_replicas
        .mut_or_insert_default()
        .placement_blocks
    {
        block.set_max_num_replicas(max);
    }
}

#[test]
fn repair_placement_above_maximum() {
    let mut h = Harness::new(1);
    h.prepare(
        [
            ("a000", "a"),
            ("a001", "a"),
            ("a002", "a"),
            ("a003", "a"),
            ("b000", "b"),
            ("b001", "b"),
            ("c000", "c"),
            ("c001", "c"),
        ]
        .iter()
        .map(|(u, z)| ts(u, z))
        .collect(),
    );
    h.table().replication_info = cluster_config(&["a", "b", "c"], 5);
    max_replicas(&mut h, 2);
    // a(3), b(1), c(1): at RF, above the maximum in a.
    h.keep_only_replicas_on(0, &["a000", "a001", "a002", "b000", "c000"]);
    let t = h.view.tables[0].tablets[0].id.clone();
    let to = {
        let mut run = analyze!(h);
        // Not over-replicated: a bare remove would under-replicate it; add first.
        assert_eq!(run.remove_replicas().unwrap(), None);
        assert!(!run.can_add(&t, "a003", "").unwrap());
        let (tablet, from, to) = run.add_replicas().unwrap().unwrap();
        assert_eq!(tablet, t);
        assert!(["a000", "a001", "a002"].contains(&from.as_str()));
        assert!(["b001", "c001"].contains(&to.as_str()));
        to
    };
    h.add_running_replica(0, &to);
    let from = {
        let mut run = analyze!(h);
        let (_, from) = run.remove_replicas().unwrap().unwrap();
        assert!(["a000", "a001", "a002"].contains(&from.as_str()));
        from
    };
    h.remove_replica(0, &from);
    let run = analyze!(h);
    let remaining = if from == "a000" { "a001" } else { "a000" };
    assert!(!run.can_add(&t, "a003", "").unwrap());
    assert!(run.can_add(&t, "a003", remaining).unwrap());
}

#[test]
fn under_replication_prioritized_over_max_placement() {
    let mut h = Harness::new(1);
    h.prepare(
        [
            ("a000", "a"),
            ("a001", "a"),
            ("a002", "a"),
            ("b000", "b"),
            ("c000", "c"),
            ("c001", "c"),
        ]
        .iter()
        .map(|(u, z)| ts(u, z))
        .collect(),
    );
    h.table().replication_info = cluster_config(&["a", "b", "c"], 5);
    max_replicas(&mut h, 2);
    // a(3), b(1), c(0): under-replicated in c and above the maximum in a.
    h.keep_only_replicas_on(0, &["a000", "a001", "a002", "b000"]);
    let t = h.view.tables[0].tablets[0].id.clone();
    let to = {
        let mut run = analyze!(h);
        assert_eq!(run.add_replicas().unwrap(), None);
        let (tablet, to) = run.one_add_if_missing_placement().unwrap().unwrap();
        assert_eq!(tablet, t);
        assert!(["c000", "c001"].contains(&to.as_str()));
        to
    };
    h.add_running_replica(0, &to);
    let remaining_c = if to == "c000" { "c001" } else { "c000" };
    let to = {
        let mut run = analyze!(h);
        let (tablet, from, to) = run.add_replicas().unwrap().unwrap();
        assert_eq!(tablet, t);
        assert!(["a000", "a001", "a002"].contains(&from.as_str()));
        assert_eq!(to, remaining_c);
        to
    };
    h.add_running_replica(0, &to);
    let mut run = analyze!(h);
    let (_, from) = run.remove_replicas().unwrap().unwrap();
    assert!(["a000", "a001", "a002"].contains(&from.as_str()));
}

#[test]
fn blacklisted_move_allowed_at_max_placement() {
    let mut h = Harness::new(1);
    h.prepare(
        [("a000", "a"), ("a001", "a"), ("b000", "b"), ("c000", "c")]
            .iter()
            .map(|(u, z)| ts(u, z))
            .collect(),
    );
    h.table().replication_info = cluster_config(&["a", "b", "c"], 3);
    max_replicas(&mut h, 1);
    h.keep_only_replicas_on(0, &["a000", "b000", "c000"]);
    h.ts_mut("a000").blacklisted = true;
    let t = h.view.tables[0].tablets[0].id.clone();
    {
        let mut run = analyze!(h);
        // Zone a is at its maximum, but the remove after a same-block move restores it.
        assert!(!run.can_add(&t, "a001", "").unwrap());
        assert!(run.can_add(&t, "a001", "a000").unwrap());
        assert_eq!(
            run.add_replicas().unwrap(),
            Some((t.clone(), "a000".into(), "a001".into()))
        );
    }
    h.add_running_replica(0, "a001");
    let mut run = analyze!(h);
    assert_eq!(run.remove_replicas().unwrap(), Some((t, "a000".into())));
}

#[test]
fn balanced_load_still_repairs_over_max_placement() {
    let mut h = Harness::new(4);
    h.prepare(
        [
            ("a000", "a"),
            ("a001", "a"),
            ("a002", "a"),
            ("a003", "a"),
            ("a004", "a"),
            ("a005", "a"),
            ("b000", "b"),
            ("b001", "b"),
            ("c000", "c"),
            ("c001", "c"),
        ]
        .iter()
        .map(|(u, z)| ts(u, z))
        .collect(),
    );
    h.table().replication_info = cluster_config(&["a", "b", "c"], 5);
    let layouts: [&[&str]; 4] = [
        &["a000", "a001", "a002", "b000", "c000"],
        &["a003", "a004", "a005", "b000", "c000"],
        &["a000", "a001", "a002", "b001", "c001"],
        &["a003", "a004", "a005", "b001", "c001"],
    ];
    for (i, layout) in layouts.iter().enumerate() {
        h.keep_only_replicas_on(i, layout);
    }
    {
        // Balanced, every block at its minimum: nothing to do without maximums.
        let mut run = analyze!(h);
        assert_eq!(run.add_replicas().unwrap(), None);
        assert_eq!(run.remove_replicas().unwrap(), None);
    }
    max_replicas(&mut h, 2);
    let mut run = analyze!(h);
    assert_eq!(run.remove_replicas().unwrap(), None);
    for _ in 0..4 {
        let (_, from, to) = run.add_replicas().unwrap().unwrap();
        assert!(from.starts_with('a'));
        assert!(to.starts_with('b') || to.starts_with('c'));
    }
    // Each tablet once per run.
    assert_eq!(run.add_replicas().unwrap(), None);
}

#[test]
fn under_replicated_priority() {
    let mut h = Harness::new(4);
    h.prepare_az(5, true);
    h.table().replication_info = cluster_config(&["a", "b", "c", "d", "e"], 5);
    h.remove_replica(0, "4444");
    h.remove_replica(1, "3333");
    h.remove_replica(1, "4444");
    let mut run = analyze!(h);
    // The tablet missing two replicas first.
    add_load(
        &mut run,
        &h.view.tables[0].tablets[1].id.clone(),
        "",
        "4444",
    )
    .unwrap();
    add_load(
        &mut run,
        &h.view.tables[0].tablets[0].id.clone(),
        "",
        "4444",
    )
    .unwrap();
    assert_eq!(run.one_add_if_missing_placement().unwrap(), None);
    assert!(add_load(&mut run, "", "", "").is_err());
}

#[test]
fn full_run_moves_and_goes_idle() {
    let mut h = Harness::new(4);
    h.options.check_ts_liveness = true;
    h.prepare_az(3, false);
    h.view.tservers.push(ts("3333", "a"));
    let options = h.options.clone();
    let result = h.balancer.run(&h.view, &options);
    let adds: Vec<_> = result
        .actions()
        .filter(|a| matches!(a, crate::Action::AddReplica { ts_uuid, member_type: PeerMemberType::PRE_VOTER, .. } if ts_uuid == "3333"))
        .collect();
    // Two moves: after them the source's over-replicated replicas, about to be removed, leave it
    // within the load variance of the new tserver.
    assert_eq!(adds.len(), 2, "{:?}", result.activities);
    assert!(!result.activities[0].is_idle());

    // Balanced: idle, and global balancing becomes allowed.
    let mut h = Harness::new(3);
    h.options.check_ts_liveness = true;
    h.prepare_az(3, false);
    let options = h.options.clone();
    let result = h.balancer.run(&h.view, &options);
    assert_eq!(result.actions().count(), 0);
    assert!(result.activities[0].is_idle(), "{:?}", result.activities);
    assert!(h.balancer.can_perform_global_operations);
}

// `OptimalLoadDistributionTest`.

fn tservers(rf: usize) -> Vec<TsInfo> {
    (0..rf)
        .map(|i| {
            ts(
                &char::from(b'0' + i as u8).to_string().repeat(4),
                &char::from(b'a' + i as u8).to_string(),
            )
        })
        .collect()
}

fn assert_distribution(ts: &[TsInfo], map: &TsTableLoadMap, expected: &[usize]) {
    let actual: Vec<usize> = ts.iter().map(|t| map[&t.uuid]).collect();
    assert_eq!(actual, expected);
}

fn optimal(
    ts: &[TsInfo],
    ri: &ReplicationInfoPB,
    current: &TsTableLoadMap,
    num_tablets: usize,
) -> Result<TsTableLoadMap, String> {
    let refs: Vec<&TsInfo> = ts.iter().collect();
    calculate_optimal_load_distribution(&refs, &ri.live_replicas, current, num_tablets)
}

#[test]
fn even_distribution() {
    let ts = tservers(3);
    let ri = cluster_config(&["a", "b", "c"], 3);
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &HashMap::new(), 1).unwrap(),
        &[1, 1, 1],
    );
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &HashMap::new(), 3).unwrap(),
        &[3, 3, 3],
    );
}

#[test]
fn uneven_tserver_count() {
    let mut ts = tservers(3);
    ts.push(self::ts("3333", "c"));
    let ri = cluster_config(&["a", "b", "c"], 3);
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &HashMap::new(), 2).unwrap(),
        &[2, 2, 1, 1],
    );
}

#[test]
fn break_ties_by_current_load() {
    let mut ts = tservers(3);
    ts.push(self::ts("3333", "c"));
    let ri = cluster_config(&["a", "b", "c"], 3);
    let mut current = HashMap::from([("2222".to_string(), 1), ("3333".to_string(), 2)]);
    assert_distribution(&ts, &optimal(&ts, &ri, &current, 3).unwrap(), &[3, 3, 1, 2]);
    current = HashMap::from([("2222".to_string(), 2), ("3333".to_string(), 1)]);
    assert_distribution(&ts, &optimal(&ts, &ri, &current, 3).unwrap(), &[3, 3, 2, 1]);
}

#[test]
fn not_enough_tservers() {
    let mut ts = tservers(3);
    let ri = cluster_config(&["a", "b", "c"], 3);
    let e = optimal(&[], &ri, &HashMap::new(), 1).unwrap_err();
    assert!(
        e.contains("Not enough tservers to host the required number of replicas (have 0, need 3)")
    );
    ts.push(self::ts("3333", "a"));
    let subset = vec![ts[0].clone(), ts[1].clone(), ts[3].clone()];
    let e = optimal(&subset, &ri, &HashMap::new(), 1).unwrap_err();
    assert!(
        e.contains("Not enough tservers to host the minimum number of replicas in placement block")
    );
}

#[test]
fn slack() {
    let mut ts = tservers(3);
    let mut ri = cluster_config(&["a", "b", "c"], 3);
    ri.live_replicas.mut_or_insert_default().placement_blocks[0].set_min_num_replicas(0);
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &HashMap::new(), 5).unwrap(),
        &[5, 5, 5],
    );
    ts.push(self::ts("3333", "b"));
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &HashMap::new(), 6).unwrap(),
        &[4, 4, 6, 4],
    );
}

#[test]
fn slack_many_tservers() {
    let ri = cluster_config(&["a", "b"], 3);
    let mut ts: Vec<TsInfo> = (0..5)
        .map(|i| self::ts(&format!("zone_a_ts{i}"), "a"))
        .collect();
    ts.push(self::ts("zone_b_ts0", "b"));
    ts.push(self::ts("zone_b_ts1", "b"));
    let current: TsTableLoadMap = [&ts[0], &ts[1], &ts[5]]
        .iter()
        .map(|t| (t.uuid.clone(), 1))
        .collect();
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &current, 1).unwrap(),
        &[1, 1, 0, 0, 0, 1, 0],
    );
    let current: TsTableLoadMap = [&ts[0], &ts[5], &ts[6]]
        .iter()
        .map(|t| (t.uuid.clone(), 1))
        .collect();
    assert_distribution(
        &ts,
        &optimal(&ts, &ri, &current, 1).unwrap(),
        &[1, 0, 0, 0, 0, 1, 1],
    );
}

#[test]
fn slack_rf5() {
    let mut ri = cluster_config(&["a", "b", "c"], 5);
    ri.live_replicas.mut_or_insert_default().placement_blocks[2].set_min_num_replicas(2);
    let ts: Vec<TsInfo> = ["a", "b", "c"]
        .iter()
        .flat_map(|z| (0..5).map(move |i| self::ts(&format!("zone_{z}_ts{i}"), z)))
        .collect();
    let map = optimal(&ts, &ri, &HashMap::new(), 30).unwrap();
    for t in &ts[..10] {
        assert_eq!(map[&t.uuid], 9);
    }
    for t in &ts[10..] {
        assert_eq!(map[&t.uuid], 12);
    }
}

#[test]
fn table_load_difference() {
    let m = |v: &[(&str, usize)]| -> TsTableLoadMap {
        v.iter().map(|(k, n)| (k.to_string(), *n)).collect()
    };
    assert_eq!(
        calculate_table_load_difference(
            &m(&[("ts0", 1), ("ts1", 1), ("ts2", 1)]),
            &m(&[("ts0", 1), ("ts1", 1), ("ts2", 1)])
        ),
        0
    );
    assert_eq!(
        calculate_table_load_difference(
            &m(&[("ts0", 1), ("ts1", 2), ("ts2", 1)]),
            &m(&[("ts0", 1), ("ts1", 1), ("ts2", 2)])
        ),
        1
    );
    assert_eq!(
        calculate_table_load_difference(
            &m(&[("ts0", 1), ("ts1", 1)]),
            &m(&[("ts1", 1), ("ts2", 1)])
        ),
        1
    );
}

#[test]
fn tablet_ids_follow_create_table() {
    assert_eq!(
        tablet_ids(4),
        vec!["tablet--0", "tablet-0-1", "tablet-1-2", "tablet-2-"]
    );
    let unique: BTreeSet<String> = tablet_ids(40).into_iter().collect();
    assert_eq!(unique.len(), 40);
}

/// The planner at scale: `SCALE_TSERVERS` tservers (default 500) and `SCALE_TABLETS` tablets
/// (default 500,000) in tables of 5,000 tablets, RF3, with one empty tserver to balance onto.
/// Run with `cargo test --release -p master-cluster-balancer scale -- --ignored --nocapture`.
#[test]
#[ignore]
fn scale() {
    let env = |k: &str, d: usize| {
        std::env::var(k)
            .ok()
            .and_then(|v| v.parse().ok())
            .unwrap_or(d)
    };
    let (num_ts, num_tablets) = (env("SCALE_TSERVERS", 500), env("SCALE_TABLETS", 500_000));
    let tservers: Vec<TsInfo> = (0..num_ts)
        .map(|i| TsInfo {
            uuid: format!("ts{i:05}"),
            cloud_info: cloud(&format!("z{}", i % 3)),
            live_and_reported: true,
            ..TsInfo::default()
        })
        .collect();
    // Every tserver but the last holds replicas, round robin within its zone.
    let loaded = num_ts - 1;
    let mut tables = Vec::new();
    let per_table = 5000;
    for t in 0..num_tablets.div_ceil(per_table) {
        let tablets = (0..per_table.min(num_tablets - t * per_table))
            .map(|i| {
                let n = t * per_table + i;
                let replicas = (0..3)
                    .map(|r| {
                        (
                            tservers[(n * 3 + r) % loaded].uuid.clone(),
                            ReplicaInfo {
                                is_leader: r == 0,
                                ..ReplicaInfo::default()
                            },
                        )
                    })
                    .collect();
                TabletInfo {
                    id: format!("tablet{n:08}"),
                    running: true,
                    replicas,
                    ..TabletInfo::default()
                }
            })
            .collect();
        tables.push(TableInfo {
            id: format!("table{t:05}"),
            tablets,
            ..TableInfo::default()
        });
    }
    let view = ClusterView {
        tservers,
        tables,
        ..ClusterView::default()
    };
    let mut balancer = ClusterBalancer::new();
    let options = Options::default();
    for run in 0..3 {
        let start = std::time::Instant::now();
        let result = balancer.run(&view, &options);
        println!(
            "run {run}: {} tservers, {num_tablets} tablets: {:?}, {} actions",
            num_ts,
            start.elapsed(),
            result.actions().count()
        );
    }
    // The peak resident set, view included.
    if let Ok(status) = std::fs::read_to_string("/proc/self/status") {
        if let Some(line) = status.lines().find(|l| l.starts_with("VmHWM")) {
            println!("{line}");
        }
    }
}
