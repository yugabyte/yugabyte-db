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
    GetLoadMovePercentResponsePB, IsLoadBalancedRequestPB, IsLoadBalancedResponsePB,
    IsLoadBalancerIdleResponsePB,
};
use yb_proto::pb::master_types::{master_error_pb::Code, SysRowEntryType};

use crate::{master_error_from, LeaderServices};

impl LeaderServices {
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
