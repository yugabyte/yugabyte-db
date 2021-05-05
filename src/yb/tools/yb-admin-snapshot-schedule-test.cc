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

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/master/master_backup.pb.h"

#include "yb/util/date_time.h"
#include "yb/util/random_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {
namespace tools {

namespace {

Result<const rapidjson::Value&> Get(const rapidjson::Value& value, const char* name) {
  auto it = value.FindMember(name);
  if (it == value.MemberEnd()) {
    return STATUS_FORMAT(InvalidArgument, "Missing $0 field", name);
  }
  return it->value;
}

Result<rapidjson::Value&> Get(rapidjson::Value* value, const char* name) {
  auto it = value->FindMember(name);
  if (it == value->MemberEnd()) {
    return STATUS_FORMAT(InvalidArgument, "Missing $0 field", name);
  }
  return it->value;
}

const std::string kDbName = "ybtest";
const std::string kClusterName = "yugacluster";

} // namespace

class YbAdminSnapshotScheduleTest : public AdminTestBase {
 public:
  Result<rapidjson::Document> GetSnapshotSchedule(const std::string& id = std::string()) {
    auto out = VERIFY_RESULT(id.empty() ? CallJsonAdmin("list_snapshot_schedules")
                                        : CallJsonAdmin("list_snapshot_schedules", id));
    auto schedules = VERIFY_RESULT(Get(&out, "schedules")).get().GetArray();
    SCHECK_EQ(schedules.Size(), 1, IllegalState, "Wrong schedules number");
    rapidjson::Document result;
    result.CopyFrom(schedules[0], result.GetAllocator());
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

  Result<std::string> PreparePg() {
    CreateCluster(kClusterName);
    client_ = VERIFY_RESULT(CreateClient());

    auto conn = VERIFY_RESULT(PgConnect());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDbName));

    auto schedule_id = VERIFY_RESULT(CreateSnapshotSchedule(6s, 10min, "ysql." + kDbName));
    RETURN_NOT_OK(WaitScheduleSnapshot(30s, schedule_id));
    return schedule_id;
  }

  Result<pgwrapper::PGConn> PgConnect(const std::string& db_name = std::string()) {
    auto* ts = cluster_->tablet_server(RandomUniformInt(0, cluster_->num_tablet_servers() - 1));
    return pgwrapper::PGConn::Connect(HostPort(ts->bind_host(), ts->pgsql_rpc_port()), db_name);
  }

  Result<std::string> PrepareCql() {
    CreateCluster(kClusterName);
    client_ = VERIFY_RESULT(CreateClient());

    auto conn = VERIFY_RESULT(CqlConnect());
    RETURN_NOT_OK(conn.ExecuteQuery(Format("CREATE KEYSPACE IF NOT EXISTS $0", kDbName)));

    auto schedule_id = VERIFY_RESULT(CreateSnapshotSchedule(6s, 10min, "ycql." + kDbName));
    RETURN_NOT_OK(WaitScheduleSnapshot(30s, schedule_id));
    return schedule_id;
  }

  Result<CassandraSession> CqlConnect(const std::string& db_name = std::string()) {
    if (!cql_driver_) {
      std::vector<std::string> hosts;
      for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
        hosts.push_back(cluster_->tablet_server(i)->bind_host());
      }
      cql_driver_ = std::make_unique<CppCassandraDriver>(
          hosts, cluster_->tablet_server(0)->cql_rpc_port(), true);
    }
    auto result = VERIFY_RESULT(cql_driver_->CreateSession());
    if (!db_name.empty()) {
      RETURN_NOT_OK(result.ExecuteQuery(Format("USE $0", kDbName)));
    }
    return result;
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

  std::unique_ptr<CppCassandraDriver> cql_driver_;
};

class YbAdminSnapshotScheduleTestWithYsql : public YbAdminSnapshotScheduleTest {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
    opts->extra_tserver_flags.emplace_back("--ysql_num_shards_per_tserver=1");
  }
};

TEST_F(YbAdminSnapshotScheduleTest, Basic) {
  BuildAndStart();

  std::string schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      6s, 10min, kTableName.namespace_name(), kTableName.table_name()));
  std::this_thread::sleep_for(20s);

  Timestamp last_snapshot_time;
  ASSERT_OK(WaitFor([this, schedule_id, &last_snapshot_time]() -> Result<bool> {
    auto schedule = VERIFY_RESULT(GetSnapshotSchedule());
    auto received_schedule_id = VERIFY_RESULT(Get(schedule, "id")).get().GetString();
    SCHECK_EQ(schedule_id, received_schedule_id, IllegalState, "Wrong schedule id");
    const auto& snapshots = VERIFY_RESULT(Get(schedule, "snapshots")).get().GetArray();

    if (snapshots.Size() < 2) {
      return false;
    }
    std::string last_snapshot_time_str;
    for (const auto& snapshot : snapshots) {
      std::string snapshot_time = VERIFY_RESULT(
          Get(snapshot, "snapshot_time_utc")).get().GetString();
      if (!last_snapshot_time_str.empty()) {
        std::string previous_snapshot_time = VERIFY_RESULT(
            Get(snapshot, "previous_snapshot_time_utc")).get().GetString();
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

TEST_F(YbAdminSnapshotScheduleTest, UndeleteTable) {
  LOG(INFO) << "Create cluster";
  CreateCluster("test-cluster");

  LOG(INFO) << "Create client";
  client_ = ASSERT_RESULT(CreateClient());

  LOG(INFO) << "Create namespace";
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      client::kTableName.namespace_name(), client::kTableName.namespace_type()));

  LOG(INFO) << "Create snapshot schedule";
  std::string schedule_id = ASSERT_RESULT(CreateSnapshotSchedule(
      6s, 10min, client::kTableName.namespace_name()));

  LOG(INFO) << "Wait snapshot schedule";
  ASSERT_OK(WaitScheduleSnapshot(30s, schedule_id));

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

TEST_F_EX(YbAdminSnapshotScheduleTest, YB_DISABLE_TEST_IN_TSAN(Pgsql),
          YbAdminSnapshotScheduleTestWithYsql) {
  auto schedule_id = ASSERT_RESULT(PreparePg());

  auto conn = ASSERT_RESULT(PgConnect(kDbName));

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

  auto conn = ASSERT_RESULT(CqlConnect(kDbName));

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

}  // namespace tools
}  // namespace yb
