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

#include "yb/tools/admin-test-base.h"

#include "yb/client/ql-dml-test-base.h"

#include "yb/common/json_util.h"

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_backup.pb.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/date_time.h"
#include "yb/util/random_util.h"
#include "yb/util/range.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {
namespace tools {

namespace {

const std::string kClusterName = "yugacluster";

constexpr auto kInterval = 6s;
constexpr auto kRetention = 10min;
constexpr auto kHistoryRetentionIntervalSec = 5;

} // namespace

class YbAdminSnapshotScheduleTest : public AdminTestBase {
 public:
  Result<rapidjson::Document> GetSnapshotSchedule(const std::string& id = std::string()) {
    auto out = VERIFY_RESULT(id.empty() ? CallJsonAdmin("list_snapshot_schedules")
                                        : CallJsonAdmin("list_snapshot_schedules", id));
    auto schedules = VERIFY_RESULT(Get(&out, "schedules")).get().GetArray();
    if (schedules.Empty()) {
      return STATUS(NotFound, "Snapshot schedule not found");
    }
    SCHECK_EQ(schedules.Size(), 1, NotFound, "Wrong schedules number");
    rapidjson::Document result;
    result.CopyFrom(schedules[0], result.GetAllocator());
    return result;
  }

  Result<rapidjson::Document> ListSnapshots() {
    auto out = VERIFY_RESULT(CallJsonAdmin("list_snapshots", "JSON"));
    rapidjson::Document result;
    result.CopyFrom(VERIFY_RESULT(Get(&out, "snapshots")).get(), result.GetAllocator());
    return result;
  }

  Result<rapidjson::Document> WaitScheduleSnapshot(
      MonoDelta duration, const std::string& id = std::string(), int num_snashots = 1) {
    rapidjson::Document result;
    RETURN_NOT_OK(WaitFor([this, id, num_snashots, &result]() -> Result<bool> {
      auto schedule = VERIFY_RESULT(GetSnapshotSchedule(id));
      auto snapshots = VERIFY_RESULT(Get(&schedule, "snapshots")).get().GetArray();
      if (snapshots.Size() < num_snashots) {
        return false;
      }
      result.CopyFrom(snapshots[snapshots.Size() - 1], result.GetAllocator());
      return true;
    }, duration, "Wait schedule snapshot"));
    return result;
  }

  CHECKED_STATUS RestoreSnapshotSchedule(const std::string& schedule_id, Timestamp restore_at) {
    auto out = VERIFY_RESULT(CallJsonAdmin(
        "restore_snapshot_schedule", schedule_id, restore_at.ToFormattedString()));
    std::string restoration_id = VERIFY_RESULT(Get(out, "restoration_id")).get().GetString();
    LOG(INFO) << "Restoration id: " << restoration_id;

    return WaitRestorationDone(restoration_id, 20s);
  }

  CHECKED_STATUS WaitRestorationDone(const std::string& restoration_id, MonoDelta timeout) {
    return WaitFor([this, restoration_id]() -> Result<bool> {
      auto out = VERIFY_RESULT(CallJsonAdmin("list_snapshot_restorations", restoration_id));
      const auto& restorations = VERIFY_RESULT(Get(out, "restorations")).get().GetArray();
      SCHECK_EQ(restorations.Size(), 1, IllegalState, "Wrong restorations number");
      auto id = VERIFY_RESULT(Get(restorations[0], "id")).get().GetString();
      SCHECK_EQ(id, restoration_id, IllegalState, "Wrong restoration id");
      std::string state_str = VERIFY_RESULT(Get(restorations[0], "state")).get().GetString();
      master::SysSnapshotEntryPB::State state;
      if (!master::SysSnapshotEntryPB_State_Parse(state_str, &state)) {
        return STATUS_FORMAT(IllegalState, "Failed to parse restoration state: $0", state_str);
      }
      if (state == master::SysSnapshotEntryPB::RESTORING) {
        return false;
      }
      if (state == master::SysSnapshotEntryPB::RESTORED) {
        return true;
      }
      return STATUS_FORMAT(IllegalState, "Unexpected restoration state: $0",
                           master::SysSnapshotEntryPB_State_Name(state));
    }, timeout, "Wait restoration complete");
  }

  CHECKED_STATUS PrepareCommon() {
    LOG(INFO) << "Create cluster";
    CreateCluster(kClusterName, ExtraTSFlags(), ExtraMasterFlags());

    LOG(INFO) << "Create client";
    client_ = VERIFY_RESULT(CreateClient());

    return Status::OK();
  }

  virtual std::vector<std::string> ExtraTSFlags() {
    return { Format("--timestamp_history_retention_interval_sec=$0", kHistoryRetentionIntervalSec),
             "--history_cutoff_propagation_interval_ms=1000" };
  }

  virtual std::vector<std::string> ExtraMasterFlags() {
    // To speed up tests.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500",
             "--allow_consecutive_restore=false" };
  }

  Result<std::string> PrepareQl(MonoDelta interval = kInterval, MonoDelta retention = kRetention) {
    RETURN_NOT_OK(PrepareCommon());

    LOG(INFO) << "Create namespace";
    RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(
        client::kTableName.namespace_name(), client::kTableName.namespace_type()));

    return CreateSnapshotScheduleAndWaitSnapshot(
        client::kTableName.namespace_name(), interval, retention);
  }

  Result<std::string> CreateSnapshotScheduleAndWaitSnapshot(
      const std::string& filter, MonoDelta interval, MonoDelta retention) {
    LOG(INFO) << "Create snapshot schedule";
    std::string schedule_id = VERIFY_RESULT(CreateSnapshotSchedule(
        interval, retention, filter));

    LOG(INFO) << "Wait snapshot schedule";
    RETURN_NOT_OK(WaitScheduleSnapshot(30s, schedule_id));

    return schedule_id;
  }

  Result<std::string> PreparePg() {
    RETURN_NOT_OK(PrepareCommon());

    auto conn = VERIFY_RESULT(PgConnect());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", client::kTableName.namespace_name()));

    return CreateSnapshotScheduleAndWaitSnapshot(
        "ysql." + client::kTableName.namespace_name(), kInterval, kRetention);
  }

  Result<pgwrapper::PGConn> PgConnect(const std::string& db_name = std::string()) {
    auto* ts = cluster_->tablet_server(RandomUniformInt(0, cluster_->num_tablet_servers() - 1));
    return pgwrapper::PGConn::Connect(HostPort(ts->bind_host(), ts->pgsql_rpc_port()), db_name);
  }

  Result<std::string> PrepareCql(MonoDelta interval = kInterval, MonoDelta retention = kRetention) {
    RETURN_NOT_OK(PrepareCommon());

    auto conn = VERIFY_RESULT(CqlConnect());
    RETURN_NOT_OK(conn.ExecuteQuery(Format(
        "CREATE KEYSPACE IF NOT EXISTS $0", client::kTableName.namespace_name())));

    return CreateSnapshotScheduleAndWaitSnapshot(
        "ycql." + client::kTableName.namespace_name(), interval, retention);
  }

  template <class... Args>
  Result<std::string> CreateSnapshotSchedule(
      MonoDelta interval, MonoDelta retention, Args&&... args) {
    auto out = VERIFY_RESULT(CallJsonAdmin(
        "create_snapshot_schedule", interval.ToMinutes(), retention.ToMinutes(),
        std::forward<Args>(args)...));

    std::string schedule_id = VERIFY_RESULT(Get(out, "schedule_id")).get().GetString();
    LOG(INFO) << "Schedule id: " << schedule_id;
    return schedule_id;
  }

  CHECKED_STATUS DeleteSnapshotSchedule(const std::string& schedule_id) {
    auto out = VERIFY_RESULT(CallJsonAdmin("delete_snapshot_schedule", schedule_id));

    SCHECK_EQ(VERIFY_RESULT(Get(out, "schedule_id")).get().GetString(), schedule_id, IllegalState,
              "Deleted wrong schedule");
    return Status::OK();
  }

  void TestUndeleteTable(bool restart_masters);

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->bind_to_unique_loopback_addresses = true;
    options->use_same_ts_ports = true;
    options->num_masters = 3;
  }

  std::unique_ptr<CppCassandraDriver> cql_driver_;
};

class YbAdminSnapshotScheduleTestWithYsql : public YbAdminSnapshotScheduleTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
    opts->extra_tserver_flags.emplace_back("--ysql_num_shards_per_tserver=1");
    opts->num_masters = 3;
  }
};

TEST_F(YbAdminSnapshotScheduleTest, BadArguments) {
  BuildAndStart();

  ASSERT_NOK(CreateSnapshotSchedule(
      6s, 10min, kTableName.namespace_name(), kTableName.table_name()));
}

TEST_F(YbAdminSnapshotScheduleTest, Basic) {
  BuildAndStart();

  std::string schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      6s, 10min, kTableName.namespace_name()));
  std::this_thread::sleep_for(20s);

  Timestamp last_snapshot_time;
  ASSERT_OK(WaitFor([this, schedule_id, &last_snapshot_time]() -> Result<bool> {
    auto schedule = VERIFY_RESULT(GetSnapshotSchedule());
    auto received_schedule_id = VERIFY_RESULT(Get(schedule, "id")).get().GetString();
    SCHECK_EQ(schedule_id, received_schedule_id, IllegalState, "Wrong schedule id");
    // Check schedule options.
    auto& options = VERIFY_RESULT(Get(schedule, "options")).get();
    std::string filter = VERIFY_RESULT(
        Get(options, "filter")).get().GetString();
    SCHECK_EQ(filter, Format("ycql.$0", kTableName.namespace_name()),
              IllegalState, "Wrong filter");
    std::string interval = VERIFY_RESULT(
        Get(options, "interval")).get().GetString();
    SCHECK_EQ(interval, "0 min", IllegalState, "Wrong interval");
    std::string retention = VERIFY_RESULT(
        Get(options, "retention")).get().GetString();
    SCHECK_EQ(retention, "10 min", IllegalState, "Wrong retention");
    // Check actual snapshots.
    const auto& snapshots = VERIFY_RESULT(Get(schedule, "snapshots")).get().GetArray();
    if (snapshots.Size() < 2) {
      return false;
    }
    std::string last_snapshot_time_str;
    for (const auto& snapshot : snapshots) {
      std::string snapshot_time = VERIFY_RESULT(
          Get(snapshot, "snapshot_time")).get().GetString();
      if (!last_snapshot_time_str.empty()) {
        std::string previous_snapshot_time = VERIFY_RESULT(
            Get(snapshot, "previous_snapshot_time")).get().GetString();
        SCHECK_EQ(previous_snapshot_time, last_snapshot_time_str, IllegalState,
                  "Wrong previous_snapshot_hybrid_time");
      }
      last_snapshot_time_str = snapshot_time;
    }
    LOG(INFO) << "Last snapshot time: " << last_snapshot_time_str;
    last_snapshot_time = VERIFY_RESULT(DateTime::TimestampFromString(last_snapshot_time_str));
    return true;
  }, 20s, "At least 2 snapshots"));

  last_snapshot_time.set_value(last_snapshot_time.value() + 1);
  LOG(INFO) << "Restore at: " << last_snapshot_time.ToFormattedString();

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, last_snapshot_time));
}

TEST_F(YbAdminSnapshotScheduleTest, Delete) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(kRetention, kRetention));

  auto session = client_->NewSession();
  LOG(INFO) << "Create table";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  LOG(INFO) << "Write values";
  const auto kKeys = Range(100);
  for (auto i : kKeys) {
    ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, i, -i));
  }

  for (auto i : kKeys) {
    ASSERT_OK(client::kv_table_test::DeleteRow(&table_, session, i));
  }

  ASSERT_OK(DeleteSnapshotSchedule(schedule_id));

  ASSERT_OK(WaitFor([this, schedule_id]() -> Result<bool> {
    auto schedule = GetSnapshotSchedule();
    if (!schedule.ok()) {
      if (schedule.status().IsNotFound()) {
        return true;
      }
      return schedule.status();
    }

    auto& options = VERIFY_RESULT(Get(*schedule, "options")).get();
    auto delete_time = VERIFY_RESULT(Get(options, "delete_time")).get().GetString();
    LOG(INFO) << "Delete time: " << delete_time;
    return false;
  }, 10s * kTimeMultiplier, "Snapshot schedule cleaned up"));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(ListSnapshots()).GetArray();
    LOG(INFO) << "Snapshots left: " << snapshots.Size();
    return snapshots.Empty();
  }, 10s * kTimeMultiplier, "Snapshots cleaned up"));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    for (auto* tserver : cluster_->tserver_daemons()) {
      auto proxy = cluster_->GetProxy<tserver::TabletServerAdminServiceProxy>(tserver);
      tserver::FlushTabletsRequestPB req;
      req.set_dest_uuid(tserver->uuid());
      req.set_all_tablets(true);
      tserver::FlushTabletsResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(30s);
      RETURN_NOT_OK(proxy->FlushTablets(req, &resp, &controller));

      req.set_operation(tserver::FlushTabletsRequestPB::COMPACT);
      controller.Reset();
      RETURN_NOT_OK(proxy->FlushTablets(req, &resp, &controller));
    }

    for (auto* tserver : cluster_->tserver_daemons()) {
      auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(tserver);
      tserver::ListTabletsRequestPB req;
      tserver::ListTabletsResponsePB resp;

      rpc::RpcController controller;
      controller.set_timeout(30s);

      RETURN_NOT_OK(proxy->ListTablets(req, &resp, &controller));

      for (const auto& tablet : resp.status_and_schema()) {
        if (tablet.tablet_status().table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
          continue;
        }
        if (tablet.tablet_status().sst_files_disk_size() != 0) {
          LOG(INFO) << "Tablet status: " << tablet.tablet_status().ShortDebugString();
          return false;
        }
      }
    }
    return true;
  }, 1s * kHistoryRetentionIntervalSec * kTimeMultiplier, "Compact SST files"));
}

void YbAdminSnapshotScheduleTest::TestUndeleteTable(bool restart_masters) {
  auto schedule_id = ASSERT_RESULT(PrepareQl());

  auto session = client_->NewSession();
  LOG(INFO) << "Create table";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  LOG(INFO) << "Write values";
  constexpr int kMinKey = 1;
  constexpr int kMaxKey = 100;
  for (int i = kMinKey; i <= kMaxKey; ++i) {
    ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, i, -i));
  }

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  LOG(INFO) << "Delete table";
  ASSERT_OK(client_->DeleteTable(client::kTableName));

  ASSERT_NOK(client::kv_table_test::WriteRow(&table_, session, kMinKey, 0));

  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, kMinKey, 0));

  if (restart_masters) {
    ASSERT_OK(RestartAllMasters(cluster_.get()));
  }

  LOG(INFO) << "Restore schedule";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  ASSERT_OK(table_.Open(client::kTableName, client_.get()));

  LOG(INFO) << "Reading rows";
  auto rows = ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table_, session));
  LOG(INFO) << "Rows: " << AsString(rows);
  ASSERT_EQ(rows.size(), kMaxKey - kMinKey + 1);
  for (int i = kMinKey; i <= kMaxKey; ++i) {
    ASSERT_EQ(rows[i], -i);
  }

  constexpr int kExtraKey = kMaxKey + 1;
  ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, kExtraKey, -kExtraKey));
  auto extra_value = ASSERT_RESULT(client::kv_table_test::SelectRow(&table_, session, kExtraKey));
  ASSERT_EQ(extra_value, -kExtraKey);
}

TEST_F(YbAdminSnapshotScheduleTest, UndeleteTable) {
  TestUndeleteTable(false);
}

TEST_F(YbAdminSnapshotScheduleTest, UndeleteTableWithRestart) {
  TestUndeleteTable(true);
}

TEST_F(YbAdminSnapshotScheduleTest, CleanupDeletedTablets) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(kInterval, kInterval));

  auto session = client_->NewSession();
  LOG(INFO) << "Create table";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  LOG(INFO) << "Write values";
  constexpr int kMinKey = 1;
  constexpr int kMaxKey = 100;
  for (int i = kMinKey; i <= kMaxKey; ++i) {
    ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, i, -i));
  }

  LOG(INFO) << "Delete table";
  ASSERT_OK(client_->DeleteTable(client::kTableName));

  auto deadline = CoarseMonoClock::now() + kInterval + 10s;

  // Wait tablets deleted from tservers.
  ASSERT_OK(Wait([this, deadline]() -> Result<bool> {
    for (int i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
      tserver::ListTabletsRequestPB req;
      tserver::ListTabletsResponsePB resp;
      rpc::RpcController controller;
      controller.set_deadline(deadline);
      RETURN_NOT_OK(proxy->ListTablets(req, &resp, &controller));
      for (const auto& tablet : resp.status_and_schema()) {
        if (tablet.tablet_status().table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE) {
          LOG(INFO) << "Not yet deleted tablet: " << tablet.ShortDebugString();
          return false;
        }
      }
    }
    return true;
  }, deadline, "Deleted tablet cleanup"));

  // Wait table marked as deleted.
  ASSERT_OK(Wait([this, deadline]() -> Result<bool> {
    auto proxy = cluster_->GetLeaderMasterProxy<master::MasterServiceProxy>();
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;
    rpc::RpcController controller;
    controller.set_deadline(deadline);
    req.set_include_not_running(true);
    RETURN_NOT_OK(proxy->ListTables(req, &resp, &controller));
    for (const auto& table : resp.tables()) {
      if (table.table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE
          && table.relation_type() != master::RelationType::SYSTEM_TABLE_RELATION
          && table.state() != master::SysTablesEntryPB::DELETED) {
        LOG(INFO) << "Not yet deleted table: " << table.ShortDebugString();
        return false;
      }
    }
    return true;
  }, deadline, "Deleted table cleanup"));
}

TEST_F_EX(YbAdminSnapshotScheduleTest, YB_DISABLE_TEST_IN_TSAN(Pgsql),
          YbAdminSnapshotScheduleTestWithYsql) {
  auto schedule_id = ASSERT_RESULT(PreparePg());

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));

  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  ASSERT_OK(conn.Execute("UPDATE test_table SET value = 'after'"));

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  auto res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table"));

  ASSERT_EQ(res, "before");
}

TEST_F(YbAdminSnapshotScheduleTest, UndeleteIndex) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
      "WITH transactions = { 'enabled' : true }"));
  ASSERT_OK(conn.ExecuteQuery("CREATE UNIQUE INDEX test_table_idx ON test_table (value)"));

  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (1, 'value')"));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  ASSERT_OK(conn.ExecuteQuery("DROP INDEX test_table_idx"));

  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (3, 'value')"));

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  ASSERT_NOK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (5, 'value')"));

  auto res = ASSERT_RESULT(conn.FetchValue<int32_t>(
      "SELECT key FROM test_table WHERE value = 'value'"));

  ASSERT_EQ(res, 1);
}

// This test is for schema version patching after restore.
// Consider the following scenario, w/o patching:
//
// 1) Create table.
// 2) Add text column to table. Schema version - 1.
// 3) Insert values into table. Each CQL proxy suppose schema version 1 for this table.
// 4) Restore to time between (1) and (2). Schema version - 0.
// 5) Add int column to table. Schema version - 1.
// 6) Try insert values with wrong type into table.
//
// So table has schema version 1, but new column is INT.
// CQL proxy suppose schema version is also 1, but the last column is TEXT.
TEST_F(YbAdminSnapshotScheduleTest, AlterTable) {
  const auto kKeys = Range(10);

  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));

  for (auto key : kKeys) {
    ASSERT_OK(conn.ExecuteQuery(Format(
        "INSERT INTO test_table (key, value) VALUES ($0, 'A')", key)));
  }

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  ASSERT_OK(conn.ExecuteQuery(
      "ALTER TABLE test_table ADD value2 TEXT"));

  for (auto key : kKeys) {
    ASSERT_OK(conn.ExecuteQuery(Format(
        "INSERT INTO test_table (key, value, value2) VALUES ($0, 'B', 'X')", key)));
  }

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  ASSERT_OK(conn.ExecuteQuery(
      "ALTER TABLE test_table ADD value2 INT"));

  for (auto key : kKeys) {
    // It would succeed on some TServers if we would not refresh metadata after restore.
    // But it should not succeed because of last column type.
    ASSERT_NOK(conn.ExecuteQuery(Format(
        "INSERT INTO test_table (key, value, value2) VALUES ($0, 'D', 'Y')", key)));
  }
}

TEST_F(YbAdminSnapshotScheduleTest, TestVerifyRestorationLogic) {
  const auto kKeys = Range(10);

  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));

  for (auto key : kKeys) {
    ASSERT_OK(conn.ExecuteQuery(Format(
        "INSERT INTO test_table (key, value) VALUES ($0, 'A')", key)));
  }

  ASSERT_OK(conn.ExecuteQuery("DROP TABLE test_table"));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table1 (key INT PRIMARY KEY)"));

  for (auto key : kKeys) {
    ASSERT_OK(conn.ExecuteQuery(Format(
        "INSERT INTO test_table1 (key) VALUES ($0)", key)));
  }

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Reconnect because of caching issues with YCQL.
  conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_NOK(conn.ExecuteQuery("SELECT * from test_table"));

  ASSERT_OK(WaitFor([&conn]() -> Result<bool> {
    auto res = conn.ExecuteQuery("SELECT * from test_table1");
    if (res.ok()) {
      return false;
    }
    return true;
  }, 30s * kTimeMultiplier, "Wait for table to be deleted"));
}

TEST_F(YbAdminSnapshotScheduleTest, TestGCHiddenTables) {
  const auto interval = 15s;
  const auto retention = 30s;
  auto schedule_id = ASSERT_RESULT(PrepareQl(interval, retention));

  auto session = client_->NewSession();
  LOG(INFO) << "Create table";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  LOG(INFO) << "Write values";
  constexpr int kMinKey = 1;
  constexpr int kMaxKey = 100;
  for (int i = kMinKey; i <= kMaxKey; ++i) {
    ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, i, -i));
  }

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  LOG(INFO) << "Delete table";
  ASSERT_OK(client_->DeleteTable(client::kTableName));

  ASSERT_NOK(client::kv_table_test::WriteRow(&table_, session, kMinKey, 0));

  LOG(INFO) << "Restore schedule";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  ASSERT_OK(table_.Open(client::kTableName, client_.get()));

  LOG(INFO) << "Reading rows";
  auto rows = ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table_, session));
  LOG(INFO) << "Rows: " << AsString(rows);
  ASSERT_EQ(rows.size(), kMaxKey - kMinKey + 1);
  for (int i = kMinKey; i <= kMaxKey; ++i) {
    ASSERT_EQ(rows[i], -i);
  }

  // Wait for snapshot schedule retention time and verify that GC
  // for the table isn't initiated.
  SleepFor(2*retention);

  Timestamp time1(ASSERT_RESULT(WallClock()->Now()).time_point);

  LOG(INFO) << "Delete table again.";
  ASSERT_OK(client_->DeleteTable(client::kTableName));

  ASSERT_NOK(client::kv_table_test::WriteRow(&table_, session, kMinKey, 0));

  LOG(INFO) << "Restore schedule again.";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time1));

  ASSERT_OK(table_.Open(client::kTableName, client_.get()));

  LOG(INFO) << "Reading rows again.";
  rows = ASSERT_RESULT(client::kv_table_test::SelectAllRows(&table_, session));
  LOG(INFO) << "Rows: " << AsString(rows);
  ASSERT_EQ(rows.size(), kMaxKey - kMinKey + 1);
  for (int i = kMinKey; i <= kMaxKey; ++i) {
    ASSERT_EQ(rows[i], -i);
  }

  // Write and verify the extra row.
  constexpr int kExtraKey = kMaxKey + 1;
  ASSERT_OK(client::kv_table_test::WriteRow(&table_, session, kExtraKey, -kExtraKey));
  auto extra_value = ASSERT_RESULT(client::kv_table_test::SelectRow(&table_, session, kExtraKey));
  ASSERT_EQ(extra_value, -kExtraKey);
}

TEST_F(YbAdminSnapshotScheduleTest, DropKeyspaceAndSchedule) {
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, kInterval));
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));
  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
      "WITH transactions = { 'enabled' : true }"));

  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (1, 'before')"));
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (1, 'after')"));
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
  auto res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table"));
  ASSERT_EQ(res, "before");
  ASSERT_OK(conn.ExecuteQuery("DROP TABLE test_table"));
  // Wait until table completely removed, because of schedule retention.
  std::this_thread::sleep_for(kInterval * 3);
  ASSERT_NOK(conn.ExecuteQuery(Format("DROP KEYSPACE $0", client::kTableName.namespace_name())));
  ASSERT_OK(DeleteSnapshotSchedule(schedule_id));
  ASSERT_OK(conn.ExecuteQuery(Format("DROP KEYSPACE $0", client::kTableName.namespace_name())));
}

TEST_F(YbAdminSnapshotScheduleTest, DeleteIndexOnRestore) {
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, kInterval * 4));

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
      "WITH transactions = { 'enabled' : true }"));

  for (int i = 0; i != 3; ++i) {
    LOG(INFO) << "Iteration: " << i;
    ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (1, 'value')"));
    Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
    ASSERT_OK(conn.ExecuteQuery("CREATE UNIQUE INDEX test_table_idx ON test_table (value)"));
    std::this_thread::sleep_for(kInterval * 2);
    ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
  }

  auto snapshots = ASSERT_RESULT(ListSnapshots());
  LOG(INFO) << "Snapshots:\n" << common::PrettyWriteRapidJsonToString(snapshots);
  std::string id = ASSERT_RESULT(Get(snapshots[0], "id")).get().GetString();
  ASSERT_OK(WaitFor([this, &id]() -> Result<bool> {
    auto snapshots = VERIFY_RESULT(ListSnapshots());
    LOG(INFO) << "Snapshots:\n" << common::PrettyWriteRapidJsonToString(snapshots);
    auto current_id = VERIFY_RESULT(Get(snapshots[0], "id")).get().GetString();
    return current_id != id;
  }, kInterval * 3, "Wait first snapshot to be deleted"));
}

class YbAdminSnapshotScheduleFlushTest : public YbAdminSnapshotScheduleTest {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    // To speed up tests.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500",
             "--flush_rocksdb_on_shutdown=false",
             "--vmodule=tablet_bootstrap=3" };
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, TestSnapshotBootstrap, YbAdminSnapshotScheduleFlushTest) {
  LOG(INFO) << "Create cluster";
  CreateCluster(kClusterName, ExtraTSFlags(), ExtraMasterFlags());

  // Disable modifying flushed frontier when snapshot is created.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_modify_flushed_frontier_snapshot_op", "false"));

  // Create a database and a table.
  auto conn = ASSERT_RESULT(CqlConnect());
  ASSERT_OK(conn.ExecuteQuery(Format(
      "CREATE KEYSPACE IF NOT EXISTS $0", client::kTableName.namespace_name())));

  conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));
  LOG(INFO) << "Created Keyspace and table";

  // Create a CREATE_ON_MASTER op in WALs without flushing frontier.
  ASSERT_OK(CallAdmin("create_keyspace_snapshot",
                      Format("ycql.$0", client::kTableName.namespace_name())));
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));
  LOG(INFO) << "Created snapshot on keyspace";

  // Enable modifying flushed frontier when snapshot is replayed.
  LOG(INFO) << "Resetting test flag to modify flushed frontier";

  // Restart the masters so that this op gets replayed.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_modify_flushed_frontier_snapshot_op", "true"));
  LOG(INFO) << "Restart#1";
  cluster_->Shutdown();
  ASSERT_OK(cluster_->Restart());

  // Restart the masters again. Now this op shouldn't be replayed.
  LOG(INFO) << "Restart#2";
  cluster_->Shutdown();
  ASSERT_OK(cluster_->Restart());
}

TEST_F(YbAdminSnapshotScheduleTest, DisallowConsecutiveRestore) {
  const auto retention = kInterval * 5 * kTimeMultiplier;
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, retention));

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  std::this_thread::sleep_for(500 * 1000 * 1us);

  Timestamp time1(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time1: " << time1;

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));

  auto insert_pattern = Format(
      "INSERT INTO $0 (key, value) VALUES (1, '$$0')", client::kTableName.table_name());
  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "before"));
  Timestamp time2(ASSERT_RESULT(WallClock()->Now()).time_point);
  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "after"));

  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "1,after");

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time1));

  std::this_thread::sleep_for(3s * kTimeMultiplier);

  auto s = conn.ExecuteAndRenderToString(select_expr);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.status().message().ToBuffer(), "Object Not Found");

  Status s2 = RestoreSnapshotSchedule(schedule_id, time2);
  ASSERT_NOK(s2);

  Timestamp time3(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time3: " << time1;

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));

  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "after"));

  rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "1,after");

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time3));

  std::this_thread::sleep_for(3s * kTimeMultiplier);

  s = conn.ExecuteAndRenderToString(select_expr);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.status().message().ToBuffer(), "Object Not Found");
}

}  // namespace tools
}  // namespace yb
