// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"

#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_loading_state.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_initialization.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"

DECLARE_bool(disable_pitr);
DECLARE_bool(enable_ysql);
DECLARE_bool(master_auto_run_initdb);
DECLARE_bool(ysql_enable_auth_catalog_follower_reads);

using namespace std::literals;

namespace yb::master {

class PitrDisabledTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  static inline const MonoDelta kRpcTimeout = 30s * kTimeMultiplier;
  static constexpr auto kNamespace = "pitr_mode_test";

  virtual bool DisablePitr() const { return true; }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = DisablePitr();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;
    SetDefaultInitialSysCatalogSnapshotFlags();
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions options;
    options.num_masters = 3;
    options.num_tablet_servers = 1;
    cluster_ = std::make_unique<MiniCluster>(options);
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(WaitForInitDb(cluster_.get()));
    client_ = ASSERT_RESULT(cluster_->CreateClient());
    ASSERT_OK(client_->CreateNamespaceIfNotExists(kNamespace, YQL_DATABASE_CQL));
    ASSERT_OK(WaitForMode(DisablePitr()));
  }

  Result<SysClusterConfigEntryPB> Config() {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterClusterProxy>());
    GetMasterClusterConfigRequestPB request;
    GetMasterClusterConfigResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(proxy.GetMasterClusterConfig(request, &response, &rpc));
    if (response.has_error()) {
      return StatusFromPB(response.error().status());
    }
    return response.cluster_config();
  }

  Status SetConfig(const SysClusterConfigEntryPB& config) {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterClusterProxy>());
    ChangeMasterClusterConfigRequestPB request;
    *request.mutable_cluster_config() = config;
    ChangeMasterClusterConfigResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(proxy.ChangeMasterClusterConfig(request, &response, &rpc));
    return response.has_error() ? StatusFromPB(response.error().status()) : Status::OK();
  }

  Result<std::string> CreateSchedule(
      YQLDatabase type = YQL_DATABASE_CQL, const std::string& name = kNamespace) {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterBackupProxy>());
    CreateSnapshotScheduleRequestPB request;
    auto* options = request.mutable_options();
    options->set_interval_sec(600);
    options->set_retention_duration_sec(3600);
    auto* ns = options->mutable_filter()->mutable_tables()->add_tables()->mutable_namespace_();
    ns->set_database_type(type);
    ns->set_name(name);
    CreateSnapshotScheduleResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(proxy.CreateSnapshotSchedule(request, &response, &rpc));
    if (response.has_error()) {
      return StatusFromPB(response.error().status());
    }
    return response.snapshot_schedule_id();
  }

  Status WaitForMode(bool disabled) {
    return WaitFor([&] {
      for (size_t i = 0; i < cluster_->num_masters(); ++i) {
        if (cluster_->mini_master(i)->master()->snapshot_coordinator().PitrDisabled() != disabled) {
          return false;
        }
      }
      return true;
    }, kRpcTimeout, "Wait for persisted PITR mode on every master");
  }

  Result<TableId> CreateTable() {
    client::YBSchemaBuilder builder;
    builder.AddColumn("k")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    client::YBSchema schema;
    RETURN_NOT_OK(builder.Build(&schema));
    const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t1");
    RETURN_NOT_OK(client_->NewTableCreator()->table_name(table_name).schema(&schema)
                      .num_tablets(1).wait(true).Create());
    return VERIFY_RESULT(client_->GetYBTableInfo(table_name)).table_id;
  }

  std::unique_ptr<client::YBClient> client_;
};

TEST_F(PitrDisabledTest, RejectsYsqlAndYcqlPitr) {
  ASSERT_TRUE(ASSERT_RESULT(Config()).pitr_disabled());
  ASSERT_FALSE(ASSERT_RESULT(Config()).is_initial_sys_catalog_snapshot());
  ASSERT_FALSE(FLAGS_ysql_enable_auth_catalog_follower_reads);
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");
  ASSERT_NOK_STR_CONTAINS(
      CreateSchedule(YQL_DATABASE_PGSQL, "yugabyte"), "PITR is disabled for this universe");

  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<MasterBackupProxy>());
  RestoreSnapshotScheduleRequestPB request;
  request.set_snapshot_schedule_id(SnapshotScheduleId::GenerateRandom().AsSlice().ToBuffer());
  request.set_restore_ht(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Now().ToUint64());
  RestoreSnapshotScheduleResponsePB response;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  ASSERT_OK(proxy.RestoreSnapshotSchedule(request, &response, &rpc));
  ASSERT_TRUE(response.has_error());
  ASSERT_TRUE(StatusFromPB(response.error().status()).IsNotSupported());
  ASSERT_STR_CONTAINS(response.error().status().message(), "PITR is disabled for this universe");
}

TEST_F(PitrDisabledTest, PersistsAcrossFailoverAndRestart) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = false;
  ASSERT_OK(SET_FLAG(ysql_enable_auth_catalog_follower_reads, false));
  const auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  std::string target;
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    auto* follower = cluster_->mini_master(i);
    if (follower != leader) {
      ASSERT_OK(follower->Restart());
      ASSERT_TRUE(follower->master()->snapshot_coordinator().PitrDisabled());
      target = follower->permanent_uuid();
      break;
    }
  }
  ASSERT_FALSE(target.empty());
  ASSERT_OK(cluster_->StepDownMasterLeader(target));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid() == target;
  }, kRpcTimeout, "Wait for chosen master leader"));
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");
  ASSERT_OK(cluster_->RestartSync());
  ASSERT_OK(WaitForMode(true));
  ASSERT_TRUE(ASSERT_RESULT(Config()).pitr_disabled());
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");
}

TEST_F(PitrDisabledTest, ClusterConfigCannotClearMode) {
  auto config = ASSERT_RESULT(Config());
  config.clear_pitr_disabled();
  ASSERT_OK(SetConfig(config));
  config = ASSERT_RESULT(Config());
  ASSERT_TRUE(config.pitr_disabled());
  ASSERT_OK(WaitForMode(true));

  config.set_pitr_disabled(false);
  ASSERT_NOK_STR_CONTAINS(SetConfig(config), "Universe creation settings cannot be updated");
  config = ASSERT_RESULT(Config());
  config.set_is_initial_sys_catalog_snapshot(true);
  ASSERT_NOK_STR_CONTAINS(SetConfig(config), "Universe creation settings cannot be updated");
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");
}

TEST_F(PitrDisabledTest, OrdinaryBackupSnapshotsRemainAllowed) {
  const auto table_id = ASSERT_RESULT(CreateTable());
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto snapshot_id = ASSERT_RESULT(snapshots.CreateSnapshot(table_id));
  ASSERT_OK(snapshots.RestoreSnapshot(snapshot_id));
  ASSERT_RESULT(snapshots.CreateSnapshot(table_id));
  ASSERT_TRUE(ASSERT_RESULT(Config()).pitr_disabled());
}

class PitrEnabledTest : public PitrDisabledTest {
 protected:
  bool DisablePitr() const override { return false; }
};

TEST_F(PitrEnabledTest, DefaultModeSupportsPitrRestore) {
  ASSERT_FALSE(ASSERT_RESULT(Config()).pitr_disabled());
  ASSERT_RESULT(CreateTable());
  const auto schedule_bytes = ASSERT_RESULT(CreateSchedule());
  const auto schedule_id = ASSERT_RESULT(FullyDecodeSnapshotScheduleId(schedule_bytes));
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto snapshot = ASSERT_RESULT(snapshots.WaitScheduleSnapshot(schedule_id));
  const auto restore_at = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->Now() >= restore_at;
  }, kRpcTimeout, "Wait for the first restorable snapshot time"));
  ASSERT_OK(snapshots.RestoreSnapshotSchedule(schedule_id, restore_at));
  ASSERT_OK(WaitForMode(false));
  ASSERT_FALSE(ASSERT_RESULT(Config()).pitr_disabled());
}

TEST_F(PitrEnabledTest, CannotEnableModeOnExistingUniverse) {
  auto config = ASSERT_RESULT(Config());
  ASSERT_FALSE(config.pitr_disabled());
  config.set_pitr_disabled(true);
  ASSERT_NOK_STR_CONTAINS(SetConfig(config), "Universe creation settings cannot be updated");

  // Exercise the startup loader directly: a rejected leader startup otherwise calls LOG(FATAL).
  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  SysCatalogLoadingState state(catalog_manager.GetLeaderEpochInternal());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = true;
  const auto status = catalog_manager.VisitSysCatalog(&state);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = false;
  ASSERT_TRUE(status.IsNotSupported()) << status;
  ASSERT_STR_CONTAINS(
      status.ToString(), "--disable_pitr can only be set when creating a new universe");
  ASSERT_FALSE(ASSERT_RESULT(Config()).pitr_disabled());
  ASSERT_OK(WaitForMode(false));
  ASSERT_RESULT(CreateSchedule());
}

TEST_F(PitrEnabledTest, RejectsLegacyReservationMetadataBeforeAndAfterReplay) {
  auto* master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->master();
  auto& catalog_manager = *master->catalog_manager_impl();
  auto* sys_catalog = catalog_manager.sys_catalog();
  const auto epoch = catalog_manager.GetLeaderEpochInternal();
  const auto tablet = ASSERT_RESULT(sys_catalog->Tablet());
  scoped_refptr<SysConfigInfo> legacy =
      new SysConfigInfo("ysql_catalog_follower_read_reservation");
  {
    auto lock = legacy->LockForWrite();
    // Removed field 4 contained a message with reserved=true at field 1.
    ASSERT_TRUE(lock.mutable_data()->pb.ParseFromString("\x22\x02\x08\x01"));
    ASSERT_OK(sys_catalog->Upsert(epoch, legacy));
    lock.Commit();
  }
  const auto bootstrap_status = master->snapshot_coordinator().Load(tablet.get());
  SysCatalogLoadingState state(epoch);
  const auto leader_status = catalog_manager.VisitSysCatalog(&state);
  {
    auto lock = legacy->LockForWrite();
    ASSERT_OK(sys_catalog->Delete(epoch, legacy));
    lock.Commit();
  }
  SysCatalogLoadingState clean_state(epoch);
  ASSERT_OK(catalog_manager.VisitSysCatalog(&clean_state));
  ASSERT_NOK_STR_CONTAINS(bootstrap_status, "old catalog follower-read reservation prototype");
  ASSERT_NOK_STR_CONTAINS(leader_status, "old catalog follower-read reservation prototype");
}


class PitrDisabledBootstrapTest : public YBTest, public ::testing::WithParamInterface<bool> {};

TEST_P(PitrDisabledBootstrapTest, ModeSurvivesBootstrapCrash) {
  ExternalMiniClusterOptions options;
  options.num_masters = 3;
  options.num_tablet_servers = 1;
  options.replication_factor = 1;
  options.enable_ysql = true;
  options.allow_crashes_during_init_db = true;
  options.extra_master_flags.push_back("--disable_pitr=true");
  options.extra_master_flags.push_back(
      GetParam() ? "--TEST_fail_initdb_after_cluster_config=true"
                 : "--TEST_fail_initdb_after_snapshot_restore=true");
  ExternalMiniCluster cluster(options);
  ASSERT_OK(cluster.Start());

  size_t crashed = 0;
  for (size_t i = 0; i < cluster.num_masters(); ++i) {
    crashed += !cluster.master(i)->IsProcessAlive();
  }
  ASSERT_GT(crashed, 0);
  auto proxy = cluster.GetLeaderMasterProxy<MasterClusterProxy>();
  GetMasterClusterConfigRequestPB request;
  GetMasterClusterConfigResponsePB response;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(proxy.GetMasterClusterConfig(request, &response, &rpc));
  ASSERT_FALSE(response.has_error()) << response.ShortDebugString();
  ASSERT_TRUE(response.cluster_config().pitr_disabled());
  ASSERT_FALSE(response.cluster_config().is_initial_sys_catalog_snapshot());
}

INSTANTIATE_TEST_CASE_P(SnapshotOrConfig, PitrDisabledBootstrapTest, ::testing::Bool());

class PitrDisabledFlaglessBootstrapTest : public YBTest {};

TEST_F(PitrDisabledFlaglessBootstrapTest, CommittedModeSurvivesFlaglessInitdbRetry) {
  ExternalMiniClusterOptions options;
  options.num_masters = 1;
  options.num_tablet_servers = 0;
  options.replication_factor = 1;
  options.enable_ysql = true;
  options.wait_for_tservers_to_accept_ysql_connections = false;
  options.data_root = GetTestPath("flagless-bootstrap");
  options.extra_master_flags = {
      "--disable_pitr=true", "--TEST_fail_initdb_after_cluster_config=true"};
  {
    ExternalMiniCluster interrupted(options);
    ASSERT_NOK(interrupted.Start());
    ASSERT_EQ(interrupted.num_masters(), 1);
    ASSERT_OK(WaitFor([&] { return !interrupted.master(0)->IsProcessAlive(); },
                      30s * kTimeMultiplier, "Wait for the post-config bootstrap crash"));
    options.master_rpc_ports = {interrupted.master(0)->bound_rpc_addr().port()};
  }

  options.extra_master_flags = {"--ysql_enable_auth_catalog_follower_reads=true"};
  ExternalMiniCluster recovered(options);
  ASSERT_OK(recovered.Start());
  auto proxy = recovered.GetLeaderMasterProxy<MasterClusterProxy>();
  GetMasterClusterConfigRequestPB config_request;
  GetMasterClusterConfigResponsePB config_response;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(proxy.GetMasterClusterConfig(config_request, &config_response, &rpc));
  ASSERT_FALSE(config_response.has_error()) << config_response.ShortDebugString();
  ASSERT_TRUE(config_response.cluster_config().pitr_disabled());
  ASSERT_FALSE(config_response.cluster_config().is_initial_sys_catalog_snapshot());

  GetYsqlAuthCatalogReadTimeRequestPB auth_request;
  GetYsqlAuthCatalogReadTimeResponsePB auth_response;
  rpc.Reset();
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(proxy.GetYsqlAuthCatalogReadTime(auth_request, &auth_response, &rpc));
  ASSERT_FALSE(auth_response.has_error()) << auth_response.ShortDebugString();
  ASSERT_TRUE(auth_response.has_read_time());

  auto backup = recovered.GetLeaderMasterProxy<MasterBackupProxy>();
  CreateSnapshotScheduleRequestPB schedule_request;
  auto* schedule_options = schedule_request.mutable_options();
  schedule_options->set_interval_sec(600);
  schedule_options->set_retention_duration_sec(3600);
  auto* ns =
      schedule_options->mutable_filter()->mutable_tables()->add_tables()->mutable_namespace_();
  ns->set_database_type(YQL_DATABASE_PGSQL);
  ns->set_name("yugabyte");
  CreateSnapshotScheduleResponsePB schedule_response;
  rpc.Reset();
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(backup.CreateSnapshotSchedule(schedule_request, &schedule_response, &rpc));
  ASSERT_TRUE(schedule_response.has_error());
  ASSERT_STR_CONTAINS(
      schedule_response.error().status().message(), "PITR is disabled for this universe");
}


}  // namespace yb::master
