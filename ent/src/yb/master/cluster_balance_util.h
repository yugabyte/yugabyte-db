// Copyright (c) YugaByte, Inc.

#include "../../src/yb/master/cluster_balance_util.h"

#ifndef ENT_SRC_YB_MASTER_CLUSTER_BALANCE_UTIL_H
#define ENT_SRC_YB_MASTER_CLUSTER_BALANCE_UTIL_H

namespace yb {
namespace master {
namespace enterprise {

class ClusterLoadState : public yb::master::ClusterLoadState {
  typedef yb::master::ClusterLoadState super;
 public:
  void UpdateTabletServer(std::shared_ptr<TSDescriptor> ts_desc) override {
    super::UpdateTabletServer(ts_desc);
    const auto& ts_uuid = ts_desc->permanent_uuid();
    if (sorted_leader_load_.empty() ||
        sorted_leader_load_.back() != ts_uuid ||
        affinitized_zones_.empty()) {
      return;
    }
    TSRegistrationPB registration;
    ts_desc->GetRegistration(&registration);
    if (affinitized_zones_.find(registration.common().cloud_info()) == affinitized_zones_.end()) {
      sorted_leader_load_.pop_back();
      sorted_non_affinitized_leader_load_.push_back(ts_uuid);
    }
  }

  void SortLeaderLoad() override {
    auto leader_count_comparator = LeaderLoadComparator(this);
    sort(sorted_non_affinitized_leader_load_.begin(),
         sorted_non_affinitized_leader_load_.end(),
         leader_count_comparator);

    super::SortLeaderLoad();
  }

  // If affinitized leaders is enabled, stores leader load for non affinitized nodes.
  vector<TabletServerId> sorted_non_affinitized_leader_load_;
  // List of availiabity zones for affinitized leaders.
  AffinitizedZonesSet affinitized_zones_;
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif // ENT_SRC_YB_MASTER_CLUSTER_BALANCE_UTIL_H
