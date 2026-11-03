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
use std::time::Instant;

use master_catalog_entity::EntityId;
use protobuf::{Enum, EnumOrUnknown};
use tokio::sync::watch;
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::consensus_metadata::PeerMemberType;
use yb_proto::pb::master_rust_peer::{ReplicaLocationPB, TabletReplicasPB};

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
}

impl Default for TabletLocations {
    fn default() -> Self {
        TabletLocations {
            shards: (0..SHARDS).map(|_| Mutex::default()).collect(),
            loads: Mutex::default(),
            dirty: Mutex::default(),
            changed: watch::channel(0).0,
            updated_at: Mutex::new(None),
        }
    }
}

impl TabletLocations {
    fn shard(&self, tablet_id: &[u8]) -> &Mutex<HashMap<EntityId, TabletReplicas>> {
        let mut hasher = DefaultHasher::new();
        tablet_id.hash(&mut hasher);
        &self.shards[hasher.finish() as usize % SHARDS]
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
}
