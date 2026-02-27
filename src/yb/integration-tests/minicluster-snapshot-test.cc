// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include <gmock/gmock.h>

#include <google/protobuf/util/message_differencer.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/snapshot_test_util.h"
#include "yb/client/table_creator.h"
#include "yb/client/table_info.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/postgres-minicluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/tablet_creation_limits.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_context.h"

#include "yb/tools/admin-test-base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_data_size_metrics.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"

#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(cleanup_split_tablets_interval_sec);
DECLARE_int32(data_size_metric_updater_interval_sec);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_bool(enable_db_clone);
DECLARE_bool(enforce_tablet_replica_limits);
DECLARE_int32(load_balancer_initial_delay_secs);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(metrics_snapshotter_interval_ms);
DECLARE_int32(num_cpus);
DECLARE_int32(pgsql_proxy_webserver_port);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);
DECLARE_int32(tserver_heartbeat_metrics_interval_ms);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_enable_auto_analyze_infra);
DECLARE_string(ysql_hba_conf_csv);
DECLARE_int32(ysql_sequence_cache_minval);
DECLARE_int32(ysql_clone_pg_schema_rpc_timeout_ms);
DECLARE_uint32(TEST_clone_pg_schema_delay_ms);
DECLARE_int32(ysql_tablespace_info_refresh_secs);
DECLARE_bool(TEST_fail_clone_pg_schema);
DECLARE_bool(TEST_fail_clone_tablets);
DECLARE_string(TEST_mini_cluster_pg_host_port);
DECLARE_bool(TEST_skip_deleting_split_tablets);

namespace yb {
namespace tserver {
METRIC_DECLARE_gauge_uint64(ts_data_size);
METRIC_DECLARE_gauge_uint64(ts_active_data_size);
}
namespace master {

constexpr auto kInterval = 20s;
constexpr auto kRetention = RegularBuildVsDebugVsSanitizers(10min, 18min, 10min);

YB_DEFINE_ENUM(YsqlColocationConfig, (kNotColocated)(kDBColocated));

using namespace std::chrono_literals;

Result<TxnSnapshotRestorationId> RestoreSnapshotSchedule(
    MasterBackupProxy* proxy, const SnapshotScheduleId& schedule_id, const HybridTime& ht,
    MonoDelta timeout) {
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  master::RestoreSnapshotScheduleRequestPB req;
  master::RestoreSnapshotScheduleResponsePB resp;
  req.set_snapshot_schedule_id(schedule_id.data(), schedule_id.size());
  req.set_restore_ht(ht.ToUint64());
  RETURN_NOT_OK(proxy->RestoreSnapshotSchedule(req, &resp, &controller));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return FullyDecodeTxnSnapshotRestorationId(resp.restoration_id());
}

Result<google::protobuf::RepeatedPtrField<RestorationInfoPB>> ListSnapshotRestorations(
    MasterBackupProxy* proxy, const TxnSnapshotRestorationId& restoration_id, MonoDelta timeout) {
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  master::ListSnapshotRestorationsRequestPB req;
  master::ListSnapshotRestorationsResponsePB resp;
  if (restoration_id) {
    req.set_restoration_id(restoration_id.data(), restoration_id.size());
  }
  RETURN_NOT_OK(proxy->ListSnapshotRestorations(req, &resp, &controller));
  if (resp.has_status()) {
    return StatusFromPB(resp.status());
  }
  return resp.restorations();
}

Result<SnapshotScheduleId> CreateSnapshotSchedule(
    MasterBackupProxy* proxy,
    YQLDatabase namespace_type,
    const std::string& namespace_name,
    MonoDelta interval,
    MonoDelta retention_duration,
    MonoDelta timeout) {
  rpc::RpcController controller;
  master::CreateSnapshotScheduleRequestPB req;
  master::CreateSnapshotScheduleResponsePB resp;
  controller.set_timeout(MonoDelta::FromSeconds(10));
  client::YBTableName keyspace;
  master::NamespaceIdentifierPB namespace_id;
  namespace_id.set_database_type(namespace_type);
  namespace_id.set_name(namespace_name);
  keyspace.GetFromNamespaceIdentifierPB(namespace_id);
  auto* options = req.mutable_options();
  auto* filter_tables = options->mutable_filter()->mutable_tables()->mutable_tables();
  keyspace.SetIntoTableIdentifierPB(filter_tables->Add());
  options->set_interval_sec(std::llround(interval.ToSeconds()));
  options->set_retention_duration_sec(std::llround(retention_duration.ToSeconds()));
  RETURN_NOT_OK(proxy->CreateSnapshotSchedule(req, &resp, &controller));
  return FullyDecodeSnapshotScheduleId(resp.snapshot_schedule_id());
}

Result<SnapshotScheduleInfoPB> GetSnapshotSchedule(
    MasterBackupProxy* proxy, const SnapshotScheduleId& id) {
  rpc::RpcController controller;
  ListSnapshotSchedulesRequestPB req;
  ListSnapshotSchedulesResponsePB resp;
  req.set_snapshot_schedule_id(id.data(), id.size());
  controller.set_timeout(10s);
  RETURN_NOT_OK(proxy->ListSnapshotSchedules(req, &resp, &controller));
  SCHECK_EQ(resp.schedules_size(), 1, NotFound, "Wrong number of schedules");
  return resp.schedules().Get(0);
}

Status DeleteSnapshotSchedule(MasterBackupProxy* proxy, const SnapshotScheduleId& id) {
  rpc::RpcController controller;
  master::DeleteSnapshotScheduleRequestPB req;
  master::DeleteSnapshotScheduleResponsePB resp;
  controller.set_timeout(MonoDelta::FromSeconds(10));
  req.set_snapshot_schedule_id(id.data(), id.size());
  return proxy->DeleteSnapshotSchedule(req, &resp, &controller);
}

Result<TxnSnapshotId> WaitNewSnapshot(MasterBackupProxy* proxy, const SnapshotScheduleId& id) {
  LOG(INFO) << "WaitNewSnapshot, schedule id: " << id;
  TxnSnapshotId last_snapshot_id;
  TxnSnapshotId new_snapshot_id;
  RETURN_NOT_OK(WaitFor(
      [&proxy, &id, &last_snapshot_id, &new_snapshot_id]() -> Result<bool> {
        // If there's a master leader failover then we should wait for the next cycle.
        auto schedule_info = VERIFY_RESULT(GetSnapshotSchedule(proxy, id));
        auto& snapshots = schedule_info.snapshots();
        if (snapshots.empty()) {
          return false;
        }
        auto snapshot_id = VERIFY_RESULT(FullyDecodeTxnSnapshotId(
            snapshots[snapshots.size() - 1].id()));
        LOG(INFO) << "WaitNewSnapshot, last snapshot id: " << snapshot_id;
        if (last_snapshot_id.IsNil()) {
          last_snapshot_id = snapshot_id;
          return false;
        }
        if (last_snapshot_id != snapshot_id) {
          new_snapshot_id = snapshot_id;
          return true;
        } else {
          return false;
        }
      },
      kInterval * 5, "Wait new schedule snapshot"));
  return new_snapshot_id;
}

Status WaitForRestoration(
    MasterBackupProxy* proxy, const TxnSnapshotRestorationId& restoration_id, MonoDelta timeout) {
  auto condition = [proxy, &restoration_id, timeout]() -> Result<bool> {
    auto restorations_status = ListSnapshotRestorations(proxy, restoration_id, timeout);
    RETURN_NOT_OK_RET(ResultToStatus(restorations_status), false);
    google::protobuf::RepeatedPtrField<RestorationInfoPB> restorations = *restorations_status;
    for (const auto& restoration : restorations) {
      if (!(VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(restoration.id())) ==
            restoration_id)) {
        continue;
      }
      return restoration.entry().state() == SysSnapshotEntryPB::RESTORED;
    }
    return false;
  };
  return WaitFor(condition, timeout, "Waiting for restoration to complete");
}

Status WaitForSnapshotComplete(
    MasterBackupProxy* proxy, const TxnSnapshotId& snapshot_id, bool check_deleted = false) {
  return WaitFor(
      [&]() -> Result<bool> {
        master::ListSnapshotsRequestPB req;
        master::ListSnapshotsResponsePB resp;
        rpc::RpcController rpc;
        rpc.set_timeout(30s * kTimeMultiplier);
        req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
        Status s = proxy->ListSnapshots(req, &resp, &rpc);
        // If snapshot is cleaned up and we are waiting for a delete
        // then succeed this call.
        if (check_deleted && !s.ok() && s.IsNotFound()) {
          return true;
        }
        if (resp.has_error()) {
          Status s = StatusFromPB(resp.error().status());
          if (check_deleted && s.IsNotFound()) {
            return true;
          }
          return s;
        }
        if (resp.snapshots_size() != 1) {
          return STATUS(
              IllegalState, Format("There should be exactly one snapshot of id $0", snapshot_id));
        }
        if (check_deleted) {
          return resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB::DELETED;
        }
        return resp.snapshots(0).entry().state() == master::SysSnapshotEntryPB::COMPLETE;
      },
      30s * kTimeMultiplier, "Waiting for snapshot to complete");
}

Result<SnapshotInfoPB> WaitScheduleSnapshot(
    MasterBackupProxy* proxy, const SnapshotScheduleId& id, MonoDelta duration,
    uint32_t num_snapshots = 1) {
  SnapshotInfoPB snapshot;
  RETURN_NOT_OK(WaitFor(
      [proxy, id, num_snapshots, &snapshot]() -> Result<bool> {
        // If there's a master leader failover then we should wait for the next cycle.
        auto schedule = VERIFY_RESULT(GetSnapshotSchedule(proxy, id));
        if ((uint32_t)schedule.snapshots_size() < num_snapshots) {
          return false;
        }
        snapshot = schedule.snapshots()[schedule.snapshots_size() - 1];
        return true;
      },
      duration, Format("Wait for schedule to have $0 snapshots", num_snapshots)));

  // Wait for the present time to become at-least the time chosen by the snapshot.
  auto snapshot_time_string = snapshot.entry().snapshot_hybrid_time();
  HybridTime snapshot_ht = HybridTime::FromPB(snapshot_time_string);

  RETURN_NOT_OK(WaitFor(
      [&snapshot_ht]() -> Result<bool> {
        Timestamp current_time(VERIFY_RESULT(WallClock()->Now()).time_point);
        HybridTime current_ht = HybridTime::FromMicros(current_time.ToInt64());
        return snapshot_ht <= current_ht;
      },
      duration, "Wait Snapshot Time Elapses"));
  return snapshot;
}

Result<master::SnapshotInfoPB> ExportSnapshot(
    MasterBackupProxy* proxy, const TxnSnapshotId& snapshot_id, bool prepare_for_backup = true) {
  master::ListSnapshotsRequestPB req;
  master::ListSnapshotsResponsePB resp;
  rpc::RpcController rpc;
  rpc.set_timeout(30s * kTimeMultiplier);
  req.set_snapshot_id(snapshot_id.data(), snapshot_id.size());
  req.set_prepare_for_backup(prepare_for_backup);
  Status s = proxy->ListSnapshots(req, &resp, &rpc);
  LOG(INFO) << Format("ExportSnapshot response is: $0", resp.ShortDebugString());
  if (!s.ok()) {
    return s;
  }
  if (resp.snapshots_size() != 1) {
    return STATUS(
        IllegalState, Format("There should be exactly one snapshot of id $0", snapshot_id));
  }
  return resp.snapshots(0);
}

class MasterSnapshotTest : public YBMiniClusterTestBase<MiniCluster> {
 public:
  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 1;
    cluster_ = std::make_unique<MiniCluster>(opts);
    ASSERT_OK(cluster_->Start());
    client_ =
        ASSERT_RESULT(client::YBClientBuilder()
                          .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
                          .Build());
    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    backup_proxy_ = std::make_unique<MasterBackupProxy>(
        proxy_cache_.get(), cluster_->mini_master()->bound_rpc_addr());
  }

  Result<client::YBSchema> CreateTestSchema() {
    client::YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
    b.AddColumn("v1")->Type(DataType::INT64)->NotNull();
    b.AddColumn("v2")->Type(DataType::STRING)->NotNull();
    client::YBSchema schema;
    RETURN_NOT_OK(b.Build(&schema));
    return schema;
  }

  LeaderEpoch CurrentLeaderEpoch() {
    return LeaderEpoch(
        cluster_->mini_master()->catalog_manager().leader_ready_term(),
        cluster_->mini_master()->sys_catalog().pitr_count());
  }

 protected:
  void DoTearDown() override {
    messenger_->Shutdown();
    YBMiniClusterTestBase::DoTearDown();
  }

  std::unique_ptr<client::YBClient> client_;
  std::unique_ptr<MasterBackupProxy> backup_proxy_;

 private:
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

TEST_F(MasterSnapshotTest, FailSysCatalogWriteWithStaleTable) {
  auto first_epoch = CurrentLeaderEpoch();
  const auto timeout = MonoDelta::FromSeconds(20);
  client::YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      table_name.namespace_name(), table_name.namespace_type()));
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      backup_proxy_.get(), table_name.namespace_type(), table_name.namespace_name(),
      MonoDelta::FromSeconds(60), MonoDelta::FromSeconds(600), timeout));
  ASSERT_OK(WaitScheduleSnapshot(backup_proxy_.get(), schedule_id, timeout));

  auto table_creator = client_->NewTableCreator();
  auto schema = ASSERT_RESULT(CreateTestSchema());
  ASSERT_OK(
      table_creator->table_name(table_name).schema(&schema).num_tablets(1).wait(true).Create());

  auto yb_table_info = ASSERT_RESULT(client_->GetYBTableInfo(table_name));
  LOG(INFO) << "Getting table info,";
  auto table_info =
      cluster_->mini_master()->catalog_manager_impl().GetTableInfo(yb_table_info.table_id);
  ASSERT_TRUE(table_info != nullptr);
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  HybridTime ht = ASSERT_RESULT(HybridTime::ParseHybridTime(time.ToString()));
  LOG(INFO) << "Performing restoration.";
  auto restoration_id =
      ASSERT_RESULT(RestoreSnapshotSchedule(backup_proxy_.get(), schedule_id, ht, timeout));
  LOG(INFO) << "Waiting for restoration.";
  ASSERT_OK(WaitForRestoration(backup_proxy_.get(), restoration_id, timeout));

  LOG(INFO) << "Restoration finished.";
  {
    auto table_lock = table_info->LockForWrite();
    table_lock.mutable_data()->pb.set_parent_table_id("fnord");
    LOG(INFO) << Format(
        "Writing with stale epoch: $0, $1", first_epoch.leader_term, first_epoch.pitr_count);
    ASSERT_NOK(cluster_->mini_master()->sys_catalog().Upsert(first_epoch, table_info));
    auto post_restore_epoch = CurrentLeaderEpoch();
    LOG(INFO) << Format(
        "Writing with fresh epoch: $0, $1", post_restore_epoch.leader_term,
        post_restore_epoch.pitr_count);
    ASSERT_OK(cluster_->mini_master()->sys_catalog().Upsert(post_restore_epoch, table_info));
  }
}

TEST_F(MasterSnapshotTest, ListSnapshotSchedulesHasNoSnapshotDetails) {
  const auto timeout = MonoDelta::FromSeconds(20);
  client::YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      table_name.namespace_name(), table_name.namespace_type()));

  auto table_creator = client_->NewTableCreator();
  auto schema = ASSERT_RESULT(CreateTestSchema());
  ASSERT_OK(
      table_creator->table_name(table_name).schema(&schema).num_tablets(1).wait(true).Create());

  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      backup_proxy_.get(), table_name.namespace_type(), table_name.namespace_name(),
      MonoDelta::FromSeconds(1), MonoDelta::FromSeconds(60), timeout));
  LOG(INFO) << "Created schedule: " << schedule_id;

  auto snapshot = ASSERT_RESULT(WaitScheduleSnapshot(backup_proxy_.get(), schedule_id, timeout));
  auto& sys_snapshot_entry = snapshot.entry();
  EXPECT_THAT(sys_snapshot_entry.entries(), testing::IsEmpty()) << Format(
      "Schedule $0 has a snapshot $1 containing sys row entries: $2", schedule_id, snapshot.id(),
      snapshot.DebugString());
  EXPECT_THAT(snapshot.backup_entries(), testing::IsEmpty())
      << Format("Snapshot $0 includes backup details: $1", snapshot.id(), snapshot.DebugString());
}

class PostgresMiniClusterTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 250;
    pgwrapper::PgMiniTestBase::SetUp();
  }

  MiniCluster* mini_cluster() { return cluster_.get(); }

  Result<TableInfoPtr> GetTable(const std::string& table_name, const std::string& db_name) {
    auto leader_master = VERIFY_RESULT(cluster_->GetLeaderMiniMaster());
    for (const auto& table : leader_master->catalog_manager_impl().GetTables(GetTablesMode::kAll)) {
      if (table->name() == table_name && table->namespace_name() == db_name) {
        return table;
      }
    }
    return STATUS_FORMAT(NotFound, "Table $0 not found", table_name);
  }

  Status CreateDatabase(
      const std::string& namespace_name,
      master::YsqlColocationConfig colocated = master::YsqlColocationConfig::kNotColocated) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE DATABASE $0$1", namespace_name,
        colocated == master::YsqlColocationConfig::kDBColocated ? " with colocation = true" : ""));
    return Status::OK();
  }

  Result<Timestamp> GetCurrentTime() {
    // IMPORTANT NOTE: THE SLEEP IS TEMPORARY AND
    // SHOULD BE REMOVED ONCE GH#12796 IS FIXED.
    SleepFor(MonoDelta::FromSeconds(4 * kTimeMultiplier));
    auto time = Timestamp(VERIFY_RESULT(WallClock()->Now()).time_point);
    LOG(INFO) << "Time to restore: " << time.ToHumanReadableTime();
    return time;
  }
};

class MasterExportSnapshotTest
    : public PostgresMiniClusterTest,
      public ::testing::WithParamInterface<master::YsqlColocationConfig> {
 public:
  void SetUp() override {
    PostgresMiniClusterTest::SetUp();
    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    backup_proxy = std::make_unique<master::MasterBackupProxy>(
        proxy_cache_.get(), mini_cluster()->mini_master()->bound_rpc_addr());
    admin_proxy = std::make_unique<master::MasterAdminProxy>(
        proxy_cache_.get(), mini_cluster()->mini_master()->bound_rpc_addr());
    client_ =
        ASSERT_RESULT(client::YBClientBuilder()
                          .add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str())
                          .Build());
    ASSERT_OK(CreateDatabaseWithSnapshotSchedule(GetParam()));
  }

  Status CreateDatabaseWithSnapshotSchedule(
      master::YsqlColocationConfig colocated = master::YsqlColocationConfig::kNotColocated) {
    RETURN_NOT_OK(CreateDatabase(kNamespaceName, colocated));
    LOG(INFO) << "Database created.";
    schedule_id = VERIFY_RESULT(CreateSnapshotSchedule(
        backup_proxy.get(), YQL_DATABASE_PGSQL, kNamespaceName, kInterval, kRetention, timeout));
    RETURN_NOT_OK(WaitScheduleSnapshot(backup_proxy.get(), schedule_id, timeout));
    return Status::OK();
  }

 protected:
  const std::string kNamespaceName = "testdb";
  const MonoDelta timeout = MonoDelta::FromSeconds(30);
  SnapshotScheduleId schedule_id = SnapshotScheduleId::Nil();
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<master::MasterBackupProxy> backup_proxy;
  std::unique_ptr<master::MasterAdminProxy> admin_proxy;
  std::unique_ptr<client::YBClient> client_;
};

INSTANTIATE_TEST_CASE_P(
    Colocation, MasterExportSnapshotTest,
    ::testing::Values(
        master::YsqlColocationConfig::kNotColocated, master::YsqlColocationConfig::kDBColocated));

// Test that export_snapshot_from_schedule as of time generates correct SnapshotInfoPB.
// 1. Create some tables.
// 2. Mark time t and wait for a new snapshot to be created as part of the snapshot schedule.
// 3. export_snapshot to generate the SnapshotInfoPB as of current time. It is the traditional
// export_snapshot command (not the new command) to serve as ground truth.
// 4. Create more tables.
// 5. Generate snapshotInfo from schedule using the time t.
// 6. Assert the output of 5 and 3 are the same (after removing PITR related fields from 3).
TEST_P(MasterExportSnapshotTest, ExportSnapshotAsOfTime) {
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  // 1.
  LOG(INFO) << Format("Create tables t1,t2");
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (key INT PRIMARY KEY, c1 TEXT, c2 TEXT)"));
  // 2.
  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << Format("current timestamp is: {$0}", time);

  // 3.
  auto decoded_snapshot_id = ASSERT_RESULT(WaitNewSnapshot(backup_proxy.get(), schedule_id));
  ASSERT_OK(WaitForSnapshotComplete(backup_proxy.get(), decoded_snapshot_id));
  master::SnapshotInfoPB ground_truth =
      ASSERT_RESULT(ExportSnapshot(backup_proxy.get(), decoded_snapshot_id));
  // 4.
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (key INT PRIMARY KEY, c1 INT, c2 TEXT, c3 TEXT)"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 ADD COLUMN new_col TEXT"));
  // 5.
  LOG(INFO) << Format(
      "Exporting snapshot from snapshot schedule: $0, Hybrid time = $1", schedule_id, time);
  auto deadline = CoarseMonoClock::Now() + timeout;
  auto [snapshot_info_as_of_time, not_snapshotted_tablets, _] = ASSERT_RESULT(
      mini_cluster()
          ->mini_master()
          ->catalog_manager_impl()
          .GenerateSnapshotInfoFromScheduleForClone(
              schedule_id, HybridTime::FromMicros(static_cast<uint64>(time.ToInt64())), deadline));
  // 6.
  // Clear PITR related fields from ground_truth as these fields are cleared when generating
  // snapshotInfo as of time.
  ground_truth.mutable_entry()->clear_schedule_id();
  ground_truth.mutable_entry()->clear_previous_snapshot_hybrid_time();
  LOG(INFO) << Format("SnapshotInfoPB ground_truth: $0", ground_truth.ShortDebugString());
  LOG(INFO) << Format(
      "SnapshotInfoPB as of time=$0 :$1", time, snapshot_info_as_of_time.ShortDebugString());
  std::string diff;
  ASSERT_TRUE(
      pb_util::ArePBsEqual(std::move(ground_truth), std::move(snapshot_info_as_of_time), &diff))
      << diff;
  messenger_->Shutdown();
}

// Test that export_snapshot_from_schedule as of time doesn't include hidden tables in
// SnapshotInfoPB.
TEST_P(MasterExportSnapshotTest, ExportSnapshotAsOfTimeWithHiddenTables) {
  auto conn = ASSERT_RESULT(ConnectToDB(kNamespaceName));
  // 1. Create table t1, then delete it to mark it as hidden and then recreate table t1.
  LOG(INFO) << Format("Create tables t1");
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(conn.Execute("DROP TABLE t1"));
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));

  // 2. Mark time t and wait for a new snapshot to be created as part of the snapshot schedule.
  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << Format("current timestamp is: {$0}", time);

  // 3. export_snapshot to generate the SnapshotInfoPB as of current time. It is the traditional
  // export_snapshot command (not the new command) to serve as ground truth.
  auto decoded_snapshot_id = ASSERT_RESULT(WaitNewSnapshot(backup_proxy.get(), schedule_id));
  ASSERT_OK(WaitForSnapshotComplete(backup_proxy.get(), decoded_snapshot_id));
  master::SnapshotInfoPB ground_truth =
      ASSERT_RESULT(ExportSnapshot(backup_proxy.get(), decoded_snapshot_id));
  // 4. Create another table that shouldn't be included in generate snapshot as of time
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (key INT PRIMARY KEY, c1 TEXT, c2 TEXT)"));
  // 5. Generate snapshotInfo from schedule using the time t.
  LOG(INFO) << Format(
      "Exporting snapshot from snapshot schedule: $0, Hybrid time = $1", schedule_id, time);
  auto deadline = CoarseMonoClock::Now() + timeout;
  auto [snapshot_info_as_of_time, not_snapshotted_tablets, _] = ASSERT_RESULT(
      mini_cluster()
          ->mini_master()
          ->catalog_manager_impl()
          .GenerateSnapshotInfoFromScheduleForClone(
              schedule_id, HybridTime::FromMicros(static_cast<uint64>(time.ToInt64())), deadline));
  // 6. Assert the output of 5 and 3 are the same (after removing PITR related fields from 3).
  ground_truth.mutable_entry()->clear_schedule_id();
  ground_truth.mutable_entry()->clear_previous_snapshot_hybrid_time();
  LOG(INFO) << Format("SnapshotInfoPB ground_truth: $0", ground_truth.ShortDebugString());
  LOG(INFO) << Format(
      "SnapshotInfoPB as of time=$0 :$1", time, snapshot_info_as_of_time.ShortDebugString());
  std::string diff;
  ASSERT_TRUE(
      pb_util::ArePBsEqual(std::move(ground_truth), std::move(snapshot_info_as_of_time), &diff))
      << diff;
  messenger_->Shutdown();
}

class PgCloneInitiallyEmptyDBTest : public PostgresMiniClusterTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_load_balancer_initial_delay_secs) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_tablespace_info_refresh_secs) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cleanup_split_tablets_interval_sec) = 1;
    PostgresMiniClusterTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_db_clone) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_mini_cluster_pg_host_port) = pg_host_port().ToString();
    ASSERT_OK(CreateProxies());
    ASSERT_OK(CreateSourceDbAndSnapshotSchedule(ColocateDatabase()));
  }

  virtual Status CreateProxies() {
    messenger_ = VERIFY_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    auto* master = VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster());
    master_admin_proxy_ = std::make_unique<MasterAdminProxy>(
        proxy_cache_.get(), master->bound_rpc_addr());
    master_backup_proxy_ = std::make_shared<MasterBackupProxy>(
        proxy_cache_.get(), master->bound_rpc_addr());
    return Status::OK();
  }

  Status CreateSourceDbAndSnapshotSchedule(master::YsqlColocationConfig colocated) {
    RETURN_NOT_OK(CreateDatabase(kSourceNamespaceName, colocated));
    source_conn_ = std::make_unique<pgwrapper::PGConn>(
        VERIFY_RESULT(ConnectToDB(kSourceNamespaceName)));
    schedule_id_ = VERIFY_RESULT(CreateSnapshotSchedule(
        master_backup_proxy_.get(), YQL_DATABASE_PGSQL, kSourceNamespaceName, kInterval, kRetention,
        kTimeout));
    RETURN_NOT_OK(WaitScheduleSnapshot(master_backup_proxy_.get(), schedule_id_, kTimeout));
     return Status::OK();
  }

  void DoTearDown() override {
    messenger_->Shutdown();
    PostgresMiniClusterTest::DoTearDown();
  }

  Status SplitTablet(const TabletId& tablet_id) {
    SplitTabletRequestPB req;
    SplitTabletResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    req.set_tablet_id(tablet_id);
    RETURN_NOT_OK(master_admin_proxy_->SplitTablet(req, &resp, &controller));
    SCHECK_FORMAT(
        !resp.has_error(), InternalError, "SplitTablet RPC failed. Error: $0",
        resp.error().ShortDebugString());
    return Status::OK();
  }

  virtual master::YsqlColocationConfig ColocateDatabase() {
    return master::YsqlColocationConfig::kNotColocated;
  }

  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::shared_ptr<MasterAdminProxy> master_admin_proxy_;
  std::shared_ptr<MasterBackupProxy> master_backup_proxy_;
  std::unique_ptr<pgwrapper::PGConn> source_conn_;
  SnapshotScheduleId schedule_id_ = SnapshotScheduleId::Nil();

  const std::string kSourceNamespaceName = "testdb";
  const std::string kSourceTableName = "t1";
  const std::string kTargetNamespaceName1 = "testdb_clone1";
  const std::string kTargetNamespaceName2 = "testdb_clone2";
  const MonoDelta kTimeout = MonoDelta::FromSeconds(30);
};

class PgCloneTest : public PgCloneInitiallyEmptyDBTest {
 protected:
  void SetUp() override {
    PgCloneInitiallyEmptyDBTest::SetUp();
    ASSERT_OK(source_conn_->ExecuteFormat(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", kSourceTableName));
  }
};

class PgCloneTestWithColocatedDBParam
    : public PgCloneTest,
      public ::testing::WithParamInterface<master::YsqlColocationConfig> {
 public:
  virtual void SetUp() override { PgCloneTest::SetUp(); }

  master::YsqlColocationConfig ColocateDatabase() override { return GetParam(); }
};

INSTANTIATE_TEST_CASE_P(
    Colocation, PgCloneTestWithColocatedDBParam,
    ::testing::Values(
        master::YsqlColocationConfig::kNotColocated, master::YsqlColocationConfig::kDBColocated));

TEST_P(PgCloneTestWithColocatedDBParam, CloneYsqlSyntax) {
  // Basic clone test for PG using the YSQL TEMPLATE syntax.
  // Writes some data before time t and some data after t, and verifies that the cloning as of t
  // creates a clone with only the first set of rows, and cloning after t creates a clone with both
  // sets of rows.

  // Write a row.
  const std::vector<std::tuple<int32_t, int32_t>> kRows = {{1, 10}, {2, 20}};
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRows[0]), std::get<1>(kRows[0])));
  // Write a second row after recording the hybrid time.
  auto timestamp = ASSERT_RESULT(GetCurrentTime());
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRows[1]), std::get<1>(kRows[1])));

  // Clone to ht and verify clone only has the first row.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      timestamp.ToInt64()));
  // Use a scope here and below so we can drop the cloned databases after.
  {
    auto target_conn1 = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
    auto row = ASSERT_RESULT((target_conn1.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
    ASSERT_EQ(row, kRows[0]);
  }
  ASSERT_OK(source_conn_->ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName1));

  // Clone using the current timestamp (AS OF is not specified) and check that it has both rows.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName2, kSourceNamespaceName));
  {
    auto target_conn2 = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName2));
    auto rows = ASSERT_RESULT((target_conn2.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
    ASSERT_VECTORS_EQ(rows, kRows);
  }
  ASSERT_OK(source_conn_->ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName2));

  // Clone using a timestamp in PG's timestamptz format. It should only have the first row.
  const auto kTargetNamespaceName3 = "testdb_clone3";
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF '$2'", kTargetNamespaceName3, kSourceNamespaceName,
      timestamp.ToHumanReadableTime()));
  {
    auto target_conn3 = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName3));
    auto row = ASSERT_RESULT((target_conn3.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
    ASSERT_EQ(row, kRows[0]);
  }
  ASSERT_OK(source_conn_->ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName3));

  // Cloning using a timestamp in Unix seconds should not work.
  const auto kTargetNamespaceName4 = "testdb_clone4";
  auto status = source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName4, kSourceNamespaceName,
      timestamp.ToInt64() / 1000000);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "invalid clone time type");

  // Verify source rows are unchanged.
  auto rows = ASSERT_RESULT((source_conn_->FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_VECTORS_EQ(rows, kRows);
}

TEST_F_EX(PgCloneTest, TestOidsAdvancedAfterClone, PgCloneInitiallyEmptyDBTest) {
  ASSERT_OK(source_conn_->Execute("CREATE TABLE my_table (a INT, b INT)"));
  auto timestamp = ASSERT_RESULT(GetCurrentTime());

  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      timestamp.ToInt64()));

  // Ensure that the DROP below will only hide the table, not delete it.
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      master_backup_proxy_.get(), YQL_DATABASE_PGSQL, kTargetNamespaceName1, kInterval, kRetention,
      kTimeout));
  ASSERT_OK(WaitScheduleSnapshot(master_backup_proxy_.get(), schedule_id, kTimeout));

  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  ASSERT_OK(target_conn.Execute("DROP TABLE my_table"));

  // At this point, if we have not advanced the normal space OID counter, then we will be attempting
  // to create a table with the same OID as the one we just dropped.  That would fail.
  ASSERT_OK(target_conn.Execute("CREATE TABLE my_table (a INT, b INT)"));
}

TEST_F(PgCloneTest, CloneVectorIndex) {
  ASSERT_OK(source_conn_->Execute("CREATE EXTENSION vector"));
  ASSERT_OK(source_conn_->Execute(
      "CREATE TABLE test (id bigserial PRIMARY KEY, embedding vector(1))"));
  ASSERT_OK(source_conn_->Execute(
      "CREATE INDEX test_vi ON test USING ybhnsw (embedding vector_l2_ops)"));

  for (int i = 0; i <= 10; i += 2) {
    ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO test VALUES ($0, '[$0]')", i));
  }
  auto timestamp = ASSERT_RESULT(GetCurrentTime());
  for (int i = 1; i <= 10; i += 2) {
    ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO test VALUES ($0, '[$0]')", i));
  }

  LOG(INFO) << "Create first clone";
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      timestamp.ToInt64()));

  LOG(INFO) << "Read first clone";
  {
    auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
    auto row = ASSERT_RESULT((target_conn.FetchAllAsString(
        "SELECT id FROM test ORDER BY embedding <-> '[3.9]' LIMIT 3")));
    ASSERT_EQ(row, "4; 2; 6");
  }
  LOG(INFO) << "Drop first clone";
  ASSERT_OK(source_conn_->ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName1));

  LOG(INFO) << "Create second clone";
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName2, kSourceNamespaceName));

  LOG(INFO) << "Read second clone";
  {
    auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName2));
    auto row = ASSERT_RESULT((target_conn.FetchAllAsString(
        "SELECT id FROM test ORDER BY embedding <-> '[3.9]' LIMIT 3")));
    ASSERT_EQ(row, "4; 3; 5");
  }
  LOG(INFO) << "Drop second clone";
  ASSERT_OK(source_conn_->ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName2));
}

TEST_F(PgCloneTest, CloneWithAlterDatabaseSet) {
  // Ensure cloning succeeds even when the source database has a ALTER DATABASE.
  ASSERT_OK(source_conn_->ExecuteFormat(
      R"(ALTER DATABASE $0 SET "TimeZone" TO 'US/Central')", kSourceNamespaceName));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
}

class TabletDataSizeMetricsTest : public PostgresMiniClusterTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_data_size_metric_updater_interval_sec) = 1;
    PostgresMiniClusterTest::SetUp();
    messenger_ = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    snapshot_util_ = std::make_unique<client::SnapshotTestUtil>();
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }

  void DoTearDown() override {
    messenger_->Shutdown();
    PostgresMiniClusterTest::DoTearDown();
  }

  std::unique_ptr<client::SnapshotTestUtil> snapshot_util_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
};

TEST_F(TabletDataSizeMetricsTest, TotalOnDiskSizeIncludesSnapshots) {
  ASSERT_OK(CreateDatabase("testdb"));
  auto conn = ASSERT_RESULT(ConnectToDB("testdb"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (key INT PRIMARY KEY, value INT) SPLIT INTO 1 TABLETS"));
  auto table = ASSERT_RESULT(GetTable("t1", "testdb"));
  auto tablet_id = ASSERT_RESULT(table->GetTablets())[0]->tablet_id();
  auto tablet_peer = ASSERT_RESULT(
      cluster_->mini_tablet_server(0)->server()->tablet_peer_lookup()->GetServingTablet(tablet_id));
  LOG(INFO) << "Tablet id: " << tablet_id;

  // Write some data and create a snapshot (this should create an SST file).
  ASSERT_OK(conn.Execute(
      "INSERT INTO t1 VALUES (generate_series(1,1000), generate_series(1,1000))"));
  ASSERT_RESULT(snapshot_util_->CreateSnapshot(table->id()));

  // Get the on-disk size of the tablet.
  tablet::TabletOnDiskSizeInfo size_before;
  ASSERT_OK(WaitFor([&]() {
    size_before = tablet_peer->GetOnDiskSizeInfo();
    return size_before.sst_files_disk_size > 0 &&
           size_before.total_on_disk_size > size_before.sst_files_disk_size;
  }, 30s, "Wait for on-disk size to include the SST"));

  // Delete the data in the tablet and re-compact. The SST file size should drop to 0.
  ASSERT_OK(conn.Execute("DELETE FROM t1"));
  ASSERT_OK(WaitFor([&]() {
    // Retrigger compactions until this peer has no SST files. This is required because if the
    // delete has only applied on the other two peers, the compaction would not do anything on this
    // peer.
    FlushAndCompactTablets();
    return tablet_peer->GetOnDiskSizeInfo().sst_files_disk_size == 0;
  }, 30s, "Wait for on-disk size to drop after delete"));

  // Once the on-disk size metric updater has run, check that the total on-disk size has not
  // decrease by a significant portion of the size of the SST we deleted.
  SleepFor(FLAGS_data_size_metric_updater_interval_sec * 2s);
  auto size_after = tablet_peer->GetOnDiskSizeInfo();
  auto size_diff = size_before.total_on_disk_size - size_after.total_on_disk_size;
  ASSERT_LT(size_diff, size_before.sst_files_disk_size * 0.9);
}

class TsDataSizeMetricsTest : public PgCloneTest {
 public:
  uint64_t GetTsDataSize() {
    const auto& metric_entity = recorded_tserver_->metric_entity();
    const auto metric =
        metric_entity.FindOrNull<AtomicGauge<uint64_t>>(tserver::METRIC_ts_data_size);
    CHECK_NOTNULL(metric.get());
    return metric->value();
  }

  uint64_t GetTsActiveDataSize() {
    const auto& metric_entity = recorded_tserver_->metric_entity();
    const auto metric =
        metric_entity.FindOrNull<AtomicGauge<uint64_t>>(tserver::METRIC_ts_active_data_size);
    CHECK_NOTNULL(metric.get());
    return metric->value();
  }

 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_data_size_metric_updater_interval_sec) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_metrics_snapshotter_interval_ms) = 1000;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_tserver_heartbeat_metrics_interval_ms) = 1000;
    PgCloneTest::SetUp();
    recorded_tserver_ = cluster_->mini_tablet_server(0);
  }

  tserver::MiniTabletServer* recorded_tserver_;
};

TEST_F(TsDataSizeMetricsTest, TestSnapshotSchedule) {
  ASSERT_OK(source_conn_->Execute("INSERT INTO t1 VALUES (1, 10)"));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto ts_data_size = GetTsDataSize();
    auto ts_active_data_size = GetTsActiveDataSize();
    return ts_data_size > 0 && ts_data_size == ts_active_data_size;
  }, 30s, "Wait for both data sizes to be equal and non-zero"));

  // Active data size should be less than total data size after dropping a table which is retained
  // by a snapshot schedule.
  ASSERT_OK(source_conn_->Execute("DROP TABLE t1"));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return GetTsDataSize() > GetTsActiveDataSize();
  }, 30s, "Wait for active data size to be less than total data size"));

  ASSERT_OK(DeleteSnapshotSchedule(master_backup_proxy_.get(), schedule_id_));
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return GetTsDataSize() == GetTsActiveDataSize();
  }, 30s, "Wait for both data sizes to be equal"));
}

// Test that hard links are not double-counted by the data size metric updater.
TEST_F(TsDataSizeMetricsTest, Hardlinks) {
  // Drop the table to get the size of just the transaction table.
  ASSERT_OK(source_conn_->ExecuteFormat("DROP TABLE $0", kSourceTableName));
  SleepFor(FLAGS_data_size_metric_updater_interval_sec * 2s);
  auto baseline_size = GetTsActiveDataSize();
  ASSERT_LT(baseline_size, GetTsDataSize());

  // Recreate the table, insert some data, and measure its size.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", kSourceTableName));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO $0 VALUES (generate_series(1,1000),generate_series(1,1000))", kSourceTableName));
  FlushAndCompactTablets();
  SleepFor(FLAGS_data_size_metric_updater_interval_sec * 2s);
  auto table_size = GetTsActiveDataSize() - baseline_size;
  ASSERT_GE(table_size, 0);

  // Clone the database. The size should not increase by the table size.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
  SleepFor(FLAGS_data_size_metric_updater_interval_sec * 2s);
  auto size_after_clone = GetTsActiveDataSize();
  ASSERT_LT(size_after_clone - baseline_size, 2 * table_size);
}

TEST_P(PgCloneTestWithColocatedDBParam, CloneWithAlterTableSchema) {
  // Clone to a time before a schema change happened.
  // Writes some data before time t and alter the table schema after t and add some data according
  // to the new schema. Verifies that the cloning as of t creates a clone with the correct schema
  // and only the first row.
  const std::tuple<int32_t, int32_t> kRow = {1, 10};
  const std::tuple<int32_t, int32_t, int32_t> kRowNewSchema = {2, 20, 200};
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRow), std::get<1>(kRow)));

  // Write a second row after recording the hybrid time.
  auto ht = ASSERT_RESULT(GetCurrentTime()).ToInt64();

  ASSERT_OK(source_conn_->ExecuteFormat("ALTER TABLE t1 ADD COLUMN c1 INT"));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1, $2)", std::get<0>(kRowNewSchema), std::get<1>(kRowNewSchema),
      std::get<2>(kRowNewSchema)));

  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName, ht));

  // Verify clone only has the first row.
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  auto rows = ASSERT_RESULT((target_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(rows[0], kRow);
}

TEST_F(PgCloneTest, CloneYsqlDbTimeout) {
  // Inject an artificial delay that would make CREATE DATABASE timeout in case clone is using
  // a timeout other than ysql_clone_pg_schema_rpc_timeout_ms.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_clone_pg_schema_delay_ms) =
      FLAGS_yb_client_admin_operation_timeout_sec * 1000;
  auto status = source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName);
  ASSERT_OK(status);
}

TEST_F(PgCloneTest, AbortMessage) {
  // Assert that we propagate the error message from the clone operation to the user.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_clone_pg_schema) = true;
  auto status = source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "fail_clone_pg_schema");
}

TEST_F(PgCloneTest, CloneTimeoutExceeded) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_clone_pg_schema_delay_ms) = 10000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_clone_pg_schema_rpc_timeout_ms) = 1000;
  auto status = source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName);
  ASSERT_NOK(status);

  // We have to wait here because the client uses FLAGS_ysql_clone_pg_schema_rpc_timeout_ms as its
  // timeout too, so if it times out before the clone does, the clone will not be in the ABORTED
  // state when we check.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto row = VERIFY_RESULT(source_conn_->FetchRowAsString(
        "SELECT db_name, parent_db_name, state FROM yb_database_clones()"));
    return row == Format("$0, $1, ABORTED", kTargetNamespaceName1, kSourceNamespaceName);
  }, 10s, "Wait for clone to be aborted"));
  auto error_msg = ASSERT_RESULT((source_conn_->FetchRowAsString(
      "SELECT failure_reason FROM yb_database_clones()")));
  ASSERT_STR_CONTAINS(error_msg, "timed out");
}

TEST_P(PgCloneTestWithColocatedDBParam, CloneAfterDropTable) {
  // Clone to a time before a drop table and check that the table exists with correct data.
  // 1. Create a table and load some data.
  // 2. Mark time t.
  // 3. Drop table.
  // 4. Clone the database as of time t.
  // 5. Check the table exists in the clone with the correct data.
  const std::vector<std::tuple<int32_t, int32_t>> kRows = {{1, 10}};

  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRows[0]), std::get<1>(kRows[0])));

  auto clone_to_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  // Drop table t1
  ASSERT_OK(source_conn_->ExecuteFormat("DROP TABLE t1"));

  // Perform the clone operation to ht
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      clone_to_time));

  // Verify table t1 exists in the clone database and rows are as of ht1.
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  auto row = ASSERT_RESULT((target_conn.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(row, kRows[0]);
}

TEST_P(PgCloneTestWithColocatedDBParam, CloneAfterDropIndex) {
  // (Auto-Analyze #28427)
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze_infra) = false;
  // Clone to a time before a drop index and check that the index exists with correct data.
  // 1. Create a table and load some data.
  // 2. Create an index on the table.
  // 3. Mark time t.
  // 4. Drop index.
  // 5. Clone the database as of time t.
  // 6. Check the index exists in the clone with the correct data.
  const std::vector<std::tuple<int32_t, int32_t>> kRows = {{1, 10}};
  const std::string kIndexName = "t1_v_idx";

  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRows[0]), std::get<1>(kRows[0])));

  ASSERT_OK(source_conn_->ExecuteFormat("CREATE INDEX $0 ON t1(value)", kIndexName));

  // Scans should use the index now.
  auto is_index_scan = ASSERT_RESULT(
      source_conn_->HasIndexScan(Format("SELECT * FROM t1 where value=$0", std::get<1>(kRows[0]))));
  LOG(INFO) << "Scans uses index scan " << is_index_scan;
  ASSERT_TRUE(is_index_scan);

  auto clone_to_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  ASSERT_OK(source_conn_->ExecuteFormat("DROP INDEX $0", kIndexName));

  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      clone_to_time));

  // Verify table t1 exists in the clone database and that the index is used to fetch the data.
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  is_index_scan = ASSERT_RESULT(
      target_conn.HasIndexScan(Format("SELECT * FROM t1 WHERE value=$0", std::get<1>(kRows[0]))));
  ASSERT_TRUE(is_index_scan);
  auto row = ASSERT_RESULT((target_conn.FetchRow<int32_t, int32_t>(
      Format("SELECT * FROM t1 WHERE value=$0", std::get<1>(kRows[0])))));
  ASSERT_EQ(row, kRows[0]);
}

TEST_P(PgCloneTestWithColocatedDBParam, CloneWithSequences) {
  int kIncrement = 5;
  // First 3 rows will be inserted into source database while the 4th row will be inserted in the
  // clone. The 4th row takes into account that the first "FLAGS_ysql_sequence_cache_minval" values
  // are allocated to the source DB cache.
  const std::vector<std::tuple<int32_t, int32_t>> kRows = {
      {1, 1}, {2, 6}, {3, 11}, {4, kIncrement * FLAGS_ysql_sequence_cache_minval + 1}};
  // Create a sequence and attach it to the column value.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE SEQUENCE value_data INCREMENT $0 OWNED BY t1.value", kIncrement));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "ALTER TABLE t1 ALTER COLUMN value SET DEFAULT nextval('value_data')"));
  ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO t1 (key) VALUES ($0)", std::get<0>(kRows[0])));
  auto clone_to_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO t1 (key) VALUES ($0)", std::get<0>(kRows[1])));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      clone_to_time));
  // Verify table t1 exists in the clone database and rows are as of ht1.
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  auto row = ASSERT_RESULT((target_conn.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(row, kRows[0]);
  // Insert a row on both source and clone. Check the sequence behavior in both.
  auto key = std::get<0>(kRows[2]);
  ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO t1 (key) VALUES ($0)", key));
  row = ASSERT_RESULT(
      (source_conn_->FetchRow<int32_t, int32_t>(Format("SELECT * FROM t1 WHERE key=$0", key))));
  ASSERT_EQ(row, kRows[2]);
  key = std::get<0>(kRows[3]);
  ASSERT_OK(target_conn.ExecuteFormat("INSERT INTO t1 (key) VALUES ($0)", key));
  row = ASSERT_RESULT(
      (target_conn.FetchRow<int32_t, int32_t>(Format("SELECT * FROM t1 WHERE key=$0", key))));
  ASSERT_EQ(row, kRows[3]);
}

TEST_P(PgCloneTestWithColocatedDBParam, CloneWithSequencesAndDdl) {
  auto seq_table_name = "table_with_sequence";
  ASSERT_OK(
      source_conn_->ExecuteFormat("CREATE TABLE $0 (id INT, i2 SERIAL, c1 INT)", seq_table_name));
  ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO $0 (id,c1) VALUES (11,22)", seq_table_name));
  auto clone_to_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  // Run a DDL that increments the last_breaking_version of pg_yb_catalog.
  ASSERT_OK(source_conn_->ExecuteFormat("ALTER TABLE $0 RENAME COLUMN c1 TO c2", seq_table_name));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      clone_to_time));
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  auto row = ASSERT_RESULT(
      (target_conn.FetchRow<int64_t>(Format("SELECT count(*) FROM $0", seq_table_name))));
  ASSERT_EQ(row, 1);
}

// Test yb_database_clones (ysql function to list clones)
TEST_F(PgCloneTest, YsqlListClonesAPI) {
  std::string list_clones_query =
      "SELECT db_oid, db_name, parent_db_oid, parent_db_name, state, failure_reason FROM "
      "yb_database_clones()";
  auto row = ASSERT_RESULT((source_conn_->FetchAllAsString(list_clones_query)));
  ASSERT_TRUE(row.empty());
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
  auto kExpectedCloneRow =
      Format("16386, $0, 16384, $1, COMPLETE, NULL", kTargetNamespaceName1, kSourceNamespaceName);
  row = ASSERT_RESULT((source_conn_->FetchRowAsString(list_clones_query)));
  ASSERT_EQ(row, kExpectedCloneRow);
}

TEST_F(PgCloneTest, TabletSplitting) {
  const int kNumRows = 1000;

  // Test that we are able to clone to:
  // 1. Before the split occurs on the master (when the children are upserted into the sys catalog).
  // 2. After the split occurs on the master server but before the parent is hidden.
  // 3. After the split parent is hidden.
  auto clone_and_validate = [&]
      (const std::string& target_namespace, int64_t timestamp, int expected_num_tablets) -> Status {
    RETURN_NOT_OK(source_conn_->ExecuteFormat(
        "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", target_namespace, kSourceNamespaceName,
        timestamp));
    auto target_conn = VERIFY_RESULT(ConnectToDB(target_namespace));
    auto rows = VERIFY_RESULT((target_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
    SCHECK_EQ(rows.size(), kNumRows, IllegalState, "Number of rows mismatch");
    auto table = VERIFY_RESULT(GetTable("t1", target_namespace));
    SCHECK_EQ(
        VERIFY_RESULT(table->GetTablets()).size(), expected_num_tablets, IllegalState,
        "Number of tablets mismatch");
    return Status::OK();
  };

  // Do not clean up split tablets for now.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = true;

  // Write enough data for a middle key so tablet splitting succeeds.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES (generate_series(1, $0), generate_series(1, $0))", kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  TableInfoPtr source_table = ASSERT_RESULT(GetTable("t1", kSourceNamespaceName));
  auto tablets = ASSERT_RESULT(source_table->GetTablets());
  ASSERT_EQ(tablets.size(), 3);
  auto before_split_timestamp = ASSERT_RESULT(GetCurrentTime()).ToInt64();

  auto split_tablet_id = tablets[0]->tablet_id();
  ASSERT_OK(SplitTablet(split_tablet_id));

  // Wait for the split to complete on master.
  // The parent should still be running because we have cleanup is still disabled.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(source_table->GetTabletsIncludeInactive()).size() == 5;
  }, 30s, "Wait for master split."));
  auto after_master_split_timestamp = ASSERT_RESULT(GetCurrentTime()).ToInt64();

  // We should have 3 tablets before the master side split, and 4 after.
  ASSERT_OK(clone_and_validate(kTargetNamespaceName1, before_split_timestamp, 3));
  ASSERT_OK(clone_and_validate(kTargetNamespaceName2, after_master_split_timestamp, 4));

  // Enable cleanup of split parents and wait for the split parent to be deleted.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_skip_deleting_split_tablets) = false;
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tablets = VERIFY_RESULT(source_table->GetTabletsIncludeInactive());
    for (auto& tablet : tablets) {
      if (tablet->id() == split_tablet_id) {
        return tablet->LockForRead()->is_hidden();
      }
    }
    return STATUS_FORMAT(NotFound, "Split parent tablet $0 not found", split_tablet_id);
  }, 30s, "Wait for split parent to be hidden."));
  auto parent_hidden_timestamp = ASSERT_RESULT(GetCurrentTime()).ToInt64();

  // Clone to after the split parent was hidden. We should have 4 child tablets.
  ASSERT_OK(clone_and_validate("testdb_clone3", parent_hidden_timestamp, 4));
}

TEST_F(PgCloneTest, TabletSplittingWithIndex) {
  // Test that we can clone after splitting an index.
  // Write enough data for a middle key so tablet splitting succeeds.
  const int kNumRows = 1000;
  ASSERT_OK(source_conn_->Execute("CREATE INDEX i1 ON t1(value)"));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES (generate_series(1, $0), generate_series(1, $0))", kNumRows));
  ASSERT_OK(cluster_->FlushTablets());

  // Split an index tablet.
  TableInfoPtr source_index = ASSERT_RESULT(GetTable("i1", kSourceNamespaceName));
  auto tablets = ASSERT_RESULT(source_index->GetTablets());
  ASSERT_EQ(tablets.size(), 3);
  auto split_tablet_id = tablets[0]->tablet_id();
  ASSERT_OK(SplitTablet(split_tablet_id));

  // Wait for split to complete.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    return VERIFY_RESULT(source_index->GetTablets()).size() == 4;
  }, 30s, "Wait for split to complete."));

  // Clone.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
  ASSERT_RESULT(GetTable("i1", kTargetNamespaceName1));
}

TEST_F(PgCloneTest, UserIsSet) {
  // Test that the user is set to the user running the clone operation.
  ASSERT_OK(source_conn_->Execute("CREATE ROLE test_user WITH LOGIN PASSWORD 'test'"));
  ASSERT_OK(source_conn_->Execute("ALTER ROLE test_user SUPERUSER"));
  ASSERT_OK(source_conn_->Execute("SET ROLE test_user"));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));

  auto owner_query = Format(
      "SELECT pg_get_userbyid(datdba) FROM pg_database WHERE datname = '$0'",
      kTargetNamespaceName1);
  auto owner = ASSERT_RESULT(source_conn_->FetchRows<std::string>(owner_query));
  ASSERT_EQ(owner.size(), 1);
  ASSERT_EQ(owner[0], "test_user");
}

TEST_F(PgCloneTest, PreventConnectionsUntilCloneSuccessful) {
  // Test that we prevent connections to the target DB until the clone operation is successful.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fail_clone_tablets) = true;
  auto status = source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName);
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.message().ToBuffer(), "fail_clone_tablets");

  auto result = ConnectToDB(kTargetNamespaceName1, 3 /* connection timeout */);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(
      result.status().message().ToBuffer(),
      Format("database \"$0\" is not currently accepting connections", kTargetNamespaceName1));
}

TEST_F(PgCloneTest, Tablespaces) {
  const auto kTablespaceName = "test_tablespace";
  // With (index_ + 1) / FLAGS_TEST_nodes_per_cloud formula, ts-0 (index_=1) is in cloud1.region1
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE TABLESPACE $0 WITH (replica_placement='{\"num_replicas\": 1, \"placement_blocks\": "
      "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone\","
      "\"min_num_replicas\":1}]}')", kTablespaceName));
  auto* catalog_mgr = cluster_->mini_master()->master()->catalog_manager();

  // Wait for n running replicas of each tablet of table t1 in the given namespace.
  auto wait_for_n_replicas = [&](const std::string& namespace_name, int n) {
    return WaitFor([&]() -> Result<bool> {
      GetTableLocationsRequestPB req;
      req.mutable_table()->set_table_id(VERIFY_RESULT(GetTable("t1", namespace_name))->id());
      GetTableLocationsResponsePB resp;
      RETURN_NOT_OK(catalog_mgr->GetTableLocations(&req, &resp));
      SCHECK(!resp.tablet_locations().empty(), IllegalState, "No tablets found");
      for (const auto& tablet_locs : resp.tablet_locations()) {
        if (tablet_locs.replicas_size() != n) return false;
        for (const auto& replica : tablet_locs.replicas()) {
          if (replica.state() != tablet::RaftGroupStatePB::RUNNING) return false;
        }
      }
      return true;
    }, 30s, Format("Wait for $0 replicas", n));
  };
  auto before_tablespace_timestamp = ASSERT_RESULT(GetCurrentTime()).ToInt64();

  // Attach the tablespace and wait for there to only be 1 tablet replica for t1.
  ASSERT_OK(source_conn_->ExecuteFormat("ALTER TABLE t1 SET TABLESPACE $0", kTablespaceName));
  ASSERT_OK(wait_for_n_replicas(kSourceNamespaceName, 1));

  // Clone before and after the table was added to the tablespace.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      before_tablespace_timestamp));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName2, kSourceNamespaceName));

  // Wait for the table in the first DB to have 3 replicas (since it was created before the
  // tablespace was added). The table in the second DB should have the tablespace and thus only 1
  // replica.
  ASSERT_OK(wait_for_n_replicas(kTargetNamespaceName1, 3));
  ASSERT_OK(wait_for_n_replicas(kTargetNamespaceName2, 1));
}

class PgCloneMultiMaster : public PgCloneTest {
  virtual void OverrideMiniClusterOptions(MiniClusterOptions* options) override {
    options->num_masters = 3;
  }
};

TEST_F(PgCloneMultiMaster, CloneAfterMasterChange) {
  const std::tuple<int32_t, int32_t> kRow = {1, 10};
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRow), std::get<1>(kRow)));
  auto clone_to_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO t1 VALUES (2, 2)"));
  ASSERT_OK(source_conn_->ExecuteFormat("DROP TABLE t1"));

  ASSERT_OK(cluster_->StepDownMasterLeader());
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      clone_to_time));

  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  auto row = ASSERT_RESULT((target_conn.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(row, kRow);
}

class PgCloneColocationTest : public PgCloneTest {
  virtual void OverrideMiniClusterOptions(MiniClusterOptions* options) override {
    options->num_masters = 3;
  }

  virtual Status CreateProxies() override {
    messenger_ = VERIFY_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());
    master_admin_proxy_ = std::make_unique<MasterAdminProxy>(
        proxy_cache_.get(), VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    master_backup_proxy_ = std::make_shared<MasterBackupProxy>(
        proxy_cache_.get(), VERIFY_RESULT(mini_cluster()->GetLeaderMiniMaster())->bound_rpc_addr());
    return Status::OK();
  }

  virtual master::YsqlColocationConfig ColocateDatabase() override {
    return master::YsqlColocationConfig::kDBColocated;
  }
};

class PgCloneTestWithColocatedDBTabletLimitsCheck : public PgCloneTestWithColocatedDBParam {
 public:
  void SetUp() override {
    // Simulate a single core cluster.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 1;
    PgCloneTestWithColocatedDBParam::SetUp();
  }
};

INSTANTIATE_TEST_CASE_P(
  Colocation, PgCloneTestWithColocatedDBTabletLimitsCheck,
  ::testing::Values(
      master::YsqlColocationConfig::kNotColocated, master::YsqlColocationConfig::kDBColocated));

TEST_P(PgCloneTestWithColocatedDBTabletLimitsCheck, TabletLimitsCheck) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_replicas_per_core_limit) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_tablet_replica_limits) = true;
  ASSERT_NOK_STR_CONTAINS(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName),
      "to exceed the safe system maximum");

  // If we don't hit a limit, the clone should succeed.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_replicas_per_core_limit) = 1000;
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
}

class PgCloneColocationTestWithTabletLimitsCheck : public PgCloneColocationTest {
 public:
  void SetUp() override {
    // Simulate a single core cluster.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_cpus) = 1;
    PgCloneColocationTest::SetUp();
  }
};

TEST_F(PgCloneColocationTestWithTabletLimitsCheck, TabletLimitsColocated) {
  // We should only count physical tables when checking the tablet limits.
  auto leader_master = ASSERT_RESULT(cluster_->GetLeaderMiniMaster());
  auto cluster_info = ComputeAggregatedClusterInfo(
      leader_master->catalog_manager().GetAllLiveNotBlacklistedTServers(), BlacklistSet(), "");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enforce_tablet_replica_limits) = true;
  // Allow no extra tablet replicas initially.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_replicas_per_core_limit) =
      static_cast<uint32_t>(cluster_info.total_live_replicas) / NumTabletServers();

  // Create some more colocated tables. These should use the existing tablet created in the test
  // setup.
  for (int i = 0; i < 10; i++) {
    ASSERT_OK(source_conn_->ExecuteFormat("CREATE TABLE table_$0 (id INT)", i));
  }

  // The clone should fail because we are at the limit.
  ASSERT_NOK_STR_CONTAINS(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName),
      "to exceed the safe system maximum");
  // Allow for NumTabletServers() more tablet replicas so the clone of the one colocated tablet
  // succeeds.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_tablet_replicas_per_core_limit) =
      static_cast<uint32_t>(cluster_info.total_live_replicas + NumTabletServers());
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
}

// Verify that after a successful clone, the target database is readable even after performing
// master leader failover. This is a sanity check on the persisted tables and tablet sys catalog
// entities in case of cloning a colocated database.
TEST_F(
    PgCloneColocationTest, ReadClonedDatabaseAfterMasterFailover) {
  const std::tuple<int32_t, int32_t> kRowT1 = {1, 10};
  const std::tuple<int32_t, int32_t, int32_t> kRowT2 = {2, 20, 200};
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES ($0, $1)", std::get<0>(kRowT1), std::get<1>(kRowT1)));
  ASSERT_OK(source_conn_->ExecuteFormat("CREATE TABLE t2 ( k int, v1 int, v2 int)"));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t2 VALUES ($0, $1, $2)", std::get<0>(kRowT2), std::get<1>(kRowT2),
      std::get<2>(kRowT2)));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
  // Verify t1 can be read from the cloned database.
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  auto rows = ASSERT_RESULT((target_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(rows[0], kRowT1);
  // Perform a master leader failover.
  LOG(INFO) << Format(
      "Stepping down master leader with permanent UUID: $0",
      ASSERT_RESULT(mini_cluster()->GetLeaderMiniMaster())->permanent_uuid());
  auto new_master_leader = ASSERT_RESULT(mini_cluster()->StepDownMasterLeader());
  LOG(INFO) << Format("The new master leader permanent UUID: $0", new_master_leader);
  // Verify both tables t1 and t2 can be read from the cloned database.
  target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  rows = ASSERT_RESULT((target_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_EQ(rows[0], kRowT1);
  auto rows_t2 =
      ASSERT_RESULT((target_conn.FetchRows<int32_t, int32_t, int32_t>("SELECT * FROM t2")));
  ASSERT_EQ(rows_t2.size(), 1);
  ASSERT_EQ(rows_t2[0], kRowT2);
}

TEST_P(PgCloneTestWithColocatedDBParam, CreateTableAfterClone) {
  ASSERT_OK(source_conn_->ExecuteFormat("INSERT INTO t1 VALUES (1, 1)"));

  auto clone_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  ASSERT_OK(source_conn_->Execute("CREATE TABLE t2 (k int, value int)"));
  ASSERT_OK(source_conn_->Execute("CREATE INDEX i2 on t2(value)"));

  // Clone before t2 and i2 were created.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      clone_time));
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));

  // Test that we can recreate dropped tables and create brand new tables, with indexes.
  ASSERT_OK(target_conn.Execute("CREATE TABLE t2 (k int, value int)"));
  ASSERT_OK(target_conn.Execute("CREATE TABLE t3 (k int, value int)"));
  ASSERT_OK(target_conn.Execute("CREATE INDEX i1 on t1(value)"));
  ASSERT_OK(target_conn.Execute("CREATE INDEX i2 on t2(value)"));
  ASSERT_OK(target_conn.Execute("CREATE INDEX i3 on t3(value)"));

  // Test that we can insert into all tables.
  ASSERT_OK(target_conn.Execute("INSERT INTO t1 VALUES (2, 2)"));
  ASSERT_OK(target_conn.Execute("INSERT INTO t2 VALUES (1, 1)"));
  ASSERT_OK(target_conn.Execute("INSERT INTO t3 VALUES (1, 1)"));
}

TEST_P(PgCloneTestWithColocatedDBParam, CloneOfClone) {
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      master_backup_proxy_.get(), YQL_DATABASE_PGSQL, kTargetNamespaceName1, kInterval, kRetention,
      kTimeout));
  ASSERT_OK(WaitScheduleSnapshot(master_backup_proxy_.get(), schedule_id, kTimeout));

  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  ASSERT_OK(target_conn.Execute("CREATE TABLE t2 (k int, v1 int)"));
  ASSERT_OK(target_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName2, kTargetNamespaceName1));
  ASSERT_RESULT(ConnectToDB(kTargetNamespaceName2));
}

TEST_F(PgCloneColocationTest, NoColocatedChildTables) {
  ASSERT_OK(source_conn_->Execute("CREATE TABLE t2(k int, v1 int) WITH (COLOCATION = false)"));
  ASSERT_OK(source_conn_->Execute("DROP TABLE t1"));
  auto no_child_tables_time = ASSERT_RESULT(GetCurrentTime()).ToInt64();
  ASSERT_OK(source_conn_->Execute("DROP TABLE t2"));

  // Clone to a time when there are no colocated child tables.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      no_child_tables_time));
  ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));

  // Clone to a time when there are no tables.
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName2, kSourceNamespaceName));
  ASSERT_RESULT(ConnectToDB(kTargetNamespaceName2));
}

TEST_F_EX(PgCloneTest, ClonePartitionedTableOidCollision, PgCloneInitiallyEmptyDBTest) {
  // Regression test for GitHub issue #29335.
  // Create a partitioned table with many partitions, an index, and CHECK constraints
  // in the source DB. ysql_dump's binary_upgrade mode sets OIDs for pg_class and pg_type entries,
  // but CHECK constraint OIDs in pg_constraint are always dynamically allocated via
  // GetNewObjectId. This forces the tserver to call ReservePgsqlOids during the clone's
  // DDL replay, populating its OID cache with a stale range. This should be invalidated after
  // the clone so if any objects are dropped and recreated, they will get a new OID instead of
  // colliding with the hidden objects.
  auto create_partitioned_table = [&](pgwrapper::PGConn& conn) -> Status {
    RETURN_NOT_OK(conn.Execute(
        "CREATE TABLE t (key INT, value INT, CHECK (key >= 0), CHECK (value >= 0)) "
        "PARTITION BY RANGE (key)"));
    for (int i = 0; i < 10; i++) {
      RETURN_NOT_OK(conn.ExecuteFormat(
          "CREATE TABLE t_partition_$0 PARTITION OF t FOR VALUES FROM ($1) TO ($2)",
          i, i * 100, (i + 1) * 100));
    }
    RETURN_NOT_OK(conn.Execute("CREATE INDEX t_idx ON t (value)"));
    return Status::OK();
  };

  ASSERT_OK(create_partitioned_table(*source_conn_));
  ASSERT_OK(source_conn_->ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1", kTargetNamespaceName1, kSourceNamespaceName));

  // Create a snapshot schedule on the cloned DB so DROP will HIDE tables.
  ASSERT_OK(CreateSnapshotSchedule(
      master_backup_proxy_.get(), YQL_DATABASE_PGSQL, kTargetNamespaceName1,
      kInterval, kRetention, kTimeout));
  auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
  ASSERT_OK(target_conn.Execute("DROP TABLE t CASCADE"));

  // Recreate the table.
  ASSERT_OK(create_partitioned_table(target_conn));
}

TEST_F(PgCloneTest, CloneAfterSuccessiveRenames) {
  const std::string kRenamedNamespaceName = "testdb_renamed";

  ASSERT_OK(source_conn_->ExecuteFormat(
      "INSERT INTO t1 VALUES (1, 10)"));

  // Rename requires disconnecting from the source DB first.
  source_conn_.reset();
  auto default_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(default_conn.ExecuteFormat(
      "ALTER DATABASE $0 RENAME TO $1", kSourceNamespaceName, kRenamedNamespaceName));

  auto timestamp = ASSERT_RESULT(GetCurrentTime());

  // Clone using the renamed DB name. This should succeed.
  ASSERT_OK(default_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kRenamedNamespaceName,
      timestamp.ToInt64()));
  {
    auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
    auto row = ASSERT_RESULT((target_conn.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
    ASSERT_EQ(row, (std::tuple<int32_t, int32_t>{1, 10}));
  }
  ASSERT_OK(default_conn.ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName1));

  // Rename the DB back to the original name.
  ASSERT_OK(default_conn.ExecuteFormat(
      "ALTER DATABASE $0 RENAME TO $1", kRenamedNamespaceName, kSourceNamespaceName));

  // Clone using the current (original) name.
  ASSERT_OK(default_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName1, kSourceNamespaceName,
      timestamp.ToInt64()));
  {
    auto target_conn = ASSERT_RESULT(ConnectToDB(kTargetNamespaceName1));
    auto row = ASSERT_RESULT((target_conn.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
    ASSERT_EQ(row, (std::tuple<int32_t, int32_t>{1, 10}));
  }
  ASSERT_OK(default_conn.ExecuteFormat("DROP DATABASE $0", kTargetNamespaceName1));

  // Clone using the old renamed name.
  auto status = default_conn.ExecuteFormat(
      "CREATE DATABASE $0 TEMPLATE $1 AS OF $2", kTargetNamespaceName2, kRenamedNamespaceName,
      timestamp.ToInt64());
  // The renamed name no longer exists, so this should fail.
  ASSERT_NOK(status);
}

}  // namespace master
}  // namespace yb
