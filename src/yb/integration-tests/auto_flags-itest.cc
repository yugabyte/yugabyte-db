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

#include "yb/consensus/log.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/ts_itest-base.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/mini_master.h"
#include "yb/master/master.h"
#include "yb/server/server_base.proxy.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/client/auto_flags_manager.h"
#include "yb/util/auto_flags.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/version_info.h"

DECLARE_bool(TEST_auto_flags_initialized);
DECLARE_bool(disable_auto_flags_management);
DECLARE_int32(limit_auto_flag_promote_for_new_universe);
DECLARE_int32(heartbeat_interval_ms);

// Required for tests with AutoFlags management disabled
DISABLE_PROMOTE_ALL_AUTO_FLAGS_FOR_TEST;

using std::string;
using std::vector;

namespace yb {
using OK = Status::OK;
const string kDisableAutoFlagsManagementFlagName = "disable_auto_flags_management";
const string kTESTAutoFlagsInitializedFlagName = "TEST_auto_flags_initialized";
const string kTrue = "true";
const string kFalse = "false";
const MonoDelta kTimeout = 20s * kTimeMultiplier;
const int kNumMasterServers = 3;
const int kNumTServers = 3;

class AutoFlagsMiniClusterTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  void SetUp() override {}
  void TestBody() override {}

 public:
  Status RunSetUp() {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = kNumTServers;
    opts.num_masters = kNumMasterServers;
    cluster_.reset(new MiniCluster(opts));
    return cluster_->Start();
  }

  Status ValidateConfig() {
    int count_flags = 0;
    auto leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    const AutoFlagsConfigPB leader_config = leader_master->master()->GetAutoFlagsConfig();
    for (const auto& per_process_flags : leader_config.promoted_flags()) {
      auto it = std::find(
          per_process_flags.flags().begin(), per_process_flags.flags().end(),
          kTESTAutoFlagsInitializedFlagName);
      SCHECK(it != per_process_flags.flags().end(), IllegalState, "Unable to find");
      count_flags++;
    }

    if (FLAGS_disable_auto_flags_management) {
      SCHECK(
          !FLAGS_TEST_auto_flags_initialized, IllegalState,
          "TEST_auto_flags_initialized should not be set");
      SCHECK_EQ(
          count_flags, 0, IllegalState,
          "TEST_auto_flags_initialized should not be set in any process");
    } else {
      SCHECK(
          FLAGS_TEST_auto_flags_initialized, IllegalState,
          "TEST_auto_flags_initialized should be set");
      SCHECK_EQ(
          count_flags, leader_config.promoted_flags().size(), IllegalState,
          "TEST_auto_flags_initialized should be set in every process");
    }

    for (size_t i = 0; i < cluster_->num_masters(); i++) {
      auto master = cluster_->mini_master(i);
      const AutoFlagsConfigPB follower_config = master->master()->GetAutoFlagsConfig();
      SCHECK_EQ(
          follower_config.DebugString(), leader_config.DebugString(), IllegalState,
          Format(
              "Config of master follower $0 does not match leader $1", *master->master(),
              *leader_master->master()));
    }

    RETURN_NOT_OK(cluster_->AddTabletServer());
    RETURN_NOT_OK(cluster_->WaitForTabletServerCount(kNumTServers + 1));

    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      auto tserver = cluster_->mini_tablet_server(i);
      const AutoFlagsConfigPB tserver_config = tserver->server()->TEST_GetAutoFlagConfig();
      SCHECK_EQ(
          tserver_config.DebugString(), leader_config.DebugString(), IllegalState,
          Format(
              "Config of tserver $0 does not match leader $1", *tserver->server(),
              *leader_master->master()));
    }

    return OK();
  }

  Status WaitForAllLeaderOpsToApply() {
    auto leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->master();
    auto leader_peer = leader_master->catalog_manager()->tablet_peer();
    const auto last_op_id = leader_peer->log()->GetLatestEntryOpId();

    return WaitFor(
        [&]() -> Result<bool> {
          auto applied_op_id = leader_peer->raft_consensus()->GetAllAppliedOpId();
          return applied_op_id >= last_op_id;
        },
        kTimeout,
        Format("Wait OpId apply within $0 seconds failed", kTimeout.ToSeconds()));
  }

  Status ValidateConfigOnMasters(uint32_t expected_config_version) {
    RETURN_NOT_OK(WaitForAllLeaderOpsToApply());

    auto* leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->master();
    const auto leader_config = leader_master->GetAutoFlagsConfig();
    SCHECK_EQ(
        expected_config_version, leader_config.config_version(), IllegalState,
        Format("Invalid Config version leader master $0", *leader_master));

    for (size_t i = 0; i < cluster_->num_masters(); i++) {
      auto master = cluster_->mini_master(i);
      const auto follower_config = master->master()->GetAutoFlagsConfig();
      SCHECK_EQ(
          follower_config.DebugString(), leader_config.DebugString(), IllegalState,
          Format(
              "Config of master follower $0 does not match leader $1", *master->master(),
              *leader_master));
    }

    return OK();
  }

  Status ValidateConfigOnTservers(uint32_t expected_config_version) {
    auto leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->master();
    const auto master_config = leader_master->GetAutoFlagsConfig();
    SCHECK_EQ(
        expected_config_version, master_config.config_version(), IllegalState,
        Format("Invalid Config version leader master $0", *leader_master));
    LOG(INFO) << leader_master->ToString() << " AutoFlag config: " << master_config.DebugString();

    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      auto tserver = cluster_->mini_tablet_server(i);
      RETURN_NOT_OK(WaitFor(
          [&]() {
            const auto config = tserver->server()->TEST_GetAutoFlagConfig();
            LOG(INFO) << tserver->server()->ToString()
                      << " AutoFlag config: " << config.DebugString();
            return config.DebugString() == master_config.DebugString();
          },
          FLAGS_heartbeat_interval_ms * 3ms * kTimeMultiplier,
          "AutoFlags not propagated to all TServers"));
    }
    return OK();
  }
};

TEST_F(AutoFlagsMiniClusterTest, NewCluster) {
  ASSERT_OK(RunSetUp());
  ASSERT_OK(ValidateConfig());
}

TEST_F(AutoFlagsMiniClusterTest, DisableAutoFlagManagement) {
  FLAGS_disable_auto_flags_management = true;

  ASSERT_OK(RunSetUp());
  ASSERT_OK(ValidateConfig());
}

TEST_F(AutoFlagsMiniClusterTest, Promote) {
  // Start with an empty config
  FLAGS_limit_auto_flag_promote_for_new_universe = 0;
  ASSERT_OK(RunSetUp());

  // Initial empty config
  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master();
  auto previous_config = leader_master->GetAutoFlagsConfig();
  ASSERT_EQ(0, previous_config.config_version());
  ASSERT_EQ(0, previous_config.promoted_flags_size());

  master::PromoteAutoFlagsRequestPB req;
  master::PromoteAutoFlagsResponsePB resp;
  req.set_max_flag_class(ToString(AutoFlagClass::kExternal));
  req.set_promote_non_runtime_flags(true);
  req.set_force(false);

  // Promote all AutoFlags
  ASSERT_OK(leader_master->catalog_manager_impl()->PromoteAutoFlags(&req, &resp));
  ASSERT_TRUE(resp.has_new_config_version());
  ASSERT_EQ(resp.new_config_version(), previous_config.config_version() + 1);

  ASSERT_OK(ValidateConfigOnMasters(resp.new_config_version()));
  ASSERT_OK(ValidateConfigOnTservers(resp.new_config_version()));
  previous_config = leader_master->GetAutoFlagsConfig();
  ASSERT_EQ(resp.new_config_version(), previous_config.config_version());
  ASSERT_GE(previous_config.promoted_flags_size(), 1);

  // Running again should be no op
  resp.Clear();
  const auto status = leader_master->catalog_manager_impl()->PromoteAutoFlags(&req, &resp);
  ASSERT_TRUE(status.IsAlreadyPresent()) << status.ToString();
  ASSERT_FALSE(resp.has_new_config_version()) << resp.new_config_version();

  // Force to bump up version alone
  req.set_force(true);
  resp.Clear();
  ASSERT_OK(leader_master->catalog_manager_impl()->PromoteAutoFlags(&req, &resp));
  ASSERT_EQ(resp.new_config_version(), previous_config.config_version() + 1);
  ASSERT_FALSE(resp.non_runtime_flags_promoted());

  ASSERT_OK(ValidateConfigOnMasters(resp.new_config_version()));
  ASSERT_OK(ValidateConfigOnTservers(resp.new_config_version()));
}

TEST_F(AutoFlagsMiniClusterTest, CheckMissingFlag) {
  ASSERT_OK(RunSetUp());
  ASSERT_OK(ValidateConfig());

  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto auto_flags_manager = leader_master->master()->auto_flags_manager();

  auto config = auto_flags_manager->GetConfig();
  for (auto& promoted_flags : *config.mutable_promoted_flags()) {
    promoted_flags.add_flags("missing_flag");
  }
  config.set_config_version(config.config_version() + 1);

  auto s = auto_flags_manager->LoadFromConfig(config, ApplyNonRuntimeAutoFlags::kFalse);
  ASSERT_NOK(s);
  ASSERT_TRUE(s.ToString().find("missing_flag") != std::string::npos) << s;
  ASSERT_TRUE(s.ToString().find(VersionInfo::GetShortVersionString()) != std::string::npos) << s;
}

class AutoFlagsExternalMiniClusterTest : public ExternalMiniClusterITestBase {
 public:
  void BuildAndStart(
      const std::vector<string>& extra_ts_flags = std::vector<string>(),
      const std::vector<string>& extra_master_flags = std::vector<string>()) {
    ASSERT_NO_FATALS(
        StartCluster(extra_ts_flags, extra_master_flags, kNumTServers, kNumMasterServers));
  }

  void SetUpCluster(ExternalMiniClusterOptions* opts) override {
    ASSERT_NO_FATALS(ExternalMiniClusterITestBase::SetUpCluster(opts));
    opts_ = *opts;
  }

  Status CheckFlagOnNode(
      const string& flag_name, const string& expected_val, ExternalDaemon* daemon) {
    auto value = VERIFY_RESULT(daemon->GetFlag(flag_name));
    SCHECK_EQ(
        value, expected_val, IllegalState,
        Format("Invalid value for flag $0 in $1", flag_name, daemon->id()));
    return OK();
  }

  Status CheckFlagOnAllNodes(string flag_name, string expected_val) {
    for (auto* daemon : cluster_->daemons()) {
      RETURN_NOT_OK(CheckFlagOnNode(flag_name, expected_val, daemon));
    }
    return OK();
  }

  uint32_t GetAutoFlagConfigVersion(ExternalDaemon* daemon) {
    server::GetAutoFlagsConfigVersionRequestPB req;
    server::GetAutoFlagsConfigVersionResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(kTimeout);
    EXPECT_OK(cluster_->GetProxy<server::GenericServiceProxy>(daemon).GetAutoFlagsConfigVersion(
        req, &resp, &rpc));

    return resp.config_version();
  }

 protected:
  ExternalMiniClusterOptions opts_;
};

// Validate AutoFlags in new cluster and make sure it handles process restarts, and addition of
// new nodes.
TEST_F(AutoFlagsExternalMiniClusterTest, NewCluster) {
  ASSERT_NO_FATALS(BuildAndStart());

  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kTrue));

  ExternalMaster* new_master = nullptr;
  cluster_->StartShellMaster(&new_master);

  OpIdPB op_id;
  ASSERT_OK(cluster_->GetLastOpIdForLeader(&op_id));
  ASSERT_OK(cluster_->ChangeConfig(new_master, consensus::ADD_SERVER));
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(op_id.index()));

  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kTrue, new_master));

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers + 1, kTimeout));

  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kTrue));

  for (auto* master : cluster_->master_daemons()) {
    master->Shutdown();
    ASSERT_OK(master->Restart());
    ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kTrue, master));
    ASSERT_EQ(GetAutoFlagConfigVersion(master), 1);
  }

  for (auto* tserver : cluster_->tserver_daemons()) {
    tserver->Shutdown();
    ASSERT_OK(tserver->Restart());
    ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kTrue, tserver));
    ASSERT_EQ(GetAutoFlagConfigVersion(tserver), 1);
  }
}

namespace {
template <typename T>
void RemoveFromVector(vector<T>* collection, const T& val) {
  auto it = std::find(collection->begin(), collection->end(), val);
  if (it != collection->end()) {
    collection->erase(it);
  }
}
}  // namespace

// Create a Cluster with AutoFlags management turned off to simulate a cluster running old code.
// Restart the cluster with AutoFlags management enabled to simulate an upgrade. Make sure nodes
// added to this cluster works as expected.
TEST_F(AutoFlagsExternalMiniClusterTest, UpgradeCluster) {
  string disable_auto_flag_management = "--" + kDisableAutoFlagsManagementFlagName;
  ASSERT_NO_FATALS(BuildAndStart(
      {disable_auto_flag_management} /* ts_flags */,
      {disable_auto_flag_management} /* master_flags */));

  ASSERT_OK(CheckFlagOnAllNodes(kDisableAutoFlagsManagementFlagName, kTrue));
  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kFalse));

  // Remove the disable_auto_flag_management flag from cluster config
  RemoveFromVector(cluster_->mutable_extra_master_flags(), disable_auto_flag_management);
  RemoveFromVector(cluster_->mutable_extra_tserver_flags(), disable_auto_flag_management);

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers + 1, kTimeout));

  // Add a new tserver
  auto* new_tserver = cluster_->tablet_server(cluster_->num_tablet_servers() - 1);
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_tserver));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_tserver));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_tserver), 0);

  // Restart the new tserver
  new_tserver->Shutdown();
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers, kTimeout));
  ASSERT_OK(new_tserver->Restart());
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers + 1, kTimeout));
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_tserver));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_tserver));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_tserver), 0);

  // Add a new master
  ExternalMaster* new_master = nullptr;
  cluster_->StartShellMaster(&new_master);

  OpIdPB op_id;
  ASSERT_OK(cluster_->GetLastOpIdForLeader(&op_id));
  ASSERT_OK(cluster_->ChangeConfig(new_master, consensus::ADD_SERVER));
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(op_id.index()));
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_master));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_master));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_master), 0);

  // Restart the master
  new_master->Shutdown();
  ASSERT_OK(new_master->Restart());
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_master));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_master));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_master), 0);

  // Remove disable_auto_flag_management from each process config and restart
  for (auto* master : cluster_->master_daemons()) {
    RemoveFromVector(master->mutable_flags(), disable_auto_flag_management);

    master->Shutdown();
    ASSERT_OK(master->Restart());
    ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, master));
    const auto config_version = GetAutoFlagConfigVersion(master);
    if (master == new_master) {
      ASSERT_EQ(config_version, 0);
    } else {
      ASSERT_EQ(config_version, 1);
    }
  }

  for (auto* tserver : cluster_->tserver_daemons()) {
    RemoveFromVector(tserver->mutable_flags(), disable_auto_flag_management);

    tserver->Shutdown();
    ASSERT_OK(tserver->Restart());
    ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, tserver));
    ASSERT_OK(WaitFor(
        [&]() {
          const auto config_version = GetAutoFlagConfigVersion(tserver);
          return config_version == 1;
        },
        FLAGS_heartbeat_interval_ms * 3ms * kTimeMultiplier,
        Format("Wait for tserver $0 to reach config version 1", tserver->id())));
  }

  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kFalse));
}

}  // namespace yb
