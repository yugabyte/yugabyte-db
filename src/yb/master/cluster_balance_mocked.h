// Copyright (c) YugaByte, Inc.

#ifndef YB_MASTER_CLUSTER_BALANCE_MOCKED_H
#define YB_MASTER_CLUSTER_BALANCE_MOCKED_H

#include "yb/master/cluster_balance.h"

namespace yb {
namespace master {

class ClusterLoadBalancerMocked : public ClusterLoadBalancer {
 public:
  ClusterLoadBalancerMocked() : ClusterLoadBalancer(nullptr) {
    const int kHighNumber = 100;
    options_.kMaxConcurrentAdds = kHighNumber;
    options_.kMaxConcurrentRemovals = kHighNumber;
    options_.kAllowLimitStartingTablets = false;
    options_.kAllowLimitOverReplicatedTablets = false;
  }
  // Overrides for base class functionality to bypass calling CatalogManager.
  void GetAllLiveDescriptors(TSDescriptorVector* ts_descs) const override { *ts_descs = ts_descs_; }

  const TabletInfoMap& GetTabletMap() const override { return tablet_map_; }

  const TableInfoMap& GetTableMap() const override { return table_map_; }

  const scoped_refptr<TableInfo> GetTableInfo(const TableId& table_uuid) const override {
    return FindPtrOrNull(table_map_, table_uuid);
  }

  const PlacementInfoPB& GetClusterPlacementInfo() const override { return cluster_placement_; }

  const BlacklistPB& GetServerBlacklist() const override { return blacklist_; }

  void SendReplicaChanges(scoped_refptr<TabletInfo> tablet, const string& ts_uuid,
                          const bool is_add, const bool should_remove,
                          const string& new_leader_uuid) override {
    // Do nothing.
  }

  void GetPendingTasks(const TableId& table_uuid,
                       TabletToTabletServerMap* pending_add_replica_tasks,
                       TabletToTabletServerMap* pending_remove_replica_tasks,
                       TabletToTabletServerMap* pending_stepdown_leader_tasks) override {
    for (const auto& tablet_id : pending_add_replica_tasks_) {
      (*pending_add_replica_tasks)[tablet_id] = "";
    }
    for (const auto& tablet_id : pending_remove_replica_tasks_) {
      (*pending_remove_replica_tasks)[tablet_id] = "";
    }
    for (const auto& tablet_id : pending_stepdown_leader_tasks_) {
      (*pending_stepdown_leader_tasks)[tablet_id] = "";
    }
  }

  TSDescriptorVector ts_descs_;
  AffinitizedZonesSet affinitized_zones_;
  TabletInfoMap tablet_map_;
  TableInfoMap table_map_;
  PlacementInfoPB cluster_placement_;
  BlacklistPB blacklist_;
  vector<string> pending_add_replica_tasks_;
  vector<string> pending_remove_replica_tasks_;
  vector<string> pending_stepdown_leader_tasks_;
};

} // namespace master
} // namespace yb

#endif // YB_MASTER_CLUSTER_BALANCE_MOCKED_H
