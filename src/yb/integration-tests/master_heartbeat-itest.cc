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
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_cluster.proxy.h"

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
  FLAGS_TEST_pause_before_remote_bootstrap = false;

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

}  // namespace integration_tests

}  // namespace yb
