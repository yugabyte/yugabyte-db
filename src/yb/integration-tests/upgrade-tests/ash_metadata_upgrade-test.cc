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

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class AshMetadataUpgradeTest : public UpgradeTestBase {
 public:
  AshMetadataUpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}

 protected:
  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.extra_tserver_flags.push_back("--ysql_yb_enable_ash=true");
    opts.num_masters = kNumServers;
    opts.num_tablet_servers = kNumServers;
    UpgradeTestBase::SetUpOptions(opts);
  }

  Status UpgradeClusterToMixedMode() {
    RETURN_NOT_OK_PREPEND(
        RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes), "Failed to restart masters");

    LOG(INFO) << "Restarting yb-tserver " << kNewVersionTserver << " in current version";
    auto new_version_tserver = cluster_->tablet_server(kNewVersionTserver);
    RETURN_NOT_OK(RestartTServerInCurrentVersion(
        *new_version_tserver, /*wait_for_cluster_to_stabilize=*/true));

    return Status::OK();
  }

  Status FinalizeUpgradeFromMixedMode() {
    LOG(INFO) << "Restarting all other yb-tservers in current version";

    auto new_version_tserver = cluster_->tablet_server(kNewVersionTserver);
    for (auto* tserver : cluster_->tserver_daemons()) {
      if (tserver == new_version_tserver) {
        continue;
      }
      RETURN_NOT_OK(
          RestartTServerInCurrentVersion(*tserver, /*wait_for_cluster_to_stabilize=*/false));
    }

    RETURN_NOT_OK(WaitForClusterToStabilize());
    RETURN_NOT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));
    RETURN_NOT_OK(FinalizeUpgrade());

    return Status::OK();
  }

  Status SetupTables() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    return conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTableName);
  }

  Status DoReadWriteWorkload() {
    TestThreadHolder thread_holder;
    for (int tserver_idx = 0; tserver_idx < kNumServers; ++tserver_idx) {
      thread_holder.AddThreadFunctor([this, tserver_idx]() {
        auto conn = ASSERT_RESULT(cluster_->ConnectToDB("yugabyte", tserver_idx));
        for (int i = 0; i < 100; ++i) {
          auto v = RandomUniformInt<int>();
          ASSERT_OK(conn.ExecuteFormat(
              "INSERT INTO $0 VALUES ($1, $2) ON CONFLICT (k) DO UPDATE SET v = $2",
              kTableName, i, v));
          ASSERT_OK(conn.FetchFormat("SELECT v FROM $0 WHERE k = $1", kTableName, i));
        }
      });
    }
    thread_holder.JoinAll();
    return Status::OK();
  }

 private:
  static constexpr auto kNewVersionTserver = 0;
  static constexpr auto kTableName = "test_table";
  static constexpr auto kNumServers = 3;
};

// Verify that the new mechanism to propagate ASH metadata doesn't break existing functionality.
TEST_F(AshMetadataUpgradeTest, TestMixedCluster) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(SetupTables());
  ASSERT_OK(DoReadWriteWorkload()); // do read write before upgrade
  ASSERT_OK(UpgradeClusterToMixedMode());
  ASSERT_OK(DoReadWriteWorkload()); // do read write duriung upgrade
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  ASSERT_OK(DoReadWriteWorkload()); // do read write after upgrade
}

} // namespace yb
