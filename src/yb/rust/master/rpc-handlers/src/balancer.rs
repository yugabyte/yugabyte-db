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

//! The cluster balancer's RPCs (`CatalogManager::IsLoadBalancerIdle`, `IsLoadBalanced` and
//! `MasterClusterHandler::GetLoadMoveCompletionPercent`), from the balancer controller's status.

use std::sync::atomic::Ordering;
use std::time::Duration;

use master_catalog_state::CatalogSnapshot;
use master_ts_manager::placement::is_blacklisted;
use yb_host_api::{Status, StatusCode};
use yb_proto::pb::catalog_entity_info::{sys_tablets_entry_pb, SysClusterConfigEntryPB};
use yb_proto::pb::common_types::PeerRole;
use yb_proto::pb::master_cluster::{
    AreLeadersOnPreferredOnlyResponsePB, GetLoadMovePercentResponsePB, IsLoadBalancedRequestPB,
    IsLoadBalancedResponsePB, IsLoadBalancerIdleResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, SysRowEntryType};

use crate::{master_error_from, LeaderServices};

impl LeaderServices {
    /// `AreLeadersOnPreferredOnly`: every leader of a running table is on a live, non-blacklisted
    /// tserver of the live placement that its table's leader affinity accepts.
    pub fn are_leaders_on_preferred_only(
        &self,
        snapshot: &CatalogSnapshot,
        resp: &mut AreLeadersOnPreferredOnlyResponsePB,
    ) {
        if let Err(status) = self.leaders_on_preferred_only(snapshot) {
            resp.error = Some(master_error_from(
                Code::CAN_RETRY_ARE_LEADERS_ON_PREFERRED_ONLY_CHECK,
                &status,
            ))
            .into();
        }
    }

    fn leaders_on_preferred_only(&self, snapshot: &CatalogSnapshot) -> Result<(), Status> {
        use master_table_lifecycle::table_replication_info;
        use yb_proto::pb::catalog_entity_info::{sys_tables_entry_pb, SysTablesEntryPB};
        use protobuf::Enum;
        use yb_proto::pb::common_types::TableType;
        let config: SysClusterConfigEntryPB = snapshot
            .cluster_config()
            .and_then(|c| c.parse().ok())
            .unwrap_or_default();
        let live_uuid = config
            .replication_info
            .live_replicas
            .placement_uuid()
            .to_vec();
        let blacklist = master_ts_manager::placement::blacklist_addresses(&config);
        let tservers: Vec<_> = self
            .ts_manager
            .live()
            .into_iter()
            .filter(|d| live_uuid.is_empty() || d.placement_uuid() == live_uuid.as_slice())
            .filter(|d| !is_blacklisted(d, &blacklist))
            .collect();
        for table in snapshot.iter(SysRowEntryType::TABLE) {
            let Some(hot) = table.table() else { continue };
            // `is_running`: RUNNING or ALTERING.
            if hot.state != sys_tables_entry_pb::State::RUNNING as i32
                && hot.state != sys_tables_entry_pb::State::ALTERING as i32
            {
                continue;
            }
            if hot.table_type == TableType::TRANSACTION_STATUS_TABLE_TYPE.value()
                && !(self.transaction_tables_use_preferred_zones)()
            {
                continue;
            }
            let Ok(pb) = table.parse::<SysTablesEntryPB>() else {
                continue;
            };
            let info = table_replication_info(pb.replication_info.as_ref(), &config);
            // `TSDescriptor::IsAcceptingLeaderLoad`.
            let accepting = |d: &master_ts_manager::TsDescriptor| {
                let read_only = if info.live_replicas.has_placement_uuid() {
                    info.live_replicas.placement_uuid() != d.placement_uuid()
                } else {
                    !d.placement_uuid().is_empty()
                };
                if read_only {
                    return false;
                }
                if info.affinitized_leaders.is_empty() && info.multi_affinitized_leaders.is_empty()
                {
                    return true;
                }
                let ci = d.pb.registration.cloud_info.clone().unwrap_or_default();
                info.multi_affinitized_leaders
                    .iter()
                    .flat_map(|s| s.zones.iter())
                    .chain(info.affinitized_leaders.iter())
                    .any(|z| master_ts_manager::placement::is_cloud_info_prefix(z, &ci))
            };
            let accepting: Vec<&[u8]> = tservers
                .iter()
                .filter(|d| accepting(d))
                .map(|d| &*d.uuid)
                .collect();
            for tablet in snapshot.active_tablets_of(&table.key.id) {
                let Some(replicas) = self.ts_manager.locations.get(&tablet.key.id) else {
                    continue;
                };
                if let Some(leader) = replicas.leader() {
                    if !accepting.contains(&&*leader.ts_uuid) {
                        return Err(Status::new(
                            StatusCode::IllegalState,
                            format!(
                                "Tserver {} not expected to have leader of tablet {}",
                                String::from_utf8_lossy(&leader.ts_uuid),
                                String::from_utf8_lossy(&tablet.key.id)
                            ),
                        ));
                    }
                }
            }
        }
        Ok(())
    }

    /// `ClusterLoadBalancer::IsIdle`: a disabled balancer is idle.
    fn balancer_idle(&self) -> Result<(), Status> {
        let enabled =
            (self.load_balancing_flag)() && self.load_balancer_enabled.load(Ordering::Relaxed);
        if enabled && !self.balancer.is_idle() {
            return Err(Status::new(
                StatusCode::IllegalState,
                "Task or error encountered recently.",
            ));
        }
        Ok(())
    }

    /// `IsLoadBalancerIdle`.
    pub fn is_load_balancer_idle(&self, resp: &mut IsLoadBalancerIdleResponsePB) {
        if let Err(status) = self.balancer_idle() {
            resp.error = Some(master_error_from(
                Code::LOAD_BALANCER_RECENTLY_ACTIVE,
                &status,
            ))
            .into();
        }
    }

    /// `IsLoadBalanced`: enough live tservers, and the balancer idle.
    pub fn is_load_balanced(
        &self,
        req: &IsLoadBalancedRequestPB,
        resp: &mut IsLoadBalancedResponsePB,
    ) {
        let live = self.ts_manager.live().len();
        let result = if req.has_expected_num_servers()
            && req.expected_num_servers().max(0) as usize > live
        {
            Err(Status::new(
                StatusCode::IllegalState,
                format!(
                    "Found {live}, which is below the expected number of servers {}.",
                    req.expected_num_servers()
                ),
            ))
        } else {
            self.balancer_idle()
        };
        if let Err(status) = result {
            resp.error = Some(master_error_from(
                Code::CAN_RETRY_LOAD_BALANCE_CHECK,
                &status,
            ))
            .into();
        }
    }

    /// `GetLoadMoveCompletionPercent`: how much of the (leader) blacklisted tservers' initial
    /// replica (leader) load has moved off them.
    pub fn load_move_completion(
        &self,
        snapshot: &CatalogSnapshot,
        leaders_only: bool,
        initial_delay: Duration,
        resp: &mut GetLoadMovePercentResponsePB,
    ) {
        let config: SysClusterConfigEntryPB = snapshot
            .cluster_config()
            .and_then(|c| c.parse().ok())
            .unwrap_or_default();
        let blacklist = if leaders_only {
            &config.leader_blacklist
        } else {
            &config.server_blacklist
        };
        let addresses = blacklist
            .hosts
            .iter()
            .map(|h| (h.host().to_string(), h.port()))
            .collect();
        let blacklisted: Vec<Vec<u8>> = self
            .ts_manager
            .all()
            .into_iter()
            .filter(|d| is_blacklisted(d, &addresses))
            .map(|d| d.uuid.to_vec())
            .collect();
        // `GetNumRelevantReplicas`: replicas of tablets of existing tables, not deleted.
        let mut remaining: i64 = 0;
        if !blacklisted.is_empty() {
            for tablet in snapshot.iter(SysRowEntryType::TABLET) {
                let Some(hot) = tablet.tablet() else { continue };
                if hot.state == sys_tablets_entry_pb::State::DELETED as i32
                    || snapshot
                        .get(SysRowEntryType::TABLE, &hot.table_id)
                        .is_none()
                {
                    continue;
                }
                let Some(replicas) = self.ts_manager.locations.get(&tablet.key.id) else {
                    continue;
                };
                remaining += replicas
                    .replicas
                    .iter()
                    .filter(|r| !leaders_only || r.role == PeerRole::LEADER)
                    .filter(|r| blacklisted.iter().any(|b| **b == *r.ts_uuid))
                    .count() as i64;
            }
        }
        let mut initial = if leaders_only {
            blacklist.initial_leader_load()
        } else {
            blacklist.initial_replica_load()
        } as i64;
        // Right after an election the locations may not have the load yet.
        if remaining == 0
            && self
                .balancer
                .time_since_elected()
                .is_none_or(|t| t <= initial_delay)
        {
            resp.set_percent(0.0);
            resp.set_total(initial as u64);
            resp.set_remaining(initial as u64);
            return;
        }
        // A new leader may not have the initial load: report 0% rather than 100%.
        if initial < remaining {
            initial = remaining;
        }
        if initial == 0 {
            resp.set_percent(100.0);
            return;
        }
        resp.set_percent(100.0 - remaining as f64 * 100.0 / initial as f64);
        resp.set_remaining(remaining as u64);
        resp.set_total(initial as u64);
    }
}
