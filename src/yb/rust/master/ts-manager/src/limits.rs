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

//! Tablet replica limits (`tablet_creation_limits.cc`, `common/tablet_limits.cc`): a table
//! creation or split that would take the universe's running tablet replicas past what its
//! tservers' cores and tablet overhead memory support is refused.

use std::collections::HashSet;

use crate::placement::is_blacklisted;
use crate::{TsDescriptor, TsManager};

/// `AggregatedClusterInfo`: the resources of the live, non-blacklisted tservers of one placement.
/// A total is None when some tserver did not report it.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct AggregatedClusterInfo {
    /// Bytes the tservers reserve for tablet overheads.
    pub total_memory: Option<i64>,
    /// Cores.
    pub total_cores: Option<i64>,
    /// Running tablet replicas, as the tservers last reported.
    pub total_live_replicas: i64,
}

/// `ComputeAggregatedClusterInfo`.
pub fn aggregate(
    tservers: &[TsDescriptor],
    blacklist: &HashSet<(String, u32)>,
    placement_uuid: &[u8],
) -> AggregatedClusterInfo {
    let mut info = AggregatedClusterInfo {
        total_memory: Some(0),
        total_cores: Some(0),
        total_live_replicas: 0,
    };
    for ts in tservers {
        if !ts.is_live() || is_blacklisted(ts, blacklist) || ts.placement_uuid() != placement_uuid {
            continue;
        }
        let resources = ts.pb.resources.get_or_default();
        info.total_cores = info
            .total_cores
            .zip(resources.core_count)
            .map(|(a, b)| a + b);
        info.total_memory = info
            .total_memory
            .zip(resources.tablet_overhead_ram_in_bytes)
            .map(|(a, b)| a + b);
        info.total_live_replicas += i64::from(ts.num_live_replicas);
    }
    info
}

/// `ComputeTabletReplicaLimit`, with `tablet_replicas_per_core_limit` and
/// `tablet_replicas_per_gib_limit` (0 for none): `i64::MAX` when nothing limits.
pub fn replica_limit(info: &AggregatedClusterInfo, per_core: u32, per_gib: u32) -> i64 {
    let mut limit = match info.total_cores {
        Some(cores) if per_core > 0 => i64::from(per_core) * cores,
        _ => i64::MAX,
    };
    if let Some(memory) = info.total_memory.filter(|_| per_gib > 0) {
        // Less than 1 GiB per tserver is allowed, so in floating point.
        let by_memory = (f64::from(per_gib) * (memory as f64 / (1u64 << 30) as f64)).trunc();
        limit = limit.min(by_memory.round() as i64);
    }
    limit
}

impl TsManager {
    /// `CanSupportAdditionalTabletsForTableCreation` (and `CanSupportAdditionalTablet` for a
    /// split): an error when `enforce_tablet_replica_limits` is on and `replicas` more tablet
    /// replicas in the placement would pass the limit. Not checked for
    /// `initial_tserver_registration_duration_secs` after this master became leader, when not
    /// every live tserver may have reported yet.
    pub fn check_tablet_replica_limits(
        &self,
        replicas: i64,
        placement_uuid: &[u8],
        blacklist: &HashSet<(String, u32)>,
    ) -> Result<(), String> {
        let ready_at = *self.leader_ready_at.lock().unwrap();
        if ready_at.is_none_or(|at| at.elapsed() < (self.options.initial_registration_duration)()) {
            return Ok(());
        }
        self.can_create_tablet_replicas(replicas, placement_uuid, blacklist)
    }

    /// `CanCreateTabletReplicas`: the check itself, with no registration grace, as a clone
    /// (`CheckTabletLimits`) calls it.
    pub fn can_create_tablet_replicas(
        &self,
        replicas: i64,
        placement_uuid: &[u8],
        blacklist: &HashSet<(String, u32)>,
    ) -> Result<(), String> {
        let options = &self.options;
        if replicas <= 0 || !(options.enforce_tablet_replica_limits)() {
            return Ok(());
        }
        let (per_core, per_gib) = (
            (options.tablet_replicas_per_core_limit)(),
            (options.tablet_replicas_per_gib_limit)(),
        );
        if per_core == 0 && per_gib == 0 {
            return Ok(());
        }
        let info = aggregate(&self.all(), blacklist, placement_uuid);
        let limit = replica_limit(&info, per_core, per_gib);
        let total = info.total_live_replicas + replicas;
        if total > limit {
            return Err(format!(
                "The requested number of tablet replicas ({replicas}) would cause the total \
                 running tablet replica count ({total}) to exceed the safe system maximum \
                 ({limit})"
            ));
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;
    use std::time::{Duration, Instant};

    use super::*;
    use crate::{TsManagerOptions, TsState};

    fn tserver(uuid: &str, cores: i64, gib: i64, replicas: i32) -> TsDescriptor {
        let mut ts = TsDescriptor::new(Arc::from(uuid.as_bytes()));
        ts.pb.set_state(TsState::LIVE);
        let resources = ts.pb.resources.mut_or_insert_default();
        resources.set_core_count(cores);
        resources.set_tablet_overhead_ram_in_bytes(gib << 30);
        ts.num_live_replicas = replicas;
        ts
    }

    #[test]
    fn the_limit_is_the_lower_of_cores_and_memory() {
        let info = aggregate(
            &[tserver("a", 2, 1, 5), tserver("b", 2, 1, 7)],
            &HashSet::new(),
            b"",
        );
        assert_eq!(
            info,
            AggregatedClusterInfo {
                total_memory: Some(2 << 30),
                total_cores: Some(4),
                total_live_replicas: 12,
            }
        );
        assert_eq!(replica_limit(&info, 10, 0), 40);
        assert_eq!(replica_limit(&info, 10, 15), 30);
        assert_eq!(replica_limit(&info, 0, 0), i64::MAX);
        // A tserver that did not report its cores leaves them out of the limit.
        let mut unknown = tserver("c", 0, 1, 0);
        unknown.pb.resources.mut_or_insert_default().core_count = None;
        let info = aggregate(&[tserver("a", 2, 1, 5), unknown], &HashSet::new(), b"");
        assert_eq!(info.total_cores, None);
        assert_eq!(replica_limit(&info, 10, 0), i64::MAX);
    }

    #[test]
    fn creation_is_refused_past_the_limit_once_enforced_and_registered() {
        let manager = TsManager::new(
            Arc::default(),
            TsManagerOptions {
                enforce_tablet_replica_limits: || true,
                tablet_replicas_per_core_limit: || 3,
                tablet_replicas_per_gib_limit: || 0,
                initial_registration_duration: || Duration::from_secs(60),
                ..TsManagerOptions::default()
            },
        );
        manager
            .descriptors
            .write()
            .unwrap()
            .insert(Arc::from(b"a".as_slice()), tserver("a", 1, 1, 2));
        let blacklist = HashSet::new();
        // Not every tserver may have reported yet; a clone checks regardless.
        assert!(manager
            .check_tablet_replica_limits(5, b"", &blacklist)
            .is_ok());
        assert!(manager
            .can_create_tablet_replicas(5, b"", &blacklist)
            .is_err());
        *manager.leader_ready_at.lock().unwrap() = Some(Instant::now() - Duration::from_secs(61));
        assert!(manager
            .check_tablet_replica_limits(1, b"", &blacklist)
            .is_ok());
        let e = manager
            .check_tablet_replica_limits(2, b"", &blacklist)
            .unwrap_err();
        assert!(e.contains("exceed the safe system maximum (3)"), "{e}");
    }
}
