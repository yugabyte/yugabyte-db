// Copyright (c) YugaByte, Inc.
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
//

#pragma once

#include <memory>

#include <gtest/gtest.h>

#include "yb/master/catalog_manager-test_base.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/cluster_balance_mocked.h"
#include "yb/master/cluster_balance_util.h"
#include "yb/master/ts_descriptor.h"

#include "yb/util/atomic.h"
#include "yb/util/status_log.h"
#include "yb/util/test_util.h"

DECLARE_bool(load_balancer_ignore_cloud_info_similarity);

namespace yb {
namespace master {

// This class allows for testing the behavior of single tablet moves (adding / removing tablets and
// leaders) without having to deal with the asynchronous nature of an actual cluster.
class LoadBalancerMockedBase : public YBTest {
 public:
  LoadBalancerMockedBase()
      : kTableId("my_table_id"),
        cb_(kTableId),
        blacklist_(cb_.blacklist_),
        leader_blacklist_(cb_.leader_blacklist_),
        ts_descs_(cb_.ts_descs_),
        affinitized_zones_(cb_.affinitized_zones_),
        tablet_map_(cb_.tablet_map_),
        tables_(cb_.tables_),
        replication_info_(cb_.replication_info_),
        pending_add_replica_tasks_(cb_.pending_add_replica_tasks_),
        pending_remove_replica_tasks_(cb_.pending_remove_replica_tasks_),
        pending_stepdown_leader_tasks_(cb_.pending_stepdown_leader_tasks_) {}

  void SetUp() override {
    YBTest::SetUp();
    scoped_refptr<TableInfo> table(new TableInfo(kTableId, /* colocated */ false));
    std::vector<TabletInfoPtr> tablets;

    // Generate 12 tablets total: 4 splits and 3 replicas each.
    std::vector<std::string> splits = {"a", "b", "c"};
    const int num_replicas = NumReplicas();
    total_num_tablets_ = narrow_cast<int>(num_replicas * (splits.size() + 1));

    ASSERT_OK(CreateTable(splits, num_replicas, false, table.get(), &tablets));

    tablets_ = std::move(tablets);
    cur_table_uuid_ = kTableId;
    tables_.AddOrReplace(table);
  }

 protected:
  virtual int NumReplicas() const { return 3; }

  struct PendingTasks {
    int adds, removes, stepdowns;
  };

  // Don't need locks for mock class.
  Result<PendingTasks> ResetLoadBalancerAndAnalyzeTablets() NO_THREAD_SAFETY_ANALYSIS  {
    ResetLoadBalancerState();

    cb_.GetAllDescriptors(&cb_.global_state_->ts_descs_);
    cb_.SetBlacklistAndPendingDeleteTS();

    const auto& table = tables_.FindTableOrNull(cur_table_uuid_);
    const auto replication_info = cb_.GetTableReplicationInfo(table);
    RETURN_NOT_OK(cb_.PopulateReplicationInfo(table, replication_info));

    cb_.InitializeTSDescriptors();

    int adds = 0;
    int removes = 0;
    int stepdowns = 0;
    RETURN_NOT_OK(cb_.CountPendingTasks(table, &adds, &removes, &stepdowns));

    RETURN_NOT_OK(cb_.AnalyzeTablets(table));
    return PendingTasks { adds, removes, stepdowns };
  }

  void ResetLoadBalancerState() {
    cb_.global_state_ = std::make_unique<GlobalLoadState>();
    cb_.ResetTableStatePtr(cur_table_uuid_, nullptr);
  }

  void StopTsHeartbeat(std::shared_ptr<TSDescriptor> ts_desc) {
    std::lock_guard l(ts_desc->mutex_);
    ts_desc->last_heartbeat_ = MonoTime();
  }

  void ResumeTsHeartbeat(std::shared_ptr<TSDescriptor> ts_desc) {
    std::lock_guard l(ts_desc->mutex_);
    ts_desc->last_heartbeat_ = MonoTime::Now();
  }

  void AddLeaderBlacklist(const ::std::string& host_uuid) {
    leader_blacklist_.add_hosts()->set_host(host_uuid);
  }

  void ClearLeaderBlacklist() {
    leader_blacklist_.Clear();
    cb_.global_state_->leader_blacklisted_servers_.clear();
  }

  size_t GetTotalOverreplication() {
    return cb_.get_total_over_replication();
  }

  size_t GetTotalStartingTablets() {
    return cb_.get_total_starting_tablets();
  }

  size_t GetTotalRunningTablets() {
    return cb_.get_total_running_tablets();
  }

  Result<bool> HandleOneAddIfMissingPlacement(TabletId& out_tablet_id, TabletServerId& out_to_ts)
      NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    // Only do one add at most. If we do an add then we'll call AddReplica which will update state.
    int remaining_adds = 1;
    bool task_added = false;
    cb_.ProcessUnderReplicatedTablets(remaining_adds, task_added, out_tablet_id, out_to_ts);

    SCHECK(cb_.global_state_->activity_info_.GetWarningsSummary().empty(), IllegalState,
        "ProcessUnderReplicatedTablets hit an error");
    SCHECK_NE(remaining_adds, task_added, IllegalState, "task_added and remaining_adds mismatch");
    return task_added;
  }

  // cb_.HandleAddReplicas modifies state to account for the corresponding add.
  Result<bool> HandleAddReplicas(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts)
      NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    return cb_.HandleAddReplicas(out_tablet_id, out_from_ts, out_to_ts);
  }

  // cb_.HandleRemoveReplicas modifies state to account for the corresponding add.
  Result<bool> HandleRemoveReplicas(TabletId* out_tablet_id, TabletServerId* out_from_ts)
      NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    return cb_.HandleRemoveReplicas(out_tablet_id, out_from_ts);
  }

  Result<bool> HandleLeaderMoves(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts)
      NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    return cb_.HandleLeaderMoves(out_tablet_id, out_from_ts, out_to_ts);
  }

  void PrepareTestStateSingleAz() {
    std::vector<std::shared_ptr<TSDescriptor>> ts_descs;
    for (int i = 0; i < NumReplicas(); ++i) {
      // create TS with uuid 0000, 1111, etc and AZ a.
      ts_descs.push_back(SetupTS(std::string(4, '0' + i), "a"));
    }
    PrepareTestState(ts_descs);
  }

  void PrepareTestStateMultiAz() {
    std::vector<std::shared_ptr<TSDescriptor>> ts_descs;
    for (int i = 0; i < NumReplicas(); ++i) {
      // create TS with uuid 0000, 1111, etc and AZ a, b, etc.
      ts_descs.push_back(SetupTS(std::string(4, '0' + i), std::string(1, 'a' + i)));
    }
    PrepareTestState(ts_descs);
  }

  // Methods to prepare the state of the current test.
  void PrepareTestState(const TSDescriptorVector& ts_descs) {
    // Clear old state.
    ResetLoadBalancerState();
    replication_info_.Clear();
    blacklist_.Clear();
    ClearLeaderBlacklist();
    tablet_map_.clear();
    TSDescriptorVector old_ts_descs;
    old_ts_descs.swap(ts_descs_);
    affinitized_zones_.clear();

    // Set TS desc.
    ts_descs_ = ts_descs;

    // Reset the tablet map tablets.
    for (const auto& tablet : tablets_) {
      tablet_map_[tablet->tablet_id()] = tablet;
    }

    // Prepare the replicas.
    tablet::RaftGroupStatePB state = tablet::RUNNING;
    for (size_t i = 0; i < tablets_.size(); ++i) {
      auto replica_map = std::make_shared<TabletReplicaMap>();
      for (size_t j = 0; j < ts_descs_.size(); ++j) {
        TabletReplica replica;
        auto ts_desc = ts_descs_[j];
        bool is_leader = i % ts_descs_.size() == j;
        PeerRole role = is_leader ? PeerRole::LEADER : PeerRole::FOLLOWER;
        NewReplica(ts_desc.get(), state, role, &replica);
        InsertOrDie(replica_map.get(), ts_desc->permanent_uuid(), replica);
      }
      // Set the replica locations directly into the tablet map.
      tablet_map_[tablets_[i]->tablet_id()]->SetReplicaLocations(replica_map);
    }
  }

  // Tester methods that actually do the calls and asserts.
  void TestRemoveLoad(const std::string& expected_tablet_id, const std::string& expected_from_ts)
    NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    std::string tablet_id, from_ts;
    ASSERT_TRUE(ASSERT_RESULT(cb_.HandleRemoveReplicas(&tablet_id, &from_ts)));
    if (!expected_tablet_id.empty()) {
      ASSERT_EQ(expected_tablet_id, tablet_id);
    }
    if (!expected_from_ts.empty()) {
      ASSERT_EQ(expected_from_ts, from_ts);
    }
  }

  Status TestAddLoad(const std::string& expected_tablet_id,
                    const std::string& expected_from_ts,
                    const std::string& expected_to_ts,
                    std::string* actual_tablet_id = nullptr,
                    std::string* actual_from_ts = nullptr,
                    std::string* actual_to_ts = nullptr) NO_THREAD_SAFETY_ANALYSIS {
    std::string tablet_id, from_ts, to_ts;
    auto over_replication_at_start = cb_.get_total_over_replication();
    // First try to handle missing placement, otherwise handle regular adds.
    if (!VERIFY_RESULT(HandleOneAddIfMissingPlacement(tablet_id, to_ts))) {
      SCHECK_EQ(VERIFY_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)), true, IllegalState,
                "Failed to add replica");
    }
    if (actual_tablet_id) {
      *actual_tablet_id = tablet_id;
    }
    if (actual_from_ts) {
      *actual_from_ts = from_ts;
    }
    if (actual_to_ts) {
      *actual_to_ts = to_ts;
    }

    if (!expected_tablet_id.empty()) {
      SCHECK_EQ(expected_tablet_id, tablet_id, IllegalState, "Tablet id mismatch");
    }
    if (!expected_from_ts.empty()) {
      SCHECK_EQ(expected_from_ts, from_ts, IllegalState, "From ts mismatch");
    }
    if (!expected_to_ts.empty()) {
      SCHECK_EQ(expected_to_ts, to_ts, IllegalState, "To ts mismatch");
    }

    if (!from_ts.empty()) {
      SCHECK_EQ(1, cb_.get_total_over_replication() - over_replication_at_start, IllegalState,
                "Overreplication count mismatch");
      RETURN_NOT_OK(cb_.RemoveReplica(tablet_id, from_ts, "Remove replica for move"));
    }
    return Status::OK();
  }

  void TestMoveLeader(std::string* tablet_id,
                      const std::string& expected_from_ts,
                      const std::string& expected_to_ts) {
    std::string from_ts, to_ts;
    ASSERT_TRUE(ASSERT_RESULT(HandleLeaderMoves(tablet_id, &from_ts, &to_ts)));
    if (!expected_from_ts.empty()) {
      ASSERT_EQ(expected_from_ts, from_ts);
    }
    if (!expected_to_ts.empty()) {
      ASSERT_EQ(expected_to_ts, to_ts);
    }
  }
  void AddRunningReplica(TabletInfo* tablet, std::shared_ptr<TSDescriptor> ts_desc,
                         bool is_live = true) {
    std::shared_ptr<TabletReplicaMap> replicas =
      std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());

    TabletReplica replica;
    NewReplica(ts_desc.get(), tablet::RaftGroupStatePB::RUNNING, PeerRole::FOLLOWER, &replica);
    InsertOrDie(replicas.get(), ts_desc->permanent_uuid(), replica);
    tablet->SetReplicaLocations(replicas);
  }

  void RemoveReplica(TabletInfo* tablet, std::shared_ptr<TSDescriptor> ts_desc) {
    std::shared_ptr<TabletReplicaMap> replicas =
      std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());
    ASSERT_TRUE(replicas->erase(ts_desc->permanent_uuid()));
    tablet->SetReplicaLocations(replicas);
  }

  void MoveTabletLeader(TabletInfo* tablet, std::shared_ptr<TSDescriptor> ts_desc) {
    std::shared_ptr<TabletReplicaMap> replicas =
      std::const_pointer_cast<TabletReplicaMap>(tablet->GetReplicaLocations());
    for (auto& replica : *replicas) {
      if (replica.second.ts_desc->permanent_uuid() == ts_desc->permanent_uuid()) {
        replica.second.role = PeerRole::LEADER;
      } else {
        replica.second.role = PeerRole::FOLLOWER;
      }
    }
    tablet->SetReplicaLocations(replicas);
  }

  // Clear the tablets_added_ field from the state, used for testing.
  void ClearTabletsAddedForTest() {
    cb_.state_->tablets_added_.clear();
  }

  Options* GetOptions() {
    return cb_.state_->options_;
  }

  const TableId kTableId;
  ClusterLoadBalancerMocked cb_;

  int total_num_tablets_;
  std::vector<TabletInfoPtr> tablets_;
  BlacklistPB& blacklist_;
  BlacklistPB& leader_blacklist_;
  TableId cur_table_uuid_;
  TSDescriptorVector& ts_descs_;
  std::vector<AffinitizedZonesSet>& affinitized_zones_;
  TabletInfoMap& tablet_map_;
  TableIndex& tables_;
  ReplicationInfoPB& replication_info_;
  TabletToTabletServerMap& pending_add_replica_tasks_;
  TabletToTabletServerMap& pending_remove_replica_tasks_;
  TabletToTabletServerMap& pending_stepdown_leader_tasks_;
};

} // namespace master
} // namespace yb
