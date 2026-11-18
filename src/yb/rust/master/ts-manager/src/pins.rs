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

//! YSQL history retention pins (`enable_db_history_retention_pins`): each tserver reports the
//! oldest read time its live YSQL transactions pin per database, the leader answers every
//! heartbeat with the cluster-wide minimum per database, and publishes the overall minimum to the
//! sys catalog so master followers hold the pg catalog history back to it.

use std::collections::BTreeMap;
use std::sync::Arc;
use std::time::{Duration, Instant};

use master_catalog_state::{Catalog, CatalogWriter, Mutation};
use master_leadership::Leadership;
use master_task_runtime::{BoxFuture, Controller, TermScope};
use yb_proto::pb::catalog_entity_info::SysHistoryRetentionPinEntryPB;
use yb_proto::pb::master_heartbeat::{TSHeartbeatResponsePB, YsqlDbOldestPinnedReadTime};
use yb_proto::pb::master_types::SysRowEntryType;

use crate::TsManager;

/// `HybridTime::kInvalid`.
pub const INVALID_HYBRID_TIME: u64 = u64::MAX - 1;

/// How long an aggregate is reused while the live tservers stay the same: recomputing it is
/// O(live tservers), and every heartbeat needs it (C++ recomputes it per heartbeat).
const CACHE_FOR: Duration = Duration::from_millis(100);

/// `ClusterYsqlDbPins`: the oldest pinned read time per database over the live tservers.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ClusterYsqlPins {
    /// By database oid.
    pub pins: BTreeMap<u32, u64>,
    /// Every live tserver reported its pins in this term, and the post-failover registration
    /// window has passed unless registrations are persisted: an incomplete map must not be
    /// applied or published, or it would drop a pin a transaction still needs.
    pub ready: bool,
}

impl ClusterYsqlPins {
    /// `ClusterYsqlOldestPinnedReadTime`: the oldest pin of any database, as the sys catalog
    /// applies one cutoff to every database's catalog.
    pub fn oldest(&self) -> Option<u64> {
        self.pins.values().copied().min()
    }
}

impl TsManager {
    /// Records the pins a heartbeat reported (`TSDescriptor::UpdateFromHeartbeat`).
    pub(crate) fn pins_from_heartbeat(
        req: &yb_proto::pb::master_heartbeat::TSHeartbeatRequestPB,
    ) -> BTreeMap<u32, u64> {
        req.ts_ysql_db_oldest_pinned_read_times
            .iter()
            .map(|(db, pin)| (*db, pin.db_level_oldest_read_time()))
            .filter(|(_, pin)| *pin != INVALID_HYBRID_TIME)
            .collect()
    }

    /// `TSManager::GetClusterYsqlDbPinsForPublishing`, at most [`CACHE_FOR`] old. A tserver that
    /// registers or becomes live invalidates it, so a map is never ready without its pins.
    pub fn cluster_ysql_pins(&self) -> Arc<ClusterYsqlPins> {
        let membership = self.membership.load(std::sync::atomic::Ordering::Acquire);
        let mut cache = self.pins_cache.lock().unwrap();
        if let Some((at, version, pins)) = cache.as_ref() {
            if *version == membership && at.elapsed() < CACHE_FOR {
                return pins.clone();
            }
        }
        let mut pins = ClusterYsqlPins {
            pins: BTreeMap::new(),
            ready: true,
        };
        for descriptor in self.descriptors.read().unwrap().values() {
            if !descriptor.is_live() {
                continue;
            }
            let Some(reported) = &descriptor.ysql_db_pins else {
                pins.ready = false;
                continue;
            };
            for (db, pin) in reported {
                pins.pins
                    .entry(*db)
                    .and_modify(|p| *p = (*p).min(*pin))
                    .or_insert(*pin);
            }
        }
        // Without persisted registrations, a tserver that has not heartbeated this leader yet is
        // unknown, so wait out the registration window after becoming leader.
        let since_ready = self
            .leader_ready_at
            .lock()
            .unwrap()
            .map_or(Duration::ZERO, |t| t.elapsed());
        pins.ready = pins.ready
            && ((self.options.persist_registry)()
                || since_ready >= (self.options.initial_registration_duration)());
        let pins = Arc::new(pins);
        *cache = Some((Instant::now(), membership, pins.clone()));
        pins
    }

    /// `PopulateYsqlDbOldestPinnedReadTimes`.
    pub(crate) fn fill_ysql_pins(&self, resp: &mut TSHeartbeatResponsePB) {
        if !(self.options.ysql_db_pins)() {
            return;
        }
        let cluster = self.cluster_ysql_pins();
        resp.set_cluster_ysql_db_pins_ready(cluster.ready);
        for (db, pin) in &cluster.pins {
            let mut entry = YsqlDbOldestPinnedReadTime::new();
            entry.set_db_level_oldest_read_time(*pin);
            resp.cluster_ysql_db_oldest_pinned_read_times
                .insert(*db, entry);
        }
    }
}

/// Hands the host the leader's live cluster pin (host method `SetLiveYsqlHistoryPin`), and how
/// long it counts.
pub type LivePinSink = Arc<dyn Fn(Option<u64>, Duration) + Send + Sync>;

/// The `ysql-history-pin` controller: each `catalog_manager_bg_task_wait_ms`, gives the host the
/// live cluster pin and publishes it when it changed (`PersistYsqlHistoryRetentionPin`).
pub struct YsqlPinPublisher {
    ts_manager: Arc<TsManager>,
    catalog: Arc<Catalog>,
    writer: Arc<CatalogWriter>,
    leadership: Arc<Leadership>,
    interval: fn() -> Duration,
    live_sink: LivePinSink,
}

impl YsqlPinPublisher {
    /// A publisher for the term that just began.
    pub fn new(
        ts_manager: Arc<TsManager>,
        catalog: Arc<Catalog>,
        writer: Arc<CatalogWriter>,
        leadership: Arc<Leadership>,
        interval: fn() -> Duration,
        live_sink: LivePinSink,
    ) -> Arc<YsqlPinPublisher> {
        Arc::new(YsqlPinPublisher {
            ts_manager,
            catalog,
            writer,
            leadership,
            interval,
            live_sink,
        })
    }

    async fn pass(&self) {
        if !(self.ts_manager.options.ysql_db_pins)() {
            return;
        }
        let cluster = self.ts_manager.cluster_ysql_pins();
        // The C++ leader reads the live pins at every compaction; the host gets them each pass and
        // keeps them for a few passes, so a master that stopped leading stops holding them.
        (self.live_sink)(cluster.oldest(), (self.interval)() * 3);
        if !cluster.ready {
            return;
        }
        let pin = cluster.oldest();
        let current = self
            .catalog
            .snapshot()
            .get(SysRowEntryType::HISTORY_RETENTION_PIN, b"")
            .and_then(|e| e.parse::<SysHistoryRetentionPinEntryPB>().ok())
            .and_then(|pb| pb.ysql_oldest_pinned_read_time);
        if current == pin {
            return;
        }
        let Ok(token) = self.leadership.token() else {
            return;
        };
        let mut pb = SysHistoryRetentionPinEntryPB::new();
        pb.ysql_oldest_pinned_read_time = pin;
        let result = self
            .writer
            .mutate(&token, |_| {
                Ok((
                    Mutation::new().upsert_message(
                        SysRowEntryType::HISTORY_RETENTION_PIN,
                        b"",
                        &pb,
                    )?,
                    (),
                ))
            })
            .await;
        match result {
            Ok(_) => tracing::info!(
                "Published ysql catalog history retention pin: {}",
                pin.map_or("<invalid>".to_string(), |p| p.to_string())
            ),
            Err(status) => {
                tracing::warn!("Failed to publish the ysql catalog history retention pin: {status}")
            }
        }
    }
}

impl Controller for YsqlPinPublisher {
    fn name(&self) -> &'static str {
        "ysql-history-pin"
    }

    fn interval(&self) -> Duration {
        (self.interval)()
    }

    fn reconcile<'a>(&'a self, _scope: &'a TermScope) -> BoxFuture<'a, ()> {
        Box::pin(self.pass())
    }
}
