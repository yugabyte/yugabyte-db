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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <google/protobuf/util/message_differencer.h>

#include "yb/client/client.h"
#include "yb/client/schema.h"
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
#include "yb/master/leader_epoch.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_types.pb.h"
#include "yb/master/mini_master.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_context.h"

#include "yb/tools/admin-test-base.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/backoff_waiter.h"

#include "yb/util/test_macros.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(enable_db_clone);
DECLARE_bool(master_auto_run_initdb);
DECLARE_int32(pgsql_proxy_webserver_port);

namespace yb {
namespace master {

constexpr auto kInterval = 6s;
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

Result<TxnSnapshotId> WaitNewSnapshot(MasterBackupProxy* proxy, const SnapshotScheduleId& id) {
  LOG(INFO) << "WaitNewSnapshot, schedule id: " << id;
  std::string last_snapshot_id;
  std::string new_snapshot_id;
  RETURN_NOT_OK(WaitFor(
      [&proxy, &id, &last_snapshot_id, &new_snapshot_id]() -> Result<bool> {
        // If there's a master leader failover then we should wait for the next cycle.
        auto schedule_info = VERIFY_RESULT(GetSnapshotSchedule(proxy, id));
        auto& snapshots = schedule_info.snapshots();
        if (snapshots.empty()) {
          return false;
        }
        auto snapshot_id = snapshots[snapshots.size() - 1].id();
        LOG(INFO) << "WaitNewSnapshot, last snapshot id: " << snapshot_id;
        if (last_snapshot_id.empty()) {
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
  return FullyDecodeTxnSnapshotId(new_snapshot_id);
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
  }

 protected:
  std::unique_ptr<client::YBClient> client_;
};

TEST_F(MasterSnapshotTest, FailSysCatalogWriteWithStaleTable) {
  auto messenger = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
  auto proxy_cache = rpc::ProxyCache(messenger.get());
  auto proxy = MasterBackupProxy(&proxy_cache, cluster_->mini_master()->bound_rpc_addr());

  auto first_epoch = LeaderEpoch(
      cluster_->mini_master()->catalog_manager().leader_ready_term(),
      cluster_->mini_master()->sys_catalog().pitr_count());
  const auto timeout = MonoDelta::FromSeconds(20);
  client::YBTableName table_name(YQL_DATABASE_CQL, "my_keyspace", "test_table");
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      table_name.namespace_name(), table_name.namespace_type()));
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      &proxy, table_name.namespace_type(), table_name.namespace_name(), MonoDelta::FromSeconds(60),
      MonoDelta::FromSeconds(600), timeout));
  ASSERT_OK(WaitScheduleSnapshot(&proxy, schedule_id, timeout));

  auto table_creator = client_->NewTableCreator();
  client::YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT32)->NotNull()->HashPrimaryKey();
  b.AddColumn("v1")->Type(DataType::INT64)->NotNull();
  b.AddColumn("v2")->Type(DataType::STRING)->NotNull();
  client::YBSchema schema;
  ASSERT_OK(b.Build(&schema));
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
  auto restoration_id = ASSERT_RESULT(RestoreSnapshotSchedule(&proxy, schedule_id, ht, timeout));
  LOG(INFO) << "Waiting for restoration.";
  ASSERT_OK(WaitForRestoration(&proxy, restoration_id, timeout));

  LOG(INFO) << "Restoration finished.";
  {
    auto table_lock = table_info->LockForWrite();
    table_lock.mutable_data()->pb.set_parent_table_id("fnord");
    LOG(INFO) << Format(
        "Writing with stale epoch: $0, $1",
        first_epoch.leader_term,
        first_epoch.pitr_count);
    ASSERT_NOK(cluster_->mini_master()->sys_catalog().Upsert(first_epoch, table_info));
    auto post_restore_epoch = LeaderEpoch(
        cluster_->mini_master()->catalog_manager().leader_ready_term(),
        cluster_->mini_master()->sys_catalog().pitr_count());
    LOG(INFO) << Format(
        "Writing with fresh epoch: $0, $1", post_restore_epoch.leader_term,
        post_restore_epoch.pitr_count);
    ASSERT_OK(cluster_->mini_master()->sys_catalog().Upsert(post_restore_epoch, table_info));
  }
  messenger->Shutdown();
}

class PgSnapshotTest : public YBTest {
 public:
  void SetUp() override {
    master::SetDefaultInitialSysCatalogSnapshotFlags();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_auto_run_initdb) = true;
    YBTest::SetUp();
    MiniClusterOptions opts;
    opts.num_tablet_servers = 3;

    test_cluster_.mini_cluster_ = std::make_unique<MiniCluster>(opts);

    ASSERT_OK(mini_cluster()->Start());
    ASSERT_OK(mini_cluster()->WaitForTabletServerCount(3));
    ASSERT_OK(WaitForInitDb(mini_cluster()));
    test_cluster_.client_ = ASSERT_RESULT(mini_cluster()->CreateClient());
    ASSERT_OK(InitPostgres(&test_cluster_));

    LOG(INFO) << "Cluster created successfully";
  }

  void TearDown() override {
    YBTest::TearDown();

    LOG(INFO) << "Destroying cluster";

    if (test_cluster_.pg_supervisor_) {
      test_cluster_.pg_supervisor_->Stop();
    }
    if (test_cluster_.mini_cluster_) {
      test_cluster_.mini_cluster_->Shutdown();
      test_cluster_.mini_cluster_.reset();
    }
    test_cluster_.client_.reset();
  }

  Status InitPostgres(PostgresMiniCluster* cluster) {
    auto pg_ts = RandomElement(cluster->mini_cluster_->mini_tablet_servers());
    auto port = cluster->mini_cluster_->AllocateFreePort();
    pgwrapper::PgProcessConf pg_process_conf =
        VERIFY_RESULT(pgwrapper::PgProcessConf::CreateValidateAndRunInitDb(
            AsString(Endpoint(pg_ts->bound_rpc_addr().address(), port)),
            pg_ts->options()->fs_opts.data_paths.front() + "/pg_data",
            pg_ts->server()->GetSharedMemoryFd()));
    pg_process_conf.master_addresses = pg_ts->options()->master_addresses_flag;
    pg_process_conf.force_disable_log_file = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pgsql_proxy_webserver_port) =
        cluster->mini_cluster_->AllocateFreePort();

    LOG(INFO) << "Starting PostgreSQL server listening on " << pg_process_conf.listen_addresses
              << ":" << pg_process_conf.pg_port << ", data: " << pg_process_conf.data_dir
              << ", pgsql webserver port: " << FLAGS_pgsql_proxy_webserver_port;
    cluster->pg_supervisor_ =
        std::make_unique<pgwrapper::PgSupervisor>(pg_process_conf, nullptr /* tserver */);
    RETURN_NOT_OK(cluster->pg_supervisor_->Start());

    cluster->pg_host_port_ = HostPort(pg_process_conf.listen_addresses, pg_process_conf.pg_port);
    return Status::OK();
  }
  MiniCluster* mini_cluster() { return test_cluster_.mini_cluster_.get(); }

  client::YBClient* client() { return test_cluster_.client_.get(); }
  Status CreateDatabase(
      PostgresMiniCluster* cluster, const std::string& namespace_name,
      YsqlColocationConfig colocated) {
    auto conn = VERIFY_RESULT(cluster->Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(
        "CREATE DATABASE $0$1", namespace_name,
        colocated == YsqlColocationConfig::kDBColocated ? " with colocation = true" : ""));
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

 protected:
  PostgresMiniCluster test_cluster_;
};

class MasterExportSnapshotTest : public PgSnapshotTest,
                                 public ::testing::WithParamInterface<YsqlColocationConfig> {};

INSTANTIATE_TEST_CASE_P(
    Colocation, MasterExportSnapshotTest,
    ::testing::Values(YsqlColocationConfig::kNotColocated, YsqlColocationConfig::kDBColocated));

// Test that export_snapshot_from_schedule as of time generates correct SnapshotInfoPB.
// 1. Create some tables.
// 2. Mark time t and wait for a new snapshot to be created as part of the snapshot schedule.
// 3. export_snapshot to generate the SnapshotInfoPB as of current time. It is the traditional
// export_snapshot command (not the new command) to serve as ground truth.
// 4. Create more tables.
// 5. Generate snapshotInfo from schedule using the time t.
// 6. Assert the output of 5 and 3 are the same.
TEST_P(MasterExportSnapshotTest, ExportSnapshotAsOfTime) {
  const auto kNamespaceName = "testdb";
  ASSERT_OK(CreateDatabase(&test_cluster_, kNamespaceName, GetParam()));
  LOG(INFO) << "Database created.";
  auto messenger = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
  auto proxy_cache = rpc::ProxyCache(messenger.get());
  auto proxy = MasterBackupProxy(&proxy_cache, mini_cluster()->mini_master()->bound_rpc_addr());

  const auto timeout = MonoDelta::FromSeconds(30);
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      &proxy, YQL_DATABASE_PGSQL, kNamespaceName, kInterval, kRetention, timeout));
  ASSERT_OK(WaitScheduleSnapshot(&proxy, schedule_id, timeout));
  auto conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kNamespaceName));
  // 1.
  LOG(INFO) << Format("Create tables t1,t2");
  ASSERT_OK(conn.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(conn.Execute("CREATE TABLE t2 (key INT PRIMARY KEY, c1 TEXT, c2 TEXT)"));
  // 2.
  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << Format("current timestamp is: {$0}", time);

  // 3.
  auto decoded_snapshot_id = ASSERT_RESULT(WaitNewSnapshot(&proxy, schedule_id));
  ASSERT_OK(WaitForSnapshotComplete(&proxy, decoded_snapshot_id));
  master::SnapshotInfoPB ground_truth = ASSERT_RESULT(ExportSnapshot(&proxy, decoded_snapshot_id));
  // 4.
  ASSERT_OK(conn.Execute("CREATE TABLE t3 (key INT PRIMARY KEY, c1 INT, c2 TEXT, c3 TEXT)"));
  ASSERT_OK(conn.Execute("ALTER TABLE t2 ADD COLUMN new_col TEXT"));
  // 5.
  LOG(INFO) << Format(
      "Exporting snapshot from snapshot schedule: $0, Hybrid time = $1", schedule_id, time);
  auto deadline = CoarseMonoClock::Now() + timeout;
  master::SnapshotInfoPB snapshot_info_as_of_time = ASSERT_RESULT(
      mini_cluster()->mini_master()->catalog_manager_impl().GenerateSnapshotInfoFromSchedule(
          schedule_id, HybridTime::FromMicros(static_cast<uint64>(time.ToInt64())), deadline));
  // 6.
  LOG(INFO) << Format("SnapshotInfoPB ground_truth: $0", ground_truth.ShortDebugString());
  LOG(INFO) << Format(
      "SnapshotInfoPB as of time=$0 :$1", time, snapshot_info_as_of_time.ShortDebugString());
  ASSERT_TRUE(pb_util::ArePBsEqual(
      std::move(ground_truth), std::move(snapshot_info_as_of_time), /* diff_str */ nullptr));
  messenger->Shutdown();
}

class PgCloneTest : public PgSnapshotTest {
 protected:
  void SetUp() override {
    PgSnapshotTest::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_db_clone) = true;
  }

  Status CloneAndWait(
      const master::CloneNamespaceRequestPB& clone_req, const MasterBackupProxy& proxy) {
    rpc::RpcController controller;
    controller.set_timeout(60s);
    master::CloneNamespaceResponsePB clone_resp;
    RETURN_NOT_OK(proxy.CloneNamespace(clone_req, &clone_resp, &controller));
    if (clone_resp.has_error()) {
      return StatusFromPB(clone_resp.error().status());
    }

    // Wait until clone is done.
    master::IsCloneDoneRequestPB done_req;
    master::IsCloneDoneResponsePB done_resp;
    done_req.set_seq_no(clone_resp.seq_no());
    done_req.set_source_namespace_id(clone_resp.source_namespace_id());
    RETURN_NOT_OK(WaitFor([&]() -> Result<bool> {
      controller.Reset();
      RETURN_NOT_OK(proxy.IsCloneDone(done_req, &done_resp, &controller));
      if (done_resp.has_error()) {
        return StatusFromPB(clone_resp.error().status());
      }
      return done_resp.is_done();
    }, 60s, "Wait for clone to finish"));

    return Status::OK();
  }
};

TEST_F(PgCloneTest, Clone) {
  // Basic clone test for PG.
  // Writes some data before time t and some data after t, and verifies that the cloning as of t
  // creates a clone with only the first set of rows, and cloning after t creates a clone with both
  // sets of rows.
  auto conn = ASSERT_RESULT(test_cluster_.Connect());
  const auto kSourceNamespaceName = "testdb";
  const auto kTargetNamespaceName1 = "testdb_clone1";
  const auto kTargetNamespaceName2 = "testdb_clone2";
  const std::vector<std::tuple<int32_t, int32_t>> kRows = {{1, 10}, {2, 20}};
  const auto kTimeout = MonoDelta::FromSeconds(30);

  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kSourceNamespaceName));
  // TODO: remove these CREATEs once ysql_dump is included in CloneNamespace.
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kTargetNamespaceName1));
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kTargetNamespaceName2));

  // Create a snapshot schedule.
  auto messenger = ASSERT_RESULT(rpc::MessengerBuilder("test-msgr").set_num_reactors(1).Build());
  auto proxy_cache = rpc::ProxyCache(messenger.get());
  auto proxy = MasterBackupProxy(&proxy_cache, mini_cluster()->mini_master()->bound_rpc_addr());
  SnapshotScheduleId schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      &proxy, YQL_DATABASE_PGSQL, kSourceNamespaceName, kInterval, kRetention, kTimeout));
  ASSERT_OK(WaitScheduleSnapshot(&proxy, schedule_id, kTimeout));

  // Write a row.
  auto source_conn = ASSERT_RESULT(test_cluster_.ConnectToDB(kSourceNamespaceName));
  ASSERT_OK(source_conn.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(source_conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)",
            std::get<0>(kRows[0]), std::get<1>(kRows[0])));

  // TODO: remove these CREATEs once ysql_dump is included in CloneNamespace.
  auto target_conn1 = ASSERT_RESULT(test_cluster_.ConnectToDB(kTargetNamespaceName1));
  ASSERT_OK(target_conn1.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));
  auto target_conn2 = ASSERT_RESULT(test_cluster_.ConnectToDB(kTargetNamespaceName2));
  ASSERT_OK(target_conn2.Execute("CREATE TABLE t1 (key INT PRIMARY KEY, value INT)"));

  // Write a second row after recording the hybrid time.
  auto ht1 = HybridTime::FromMicros(static_cast<uint64>(ASSERT_RESULT(GetCurrentTime()).ToInt64()));
  ASSERT_OK(source_conn.ExecuteFormat("INSERT INTO t1 VALUES ($0, $1)",
            std::get<0>(kRows[1]), std::get<1>(kRows[1])));
  auto ht2 = HybridTime::FromMicros(static_cast<uint64>(ASSERT_RESULT(GetCurrentTime()).ToInt64()));

  CloneNamespaceRequestPB req;
  NamespaceIdentifierPB source_namespace;
  source_namespace.set_name(kSourceNamespaceName);
  source_namespace.set_database_type(YQLDatabase::YQL_DATABASE_PGSQL);
  *req.mutable_source_namespace() = source_namespace;
  req.set_restore_ht(ht1.ToUint64());
  req.set_target_namespace_name(kTargetNamespaceName1);
  ASSERT_OK(CloneAndWait(req, proxy));

  req.set_restore_ht(ht2.ToUint64());
  req.set_target_namespace_name(kTargetNamespaceName2);
  ASSERT_OK(CloneAndWait(req, proxy));

  // Verify source rows are unchanged.
  auto rows = ASSERT_RESULT((source_conn.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_VECTORS_EQ(rows, kRows);

  // Verify first clone only has the first row.
  auto row = ASSERT_RESULT((target_conn1.FetchRow<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_EQ(row, kRows[0]);

  // Verify second clone has both rows.
  rows = ASSERT_RESULT((target_conn2.FetchRows<int32_t, int32_t>("SELECT * FROM t1")));
  ASSERT_VECTORS_EQ(rows, kRows);

  messenger->Shutdown();
}

}  // namespace master
}  // namespace yb
