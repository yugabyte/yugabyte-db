// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/upgrade-tests/pg15_upgrade_test_base.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/tools/yb-admin_client.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/sync_point.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb {

static const MonoDelta kNoDelayBetweenNodes = 0s;

class YsqlMajorUpgradeRpcsTest : public Pg15UpgradeTestBase {
 public:
  YsqlMajorUpgradeRpcsTest() = default;

  void SetUp() override {
    Pg15UpgradeTestBase::SetUp();
    if (IsTestSkipped()) {
      return;
    }

    CHECK_OK(CreateSimpleTable());
  }

 protected:
  master::MasterAdminProxy GetMasterAdminProxy() {
    return master::MasterAdminProxy(cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>());
  }

  void AsyncStartYsqlMajorUpgrade(
      master::StartYsqlMajorCatalogUpgradeResponsePB& resp, rpc::RpcController& rpc,
      CountDownLatch& latch) {
    LOG_WITH_FUNC(INFO) << "Starting ysql major upgrade";

    master::StartYsqlMajorCatalogUpgradeRequestPB req;
    GetMasterAdminProxy().StartYsqlMajorCatalogUpgradeAsync(
        req, &resp, &rpc, [&latch]() { latch.CountDown(); });
  }

  void AsyncStartYsqlMajorRollback(
      master::RollbackYsqlMajorCatalogVersionResponsePB& resp, rpc::RpcController& rpc,
      CountDownLatch& latch) {
    LOG_WITH_FUNC(INFO) << "Starting ysql major upgrade rollback";

    master::RollbackYsqlMajorCatalogVersionRequestPB req;
    GetMasterAdminProxy().RollbackYsqlMajorCatalogVersionAsync(
        req, &resp, &rpc, [&latch]() { latch.CountDown(); });
  }

  // Waits for the catalog upgrade to finish and completes the rest of the upgrade with validation
  // checks.
  // Ysql major upgrade must already be started.
  Status CompleteUpgradeAndValidate() {
    RETURN_NOT_OK(WaitForYsqlMajorCatalogUpgradeToFinish());

    LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in current version";
    auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
    RETURN_NOT_OK(RestartTServerInCurrentVersion(
        *mixed_mode_pg15_tserver, /*wait_for_cluster_to_stabilize=*/true));

    RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg11));
    RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg15));

    RETURN_NOT_OK(FinalizeUpgradeFromMixedMode());

    return InsertRowInSimpleTableAndValidate();
  }
};

// Start multiple ysql major upgrade RPCs simultaneously. Only one RPC should succeed.
TEST_F(YsqlMajorUpgradeRpcsTest, SimultaneousUpgrades) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  const int kNumSimultaneousRpcs = 2;

  rpc::RpcController rpcs[kNumSimultaneousRpcs];
  CountDownLatch latch(kNumSimultaneousRpcs);
  master::StartYsqlMajorCatalogUpgradeResponsePB resps[kNumSimultaneousRpcs];
  for (int i = 0; i < kNumSimultaneousRpcs; i++) {
    AsyncStartYsqlMajorUpgrade(resps[i], rpcs[i], latch);
  }

  latch.Wait();

  for (auto& rpc : rpcs) {
    ASSERT_TRUE(rpc.finished());
  }

  int num_success = 0;
  for (auto& resp : resps) {
    LOG(INFO) << "Start upgrade response: " << resp.DebugString();
    num_success += !resp.has_error();
  }
  ASSERT_EQ(num_success, 1);

  ASSERT_OK(CompleteUpgradeAndValidate());

  ASSERT_NOK_STR_CONTAINS(
      PerformYsqlMajorCatalogUpgrade(), "Ysql Catalog is already on the current major version");
}

// Start multiple ysql major upgrade rollback RPCs simultaneously. Only one RPC should succeed.
TEST_F(YsqlMajorUpgradeRpcsTest, SimultaneousRollback) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  // Calling rollback before upgrade should succeed immediately.
  ASSERT_OK(RollbackYsqlMajorCatalogVersion());

  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  const int kNumSimultaneousRpcs = 2;

  rpc::RpcController rpcs[kNumSimultaneousRpcs];
  CountDownLatch latch(kNumSimultaneousRpcs);
  master::RollbackYsqlMajorCatalogVersionResponsePB resps[kNumSimultaneousRpcs];
  for (int i = 0; i < kNumSimultaneousRpcs; i++) {
    AsyncStartYsqlMajorRollback(resps[i], rpcs[i], latch);
  }

  latch.Wait();

  for (auto& rpc : rpcs) {
    ASSERT_TRUE(rpc.finished());
  }

  int num_success = 0;
  for (auto& resp : resps) {
    LOG(INFO) << "Rollback upgrade response: " << resp.DebugString();
    num_success += !resp.has_error();
  }
  ASSERT_EQ(num_success, 1);

  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));
  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

// Make sure ysql major catalog rollback is blocked while we are running ysql major upgrade.
TEST_F(YsqlMajorUpgradeRpcsTest, RollbackDuringUpgrade) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  master::StartYsqlMajorCatalogUpgradeResponsePB upgrade_response;
  master::RollbackYsqlMajorCatalogVersionResponsePB rollback_response;

  rpc::RpcController rpc;
  CountDownLatch latch(1);

  AsyncStartYsqlMajorUpgrade(upgrade_response, rpc, latch);

  // The upgrade takes longer than 5s in all builds.
  SleepFor(5s);
  ASSERT_NOK_STR_CONTAINS(
      RollbackYsqlMajorCatalogVersion(),
      "Global initdb or ysql major catalog upgrade/rollback is already in progress");

  latch.Wait();
  ASSERT_TRUE(rpc.finished());

  ASSERT_FALSE(upgrade_response.has_error()) << upgrade_response.error().ShortDebugString();

  ASSERT_OK(CompleteUpgradeAndValidate());
}

// Make sure ysql major catalog upgrade is blocked while we are running ysql major upgrade rollback.
TEST_F(YsqlMajorUpgradeRpcsTest, UpgradeDuringRollback) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  master::RollbackYsqlMajorCatalogVersionResponsePB rollback_response;
  rpc::RpcController rpc;
  CountDownLatch latch(1);

  AsyncStartYsqlMajorRollback(rollback_response, rpc, latch);

  // The rollback takes around 2s in all builds.
  SleepFor(2s);
  ASSERT_NOK(PerformYsqlMajorCatalogUpgrade());

  latch.Wait();
  ASSERT_TRUE(rpc.finished());

  ASSERT_FALSE(rollback_response.has_error()) << rollback_response.ShortDebugString();

  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));
  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

// Make sure ysql major catalog upgrade works with a master crash during the upgrade.
// Disabling in debug builds since this test times out on it.
TEST_F(YsqlMajorUpgradeRpcsTest, YB_DISABLE_TEST_EXCEPT_RELEASE(MasterCrashDuringUpgrade)) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  auto master_leader = cluster_->GetLeaderMaster();

  // Block the upgrade from finishing.
  ASSERT_OK(cluster_->SetFlag(
      master_leader, "TEST_fail_ysql_catalog_upgrade_state_transition_from",
      "PERFORMING_PG_UPGRADE"));

  rpc::RpcController rpc;
  CountDownLatch latch(1);
  master::StartYsqlMajorCatalogUpgradeResponsePB upgrade_response;
  AsyncStartYsqlMajorUpgrade(upgrade_response, rpc, latch);
  latch.Wait();

  ASSERT_OK(LoggedWaitFor(
      [this]() -> Result<bool> {
        return VERIFY_RESULT(DumpYsqlCatalogConfig()).find("state: PERFORMING_PG_UPGRADE") !=
               std::string::npos;
      },
      5min, "Waiting for pg_upgrade to start"));

  // The pg_upgrade takes longer than 2s in all builds.
  SleepFor(2s);

  master_leader->Shutdown();
  ASSERT_OK(WaitForClusterToStabilize());

  auto ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_CONTAINS(ysql_catalog_config, "state: FAILED");
  ASSERT_OK(InsertRowInSimpleTableAndValidate());

  ASSERT_OK(master_leader->Restart());

  ASSERT_OK(RollbackYsqlMajorCatalogVersion());
  ASSERT_OK(InsertRowInSimpleTableAndValidate());

  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));
  ASSERT_OK(InsertRowInSimpleTableAndValidate());

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

// Make sure ysql major catalog upgrade works with a master crash during the upgrade.
TEST_F(YsqlMajorUpgradeRpcsTest, MasterCrashDuringMonitoring) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  auto master_leader = cluster_->GetLeaderMaster();
  master_leader->Shutdown();
  ASSERT_OK(WaitForClusterToStabilize());

  auto ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_CONTAINS(ysql_catalog_config, "state: MONITORING");

  ASSERT_OK(InsertRowInSimpleTableAndValidate());

  ASSERT_OK(master_leader->Restart());
  ASSERT_OK(CompleteUpgradeAndValidate());
}

TEST_F(YsqlMajorUpgradeRpcsTest, CompactSysCatalogAfterUpgrade) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  ASSERT_OK(FlushAndCompactSysCatalog(cluster_.get(), 5min));
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

TEST_F(YsqlMajorUpgradeRpcsTest, CompactSysCatalogAfterRollback) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  ASSERT_OK(InsertRowInSimpleTableAndValidate());

  ASSERT_OK(RollbackYsqlMajorCatalogVersion());

  ASSERT_OK(FlushAndCompactSysCatalog(cluster_.get(), 5min));
  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));

  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

TEST_F(YsqlMajorUpgradeRpcsTest, CompactSysCatalogAfterEveryPhase) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  ASSERT_OK(FlushAndCompactSysCatalog(cluster_.get(), 5min));
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_OK(InsertRowInSimpleTableAndValidate());

  ASSERT_OK(RollbackYsqlMajorCatalogVersion());

  ASSERT_OK(FlushAndCompactSysCatalog(cluster_.get(), 5min));
  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));

  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

class YsqlMajorUpgradeYbAdminTest : public Pg15UpgradeTestBase {
 public:
  YsqlMajorUpgradeYbAdminTest() = default;

 protected:
  Status PerformYsqlMajorCatalogUpgrade() override {
    return cluster_->CallYbAdmin({"ysql_major_version_catalog_upgrade"}, 10min);
  }

  Status RollbackYsqlMajorCatalogVersion() override {
    return cluster_->CallYbAdmin({"rollback_ysql_major_version_upgrade"}, 10min);
  }
};

TEST_F(YsqlMajorUpgradeYbAdminTest, Upgrade) { ASSERT_OK(TestUpgradeWithSimpleTable()); }

TEST_F(YsqlMajorUpgradeYbAdminTest, Rollback) { ASSERT_OK(TestRollbackWithSimpleTable()); }

}  // namespace yb
