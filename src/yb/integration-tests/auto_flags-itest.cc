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

#include "yb/common/wire_protocol.h"

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

#include "yb/util/flags.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/version_info.h"

DECLARE_bool(TEST_auto_flags_initialized);
DECLARE_bool(TEST_auto_flags_new_install);
DECLARE_bool(disable_auto_flags_management);
DECLARE_int32(limit_auto_flag_promote_for_new_universe);
DECLARE_int32(heartbeat_interval_ms);

// Required for tests with AutoFlags management disabled
DISABLE_PROMOTE_ALL_AUTO_FLAGS_FOR_TEST;

using std::string;

namespace yb {

using OK = Status::OK;

const string kDisableAutoFlagsManagementFlagName = "disable_auto_flags_management";
const string kTESTAutoFlagsInitializedFlagName = "TEST_auto_flags_initialized";
const string kTESTAutoFlagsNewInstallFlagName  = "TEST_auto_flags_new_install";
const string kTrue = "true";
const string kFalse = "false";
const MonoDelta kTimeout = 20s * kTimeMultiplier;
const int kNumMasterServers = 3;
const int kNumTServers = 3;

namespace {

size_t CountFlag(
    const std::string& flag_name,
    decltype(std::declval<AutoFlagsConfigPB>().promoted_flags()) promoted_flags) {
  size_t count = 0;
  for (const auto& per_process_flags : promoted_flags) {
    for (const auto& flag : per_process_flags.flags()) {
      count += (flag == flag_name);
    }
  }
  return count;
}

void TestPromote(
    std::function<Result<AutoFlagsConfigPB>()> get_current_config,
    std::function<Result<master::PromoteAutoFlagsResponsePB>(master::PromoteAutoFlagsRequestPB)>
        promote_auto_flags,
    std::function<Status(uint32)>
        validate_config_on_all_nodes) {
  // Initial empty config.
  auto previous_config = ASSERT_RESULT(get_current_config());
  ASSERT_EQ(0, previous_config.config_version());
  ASSERT_EQ(0, previous_config.promoted_flags_size());

  master::PromoteAutoFlagsRequestPB req;
  req.set_max_flag_class(ToString(AutoFlagClass::kExternal));
  req.set_promote_non_runtime_flags(true);
  req.set_force(false);

  // Promote all AutoFlags.
  auto resp = ASSERT_RESULT(promote_auto_flags(req));
  ASSERT_TRUE(resp.has_new_config_version());
  ASSERT_EQ(resp.new_config_version(), previous_config.config_version() + 1);

  ASSERT_OK(validate_config_on_all_nodes(resp.new_config_version()));
  previous_config = ASSERT_RESULT(get_current_config());
  ASSERT_EQ(resp.new_config_version(), previous_config.config_version());
  ASSERT_GE(previous_config.promoted_flags_size(), 1);
  ASSERT_EQ(0, CountFlag(kTESTAutoFlagsNewInstallFlagName, previous_config.promoted_flags()));
  ASSERT_EQ(
      previous_config.promoted_flags_size(),
      CountFlag(kTESTAutoFlagsInitializedFlagName, previous_config.promoted_flags()));

  // Running again should be no op.
  auto result = promote_auto_flags(req);
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsAlreadyPresent()) << result.status().ToString();

  // Force to bump up version alone, make sure flags still have expected values.
  req.set_force(true);
  resp.Clear();
  resp = ASSERT_RESULT(promote_auto_flags(req));
  ASSERT_FALSE(resp.non_runtime_flags_promoted());
  ASSERT_OK(validate_config_on_all_nodes(resp.new_config_version()));
  previous_config = ASSERT_RESULT(get_current_config());
  ASSERT_EQ(resp.new_config_version(), previous_config.config_version());
  ASSERT_EQ(0, CountFlag(kTESTAutoFlagsNewInstallFlagName, previous_config.promoted_flags()));
  ASSERT_EQ(
      previous_config.promoted_flags_size(),
      CountFlag(kTESTAutoFlagsInitializedFlagName, previous_config.promoted_flags()));

  // Verify it is not possible to promote with AutoFlagClass::kNewInstallsOnly.
  req.set_max_flag_class(ToString(AutoFlagClass::kNewInstallsOnly));
  result = promote_auto_flags(req);
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsInvalidArgument()) << result.status().ToString();
}

}  // namespace

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
    auto leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    const AutoFlagsConfigPB leader_config = leader_master->master()->GetAutoFlagsConfig();
    const auto leader_config_process_count =
        static_cast<size_t>(leader_config.promoted_flags().size());
    SCHECK_EQ(
        CountFlag(kTESTAutoFlagsInitializedFlagName, leader_config.promoted_flags()),
        leader_config_process_count, IllegalState, "Unable to find");
    SCHECK_EQ(
        CountFlag(kTESTAutoFlagsNewInstallFlagName, leader_config.promoted_flags()),
        leader_config_process_count, IllegalState, "Unable to find");

    if (FLAGS_disable_auto_flags_management) {
      SCHECK(
          !FLAGS_TEST_auto_flags_initialized, IllegalState,
          "TEST_auto_flags_initialized should not be set");
      SCHECK(
          !FLAGS_TEST_auto_flags_new_install, IllegalState,
          "TEST_auto_flags_new_install should not be set");
    } else {
      SCHECK(
          FLAGS_TEST_auto_flags_initialized, IllegalState,
          "TEST_auto_flags_initialized should be set");
      SCHECK(
          FLAGS_TEST_auto_flags_new_install, IllegalState,
          "TEST_auto_flags_new_install should be set");
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
  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master();

  ASSERT_NO_FATALS(TestPromote(
      /* get_current_config */
      [&]() { return leader_master->GetAutoFlagsConfig(); },
      /* promote_auto_flags */
      [&](const auto& req) -> Result<master::PromoteAutoFlagsResponsePB> {
        master::PromoteAutoFlagsResponsePB resp;
        RETURN_NOT_OK(leader_master->catalog_manager_impl()->PromoteAutoFlags(&req, &resp));
        return resp;
      },
      /* validate_config_on_all_nodes */
      [&](uint32_t config_version) {
        RETURN_NOT_OK(ValidateConfigOnMasters(config_version));
        return ValidateConfigOnTservers(config_version);
      }));
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
  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsNewInstallFlagName, kTrue));

  ExternalMaster* new_master = nullptr;
  cluster_->StartShellMaster(&new_master);

  OpIdPB op_id;
  ASSERT_OK(cluster_->GetLastOpIdForLeader(&op_id));
  ASSERT_OK(cluster_->ChangeConfig(new_master, consensus::ADD_SERVER));
  ASSERT_OK(cluster_->WaitForMastersToCommitUpTo(op_id.index()));

  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kTrue, new_master));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kTrue, new_master));

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers + 1, kTimeout));

  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kTrue));
  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsNewInstallFlagName, kTrue));

  for (auto* master : cluster_->master_daemons()) {
    master->Shutdown();
    ASSERT_OK(master->Restart());
    ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kTrue, master));
    ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kTrue, master));
    ASSERT_EQ(GetAutoFlagConfigVersion(master), 1);
  }

  for (auto* tserver : cluster_->tserver_daemons()) {
    tserver->Shutdown();
    ASSERT_OK(tserver->Restart());
    ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kTrue, tserver));
    ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kTrue, tserver));
    ASSERT_EQ(GetAutoFlagConfigVersion(tserver), 1);
  }
}

// Create a Cluster with AutoFlags management turned off to simulate a cluster running old code.
// Restart the cluster with AutoFlags management enabled to simulate an upgrade. Make sure nodes
// added to this cluster works as expected. And finally make sure Promotion after the upgrade works.
TEST_F(AutoFlagsExternalMiniClusterTest, UpgradeCluster) {
  string disable_auto_flag_management = "--" + kDisableAutoFlagsManagementFlagName;
  ASSERT_NO_FATALS(BuildAndStart(
      {disable_auto_flag_management} /* ts_flags */,
      {disable_auto_flag_management} /* master_flags */));

  ASSERT_OK(CheckFlagOnAllNodes(kDisableAutoFlagsManagementFlagName, kTrue));
  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kFalse));
  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsNewInstallFlagName, kFalse));

  // Remove the disable_auto_flag_management flag from cluster config
  ASSERT_TRUE(Erase(disable_auto_flag_management, cluster_->mutable_extra_master_flags()));
  ASSERT_TRUE(Erase(disable_auto_flag_management, cluster_->mutable_extra_tserver_flags()));

  ASSERT_OK(cluster_->AddTabletServer());
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers + 1, kTimeout));

  // Add a new tserver
  auto* new_tserver = cluster_->tablet_server(cluster_->num_tablet_servers() - 1);
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_tserver));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_tserver));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kFalse, new_tserver));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_tserver), 0);

  // Restart the new tserver
  new_tserver->Shutdown();
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers, kTimeout));
  ASSERT_OK(new_tserver->Restart());
  ASSERT_OK(cluster_->WaitForTabletServerCount(opts_.num_tablet_servers + 1, kTimeout));
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_tserver));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_tserver));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kFalse, new_tserver));
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
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kFalse, new_master));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_master), 0);

  // Restart the master
  new_master->Shutdown();
  ASSERT_OK(new_master->Restart());
  ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, new_master));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsInitializedFlagName, kFalse, new_master));
  ASSERT_OK(CheckFlagOnNode(kTESTAutoFlagsNewInstallFlagName, kFalse, new_master));
  ASSERT_EQ(GetAutoFlagConfigVersion(new_master), 0);

  // Remove disable_auto_flag_management from each process config and restart
  for (auto* master : cluster_->master_daemons()) {
    Erase(disable_auto_flag_management, master->mutable_flags());

    master->Shutdown();
    ASSERT_OK(master->Restart());
    ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, master));
    const auto config_version = GetAutoFlagConfigVersion(master);
    ASSERT_EQ(config_version, 0);
  }

  for (auto* tserver : cluster_->tserver_daemons()) {
    Erase(disable_auto_flag_management, tserver->mutable_flags());

    tserver->Shutdown();
    ASSERT_OK(tserver->Restart());
    ASSERT_OK(CheckFlagOnNode(kDisableAutoFlagsManagementFlagName, kFalse, tserver));
    const auto config_version = GetAutoFlagConfigVersion(tserver);
    ASSERT_EQ(config_version, 0);
  }

  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsInitializedFlagName, kFalse));
  ASSERT_OK(CheckFlagOnAllNodes(kTESTAutoFlagsNewInstallFlagName, kFalse));

  ASSERT_NO_FATALS(TestPromote(
      /* get_current_config */
      [&]() -> Result<AutoFlagsConfigPB> {
        master::GetAutoFlagsConfigRequestPB req;
        master::GetAutoFlagsConfigResponsePB resp;
        rpc::RpcController rpc;
        rpc.set_timeout(kTimeout);
        EXPECT_OK(cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>().GetAutoFlagsConfig(
            req, &resp, &rpc));
        if (resp.has_error()) {
          return StatusFromPB(resp.error().status())
              .CloneAndPrepend(Format("Code $0", resp.error().code()));
        }

        return resp.config();
      },
      /* promote_auto_flags */
      [&](const auto& req) -> Result<master::PromoteAutoFlagsResponsePB> {
        auto master_proxy = cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>();
        master::PromoteAutoFlagsResponsePB resp;
        rpc::RpcController rpc;
        rpc.set_timeout(opts_.timeout);
        RETURN_NOT_OK(master_proxy.PromoteAutoFlags(req, &resp, &rpc));
        if (resp.has_error()) {
          return StatusFromPB(resp.error().status())
              .CloneAndPrepend(Format("Code $0", resp.error().code()));
        }

        return resp;
      },
      /* validate_config_on_all_nodes */
      [&](uint32_t config_version) -> Status {
        for (auto* daemon : cluster_->daemons()) {
          uint32_t version = 0;
          RETURN_NOT_OK(WaitFor(
              [&]() {
                version = GetAutoFlagConfigVersion(daemon);
                return version == config_version;
              },
              kTimeout, "Config version to be updated"));

          SCHECK_EQ(
              version, config_version, IllegalState,
              Format(
                  "Expected config version $0, got $1 on $2 $3", config_version, version,
                  daemon->exe(), daemon->id()));
        }

        return OK();
      }));
}

}  // namespace yb
