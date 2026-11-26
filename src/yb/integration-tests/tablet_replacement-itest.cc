// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

#include "yb/common/wire_protocol-test-util.h"
#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"

using yb::consensus::PeerMemberType;
using yb::itest::TServerDetails;
using yb::tablet::TABLET_DATA_READY;
using yb::tablet::TABLET_DATA_TOMBSTONED;
using yb::tserver::ListTabletsResponsePB;
using std::string;
using std::vector;

namespace yb {

constexpr auto kTimeout = 30s * kTimeMultiplier;
constexpr auto kHeartbeatInterval = 1s;

class TabletReplacementITest : public ExternalMiniClusterITestBase {
 protected:
  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    ExternalMiniClusterITestBase::SetUpOptions(opts);
    opts.extra_tserver_flags.push_back(
        Format("--heartbeat_interval_ms=$0", ToMilliseconds(kHeartbeatInterval)));
  }

  Status CreateTestTablet() {
    return itest::CreateTestTablet(*client_);
  }

  void AllowTimeForSpuriousDelete() {
    // Give master enough time to have a chance to delete replica in case of incorrect condition.
    SleepFor(5 * kHeartbeatInterval);
  }

  Result<TabletId> WaitForAndGetSingleTabletId(TServerDetails* leader_ts_details) {
    std::vector<ListTabletsResponsePB::StatusAndSchemaPB> tablets;
    RETURN_NOT_OK(
        itest::WaitForNumTabletsOnTS(leader_ts_details, /* count = */ 1, kTimeout, &tablets));
    return tablets[0].tablet_status().tablet_id();
  }

  Status ElectAsLeader(TServerDetails* leader_ts_details, const TabletId tablet_id) {
    int64_t actual_index;
    RETURN_NOT_OK(
        itest::WaitForServersToAgree(
            kTimeout, ts_map_, tablet_id, /* minimum_index = */ 0, &actual_index));
    RETURN_NOT_OK(itest::StartElection(leader_ts_details, tablet_id, kTimeout));
    // Wait for NO_OP to be added and committed.
    return itest::WaitForServersToAgree(
        kTimeout, ts_map_, tablet_id, /* minimum_index = */ actual_index + 1,
        /* actual_index = */ nullptr, itest::MustBeCommitted::kTrue);
  }

  Result<TabletId> ElectAsLeaderAndGetSingleTabletId(TServerDetails* leader_ts_details) {
    const auto tablet_id = VERIFY_RESULT(WaitForAndGetSingleTabletId(leader_ts_details));
    RETURN_NOT_OK(
        itest::WaitUntilAllTabletReplicasRunning(
            itest::TServerDetailsVector(ts_map_), tablet_id, kTimeout));
    RETURN_NOT_OK(ElectAsLeader(leader_ts_details, tablet_id));
    return tablet_id;
  }

  void TestTombstoneEvictedReplicaWithRbsAndConfigChangeRace(bool do_master_restart) {
    std::vector<std::string> ts_flags = {
        "--enable_leader_failure_detection=false", "--committed_config_change_role_timeout_sec=1"};
    std::vector<std::string> master_flags = {
        "--enable_load_balancing=false",
        "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
        "--use_create_table_leader_hint=false",
    };
    ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

    ASSERT_OK(CreateTestTablet());

    const int kLeaderIndex = 0;
    auto* leader_ts = cluster_->tablet_server(kLeaderIndex);
    auto* leader_ts_details = ts_map_[leader_ts->uuid()].get();

    const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts_details));

    // Add new server.
    ASSERT_OK(cluster_->AddTabletServer());
    ASSERT_OK(cluster_->WaitForTabletServerCount(4, kTimeout));
    ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
    leader_ts_details = ts_map_[leader_ts->uuid()].get();

    const auto kFollowerIndex = 3;
    auto* follower_ts = cluster_->tablet_server(kFollowerIndex);
    auto* follower_ts_details = ts_map_[follower_ts->uuid()].get();
    LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << follower_ts->uuid();
    ASSERT_OK(cluster_->SetFlag(
        cluster_->tablet_server(kFollowerIndex), "TEST_pause_rbs_before_download_wal", "true"));
    LogWaiter log_waiter(
        follower_ts, tablet_id + ": Pausing due to flag TEST_pause_rbs_before_download_wal");
    ASSERT_OK(itest::AddServer(
        leader_ts_details, tablet_id, follower_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
        kTimeout));
    ASSERT_OK(log_waiter.WaitFor(kTimeout));

    // Wait for CHANGE_CONFIG_OP to be applied.
    ASSERT_OK(itest::WaitUntilCommittedConfigMemberTypeIs(
        /* config_size = */ 1, leader_ts_details, tablet_id, kTimeout, PeerMemberType::PRE_VOTER));

    ASSERT_OK(itest::RemoveServer(
        leader_ts_details, tablet_id, follower_ts_details, std::nullopt, kTimeout));

    if (do_master_restart) {
      LOG(INFO) << "Restarting master";
      cluster_->master()->Shutdown();
      ASSERT_OK(cluster_->master()->Restart());
      LOG(INFO) << "Restarted master";
    }

    ASSERT_OK(cluster_->SetFlag(
        cluster_->tablet_server(kFollowerIndex), "TEST_pause_rbs_before_download_wal", "false"));

    ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
        kFollowerIndex, tablet_id, tablet::TABLET_DATA_TOMBSTONED, kTimeout));
  }
};

// Test that the Master will tombstone a newly-evicted replica.
// Then, test that the Master will NOT tombstone a newly-added replica that is
// not part of the committed config yet (only the pending config).
TEST_F(TabletReplacementITest, TestMasterTombstoneEvictedReplica) {
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  int num_tservers = 5;
  vector<string> master_flags = {
    "--catalog_manager_wait_for_new_tablets_to_elect_leader=false"s,
    "--replication_factor=5",
    "--use_create_table_leader_hint=false"s,
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, num_tservers));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  TServerDetails* leader_ts = ts_map_[cluster_->tablet_server(kLeaderIndex)->uuid()].get();
  const int kFollowerIndex = 4;
  TServerDetails* follower_ts = ts_map_[cluster_->tablet_server(kFollowerIndex)->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts));

  // Remove a follower from the config.
  ASSERT_OK(itest::RemoveServer(leader_ts, tablet_id, follower_ts, std::nullopt, kTimeout));

  // Wait for the Master to tombstone the replica.
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kFollowerIndex, tablet_id, TABLET_DATA_TOMBSTONED, kTimeout));

  // Shut down a majority of followers (3 servers) and then try to add the
  // follower back to the config. This will cause the config change to end up
  // in a pending state.
  auto active_ts_map = CreateTabletServerMapUnowned(ts_map_);
  for (int i = 1; i <= 3; i++) {
    cluster_->tablet_server(i)->Shutdown();
    ASSERT_EQ(1, active_ts_map.erase(cluster_->tablet_server(i)->uuid()));
  }
  // This will time out, but should take effect.
  Status s = itest::AddServer(
      leader_ts, tablet_id, follower_ts, PeerMemberType::PRE_VOTER, std::nullopt,
      MonoDelta::FromSeconds(5), NULL, false /* retry */);
  ASSERT_TRUE(s.IsTimedOut());
  ASSERT_OK(
      inspect_->WaitForTabletDataStateOnTS(kFollowerIndex, tablet_id, TABLET_DATA_READY, kTimeout));
  ASSERT_OK(itest::WaitForServersToAgree(kTimeout, active_ts_map, tablet_id, 3));

  // Sleep for a few more seconds and check again to ensure that the Master
  // didn't end up tombstoning the replica.
  SleepFor(MonoDelta::FromSeconds(3));
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kFollowerIndex, tablet_id, TABLET_DATA_READY));
}

TEST_F(TabletReplacementITest, TombstoneEvictedReplicaWithRbsAndConfigChangeRace) {
  TestTombstoneEvictedReplicaWithRbsAndConfigChangeRace(/* do_restart_master = */ false);
}

TEST_F(TabletReplacementITest, TombstoneEvictedReplicaWithRbsAndConfigChangeRaceAndMasterRestart) {
  TestTombstoneEvictedReplicaWithRbsAndConfigChangeRace(/* do_restart_master = */ true);
}

TEST_F(TabletReplacementITest, TombstoneEvictedReplicaAfterAbortedAddServer) {
  std::vector<std::string> ts_flags = {"--enable_leader_failure_detection=false"};
  std::vector<std::string> master_flags = {
      "--enable_load_balancing=false",
      "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
      "--use_create_table_leader_hint=false",
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  ASSERT_OK(CreateTestTablet());

  int leader_index = 0;
  auto* leader_ts = cluster_->tablet_server(leader_index);
  auto* leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts_details));

  // Add new server.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(4, kTimeout));
  ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto kNewFollowerIndex = 3;
  auto* new_follower_ts = cluster_->tablet_server(kNewFollowerIndex);
  auto* new_follower_ts_details = ts_map_[new_follower_ts->uuid()].get();
  LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << new_follower_ts->uuid();

  std::vector<ExternalTabletServer*> old_followers = {
      cluster_->tablet_server(1), cluster_->tablet_server(2)};
  // Pause old followers so ADD_SERVER cannot replicate to majority.
  for (auto* follower_ts : old_followers) {
    ASSERT_OK(
        cluster_->SetFlag(follower_ts, "TEST_follower_pause_update_consensus_requests", "true"));
  }

  // Timeout is expected because config change can't be committed without a majority.
  ASSERT_NOK(
      itest::AddServer(
          leader_ts_details, tablet_id, new_follower_ts_details, PeerMemberType::PRE_VOTER,
          std::nullopt, 500ms));
  ASSERT_OK(itest::WaitUntilTabletRunning(new_follower_ts_details, tablet_id, kTimeout));

  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        consensus::ConsensusStatePB consensus_state;
        RETURN_NOT_OK(itest::GetConsensusState(
            new_follower_ts_details, tablet_id, consensus::CONSENSUS_CONFIG_ACTIVE, kTimeout,
            &consensus_state));
        return consensus_state.config().peers_size() == 4;
      },
      kTimeout,
      "Waiting for new follower to observe 4-peer Raft config"));

  // Change leader while ADD_SERVER is still pending so new leader aborts it.
  leader_index = 1;
  leader_ts = cluster_->tablet_server(leader_index);
  leader_ts_details = ts_map_[leader_ts->uuid()].get();
  ASSERT_OK(itest::StartElection(leader_ts_details, tablet_id, kTimeout));
  ASSERT_OK(itest::WaitUntilLeader(leader_ts_details, tablet_id, kTimeout));

  LOG(INFO) << "Leader changed to: " << leader_ts->uuid();

  for (auto* follower_ts : old_followers) {
    ASSERT_OK(
        cluster_->SetFlag(follower_ts, "TEST_follower_pause_update_consensus_requests", "false"));
  }

  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kNewFollowerIndex, tablet_id, tablet::TABLET_DATA_TOMBSTONED, kTimeout));
}

// A replica that was remote bootstrapped while its ADD_SERVER was pending is removed in the same
// term before it gets an update from the leader. Master must still tombstone it.
TEST_F(TabletReplacementITest, TombstoneReplicaRemovedAfterPendingAddServerCommitted) {
  // The tserver reports the replica only while RBS waits for its promotion, so the wait is long.
  std::vector<std::string> ts_flags = {
      "--enable_leader_failure_detection=false",
      "--committed_config_change_role_timeout_sec=3600"};
  std::vector<std::string> master_flags = {
      "--enable_load_balancing=false",
      "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
      "--use_create_table_leader_hint=false",
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));
  const auto set_frequent_heartbeats = [this](size_t ts_index) {
    return cluster_->SetFlag(cluster_->tablet_server(ts_index), "heartbeat_interval_ms", "100");
  };
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    ASSERT_OK(set_frequent_heartbeats(i));
  }

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  auto* leader_ts = cluster_->tablet_server(kLeaderIndex);
  auto* leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts_details));

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(4, kTimeout));
  ASSERT_OK(set_frequent_heartbeats(3));
  ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto kAddedTsIndex = 3;
  auto* added_ts = cluster_->tablet_server(kAddedTsIndex);
  auto* added_ts_details = ts_map_[added_ts->uuid()].get();

  std::vector<ExternalTabletServer*> old_followers = {
      cluster_->tablet_server(1), cluster_->tablet_server(2)};

  // Pause old followers so ADD_SERVER stays pending while the new replica is remote bootstrapped.
  for (auto* follower_ts : old_followers) {
    ASSERT_OK(
        cluster_->SetFlag(follower_ts, "TEST_follower_pause_update_consensus_requests", "true"));
  }
  // Without updates from the leader the new replica keeps the pending config op id from RBS.
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_follower_pause_update_consensus_requests", "true"));

  LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << added_ts->uuid();
  // Timeout is expected because config change can't be committed without a majority.
  ASSERT_NOK(itest::AddServer(
      leader_ts_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
      500ms));
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  ASSERT_OK(itest::WaitUntilTabletRunning(added_ts_details, tablet_id, kTimeout));
  LOG(INFO) << "RBS has been completed on added node";

  // Master must not see the committed config that has the new replica. Otherwise it deletes the
  // replica as a peer of the previous config, and ShouldDeleteNonMember is not exercised.
  const std::vector<ExternalTabletServer*> old_peers = {
      leader_ts, cluster_->tablet_server(1), cluster_->tablet_server(2)};
  for (auto* ts : old_peers) {
    ASSERT_OK(cluster_->SetFlag(ts, "TEST_tserver_disable_heartbeat", "true"));
  }

  for (auto* follower_ts : old_followers) {
    ASSERT_OK(
        cluster_->SetFlag(follower_ts, "TEST_follower_pause_update_consensus_requests", "false"));
  }
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        consensus::ConsensusStatePB consensus_state;
        RETURN_NOT_OK(itest::GetConsensusState(
            leader_ts_details, tablet_id, consensus::CONSENSUS_CONFIG_COMMITTED, kTimeout,
            &consensus_state));
        return consensus_state.config().peers_size() == 4;
      },
      kTimeout, "Waiting for ADD_SERVER to be committed"));

  LOG(INFO) << "Remove tablet " << tablet_id << " from tserver " << added_ts->uuid();
  ASSERT_OK(itest::RemoveServer(
      leader_ts_details, tablet_id, added_ts_details, std::nullopt, kTimeout));
  // A peer that has not applied REMOVE_SERVER yet could still report the config with the replica.
  for (auto* ts : old_peers) {
    ASSERT_OK(LoggedWaitFor(
        [&]() -> Result<bool> {
          consensus::ConsensusStatePB leader_state;
          RETURN_NOT_OK(itest::GetConsensusState(
              leader_ts_details, tablet_id, consensus::CONSENSUS_CONFIG_COMMITTED, kTimeout,
              &leader_state));
          consensus::ConsensusStatePB peer_state;
          RETURN_NOT_OK(itest::GetConsensusState(
              ts_map_[ts->uuid()].get(), tablet_id, consensus::CONSENSUS_CONFIG_COMMITTED,
              kTimeout, &peer_state));
          return leader_state.config().peers_size() == 3 &&
                 peer_state.config().committed_op_index() ==
                     leader_state.config().committed_op_index();
        },
        kTimeout, Format("Waiting for REMOVE_SERVER to be applied on $0", ts->uuid())));
  }

  // The tserver logs rejected DeleteTablet requests too, so this shows the master's decision.
  LogWaiter delete_tablet_waiter(
      added_ts,
      Format(
          "T $0 P $1: Processing DeleteTablet with delete_type TABLET_DATA_TOMBSTONED", tablet_id,
          added_ts->uuid()));
  for (auto* ts : old_peers) {
    ASSERT_OK(cluster_->SetFlag(ts, "TEST_tserver_disable_heartbeat", "false"));
  }
  ASSERT_OK(delete_tablet_waiter.WaitFor(kTimeout));

  // End the wait for the promotion, so the tserver accepts the retried DeleteTablet.
  ASSERT_OK(cluster_->SetFlag(added_ts, "committed_config_change_role_timeout_sec", "0"));
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_TOMBSTONED, kTimeout));
}

TEST_F(TabletReplacementITest, DontDeleteNewReplicaInPendingConfig) {
  std::vector<std::string> ts_flags = {
      "--enable_leader_failure_detection=false", "--committed_config_change_role_timeout_sec=1"};
  std::vector<std::string> master_flags = {
      "--enable_load_balancing=false",
      "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
      "--use_create_table_leader_hint=false",
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  auto* leader_ts = cluster_->tablet_server(kLeaderIndex);
  auto* leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts_details));

  // Add new server.
  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(4, kTimeout));
  ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));

  leader_ts_details = ts_map_[leader_ts->uuid()].get();
  auto active_ts_map = CreateTabletServerMapUnowned(ts_map_);

  const auto kAddedTsIndex = cluster_->num_tablet_servers() - 1;
  auto* added_ts = cluster_->tablet_server(kAddedTsIndex);
  auto* added_ts_details = ts_map_[added_ts->uuid()].get();

  // Turn off part of followers so we don't have a majority and CHANGE_CONFIG remains pending for
  // now.
  for (int i = 1; i <= 2; i++) {
    cluster_->tablet_server(i)->Shutdown();
    ASSERT_EQ(1, active_ts_map.erase(cluster_->tablet_server(i)->uuid()));
  }

  // Ensure RBS is paused so we can reliably turn off next RBS attempts then.
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_pause_rbs_before_download_wal", "true"));
  LogWaiter log_waiter(
      added_ts, tablet_id + ": Pausing due to flag TEST_pause_rbs_before_download_wal");

  LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << added_ts->uuid();

  // Expect timeout since CHANGE_CONFIG apply is artificially delayed.
  ASSERT_NOK(itest::AddServer(
      leader_ts_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
      500ms));

  // Wait for RBS to start.
  ASSERT_OK(log_waiter.WaitFor(kTimeout));

  // Disable next RBS attempts.
  ASSERT_OK(cluster_->SetFlag(leader_ts, "TEST_enable_remote_bootstrap", "false"));

  // Unpause RBS.
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_pause_rbs_before_download_wal", "false"));

  LOG(INFO) << "Waiting for RBS to be completed on added node...";
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  LOG(INFO) << "RBS has been completed on added node";

  AllowTimeForSpuriousDelete();

  LOG(INFO) << "Make sure new replica doesn't get deleted";
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kAddedTsIndex, tablet_id, TABLET_DATA_READY));

  for (int i = 1; i <= 2; i++) {
    ASSERT_OK(cluster_->tablet_server(i)->Restart());
  }
  active_ts_map = CreateTabletServerMapUnowned(ts_map_);

  AllowTimeForSpuriousDelete();
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kAddedTsIndex, tablet_id, TABLET_DATA_READY));
}

TEST_F(TabletReplacementITest, DontDeleteNewReplicaInPendingConfigAfterRbsFromFollowerRf5) {
  constexpr auto kInitialNumTservers = 5;
  std::vector<std::string> ts_flags = {
      "--enable_leader_failure_detection=false", "--committed_config_change_role_timeout_sec=1",
      "--placement_zone=z${index}"};
  std::vector<std::string> master_flags = {
      "--replication_factor=5",
      "--enable_load_balancing=false",
      "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
      "--use_create_table_leader_hint=false",
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kInitialNumTservers));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  auto* leader_ts = cluster_->tablet_server(kLeaderIndex);
  auto* leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts_details));

  // Add new server.
  ASSERT_OK(cluster_->AddTabletServer(true, {"--placement_zone=z4"}));
  ASSERT_OK(cluster_->WaitForTabletServerCount(6, kTimeout));
  ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  leader_ts_details = ts_map_[leader_ts->uuid()].get();
  auto active_ts_map = CreateTabletServerMapUnowned(ts_map_);

  const auto kAddedTsIndex = cluster_->num_tablet_servers() - 1;
  auto* added_ts = cluster_->tablet_server(kAddedTsIndex);
  auto* added_ts_details = ts_map_[added_ts->uuid()].get();

  LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << added_ts->uuid();
  ASSERT_OK(itest::AddServer(
      leader_ts_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
      kTimeout));

  LOG(INFO) << "Waiting for RBS to be completed on added node...";
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  LOG(INFO) << "RBS has been completed on added node";

  // Wait for 1st CONFIG_CHANGE ABCDE->ABCDEF to be committed on all nodes.
  ASSERT_OK(itest::WaitForServersToAgree(
      kTimeout, ts_map_, tablet_id, 1, /* actual_index = */ nullptr,
      itest::MustBeCommitted::kTrue));

  const auto kRbsSourceFollowerTsIndex = 4;
  // Ensure RBS source follower won't have next CONFIG_CHANGE operations in Raft log.
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(kRbsSourceFollowerTsIndex),
      "TEST_follower_pause_update_consensus_requests", "true"));

  LOG(INFO) << "Remove tablet " << tablet_id << " from tserver " << added_ts->uuid();
  ASSERT_OK(itest::RemoveServer(
      leader_ts_details, tablet_id, added_ts_details, std::nullopt, kTimeout));
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_TOMBSTONED, kTimeout));
  LOG(INFO) << "Tablet replica has been removed from added node";

  // Turn off part of followers so we don't have a majority and 2nd CHANGE_CONFIG ABCDE->ABCDEF
  // remains pending for now.
  for (int i = 1; i <= 3; i++) {
    cluster_->tablet_server(i)->Shutdown();
    ASSERT_EQ(1, active_ts_map.erase(cluster_->tablet_server(i)->uuid()));
  }

  // Ensure RBS is paused so we can reliably turn off next RBS attempts then.
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_pause_rbs_before_download_wal", "true"));
  LogWaiter log_waiter(
      added_ts, tablet_id + ": Pausing due to flag TEST_pause_rbs_before_download_wal");

  LOG(INFO) << "Adding tablet " << tablet_id << " back to tserver " << added_ts->uuid();
  // Expected timeout due to CHANGE_CONFIG can't commit yet.
  ASSERT_NOK(itest::AddServer(
      leader_ts_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
      500ms));

  // Wait for RBS to start.
  ASSERT_OK(log_waiter.WaitFor(kTimeout));

  // Disable next RBS attempts.
  ASSERT_OK(cluster_->SetFlag(leader_ts, "TEST_enable_remote_bootstrap", "false"));

  // Unpause RBS.
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_pause_rbs_before_download_wal", "false"));

  LOG(INFO) << "Waiting for RBS to be completed on added node...";
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  LOG(INFO) << "RBS has been completed on added node";

  AllowTimeForSpuriousDelete();

  LOG(INFO) << "Make sure new replica doesn't get deleted";
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kAddedTsIndex, tablet_id, TABLET_DATA_READY));

  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    auto* ts = cluster_->tablet_server(i);
    if (ts->IsShutdown()) {
      ASSERT_OK(cluster_->tablet_server(i)->Restart());
    }
  }
  active_ts_map = CreateTabletServerMapUnowned(ts_map_);

  AllowTimeForSpuriousDelete();
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kAddedTsIndex, tablet_id, TABLET_DATA_READY));

  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(kRbsSourceFollowerTsIndex),
      "TEST_follower_pause_update_consensus_requests", "false"));
}

// A replica is remote bootstrapped from a follower whose Raft log has a config change op that the
// follower doesn't know is committed, while the leader has a newer pending ADD_SERVER for the
// replica. The replica reports the follower's op as its pending config op, which hides the pending
// ADD_SERVER the leader sent with the RBS request. Master must not delete the replica.
TEST_F(TabletReplacementITest, DontDeleteNewReplicaAfterRbsFromFollowerWithUncommittedConfigOp) {
  constexpr auto kInitialNumTservers = 5;
  std::vector<std::string> ts_flags = {
      "--enable_leader_failure_detection=false", "--committed_config_change_role_timeout_sec=1",
      "--placement_zone=z${index}"};
  std::vector<std::string> master_flags = {
      "--replication_factor=5",
      "--enable_load_balancing=false",
      "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
      "--use_create_table_leader_hint=false",
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kInitialNumTservers));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  auto* leader_ts = cluster_->tablet_server(kLeaderIndex);
  auto* leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts_details));

  ASSERT_OK(cluster_->AddTabletServer(true, {"--placement_zone=z4"}));
  ASSERT_OK(cluster_->WaitForTabletServerCount(6, kTimeout));
  ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  leader_ts_details = ts_map_[leader_ts->uuid()].get();

  const auto kAddedTsIndex = cluster_->num_tablet_servers() - 1;
  auto* added_ts = cluster_->tablet_server(kAddedTsIndex);
  auto* added_ts_details = ts_map_[added_ts->uuid()].get();

  LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << added_ts->uuid();
  ASSERT_OK(itest::AddServer(
      leader_ts_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
      kTimeout));
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  ASSERT_OK(itest::WaitForServersToAgree(
      kTimeout, ts_map_, tablet_id, 1, /* actual_index = */ nullptr,
      itest::MustBeCommitted::kTrue));

  // The follower in the same zone as the added tserver is the RBS source.
  const auto kRbsSourceFollowerTsIndex = 4;
  auto* rbs_source_ts = cluster_->tablet_server(kRbsSourceFollowerTsIndex);
  auto* rbs_source_ts_details = ts_map_[rbs_source_ts->uuid()].get();

  // The RBS source follower holds the request with REMOVE_SERVER after appending the op and before
  // marking ops committed. The request was sent before the op was committed, and later requests are
  // paused, so the follower doesn't learn that REMOVE_SERVER is committed.
  ASSERT_OK(cluster_->SetFlag(
      rbs_source_ts, "TEST_delay_update_consensus_before_mark_committed_tablet_id", tablet_id));
  ASSERT_OK(cluster_->SetFlag(
      rbs_source_ts, "TEST_delay_update_consensus_before_mark_committed_ms",
      ToString(10000 * kTimeMultiplier)));

  LOG(INFO) << "Remove tablet " << tablet_id << " from tserver " << added_ts->uuid();
  ASSERT_OK(itest::RemoveServer(
      leader_ts_details, tablet_id, added_ts_details, std::nullopt, kTimeout));
  ASSERT_OK(cluster_->SetFlag(
      rbs_source_ts, "TEST_follower_pause_update_consensus_requests", "true"));
  ASSERT_OK(cluster_->SetFlag(
      rbs_source_ts, "TEST_delay_update_consensus_before_mark_committed_ms", "0"));
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_TOMBSTONED, kTimeout));
  LOG(INFO) << "Tablet replica has been removed from added node";

  consensus::ConsensusStatePB leader_state;
  ASSERT_OK(itest::GetConsensusState(
      leader_ts_details, tablet_id, consensus::CONSENSUS_CONFIG_COMMITTED, kTimeout,
      &leader_state));
  const auto remove_server_index = leader_state.config().committed_op_index();
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        const auto received = VERIFY_RESULT(itest::GetLastOpIdForReplica(
            tablet_id, rbs_source_ts_details, consensus::RECEIVED_OPID, kTimeout));
        return received.index >= remove_server_index;
      },
      kTimeout, "Waiting for the RBS source follower to receive REMOVE_SERVER"));
  const auto rbs_source_committed = ASSERT_RESULT(itest::GetLastOpIdForReplica(
      tablet_id, rbs_source_ts_details, consensus::COMMITTED_OPID, kTimeout));
  LOG(INFO) << "REMOVE_SERVER index: " << remove_server_index
            << ", committed op id on the RBS source follower: " << rbs_source_committed;
  ASSERT_LT(rbs_source_committed.index, remove_server_index);

  // Turn off part of followers so ADD_SERVER below remains pending.
  for (int i = 1; i <= 3; i++) {
    cluster_->tablet_server(i)->Shutdown();
  }

  // Ensure RBS is paused so we can reliably turn off next RBS attempts then.
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_pause_rbs_before_download_wal", "true"));
  LogWaiter log_waiter(
      added_ts, tablet_id + ": Pausing due to flag TEST_pause_rbs_before_download_wal");

  LOG(INFO) << "Adding tablet " << tablet_id << " back to tserver " << added_ts->uuid();
  // Timeout is expected because ADD_SERVER can't be committed without a majority.
  ASSERT_NOK(itest::AddServer(
      leader_ts_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt,
      500ms));

  ASSERT_OK(log_waiter.WaitFor(kTimeout));
  ASSERT_OK(cluster_->SetFlag(leader_ts, "TEST_enable_remote_bootstrap", "false"));
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_pause_rbs_before_download_wal", "false"));

  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  LOG(INFO) << "RBS has been completed on added node";

  AllowTimeForSpuriousDelete();

  LOG(INFO) << "Make sure new replica doesn't get deleted";
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kAddedTsIndex, tablet_id, TABLET_DATA_READY));

  for (int i = 1; i <= 3; i++) {
    ASSERT_OK(cluster_->tablet_server(i)->Restart());
  }
  ASSERT_OK(cluster_->SetFlag(
      rbs_source_ts, "TEST_follower_pause_update_consensus_requests", "false"));
}

// ADD_SERVER for a new replica is pending when the leader changes, and the new leader commits it
// with its NO_OP. Master learns the new term before that commit, so the replica reports a pending
// config op from an older term. Master must not delete the replica.
TEST_F(TabletReplacementITest, DontDeleteNewReplicaWhenNewLeaderCommitsPendingAddServer) {
  // RBS waits for the promotion until it happens, so the replica is reported with every heartbeat.
  std::vector<std::string> ts_flags = {
      "--enable_leader_failure_detection=false",
      "--committed_config_change_role_timeout_sec=3600"};
  std::vector<std::string> master_flags = {
      "--replication_factor=5",
      "--enable_load_balancing=false",
      "--catalog_manager_wait_for_new_tablets_to_elect_leader=false",
      "--use_create_table_leader_hint=false",
  };
  constexpr auto kInitialNumTservers = 5;
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags, kInitialNumTservers));
  ASSERT_OK(CreateTestTablet());

  auto* ts_a = cluster_->tablet_server(0);
  // These followers don't get ADD_SERVER. They still vote in the election below.
  const std::vector<ExternalTabletServer*> paused_followers = {
      cluster_->tablet_server(2), cluster_->tablet_server(3), cluster_->tablet_server(4)};
  const auto tablet_id = ASSERT_RESULT(
      ElectAsLeaderAndGetSingleTabletId(ts_map_[ts_a->uuid()].get()));

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(kInitialNumTservers + 1, kTimeout));
  ts_map_ = ASSERT_RESULT(itest::CreateTabletServerMap(cluster_.get()));
  auto* ts_a_details = ts_map_[ts_a->uuid()].get();
  auto* ts_b_details = ts_map_[cluster_->tablet_server(1)->uuid()].get();

  const auto kAddedTsIndex = kInitialNumTservers;
  auto* added_ts = cluster_->tablet_server(kAddedTsIndex);
  auto* added_ts_details = ts_map_[added_ts->uuid()].get();

  // A pre-voter doesn't count for the majority, so ADD_SERVER stays pending with only A and B
  // having it: 2 of 5 voters. The new replica doesn't accept updates either, so it keeps the
  // pending config op id from RBS.
  for (auto* ts : paused_followers) {
    ASSERT_OK(cluster_->SetFlag(ts, "TEST_follower_pause_update_consensus_requests", "true"));
  }
  ASSERT_OK(cluster_->SetFlag(added_ts, "TEST_follower_pause_update_consensus_requests", "true"));

  LOG(INFO) << "Adding tablet " << tablet_id << " to tserver " << added_ts->uuid();
  // Timeout is expected because ADD_SERVER can't be committed without a majority.
  ASSERT_NOK(itest::AddServer(
      ts_a_details, tablet_id, added_ts_details, PeerMemberType::PRE_VOTER, std::nullopt, 500ms));
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(
      kAddedTsIndex, tablet_id, tablet::TABLET_DATA_READY, kTimeout));
  ASSERT_OK(itest::WaitUntilTabletRunning(added_ts_details, tablet_id, kTimeout));
  LOG(INFO) << "RBS has been completed on added node";

  // B wins the election with the votes of the paused followers and has ADD_SERVER in its log. Its
  // NO_OP can't be committed yet: A is down, and the other voters don't accept updates.
  ts_a->Shutdown();
  ASSERT_OK(itest::StartElection(ts_b_details, tablet_id, kTimeout));
  ASSERT_OK(itest::WaitUntilLeader(
      ts_b_details, tablet_id, kTimeout, consensus::LeaderLeaseCheckMode::DONT_NEED_LEASE));
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        master::TabletLocationsPB locations;
        RETURN_NOT_OK(itest::GetTabletLocations(cluster_.get(), tablet_id, kTimeout, &locations));
        for (const auto& replica : locations.replicas()) {
          if (replica.role() == PeerRole::LEADER) {
            return replica.ts_info().permanent_uuid() == ts_b_details->uuid();
          }
        }
        return false;
      },
      kTimeout, "Waiting for master to know the new leader"));
  // Give master the time to process reports of the new replica while ADD_SERVER is pending.
  AllowTimeForSpuriousDelete();

  // Let the new leader commit its NO_OP and ADD_SERVER with it.
  for (auto* ts : paused_followers) {
    ASSERT_OK(cluster_->SetFlag(ts, "TEST_follower_pause_update_consensus_requests", "false"));
  }
  ASSERT_OK(cluster_->SetFlag(
      added_ts, "TEST_follower_pause_update_consensus_requests", "false"));
  ASSERT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        consensus::ConsensusStatePB consensus_state;
        RETURN_NOT_OK(itest::GetConsensusState(
            ts_b_details, tablet_id, consensus::CONSENSUS_CONFIG_COMMITTED, kTimeout,
            &consensus_state));
        for (const auto& peer : consensus_state.config().peers()) {
          if (peer.permanent_uuid() == added_ts->uuid()) {
            return peer.member_type() == PeerMemberType::VOTER;
          }
        }
        return false;
      },
      kTimeout, "Waiting for the new replica to be a voter in the committed config"));

  AllowTimeForSpuriousDelete();
  LOG(INFO) << "Make sure new replica doesn't get deleted";
  ASSERT_OK(inspect_->CheckTabletDataStateOnTS(kAddedTsIndex, tablet_id, TABLET_DATA_READY));
}

// Ensure that the Master will tombstone a replica if it reports in with an old
// config. This tests a slightly different code path in the catalog manager
// than TestMasterTombstoneEvictedReplica does.
TEST_F(TabletReplacementITest, TestMasterTombstoneOldReplicaOnReport) {
  vector<string> ts_flags = { "--enable_leader_failure_detection=false" };
  vector<string> master_flags = {
    "--catalog_manager_wait_for_new_tablets_to_elect_leader=false"s,
    "--use_create_table_leader_hint=false"s,
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  TServerDetails* leader_ts = ts_map_[cluster_->tablet_server(kLeaderIndex)->uuid()].get();
  const int kFollowerIndex = 2;
  TServerDetails* follower_ts = ts_map_[cluster_->tablet_server(kFollowerIndex)->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts));

  // Shut down the follower to be removed, then remove it from the config.
  // We will wait for the Master to be notified of the config change, then shut
  // down the rest of the cluster and bring the follower back up. The follower
  // will heartbeat to the Master and then be tombstoned.
  cluster_->tablet_server(kFollowerIndex)->Shutdown();

  // Remove the follower from the config and wait for the Master to notice the
  // config change.
  ASSERT_OK(itest::RemoveServer(leader_ts, tablet_id, follower_ts, std::nullopt, kTimeout));
  ASSERT_OK(itest::WaitForNumVotersInConfigOnMaster(cluster_.get(), tablet_id, 2, kTimeout));

  // Shut down the remaining tablet servers and restart the dead one.
  cluster_->tablet_server(0)->Shutdown();
  cluster_->tablet_server(1)->Shutdown();
  ASSERT_OK(cluster_->tablet_server(kFollowerIndex)->Restart());

  // Wait for the Master to tombstone the revived follower.
  ASSERT_OK(inspect_->WaitForTabletDataStateOnTS(kFollowerIndex, tablet_id, TABLET_DATA_TOMBSTONED,
                                                 kTimeout));
}

// Test that unreachable followers are evicted and replaced.
TEST_F(TabletReplacementITest, TestEvictAndReplaceDeadFollower) {
  if (!AllowSlowTests()) {
    LOG(INFO) << "Skipping test in fast-test mode.";
    return;
  }

  vector<string> ts_flags = { "--enable_leader_failure_detection=false",
                              "--follower_unavailable_considered_failed_sec=5" };
  vector<string> master_flags = {
    "--catalog_manager_wait_for_new_tablets_to_elect_leader=false"s,
    "--use_create_table_leader_hint=false"s,
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  TServerDetails* leader_ts = ts_map_[cluster_->tablet_server(kLeaderIndex)->uuid()].get();
  const int kFollowerIndex = 2;

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts));

  // Shut down the follower to be removed. It should be evicted.
  cluster_->tablet_server(kFollowerIndex)->Shutdown();

  // With a RemoveServer and AddServer, the opid_index of the committed config will be 3.
  ASSERT_OK(itest::WaitUntilCommittedOpIdIndexIs(3,
                                                 leader_ts,
                                                 tablet_id,
                                                 kTimeout,
                                                 itest::CommittedEntryType::CONFIG));
  ASSERT_OK(cluster_->tablet_server(kFollowerIndex)->Restart());
}

// Regression test for KUDU-1233. This test creates a situation in which tablet
// bootstrap will attempt to replay committed (and applied) config change
// operations. This is achieved by delaying application of a write at the
// tablet level that precedes the config change operations in the WAL, then
// initiating a remote bootstrap to a follower. The follower will not have the
// COMMIT for the write operation, so will ignore COMMIT messages for the
// applied config change operations. At startup time, the newly
// remotely-bootstrapped tablet should detect that these config change
// operations have already been applied and skip them.
TEST_F(TabletReplacementITest, TestRemoteBoostrapWithPendingConfigChangeCommits) {
  vector<string> ts_flags = {
    "--enable_leader_failure_detection=false"s,
  };
  // We will manage doing the AddServer() manually, in order to make this test
  // more deterministic.
  vector<string> master_flags = {
    "--master_tombstone_evicted_tablet_replicas=false"s,
    "--catalog_manager_wait_for_new_tablets_to_elect_leader=false"s,
    "--use_create_table_leader_hint=false"s,
  };
  ASSERT_NO_FATALS(StartCluster(ts_flags, master_flags));

  ASSERT_OK(CreateTestTablet());

  const int kLeaderIndex = 0;
  TServerDetails* leader_ts = ts_map_[cluster_->tablet_server(kLeaderIndex)->uuid()].get();
  const int kFollowerIndex = 2;
  TServerDetails* ts_to_remove = ts_map_[cluster_->tablet_server(kFollowerIndex)->uuid()].get();

  const auto tablet_id = ASSERT_RESULT(ElectAsLeaderAndGetSingleTabletId(leader_ts));

  // Write a single row.
  ASSERT_OK(WriteSimpleTestRow(leader_ts, tablet_id, 0, 0, "", kTimeout));

  // Delay tablet applies in order to delay COMMIT messages to trigger KUDU-1233.
  // Then insert another row.
  ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server_by_uuid(leader_ts->uuid()),
                              "TEST_tablet_inject_latency_on_apply_write_txn_ms", "5000"));

  // Kick off an async insert, which will be delayed for 5 seconds. This is
  // normally enough time to evict a replica, tombstone it, add it back, and
  // remotely bootstrap it when the log is only a few entries.
  tserver::WriteRequestPB req;
  tserver::WriteResponsePB resp;
  CountDownLatch latch(1);
  rpc::RpcController rpc;
  rpc.set_timeout(kTimeout);
  req.set_tablet_id(tablet_id);
  AddTestRowInsert(1, 1, "", &req);
  leader_ts->tserver_proxy->WriteAsync(req, &resp, &rpc, [&latch]() { latch.CountDown(); });

  // Wait for the replicate to show up (this doesn't wait for COMMIT messages).
  ASSERT_OK(itest::WaitForServersToAgree(kTimeout, ts_map_, tablet_id, 3));
  ASSERT_OK(itest::WaitUntilCommittedOpIdIndexIs(3, leader_ts, tablet_id, kTimeout));

  // Manually evict the server from the cluster, tombstone the replica, then
  // add the replica back to the cluster. Without the fix for KUDU-1233, this
  // will cause the replica to fail to start up.
  ASSERT_OK(itest::RemoveServer(leader_ts, tablet_id, ts_to_remove, std::nullopt, kTimeout));
  ASSERT_OK(
      itest::DeleteTablet(ts_to_remove, tablet_id, TABLET_DATA_TOMBSTONED, std::nullopt, kTimeout));
  ASSERT_OK(itest::AddServer(
      leader_ts, tablet_id, ts_to_remove, PeerMemberType::PRE_VOTER, std::nullopt, kTimeout));
  ASSERT_OK(itest::WaitUntilTabletRunning(ts_to_remove, tablet_id, kTimeout));

  ClusterVerifier cluster_verifier(cluster_.get());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(
      cluster_verifier.CheckRowCount(itest::kYcqlTestTableName, ClusterVerifier::EXACTLY, 2));

  latch.Wait(); // Avoid use-after-free on the response from the delayed RPC callback.
}

} // namespace yb
