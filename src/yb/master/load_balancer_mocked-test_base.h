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

DECLARE_bool(load_balancer_count_move_as_add);

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
        pending_stepdown_leader_tasks_(cb_.pending_stepdown_leader_tasks_) {
    scoped_refptr<TableInfo> table(new TableInfo(kTableId, /* colocated */ false));
    std::vector<scoped_refptr<TabletInfo>> tablets;

    // Generate 12 tablets total: 4 splits and 3 replicas each.
    std::vector<std::string> splits = {"a", "b", "c"};
    const int num_replicas = 3;
    total_num_tablets_ = narrow_cast<int>(num_replicas * (splits.size() + 1));

    CreateTable(splits, num_replicas, false, table.get(), &tablets);

    tablets_ = std::move(tablets);
    cur_table_uuid_ = kTableId;
    tables_.AddOrReplace(table);
  }

 protected:
  struct PendingTasks {
    int adds, removes, stepdowns;
  };

  // Don't need locks for mock class.
  Result<PendingTasks> ResetLoadBalancerAndAnalyzeTablets() NO_THREAD_SAFETY_ANALYSIS  {
    ResetLoadBalancerState();

    cb_.GetAllDescriptors(&cb_.global_state_->ts_descs_);
    cb_.SetBlacklistAndPendingDeleteTS();

    const auto& table = tables_.FindTableOrNull(cur_table_uuid_);
    const auto& replication_info = VERIFY_RESULT(cb_.GetTableReplicationInfo(table));
    RETURN_NOT_OK(cb_.PopulateReplicationInfo(table, replication_info));

    cb_.InitializeTSDescriptors();

    int adds = 0;
    int removes = 0;
    int stepdowns = 0;
    RETURN_NOT_OK(cb_.CountPendingTasksUnlocked(cur_table_uuid_, &adds, &removes, &stepdowns));

    RETURN_NOT_OK(cb_.AnalyzeTabletsUnlocked(cur_table_uuid_));
    return PendingTasks { adds, removes, stepdowns };
  }

  void ResetLoadBalancerState() {
    cb_.global_state_ = std::make_unique<GlobalLoadState>();
    cb_.ResetTableStatePtr(cur_table_uuid_, nullptr);
  }

  void StopTsHeartbeat(std::shared_ptr<TSDescriptor> ts_desc) {
    ts_desc->last_heartbeat_ = MonoTime();
  }

  void ResumeTsHeartbeat(std::shared_ptr<TSDescriptor> ts_desc) {
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

  Status CountPendingTasksUnlocked(
      const TableId& table_uuid, int* pending_add_replica_tasks, int* pending_remove_replica_tasks,
      int* pending_stepdown_leader_tasks)
      NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    return cb_.CountPendingTasksUnlocked(table_uuid, pending_add_replica_tasks,
        pending_remove_replica_tasks, pending_stepdown_leader_tasks);
  }

  Result<bool> HandleAddReplicas(
      TabletId* out_tablet_id, TabletServerId* out_from_ts, TabletServerId* out_to_ts)
      NO_THREAD_SAFETY_ANALYSIS /* disabling for controlled test */ {
    return cb_.HandleAddReplicas(out_tablet_id, out_from_ts, out_to_ts);
  }

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
    std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a");
    std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "a");
    std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "a");
    PrepareTestState({ts0, ts1, ts2});
  }

  void PrepareTestStateMultiAz() {
    std::shared_ptr<TSDescriptor> ts0 = SetupTS("0000", "a");
    std::shared_ptr<TSDescriptor> ts1 = SetupTS("1111", "b");
    std::shared_ptr<TSDescriptor> ts2 = SetupTS("2222", "c");
    PrepareTestState({ts0, ts1, ts2});
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

  void TestAddLoad(const std::string& expected_tablet_id,
                    const std::string& expected_from_ts,
                    const std::string& expected_to_ts,
                    std::string* actual_tablet_id = nullptr,
                    std::string* actual_from_ts = nullptr,
                    std::string* actual_to_ts = nullptr) NO_THREAD_SAFETY_ANALYSIS {
    std::string tablet_id, from_ts, to_ts;
    auto over_replication_at_start = cb_.get_total_over_replication();
    ASSERT_TRUE(ASSERT_RESULT(HandleAddReplicas(&tablet_id, &from_ts, &to_ts)));
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
      ASSERT_EQ(expected_tablet_id, tablet_id);
    }
    if (!expected_from_ts.empty()) {
      ASSERT_EQ(expected_from_ts, from_ts);
    }
    if (!expected_to_ts.empty()) {
      ASSERT_EQ(expected_to_ts, to_ts);
    }

    if (!from_ts.empty() && GetAtomicFlag(&FLAGS_load_balancer_count_move_as_add)) {
      ASSERT_EQ(1, cb_.get_total_over_replication() - over_replication_at_start);
      ASSERT_OK(cb_.RemoveReplica(tablet_id, from_ts));
    }
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

  const TableId kTableId;
  ClusterLoadBalancerMocked cb_;

  int total_num_tablets_;
  std::vector<scoped_refptr<TabletInfo>> tablets_;
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
