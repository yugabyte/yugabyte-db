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

#include <tuple>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"

#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/mini_master.h"

#include "yb/tserver/service_util.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_thread_holder.h"

DECLARE_bool(ysql_enable_catalog_follower_read_reservation);
DECLARE_uint64(snapshot_coordinator_cleanup_delay_ms);

using namespace std::literals;

namespace yb::master {

class CatalogFollowerReadReservationTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  static inline const MonoDelta kRpcTimeout = 30s * kTimeMultiplier;

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_catalog_follower_read_reservation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_cleanup_delay_ms) = 3600000;
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions options;
    options.num_masters = 3;
    options.num_tablet_servers = 1;
    cluster_ = std::make_unique<MiniCluster>(options);
    ASSERT_OK(cluster_->Start());
    client::YBClientBuilder builder;
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      builder.add_master_server_addr(cluster_->mini_master(i)->bound_rpc_addr_str());
    }
    client_ = ASSERT_RESULT(builder.Build());
    ASSERT_OK(client_->CreateNamespaceIfNotExists(kNamespace, YQL_DATABASE_CQL));
  }

  Result<GetYsqlCatalogFollowerReadReservationResponsePB> ReservationStatus() {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterClusterProxy>());
    GetYsqlCatalogFollowerReadReservationRequestPB request;
    GetYsqlCatalogFollowerReadReservationResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(kRpcTimeout);
    RETURN_NOT_OK(proxy.GetYsqlCatalogFollowerReadReservation(request, &response, &rpc));
    if (response.has_error()) {
      return StatusFromPB(response.error().status());
    }
    return response;
  }

  Status Reserve(bool acknowledge = true, MonoDelta timeout = kRpcTimeout) {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterClusterProxy>());
    ReserveYsqlCatalogFollowerReadsRequestPB request;
    request.set_acknowledge_permanent_pitr_exclusion(acknowledge);
    ReserveYsqlCatalogFollowerReadsResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(timeout);
    RETURN_NOT_OK(proxy.ReserveYsqlCatalogFollowerReads(request, &response, &rpc));
    return response.has_error() ? StatusFromPB(response.error().status()) : Status::OK();
  }

  Result<std::string> CreateSchedule(MonoDelta timeout = kRpcTimeout) {
    auto proxy = VERIFY_RESULT(cluster_->GetLeaderMasterProxy<MasterBackupProxy>());
    CreateSnapshotScheduleRequestPB request;
    auto* options = request.mutable_options();
    options->set_interval_sec(600);
    options->set_retention_duration_sec(3600);
    auto* ns = options->mutable_filter()->mutable_tables()->add_tables()->mutable_namespace_();
    ns->set_database_type(YQL_DATABASE_CQL);
    ns->set_name(kNamespace);
    CreateSnapshotScheduleResponsePB response;
    rpc::RpcController rpc;
    rpc.set_timeout(timeout);
    RETURN_NOT_OK(proxy.CreateSnapshotSchedule(request, &response, &rpc));
    if (response.has_error()) {
      return StatusFromPB(response.error().status());
    }
    return response.snapshot_schedule_id();
  }

  Status WaitForReservation() {
    return WaitFor([&] {
      for (size_t i = 0; i < cluster_->num_masters(); ++i) {
        if (!cluster_->mini_master(i)->master()->snapshot_coordinator()
                 .YsqlCatalogFollowerReadsReserved()) {
          return false;
        }
      }
      return true;
    }, 30s * kTimeMultiplier, "Wait for replicated catalog follower-read reservation");
  }

  Status ChangeLeader(const std::string& requested_target = {}) {
    const auto* leader = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      const auto target = cluster_->mini_master(i)->permanent_uuid();
      if (target == leader->permanent_uuid() ||
          (!requested_target.empty() && target != requested_target)) {
        continue;
      }
      RETURN_NOT_OK(cluster_->StepDownMasterLeader(target));
      return WaitFor([&]() -> Result<bool> {
        return VERIFY_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid() == target;
      }, 30s * kTimeMultiplier, "Wait for chosen master leader");
    }
    return STATUS(IllegalState, "No other master");
  }

  static constexpr auto kNamespace = "catalog_reservation_test";
  std::unique_ptr<client::YBClient> client_;
};

TEST_F(CatalogFollowerReadReservationTest, ExplicitAndGated) {
  auto* leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  ASSERT_FALSE(leader->master()->snapshot_coordinator().YsqlCatalogFollowerReadsReserved());
  const auto initial = ASSERT_RESULT(ReservationStatus());
  ASSERT_FALSE(initial.reserved());
  ASSERT_FALSE(initial.reservation_pending());
  ASSERT_FALSE(initial.pitr_admitted_in_term());
  ASSERT_NOK_STR_CONTAINS(Reserve(false), "acknowledge permanent PITR exclusion");
  ASSERT_OK(SET_FLAG(ysql_enable_catalog_follower_read_reservation, false));
  ASSERT_NOK_STR_CONTAINS(Reserve(), "capability is not enabled");
  ASSERT_OK(SET_FLAG(ysql_enable_catalog_follower_read_reservation, true));
  ASSERT_OK(Reserve());
  ASSERT_OK(WaitForReservation());
}

TEST_F(CatalogFollowerReadReservationTest, PersistsAcrossDemotionFailoverAndRestart) {
  ASSERT_OK(Reserve());
  ASSERT_OK(WaitForReservation());
  ASSERT_OK(SET_FLAG(ysql_enable_catalog_follower_read_reservation, false));
  ASSERT_OK(Reserve());
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is prohibited");
  ASSERT_NOK_STR_CONTAINS(Reserve(false), "acknowledge permanent PITR exclusion");
  ASSERT_OK(ChangeLeader());
  ASSERT_OK(Reserve());
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is prohibited");
  ASSERT_OK(cluster_->RestartSync());
  ASSERT_OK(WaitForReservation());
  ASSERT_OK(Reserve());
  ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is prohibited");
  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<MasterBackupProxy>());
  RestoreSnapshotScheduleRequestPB request;
  request.set_snapshot_schedule_id(SnapshotScheduleId::GenerateRandom().AsSlice().ToBuffer());
  request.set_restore_ht(ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->Now().ToUint64());
  RestoreSnapshotScheduleResponsePB response;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(proxy.RestoreSnapshotSchedule(request, &response, &rpc));
  ASSERT_TRUE(response.has_error());
  ASSERT_STR_CONTAINS(response.error().status().message(), "PITR is prohibited");
}

TEST_F(CatalogFollowerReadReservationTest, ExistingAndDeletedPitrStateBlocksReservation) {
  const auto schedule_id = ASSERT_RESULT(CreateSchedule());
  ASSERT_NOK_STR_CONTAINS(Reserve(), "existing or pending PITR state");
  auto proxy = ASSERT_RESULT(cluster_->GetLeaderMasterProxy<MasterBackupProxy>());
  DeleteSnapshotScheduleRequestPB request;
  request.set_snapshot_schedule_id(schedule_id);
  DeleteSnapshotScheduleResponsePB response;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  ASSERT_OK(proxy.DeleteSnapshotSchedule(request, &response, &rpc));
  ASSERT_FALSE(response.has_error()) << response.ShortDebugString();
  // A new leader clears local admission attempts, but retained PITR state still excludes reads.
  ASSERT_OK(ChangeLeader());
  ASSERT_NOK_STR_CONTAINS(Reserve(), "existing or pending PITR state");
}

TEST_F(CatalogFollowerReadReservationTest, OrdinaryBackupSnapshotsRemainAllowed) {
  client::YBSchemaBuilder builder;
  builder.AddColumn("k")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
  client::YBSchema schema;
  ASSERT_OK(builder.Build(&schema));
  const client::YBTableName table_name(YQL_DATABASE_CQL, kNamespace, "t1");
  ASSERT_OK(client_->NewTableCreator()->table_name(table_name).schema(&schema)
                .num_tablets(1).wait(true).Create());
  const auto table_info = ASSERT_RESULT(client_->GetYBTableInfo(table_name));
  client::SnapshotTestUtil snapshots(*cluster_, cluster_->proxy_cache());
  const auto snapshot_id = ASSERT_RESULT(snapshots.CreateSnapshot(table_info.table_id));
  ASSERT_OK(Reserve());
  ASSERT_OK(snapshots.RestoreSnapshot(snapshot_id));
  ASSERT_RESULT(snapshots.CreateSnapshot(table_info.table_id));
}

class CatalogReadPitrAdmissionTest : public CatalogFollowerReadReservationTest,
                                    public ::testing::WithParamInterface<std::tuple<bool, bool>> {};

TEST_P(CatalogReadPitrAdmissionTest, OnlyOneModeCanBeAdmitted) {
  const bool reserve_first = std::get<0>(GetParam());
  const bool timeout_first = std::get<1>(GetParam());
  CountDownLatch entered(1), release(1), finished(1);
  auto* sync_point = SyncPoint::GetInstance();
  Status first_status;
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  sync_point->SetCallBack(
      reserve_first ? "MasterSnapshotCoordinator::ReserveCatalogFollowerReads:BeforeWrite"
                    : "MasterSnapshotCoordinator::CreateSchedule:BeforeWrite",
      [&](void*) {
        entered.CountDown();
        release.Wait();
      });
  sync_point->EnableProcessing();
  const MonoDelta timeout = timeout_first ? 300ms * kTimeMultiplier : 30s * kTimeMultiplier;
  threads.AddThread([&, timeout] {
    first_status = reserve_first ? Reserve(true, timeout) : ResultToStatus(CreateSchedule(timeout));
    finished.CountDown();
  });
  ASSERT_TRUE(entered.WaitFor(10s * kTimeMultiplier));
  if (timeout_first) {
    ASSERT_TRUE(finished.WaitFor(10s * kTimeMultiplier));
    ASSERT_TRUE(first_status.IsTimedOut()) << first_status;
  }
  const auto pending = ASSERT_RESULT(ReservationStatus());
  ASSERT_FALSE(pending.reserved());
  ASSERT_EQ(pending.reservation_pending(), reserve_first);
  ASSERT_EQ(pending.pitr_admitted_in_term(), !reserve_first);
  const auto second_status = reserve_first ? ResultToStatus(CreateSchedule()) : Reserve();
  ASSERT_TRUE(second_status.IsNotSupported()) << second_status;
  if (reserve_first) {
    ASSERT_STR_CONTAINS(second_status.ToString(), "reservation is pending in leader term");
  }
  release.CountDown();
  threads.JoinAll();
  if (!timeout_first) {
    ASSERT_OK(first_status);
  }
  if (reserve_first) {
    ASSERT_OK(WaitForReservation());
    ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is prohibited");
  } else {
    ASSERT_NOK_STR_CONTAINS(Reserve(), "existing or pending PITR state");
  }
}

INSTANTIATE_TEST_CASE_P(
    ReservationFirstAndTimeout, CatalogReadPitrAdmissionTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

class CatalogReadPitrFailoverTest : public CatalogFollowerReadReservationTest,
                                  public ::testing::WithParamInterface<std::tuple<bool, bool>> {};

TEST_P(CatalogReadPitrFailoverTest, OldLeaderCannotAdmitConflictingMode) {
  const bool reserve_first = std::get<0>(GetParam());
  const bool return_to_original = std::get<1>(GetParam());
  const auto original_leader = ASSERT_RESULT(cluster_->GetLeaderMiniMaster())->permanent_uuid();
  CountDownLatch entered(1), release(1);
  auto* sync_point = SyncPoint::GetInstance();
  Status old_status;
  TestThreadHolder threads;
  auto cleanup = ScopeExit([&] {
    release.CountDown();
    sync_point->DisableProcessing();
    sync_point->ClearAllCallBacks();
  });
  sync_point->SetCallBack(
      reserve_first ? "MasterSnapshotCoordinator::ReserveCatalogFollowerReads:BeforeWrite"
                    : "MasterSnapshotCoordinator::CreateSchedule:BeforeWrite",
      [&](void*) {
        entered.CountDown();
        release.Wait();
      });
  sync_point->EnableProcessing();
  threads.AddThread([&] {
    old_status = reserve_first ? Reserve() : ResultToStatus(CreateSchedule());
  });
  ASSERT_TRUE(entered.WaitFor(10s * kTimeMultiplier));
  ASSERT_OK(ChangeLeader());
  if (!return_to_original) {
    ASSERT_OK(reserve_first ? ResultToStatus(CreateSchedule()) : Reserve());
  }
  release.CountDown();
  threads.JoinAll();
  if (old_status.IsAborted()) {
    ASSERT_STR_CONTAINS(old_status.ToString(), "cannot be replicated in term");
  } else {
    ASSERT_TRUE(tserver::IsErrorCodeNotTheLeader(old_status)) << old_status;
  }
  if (return_to_original) {
    ASSERT_OK(ChangeLeader(original_leader));
    const auto state = ASSERT_RESULT(ReservationStatus());
    ASSERT_FALSE(state.reserved());
    ASSERT_FALSE(state.reservation_pending());
    ASSERT_FALSE(state.pitr_admitted_in_term());
    ASSERT_OK(reserve_first ? ResultToStatus(CreateSchedule()) : Reserve());
  }
  if (reserve_first) {
    ASSERT_NOK_STR_CONTAINS(Reserve(), "existing or pending PITR state");
  } else {
    ASSERT_OK(WaitForReservation());
    ASSERT_NOK_STR_CONTAINS(CreateSchedule(), "PITR is prohibited");
    for (size_t i = 0; i < cluster_->num_masters(); ++i) {
      ASSERT_FALSE(cluster_->mini_master(i)->master()->snapshot_coordinator().IsPitrActive());
    }
  }
}

INSTANTIATE_TEST_CASE_P(
    ReservationFirstAndReturningLeader, CatalogReadPitrFailoverTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));


}  // namespace yb::master
