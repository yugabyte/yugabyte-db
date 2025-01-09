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

    RETURN_NOT_OK(ValidateYsqlMajorCatalogUpgradeState(
        master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));

    LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in current version";
    auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
    RETURN_NOT_OK(RestartTServerInCurrentVersion(
        *mixed_mode_pg15_tserver, /*wait_for_cluster_to_stabilize=*/true));

    RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg11));
    RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg15));

    RETURN_NOT_OK(FinalizeUpgradeFromMixedMode());

    return InsertRowInSimpleTableAndValidate();
  }

  Status ValidateYsqlMajorCatalogUpgradeState(master::YsqlMajorCatalogUpgradeState state) {
    rpc::RpcController rpc;
    rpc.set_timeout(10s);
    master::GetYsqlMajorCatalogUpgradeStateRequestPB req;
    master::GetYsqlMajorCatalogUpgradeStateResponsePB resp;
    RETURN_NOT_OK(GetMasterAdminProxy().GetYsqlMajorCatalogUpgradeState(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    SCHECK(
        resp.state() == state, IllegalState,
        Format(
            "Expected state: $0, actual state: $1",
            master::YsqlMajorCatalogUpgradeState_Name(state),
            master::YsqlMajorCatalogUpgradeState_Name(resp.state())));

    RETURN_NOT_OK(ValidateYsqlMajorUpgradeCatalogStateViaYbAdmin(state));
    return Status::OK();
  }

  Status ValidateYsqlMajorUpgradeCatalogStateViaYbAdmin(
      master::YsqlMajorCatalogUpgradeState state) {
    std::string output;
    RETURN_NOT_OK(
        cluster_->CallYbAdmin({"get_ysql_major_version_catalog_upgrade_state"}, 10min, &output));

    switch (state) {
      case master::YSQL_MAJOR_CATALOG_UPGRADE_UNINITIALIZED:
        // Bad enum, fail the call.
        break;
      case master::YSQL_MAJOR_CATALOG_UPGRADE_DONE:
        SCHECK_STR_CONTAINS(
            output, "YSQL major catalog upgrade already completed, or is not required.");
        return Status::OK();
      case master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING:
        SCHECK_STR_CONTAINS(
            output, "YSQL major catalog upgrade for YSQL major upgrade has not yet started.");
        return Status::OK();
      case master::YSQL_MAJOR_CATALOG_UPGRADE_IN_PROGRESS:
        SCHECK_STR_CONTAINS(output, "YSQL major catalog upgrade is in progress.");
        return Status::OK();
      case master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK:
        SCHECK_STR_CONTAINS(output, "YSQL major catalog upgrade failed.");
        return Status::OK();
      case master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK:
        SCHECK_STR_CONTAINS(output, "YSQL major catalog awaiting finalization or rollback.");
        return Status::OK();
      case master::YSQL_MAJOR_CATALOG_UPGRADE_ROLLBACK_IN_PROGRESS:
        SCHECK_STR_CONTAINS(output, "YSQL major catalog rollback is in progress.");
        return Status::OK();
    }
    return STATUS_FORMAT(IllegalState, "Unknown state: $0", state);
  }
};

// Start multiple ysql major upgrade RPCs simultaneously. Only one RPC should succeed.
TEST_F(YsqlMajorUpgradeRpcsTest, SimultaneousUpgrades) {
  ASSERT_NOK_STR_CONTAINS(
      ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_DONE),
      "invalid method name");

  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING));

  const int kNumSimultaneousRpcs = 2;

  rpc::RpcController rpcs[kNumSimultaneousRpcs];
  CountDownLatch latch(kNumSimultaneousRpcs);
  master::StartYsqlMajorCatalogUpgradeResponsePB resps[kNumSimultaneousRpcs];
  for (int i = 0; i < kNumSimultaneousRpcs; i++) {
    AsyncStartYsqlMajorUpgrade(resps[i], rpcs[i], latch);
  }

  // The upgrade takes longer than 5s in all builds.
  SleepFor(5s);
  // Make sure ysql major catalog rollback is blocked while we are running ysql major upgrade.
  ASSERT_NOK_STR_CONTAINS(
      RollbackYsqlMajorCatalogVersion(),
      "Global initdb or ysql major catalog upgrade/rollback is already in progress");

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

  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_IN_PROGRESS));

  ASSERT_OK(CompleteUpgradeAndValidate());

  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_DONE));

  ASSERT_NOK_STR_CONTAINS(
      PerformYsqlMajorCatalogUpgrade(), "Ysql Catalog is already on the current major version");
}

// Start multiple ysql major upgrade rollback RPCs simultaneously. Only one RPC should succeed.
TEST_F(YsqlMajorUpgradeRpcsTest, SimultaneousRollback) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  // Calling rollback before upgrade should succeed immediately.
  ASSERT_OK(RollbackYsqlMajorCatalogVersion());

  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING));

  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  const int kNumSimultaneousRpcs = 2;

  rpc::RpcController rpcs[kNumSimultaneousRpcs];
  CountDownLatch latch(kNumSimultaneousRpcs);
  master::RollbackYsqlMajorCatalogVersionResponsePB resps[kNumSimultaneousRpcs];
  for (int i = 0; i < kNumSimultaneousRpcs; i++) {
    AsyncStartYsqlMajorRollback(resps[i], rpcs[i], latch);
  }

  // Make sure ysql major catalog upgrade is blocked while we are running ysql major upgrade
  // rollback.
  // Wait for the async work to start. The rollback takes more than 2s in all builds.
  SleepFor(2s);
  ASSERT_NOK_STR_CONTAINS(
      PerformYsqlMajorCatalogUpgrade(),
      "Invalid state transition from PERFORMING_ROLLBACK to PERFORMING_INIT_DB");

  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(
      master::YSQL_MAJOR_CATALOG_UPGRADE_ROLLBACK_IN_PROGRESS));

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

  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING));

  ASSERT_OK(RestartAllMastersInOldVersion(kNoDelayBetweenNodes));

  ASSERT_NOK_STR_CONTAINS(
      ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_DONE),
      "invalid method name");

  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

// Make sure ysql major catalog upgrade works with a master crash during the upgrade.
// Disabling in debug builds since this test times out on it.
TEST_F(YsqlMajorUpgradeRpcsTest, YB_DISABLE_TEST_EXCEPT_RELEASE(MasterCrashDuringUpgrade)) {
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));
  auto master_leader = cluster_->GetLeaderMaster();

  auto state_to_fail_at = master::YsqlMajorCatalogUpgradeInfoPB::PERFORMING_PG_UPGRADE;
#ifdef __APPLE__
  // Mac machines are slow so fail earlier.
  state_to_fail_at = master::YsqlMajorCatalogUpgradeInfoPB::PERFORMING_INIT_DB;
#endif

  // Block the upgrade from finishing.
  ASSERT_OK(cluster_->SetFlag(
      master_leader, "TEST_fail_ysql_catalog_upgrade_state_transition_from",
      master::YsqlMajorCatalogUpgradeInfoPB::State_Name(state_to_fail_at)));

  rpc::RpcController rpc;
  CountDownLatch latch(1);
  master::StartYsqlMajorCatalogUpgradeResponsePB upgrade_response;
  AsyncStartYsqlMajorUpgrade(upgrade_response, rpc, latch);
  latch.Wait();

  ASSERT_OK(WaitForState(state_to_fail_at));

  // The pg_upgrade takes longer than 2s in all builds.
  SleepFor(2s);

  master_leader->Shutdown();
  ASSERT_OK(WaitForClusterToStabilize());

  auto ysql_catalog_config = ASSERT_RESULT(DumpYsqlCatalogConfig());
  ASSERT_STR_CONTAINS(ysql_catalog_config, "state: FAILED");
  ASSERT_OK(
      ValidateYsqlMajorCatalogUpgradeState(master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK));

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
  ASSERT_OK(ValidateYsqlMajorCatalogUpgradeState(
      master::YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));

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
