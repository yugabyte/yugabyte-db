// Copyright (c) YugaByte, Inc.

#include "yb/master/cluster_balance.h"
#include "yb/master/cluster_balance_util.h"

using std::set;

namespace yb {
namespace master {
namespace enterprise {

bool ClusterLoadBalancer::HandleLeaderMoves(
    TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts) {
  if (HandleLeaderLoadIfNonAffinitized(out_tablet_id, out_from_ts, out_to_ts)) {
    MoveLeader(*out_tablet_id, *out_from_ts, *out_to_ts);
    return true;
  }

  return super::HandleLeaderMoves(out_tablet_id, out_from_ts, out_to_ts);
}

bool ClusterLoadBalancer::AnalyzeTablets(const TableId& table_uuid) {
  GetAllAffinitizedZones(&state_->affinitized_zones_);
  return super::AnalyzeTablets(table_uuid);
}

void ClusterLoadBalancer::GetAllAffinitizedZones(AffinitizedZonesSet* affinitized_zones) const {
  SysClusterConfigEntryPB config;
  if (!catalog_manager_->GetClusterConfig(&config).ok()) {
    return;
  }
  const int num_zones = config.replication_info().affinitized_leaders_size();
  for (int i = 0; i < num_zones; i++) {
    CloudInfoPB ci = config.replication_info().affinitized_leaders(i);
    affinitized_zones->insert(ci);
  }
}

bool ClusterLoadBalancer::HandleLeaderLoadIfNonAffinitized(TabletId* moving_tablet_id,
                                                           TabletServerId* from_ts,
                                                           TabletServerId* to_ts) {
  // Similar to normal leader balancing, we double iterate from most loaded to least loaded
  // non-affinitized nodes and least to most affinitized nodes. For each pair, we check whether
  // there is any tablet intersection and if so, there is a match and we return true.
  //
  // If we go through all the node pairs or we see that the current non-affinitized
  // leader load is 0, we know that there is no match from non-affinitized to affinitized nodes
  // and we return false.
  const int non_affinitized_last_pos = state_->sorted_non_affinitized_leader_load_.size() - 1;

  for (int non_affinitized_idx = non_affinitized_last_pos;
      non_affinitized_idx >= 0;
      non_affinitized_idx--) {
    for (const auto& affinitized_uuid : state_->sorted_leader_load_) {
      const TabletServerId& non_affinitized_uuid =
          state_->sorted_non_affinitized_leader_load_[non_affinitized_idx];
      if (state_->GetLeaderLoad(non_affinitized_uuid) == 0) {
        // All subsequent non-affinitized nodes have no leaders, no match found.
        return false;
      }

      const set<TabletId>& leaders = state_->per_ts_meta_[non_affinitized_uuid].leaders;
      const set<TabletId>& peers = state_->per_ts_meta_[affinitized_uuid].running_tablets;
      set<TabletId> intersection;
      const auto& itr = std::inserter(intersection, intersection.begin());
      std::set_intersection(leaders.begin(), leaders.end(), peers.begin(), peers.end(), itr);
      if (!intersection.empty()) {
        *moving_tablet_id = *intersection.begin();
        *from_ts = non_affinitized_uuid;
        *to_ts = affinitized_uuid;
        return true;
      }
    }
  }
  YB_LOG_EVERY_N(WARNING, 100)
    << "Cannot relocate leader from tablet server in non-affinitized zone to affinitized zone";
  return false;
}

} // namespace enterprise
} // namespace master
} // namespace yb
