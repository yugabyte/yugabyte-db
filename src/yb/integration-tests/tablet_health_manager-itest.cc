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

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

#include "yb/integration-tests/cluster_itest_util.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/integration-tests/yb_table_test_base.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(raft_heartbeat_interval_ms);

using std::string;
using std::vector;

namespace yb {
namespace integration_tests {

class AreNodesSafeToTakeDownItest : public YBTableTestBase {
 protected:
  void BeforeCreateTable() override {
    ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
        "c.r.z0,c.r.z1,c.r.z2", 3 /* rf */, "" /* optional_uuid */));
  }

  void SetUp() override {
    YBTableTestBase::SetUp();
    client_->TEST_set_admin_operation_timeout(5s);
    ASSERT_OK(itest::WaitForReplicasRunningOnAllTsAccordingToMaster(
        external_mini_cluster_.get(), table_.name(), 10s * kTimeMultiplier));
  }

  bool use_yb_admin_client() override { return true; }

  bool use_external_mini_cluster() override { return true; }

  bool enable_ysql() override {
    // Do not create the transaction status table.
    return false;
  }

  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* opts) override {
    opts->extra_tserver_flags.push_back("--placement_cloud=c");
    opts->extra_tserver_flags.push_back("--placement_region=r");
    opts->extra_tserver_flags.push_back("--placement_zone=z${index}");
  }

  virtual size_t num_masters() override { return 3; }

  void SleepUntilLeaderElectionAndAtLeast(int sleep_at_least_ms) {
    int leader_election_duration_ms = std::ceil(
        FLAGS_raft_heartbeat_interval_ms * FLAGS_leader_failure_max_missed_heartbeat_periods);
    // Give 1s extra for elections to happen after the failure detection timeout.
    SleepFor(std::max(leader_election_duration_ms + 1000, sleep_at_least_ms) * 1ms);
  }

  void AssertTabletsWouldBeUnderReplicated(
      const vector<string>& tserver_uuids, const vector<string>& master_uuids) {
    auto status = client_->AreNodesSafeToTakeDown(tserver_uuids, master_uuids, kFollowerLagBoundMs);
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.message().ToBuffer(), "tablet(s) would be under-replicated");
  }

  void AssertCallTimesOut(
      const vector<string>& tserver_uuids, const vector<string>& master_uuids) {
    auto status = client_->AreNodesSafeToTakeDown(tserver_uuids, master_uuids, kFollowerLagBoundMs);
    ASSERT_NOK(status);
    ASSERT_TRUE(status.IsTimedOut());
  }

  const int kFollowerLagBoundMs = 1000;
};

TEST_F(AreNodesSafeToTakeDownItest, HealthyCluster) {
  vector<string> tserver_uuids, master_uuids;
  for (size_t i = 0; i < 3; ++i) {
    tserver_uuids.push_back(external_mini_cluster_->tablet_server(i)->uuid());
    master_uuids.push_back(external_mini_cluster_->master(i)->uuid());
  }

  // Basic 3 node RF3 setup. Should be able to take down one master and one tserver.
  ASSERT_OK(client_->AreNodesSafeToTakeDown(
      {tserver_uuids[0]}, {master_uuids[0]}, kFollowerLagBoundMs));

  // Should not be able to take down two masters or two tservers.
  AssertTabletsWouldBeUnderReplicated({tserver_uuids[0], tserver_uuids[1]}, {} /* master_uuids */);
  AssertTabletsWouldBeUnderReplicated({} /* tserver_uuids */, {master_uuids[0], master_uuids[1]});

  // Check that the call completes (unsuccessfully) even if we take down all masters / tservers.
  AssertTabletsWouldBeUnderReplicated(tserver_uuids, {} /* master_uuids */);
  AssertTabletsWouldBeUnderReplicated({} /* tserver_uuids */, master_uuids);
}

TEST_F(AreNodesSafeToTakeDownItest, MasterUnresponsive) {
  auto bad_master = external_mini_cluster_->master(0);
  auto good_master = external_mini_cluster_->master(1);

  bad_master->Shutdown();
  SleepUntilLeaderElectionAndAtLeast(kFollowerLagBoundMs);

  // Should time out trying to remove one of the 2 remaining masters.
  AssertCallTimesOut({} /* tserver_uuids */, {good_master->uuid()});

  // Should be able to remove the bad master.
  ASSERT_OK(client_->AreNodesSafeToTakeDown({}, {bad_master->uuid()}, kFollowerLagBoundMs));
}

TEST_F(AreNodesSafeToTakeDownItest, TserverUnresponsive) {
  auto bad_tserver = external_mini_cluster_->tablet_server(0);
  auto good_tserver = external_mini_cluster_->tablet_server(1);

  bad_tserver->Shutdown();
  SleepUntilLeaderElectionAndAtLeast(kFollowerLagBoundMs);

  // Should time out waiting to hear from bad_tserver when trying to remove one of the 2 tservers
  // that are not being taken down.
  AssertCallTimesOut({good_tserver->uuid()}, {} /* master_uuids */);

  // Should be able to remove the bad tserver.
  ASSERT_OK(client_->AreNodesSafeToTakeDown({bad_tserver->uuid()}, {}, kFollowerLagBoundMs));
}

TEST_F(AreNodesSafeToTakeDownItest, TserverLagging) {
  auto bad_tserver = external_mini_cluster_->tablet_server(0);
  auto good_tserver = external_mini_cluster_->tablet_server(1);

  ASSERT_OK(external_mini_cluster_->SetFlag(bad_tserver, "TEST_set_tablet_follower_lag_ms",
      std::to_string(kFollowerLagBoundMs + 1)));

  // Should not be able to remove one of the 2 remaining tservers.
  AssertTabletsWouldBeUnderReplicated({good_tserver->uuid()}, {} /* master_uuids */);
}

class AreNodesSafeToTakeDownRf1Itest : public AreNodesSafeToTakeDownItest {
 protected:
  void BeforeCreateTable() override {
    ASSERT_OK(yb_admin_client_->ModifyPlacementInfo("c.r.z0", 1 /* rf */, "" /* optional_uuid */));
  }

  virtual size_t num_masters() override { return 1; }
  virtual size_t num_tablet_servers() override { return 1; }
};

TEST_F(AreNodesSafeToTakeDownRf1Itest, HealthyCluster) {
  // We should not be able to take down the one tserver or one master in an RF1 universe.
  AssertTabletsWouldBeUnderReplicated(
      {external_mini_cluster_->tablet_server(0)->uuid()}, {} /* master_uuids */);
  AssertTabletsWouldBeUnderReplicated(
      {} /* tserver_uuids */, {external_mini_cluster_->master(0)->uuid()});
}

class AreNodesSafeToTakeDownRf5Itest : public AreNodesSafeToTakeDownItest {
 protected:
  void BeforeCreateTable() override {
    ASSERT_OK(yb_admin_client_->ModifyPlacementInfo(
          "c.r.z0,c.r.z1,c.r.z2,c.r.z3,c.r.z4", 5 /* rf */, "" /* optional_uuid */));
  }

  virtual size_t num_masters() override { return 5; }
  virtual size_t num_tablet_servers() override { return 5; }
};

TEST_F(AreNodesSafeToTakeDownRf5Itest, CallbackHandlerOutlivesDriver) {
  // Test that AreNodesSafeToTakeDownCallbackHandler outlives the driver since the driver may return
  // before receiving all responses (e.g. if it detects quorum for all tablets, or times out).
  auto tserver_to_take_down = external_mini_cluster_->tablet_server(0);
  auto slow_tserver = external_mini_cluster_->tablet_server(1);
  ASSERT_OK(external_mini_cluster_->SetFlag(
      slow_tserver, "TEST_pause_before_tablet_health_response", "true"));

  // The driver should return true as soon as it receives responses from 3/5 tservers.
  ASSERT_OK(client_->AreNodesSafeToTakeDown(
      {tserver_to_take_down->uuid()}, {}, kFollowerLagBoundMs));

  // Should not get any crashes when processing the last tserver's response.
  ASSERT_OK(external_mini_cluster_->SetFlag(
      slow_tserver, "TEST_pause_before_tablet_health_response", "false"));
  ASSERT_NO_FATALS(SleepFor(2s));
}

TEST_F(AreNodesSafeToTakeDownRf5Itest, TserverUnresponsive) {
  auto bad_tserver = external_mini_cluster_->tablet_server(0);
  auto good_tserver = external_mini_cluster_->tablet_server(1);

  bad_tserver->Shutdown();

  // Should be able to remove one of the 4 remaining tservers since we would still have quorum.
  ASSERT_OK(client_->AreNodesSafeToTakeDown({good_tserver->uuid()}, {}, kFollowerLagBoundMs));
}

TEST_F(AreNodesSafeToTakeDownRf5Itest, TserverLagging) {
  auto bad_tserver = external_mini_cluster_->tablet_server(0);
  auto good_tserver1 = external_mini_cluster_->tablet_server(1);
  auto good_tserver2 = external_mini_cluster_->tablet_server(2);

  ASSERT_OK(external_mini_cluster_->SetFlag(bad_tserver, "TEST_set_tablet_follower_lag_ms",
      std::to_string(kFollowerLagBoundMs + 1)));

  // Should not be able to remove two of the 4 remaining tservers.
  AssertTabletsWouldBeUnderReplicated(
      {good_tserver1->uuid(), good_tserver2->uuid()}, {} /* master_uuids */);

  // Should be able to remove one of the 4 remaining tservers since we would still have quorum.
  ASSERT_OK(client_->AreNodesSafeToTakeDown({good_tserver1->uuid()}, {}, kFollowerLagBoundMs));

  // Should be able to remove bad tserver.
  ASSERT_OK(client_->AreNodesSafeToTakeDown({bad_tserver->uuid()}, {}, kFollowerLagBoundMs));

  // Should be able to remove bad tserver and one good tserver.
  ASSERT_OK(client_->AreNodesSafeToTakeDown(
      {bad_tserver->uuid(), good_tserver1->uuid()}, {}, kFollowerLagBoundMs));
}

} // namespace integration_tests
} // namespace yb
