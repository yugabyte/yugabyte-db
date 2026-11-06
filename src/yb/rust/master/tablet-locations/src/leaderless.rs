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

//! Leader leases as tablet leaders report them, and which tablets have gone without a valid leader
//! (K7).
//!
//! C++ (`MasterPathHandlers::GetLeaderlessTablets`) flags a tablet once `now` is more than
//! `leaderless_tablet_alert_delay_secs` past the last valid-leader report, so heartbeats that queue
//! on a busy master, or a new leader that has not heard from tservers yet, look like outages. Here
//! only observable time counts: the window starts no earlier than the term's start plus a grace
//! period, and while the last leader's tserver is live it ends at that tserver's latest processed
//! heartbeat, since a report not yet processed is not evidence of anything.

use std::collections::HashMap;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use master_catalog_entity::EntityId;

/// `consensus::kInfiniteHybridTimeLeaseExpiration`: what an RF1 leader reports.
pub const INFINITE_HT_LEASE_EXPIRATION: u64 = u64::MAX;

#[derive(Clone, Debug)]
struct LeaseInfo {
    leader: EntityId,
    /// Physical microseconds; only ever raised for one leader, as reports can arrive late.
    ht_lease_expiration: u64,
    /// When a valid-leader report was last processed.
    last_valid: Option<Instant>,
}

/// One leader-lease report: `TabletLeaderMetricsPB` from a tserver that leads the tablet.
#[derive(Clone, Copy, Debug)]
pub struct LeaseReport<'a> {
    /// The tablet.
    pub tablet_id: &'a [u8],
    /// The reporting tserver, which the locations name as the tablet's leader.
    pub leader: &'a [u8],
    /// `leader_lease_status == HAS_LEASE`.
    pub has_lease: bool,
    /// `ht_lease_expiration`, physical microseconds.
    pub ht_lease_expiration: u64,
}

/// What the leaderless check needs besides the leases.
pub struct LeaderlessCheck<'a> {
    /// The current time.
    pub now: Instant,
    /// `leaderless_tablet_alert_delay_secs`.
    pub alert_delay: Duration,
    /// The term's start plus the grace period while locations are unconfirmed: nothing before it
    /// counts.
    pub observable_from: Instant,
    /// When the tserver's latest heartbeat was processed, if it is live; `None` for an
    /// unresponsive or unknown tserver.
    pub last_heartbeat: &'a dyn Fn(&[u8]) -> Option<Instant>,
}

/// Leader leases, sharded like the locations.
pub struct LeaderLeases {
    shards: Vec<Mutex<HashMap<EntityId, LeaseInfo>>>,
}

impl LeaderLeases {
    pub(crate) fn new(shards: usize) -> LeaderLeases {
        LeaderLeases {
            shards: (0..shards).map(|_| Mutex::default()).collect(),
        }
    }

    fn shard(&self, index: usize) -> &Mutex<HashMap<EntityId, LeaseInfo>> {
        &self.shards[index % self.shards.len()]
    }

    /// `ProcessTabletMetadata`'s lease rule: a report with a lease counts as a valid leader when
    /// its expiration does not go backwards for the same leader (an RF1 leader's infinite
    /// expiration is superseded by any later one) and is not more than `max_expired` in the past.
    pub(crate) fn record(
        &self,
        shard: usize,
        report: LeaseReport<'_>,
        now_physical_us: u64,
        max_expired: Duration,
        now: Instant,
    ) {
        let mut leases = self.shard(shard).lock().unwrap();
        let info = leases
            .entry(report.tablet_id.into())
            .or_insert_with(|| LeaseInfo {
                leader: report.leader.into(),
                ht_lease_expiration: 0,
                last_valid: None,
            });
        if &*info.leader != report.leader {
            info.leader = report.leader.into();
            info.ht_lease_expiration = 0;
        }
        if !report.has_lease {
            return;
        }
        let expired_for = Duration::from_micros(
            now_physical_us.saturating_sub(report.ht_lease_expiration.min(now_physical_us)),
        );
        let advances = report.ht_lease_expiration >= info.ht_lease_expiration
            || info.ht_lease_expiration == INFINITE_HT_LEASE_EXPIRATION;
        if advances && expired_for <= max_expired {
            info.ht_lease_expiration = report.ht_lease_expiration;
            info.last_valid = Some(now);
        }
    }

    pub(crate) fn forget(&self, shard: usize, tablet_id: &[u8]) {
        self.shard(shard).lock().unwrap().remove(tablet_id);
    }

    pub(crate) fn clear(&self) {
        for shard in &self.shards {
            shard.lock().unwrap().clear();
        }
    }

    /// How long the tablet has observably gone without a valid leader, if longer than the alert
    /// delay. `leader` is the leader the locations name, if any.
    pub(crate) fn leaderless_for(
        &self,
        shard: usize,
        tablet_id: &[u8],
        leader: Option<&[u8]>,
        check: &LeaderlessCheck<'_>,
    ) -> Option<Duration> {
        let last_valid = self
            .shard(shard)
            .lock()
            .unwrap()
            .get(tablet_id)
            .and_then(|info| info.last_valid);
        let start = last_valid.map_or(check.observable_from, |t| t.max(check.observable_from));
        let end = match leader.and_then(|uuid| (check.last_heartbeat)(uuid)) {
            Some(heartbeat) => heartbeat.min(check.now),
            None => check.now,
        };
        let gone = end.saturating_duration_since(start);
        (gone > check.alert_delay).then_some(gone)
    }
}
