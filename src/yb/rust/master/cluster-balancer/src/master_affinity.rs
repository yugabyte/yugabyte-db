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

//! `CatalogManager::SysCatalogRespectLeaderAffinity`: the sys catalog leader steps down to a master
//! in a more preferred zone of the cluster's leader affinity, or off the leader blacklist.

use std::sync::{Arc, Mutex};
use std::time::Duration;

use master_catalog_state::Catalog;
use master_leadership::Leadership;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use master_ts_manager::placement::{
    any_blacklisted, cloud_info_contains, leader_blacklist_addresses,
};
use tokio::time::Instant;
use yb_host_api::Status;
use yb_proto::pb::catalog_entity_info::SysClusterConfigEntryPB;
use yb_proto::pb::common_net::CloudInfoPB;
use yb_proto::pb::consensus_metadata::RaftPeerPB;
use yb_proto::pb::wire_protocol::ServerRegistrationPB;

use crate::placement_id;
use crate::planner::affinitized_zones;

/// C++ logs these decisions at most every 10 s.
const LOG_INTERVAL: Duration = Duration::from_secs(10);

/// A master the sys catalog leader would rather hand leadership to.
#[derive(Clone, Debug, PartialEq)]
pub struct MasterCandidate {
    /// The permanent uuid.
    pub uuid: Vec<u8>,
    /// Its cloud info in the Raft config.
    pub cloud_info: CloudInfoPB,
}

/// What the sys catalog leader should do about its placement.
#[derive(Clone, Debug, PartialEq)]
pub struct LeaderMoves {
    /// `GetMoreEligibleSysCatalogLeaders`: the peers ranked above the leader, best first. The
    /// leader steps down to the first that Raft accepts.
    pub candidates: Vec<MasterCandidate>,
    /// The leader is on the leader blacklist.
    pub leader_blacklisted: bool,
    /// The leader may keep leading: it is in a preferred zone and not blacklisted.
    pub leader_eligible: bool,
}

/// The moves for the sys catalog leader `leader` among the committed config's `peers`; `None` when
/// there are no preferred zones and the leader is not on the leader blacklist.
///
/// A master's rank is the index of the first priority set of `affinitized_leaders` with a zone
/// (wildcards included) that covers it; one in no set ranks after them all, and a blacklisted one
/// last. Equal ranks keep the config's order.
pub fn sys_catalog_leader_moves(
    config: &SysClusterConfigEntryPB,
    leader: &ServerRegistrationPB,
    peers: &[RaftPeerPB],
) -> Option<LeaderMoves> {
    let blacklist = leader_blacklist_addresses(config);
    let zones = affinitized_zones(&config.replication_info);
    let rank = |ci: &CloudInfoPB, blacklisted: bool| {
        if blacklisted {
            return zones.len() + 1;
        }
        zones
            .iter()
            .position(|set| set.iter().any(|zone| cloud_info_contains(zone, ci)))
            .unwrap_or(zones.len())
    };
    let leader_blacklisted = any_blacklisted(
        leader
            .private_rpc_addresses
            .iter()
            .chain(&leader.broadcast_addresses),
        &blacklist,
    );
    if zones.is_empty() && !leader_blacklisted {
        return None;
    }
    let leader_rank = rank(&leader.cloud_info, leader_blacklisted);
    let mut ranked: Vec<(usize, MasterCandidate)> = peers
        .iter()
        .filter_map(|peer| {
            let blacklisted = any_blacklisted(
                peer.last_known_private_addr
                    .iter()
                    .chain(&peer.last_known_broadcast_addr),
                &blacklist,
            );
            let peer_rank = rank(&peer.cloud_info, blacklisted);
            (peer_rank < leader_rank).then(|| {
                (
                    peer_rank,
                    MasterCandidate {
                        uuid: peer.permanent_uuid().to_vec(),
                        cloud_info: peer.cloud_info.clone().unwrap_or_default(),
                    },
                )
            })
        })
        .collect();
    ranked.sort_by_key(|(peer_rank, _)| *peer_rank);
    Some(LeaderMoves {
        candidates: ranked.into_iter().map(|(_, c)| c).collect(),
        leader_blacklisted,
        leader_eligible: leader_rank < zones.len(),
    })
}

/// How a step-down request ended.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum StepDown {
    /// The leader stepped down to the requested master.
    Done,
    /// Raft refused: the master is not caught up (it may be down), or this one no longer leads.
    Refused(Status),
}

/// The sys catalog's Raft state and step-down, which the C++ host owns.
pub trait SysCatalogRaft: Send + Sync + 'static {
    /// This master's registration.
    fn registration(&self) -> Result<ServerRegistrationPB, Status>;
    /// The peers of the committed Raft config.
    fn committed_peers(&self) -> Result<Vec<RaftPeerPB>, Status>;
    /// A leader step-down to `new_leader`. Blocks.
    fn step_down_to(&self, new_leader: &[u8]) -> Result<StepDown, Status>;
}

/// Runs [`respect_leader_affinity`] each `catalog_manager_bg_task_wait_ms` while
/// `sys_catalog_respect_affinity_task` is set, as C++'s background task does.
pub struct MasterAffinityController {
    catalog: Arc<Catalog>,
    leadership: Arc<Leadership>,
    raft: Arc<dyn SysCatalogRaft>,
    enabled: fn() -> bool,
    interval: fn() -> Duration,
    last_log: Mutex<Option<Instant>>,
}

impl MasterAffinityController {
    /// `enabled` is `sys_catalog_respect_affinity_task`, `interval`
    /// `catalog_manager_bg_task_wait_ms`.
    pub fn new(
        catalog: Arc<Catalog>,
        leadership: Arc<Leadership>,
        raft: Arc<dyn SysCatalogRaft>,
        enabled: fn() -> bool,
        interval: fn() -> Duration,
    ) -> Arc<Self> {
        Arc::new(Self {
            catalog,
            leadership,
            raft,
            enabled,
            interval,
            last_log: Mutex::default(),
        })
    }

    fn should_log(&self) -> bool {
        let now = Instant::now();
        let mut last = self.last_log.lock().unwrap();
        if last.is_some_and(|t| now.duration_since(t) < LOG_INTERVAL) {
            return false;
        }
        *last = Some(now);
        true
    }
}

impl Controller for MasterAffinityController {
    fn name(&self) -> &'static str {
        "sys-catalog-leader-affinity"
    }

    fn interval(&self) -> Duration {
        (self.interval)().max(Duration::from_millis(1))
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(async move {
            if !(self.enabled)() || self.leadership.token().is_err() {
                return;
            }
            let config: SysClusterConfigEntryPB = self
                .catalog
                .snapshot()
                .cluster_config()
                .and_then(|c| c.parse().ok())
                .unwrap_or_default();
            let raft = self.raft.clone();
            let log = self.should_log();
            let result =
                tokio::task::spawn_blocking(move || respect_leader_affinity(&config, &*raft, log))
                    .await;
            if let Ok(Err(status)) = result {
                if log {
                    tracing::info!("{status}");
                }
            }
        })
    }
}

/// One pass of `SysCatalogRespectLeaderAffinity`: steps down to the best candidate Raft accepts.
/// Fails when the leader must move (it is blacklisted or outside every preferred zone) and no
/// candidate accepted. `log` lets the pass log what it tries.
pub fn respect_leader_affinity(
    config: &SysClusterConfigEntryPB,
    raft: &dyn SysCatalogRaft,
    log: bool,
) -> Result<(), Status> {
    let leader = raft.registration()?;
    let peers = raft.committed_peers()?;
    let Some(moves) = sys_catalog_leader_moves(config, &leader, &peers) else {
        return Ok(());
    };
    for candidate in &moves.candidates {
        if log {
            tracing::info!(
                "Sys catalog tablet leader is {}. Stepping down to master uuid {} in zone {}.",
                if moves.leader_blacklisted {
                    "blacklisted"
                } else {
                    "not in an affinitized zone"
                },
                String::from_utf8_lossy(&candidate.uuid),
                placement_id(&candidate.cloud_info),
            );
        }
        match raft.step_down_to(&candidate.uuid)? {
            StepDown::Done => return Ok(()),
            StepDown::Refused(reason) => {
                if log {
                    tracing::info!("Step down failed: {}", reason.message);
                }
            }
        }
    }
    if moves.leader_eligible {
        Ok(())
    } else {
        Err(Status::new(
            yb_host_api::StatusCode::NotFound,
            "Couldn't step down to an unblacklisted master in an affinitized zone",
        ))
    }
}

#[cfg(test)]
mod tests {
    use yb_proto::pb::catalog_entity_info::BlacklistPB;
    use yb_proto::pb::common_net::CloudInfoListPB;
    use yb_proto::pb::common_net::HostPortPB;

    use super::*;

    fn ci(cloud: &str, region: &str, zone: Option<&str>) -> CloudInfoPB {
        let mut ci = CloudInfoPB::new();
        ci.set_placement_cloud(cloud.into());
        ci.set_placement_region(region.into());
        if let Some(zone) = zone {
            ci.set_placement_zone(zone.into());
        }
        ci
    }

    fn hp(port: u32) -> HostPortPB {
        let mut hp = HostPortPB::new();
        hp.set_host("127.0.0.1".into());
        hp.set_port(port);
        hp
    }

    /// Master `i` is in zone `zones[i]` of c.r at port 7100 + i, with uuid "m<i>".
    fn peers(zones: &[&str]) -> Vec<RaftPeerPB> {
        zones
            .iter()
            .enumerate()
            .map(|(i, zone)| {
                let mut peer = RaftPeerPB::new();
                peer.set_permanent_uuid(format!("m{i}").into_bytes());
                peer.cloud_info = Some(ci("c", "r", Some(zone))).into();
                peer.last_known_private_addr.push(hp(7100 + i as u32));
                peer
            })
            .collect()
    }

    fn registration(peer: &RaftPeerPB) -> ServerRegistrationPB {
        let mut reg = ServerRegistrationPB::new();
        reg.cloud_info = peer.cloud_info.clone();
        reg.private_rpc_addresses = peer.last_known_private_addr.clone();
        reg
    }

    /// Priority sets of preferred zones, each zone "z<n>" of c.r or "*" for all of c.r.
    fn config(sets: &[&[&str]], blacklisted_ports: &[u32]) -> SysClusterConfigEntryPB {
        let mut config = SysClusterConfigEntryPB::new();
        let info = config.replication_info.mut_or_insert_default();
        for set in sets {
            let mut zones = CloudInfoListPB::new();
            for zone in *set {
                let zone = (*zone != "*").then_some(*zone);
                zones.zones.push(ci("c", "r", zone));
            }
            info.multi_affinitized_leaders.push(zones);
        }
        let mut blacklist = BlacklistPB::new();
        blacklist.hosts = blacklisted_ports.iter().map(|p| hp(*p)).collect();
        config.leader_blacklist = Some(blacklist).into();
        config
    }

    fn uuids(moves: &LeaderMoves) -> Vec<String> {
        moves
            .candidates
            .iter()
            .map(|c| String::from_utf8(c.uuid.clone()).unwrap())
            .collect()
    }

    fn moves(config: &SysClusterConfigEntryPB, leader: usize) -> Option<LeaderMoves> {
        let peers = peers(&["z0", "z1", "z2"]);
        sys_catalog_leader_moves(config, &registration(&peers[leader]), &peers)
    }

    #[test]
    fn nothing_to_do_without_preferred_zones_or_blacklist() {
        assert_eq!(moves(&config(&[], &[]), 0), None);
    }

    #[test]
    fn leader_in_preferred_zone_stays() {
        let m = moves(&config(&[&["z0"]], &[]), 0).unwrap();
        assert!(m.candidates.is_empty());
        assert!(m.leader_eligible);
    }

    #[test]
    fn leader_moves_to_preferred_zone() {
        let m = moves(&config(&[&["z1"]], &[]), 0).unwrap();
        assert_eq!(uuids(&m), ["m1"]);
        assert!(!m.leader_eligible && !m.leader_blacklisted);
    }

    #[test]
    fn priorities_order_candidates() {
        let m = moves(&config(&[&["z2"], &["z1"]], &[]), 0).unwrap();
        assert_eq!(uuids(&m), ["m2", "m1"]);
        // From the second priority only the first is better.
        let m = moves(&config(&[&["z2"], &["z1"]], &[]), 1).unwrap();
        assert_eq!(uuids(&m), ["m2"]);
        assert!(m.leader_eligible);
    }

    #[test]
    fn one_priority_set_ranks_its_zones_alike() {
        let m = moves(&config(&[&["z1", "z2"]], &[]), 0).unwrap();
        assert_eq!(uuids(&m), ["m1", "m2"]);
    }

    #[test]
    fn wildcard_covers_every_zone_of_its_region() {
        let m = moves(&config(&[&["*"]], &[]), 0).unwrap();
        assert!(m.candidates.is_empty() && m.leader_eligible);

        let m = moves(&config(&[&["z0"], &["*"]], &[]), 1).unwrap();
        assert_eq!(uuids(&m), ["m0"]);
        assert!(m.leader_eligible);

        // A master in another region is not covered.
        let mut peers = peers(&["z0", "z1", "z2"]);
        peers[2].cloud_info = Some(ci("c", "r2", Some("z3"))).into();
        let m = sys_catalog_leader_moves(
            &config(&[&["*"]], &[7101]),
            &registration(&peers[1]),
            &peers,
        )
        .unwrap();
        assert_eq!(uuids(&m), ["m0", "m2"]);
        assert!(m.leader_blacklisted && !m.leader_eligible);
    }

    #[test]
    fn blacklisted_leader_moves_anywhere_else() {
        let m = moves(&config(&[], &[7100]), 0).unwrap();
        assert_eq!(uuids(&m), ["m1", "m2"]);
        assert!(m.leader_blacklisted && !m.leader_eligible);
    }

    #[test]
    fn blacklisted_leader_prefers_preferred_zones() {
        let m = moves(&config(&[&["z0", "z2"]], &[7100]), 0).unwrap();
        assert_eq!(uuids(&m), ["m2", "m1"]);
    }

    #[test]
    fn blacklisted_peer_is_never_a_candidate() {
        let m = moves(&config(&[&["z1"]], &[7101]), 0).unwrap();
        assert!(m.candidates.is_empty());
        assert!(!m.leader_eligible);
    }

    #[test]
    fn blacklist_matches_broadcast_addresses() {
        let mut peers = peers(&["z0", "z1", "z2"]);
        peers[1].last_known_broadcast_addr.push(hp(9999));
        let m = sys_catalog_leader_moves(
            &config(&[&["z1"], &["z2"]], &[9999]),
            &registration(&peers[0]),
            &peers,
        )
        .unwrap();
        assert_eq!(uuids(&m), ["m2"]);
    }

    /// Raft that accepts a step-down only to the masters in `up`.
    struct FakeRaft {
        leader: ServerRegistrationPB,
        peers: Vec<RaftPeerPB>,
        up: Vec<&'static str>,
        asked: Mutex<Vec<String>>,
    }

    impl SysCatalogRaft for FakeRaft {
        fn registration(&self) -> Result<ServerRegistrationPB, Status> {
            Ok(self.leader.clone())
        }
        fn committed_peers(&self) -> Result<Vec<RaftPeerPB>, Status> {
            Ok(self.peers.clone())
        }
        fn step_down_to(&self, new_leader: &[u8]) -> Result<StepDown, Status> {
            let uuid = String::from_utf8(new_leader.to_vec()).unwrap();
            self.asked.lock().unwrap().push(uuid.clone());
            Ok(if self.up.contains(&uuid.as_str()) {
                StepDown::Done
            } else {
                StepDown::Refused(Status::new(
                    yb_host_api::StatusCode::IllegalState,
                    "not caught up",
                ))
            })
        }
    }

    fn fake(leader: usize, up: &[&'static str]) -> FakeRaft {
        let peers = peers(&["z0", "z1", "z2"]);
        FakeRaft {
            leader: registration(&peers[leader]),
            peers,
            up: up.to_vec(),
            asked: Mutex::default(),
        }
    }

    #[test]
    fn down_master_is_skipped_for_the_next_candidate() {
        let raft = fake(0, &["m2"]);
        let config = config(&[&["z1"], &["z2"]], &[]);
        respect_leader_affinity(&config, &raft, false).unwrap();
        assert_eq!(*raft.asked.lock().unwrap(), ["m1", "m2"]);
    }

    #[test]
    fn stops_at_the_first_accepted_step_down() {
        let raft = fake(0, &["m1", "m2"]);
        respect_leader_affinity(&config(&[&["z1"], &["z2"]], &[]), &raft, false).unwrap();
        assert_eq!(*raft.asked.lock().unwrap(), ["m1"]);
    }

    #[test]
    fn eligible_leader_stays_when_the_better_master_is_down() {
        let raft = fake(1, &[]);
        respect_leader_affinity(&config(&[&["z0"], &["z1"]], &[]), &raft, false).unwrap();
        assert_eq!(*raft.asked.lock().unwrap(), ["m0"]);
    }

    #[test]
    fn ineligible_leader_with_no_accepted_step_down_fails() {
        let raft = fake(0, &[]);
        let status = respect_leader_affinity(&config(&[&["z1"]], &[]), &raft, false).unwrap_err();
        assert_eq!(status.code, yb_host_api::StatusCode::NotFound);
        let raft = fake(0, &[]);
        assert!(respect_leader_affinity(&config(&[], &[7100]), &raft, false).is_err());
    }

    #[test]
    fn no_preferred_zones_asks_nothing() {
        let raft = fake(0, &["m1", "m2"]);
        respect_leader_affinity(&config(&[], &[]), &raft, false).unwrap();
        assert!(raft.asked.lock().unwrap().is_empty());
    }
}
