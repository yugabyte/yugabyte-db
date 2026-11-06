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

//! Observed replica state: where each tablet's replicas are and what role each has, as tserver
//! reports say. This is not catalog state and is never written to the sys catalog. See
//! docs/tablet-locations.md.
//!
//! The leader updates it from heartbeats and pushes changes to Rust followers (L9), so a new
//! leader starts with warm locations. Entries a leader did not hear about in its own term are
//! `confirmed: false` until the tserver's full report.

use std::collections::hash_map::DefaultHasher;
use std::collections::{HashMap, HashSet};
use std::hash::{Hash, Hasher};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use master_catalog_entity::EntityId;
use protobuf::{Enum, EnumOrUnknown};
use tokio::sync::watch;
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_rust_peer::{ReplicaLocationPB, TabletReplicasPB};

mod leaderless;

pub use leaderless::{LeaderlessCheck, LeaseReport, INFINITE_HT_LEASE_EXPIRATION};

const SHARDS: usize = 64;

/// One replica of a tablet.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Replica {
    /// The tserver.
    pub ts_uuid: EntityId,
    /// Its Raft role.
    pub role: PeerRole,
    /// Its member type.
    pub member_type: PeerMemberType,
}

/// A tablet's replicas.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TabletReplicas {
    /// In no particular order.
    pub replicas: Vec<Replica>,
    /// Set by a report in the current leader's term.
    pub confirmed: bool,
}

impl TabletReplicas {
    /// The leader replica, if any.
    pub fn leader(&self) -> Option<&Replica> {
        self.replicas.iter().find(|r| r.role == PeerRole::LEADER)
    }
}

/// Replica and leader counts of one tserver.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TServerLoad {
    /// Replicas hosted.
    pub replicas: u32,
    /// Of which leaders.
    pub leaders: u32,
}

/// Every tablet's observed replicas, sharded by tablet id.
pub struct TabletLocations {
    shards: Vec<Mutex<HashMap<EntityId, TabletReplicas>>>,
    loads: Mutex<HashMap<EntityId, TServerLoad>>,
    /// Tablets changed since the last [`TabletLocations::take_dirty`].
    dirty: Mutex<HashSet<EntityId>>,
    changed: watch::Sender<u64>,
    /// When the leader last heard about each tablet, for the location stream's pacing.
    updated_at: Mutex<Option<Instant>>,
    leases: leaderless::LeaderLeases,
}

impl Default for TabletLocations {
    fn default() -> Self {
        TabletLocations {
            shards: (0..SHARDS).map(|_| Mutex::default()).collect(),
            loads: Mutex::default(),
            dirty: Mutex::default(),
            changed: watch::channel(0).0,
            updated_at: Mutex::new(None),
            leases: leaderless::LeaderLeases::new(SHARDS),
        }
    }
}

impl TabletLocations {
    fn shard_index(tablet_id: &[u8]) -> usize {
        let mut hasher = DefaultHasher::new();
        tablet_id.hash(&mut hasher);
        hasher.finish() as usize % SHARDS
    }

    fn shard(&self, tablet_id: &[u8]) -> &Mutex<HashMap<EntityId, TabletReplicas>> {
        &self.shards[Self::shard_index(tablet_id)]
    }

    /// Records a leader-lease report. Ignored unless the locations name the reporting tserver as
    /// the tablet's leader, as C++ only tracks leases of the current leader.
    pub fn record_leader_lease(
        &self,
        report: LeaseReport<'_>,
        now_physical_us: u64,
        max_expired: Duration,
        now: Instant,
    ) {
        let is_leader = self
            .get(report.tablet_id)
            .is_some_and(|r| r.leader().is_some_and(|l| &*l.ts_uuid == report.leader));
        if is_leader {
            self.leases.record(
                Self::shard_index(report.tablet_id),
                report,
                now_physical_us,
                max_expired,
                now,
            );
        }
    }

    /// The tablets among `tablet_ids` that have observably gone without a valid leader for
    /// longer than the alert delay, with how long (K7; see the `leaderless` module).
    pub fn leaderless<'a>(
        &self,
        tablet_ids: impl IntoIterator<Item = &'a [u8]>,
        check: &LeaderlessCheck<'_>,
    ) -> Vec<(EntityId, Duration)> {
        tablet_ids
            .into_iter()
            .filter_map(|tablet_id| {
                let leader = self
                    .get(tablet_id)
                    .and_then(|r| r.leader().map(|l| l.ts_uuid.clone()));
                self.leases
                    .leaderless_for(
                        Self::shard_index(tablet_id),
                        tablet_id,
                        leader.as_deref(),
                        check,
                    )
                    .map(|gone| (EntityId::from(tablet_id), gone))
            })
            .collect()
    }

    /// The tablet's replicas.
    pub fn get(&self, tablet_id: &[u8]) -> Option<TabletReplicas> {
        self.shard(tablet_id)
            .lock()
            .unwrap()
            .get(tablet_id)
            .cloned()
    }

    /// Replaces the tablet's replica set.
    pub fn set(&self, tablet_id: &[u8], replicas: TabletReplicas) {
        {
            let mut shard = self.shard(tablet_id).lock().unwrap();
            let old = shard.insert(tablet_id.into(), replicas.clone());
            self.account(old.as_ref(), Some(&replicas));
        }
        self.mark_dirty(tablet_id);
    }

    /// Sets one tserver's replica of the tablet, keeping the others, and confirms the entry.
    pub fn update_replica(&self, tablet_id: &[u8], replica: Replica) {
        let changed = {
            let mut shard = self.shard(tablet_id).lock().unwrap();
            let entry = shard.entry(tablet_id.into()).or_default();
            let old = entry.clone();
            match entry
                .replicas
                .iter_mut()
                .find(|r| r.ts_uuid == replica.ts_uuid)
            {
                Some(existing) => *existing = replica,
                None => entry.replicas.push(replica),
            }
            entry.confirmed = true;
            let changed = old != *entry;
            if changed {
                let new = entry.clone();
                self.account(Some(&old), Some(&new));
            }
            changed
        };
        if changed {
            self.mark_dirty(tablet_id);
        }
    }

    /// Forgets the tablet.
    pub fn remove(&self, tablet_id: &[u8]) {
        let removed = {
            let mut shard = self.shard(tablet_id).lock().unwrap();
            let old = shard.remove(tablet_id);
            self.account(old.as_ref(), None);
            old.is_some()
        };
        self.leases.forget(Self::shard_index(tablet_id), tablet_id);
        if removed {
            self.mark_dirty(tablet_id);
        }
    }

    /// Forgets everything.
    pub fn clear(&self) {
        let mut shards: Vec<_> = self.shards.iter().map(|s| s.lock().unwrap()).collect();
        for shard in &mut shards {
            shard.clear();
        }
        self.loads.lock().unwrap().clear();
        drop(shards);
        self.leases.clear();
        self.changed.send_modify(|v| *v += 1);
    }

    /// A new leader: everything is warm but unconfirmed until reported in this term.
    pub fn mark_all_unconfirmed(&self) {
        for shard in &self.shards {
            for replicas in shard.lock().unwrap().values_mut() {
                replicas.confirmed = false;
            }
        }
    }

    /// Replica and leader counts of a tserver.
    pub fn load(&self, ts_uuid: &[u8]) -> TServerLoad {
        self.loads
            .lock()
            .unwrap()
            .get(ts_uuid)
            .copied()
            .unwrap_or_default()
    }

    /// Tablets with a replica on the tserver.
    pub fn tablets_on(&self, ts_uuid: &[u8]) -> Vec<EntityId> {
        let mut result = Vec::new();
        for shard in &self.shards {
            for (tablet_id, replicas) in shard.lock().unwrap().iter() {
                if replicas.replicas.iter().any(|r| &*r.ts_uuid == ts_uuid) {
                    result.push(tablet_id.clone());
                }
            }
        }
        result
    }

    /// Every tablet's replicas.
    pub fn all(&self) -> Vec<(EntityId, TabletReplicas)> {
        let mut result = Vec::new();
        for shard in &self.shards {
            result.extend(
                shard
                    .lock()
                    .unwrap()
                    .iter()
                    .map(|(id, r)| (id.clone(), r.clone())),
            );
        }
        result
    }

    /// Tablets changed since the last call, with their current replicas (`None` when removed).
    pub fn take_dirty(&self) -> Vec<(EntityId, Option<TabletReplicas>)> {
        let dirty = std::mem::take(&mut *self.dirty.lock().unwrap());
        dirty
            .into_iter()
            .map(|id| {
                let replicas = self.get(&id);
                (id, replicas)
            })
            .collect()
    }

    /// Bumped on every change.
    pub fn subscribe(&self) -> watch::Receiver<u64> {
        self.changed.subscribe()
    }

    /// When the locations last changed.
    pub fn last_update(&self) -> Option<Instant> {
        *self.updated_at.lock().unwrap()
    }

    fn mark_dirty(&self, tablet_id: &[u8]) {
        self.dirty.lock().unwrap().insert(tablet_id.into());
        *self.updated_at.lock().unwrap() = Some(Instant::now());
        self.changed.send_modify(|v| *v += 1);
    }

    /// Called with the tablet's shard locked, so changes to one tablet are counted in the order
    /// they were made. Lock order: shard, then loads.
    fn account(&self, old: Option<&TabletReplicas>, new: Option<&TabletReplicas>) {
        let mut loads = self.loads.lock().unwrap();
        for (replicas, sign) in [(old, -1i64), (new, 1i64)] {
            for replica in replicas.into_iter().flat_map(|r| &r.replicas) {
                let load = loads.entry(replica.ts_uuid.clone()).or_default();
                load.replicas = (load.replicas as i64 + sign).max(0) as u32;
                if replica.role == PeerRole::LEADER {
                    load.leaders = (load.leaders as i64 + sign).max(0) as u32;
                }
            }
        }
        loads.retain(|_, l| l.replicas > 0);
    }

    /// The location stream message for one tablet.
    pub fn to_pb(tablet_id: &[u8], replicas: Option<&TabletReplicas>) -> TabletReplicasPB {
        let mut pb = TabletReplicasPB::new();
        pb.set_tablet_id(tablet_id.to_vec());
        for replica in replicas.into_iter().flat_map(|r| &r.replicas) {
            let mut r = ReplicaLocationPB::new();
            r.set_ts_uuid(replica.ts_uuid.to_vec());
            r.role = Some(EnumOrUnknown::new(replica.role));
            r.member_type = Some(EnumOrUnknown::new(replica.member_type));
            pb.replicas.push(r);
        }
        pb
    }

    /// Applies one tablet from the location stream, as warm and unconfirmed.
    pub fn apply_pb(&self, pb: &TabletReplicasPB) {
        if pb.replicas.is_empty() {
            self.remove(pb.tablet_id());
            return;
        }
        let replicas = pb
            .replicas
            .iter()
            .map(|r| Replica {
                ts_uuid: r.ts_uuid().into(),
                role: r
                    .role
                    .and_then(|v| PeerRole::from_i32(v.value()))
                    .unwrap_or(PeerRole::UNKNOWN_ROLE),
                member_type: r
                    .member_type
                    .and_then(|v| PeerMemberType::from_i32(v.value()))
                    .unwrap_or(PeerMemberType::UNKNOWN_MEMBER_TYPE),
            })
            .collect();
        self.set(
            pb.tablet_id(),
            TabletReplicas {
                replicas,
                confirmed: false,
            },
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn replica(ts: &str, role: PeerRole) -> Replica {
        Replica {
            ts_uuid: ts.as_bytes().into(),
            role,
            member_type: PeerMemberType::VOTER,
        }
    }

    #[test]
    fn loads_follow_changes() {
        let locations = TabletLocations::default();
        locations.set(
            b"t1",
            TabletReplicas {
                replicas: vec![
                    replica("a", PeerRole::LEADER),
                    replica("b", PeerRole::FOLLOWER),
                ],
                confirmed: true,
            },
        );
        assert_eq!(
            locations.load(b"a"),
            TServerLoad {
                replicas: 1,
                leaders: 1
            }
        );
        locations.update_replica(b"t1", replica("b", PeerRole::LEADER));
        locations.update_replica(b"t1", replica("a", PeerRole::FOLLOWER));
        assert_eq!(locations.load(b"a").leaders, 0);
        assert_eq!(locations.load(b"b").leaders, 1);
        assert_eq!(locations.tablets_on(b"b").len(), 1);
        locations.remove(b"t1");
        assert_eq!(locations.load(b"b"), TServerLoad::default());
    }

    #[test]
    fn stream_round_trip_is_unconfirmed() {
        let leader = TabletLocations::default();
        leader.set(
            b"t1",
            TabletReplicas {
                replicas: vec![replica("a", PeerRole::LEADER)],
                confirmed: true,
            },
        );
        let dirty = leader.take_dirty();
        assert_eq!(dirty.len(), 1);
        assert!(leader.take_dirty().is_empty());
        let follower = TabletLocations::default();
        for (id, replicas) in &dirty {
            follower.apply_pb(&TabletLocations::to_pb(id, replicas.as_ref()));
        }
        let warm = follower.get(b"t1").unwrap();
        assert!(!warm.confirmed);
        assert_eq!(warm.leader().unwrap().ts_uuid.as_ref(), b"a");
        follower.apply_pb(&TabletLocations::to_pb(b"t1", None));
        assert!(follower.get(b"t1").is_none());
    }

    #[test]
    fn new_leader_unconfirms() {
        let locations = TabletLocations::default();
        locations.update_replica(b"t1", replica("a", PeerRole::LEADER));
        assert!(locations.get(b"t1").unwrap().confirmed);
        locations.mark_all_unconfirmed();
        assert!(!locations.get(b"t1").unwrap().confirmed);
    }

    fn led_by_a() -> TabletLocations {
        let locations = TabletLocations::default();
        locations.set(
            b"t1",
            TabletReplicas {
                replicas: vec![
                    replica("a", PeerRole::LEADER),
                    replica("b", PeerRole::FOLLOWER),
                ],
                confirmed: true,
            },
        );
        locations
    }

    fn lease(leader: &'static str, has_lease: bool, expiration: u64) -> LeaseReport<'static> {
        LeaseReport {
            tablet_id: b"t1",
            leader: leader.as_bytes(),
            has_lease,
            ht_lease_expiration: expiration,
        }
    }

    const NOW_US: u64 = 1_000_000_000;
    const MAX_EXPIRED: Duration = Duration::from_secs(120);
    const DELAY: Duration = Duration::from_secs(120);

    #[test]
    fn late_heartbeats_are_not_leaderless() {
        let locations = led_by_a();
        let t0 = Instant::now();
        locations.record_leader_lease(lease("a", true, NOW_US), NOW_US, MAX_EXPIRED, t0);
        let now = t0 + Duration::from_secs(300);
        let leaderless = |last_heartbeat: &dyn Fn(&[u8]) -> Option<Instant>| {
            locations.leaderless(
                [&b"t1"[..]],
                &LeaderlessCheck {
                    now,
                    alert_delay: DELAY,
                    observable_from: t0 - Duration::from_secs(1000),
                    last_heartbeat,
                },
            )
        };
        // The leader's tserver is live but its heartbeats have not been processed since t0 + 10s:
        // nothing says the tablet lost its leader.
        assert!(leaderless(&|_| Some(t0 + Duration::from_secs(10))).is_empty());
        // Its heartbeats were processed until now without a valid lease: leaderless for 300 s.
        let found = leaderless(&|_| Some(now));
        assert_eq!(
            found,
            vec![(EntityId::from(&b"t1"[..]), Duration::from_secs(300))]
        );
        // The leader's tserver is unresponsive: leaderless as well.
        assert_eq!(leaderless(&|_| None).len(), 1);
    }

    #[test]
    fn a_new_term_waits_for_the_grace_period() {
        let locations = led_by_a();
        let term_start = Instant::now();
        let check = |now: Instant| LeaderlessCheck {
            now,
            alert_delay: DELAY,
            observable_from: term_start,
            last_heartbeat: &|_: &[u8]| None,
        };
        // No lease report in this term, and the leader's tserver never heartbeated.
        assert!(locations
            .leaderless([&b"t1"[..]], &check(term_start + DELAY / 2))
            .is_empty());
        assert_eq!(
            locations
                .leaderless([&b"t1"[..]], &check(term_start + DELAY * 2))
                .len(),
            1
        );
    }

    #[test]
    fn lease_reports_follow_the_cpp_rules() {
        let locations = led_by_a();
        let t0 = Instant::now();
        let gone_at = |now: Instant| {
            locations
                .leaderless(
                    [&b"t1"[..]],
                    &LeaderlessCheck {
                        now,
                        alert_delay: Duration::ZERO,
                        observable_from: t0,
                        last_heartbeat: &|_: &[u8]| None,
                    },
                )
                .first()
                .map(|(_, gone)| *gone)
        };
        let later = |s: u64| t0 + Duration::from_secs(s);

        // An RF1 leader's infinite expiration is valid, and any later expiration supersedes it.
        locations.record_leader_lease(
            lease("a", true, INFINITE_HT_LEASE_EXPIRATION),
            NOW_US,
            MAX_EXPIRED,
            later(10),
        );
        assert_eq!(gone_at(later(10)), None);
        locations.record_leader_lease(lease("a", true, NOW_US), NOW_US, MAX_EXPIRED, later(20));
        assert_eq!(gone_at(later(25)), Some(Duration::from_secs(5)));
        // An expiration that goes backwards is a stale report.
        locations.record_leader_lease(lease("a", true, NOW_US - 1), NOW_US, MAX_EXPIRED, later(30));
        assert_eq!(gone_at(later(30)), Some(Duration::from_secs(10)));
        // No lease, or a lease expired for too long, is not a valid leader.
        locations.record_leader_lease(lease("a", false, NOW_US), NOW_US, MAX_EXPIRED, later(40));
        let long_ago = NOW_US - MAX_EXPIRED.as_micros() as u64 - 1;
        locations.record_leader_lease(lease("a", true, long_ago), NOW_US, MAX_EXPIRED, later(40));
        assert_eq!(gone_at(later(40)), Some(Duration::from_secs(20)));
        // A tserver the locations do not name as leader is ignored.
        locations.record_leader_lease(lease("b", true, NOW_US), NOW_US, MAX_EXPIRED, later(50));
        assert_eq!(gone_at(later(50)), Some(Duration::from_secs(30)));
        // A removed tablet forgets its lease.
        locations.remove(b"t1");
        assert_eq!(gone_at(later(60)), Some(Duration::from_secs(60)));
    }
}
