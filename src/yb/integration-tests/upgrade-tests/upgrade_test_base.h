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

#pragma once

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/version_info.pb.h"

namespace yb {

struct BuildInfo {
  std::string version;
  std::string build_number;
  std::string linux_debug_x86_url;
  std::string linux_release_x86_url;
  std::string darwin_debug_arm64_url;
  std::string darwin_release_arm64_url;
};

// Helper class to perform upgrades and rollback of Yugabyte DB.
// This test sets up a ExternalMini cluster on an older yb version and helps upgrade it to the
// current version, and rollback to the older version.
class UpgradeTestBase : public ExternalMiniClusterITestBase {
 public:
  explicit UpgradeTestBase(const std::string& from_version);
  virtual ~UpgradeTestBase() = default;

  void SetUp() override;

 protected:
  Status StartClusterInOldVersion();
  Status StartClusterInOldVersion(const ExternalMiniClusterOptions& options);

  Status UpgradeClusterToCurrentVersion(
      MonoDelta delay_between_nodes = 3s, bool auto_finalize = true);

  Status RestartAllMastersInCurrentVersion(MonoDelta delay_between_nodes = 3s);
  Status RestartMasterInCurrentVersion(
      ExternalMaster& master, bool wait_for_cluster_to_stabilize = true);
  Status RestartAllTServersInCurrentVersion(MonoDelta delay_between_nodes = 3s);
  Status RestartTServerInCurrentVersion(
      ExternalTabletServer& ts, bool wait_for_cluster_to_stabilize = true);

  Status FinalizeUpgrade();

  Status PromoteAutoFlags(AutoFlagClass flag_class = AutoFlagClass::kExternal);

  Status PerformYsqlUpgrade();

  Status RollbackClusterToOldVersion(MonoDelta delay_between_nodes = 3s);

  Status RestartAllMastersInOldVersion(MonoDelta delay_between_nodes = 3s);
  Status RestartMasterInOldVersion(
      ExternalMaster& master, bool wait_for_cluster_to_stabilize = true);

  Status RestartAllTServersInOldVersion(MonoDelta delay_between_nodes = 3s);
  Status RestartTServerInOldVersion(
      ExternalTabletServer& ts, bool wait_for_cluster_to_stabilize = true);

  Status RollbackVolatileAutoFlags();

  // Wait for the cluster to stabilize after an upgrade or rollback.
  // Waits for all tservers to register with the master leader, which happens after all tablets have
  // been bootstrapped.
  Status WaitForClusterToStabilize();

  BuildInfo old_version_info() const { return old_version_info_; }
  // Can only be used after SetUp has been called.
  VersionInfoPB current_version_info() const { return current_version_info_; }
  TestThreadHolder test_thread_holder_;

 private:
  const BuildInfo old_version_info_;
  VersionInfoPB current_version_info_;

  std::string old_version_bin_path_, current_version_bin_path_;
  std::string old_version_master_bin_path_, current_version_master_bin_path_;
  std::string old_version_tserver_bin_path_, current_version_tserver_bin_path_;

  std::optional<uint32> auto_flags_rollback_version_;
};

// Supported builds
static constexpr auto kBuild_2_20_2_4 = "2.20.2.4";
static constexpr auto kBuild_2024_1_0_1 = "2024.1.0.1";

// Helper classes for specific versions
class TestUpgradeFrom_2_20_2_4 : public UpgradeTestBase {
 public:
  TestUpgradeFrom_2_20_2_4() : UpgradeTestBase(kBuild_2_20_2_4) {}
  virtual ~TestUpgradeFrom_2_20_2_4() = default;
};

class TestUpgradeFrom_2024_1_0_1 : public UpgradeTestBase {
 public:
  TestUpgradeFrom_2024_1_0_1() : UpgradeTestBase(kBuild_2024_1_0_1) {}
};

}  // namespace yb
