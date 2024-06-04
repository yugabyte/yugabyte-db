// Copyright (c) YugaByte, Inc.

#pragma once

#include "yb/master/cluster_balance.h"
#include "yb/master/cluster_balance_util.h"
#include "yb/master/master_fwd.h"
#include "yb/master/ts_manager.h"

namespace yb {
namespace master {

class ClusterLoadBalancerMocked : public ClusterLoadBalancer {
 public:
  explicit ClusterLoadBalancerMocked(const TableId& table_id) : ClusterLoadBalancer(nullptr)  {
    const int kHighNumber = 100;
    options_.kMaxConcurrentAdds = kHighNumber;
    options_.kMaxConcurrentRemovals = kHighNumber;
    options_.kAllowLimitStartingTablets = false;
    options_.kAllowLimitOverReplicatedTablets = false;

    auto table_state = std::make_unique<PerTableLoadState>(global_state_.get());
    table_state->options_ = &options_;
    state_ = table_state.get();
    per_table_states_[table_id] = std::move(table_state);
    ResetOptions();

    InitTablespaceManager();
  }

  // Overrides for base class functionality to bypass calling CatalogManager.
  void GetAllDescriptors(TSDescriptorVector* ts_descs) const override {
    *ts_descs = ts_descs_;
  }

  void GetAllAffinitizedZones(
      const ReplicationInfoPB& replication_info,
      std::vector<AffinitizedZonesSet>* affinitized_zones) const override {
    *affinitized_zones = affinitized_zones_;
  }

  const TabletInfoMap& GetTabletMap() const override { return tablet_map_; }

  TableIndex::TablesRange GetTables() const override {
    return tables_.GetPrimaryTables();
  }

  const scoped_refptr<TableInfo> GetTableInfo(const TableId& table_uuid) const override {
    return tables_.FindTableOrNull(table_uuid);
  }

  Result<ReplicationInfoPB> GetTableReplicationInfo(
      const scoped_refptr<const TableInfo>& table) const override {
    return replication_info_;
  }

  const PerTableLoadState* GetTableState(const TableId& table_id) {
    return per_table_states_[table_id].get();
  }

  void SetBlacklistAndPendingDeleteTS() override {
    for (const auto& ts_desc : global_state_->ts_descs_) {
      AddTSIfBlacklisted(ts_desc, blacklist_, false);
      AddTSIfBlacklisted(ts_desc, leader_blacklist_, true);
      global_state_->pending_deletes_[ts_desc->permanent_uuid()] =
          ts_desc->TabletsPendingDeletion();
    }
  }

  Status SendReplicaChanges(scoped_refptr<TabletInfo> tablet, const TabletServerId& ts_uuid,
                          const bool is_add, const bool should_remove,
                          const TabletServerId& new_leader_uuid) override {
    // Do nothing.
    return Status::OK();
  }

  void GetPendingTasks(const TableId& table_uuid,
                       TabletToTabletServerMap* pending_add_replica_tasks,
                       TabletToTabletServerMap* pending_remove_replica_tasks,
                       TabletToTabletServerMap* pending_stepdown_leader_tasks) override {
    *pending_add_replica_tasks = pending_add_replica_tasks_;
    *pending_remove_replica_tasks = pending_remove_replica_tasks_;
    *pending_stepdown_leader_tasks = pending_stepdown_leader_tasks_;
  }

  void ResetTableStatePtr(const TableId& table_id, Options* options) override {
    if (state_) {
      options = state_->options_;
    }
    auto table_state = std::make_unique<PerTableLoadState>(global_state_.get());
    table_state->options_ = options;
    table_state->check_ts_liveness_ = false;
    state_ = table_state.get();

    per_table_states_[table_id] = std::move(table_state);
  }

  void InitTablespaceManager() override {
    tablespace_manager_ = std::make_shared<YsqlTablespaceManager>(nullptr, nullptr);
  }

  void SetOptions(ReplicaType type, const std::string& placement_uuid) {
    state_->options_->type = type;
    state_->options_->placement_uuid = placement_uuid;
  }

  void ResetOptions() { SetOptions(LIVE, ""); }

  Options options_;
  TSDescriptorVector ts_descs_;
  std::vector<AffinitizedZonesSet> affinitized_zones_;
  TabletInfoMap tablet_map_;
  TableIndex tables_;
  ReplicationInfoPB replication_info_;
  BlacklistPB blacklist_;
  BlacklistPB leader_blacklist_;
  TabletToTabletServerMap pending_add_replica_tasks_;
  TabletToTabletServerMap pending_remove_replica_tasks_;
  TabletToTabletServerMap pending_stepdown_leader_tasks_;

  friend class TestLoadBalancerEnterprise;
};

} // namespace master
} // namespace yb
