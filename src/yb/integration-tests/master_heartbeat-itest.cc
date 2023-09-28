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

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include <gtest/gtest.h>

#include "yb/client/client.h"
#include "yb/client/table.h"
#include "yb/client/table_handle.h"

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_fwd.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"

using namespace std::literals;

DECLARE_bool(enable_load_balancing);
DECLARE_int32(heartbeat_interval_ms);
DECLARE_bool(TEST_pause_before_remote_bootstrap);
DECLARE_int32(committed_config_change_role_timeout_sec);
DECLARE_string(TEST_master_universe_uuid);
DECLARE_int32(TEST_mini_cluster_registration_wait_time_sec);
DECLARE_int32(tserver_unresponsive_timeout_ms);
DECLARE_bool(master_enable_universe_uuid_heartbeat_check);

namespace yb {

namespace integration_tests {

class MasterHeartbeatITest : public YBTableTestBase {
 public:
  void SetUp() override {
    YBTableTestBase::SetUp();
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(client_->messenger());
  }
 protected:
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

TEST_F(MasterHeartbeatITest, PreventHeartbeatWrongCluster) {
  // First ensure that if a tserver heartbeats to a different cluster, heartbeats fail and
  // eventually, master marks servers as dead. Mock a different cluster by setting the flag
  // TEST_master_universe_uuid.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = Uuid::Generate().ToString();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_unresponsive_timeout_ms) = 10 * 1000;
  master::TSDescriptorVector ts_descs;
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(0, &ts_descs, true /* live_only */));

  // When the flag is unset, ensure that master leader can register tservers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = "";
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(3, &ts_descs, true /* live_only */));

  // Ensure that state for universe_uuid is persisted across restarts.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_master_universe_uuid) = Uuid::Generate().ToString();
  for (int i = 0; i < 3; i++) {
    ASSERT_OK(mini_cluster_->mini_tablet_server(i)->Restart());
  }
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(0, &ts_descs, true /* live_only */));
}


TEST_F(MasterHeartbeatITest, IgnorePeerNotInConfig) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_load_balancing) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = true;
  // Don't wait too long for PRE-OBSERVER -> OBSERVER config change to succeed, since it will fail
  // anyways in this test. This makes the TearDown complete in a reasonable amount of time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_committed_config_change_role_timeout_sec) = 1 * kTimeMultiplier;

  const auto timeout = 10s;

  ASSERT_OK(mini_cluster_->AddTabletServer());
  ASSERT_OK(mini_cluster_->WaitForTabletServerCount(4));

  auto& catalog_mgr = ASSERT_RESULT(mini_cluster_->GetLeaderMiniMaster())->catalog_manager();
  auto table_info = catalog_mgr.GetTableInfo(table_->id());
  auto tablet = table_info->GetTablets()[0];

  master::MasterClusterProxy master_proxy(
      proxy_cache_.get(), mini_cluster_->mini_master()->bound_rpc_addr());
  auto ts_map = ASSERT_RESULT(itest::CreateTabletServerMap(master_proxy, proxy_cache_.get()));

  auto new_ts_uuid = mini_cluster_->mini_tablet_server(3)->server()->permanent_uuid();
  auto* new_ts = ts_map[new_ts_uuid].get();
  itest::TServerDetails* leader_ts;
  ASSERT_OK(itest::FindTabletLeader(ts_map, tablet->id(), timeout, &leader_ts));

  // Add the tablet to the new tserver to start the RBS (it will get stuck before starting).
  ASSERT_OK(itest::AddServer(leader_ts, tablet->id(), new_ts,
                             consensus::PeerMemberType::PRE_OBSERVER, boost::none, timeout));
  ASSERT_OK(itest::WaitForTabletConfigChange(tablet, new_ts_uuid, consensus::ADD_SERVER));

  // Remove the tablet from the new tserver and let the remote bootstrap proceed.
  ASSERT_OK(itest::RemoveServer(leader_ts, tablet->id(), new_ts, boost::none, timeout));
  ASSERT_OK(itest::WaitForTabletConfigChange(tablet, new_ts_uuid, consensus::REMOVE_SERVER));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_remote_bootstrap) = false;

  ASSERT_OK(WaitFor([&]() {
    auto replica_locations = tablet->GetReplicaLocations();
    int leaders = 0, followers = 0;
    LOG(INFO) << Format("Replica locations after new tserver heartbeat: $0", *replica_locations);
    if (replica_locations->size() != 3) {
      return false;
    }
    for (auto& p : *replica_locations) {
      if (p.first == new_ts_uuid ||
          p.second.state != tablet::RaftGroupStatePB::RUNNING ||
          p.second.member_type != consensus::VOTER) {
        return false;
      }
      if (p.second.role == LEADER) {
        ++leaders;
      } else if (p.second.role == FOLLOWER) {
        ++followers;
      }
    }
    if (leaders != 1 || followers != 2) {
      return false;
    }
    return true;
  }, FLAGS_heartbeat_interval_ms * 5ms, "Wait for proper replica locations."));
}

class MasterHeartbeatITestWithExternal : public MasterHeartbeatITest {
 public:
  bool use_external_mini_cluster() { return true; }

  Status RestartAndWipeWithFlags(
      std::vector<ExternalTabletServer*> tservers,
      std::vector<std::vector<std::pair<std::string, std::string>>> extra_flags = {}) {
    for (const auto& tserver : tservers) {
      tserver->Shutdown();
    }
    for (const auto& tserver : tservers) {
      for (const auto& data_dir : tserver->GetDataDirs()) {
        RETURN_NOT_OK(Env::Default()->DeleteRecursively(data_dir));
      }
    }
    for (size_t i = 0; i < tservers.size(); ++i) {
      auto extra_flags_for_tserver = extra_flags.size() > i
                                         ? extra_flags[i]
                                         : std::vector<std::pair<std::string, std::string>>();
      RETURN_NOT_OK(tservers[i]->Restart(
          ExternalMiniClusterOptions::kDefaultStartCqlProxy, extra_flags_for_tserver));
    }
    return Status::OK();
  }

  Status WaitForRegisteredTserverSet(
      const std::set<std::string>& uuids, MonoDelta timeout, const std::string& message) {
    master::MasterClusterProxy master_proxy(
        proxy_cache_.get(), external_mini_cluster()->master()->bound_rpc_addr());
    return WaitFor(
        [&]() -> Result<bool> {
          master::ListTabletServersResponsePB resp;
          master::ListTabletServersRequestPB req;
          rpc::RpcController rpc;
          RETURN_NOT_OK(master_proxy.ListTabletServers(req, &resp, &rpc));
          std::set<std::string> current_uuids;
          for (const auto& server : resp.servers()) {
            current_uuids.insert(server.instance_id().permanent_uuid());
          }
          return current_uuids == uuids;
        },
        timeout, message);
  }
};

TEST_F(MasterHeartbeatITestWithExternal, ReRegisterRemovedPeers) {
  auto cluster = external_mini_cluster();
  ASSERT_EQ(cluster->tserver_daemons().size(), 3);
  LOG(INFO) << "Create a user table.";
  CreateTable();
  constexpr int kNumRows = 1000;
  for (int i = 0; i < kNumRows; ++i) {
    PutKeyValue(Format("k$0", i), Format("v$0", i));
  }
  std::map<std::string, ExternalTabletServer*> wiped_tservers;
  wiped_tservers[cluster->tablet_server(1)->uuid()] = cluster->tablet_server(1);
  wiped_tservers[cluster->tablet_server(2)->uuid()] = cluster->tablet_server(2);
  LOG(INFO) << "Wipe a majority of the quorum to simulate majority disk failures.";
  ASSERT_OK(RestartAndWipeWithFlags({cluster->tablet_server(1), cluster->tablet_server(2)}));

  std::set<std::string> original_uuids;
  std::set<std::string> new_uuids;
  original_uuids.insert(cluster->tablet_server(0)->uuid());
  new_uuids.insert(cluster->tablet_server(0)->uuid());
  for (const auto& [original_uuid, wiped_tserver] : wiped_tservers) {
    ASSERT_NE(original_uuid, wiped_tserver->uuid())
        << "Original tserver uuid should not be equal to the restarted tserver uuid";
    original_uuids.insert(original_uuid);
    new_uuids.insert(wiped_tserver->uuid());
  }
  ASSERT_EQ(original_uuids.size(), 3);
  ASSERT_EQ(new_uuids.size(), 3);
  ASSERT_OK(
      WaitForRegisteredTserverSet(new_uuids, 60s, "Waiting for master to register new uuids"));
  const std::string override_flag_name = "instance_uuid_override";
  std::vector<std::vector<std::pair<std::string, std::string>>> extra_flags;
  std::vector<ExternalTabletServer*> just_tservers;
  for (const auto& [original_uuid, wiped_tserver] : wiped_tservers) {
    extra_flags.push_back({{override_flag_name, original_uuid}});
    just_tservers.push_back(wiped_tserver);
  }
  ASSERT_OK(RestartAndWipeWithFlags(just_tservers, extra_flags));
  for (const auto& [original_uuid, wiped_tserver] : wiped_tservers) {
    ASSERT_EQ(original_uuid, wiped_tserver->uuid())
        << "After overriding uuid, new tserver uuid should be equal to original tserver uuid";
  }

  ASSERT_OK(WaitForRegisteredTserverSet(
      original_uuids, 60s, "Wait for master to register original uuids"));
}

}  // namespace integration_tests

}  // namespace yb
