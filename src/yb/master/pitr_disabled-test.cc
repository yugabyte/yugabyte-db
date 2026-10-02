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

#include <set>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_handle.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"
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
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_bool(disable_pitr);
DECLARE_bool(enable_ysql);
DECLARE_bool(master_auto_run_initdb);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

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

  Status RestartMasters(bool request_disable) {
    std::vector<uint16_t> ports;
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      ports.push_back(cluster_->mini_master(i)->bound_rpc_addr().port());
    }
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      cluster_->mini_master(i)->Shutdown();
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = request_disable;
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      RETURN_NOT_OK(cluster_->mini_master(i)->StartDistributedMaster(ports));
    }
    RETURN_NOT_OK(WaitForInitDb(cluster_.get()));
    return Status::OK();
  }

  Status Eligibility() {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->master()
        ->snapshot_coordinator().CheckPitrDisableEligibility();
  }

  Status WriteMetadata(int8_t type, const std::string& id, const google::protobuf::Message& pb) {
    auto& cm = VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
    return cm.sys_catalog()->ForceWrite(
        type, id, pb, QLWriteRequestPB::QL_STMT_INSERT, cm.GetLeaderEpochInternal().leader_term);
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
  ASSERT_NOK_STR_CONTAINS(SetConfig(config), "Universe startup settings cannot be updated");
  config = ASSERT_RESULT(Config());
  config.set_is_initial_sys_catalog_snapshot(true);
  ASSERT_NOK_STR_CONTAINS(SetConfig(config), "Universe startup settings cannot be updated");
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

TEST_F(PitrEnabledTest, RuntimeFlagAndConfigChangesCannotActivateMode) {
  auto config = ASSERT_RESULT(Config());
  ASSERT_FALSE(config.pitr_disabled());
  config.set_pitr_disabled(true);
  ASSERT_NOK_STR_CONTAINS(SetConfig(config), "Universe startup settings cannot be updated");

  auto& catalog_manager = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->catalog_manager_impl();
  SysCatalogLoadingState state(catalog_manager.GetLeaderEpochInternal());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = true;
  const auto status = catalog_manager.VisitSysCatalog(&state);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_disable_pitr) = false;
  ASSERT_OK(status);
  ASSERT_FALSE(ASSERT_RESULT(Config()).pitr_disabled());
  ASSERT_OK(WaitForMode(false));
  ASSERT_RESULT(CreateSchedule());
}

TEST_F(PitrEnabledTest, ExistingUniverseActivatesAcrossCoordinatedMasterRestart) {
  const auto table_id = ASSERT_RESULT(CreateTable());
  client::TableHandle table;
  ASSERT_OK(table.Open(client::YBTableName(YQL_DATABASE_CQL, kNamespace, "t1"), client_.get()));
  auto session = client_->NewSession(kRpcTimeout);
  for (int key : {7, 42}) {
    auto write = table.NewInsertOp(session->arena());
    QLAddInt32HashValue(write->mutable_request(), key);
    ASSERT_OK(session->TEST_ApplyAndFlush(write));
  }
  auto check_rows = [&] {
    std::set<int32_t> keys;
    for (const auto& row : client::TableRange(table)) {
      keys.insert(row.column(0).int32_value());
    }
    ASSERT_EQ(keys, (std::set<int32_t>{7, 42}));
  };
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto snapshot = ASSERT_RESULT(snapshots.CreateSnapshot(table_id));
  ASSERT_OK(snapshots.RestoreSnapshot(snapshot));
  ASSERT_OK(WaitFor(
      [&] { return Eligibility().ok(); }, kRpcTimeout, "Wait for restore finalization"));
  ASSERT_NO_FATAL_FAILURE(check_rows());
  auto custom = ASSERT_RESULT(Config());
  custom.set_oid_cache_invalidations_count(123);
  ASSERT_OK(SetConfig(custom));
  const auto before = ASSERT_RESULT(Config());

  ASSERT_OK(RestartMasters(true));
  ASSERT_OK(WaitForMode(true));
  const auto after = ASSERT_RESULT(Config());
  ASSERT_TRUE(after.pitr_disabled());
  ASSERT_EQ(after.cluster_uuid(), before.cluster_uuid());
  ASSERT_EQ(after.universe_uuid(), before.universe_uuid());
  ASSERT_GT(after.version(), before.version());
  auto expected = before;
  expected.set_pitr_disabled(true);
  expected.set_version(after.version());
  ASSERT_EQ(after.SerializeAsString(), expected.SerializeAsString());
  ASSERT_NO_FATAL_FAILURE(check_rows());
  ASSERT_EQ(
      ASSERT_RESULT(client_->GetYBTableInfo(
          client::YBTableName(YQL_DATABASE_CQL, kNamespace, "t1"))).table_id, table_id);
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");

  ASSERT_OK(RestartMasters(false));
  ASSERT_OK(WaitForMode(true));
  ASSERT_EQ(ASSERT_RESULT(Config()).SerializeAsString(), expected.SerializeAsString());
  ASSERT_NO_FATAL_FAILURE(check_rows());
  const auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    auto* follower = cluster_->mini_master(i);
    if (follower != leader) {
      ASSERT_OK(cluster_->StepDownMasterLeader(follower->permanent_uuid()));
      break;
    }
  }
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");
}

TEST_F(PitrEnabledTest, StartupBlocksAdmissionBeforeModeCommit) {
  std::vector<HostPort> addresses;
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    addresses.push_back(cluster_->mini_master(i)->bound_rpc_addr());
  }
  CountDownLatch entered(1), release(1);
  Status restart_status;
  TestThreadHolder threads;
  auto* sync = SyncPoint::GetInstance();
  sync->SetCallBack("CatalogManager::DisablePitr:BeforeWrite", [&](void*) {
    entered.CountDown();
    release.Wait();
  });
  sync->EnableProcessing();
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    threads.JoinAll();
    sync->DisableProcessing();
    sync->ClearAllCallBacks();
  });
  threads.AddThread([&] { restart_status = RestartMasters(true); });
  ASSERT_TRUE(entered.WaitFor(kRpcTimeout));
  for (const auto& address : addresses) {
    MasterBackupProxy proxy(&cluster_->proxy_cache(), address);
    CreateSnapshotScheduleRequestPB request;
    request.mutable_options()->set_interval_sec(600);
    request.mutable_options()->set_retention_duration_sec(3600);
    auto* ns = request.mutable_options()->mutable_filter()->mutable_tables()
                   ->add_tables()->mutable_namespace_();
    ns->set_name(kNamespace);
    ns->set_database_type(YQL_DATABASE_CQL);
    CreateSnapshotScheduleResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(100ms * kTimeMultiplier);
    const auto status = proxy.CreateSnapshotSchedule(request, &response, &rpc);
    ASSERT_TRUE(!status.ok() || response.has_error()) << response.ShortDebugString();
  }
  release.CountDown();
  threads.JoinAll();
  ASSERT_OK(restart_status);
  ASSERT_OK(WaitForMode(true));
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is disabled for this universe");
}

TEST_F(PitrEnabledTest, PastPitrHistoryAllowsActivationAfterCleanup) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 100;
  ASSERT_RESULT(CreateTable());
  const auto schedule =
      ASSERT_RESULT(FullyDecodeSnapshotScheduleId(ASSERT_RESULT(CreateSchedule())));
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto snapshot = ASSERT_RESULT(snapshots.WaitScheduleSnapshot(schedule));
  const auto restore_at = HybridTime::FromPB(snapshot.entry().snapshot_hybrid_time());
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->Now() >= restore_at;
  }, kRpcTimeout, "Wait for restorable snapshot time"));
  ASSERT_OK(snapshots.RestoreSnapshotSchedule(schedule, restore_at));

  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<MasterBackupProxy>());
  DeleteSnapshotScheduleRequestPB request;
  request.set_snapshot_schedule_id(schedule.AsSlice().ToBuffer());
  DeleteSnapshotScheduleResponsePB response;
  rpc::RpcController rpc;
  rpc.set_timeout(kRpcTimeout);
  ASSERT_OK(proxy.DeleteSnapshotSchedule(request, &response, &rpc));
  ASSERT_FALSE(response.has_error()) << response.ShortDebugString();
  ASSERT_OK(WaitFor([&] { return Eligibility().ok(); },
                    60s * kTimeMultiplier, "Wait for PITR schedule and snapshot cleanup"));
  ASSERT_OK(RestartMasters(true));
  ASSERT_OK(WaitForMode(true));
  ASSERT_TRUE(ASSERT_RESULT(Config()).pitr_disabled());
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


TEST_F(PitrDisabledTest, IncompleteOrdinaryRestoreDoesNotBlockDisabledUniverseRestart) {
  const auto table_id = ASSERT_RESULT(CreateTable());
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto snapshot_id = ASSERT_RESULT(snapshots.CreateSnapshot(table_id));
  const auto snapshot_list = ASSERT_RESULT(snapshots.ListSnapshots(snapshot_id));
  ASSERT_EQ(snapshot_list.size(), 1);
  ASSERT_EQ(snapshot_list[0].entry().tablet_snapshots_size(), 1);
  for (size_t i = 0; i < cluster_->num_masters(); ++i) {
    cluster_->mini_master(i)->master()->snapshot_coordinator().Shutdown();
  }
  const auto restoration_id = TxnSnapshotRestorationId::GenerateRandom();
  SysRestorationEntryPB restoration;
  restoration.set_state(SysSnapshotEntryPB::RESTORING);
  restoration.set_snapshot_id(snapshot_id.AsSlice().ToBuffer());
  restoration.set_schedule_id(SnapshotScheduleId::Nil().AsSlice().ToBuffer());
  restoration.set_is_sys_catalog_restored(true);
  restoration.set_version(1);
  auto* tablet = restoration.add_tablet_restorations();
  tablet->set_id(snapshot_list[0].entry().tablet_snapshots(0).id());
  tablet->set_state(SysSnapshotEntryPB::RESTORING);
  ASSERT_OK(WriteMetadata(
      SysRowEntryType::SNAPSHOT_RESTORATION, restoration_id.AsSlice().ToBuffer(), restoration));
  ASSERT_NOK_STR_CONTAINS(Eligibility(), "unfinished or incomplete state");
  ASSERT_OK(RestartMasters(true));
  ASSERT_OK(WaitForMode(true));
  ASSERT_OK(snapshots.WaitRestorationInState(restoration_id, SysSnapshotEntryPB::RESTORED));
}

class PitrDisableEligibilityTest : public PitrEnabledTest {
 protected:
  void SetUp() override {
    ASSERT_NO_FATAL_FAILURE(PitrEnabledTest::SetUp());
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      cluster_->mini_master(i)->master()->snapshot_coordinator().Shutdown();
    }
  }
};

TEST_F(PitrDisableEligibilityTest, RetainedScheduleSnapshotBlocksActivation) {
  const auto id = TxnSnapshotId::GenerateRandom();
  SysSnapshotEntryPB snapshot;
  snapshot.set_state(SysSnapshotEntryPB::CREATING);
  snapshot.set_snapshot_hybrid_time(
      ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Now().ToUint64());
  snapshot.set_version(1);
  ASSERT_OK(WriteMetadata(SysRowEntryType::SNAPSHOT, id.AsSlice().ToBuffer(), snapshot));
  ASSERT_OK(Eligibility());
  snapshot.set_schedule_id(SnapshotScheduleId::GenerateRandom().AsSlice().ToBuffer());
  snapshot.set_version(2);
  ASSERT_OK(WriteMetadata(SysRowEntryType::SNAPSHOT, id.AsSlice().ToBuffer(), snapshot));
  ASSERT_NOK_STR_CONTAINS(Eligibility(), "retained schedule snapshot " + id.ToString());
}

TEST_F(PitrDisableEligibilityTest, RestorationLifecycleMustBeFinalized) {
  const auto id = TxnSnapshotRestorationId::GenerateRandom();
  SysRestorationEntryPB restoration;
  restoration.set_state(SysSnapshotEntryPB::RESTORING);
  restoration.set_snapshot_id(TxnSnapshotId::GenerateRandom().AsSlice().ToBuffer());
  restoration.set_schedule_id(SnapshotScheduleId::Nil().AsSlice().ToBuffer());
  auto write = [&]() {
    restoration.set_version(restoration.version() + 1);
    return WriteMetadata(
        SysRowEntryType::SNAPSHOT_RESTORATION, id.AsSlice().ToBuffer(), restoration);
  };
  ASSERT_OK(write());
  ASSERT_NOK_STR_CONTAINS(Eligibility(), "restoration " + id.ToString());
  restoration.set_is_sys_catalog_restored(true);
  auto* tablet = restoration.add_tablet_restorations();
  tablet->set_id("test_tablet");
  tablet->set_state(SysSnapshotEntryPB::RESTORED);
  ASSERT_OK(write());
  ASSERT_NOK_STR_CONTAINS(Eligibility(), "unfinished or incomplete state");

  restoration.set_complete_time_ht(
      ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Now().ToUint64());
  ASSERT_OK(write());
  ASSERT_OK(Eligibility());
  tablet->set_state(SysSnapshotEntryPB::FAILED);
  auto* pending = restoration.add_tablet_restorations();
  pending->set_id("pending_tablet");
  pending->set_state(SysSnapshotEntryPB::RESTORING);
  ASSERT_OK(write());
  ASSERT_NOK_STR_CONTAINS(Eligibility(), "unfinished or incomplete state");
  pending->set_state(SysSnapshotEntryPB::FAILED);
  ASSERT_OK(write());
  ASSERT_OK(Eligibility());

  restoration.set_schedule_id(SnapshotScheduleId::GenerateRandom().AsSlice().ToBuffer());
  ASSERT_OK(write());
  ASSERT_OK(Eligibility());
  restoration.set_is_sys_catalog_restored(false);
  ASSERT_OK(write());
  ASSERT_NOK_STR_CONTAINS(Eligibility(), "unfinished or incomplete state");
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

  options.extra_master_flags.clear();
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


class PitrExistingStartupTest : public YBTest, public ::testing::WithParamInterface<bool> {
 protected:
  void SetUp() override {
    YBTest::SetUp();
    options_.num_masters = 1;
    options_.num_tablet_servers = 0;
    options_.replication_factor = 1;
    options_.enable_ysql = true;
    options_.wait_for_tservers_to_accept_ysql_connections = false;
    options_.data_root = GetTestPath("existing-universe");
    ASSERT_OK(Start({}));
  }

  void TearDown() override {
    cluster_.reset();
    YBTest::TearDown();
  }

  Status Start(std::vector<std::string> flags) {
    cluster_.reset();
    flags.push_back("--snapshot_coordinator_poll_interval_ms=600000");
    options_.extra_master_flags = std::move(flags);
    cluster_ = std::make_unique<ExternalMiniCluster>(options_);
    auto status = cluster_->Start();
    if (cluster_->num_masters() == 1) {
      options_.master_rpc_ports = {cluster_->master(0)->bound_rpc_addr().port()};
    }
    return status;
  }

  Result<SysClusterConfigEntryPB> Config() {
    auto proxy = cluster_->GetLeaderMasterProxy<MasterClusterProxy>();
    GetMasterClusterConfigRequestPB request;
    GetMasterClusterConfigResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.GetMasterClusterConfig(request, &response, &rpc));
    if (response.has_error()) {
      return StatusFromPB(response.error().status());
    }
    return response.cluster_config();
  }

  ExternalMiniClusterOptions options_;
  std::unique_ptr<ExternalMiniCluster> cluster_;
};

TEST_P(PitrExistingStartupTest, RejectsActiveAndDeletedSchedulesWithoutChangingMode) {
  const auto before = ASSERT_RESULT(Config());
  ASSERT_FALSE(before.pitr_disabled());
  {
    auto proxy = cluster_->GetLeaderMasterProxy<MasterBackupProxy>();
    CreateSnapshotScheduleRequestPB request;
    request.mutable_options()->set_interval_sec(600);
    request.mutable_options()->set_retention_duration_sec(3600);
    auto* ns = request.mutable_options()->mutable_filter()->mutable_tables()
                   ->add_tables()->mutable_namespace_();
    ns->set_name("yugabyte");
    ns->set_database_type(YQL_DATABASE_PGSQL);
    CreateSnapshotScheduleResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(30s * kTimeMultiplier);
    ASSERT_OK(proxy.CreateSnapshotSchedule(request, &response, &rpc));
    ASSERT_FALSE(response.has_error()) << response.ShortDebugString();
    if (GetParam()) {
      DeleteSnapshotScheduleRequestPB remove;
      remove.set_snapshot_schedule_id(response.snapshot_schedule_id());
      DeleteSnapshotScheduleResponsePB removed;
      rpc.Reset();
      rpc.set_timeout(30s * kTimeMultiplier);
      ASSERT_OK(proxy.DeleteSnapshotSchedule(remove, &removed, &rpc));
      ASSERT_FALSE(removed.has_error()) << removed.ShortDebugString();
    }
  }

  ASSERT_NOK(Start({"--disable_pitr=true"}));
  ASSERT_OK(Start({}));
  const auto after = ASSERT_RESULT(Config());
  ASSERT_FALSE(after.pitr_disabled());
  ASSERT_EQ(after.cluster_uuid(), before.cluster_uuid());
  auto recovered = cluster_->GetLeaderMasterProxy<MasterBackupProxy>();
  ListSnapshotSchedulesRequestPB list;
  ListSnapshotSchedulesResponsePB listed;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(recovered.ListSnapshotSchedules(list, &listed, &rpc));
  ASSERT_FALSE(listed.has_error()) << listed.ShortDebugString();
  ASSERT_EQ(listed.schedules_size(), 1);
}

INSTANTIATE_TEST_CASE_P(ActiveOrDeleted, PitrExistingStartupTest, ::testing::Bool());

class PitrExistingCrashTest : public PitrExistingStartupTest {};

TEST_F(PitrExistingCrashTest, PersistedActivationSurvivesCrashAndFlaglessRestart) {
  const auto before = ASSERT_RESULT(Config());
  ASSERT_FALSE(before.pitr_disabled());
  ASSERT_NOK(Start({"--disable_pitr=true", "--TEST_fail_pitr_disable_after_persist=true"}));
  ASSERT_OK(Start({}));
  const auto after = ASSERT_RESULT(Config());
  ASSERT_TRUE(after.pitr_disabled());
  ASSERT_EQ(after.cluster_uuid(), before.cluster_uuid());
  ASSERT_GT(after.version(), before.version());
}


class PitrExistingWriteErrorTest : public PitrExistingStartupTest {};

TEST_P(PitrExistingWriteErrorTest, WriteFailureKeepsTheDurableOutcome) {
  const auto before = ASSERT_RESULT(Config());
  const bool committed = GetParam();
  ASSERT_NOK(Start({"--disable_pitr=true", committed
      ? "--TEST_pitr_disable_write_error=2" : "--TEST_pitr_disable_write_error=1"}));
  ASSERT_OK(Start({}));
  const auto after = ASSERT_RESULT(Config());
  auto expected = before;
  if (committed) {
    expected.set_pitr_disabled(true);
    expected.set_version(before.version() + 1);
  }
  ASSERT_EQ(after.SerializeAsString(), expected.SerializeAsString());
}

INSTANTIATE_TEST_CASE_P(BeforeOrAfter, PitrExistingWriteErrorTest, ::testing::Bool());

}  // namespace yb::master
