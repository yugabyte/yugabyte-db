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

//! Leader balancing with leader affinity, with the cases of the C++
//! `cluster_balance_preferred_leader-test`.

use std::collections::{BTreeMap, BTreeSet};
use std::time::Duration;

use yb_proto::pb::common_net::{CloudInfoListPB, PlacementInfoPB};

use super::*;

/// The tservers of `GenerateDefaultTsDesc` or `Generate5NodeTsDesc`.
fn tservers(n: usize) -> Vec<TsInfo> {
    ["a", "b", "c", "d", "e"][..n]
        .iter()
        .enumerate()
        .map(|(i, az)| ts(&char::from(b'0' + i as u8).to_string().repeat(4), az))
        .collect()
}

/// `PrepareAffinitizedLeaders`: one set per priority, zones of the default cloud and region; "*"
/// leaves the zone out (any zone of the region).
fn affinitize(h: &mut Harness, sets: &[&[&str]]) {
    let sets = sets
        .iter()
        .map(|zones| {
            let mut list = CloudInfoListPB::new();
            list.zones = zones
                .iter()
                .map(|zone| {
                    let mut ci = cloud(zone);
                    if *zone == "*" {
                        ci.clear_placement_zone();
                    }
                    ci
                })
                .collect();
            list
        })
        .collect();
    h.table().replication_info.multi_affinitized_leaders = sets;
}

/// `PrepareAffinitizedLeadersWithCloudInfo`: (cloud, region, zone); "*" leaves the region (and
/// zone) or the zone out.
fn affinitize_cloud_info(h: &mut Harness, sets: &[&[(&str, &str, &str)]]) {
    let sets = sets
        .iter()
        .map(|zones| {
            let mut list = CloudInfoListPB::new();
            list.zones = zones
                .iter()
                .map(|(cloud, region, zone)| {
                    let mut ci = CloudInfoPB::new();
                    ci.set_placement_cloud(cloud.to_string());
                    if *region != "*" {
                        ci.set_placement_region(region.to_string());
                        if *zone != "*" {
                            ci.set_placement_zone(zone.to_string());
                        }
                    }
                    ci
                })
                .collect();
            list
        })
        .collect();
    h.table().replication_info.multi_affinitized_leaders = sets;
}

/// `StopTsHeartbeat` / `ResumeTsHeartbeat`.
fn set_heartbeat(h: &mut Harness, uuid: &str, stopped: bool) {
    h.ts_mut(uuid).heartbeat_age = if stopped {
        Duration::from_secs(3600)
    } else {
        Duration::ZERO
    };
}

fn tablet_index(h: &Harness, id: &str) -> usize {
    h.view.tables[0]
        .tablets
        .iter()
        .position(|t| t.id == id)
        .unwrap()
}

/// The leader moves of one analysis, until none is left; they are then applied to the view.
fn leader_moves(h: &mut Harness) -> Vec<(String, String, String)> {
    let moves = {
        let mut run = analyze!(h);
        let mut moves = Vec::new();
        while let Some(m) = run.leader_moves().unwrap() {
            moves.push(m);
            assert!(moves.len() < 100, "leader moves do not converge");
        }
        moves
    };
    for (tablet, _, to) in &moves {
        let i = tablet_index(h, tablet);
        h.move_tablet_leader(i, to);
    }
    moves
}

fn count<'a>(uuids: impl Iterator<Item = &'a String>) -> BTreeMap<String, usize> {
    let mut out = BTreeMap::new();
    for uuid in uuids {
        *out.entry(uuid.clone()).or_default() += 1;
    }
    out
}

fn froms(moves: &[(String, String, String)]) -> BTreeMap<String, usize> {
    count(moves.iter().map(|(_, from, _)| from))
}

fn tos(moves: &[(String, String, String)]) -> BTreeMap<String, usize> {
    count(moves.iter().map(|(_, _, to)| to))
}

fn distinct_tablets(moves: &[(String, String, String)]) -> usize {
    moves
        .iter()
        .map(|(t, _, _)| t.clone())
        .collect::<BTreeSet<_>>()
        .len()
}

fn map(entries: &[(&str, usize)]) -> BTreeMap<String, usize> {
    entries.iter().map(|(k, v)| (k.to_string(), *v)).collect()
}

/// Leaders per tserver in the view.
fn leaders(h: &Harness) -> BTreeMap<String, usize> {
    let mut out: BTreeMap<String, usize> = h
        .view
        .tservers
        .iter()
        .map(|t| (t.uuid.clone(), 0))
        .collect();
    for tablet in &h.view.tables[0].tablets {
        for (ts, r) in &tablet.replicas {
            if r.is_leader {
                *out.get_mut(ts).unwrap() += 1;
            }
        }
    }
    out
}

#[test]
fn balancing_one_affinitized_leader() {
    let mut h = Harness::new(4);
    h.prepare(tservers(3));
    affinitize(&mut h, &[&["a"]]);
    for i in 0..4 {
        h.move_tablet_leader(i, ["1111", "2222"][i % 2]);
    }
    // 0 2 2.
    let mut run = analyze!(h);
    let mut moves = Vec::new();
    for _ in 0..2 {
        moves.push(run.leader_moves().unwrap().unwrap());
    }
    // 2 1 1: one from each.
    assert_eq!(tos(&moves), map(&[("0000", 2)]));
    assert_eq!(froms(&moves), map(&[("1111", 1), ("2222", 1)]));
    for _ in 0..2 {
        moves.push(run.leader_moves().unwrap().unwrap());
    }
    // 4 0 0.
    assert_eq!(tos(&moves), map(&[("0000", 4)]));
    assert_eq!(froms(&moves), map(&[("1111", 2), ("2222", 2)]));
    assert_eq!(distinct_tablets(&moves), 4);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn balancing_two_affinitized_leaders() {
    let mut h = Harness::new(4);
    h.prepare(tservers(3));
    affinitize(&mut h, &[&["b", "c"]]);
    for i in 0..4 {
        h.move_tablet_leader(i, "0000");
    }
    // 4 0 0.
    let mut run = analyze!(h);
    let mut moves = Vec::new();
    for _ in 0..2 {
        moves.push(run.leader_moves().unwrap().unwrap());
    }
    // 2 1 1.
    assert_eq!(froms(&moves), map(&[("0000", 2)]));
    assert_eq!(tos(&moves), map(&[("1111", 1), ("2222", 1)]));
    for _ in 0..2 {
        moves.push(run.leader_moves().unwrap().unwrap());
    }
    // 0 2 2.
    assert_eq!(froms(&moves), map(&[("0000", 4)]));
    assert_eq!(tos(&moves), map(&[("1111", 2), ("2222", 2)]));
    assert_eq!(distinct_tablets(&moves), 4);
    assert!(run.leader_moves().unwrap().is_none());
}

const READ_ONLY: &str = "read_only";

/// `SetupClusterConfig({"a", "b", "c"}, {"a"}, {})`: RF 3 live and one read replica in "a", with
/// a read replica tserver per uuid.
fn with_read_replica(h: &mut Harness, read_only_uuids: &[&str]) {
    h.prepare(tservers(3));
    let mut ri = cluster_config(&["a", "b", "c"], 3);
    let mut rr = PlacementInfoPB::new();
    rr.set_num_replicas(1);
    rr.set_placement_uuid(READ_ONLY.into());
    let mut block = PlacementBlockPB::new();
    block.cloud_info = Some(cloud("a")).into();
    block.set_min_num_replicas(1);
    rr.placement_blocks.push(block);
    ri.read_replicas.push(rr);
    h.table().replication_info = ri;
    for uuid in read_only_uuids {
        h.view.tservers.push(TsInfo {
            placement_uuid: READ_ONLY.into(),
            ..ts(uuid, "a")
        });
    }
    h.view.read_replica_placement_uuids = vec![READ_ONLY.into()];
}

fn read_only_mode(h: &mut Harness) {
    h.mode = RunMode {
        read_only: true,
        placement_uuid: READ_ONLY.into(),
        live_placement_uuid: String::new(),
    };
}

#[test]
fn read_only_load_balancing() {
    let mut h = Harness::new(4);
    with_read_replica(&mut h, &["3333", "4444"]);
    // Every read replica on ts3: ts3 4, ts4 0.
    for i in 0..4 {
        h.add_running_replica(i, "3333");
    }
    {
        // Nothing to do in the live run.
        let mut run = analyze!(h);
        assert!(add_load(&mut run, "", "", "").is_err());
    }
    read_only_mode(&mut h);
    let mut run = analyze!(h);
    add_load(&mut run, "", "3333", "4444").unwrap();
    add_load(&mut run, "", "3333", "4444").unwrap();
    // 2 and 2.
    assert!(add_load(&mut run, "", "", "").is_err());
}

#[test]
fn leader_balancing_with_read_only() {
    let mut h = Harness::new(4);
    with_read_replica(&mut h, &["3333"]);
    for i in 0..4 {
        h.add_running_replica(i, "3333");
    }
    {
        let mut run = analyze!(h);
        assert!(run.leader_moves().unwrap().is_none());
    }
    read_only_mode(&mut h);
    let mut run = analyze!(h);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn already_balanced_affinitized_leaders() {
    let mut h = Harness::new(4);
    h.prepare(tservers(3));
    affinitize(&mut h, &[&["a"]]);
    for i in 0..4 {
        h.move_tablet_leader(i, "0000");
    }
    // Only the affinitized zone has leaders.
    let mut run = analyze!(h);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn already_balanced_multi_affinitized_leaders() {
    let mut h = Harness::new(4);
    h.prepare(tservers(5));
    affinitize(&mut h, &[&["a"], &["b"]]);
    for i in 0..4 {
        h.move_tablet_leader(i, "0000");
    }
    let mut run = analyze!(h);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn balancing_one_multi_affinitized_leader() {
    let mut h = Harness::new(4);
    h.prepare(tservers(5));
    affinitize(&mut h, &[&["a"], &["b"]]);
    for i in 0..4 {
        h.move_tablet_leader(i, ["1111", "2222"][i % 2]);
    }
    // 0 2 2.
    let mut run = analyze!(h);
    let mut moves = Vec::new();
    for _ in 0..2 {
        moves.push(run.leader_moves().unwrap().unwrap());
    }
    // ts2's zone has no priority, so its leaders go first: 2 2 0.
    assert_eq!(tos(&moves), map(&[("0000", 2)]));
    assert_eq!(froms(&moves), map(&[("2222", 2)]));
    for _ in 0..2 {
        moves.push(run.leader_moves().unwrap().unwrap());
    }
    // 4 0 0.
    assert_eq!(tos(&moves), map(&[("0000", 4)]));
    assert_eq!(froms(&moves), map(&[("1111", 2), ("2222", 2)]));
    assert_eq!(distinct_tablets(&moves), 4);
    assert!(run.leader_moves().unwrap().is_none());
}

#[test]
fn balancing_multi_primary_affinitized_leader() {
    let mut h = Harness::new(4);
    h.prepare(tservers(5));
    affinitize(&mut h, &[&["a", "b"], &["c"]]);
    set_heartbeat(&mut h, "0000", true);
    for (i, uuid) in ["3333", "1111", "3333", "1111"].iter().enumerate() {
        h.move_tablet_leader(i, uuid);
    }
    // 0 2 0 2: ts3's leaders go to the preferred tserver that heartbeats.
    let moves = leader_moves(&mut h);
    assert_eq!(
        moves
            .iter()
            .map(|(_, f, t)| (f.as_str(), t.as_str()))
            .collect::<Vec<_>>(),
        vec![("3333", "1111"), ("3333", "1111")]
    );
    // 0 4 0 0; ts0 back: it takes half of them.
    set_heartbeat(&mut h, "0000", false);
    let moves = leader_moves(&mut h);
    assert_eq!(
        moves
            .iter()
            .map(|(_, f, t)| (f.as_str(), t.as_str()))
            .collect::<Vec<_>>(),
        vec![("1111", "0000"), ("1111", "0000")]
    );
    // 2 2 0 0.
    assert_eq!(leaders(&h)["0000"], 2);
    assert_eq!(leaders(&h)["1111"], 2);
}

#[test]
fn balancing_multi_secondary_affinitized_leader() {
    let mut h = Harness::new(4);
    h.prepare(tservers(5));
    affinitize(&mut h, &[&["a"], &["b", "c"], &["d"]]);
    set_heartbeat(&mut h, "0000", true);
    for i in 0..4 {
        h.move_tablet_leader(i, "3333");
    }
    // 0 0 0 4: the second priority takes them, evenly.
    let moves = leader_moves(&mut h);
    assert_eq!(moves.len(), 4);
    assert_eq!(froms(&moves), map(&[("3333", 4)]));
    assert_eq!(tos(&moves), map(&[("1111", 2), ("2222", 2)]));
    // 0 2 2 0; ts0 back: it takes them all.
    set_heartbeat(&mut h, "0000", false);
    let moves = leader_moves(&mut h);
    assert_eq!(moves.len(), 4);
    assert_eq!(tos(&moves), map(&[("0000", 4)]));
}

#[test]
fn balancing_multi_node_affinitized_leader() {
    let mut h = Harness::new(4);
    h.prepare(vec![
        ts("0000", "a"),
        ts("1111", "a"),
        ts("2222", "b"),
        ts("3333", "c"),
        ts("4444", "d"),
    ]);
    affinitize(&mut h, &[&["a"], &["b"]]);
    h.remove_replica(0, "0000");
    h.remove_replica(1, "0000");
    h.remove_replica(2, "1111");
    h.remove_replica(3, "1111");
    set_heartbeat(&mut h, "0000", true);
    for i in 0..4 {
        h.move_tablet_leader(i, "3333");
    }
    // 0 0 0 4: tablets 0 and 1 go to ts1 (zone a); 2 and 3 have no replica there, so zone b.
    let moves = leader_moves(&mut h);
    assert_eq!(moves.len(), 4);
    assert_eq!(froms(&moves), map(&[("3333", 4)]));
    assert_eq!(tos(&moves), map(&[("1111", 2), ("2222", 2)]));
    // 0 2 2 0; ts0 back: tablets 2 and 3 move to it.
    set_heartbeat(&mut h, "0000", false);
    let moves = leader_moves(&mut h);
    assert_eq!(tos(&moves), map(&[("0000", 2)]));
    assert_eq!(leaders(&h)["0000"], 2);
    assert_eq!(leaders(&h)["1111"], 2);
}

#[test]
fn balancing_with_leader_blacklist() {
    let mut h = Harness::new(4);
    h.prepare(vec![
        ts("0000", "a"),
        ts("1111", "a"),
        ts("2222", "b"),
        ts("3333", "c"),
        ts("4444", "d"),
    ]);
    h.remove_replica(0, "1111");
    h.remove_replica(1, "1111");
    h.remove_replica(2, "0000");
    h.remove_replica(3, "0000");
    for i in 0..4 {
        h.move_tablet_leader(i, ["0000", "1111"][i / 2]);
    }
    affinitize(&mut h, &[&["a"], &["b"]]);
    h.ts_mut("0000").leader_blacklisted = true;
    let ids: Vec<String> = h.view.tables[0].tablets[..2]
        .iter()
        .map(|t| t.id.clone())
        .collect();
    // 2 2 0, ts0 leader-blacklisted: its leaders go to the next priority, ts2.
    let moves = leader_moves(&mut h);
    assert_eq!(moves.len(), 2);
    for (tablet, from, to) in &moves {
        assert!(ids.contains(tablet));
        assert_eq!((from.as_str(), to.as_str()), ("0000", "2222"));
    }
    // 0 2 2; off the blacklist: back to ts0.
    h.ts_mut("0000").leader_blacklisted = false;
    let moves = leader_moves(&mut h);
    assert_eq!(moves.len(), 2);
    for (tablet, from, to) in &moves {
        assert!(ids.contains(tablet));
        assert_eq!((from.as_str(), to.as_str()), ("2222", "0000"));
    }
}

/// `Generate18NodeTsDesc`: every cloud0/cloud1, region a/b/c, zone x/y/z, uuids "0" to "17".
fn eighteen_tservers() -> Vec<TsInfo> {
    let mut out = Vec::new();
    for cloud in ["cloud0", "cloud1"] {
        for region in ["a", "b", "c"] {
            for zone in ["x", "y", "z"] {
                let mut ci = CloudInfoPB::new();
                ci.set_placement_cloud(cloud.into());
                ci.set_placement_region(region.into());
                ci.set_placement_zone(zone.into());
                out.push(TsInfo {
                    uuid: out.len().to_string(),
                    cloud_info: ci,
                    live_and_reported: true,
                    ..TsInfo::default()
                });
            }
        }
    }
    out
}

/// `TestWildcardLeaderPreference`: from all leaders on the first tserver, leaders end only on
/// the preferred tservers, at most one apart.
fn assert_wildcard_preference(h: &mut Harness, preferred: impl Fn(&CloudInfoPB) -> bool) {
    let preferred: BTreeSet<String> = h
        .view
        .tservers
        .iter()
        .filter(|t| preferred(&t.cloud_info))
        .map(|t| t.uuid.clone())
        .collect();
    assert!(!preferred.is_empty());
    let first = h.view.tservers[0].uuid.clone();
    for i in 0..4 {
        h.move_tablet_leader(i, &first);
    }
    leader_moves(h);
    let counts = leaders(h);
    for (uuid, n) in &counts {
        if !preferred.contains(uuid) {
            assert_eq!(*n, 0, "non-preferred tserver {uuid} has leaders");
        }
    }
    let on_preferred: Vec<usize> = preferred.iter().map(|u| counts[u]).collect();
    assert_eq!(on_preferred.iter().sum::<usize>(), 4);
    assert!(on_preferred.iter().max().unwrap() - on_preferred.iter().min().unwrap() <= 1);
}

#[test]
fn balancing_wildcard_region_leader_preference() {
    let mut h = Harness::new(4);
    h.prepare(eighteen_tservers());
    affinitize_cloud_info(&mut h, &[&[("cloud0", "*", "*")]]);
    assert_wildcard_preference(&mut h, |ci| ci.placement_cloud() == "cloud0");
}

#[test]
fn balancing_wildcard_zone_leader_preference() {
    let mut h = Harness::new(4);
    h.prepare(eighteen_tservers());
    affinitize_cloud_info(&mut h, &[&[("cloud0", "a", "*")]]);
    assert_wildcard_preference(&mut h, |ci| {
        ci.placement_cloud() == "cloud0" && ci.placement_region() == "a"
    });
}

#[test]
fn balancing_multi_priority_wildcard_leader_preference() {
    let mut h = Harness::new(4);
    h.prepare(eighteen_tservers());
    affinitize_cloud_info(&mut h, &[&[("cloud0", "a", "x")], &[("cloud0", "a", "*")]]);
    let last = h.view.tservers.last().unwrap().uuid.clone();
    for i in 0..4 {
        h.move_tablet_leader(i, &last);
    }
    leader_moves(&mut h);
    // Every leader on cloud0.a.x, the first priority.
    assert_eq!(leaders(&h)["0"], 4);

    // Without it, they fall back to cloud0.a.y and cloud0.a.z, evenly.
    set_heartbeat(&mut h, "0", true);
    for i in 0..4 {
        h.move_tablet_leader(i, &last);
    }
    leader_moves(&mut h);
    let counts = leaders(&h);
    assert_eq!(counts["1"] + counts["2"], 4);
    assert!(counts["1"].abs_diff(counts["2"]) <= 1);
}
