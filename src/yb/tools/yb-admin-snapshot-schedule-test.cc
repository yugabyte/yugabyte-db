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

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"

#include "yb/common/colocated_util.h"
#include "yb/common/json_util.h"

#include "yb/gutil/logging-inl.h"

#include "yb/gutil/strings/split.h"
#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_balancer_test_util.h"

#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_backup.proxy.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_ddl.proxy.h"
#include "yb/master/master_util.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tools/admin-test-base.h"
#include "yb/tools/test_admin_client.h"

#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/date_time.h"
#include "yb/util/format.h"
#include "yb/util/random_util.h"
#include "yb/util/range.h"
#include "yb/util/scope_exit.h"
#include "yb/util/split.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using std::string;
using std::vector;

DECLARE_uint64(max_clock_skew_usec);
DECLARE_int32(num_tablet_servers);
DECLARE_int32(num_replicas);

namespace yb {
namespace tools {

namespace {

const std::string kClusterName = "yugacluster";
const std::string kTablegroupName = "ysql_tg";

constexpr auto kInterval = 6s;
constexpr auto kRetention = RegularBuildVsDebugVsSanitizers(10min, 18min, 10min);
constexpr auto kHistoryRetentionIntervalSec = 5;
constexpr auto kCleanupSplitTabletsInterval = 1s;
const std::string old_sys_catalog_snapshot_path = "/opt/yb-build/ysql-sys-catalog-snapshots/";
const std::string old_sys_catalog_snapshot_name = "initial_sys_catalog_snapshot_2.0.9.0";

Result<double> MinuteStringToSeconds(const std::string& min_str) {
      std::vector<Slice> args;
      RETURN_NOT_OK(yb::util::SplitArgs(min_str, &args));
      return MonoDelta::FromMinutes(VERIFY_RESULT(CheckedStold(args[0]))).ToSeconds();
}

Result<size_t> GetTabletCount(TestAdminClient* client) {
  auto tablets = VERIFY_RESULT(client->GetTabletLocations(
      client::kTableName.namespace_name(), client::kTableName.table_name()));
  LOG(INFO) << "Number of tablets: " << tablets.size();
  return tablets.size();
}

} // namespace

YB_DEFINE_ENUM(YsqlColocationConfig, (kNotColocated)(kDBColocated)(kTablegroup));

class YbAdminSnapshotScheduleTest : public AdminTestBase {
 public:
  Result<rapidjson::Document> GetSnapshotSchedule(
      const std::string& id = std::string()) {
    auto deadline = CoarseMonoClock::now() + 10s * kTimeMultiplier;
    for (;;) {
      auto result = id.empty() ? CallJsonAdmin("list_snapshot_schedules")
                               : CallJsonAdmin("list_snapshot_schedules", id);
      if (!result.ok()) {
        if (result.status().ToString().find("Not the leader") != std::string::npos &&
            CoarseMonoClock::now() < deadline) {
          continue;
        }
        return result.status();
      }

      auto schedules = VERIFY_RESULT(Get(&*result, "schedules")).get().GetArray();
      if (schedules.Empty()) {
        return STATUS(NotFound, "Snapshot schedule not found");
      }
      SCHECK_EQ(schedules.Size(), 1U, NotFound, "Wrong schedules number");
      rapidjson::Document document;
      document.CopyFrom(schedules[0], document.GetAllocator());
      return document;
    }
  }

  Result<rapidjson::Document> ListSnapshots() {
    auto out = VERIFY_RESULT(CallJsonAdmin("list_snapshots", "JSON"));
    rapidjson::Document result;
    result.CopyFrom(VERIFY_RESULT(Get(&out, "snapshots")).get(), result.GetAllocator());
    return result;
  }

  Result<rapidjson::Document> WaitScheduleSnapshot(
      MonoDelta duration, const std::string& id = std::string(), uint32_t num_snapshots = 1) {
    rapidjson::Document result;
    RETURN_NOT_OK(WaitFor([this, id, num_snapshots, &result]() -> Result<bool> {
      // If there's a master leader failover then we should wait for the next cycle.
      auto schedule = VERIFY_RESULT(GetSnapshotSchedule(id));
      auto snapshots = VERIFY_RESULT(Get(&schedule, "snapshots")).get().GetArray();
      if (snapshots.Size() < num_snapshots) {
        return false;
      }
      result.CopyFrom(snapshots[snapshots.Size() - 1], result.GetAllocator());
      return true;
    }, duration, "Wait schedule snapshot"));

    // Wait for the present time to become at-least the time chosen by the first snapshot.
    auto snapshot_time_string = VERIFY_RESULT(Get(&result, "snapshot_time")).get().GetString();
    HybridTime snapshot_ht = VERIFY_RESULT(HybridTime::ParseHybridTime(snapshot_time_string));

    RETURN_NOT_OK(WaitFor([&snapshot_ht]() -> Result<bool> {
      Timestamp current_time(VERIFY_RESULT(WallClock()->Now()).time_point);
      HybridTime current_ht = HybridTime::FromMicros(current_time.ToInt64());
      return snapshot_ht <= current_ht;
    }, duration, "Wait Snapshot Time Elapses"));
    return result;
  }

  Status WaitNewSnapshot(const std::string& id = {}) {
    LOG(INFO) << "WaitNewSnapshot, schedule id: " << id;
    std::string last_snapshot_id;
    return WaitFor([this, &id, &last_snapshot_id]() -> Result<bool> {
      // If there's a master leader failover then we should wait for the next cycle.
      auto schedule = VERIFY_RESULT(GetSnapshotSchedule(id));
      auto snapshots = VERIFY_RESULT(Get(&schedule, "snapshots")).get().GetArray();
      if (snapshots.Empty()) {
        return false;
      }
      auto snapshot_id = VERIFY_RESULT(
          Get(&snapshots[snapshots.Size() - 1], "id")).get().GetString();
      LOG(INFO) << "WaitNewSnapshot, last snapshot id: " << snapshot_id;
      if (last_snapshot_id.empty()) {
        last_snapshot_id = snapshot_id;
        return false;
      }
      return last_snapshot_id != snapshot_id;
    }, kInterval * 5, "Wait new schedule snapshot");
  }

  Result<std::string> StartRestoreSnapshotSchedule(
      const std::string& schedule_id, Timestamp restore_at) {
    auto out = VERIFY_RESULT(CallJsonAdmin(
        "restore_snapshot_schedule", schedule_id, restore_at.ToFormattedString(),
        "--timeout_ms", std::to_string(600000 * kTimeMultiplier)));
    std::string restoration_id = VERIFY_RESULT(Get(out, "restoration_id")).get().GetString();
    LOG(INFO) << "Restoration id: " << restoration_id;
    return restoration_id;
  }

  Status RestoreSnapshotSchedule(const std::string& schedule_id, Timestamp restore_at) {
    return WaitRestorationDone(
        VERIFY_RESULT(StartRestoreSnapshotSchedule(schedule_id, restore_at)),
        40s * kTimeMultiplier);
  }

  Status WaitRestorationDone(const std::string& restoration_id, MonoDelta timeout) {
    return WaitFor([this, restoration_id]() -> Result<bool> {
      auto out_result = CallJsonAdmin("list_snapshot_restorations", restoration_id);
      // If there's a master leader failover then we should wait for the next cycle.
      if (!out_result.ok()) {
        if (out_result.status().ToString().find("Not the leader") !=
            std::string::npos) {
          return false;
        }
        return out_result.status();
      }
      LOG(INFO) << "Restorations: " << common::PrettyWriteRapidJsonToString(*out_result);
      const auto& restorations = VERIFY_RESULT(Get(*out_result, "restorations")).get().GetArray();
      SCHECK_EQ(restorations.Size(), 1U, IllegalState, "Wrong restorations number");
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

  Result<std::vector<std::string>> GetAllRestorationIds() {
    std::vector<std::string> res;
    auto out = VERIFY_RESULT(CallJsonAdmin("list_snapshot_restorations"));
    LOG(INFO) << "Restorations: " << common::PrettyWriteRapidJsonToString(out);
    const auto& restorations = VERIFY_RESULT(Get(out, "restorations")).get().GetArray();
    auto id = VERIFY_RESULT(Get(restorations[0], "id")).get().GetString();
    res.push_back(id);

    return res;
  }

  Status PrepareCommon() {
    LOG(INFO) << "Create cluster";
    CreateCluster(kClusterName, ExtraTSFlags(), ExtraMasterFlags());

    LOG(INFO) << "Create client";
    client_ = VERIFY_RESULT(CreateClient());
    test_admin_client_ = std::make_unique<TestAdminClient>(cluster_.get(), client_.get());

    return Status::OK();
  }

  virtual std::vector<std::string> ExtraTSFlags() {
    return { Format("--timestamp_history_retention_interval_sec=$0", kHistoryRetentionIntervalSec),
             "--history_cutoff_propagation_interval_ms=1000",
             "--enable_automatic_tablet_splitting=true",
             Format("--cleanup_split_tablets_interval_sec=$0",
                      MonoDelta(kCleanupSplitTabletsInterval).ToSeconds()) };
  }

  virtual std::vector<std::string> ExtraMasterFlags() {
    // To speed up tests.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500",
             "--enable_automatic_tablet_splitting=true",
             "--enable_transactional_ddl_gc=false"};
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

  Result<std::string> PreparePg(
      YsqlColocationConfig colocation = YsqlColocationConfig::kNotColocated,
      MonoDelta interval = kInterval, MonoDelta retention = kRetention) {
    if (!cluster_) {
      RETURN_NOT_OK(PrepareCommon());
    }

    auto conn = VERIFY_RESULT(PgConnect());
    switch (colocation) {
      case YsqlColocationConfig::kNotColocated:
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE DATABASE $0", client::kTableName.namespace_name()));
        break;
      case YsqlColocationConfig::kDBColocated:
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE DATABASE $0 WITH COLOCATION=TRUE", client::kTableName.namespace_name()));
        break;
      case YsqlColocationConfig::kTablegroup:
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLEGROUP $0", kTablegroupName));

        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE DATABASE $0", client::kTableName.namespace_name()));
        auto test_ns_conn = VERIFY_RESULT(PgConnect(client::kTableName.namespace_name()));
        RETURN_NOT_OK(test_ns_conn.ExecuteFormat(
            "CREATE TABLEGROUP $0", kTablegroupName));
    }

    return CreateSnapshotScheduleAndWaitSnapshot(
        "ysql." + client::kTableName.namespace_name(), interval, retention);
  }

  Result<std::string> CreateYsqlSnapshotSchedule(
      const std::string& table_name, MonoDelta interval, MonoDelta retention) {
    return CreateSnapshotSchedule(interval, retention, "ysql." + table_name);
  }

  Result<std::string> CreateYcqlSnapshotSchedule(
      const std::string& table_name, MonoDelta interval, MonoDelta retention) {
    return CreateSnapshotSchedule(interval, retention, "ycql." + table_name);
  }

  Result<pgwrapper::PGConn> PgConnect(const std::string& db_name = std::string()) {
    auto* ts = cluster_->tablet_server(
        RandomUniformInt<size_t>(0, cluster_->num_tablet_servers() - 1));
    return pgwrapper::PGConnBuilder({
      .host = ts->bind_host(),
      .port = ts->pgsql_rpc_port(),
      .dbname = db_name
    }).Connect();
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

  Status DeleteSnapshotSchedule(const std::string& schedule_id) {
    auto out = VERIFY_RESULT(CallJsonAdmin("delete_snapshot_schedule", schedule_id));

    SCHECK_EQ(VERIFY_RESULT(Get(out, "schedule_id")).get().GetString(), schedule_id, IllegalState,
              "Deleted wrong schedule");
    return Status::OK();
  }

  Result<master::SnapshotScheduleOptionsPB> EditSnapshotSchedule(
      const std::string& schedule_id, std::optional<MonoDelta> interval,
      std::optional<MonoDelta> retention) {
    Result<rapidjson::Document> result = STATUS(InvalidArgument, "");
    if (interval && !retention) {
      result =
          CallJsonAdmin("edit_snapshot_schedule", schedule_id, "interval", interval->ToMinutes());
    } else if (!interval && retention) {
      result =
          CallJsonAdmin("edit_snapshot_schedule", schedule_id, "retention", retention->ToMinutes());
    } else if (interval && retention) {
      result = CallJsonAdmin(
          "edit_snapshot_schedule", schedule_id, "interval", interval->ToMinutes(), "retention",
          retention->ToMinutes());
    } else {
      return STATUS(InvalidArgument, "At least one of interval or retention must be set");
    }
    RETURN_NOT_OK(result);
    master::SnapshotScheduleOptionsPB options;
    const rapidjson::Value& schedule = VERIFY_RESULT(Get(*result, "schedule"));
    const rapidjson::Value& json_options = VERIFY_RESULT(Get(schedule, "options"));
    const rapidjson::Value& json_interval = VERIFY_RESULT(Get(json_options, "interval"));
    options.set_interval_sec(VERIFY_RESULT(MinuteStringToSeconds(json_interval.GetString())));
    const rapidjson::Value& json_retention = VERIFY_RESULT(Get(json_options, "retention"));
    options.set_retention_duration_sec(
        VERIFY_RESULT(MinuteStringToSeconds(json_retention.GetString())));
    return options;
  }

  Status WaitForSnapshotsInScheduleCount(
      const std::string& schedule_id, MonoDelta wait_time, uint32_t lower, uint32_t upper,
      const std::string& description) {
    return WaitFor(
        [this, &schedule_id, lower, upper]() -> Result<bool> {
          auto schedule = VERIFY_RESULT(GetSnapshotSchedule(schedule_id));
          const rapidjson::Value& snapshots = VERIFY_RESULT(Get(schedule, "snapshots"));
          snapshots.IsArray();
          return lower <= snapshots.GetArray().Size() && snapshots.GetArray().Size() <= upper;
        },
        wait_time * kTimeMultiplier, description);
  }
  // Waits for snapshot count to increase from current to current + delta.
  Status WaitForMoreSnapshots(
      const std::string& schedule_id, MonoDelta wait_time, uint32_t delta,
      const std::string& description) {
    // Get current count.
    auto schedule = VERIFY_RESULT(GetSnapshotSchedule(schedule_id));
    const rapidjson::Value& snapshots = VERIFY_RESULT(Get(schedule, "snapshots"));
    auto current_count = snapshots.GetArray().Size();
    return WaitFor(
        [this, &schedule_id, delta, current_count]() -> Result<bool> {
          auto schedule = VERIFY_RESULT(GetSnapshotSchedule(schedule_id));
          const rapidjson::Value& snapshots = VERIFY_RESULT(Get(schedule, "snapshots"));
          snapshots.IsArray();
          return current_count + delta <= snapshots.GetArray().Size();
        },
        wait_time * kTimeMultiplier, description);
  }

  // Note: Only populates interval and retention_duration.
  Result<master::SnapshotScheduleOptionsPB> GetSnapshotScheduleOptions(
      const std::string& schedule_id) {
    auto schedule = VERIFY_RESULT(GetSnapshotSchedule(schedule_id));
    const rapidjson::Value& json_options = VERIFY_RESULT(Get(schedule, "options"));

    master::SnapshotScheduleOptionsPB options;
    const rapidjson::Value& interval = VERIFY_RESULT(Get(json_options, "interval"));
    options.set_interval_sec(VERIFY_RESULT(MinuteStringToSeconds(interval.GetString())));
    const rapidjson::Value& retention = VERIFY_RESULT(Get(json_options, "retention"));
    options.set_retention_duration_sec(VERIFY_RESULT(MinuteStringToSeconds(retention.GetString())));
    return options;
  }

  Status WaitTabletsCleaned(CoarseTimePoint deadline) {
    return Wait([this, deadline]() -> Result<bool> {
      size_t alive_tablets = 0;
      for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
        auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
        tserver::ListTabletsRequestPB req;
        tserver::ListTabletsResponsePB resp;
        rpc::RpcController controller;
        controller.set_deadline(deadline);
        RETURN_NOT_OK(proxy.ListTablets(req, &resp, &controller));
        for (const auto& tablet : resp.status_and_schema()) {
          if (tablet.tablet_status().table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE) {
            LOG(INFO) << "Not yet deleted tablet: " << tablet.ShortDebugString();
            ++alive_tablets;
          }
        }
      }
      LOG(INFO) << "Alive tablets: " << alive_tablets;
      return alive_tablets == 0;
    }, deadline, "Deleted tablet cleanup");
  }

  void TestUndeleteTable(bool restart_masters);
  void TestGCHiddenTables();

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->bind_to_unique_loopback_addresses = true;
    options->use_same_ts_ports = true;
    options->num_masters = 3;
  }

  Result<Timestamp> GetCurrentTime() {
    // IMPORTANT NOTE: THE SLEEP IS TEMPORARY AND
    // SHOULD BE REMOVED ONCE GH#12796 IS FIXED.
    SleepFor(MonoDelta::FromSeconds(4 * kTimeMultiplier));
    auto time = Timestamp(VERIFY_RESULT(WallClock()->Now()).time_point);
    LOG(INFO) << "Time to restore: " << time.ToHumanReadableTime();
    return time;
  }

  std::unique_ptr<CppCassandraDriver> cql_driver_;
  std::unique_ptr<TestAdminClient> test_admin_client_;
};

class YbAdminSnapshotScheduleTestWithYsql : public YbAdminSnapshotScheduleTest {
 public:
  void SetUp() override {
    YB_SKIP_TEST_IN_TSAN();
    YbAdminSnapshotScheduleTest::SetUp();
  }
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
    opts->extra_tserver_flags.emplace_back("--ysql_num_shards_per_tserver=1");
    opts->extra_master_flags.emplace_back("--log_ysql_catalog_versions=true");
    opts->extra_master_flags.emplace_back("--consensus_rpc_timeout_ms=5000");
    opts->num_masters = 3;
  }

  Status WaitForSelectQueryToMatchExpectation(
      const std::string& query, const std::string& expectation, pgwrapper::PGConn* conn) {
    return LoggedWaitFor([&]() -> Result<bool> {
      auto res = conn->FetchValue<std::string>(query);
      if (!res.ok()) {
        return false;
      }
      LOG(INFO) << "Got value: " << *res << ", expected: " << expectation;
      return *res == expectation;
    }, 5s * kTimeMultiplier, "Wait for query to match expectation");
  }

  Status WaitForInsertQueryToSucceed(const std::string& query, pgwrapper::PGConn* conn) {
    return LoggedWaitFor([&]() -> Result<bool> {
      auto res = conn->Execute(query);
      if (res.ok()) {
        return true;
      }
      return false;
    }, 5s * kTimeMultiplier, "Wait for query to match expectation");
  }

  Status WaitForInsertQueryToStopWorking(
      const std::string& insert_query_template, pgwrapper::PGConn* conn, int initial_value) {
    int val = initial_value;
    return LoggedWaitFor([&]() -> Result<bool> {
      std::string insert_query = Format(insert_query_template, val);
      LOG(INFO) << "Executing query: " << insert_query;
      auto res = conn->Execute(insert_query);
      ++val;
      if (!res.ok()) {
        LOG(INFO) << res.ToUserMessage();
        return true;
      }
      return false;
    }, 5s * kTimeMultiplier, "Wait for query to match expectation");
  }

  Status WaitForInsertQueryToMatchExpectation(
      const std::string& insert_query_template, const std::string& select_query_template,
      int initial_value, const std::string& expectation, pgwrapper::PGConn* conn) {
    int val = initial_value;
    return LoggedWaitFor([&]() -> Result<bool> {
      // First write.
      std::string insert_query = Format(insert_query_template, val);
      LOG(INFO) << "Executing query: " << insert_query;
      RETURN_NOT_OK(conn->Execute(insert_query));
      // Now read and check if it matches expectation.
      std::string select_query = Format(select_query_template, val);
      LOG(INFO) << "Executing query: " << select_query;
      auto res = VERIFY_RESULT(conn->FetchValue<std::string>(select_query));
      ++val;
      LOG(INFO) << "Got result: " << res << ", expected: " << expectation;
      return res == expectation;
    }, 5s * kTimeMultiplier, "Wait for query to match expectation");
  }

  void TestPgsqlDropDefault();
};

class YbAdminSnapshotScheduleTestWithYsqlAndPackedRow : public YbAdminSnapshotScheduleTestWithYsql {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    YbAdminSnapshotScheduleTestWithYsql::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back("--ysql_enable_packed_row=true");
    opts->extra_tserver_flags.emplace_back("--timestamp_history_retention_interval_sec=0");
    opts->extra_master_flags.emplace_back("--ysql_enable_packed_row=true");
    opts->extra_master_flags.emplace_back("--timestamp_history_retention_interval_sec=0");
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

TEST_F(YbAdminSnapshotScheduleTest, TestTruncateDisallowedWithPitr) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));
  ASSERT_OK(
      conn.ExecuteQuery("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
                        "WITH transactions = { 'enabled' : true }"));
  auto s = conn.ExecuteQuery("TRUNCATE TABLE test_table");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), "Cannot truncate table test_table which has schedule");

  LOG(INFO) << "Enable flag to allow truncate and validate that truncate succeeds";
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_truncate_on_pitr_table", "true"));
  ASSERT_OK(conn.ExecuteQuery("TRUNCATE TABLE test_table"));
}

TEST_F(YbAdminSnapshotScheduleTest, Delete) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(kRetention, kRetention + 1s));

  auto session = client_->NewSession(60s);
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

  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        auto snapshots_json = VERIFY_RESULT(ListSnapshots());
        auto snapshots = snapshots_json.GetArray();
        LOG(INFO) << "Snapshots left: " << snapshots.Size();
        return snapshots.Empty();
      },
      10s * kTimeMultiplier, "Snapshots cleaned up"));

  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    for (auto* tserver : cluster_->tserver_daemons()) {
      auto proxy = cluster_->GetProxy<tserver::TabletServerAdminServiceProxy>(tserver);
      tserver::FlushTabletsRequestPB req;
      req.set_dest_uuid(tserver->uuid());
      req.set_all_tablets(true);
      tserver::FlushTabletsResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(30s);
      RETURN_NOT_OK(proxy.FlushTablets(req, &resp, &controller));

      req.set_operation(tserver::FlushTabletsRequestPB::COMPACT);
      controller.Reset();
      RETURN_NOT_OK(proxy.FlushTablets(req, &resp, &controller));
    }

    for (auto* tserver : cluster_->tserver_daemons()) {
      auto proxy = cluster_->GetProxy<tserver::TabletServerServiceProxy>(tserver);
      tserver::ListTabletsRequestPB req;
      tserver::ListTabletsResponsePB resp;

      rpc::RpcController controller;
      controller.set_timeout(30s);

      RETURN_NOT_OK(proxy.ListTablets(req, &resp, &controller));

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

// Modifies the interval of a snapshot schedule and uses the number of snapshots in the schedule as
// proxy to verify the update was successfully applied.
TEST_F(YbAdminSnapshotScheduleTest, EditInterval) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(10s, 60s));
  // For a schedule with interval 10s and retention 60s, we expect 6 snapshots to be live
  // in the schedule in the steady state.
  ASSERT_OK(WaitForSnapshotsInScheduleCount(
      schedule_id, 5min, 5, 7, "Waiting for initial steady state snapshots"));
  LOG(INFO) << "Edit snapshot schedule.";
  // We don't check the return value because values are rounded down to nearest minute.
  ASSERT_RESULT(EditSnapshotSchedule(schedule_id, 20s, {}));
  // For a schedule with interval 20s and retention 60s, we expect 3 snapshots to be live in the
  // schedule in the steady state.
  ASSERT_OK(WaitForSnapshotsInScheduleCount(
      schedule_id, 5min, 2, 4, "Waiting for edited steady state snapshots"));
}

// Modifies the retention of a snapshot schedule and uses the number of snapshots in the schedule as
// a proxy to verify the update was successfully applied.
TEST_F(YbAdminSnapshotScheduleTest, EditRetention) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(10s, 60s));
  // For a schedule with interval 10s and retention 60s, we expect 6 snapshots to be live
  // in the schedule in the steady state.
  ASSERT_OK(WaitForSnapshotsInScheduleCount(
      schedule_id, 5min, 5, 7, "Waiting for initial steady state snapshots"));
  LOG(INFO) << "Edit snapshot schedule.";
  // We don't check the return value because values are rounded down to nearest minute.
  ASSERT_RESULT(EditSnapshotSchedule(schedule_id, {}, 30s));
  // For a schedule with interval 10s and retention 30s, we expect 3 snapshots to be live in the
  // schedule in the steady state.
  ASSERT_OK(WaitForSnapshotsInScheduleCount(
      schedule_id, 5min, 2, 4, "Waiting for edited steady state snapshots"));
}

// Modifies the interval and retention of a snapshot schedule and uses the number of snapshots in
// the schedule as a proxy to verify the update was successfully applied.
TEST_F(YbAdminSnapshotScheduleTest, EditIntervalAndRetention) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(10s, 60s));
  // For a schedule with interval 10s and retention 90s, we expect 6 snapshots to be live
  // in the schedule in the steady state.
  ASSERT_OK(WaitForSnapshotsInScheduleCount(
      schedule_id, 5min, 5, 7, "Waiting for initial steady state snapshots"));
  LOG(INFO) << "Edit snapshot schedule.";
  // We don't check the return value because values are rounded down to nearest minute.
  ASSERT_RESULT(EditSnapshotSchedule(schedule_id, 15s, 30s));
  // For a schedule with interval 15s and retention 30s, we expect 2 snapshots to be live in the
  // schedule in the steady state.
  ASSERT_OK(WaitForSnapshotsInScheduleCount(
      schedule_id, 5min, 1, 3, "Waiting for edited steady state snapshots"));
}

TEST_F(YbAdminSnapshotScheduleTest, EditSnapshotScheduleCheckOptions) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(1min, 10min));
  LOG(INFO) << "Edit snapshot schedule.";
  auto edit_options = ASSERT_RESULT(EditSnapshotSchedule(schedule_id, 2min, 8min));
  ASSERT_EQ(edit_options.interval_sec(), 60 * 2);
  ASSERT_EQ(edit_options.retention_duration_sec(), 60 * 8);
  LOG(INFO) << "Sanity check returned value against list_snapshot_schedule.";
  auto list_options = ASSERT_RESULT(GetSnapshotScheduleOptions(schedule_id));
  ASSERT_EQ(list_options.interval_sec(), 60 * 2);
  ASSERT_EQ(list_options.retention_duration_sec(), 60 * 8);
}

TEST_F(YbAdminSnapshotScheduleTest, EditIntervalZero) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(1min, 10min));
  auto result = EditSnapshotSchedule(schedule_id, 0min, {});
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Zero interval");
}

TEST_F(YbAdminSnapshotScheduleTest, EditRetentionZero) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(1min, 10min));
  auto result = EditSnapshotSchedule(schedule_id, {}, 0min);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Zero retention");
}

TEST_F(YbAdminSnapshotScheduleTest, EditRepeatedInterval) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(1min, 10min));
  auto result =
      CallJsonAdmin("edit_snapshot_schedule", schedule_id, "interval", "1", "interval", "2");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Repeated interval");
}

TEST_F(YbAdminSnapshotScheduleTest, EditRepeatedRetention) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(1min, 10min));
  auto result =
      CallJsonAdmin("edit_snapshot_schedule", schedule_id, "retention", "1", "retention", "2");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Repeated retention");
}

TEST_F(YbAdminSnapshotScheduleTest, EditIntervalLargerThanRetention) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(2min, 5min));
  auto result = EditSnapshotSchedule(schedule_id, {}, 1min);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Interval must be strictly less than retention");
}

TEST_F(YbAdminSnapshotScheduleTest, CreateIntervalZero) {
  ASSERT_OK(PrepareCommon());
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      client::kTableName.namespace_name(), client::kTableName.namespace_type()));
  auto result = CreateYcqlSnapshotSchedule(client::kTableName.namespace_name(), 0min, kRetention);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Zero interval");
}

TEST_F(YbAdminSnapshotScheduleTest, CreateRetentionZero) {
  ASSERT_OK(PrepareCommon());
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      client::kTableName.namespace_name(), client::kTableName.namespace_type()));
  auto result = CreateYcqlSnapshotSchedule(client::kTableName.namespace_name(), kInterval, 0min);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Zero retention");
}

TEST_F(YbAdminSnapshotScheduleTest, CreateIntervalLargerThanRetention) {
  ASSERT_OK(PrepareCommon());
  ASSERT_OK(client_->CreateNamespaceIfNotExists(
      client::kTableName.namespace_name(), client::kTableName.namespace_type()));
  auto result = CreateYcqlSnapshotSchedule(client::kTableName.namespace_name(), 2min, 1min);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "Interval must be strictly less than retention");
}

void YbAdminSnapshotScheduleTest::TestUndeleteTable(bool restart_masters) {
  auto schedule_id = ASSERT_RESULT(PrepareQl());

  auto session = client_->NewSession(60s);
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
  auto schedule_id = ASSERT_RESULT(PrepareQl(kInterval, kInterval + 1s));

  auto session = client_->NewSession(60s);
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
  ASSERT_OK(WaitTabletsCleaned(deadline));

  // Wait table marked as deleted.
  ASSERT_OK(Wait([this, deadline]() -> Result<bool> {
    auto proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;
    rpc::RpcController controller;
    controller.set_deadline(deadline);
    req.set_include_not_running(true);
    RETURN_NOT_OK(proxy.ListTables(req, &resp, &controller));
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

TEST_F(YbAdminSnapshotScheduleTest, ListRestorationsAfterFailover) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(2s * kTimeMultiplier, 10s * kTimeMultiplier));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Create a table.
  LOG(INFO) << "Create table";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  SleepFor(MonoDelta::FromSeconds(6 * kTimeMultiplier));

  // Restore to the time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Perform a master leader stepdown.
  LOG(INFO) << "Stepping down the master leader";
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());

  // list_snapshot_restorations should not fatal now.
  auto restorations = ASSERT_RESULT(GetAllRestorationIds());
  ASSERT_EQ(restorations.size(), 1);
  LOG(INFO) << "Restoration: " << restorations[0];
}

TEST_F(YbAdminSnapshotScheduleTest, ListRestorationsTestMigration) {
  auto schedule_id = ASSERT_RESULT(PrepareQl(2s * kTimeMultiplier, 10s * kTimeMultiplier));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Create a table.
  LOG(INFO) << "Create table";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 3, client_.get(), &table_));

  SleepFor(MonoDelta::FromSeconds(6 * kTimeMultiplier));

  // Simulate old behavior.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_update_aggregated_restore_state", "true"));

  // Restore to the time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Reload this data.
  cluster_->Shutdown();
  ASSERT_OK(cluster_->Restart());
  LOG(INFO) << "Restarted cluster";

  // list_snapshot_restorations should not fatal now.
  auto restorations = ASSERT_RESULT(GetAllRestorationIds());
  ASSERT_EQ(restorations.size(), 1);
  LOG(INFO) << "Restoration: " << restorations[0];
}

// This class simplifies the way to run PITR tests against (not) colocated database.
// After setting proper callback functions, calling RunTestWithColocatedParam performs the test.
// You can also write tests inherit from this class without using this framework.
class YbAdminSnapshotScheduleTestWithYsqlParam
    : public YbAdminSnapshotScheduleTestWithYsql,
      public ::testing::WithParamInterface<YsqlColocationConfig> {
 public:
  typedef std::function<void(const std::string, const std::string)> StepCallback;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    YbAdminSnapshotScheduleTestWithYsql::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back("--ysql_beta_feature_tablegroup=true");
  }

  Result<std::string> PreparePgWithColocatedParam() { return PreparePg(GetParam()); }

  void ExecuteOnTables(std::string non_colo_prefix,
                       std::string non_colo_option,
                       std::vector<std::string> colo_prefixes,
                       std::string colo_option,
                       StepCallback step) {
    if (step == nullptr) {
      return;
    }

    if (GetParam() != YsqlColocationConfig::kNotColocated) {
      for (const auto& colo_prefix : colo_prefixes) {
        step(colo_prefix, colo_option);
      }
    }
    step(non_colo_prefix, non_colo_option);
  }

  // To test PITR against a colocated database, two by default colocated tables and
  // one not colocated table are created where same operations are performed on them.
  void RunTestWithColocatedParam(std::string schedule_id) {
    std::vector<std::string> colocated_prefixes = {"colocated", "colocated2"};
    std::string colocated_option =
        GetParam() == YsqlColocationConfig::kTablegroup ? "TABLEGROUP " + kTablegroupName : "";
    std::string not_colocated_prefix = "not_colocated";
    std::string not_colocated_option = "WITH (COLOCATION = FALSE)";

    ExecuteOnTables(not_colocated_prefix, not_colocated_option, colocated_prefixes,
        colocated_option, ExecBeforeRestoreTS);

    Timestamp time = ASSERT_RESULT(GetCurrentTime());
    LOG(INFO) << "Time noted to restore the database back: " << time;

    ExecuteOnTables(not_colocated_prefix, not_colocated_option, colocated_prefixes,
        colocated_option, ExecAfterRestoreTS);

    LOG(INFO) << "Perform a Restore to the time noted above";
    ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

    ASSERT_NE(CheckAfterPITR, nullptr);
    ExecuteOnTables(not_colocated_prefix, not_colocated_option, colocated_prefixes,
        colocated_option, CheckAfterPITR);
  }

  StepCallback ExecBeforeRestoreTS;
  StepCallback ExecAfterRestoreTS;
  StepCallback CheckAfterPITR;
};

INSTANTIATE_TEST_CASE_P(Colocation, YbAdminSnapshotScheduleTestWithYsqlParam,
                        ::testing::Values(YsqlColocationConfig::kNotColocated,
                                          YsqlColocationConfig::kDBColocated,
                                          YsqlColocationConfig::kTablegroup));

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, Pgsql) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  ASSERT_OK(conn.Execute("UPDATE test_table SET value = 'after'"));

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  auto res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table"));
  ASSERT_EQ(res, "before");
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropDatabaseAndSchedule) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect());

  auto res = conn.Execute(Format("DROP DATABASE $0", client::kTableName.namespace_name()));
  ASSERT_NOK(res);
  ASSERT_STR_CONTAINS(res.message().ToBuffer(), "Cannot delete database which has schedule");

  // Once the schedule is deleted, we should be able to drop the database.
  ASSERT_OK(DeleteSnapshotSchedule(schedule_id));
  ASSERT_OK(conn.Execute(Format("DROP DATABASE $0", client::kTableName.namespace_name())));
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlCreateTable) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", table_name));
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    // Wait for Restore to complete. Applicable only for non-colocated tablets.
    ASSERT_OK(WaitFor([this]() -> Result<bool> {
      bool all_tablets_hidden = true;
      for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
        auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
        tserver::ListTabletsRequestPB req;
        tserver::ListTabletsResponsePB resp;
        rpc::RpcController controller;
        controller.set_timeout(30s);
        RETURN_NOT_OK(proxy.ListTablets(req, &resp, &controller));
        for (const auto& tablet : resp.status_and_schema()) {
          if (tablet.tablet_status().namespace_name() == client::kTableName.namespace_name()
              && tablet.tablet_status().table_name().find("colocated.parent") == string::npos
              && tablet.tablet_status().table_name().find("colocation.parent") == string::npos
              && tablet.tablet_status().table_name().find("tablegroup.parent") == string::npos) {
            LOG(INFO) << "Tablet " << tablet.tablet_status().tablet_id() << " of table "
                      << tablet.tablet_status().table_name() << ", hidden status "
                      << tablet.tablet_status().is_hidden();
            all_tablets_hidden = all_tablets_hidden && tablet.tablet_status().is_hidden();
          }
        }
      }
      return all_tablets_hidden;
    }, 30s, "Restore failed."));

    ASSERT_NOK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'now')", table_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'after')", table_name));
    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 WHERE key = 1", table_name)));
    ASSERT_EQ(res, "after");
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropTable) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 WHERE key = 1", table_name)));
    ASSERT_EQ(res, "before");
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET value = 'after'", table_name));
    res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 WHERE key = 1", table_name)));
    ASSERT_EQ(res, "after");
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, FailAfterMigration) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Save time to restore to: " << time;

  LOG(INFO) << "Insert new row into pb_yg_migration table";
  ASSERT_OK(conn.Execute(
      "BEGIN;\n"
      "SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;\n"
      "INSERT INTO pg_yb_migration (major, minor, name) VALUES (2147483640, 0, 'version n');\n"
      "COMMIT;"));
  LOG(INFO) << "Assert restore for time " << time
            << " fails because of new row in pg_yb_migration";

  auto restore_status = RestoreSnapshotSchedule(schedule_id, time);
  ASSERT_NOK(restore_status);

  ASSERT_STR_CONTAINS(
      restore_status.message().ToBuffer(), "Unable to restore as YSQL upgrade was performed");
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlCreateIndex) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_idx_name = prefix + "_table_idx";

    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (value)", table_idx_name, table_name));

    // Scans should use the index now.
    bool is_index_scan = ASSERT_RESULT(
        conn.HasIndexScan(Format("SELECT value FROM $0 where value='before'", table_name)));
    LOG(INFO) << "Scans uses index scan " << is_index_scan;
    ASSERT_TRUE(is_index_scan);
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_idx_name = prefix + "_table_idx";

    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0", table_name)));
    ASSERT_EQ(res, "before");

    // Scans should not use index. Waiting for upto 5
    // HBs for this to take effect.
    ASSERT_OK(WaitFor([&conn, &table_name]() -> Result<bool> {
      bool is_index_scan = VERIFY_RESULT(
          conn.HasIndexScan(Format("SELECT value FROM $0 where value='before'", table_name)));
      LOG(INFO) << "Post restore scans uses index scan " << is_index_scan;
      return !is_index_scan;
    }, 5s * kTimeMultiplier, "Wait for scans to not use the index"));

    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (value)", table_idx_name, table_name));
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET value = 'after'", table_name));

    // Scans should use index.
    bool is_index_scan = ASSERT_RESULT(
        conn.HasIndexScan(Format("SELECT value FROM $0 where value='after'", table_name)));
    LOG(INFO) << "Scans uses index scan " << is_index_scan;
    ASSERT_TRUE(is_index_scan);

    res = ASSERT_RESULT(conn.FetchValue<std::string>(Format("SELECT value FROM $0", table_name)));
    ASSERT_EQ(res, "after");
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropIndex) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_idx_name = prefix + "_table_idx";

    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (value)", table_idx_name, table_name));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_idx_name = prefix + "_table_idx";

    ASSERT_OK(conn.ExecuteFormat("DROP INDEX $0", table_idx_name));

    // Reads should not use the index scan.
    bool is_index_scan = ASSERT_RESULT(
        conn.HasIndexScan(Format("SELECT value FROM $0 where value='before'", table_name)));
    LOG(INFO) << "Post drop scans uses index scan " << is_index_scan;
    ASSERT_FALSE(is_index_scan);
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_idx_name = prefix + "_table_idx";

    // Reads should use the index scan now. Waiting for upto 5
    // HBs for this to take effect.
    ASSERT_OK(WaitFor([&conn, &table_name]() -> Result<bool> {
      bool is_index_scan = VERIFY_RESULT(
          conn.HasIndexScan(Format("SELECT value FROM $0 where value='before'", table_name)));
      LOG(INFO) << "Post restore scans uses index scan " << is_index_scan;
      return is_index_scan;
    }, 5s * kTimeMultiplier, "Wait for scans to use the index"));

    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0", table_name)));
    ASSERT_EQ(res, "before");
    ASSERT_NOK(conn.ExecuteFormat("CREATE INDEX $0 ON $1 (value)", table_idx_name, table_name));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'after')", table_name));
    res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 WHERE key = 2", table_name)));
    ASSERT_EQ(res, "after");

    bool is_index_scan = ASSERT_RESULT(
        conn.HasIndexScan(Format("SELECT value FROM $0 where value='after'", table_name)));
    ASSERT_TRUE(is_index_scan);
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlAddColumn) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Create a table and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Alter the table -> Add 'value2' column";
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD COLUMN value2 TEXT", table_name));
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET value = 'now'", table_name));
    ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET value2 = 'now2'", table_name));
    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value2 FROM $0", table_name)));
    ASSERT_EQ(res, "now2");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Select data from the table after restore";
    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0", table_name)));
    ASSERT_EQ(res, "before");

    LOG(INFO) << "Insert data to the table after restore";
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'one more')", table_name));

    // There might be a transient period when we get stale data before
    // the new catalog version gets propagated to all tservers via heartbeats.
    std::string query_template = Format(
        "INSERT INTO $0 VALUES ($$0, 'again one more', 'new_value')", table_name);
    ASSERT_OK(WaitForInsertQueryToStopWorking(query_template, &conn, 3));

    auto result_status = conn.FetchValue<std::string>(Format(
        "SELECT value2 FROM $0", table_name));
    ASSERT_FALSE(result_status.ok());
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDeleteColumn) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Create a table and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Alter the table -> Drop 'value' column";
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN value", table_name));
    auto query_and_result = conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0", table_name));
    ASSERT_FALSE(query_and_result.ok());
    ASSERT_STR_CONTAINS(query_and_result.status().ToString(), "does not exist");

    LOG(INFO) << "Reading Rows";
    auto select_res = ASSERT_RESULT(conn.FetchValue<int32_t>(
        Format("SELECT * FROM $0", table_name)));
    LOG(INFO) << "Read result: " << select_res;
    ASSERT_EQ(select_res, 1);

    auto insert_status = conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'new_value')", table_name);
    ASSERT_FALSE(insert_status.ok());
    ASSERT_STR_CONTAINS(insert_status.ToString(), "more expressions than target columns");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Select data from the table after restore";
    std::string query = Format("SELECT value FROM $0", table_name);
    // There might be a transient period when we get stale data before
    // the new catalog version gets propagated to all tservers via heartbeats.
    ASSERT_OK(WaitForSelectQueryToMatchExpectation(query, "before", &conn));

    // We should now be able to insert with restored column.
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'next value')", table_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDeleteColumnWithMissingDefault) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    const auto table_name = prefix + "_table";

    LOG(INFO) << "Create a table and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY) $1",
        table_name, option));
    // Insert a row.
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", table_name));
    // Add a column with a missing default value.
    ASSERT_OK(conn.ExecuteFormat(
        "ALTER TABLE $0 ADD COLUMN value TEXT DEFAULT 'default'", table_name));
    // Insert some rows with explicitly set values and nulls for the new column.
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (2, null), (3, 'not_default')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    const auto table_name = prefix + "_table";

    LOG(INFO) << "Alter the table -> Drop 'value' column";
    // Drop the column.
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 DROP COLUMN value", table_name));
    // Verify that we cannot read the column.
    auto query_and_result = conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0", table_name));
    ASSERT_FALSE(query_and_result.ok());
    ASSERT_STR_CONTAINS(query_and_result.status().ToString(), "does not exist");
    // Verify that we cannot insert into the column.
    auto insert_status = conn.ExecuteFormat("INSERT INTO $0 VALUES (4, 'new_value')", table_name);
    ASSERT_FALSE(insert_status.ok());
    ASSERT_STR_CONTAINS(insert_status.ToString(), "more expressions than target columns");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    const auto table_name = prefix + "_table";

    LOG(INFO) << "Select data from the table after restore";
    // Verify that the missing default values are restored correctly.
    const auto query = Format("SELECT value FROM $0", table_name);
    // There might be a transient period when we get stale data before
    // the new catalog version gets propagated to all tservers via heartbeats.
    ASSERT_OK(WaitForSelectQueryToMatchExpectation(query + " WHERE key = 1", "default", &conn));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchValue<std::string>(query + " WHERE key = 2")), "");
    ASSERT_EQ(ASSERT_RESULT(
        conn.FetchValue<std::string>(query + " WHERE key = 3")), "not_default");
    // We should now be able to insert with restored column.
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (4, 'new_value')", table_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlRenameTable) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table 'test_table' and insert a row";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time to restore back: " << time;

  LOG(INFO) << "Alter table 'test_table' -> Rename table to 'new_table'";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table RENAME TO new_table"));

  auto renamed_value = ASSERT_RESULT(
      conn.FetchValue<std::string>("SELECT value FROM new_table"));
  LOG(INFO) << "Read result: " << renamed_value;
  ASSERT_EQ(renamed_value, "before");

  auto result_with_old_name = conn.FetchValue<std::string>("SELECT value FROM test_table");
  ASSERT_FALSE(result_with_old_name.ok());

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Select data from table after restore";
  // There might be a transient period when we get stale data before
  // the new catalog and schema version gets propagated to all tservers via heartbeats.
  std::string select_query = "SELECT value FROM test_table";
  ASSERT_OK(WaitForSelectQueryToMatchExpectation(select_query, "before", &conn));

  auto restore_result = conn.FetchValue<std::string>("SELECT value FROM new_table");
  ASSERT_FALSE(restore_result.ok());
  ASSERT_STR_CONTAINS(restore_result.status().ToString(), "does not exist");

  LOG(INFO) << "Insert data to table after restore";
  auto insert_status = conn.Execute("INSERT INTO new_table VALUES (2, 'new value')");
  ASSERT_FALSE(insert_status.ok());
  ASSERT_STR_CONTAINS(insert_status.ToString(), "does not exist");

  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, 'new value')"));
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlRenameColumn) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table 'test_table' and insert a row";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time to restore back: " << time;

  LOG(INFO) << "Alter table 'test_table' -> Rename 'value' column to 'value2'";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table RENAME COLUMN value TO value2"));

  auto result_with_old_name = conn.FetchValue<std::string>("SELECT value FROM test_table");
  ASSERT_FALSE(result_with_old_name.ok());
  ASSERT_STR_CONTAINS(result_with_old_name.status().ToString(), "does not exist");

  LOG(INFO) << "Reading Rows";
  auto select_res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value2 FROM test_table"));
  LOG(INFO) << "Read result: " << select_res << ", expected: before";
  ASSERT_EQ(select_res, "before");

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Select data from table after restore";
  std::string query = "SELECT value FROM test_table";
  // There might be a transient period when we get stale data before
  // the new catalog version gets propagated to all tservers via heartbeats.
  ASSERT_OK(WaitForSelectQueryToMatchExpectation(query, "before", &conn));

  LOG(INFO) << "Insert data to table after restore";
  auto insert_res = conn.Execute("INSERT INTO test_table(key, value2) VALUES (2, 'new_value')");
  ASSERT_FALSE(insert_res.ok());
  ASSERT_STR_CONTAINS(insert_res.ToString(), "does not exist");

  ASSERT_OK(conn.Execute("INSERT INTO test_table(key, value) VALUES (2, 'new_value')"));
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSetDefault) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table and insert a row";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Alter table and set a default value to the value column";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ALTER COLUMN value SET DEFAULT 'default_value'"));

  LOG(INFO) << "Insert a row without providing a value for the default column";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2)"));

  LOG(INFO) << "Fetch the row inserted above and verify default value is inserted correctly";
  auto res = ASSERT_RESULT(conn.FetchValue<std::string>(
      "SELECT value FROM test_table WHERE key=2"));
  ASSERT_EQ(res, "default_value");

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Insert a new row and verify that the default clause is no longer present";
  // There might be a transient period when we get stale data before
  // the new catalog version gets propagated to all tservers via heartbeats.
  std::string insert_query_template = "INSERT INTO test_table VALUES ($0)";
  std::string select_query_template = "SELECT value FROM test_table WHERE key=$0";
  ASSERT_OK(WaitForInsertQueryToMatchExpectation(
      insert_query_template, select_query_template, 3, "", &conn));

  LOG(INFO) << "Verify that the row with key=2 is no longer present after restore";
  auto result_status = conn.FetchValue<std::string>("SELECT * FROM test_table where key=2");
  ASSERT_FALSE(result_status.ok());
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropDefault) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Create a table with default on the value column and insert a row";
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT default('default_value')) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1)", table_name));

    LOG(INFO) << "Verify default value is set correctly";
    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 WHERE key=1", table_name)));
    ASSERT_EQ(res, "default_value");
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Alter the table -> Drop the default value on the column 'value'";
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ALTER COLUMN value DROP DEFAULT", table_name));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2)", table_name));

    LOG(INFO) << "Verify default is dropped correctly";
    auto res2 = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=2", table_name)));
    ASSERT_EQ(res2, "");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Insert a row and verify that the default clause is still present";
    // There might be a transient period when we get stale data before
    // the new catalog version gets propagated to all tservers via heartbeats.
    std::string insert_query_template = Format("INSERT INTO $0 VALUES ($$0)", table_name);
    std::string select_query_template = Format("SELECT value FROM $0 WHERE key=$$0", table_name);
    ASSERT_OK(WaitForInsertQueryToMatchExpectation(
        insert_query_template, select_query_template, 3, "default_value", &conn));

    LOG(INFO) << "Verify that the row with key=2 is no longer present after restore";
    auto result_status = conn.FetchValue<std::string>(Format(
        "SELECT * FROM $0 where key=2", table_name));
    ASSERT_FALSE(result_status.ok());
  };

  RunTestWithColocatedParam(schedule_id);
}

// Check that postgres sys catalog schema is correctly restored during PITR.
TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlDropDefaultWithPackedRow,
          YbAdminSnapshotScheduleTestWithYsqlAndPackedRow) {
  ASSERT_OK(PrepareCommon());
  ASSERT_OK(CompactTablets(cluster_.get(), 300s * kTimeMultiplier));
  TestPgsqlDropDefault();
  ASSERT_OK(CompactTablets(cluster_.get(), 300s * kTimeMultiplier));

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN v2 TEXT DEFAULT 'v2_default'"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (100500)"));
  auto value = ASSERT_RESULT(
      conn.FetchRowAsString("SELECT value, v2 FROM test_table WHERE key=100500"));
  ASSERT_EQ(value, "default_value, v2_default");
}

// Check that we restore cleaned metadata correctly.
TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlAddColumnCompactWithPackedRow,
          YbAdminSnapshotScheduleTestWithYsqlAndPackedRow) {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  for (;;) {
    auto status = conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)");
    if (status.ok()) {
      break;
    }
    if (status.message().ToBuffer().find("Snapshot too old") != std::string::npos) {
      continue;
    }
    ASSERT_OK(status);
  }
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'one')"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN v2 TEXT"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, 'two', 'dva')"));

  auto time = ASSERT_RESULT(GetCurrentTime());

  ASSERT_OK(WaitNewSnapshot());

  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP COLUMN v2"));

  ASSERT_OK(CompactTablets(cluster_.get(), 300s * kTimeMultiplier));
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
  auto res = ASSERT_RESULT(conn.FetchAllAsString("SELECT * FROM test_table ORDER BY key"));
  ASSERT_EQ(res, "1, one, NULL; 2, two, dva");
}

TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlSingleColumnUpdate,
          YbAdminSnapshotScheduleTestWithYsqlAndPackedRow) {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  {
    auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
    // Create a sequence.
    ASSERT_OK(conn.Execute("CREATE SEQUENCE seq_1 INCREMENT 5 START 10"));
  }
  // Note down the restore time.
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Now get next val in new connection.
  {
    auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
    auto result = ASSERT_RESULT(conn.FetchRowAsString("SELECT nextval('seq_1')"));
    LOG(INFO) << "First value of sequence " << result;
    ASSERT_EQ(result, "10");
  }

  // Restore to noted time.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Open a new connection and get the next value.
  {
    auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
    auto result = ASSERT_RESULT(conn.FetchRowAsString("SELECT nextval('seq_1')"));
    LOG(INFO) << "First value of sequence post restore " << result;
    ASSERT_EQ(result, "510");
  }
}

TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlTestMonotonicSequence,
          YbAdminSnapshotScheduleTestWithYsqlAndPackedRow) {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE seq_1"));
  // last_value column in sequences_data table should be 1 currently.
  // Record this timestamp for restoration.
  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  // Once I read in the first value, last_values should change to 100
  // (since caches are of size 100). Read 50 values.
  for (int i = 1; i <= 50; i++) {
    auto result = ASSERT_RESULT(conn.FetchRowAsString("SELECT nextval('seq_1')"));
    LOG(INFO) << "Iteration: " << i << ", seq value: " << result;
    ASSERT_EQ(stoi(result), i);
  }

  // Restore to time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // The entire data should be wiped out now. Cache should still be at 50.
  // last_value column should not be reverted to 1 but instead remain 100.
  // The first 50 values should be 51,52...,100 due to the cache.
  // Subsequent values should not start from 1 but instead be 101,102...150
  // since last_value was not reverted.
  for (int i = 1; i <= 150; i++) {
    auto result = ASSERT_RESULT(conn.FetchRowAsString("SELECT nextval('seq_1')"));
    LOG(INFO) << "Iteration: " << i << ", seq value: " << result;
    ASSERT_EQ(stoi(result), i+50);
  }
}

void YbAdminSnapshotScheduleTestWithYsql::TestPgsqlDropDefault() {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table with default on the value column and insert a row";
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT default('default_value'))"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1)"));

  LOG(INFO) << "Verify default value is set correctly";
  auto res =
      ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table WHERE key=1"));
  ASSERT_EQ(res, "default_value");

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Alter table and drop the default value on the column value";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ALTER COLUMN value DROP DEFAULT"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2)"));

  LOG(INFO) << "Verify default is dropped correctly";
  auto res2 =
      ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table where key=2"));
  ASSERT_EQ(res2, "");

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Insert a row and verify that the default clause is still present";
  // There might be a transient period when we get stale data before
  // the new catalog version gets propagated to all tservers via heartbeats.
  std::string insert_query_template = "INSERT INTO test_table VALUES ($0)";
  std::string select_query_template = "SELECT value FROM test_table WHERE key=$0";
  ASSERT_OK(WaitForInsertQueryToMatchExpectation(
      insert_query_template, select_query_template, 3, "default_value", &conn));

  LOG(INFO) << "Verify that the row with key=2 is no longer present after restore";
  auto result_status = conn.FetchValue<std::string>("SELECT * FROM test_table where key=2");
  ASSERT_FALSE(result_status.ok());
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSetNotNull) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create a table and insert data";
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'Before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Alter table and set not null";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ALTER COLUMN value SET NOT NULL"));

  LOG(INFO) << "Insert null value in the not null column and assert failure";
  auto insert_res = conn.Execute("INSERT INTO test_table VALUES (2)");
  ASSERT_FALSE(insert_res.ok());
  ASSERT_STR_CONTAINS(insert_res.ToString(), "violates not-null constraint");

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Insert rows with null values and verify it goes through successfully";
  // There might be a transient period of time when constraint exists since
  // relcache is refreshed after a cycle of heartbeats.
  std::string query = "INSERT INTO test_table VALUES (2)";
  ASSERT_OK(WaitForInsertQueryToSucceed(query, &conn));

  auto res3 =
      ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table where key=2"));
  ASSERT_EQ(res3, "");
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropNotNull) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create a table with not null clause and insert data";
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT NOT NULL)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'Before')"));

  LOG(INFO) << "Verify failure on null insertion";
  auto insert_res = conn.Execute("INSERT INTO test_table VALUES (2)");
  ASSERT_FALSE(insert_res.ok());
  ASSERT_STR_CONTAINS(insert_res.ToString(), "violates not-null constraint");

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Alter table and drop not null clause";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ALTER COLUMN value DROP NOT NULL"));

  LOG(INFO) << "Insert null values and verify success";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2)"));

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Verify failure on null insertion since the drop is restored via PITR";
  // There might be a transient period of time when constraint does not exist
  // since relcache is refreshed after a cycle of heartbeats.
  std::string query_template = "INSERT INTO test_table VALUES ($0)";
  ASSERT_OK(WaitForInsertQueryToStopWorking(query_template, &conn, 3));
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlAlterTableAddPK) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Create a table and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT, value TEXT) $1", table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'BeforePK')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Alter the table and add primary key constraint";
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD PRIMARY KEY (key)", table_name));

    LOG(INFO) << "Verify Primary key constraint added";
    auto insert_res = conn.ExecuteFormat(
        "INSERT INTO $0(key, value) VALUES (1, 'AfterPK')", table_name);
    ASSERT_FALSE(insert_res.ok());
    ASSERT_STR_CONTAINS(insert_res.ToString(), "violates unique constraint");

    insert_res = conn.ExecuteFormat("INSERT INTO $0(value) VALUES ('DuringPK')", table_name);
    ASSERT_FALSE(insert_res.ok());
    ASSERT_STR_CONTAINS(insert_res.ToString(), "violates not-null constraint");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Verify primary key constraint no longer exists";
    LOG(INFO) << "Insert a row with key=1 and verify that it succeeds";
    // There might be a transient period of time when constraint exists since
    // relcache is refreshed after a cycle of heartbeats.
    std::string query = Format("INSERT INTO $0 VALUES (1, 'AfterPKRemoval')", table_name);
    ASSERT_OK(WaitForInsertQueryToSucceed(query, &conn));

    // We should now be able to insert a null as well.
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0(value) VALUES ('AfterPITR')", table_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlAlterTableAddFK) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_name_2 = prefix + "_table_2";

    LOG(INFO) << "Create tables and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'BeforeFK')", table_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key_id1 INT PRIMARY KEY, key_id2 INT) $1",
        table_name_2, option));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_name_2 = prefix + "_table_2";

    LOG(INFO) << "Alter table 2 and add foreign key constraint";
    ASSERT_OK(conn.ExecuteFormat("ALTER TABLE $0 ADD CONSTRAINT fk2 "
        "FOREIGN KEY (key_id2) REFERENCES $1(key)", table_name_2, table_name));
    LOG(INFO) << "Verify that foreign key is in effect";
    auto insert_res = conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 2)", table_name_2);
    ASSERT_FALSE(insert_res.ok());
    ASSERT_STR_CONTAINS(insert_res.ToString(), "violates foreign key constraint");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_name_2 = prefix + "_table_2";

    LOG(INFO) << "Verify foreign key no longer exists post PITR";
    // There might be a transient period of time when constraint exists since
    // relcache is refreshed after a cycle of heartbeats.
    std::string query = Format("INSERT INTO $0 VALUES (1, 2)", table_name_2);
    ASSERT_OK(WaitForInsertQueryToSucceed(query, &conn));

    // We should now be able to drop the table.
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlAlterTableSetOwner) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create user user1";
  ASSERT_OK(conn.Execute("CREATE USER user1"));

  LOG(INFO) << "Create user user2";
  ASSERT_OK(conn.Execute("CREATE USER user2"));

  LOG(INFO) << "Set Session authorization to user1";
  ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION user1"));

  LOG(INFO) << "Create table with user1 as the owner";
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Set session authorization to super user";
  ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION yugabyte"));

  LOG(INFO) << "Alter table and set owner of the table to user2";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table OWNER TO user2"));

  LOG(INFO) << "Set session authorization to user2";
  ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION user2"));

  LOG(INFO) << "Verify user session is set correctly";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table RENAME key TO key_new"));

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Wait for the tservers to refresh their cache.
  // We wait for 4 cycles of Heartbeats.
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));

  LOG(INFO) << "Set session authorization to user2";
  ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION user2"));

  LOG(INFO) << "Verify user2 is no longer the owner of the table post PITR and "
               "is unable to perform writes on the table";
  ASSERT_NOK(conn.Execute("ALTER TABLE test_table RENAME key TO key_new2"));

  LOG(INFO) << "Set session authorization to user1";
  ASSERT_OK(conn.Execute("SET SESSION AUTHORIZATION user1"));

  LOG(INFO) << "Verify user1 is able to perform write operations on the table ";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table RENAME key TO key_new3"));
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlAddUniqueConstraint) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Create a table and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT, value TEXT) $1", table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'ABC')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string constraint_name = prefix + "_uniquecst";

    LOG(INFO) << "Alter the table add unique constraint";
    ASSERT_OK(conn.ExecuteFormat(
        "ALTER TABLE $0 ADD CONSTRAINT $1 UNIQUE (value)", table_name, constraint_name));

    LOG(INFO) << "Verify unique constraint added";
    auto insert_res = conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'ABC')", table_name);
    ASSERT_FALSE(insert_res.ok());
    ASSERT_STR_CONTAINS(insert_res.ToString(), "violates unique constraint");
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Verify unique constraint is no longer present";
    // There might be a transient period of time when constraint exists since
    // relcache is refreshed after a cycle of heartbeats.
    std::string query = Format("INSERT INTO $0 VALUES (2, 'ABC')", table_name);
    ASSERT_OK(WaitForInsertQueryToSucceed(query, &conn));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropUniqueConstraint) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string constraint_name = prefix + "_uniquecst";

    LOG(INFO) << "Create a table and insert data";
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT, value TEXT) $1", table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'ABC')", table_name));

    LOG(INFO) << "Add unique constraint to the table";
    ASSERT_OK(conn.ExecuteFormat(
        "ALTER TABLE $0 ADD CONSTRAINT $1 UNIQUE (value)", table_name, constraint_name));

    LOG(INFO) << "Verify unique constraint added";
    auto insert_res = conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'ABC')", table_name);
    ASSERT_FALSE(insert_res.ok());
    ASSERT_STR_CONTAINS(insert_res.ToString(), "violates unique constraint");
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string constraint_name = prefix + "_uniquecst";

    LOG(INFO) << "Drop unique constraint";
    ASSERT_OK(conn.ExecuteFormat(
        "ALTER TABLE $0 DROP CONSTRAINT $1", table_name, constraint_name));

    LOG(INFO) << "Verify unique constraint is dropped";
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (2, 'ABC')", table_name));
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << "Verify that the unique constraint is present and drop is restored";
    // There might be a transient period of time when constraint does not exist
    // since relcache is refreshed after a cycle of heartbeats.
    std::string query_template = Format("INSERT INTO $0 VALUES ($$0, 'ABC')", table_name);
    ASSERT_OK(WaitForInsertQueryToStopWorking(query_template, &conn, 3));

    LOG(INFO) << "Verify that insertion of a row satisfying the unique constraint works";
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (3, 'DEF')", table_name));

    LOG(INFO) << "Verify that the row with key=2 is no longer present after restore";
    auto result_status = conn.FetchValue<std::string>(Format(
        "SELECT * FROM $0 where key=2", table_name));
    ASSERT_FALSE(result_status.ok());
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlAddCheckConstraint) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table and insert data";
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (150, 'ABC')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Alter table and add check constraint";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD CONSTRAINT check_1 CHECK (key > 100)"));

  LOG(INFO) << "Verify Check constraint added";
  auto insert_res = conn.Execute("INSERT INTO test_table VALUES (2, 'XYZ')");
  ASSERT_FALSE(insert_res.ok());
  ASSERT_STR_CONTAINS(insert_res.ToString(), "violates check constraint");

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Verify check constraint is removed post PITR";
  // There might be a transient period of time when constraint exists since
  // relcache is refreshed after a cycle of heartbeats.
  std::string query = "INSERT INTO test_table VALUES (2, 'PQR')";
  ASSERT_OK(WaitForInsertQueryToSucceed(query, &conn));
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlDropCheckConstraint) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table and insert data";
  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT, value TEXT, CONSTRAINT con1 CHECK (key > 1000))"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1001, 'WithCon1')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to to restore the database: " << time;

  LOG(INFO) << "Alter table and drop the check constraint";
  ASSERT_OK(conn.Execute("ALTER TABLE test_table DROP CONSTRAINT con1"));

  LOG(INFO) << "Verify check constraint is dropped";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, 'Constraint_Dropped')"));

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Verify drop constraint is undone post PITR";
  // There might be a transient period of time when constraint does not exist
  // since relcache is refreshed after a cycle of heartbeats.
  std::string query_template = "INSERT INTO test_table VALUES ($0, 'With_Constraint')";
  ASSERT_OK(WaitForInsertQueryToStopWorking(query_template, &conn, 3));

  LOG(INFO) << "Verify insertion of a row satisfying the constraint post restore";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1002, 'After_PITR')"));

  LOG(INFO) << "Verify that the row with key=2 is no longer present after restore";
  auto result_status = conn.FetchValue<std::string>("SELECT * FROM test_table where key=2");
  ASSERT_FALSE(result_status.ok());
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSequenceUndoDeletedData) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table 'test_table'";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  LOG(INFO) << "Create Sequence 'value_data'";
  ASSERT_OK(conn.Execute("CREATE SEQUENCE value_data INCREMENT 5 OWNED BY test_table.value"));
  LOG(INFO) << "Insert some rows to 'test_table'";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, nextval('value_data'))"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, nextval('value_data'))"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (3, nextval('value_data'))"));
  LOG(INFO) << "Reading Rows";
  auto res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=3"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 11);

  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  LOG(INFO) << "Time to restore back " << time;
  LOG(INFO) << "Deleting last row";
  ASSERT_OK(conn.Execute("DELETE FROM test_table where key=3"));

  auto restore_status = RestoreSnapshotSchedule(schedule_id, time);
  ASSERT_OK(restore_status);

  LOG(INFO) << "Select data from 'test_table' after restore";
  res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=3"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 11);
  LOG(INFO) << "Insert a row into 'test_table' and validate";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (4, nextval('value_data'))"));
  res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=4"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 16);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSequenceUndoInsertedData) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Create table 'test_table'";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  LOG(INFO) << "Create Sequence 'value_data'";
  ASSERT_OK(conn.Execute("CREATE SEQUENCE value_data INCREMENT 5 OWNED BY test_table.value"));
  LOG(INFO) << "Insert some rows to 'test_table'";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, nextval('value_data'))"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (2, nextval('value_data'))"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (3, nextval('value_data'))"));
  LOG(INFO) << "Reading Rows";
  auto res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=3"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 11);

  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  LOG(INFO) << "Time to restore back " << time;
  LOG(INFO) << "Inserting new row in 'test_table'";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (4, nextval('value_data'))"));
  LOG(INFO) << "Reading Rows from 'test_table'";
  res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=4"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 16);

  auto restore_status = RestoreSnapshotSchedule(schedule_id, time);
  ASSERT_OK(restore_status);

  LOG(INFO) << "Select row from 'test_table' after restore";
  auto result_status = conn.FetchValue<int32_t>("SELECT value FROM test_table where key=4");
  ASSERT_EQ(result_status.ok(), false);

  res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=3"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 11);
  LOG(INFO) << "Insert a row into 'test_table' and validate";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (4, nextval('value_data'))"));
  // Here value should be 21 instead of 16 as previous insert has value 16
  res = ASSERT_RESULT(conn.FetchValue<int32_t>("SELECT value FROM test_table where key=4"));
  LOG(INFO) << "Select result " << res;
  ASSERT_EQ(res, 21);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSequenceUndoCreateSequence) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << Format("Create table $0", table_name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value INT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 45)", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string sequence_name = prefix + "_value_seq";

    LOG(INFO) << Format("Create sequence $0", sequence_name);
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE SEQUENCE $0 INCREMENT 5 OWNED BY $1.value", sequence_name, table_name));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (2, nextval('$1'))", table_name, sequence_name));
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string sequence_name = prefix + "_value_seq";

    std::string insert_query =
        "INSERT INTO " + table_name + " VALUES ($0, nextval('" + sequence_name + "'))";
    LOG(INFO) << "Insert query " << insert_query;
    // After dropping the sequence, we increment the catalog version to let postgres and tservers
    // refresh their catalog cache. Before the refresh gets done, we can still see the sequence.
    // So WaitForInsertQueryToStopWorking starts with 3 to avoid potential PRIMARY KEY violation.
    ASSERT_OK(WaitForInsertQueryToStopWorking(insert_query, &conn, 3));

    auto res = ASSERT_RESULT(conn.FetchValue<int32_t>(Format(
        "SELECT value FROM $0 where key=1", table_name)));
    ASSERT_EQ(res, 45);

    // Ensure that you are able to create sequences post restore.
    LOG(INFO) << Format("Create sequence $0", sequence_name);
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE SEQUENCE $0 INCREMENT 5 OWNED BY $1.value", sequence_name, table_name));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 VALUES (2, nextval('$1'))", table_name, sequence_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSequenceUndoDropSequence) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << Format("Create table $0", table_name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key SERIAL, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('before')", table_name));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";

    LOG(INFO) << Format("Drop table $0", table_name);
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string new_table_name = prefix + "_table_new";

    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name)));
    ASSERT_EQ(res, "before");
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('after')", table_name));

    // Verify that we are able to create more sequences post restore.
    LOG(INFO) << Format("Create table $0", new_table_name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key SERIAL, value TEXT) $1",
        new_table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('before')", new_table_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSequenceVerifyPartialRestore) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  // Connection to yugabyte database.
  auto conn_yugabyte = ASSERT_RESULT(PgConnect());

  ExecBeforeRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_name_2 = prefix + "_table_2";

    LOG(INFO) << Format("Create table 'demo.$0'", table_name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key SERIAL, value TEXT) $1",
        table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('before')", table_name));

    LOG(INFO) << Format("Create table 'yugabyte.$0'", table_name);
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "CREATE TABLE $0 (key SERIAL, value TEXT) $1", table_name, option));
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "INSERT INTO $0 (value) values ('before')", table_name));

    LOG(INFO) << Format("Create table 'demo.$0'", table_name_2);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key SERIAL, value TEXT) $1",
        table_name_2, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('before')", table_name_2));

    LOG(INFO) << Format("Create table 'yugabyte.$0'", table_name_2);
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "CREATE TABLE $0 (key SERIAL, value TEXT) $1", table_name_2, option));
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "INSERT INTO $0 (value) values ('before')", table_name_2));
  };

  ExecAfterRestoreTS = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_name_3 = prefix + "_table_3";

    LOG(INFO) << Format("Create table 'demo.$0'", table_name_3);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key SERIAL, value TEXT) $1",
        table_name_3, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('before')", table_name_3));

    LOG(INFO) << Format("Create table 'yugabyte.$0'", table_name_3);
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "CREATE TABLE $0 (key SERIAL, value TEXT) $1", table_name_3, option));
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "INSERT INTO $0 (value) values ('before')", table_name_3));

    LOG(INFO) << Format("Drop table 'demo.$0'", table_name);
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE $0", table_name));
    LOG(INFO) << Format("Drop table 'yugabyte.$0'", table_name);
    ASSERT_OK(conn_yugabyte.ExecuteFormat("DROP TABLE $0", table_name));
  };

  CheckAfterPITR = [&](std::string prefix, std::string option) {
    std::string table_name = prefix + "_table";
    std::string table_name_2 = prefix + "_table_2";
    std::string table_name_3 = prefix + "_table_3";
    std::string new_table_name = prefix + "_table_new";

    // demo.table_name should be recreated.
    LOG(INFO) << Format("Select from demo.$0", table_name);
    auto res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name)));
    ASSERT_EQ(res, "before");
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('after')", table_name));

    // demo.table_name_2 should remain as it was.
    LOG(INFO) << Format("Select from demo.$0", table_name_2);
    res = ASSERT_RESULT(conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name_2)));
    ASSERT_EQ(res, "before");
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('after')", table_name_2));

    // demo.table_name_3 should be dropped.
    LOG(INFO) << Format("Select from demo.$0", table_name_3);
    auto r = conn.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name_3));
    ASSERT_EQ(r.ok(), false);

    // yugabyte.table_name shouldn't be recreated.
    LOG(INFO) << Format("Select from yugabyte.$0", table_name);
    r = conn_yugabyte.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name));
    ASSERT_EQ(r.ok(), false);

    // yugabyte.table_name_2 should remain as it was.
    LOG(INFO) << Format("Select from yugabyte.$0", table_name_2);
    res = ASSERT_RESULT(conn_yugabyte.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name_2)));
    ASSERT_EQ(res, "before");
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "INSERT INTO $0 (value) values ('after')", table_name_2));

    // yugabyte.table_name_3 should remain as it was.
    LOG(INFO) << Format("Select from yugabyte.$0", table_name_3);
    res = ASSERT_RESULT(conn_yugabyte.FetchValue<std::string>(Format(
        "SELECT value FROM $0 where key=1", table_name_3)));
    ASSERT_EQ(res, "before");
    ASSERT_OK(conn_yugabyte.ExecuteFormat(
        "INSERT INTO $0 (value) values ('after')", table_name_3));

    // Verify that we are able to create more sequences post restore.
    LOG(INFO) << Format("Create table $0", new_table_name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key SERIAL, value TEXT) $1",
        new_table_name, option));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 (value) values ('before')", new_table_name));
  };

  RunTestWithColocatedParam(schedule_id);
}

TEST_P(YbAdminSnapshotScheduleTestWithYsqlParam, PgsqlSequencePartialCleanupAfterRestore) {
  auto schedule_id = ASSERT_RESULT(PreparePgWithColocatedParam());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  // Connection to yugabyte database.
  auto conn_yugabyte = ASSERT_RESULT(PgConnect());

  std::string table_name = "test_table";
  std::string sequence_name = "test_value_seq";

  LOG(INFO) << Format("Create table 'demo.$0'", table_name);
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", table_name));
  LOG(INFO) << Format("Create table 'yugabyte.$0'", table_name);
  ASSERT_OK(conn_yugabyte.ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value INT)", table_name));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to restore the database back: " << time;

  LOG(INFO) << Format("Create sequence 'demo.$0'", sequence_name);
  ASSERT_OK(conn.ExecuteFormat("CREATE SEQUENCE $0 INCREMENT 5", sequence_name));
  LOG(INFO) << Format("Create sequence 'yugabyte.$0'", sequence_name);
  ASSERT_OK(conn_yugabyte.ExecuteFormat("CREATE SEQUENCE $0 INCREMENT 5", sequence_name));

  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (1, nextval('$1'))", table_name, sequence_name));
  ASSERT_OK(conn_yugabyte.ExecuteFormat(
      "INSERT INTO $0 VALUES (1, nextval('$1'))", table_name, sequence_name));

  // Restore client::kTableName.namespace_name to the time that it doesn't have any sequence. Its
  // sequence data will be cleaned up, while sequences from other namespaces should not be affected.
  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  std::string insert_query =
      "INSERT INTO " + table_name + " VALUES ($0, nextval('" + sequence_name + "'))";
  LOG(INFO) << "Insert query " << insert_query;
  ASSERT_OK(WaitForInsertQueryToStopWorking(insert_query, &conn, 2));

  ASSERT_OK(conn_yugabyte.ExecuteFormat(
      "INSERT INTO $0 VALUES (2, nextval('$1'))", table_name, sequence_name));
  auto res = ASSERT_RESULT(conn_yugabyte.FetchValue<int32_t>(Format(
      "SELECT value FROM $0 where key=2", table_name)));
  // Here value should be 6 because the sequence isn't restored and the previous insert has value 1.
  ASSERT_EQ(res, 6);

  ASSERT_OK(conn.ExecuteFormat("CREATE SEQUENCE $0 INCREMENT 5", sequence_name));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 VALUES (1, nextval('$1'))", table_name, sequence_name));
}

class YbAdminSnapshotScheduleTestWithYsqlAndNoPackedRow
    : public YbAdminSnapshotScheduleTestWithYsql {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    YbAdminSnapshotScheduleTestWithYsql::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back("--ysql_enable_packed_row=false");
    opts->extra_master_flags.emplace_back("--ysql_enable_packed_row=false");
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlTestMonotonicSequenceNoPacked,
          YbAdminSnapshotScheduleTestWithYsqlAndNoPackedRow) {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  ASSERT_OK(conn.Execute("CREATE SEQUENCE seq_1 INCREMENT 2"));
  // last_value column in sequences_data table should be 1 currently.
  // Record this timestamp for restoration.
  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  // Once I read in the first value, last_values should change to 199
  // (since caches are of size 100). Read 50 values.
  for (int i = 1; i <= 99; i += 2) {
    auto result = ASSERT_RESULT(conn.FetchRowAsString("SELECT nextval('seq_1')"));
    LOG(INFO) << "Iteration: " << i << ", seq value: " << result;
    ASSERT_EQ(stoi(result), i);
  }

  // Restore to time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // The entire data should be wiped out now. Cache should still be at 99.
  // last_value column should not be reverted to 1 but instead remain 199.
  // The first 50 values should be 101,103...,199 due to the cache.
  // Subsequent values should not start from 1 but instead be 201,203...
  // since last_value was not reverted.
  for (int i = 101; i <= 399; i += 2) {
    auto result = ASSERT_RESULT(conn.FetchRowAsString("SELECT nextval('seq_1')"));
    LOG(INFO) << "Iteration: " << i << ", seq value: " << result;
    ASSERT_EQ(stoi(result), i);
  }
}

class YbAdminSnapshotScheduleTestPerDbCatalogVersion : public YbAdminSnapshotScheduleTestWithYsql {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    YbAdminSnapshotScheduleTestWithYsql::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back(
        "--allowed_preview_flags_csv=ysql_enable_db_catalog_version_mode");
    opts->extra_master_flags.emplace_back(
        "--allowed_preview_flags_csv=ysql_enable_db_catalog_version_mode");
  }

  Status PrepareDbCatalogVersion(pgwrapper::PGConn* conn) {
    LOG(INFO) << "Preparing pg_yb_catalog_version to have one row per database";
    RETURN_NOT_OK(conn->Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=1"));
    // "ON CONFLICT DO NOTHING" is only needed for the case where the cluster already has
    // those rows (e.g., when initdb is run with --ysql_enable_db_catalog_version_mode=true).
    RETURN_NOT_OK(conn->Execute("INSERT INTO pg_catalog.pg_yb_catalog_version "
                                "SELECT oid, 1, 1 from pg_catalog.pg_database where oid != 1 "
                                "ON CONFLICT DO NOTHING"));
    return Status::OK();
  }

  void RestartClusterSetDbCatalogVersionMode(
      bool enabled, const std::vector<string>& extra_tserver_flags) {
    LOG(INFO) << "Restart the cluster and turn "
              << (enabled ? "on" : "off") << " --ysql_enable_db_catalog_version_mode";
    cluster_->Shutdown();
    const string db_catalog_version_gflag =
      Format("--ysql_enable_db_catalog_version_mode=$0", enabled ? "true" : "false");
    for (size_t i = 0; i != cluster_->num_masters(); ++i) {
      cluster_->master(i)->mutable_flags()->push_back(db_catalog_version_gflag);
    }
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      cluster_->tablet_server(i)->mutable_flags()->push_back(db_catalog_version_gflag);
      for (const auto& flag : extra_tserver_flags) {
        cluster_->tablet_server(i)->mutable_flags()->push_back(flag);
      }
    }
    ASSERT_OK(cluster_->Restart());
  }

  using Version = uint64_t;

  struct CatalogVersion {
    Version current_version;
    Version last_breaking_version;
  };

  using MasterCatalogVersionMap = std::unordered_map<Oid, CatalogVersion>;

  static Result<MasterCatalogVersionMap> GetMasterCatalogVersionMap(pgwrapper::PGConn* conn) {
    auto res = VERIFY_RESULT(conn->Fetch("SELECT * FROM pg_yb_catalog_version"));
    const auto lines = PQntuples(res.get());
    SCHECK_GT(lines, 0, IllegalState, "empty version map");
    SCHECK_EQ(PQnfields(res.get()), 3, IllegalState, "Unexpected column count");
    MasterCatalogVersionMap result;
    std::string output;
    for (int i = 0; i != lines; ++i) {
      const auto db_oid = VERIFY_RESULT(pgwrapper::GetValue<pgwrapper::PGOid>(res.get(), i, 0));
      const auto current_version = VERIFY_RESULT(
          pgwrapper::GetValue<pgwrapper::PGUint64>(res.get(), i, 1));
      const auto last_breaking_version = VERIFY_RESULT(
          pgwrapper::GetValue<pgwrapper::PGUint64>(res.get(), i, 2));
      result.emplace(db_oid, CatalogVersion{current_version, last_breaking_version});
      if (!output.empty()) {
        output += ", ";
      }
      output += Format("($0, $1, $2)", db_oid, current_version, last_breaking_version);
    }
    LOG(INFO) << "Catalog version map: " << output;
    return result;
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlTestPerDbCatalogVersion,
          YbAdminSnapshotScheduleTestPerDbCatalogVersion) {
  auto schedule_id = ASSERT_RESULT(PreparePg());

  // Turn on the per db catalog version.
  auto conn_yb = ASSERT_RESULT(PgConnect("yugabyte"));
  ASSERT_OK(PrepareDbCatalogVersion(&conn_yb));
  RestartClusterSetDbCatalogVersionMode(true, {});
  LOG(INFO) << "Per db catalog version is turned on";

  // Create another db.
  conn_yb = ASSERT_RESULT(PgConnect("yugabyte"));
  ASSERT_OK(conn_yb.Execute("CREATE DATABASE demo2"));

  // Create a table on this database and issue a few alters to bump up the catalog version.
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  LOG(INFO) << "Create table 'test_table'";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));

  // Note down the time to restore, we'll be restoring to this time.
  auto time = ASSERT_RESULT(GetCurrentTime());

  // Issue a few alters to bump up the catalog version.
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value2 INT"));
  ASSERT_OK(conn.Execute("ALTER TABLE test_table ADD COLUMN value3 INT"));

  // Wait for catalog version to be propagated.
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));

  // Create demo3.
  ASSERT_OK(conn_yb.Execute("CREATE DATABASE demo3"));

  auto ns_list = ASSERT_RESULT(client_->ListNamespaces());
  std::string my_ns_id;
  std::string demo2_ns_id;
  std::string demo3_ns_id;

  for (const auto& ns : ns_list) {
    if (ns.id.name() == client::kTableName.namespace_name()) {
      my_ns_id = ns.id.id();
    } else if (ns.id.name() == "demo2") {
      demo2_ns_id = ns.id.id();
    } else if (ns.id.name() == "demo3") {
      demo3_ns_id = ns.id.id();
    }
  }
  ASSERT_FALSE(my_ns_id.empty());
  ASSERT_FALSE(demo2_ns_id.empty());
  ASSERT_FALSE(demo3_ns_id.empty());

  auto my_ns_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(my_ns_id));
  auto demo2_ns_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(demo2_ns_id));
  auto demo3_ns_oid = ASSERT_RESULT(GetPgsqlDatabaseOid(demo3_ns_id));

  LOG(INFO) << "DB OID of my_keyspace " << my_ns_oid;
  LOG(INFO) << "DB OID of demo2 " << demo2_ns_oid;
  LOG(INFO) << "DB OID of demo3 " << demo3_ns_oid;

  // Drop demo2.
  ASSERT_OK(conn_yb.Execute("DROP DATABASE demo2"));

  // Note the catalog version of all the databases.
  auto map_before_restore = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn));

  // Restore to time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Wait for catalog version to be propagated.
  SleepFor(MonoDelta::FromSeconds(2 * kTimeMultiplier));

  // Get the map again
  auto map_after_restore = ASSERT_RESULT(GetMasterCatalogVersionMap(&conn));

  // only for this db the catalog version should change.
  bool found_demo3 = false;
  bool found_my_ns = false;
  for (const auto& [Oid, CatalogVersion] : map_after_restore) {
    // demo2 has been dropped.
    ASSERT_NE(Oid, demo2_ns_oid);
    if (Oid == demo3_ns_oid) {
      found_demo3 = true;
    }
    if (Oid == my_ns_oid) {
      found_my_ns = true;
    }
    ASSERT_TRUE(map_before_restore.contains(Oid));
    if (Oid != my_ns_oid) {
      ASSERT_EQ(CatalogVersion.current_version, map_before_restore[Oid].current_version);
      ASSERT_EQ(
          CatalogVersion.last_breaking_version, map_before_restore[Oid].last_breaking_version);
    } else {
      ASSERT_EQ(CatalogVersion.current_version, map_before_restore[Oid].current_version + 1);
      ASSERT_EQ(
          CatalogVersion.last_breaking_version, map_before_restore[Oid].last_breaking_version);
    }
    map_before_restore.erase(Oid);
  }
  ASSERT_TRUE(map_before_restore.empty());
  ASSERT_TRUE(found_demo3);
  ASSERT_TRUE(found_my_ns);
}

TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlTestTruncateDisallowedWithPitr,
          YbAdminSnapshotScheduleTestWithYsql) {
  auto schedule_id = ASSERT_RESULT(PreparePg());

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  LOG(INFO) << "Create table 'test_table'";
  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 1)"));

  auto s = conn.Execute("TRUNCATE TABLE test_table");
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.ToString(), "Cannot truncate table test_table which has schedule");

  LOG(INFO) << "Enable flag to allow truncate and validate that truncate succeeds";
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_truncate_on_pitr_table", "true"));
  ASSERT_OK(conn.Execute("TRUNCATE TABLE test_table"));
}

TEST_F_EX(YbAdminSnapshotScheduleTest, RestoreDroppedTablegroup,
          YbAdminSnapshotScheduleTestWithYsqlParam) {
  auto schedule_id = ASSERT_RESULT(PreparePg(YsqlColocationConfig::kTablegroup));
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  std::string create_table_sql = "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) TABLEGROUP $1";

  LOG(INFO) << Format("Create table 'test_table' under '$0'", kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat(create_table_sql, "test_table", kTablegroupName));
  LOG(INFO) << "Insert some rows to 'test_table'";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to restore the database back: " << time;

  ASSERT_OK(conn.Execute("DROP TABLE test_table"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLEGROUP $0", kTablegroupName));
  ASSERT_NOK(conn.ExecuteFormat(create_table_sql, "test_table_2", kTablegroupName));

  // Check all the tablets are hidden.
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    bool all_tablets_hidden = true;
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
      tserver::ListTabletsRequestPB req;
      tserver::ListTabletsResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(30s);
      RETURN_NOT_OK(proxy.ListTablets(req, &resp, &controller));
      for (const auto& tablet : resp.status_and_schema()) {
        if (tablet.tablet_status().namespace_name() == client::kTableName.namespace_name()) {
          LOG(INFO) << "Tablet " << tablet.tablet_status().tablet_id() << " of table "
                    << tablet.tablet_status().table_name() << ", hidden status "
                    << tablet.tablet_status().is_hidden();
          all_tablets_hidden = all_tablets_hidden && tablet.tablet_status().is_hidden();
        }
      }
    }
    return all_tablets_hidden;
  }, 30s, "Wait for all the tablets marking as hidden."));

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Select data from 'test_table' after restore";
  std::string query = "SELECT value FROM test_table WHERE key = 1";
  ASSERT_OK(WaitForSelectQueryToMatchExpectation(query, "before", &conn));

  ASSERT_OK(conn.ExecuteFormat(create_table_sql, "test_table_2", kTablegroupName));
  ASSERT_OK(conn.Execute("INSERT INTO test_table_2 VALUES (1, 'after')"));
}

TEST_F_EX(YbAdminSnapshotScheduleTest, DropTablegroupWithRestoredTable,
          YbAdminSnapshotScheduleTestWithYsqlParam) {
  // Verify when a tablegroup child table is HIDDEN in both current and restoring time, then PITR
  // should NOT unhide it accidently.
  auto schedule_id = ASSERT_RESULT(PreparePg(YsqlColocationConfig::kTablegroup));
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << Format("Create table 'test_table' under '$0'", kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_table (key INT, value TEXT) TABLEGROUP $0", kTablegroupName));
  LOG(INFO) << "Insert some rows to 'test_table'";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  LOG(INFO) << Format("Create table 'test_table_2' under '$0'", kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_table_2 (key INT, value TEXT) TABLEGROUP $0", kTablegroupName));
  LOG(INFO) << "Drop table 'test_table_2'";
  ASSERT_OK(conn.Execute("DROP TABLE test_table_2"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to restore the database back: " << time;

  ASSERT_OK(conn.Execute("DROP TABLE test_table"));

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Insert data to 'test_table' after restore";
  std::string query = "INSERT INTO test_table VALUES (1, 'after')";
  ASSERT_OK(WaitForInsertQueryToSucceed(query, &conn));

  LOG(INFO) << Format("Drop the tablegroup '$0'", kTablegroupName);
  ASSERT_NOK(conn.ExecuteFormat("DROP TABLEGROUP $0", kTablegroupName));

  LOG(INFO) << "Drop table 'test_table'";
  ASSERT_OK(conn.Execute("DROP TABLE test_table"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLEGROUP $0", kTablegroupName));
}

TEST_F_EX(YbAdminSnapshotScheduleTest, RestoreWithTwoTablegroups,
          YbAdminSnapshotScheduleTestWithYsqlParam) {
  // Verify PITR restore does HIDE the current tablegroup which didn't exist in the restoring time
  // and recover the one exists in the restoring time.
  auto schedule_id = ASSERT_RESULT(PreparePg(YsqlColocationConfig::kTablegroup));
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << Format("Create table 'test_table' under '$0'", kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE test_table (key INT, value TEXT) TABLEGROUP $0", kTablegroupName));
  LOG(INFO) << "Insert some rows to 'test_table'";
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to restore the database back: " << time;

  ASSERT_OK(conn.Execute("DROP TABLE test_table"));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLEGROUP $0", kTablegroupName));
  LOG(INFO) << "Create a new tablegroup 'test_tg'";
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP test_tg"));

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Insert data to 'test_table' after restore";
  std::string insert_sql = "INSERT INTO test_table VALUES (1, 'after')";
  ASSERT_OK(WaitForInsertQueryToSucceed(insert_sql, &conn));

  std::string query =
      "CREATE TABLE test_table_2 (key INT PRIMARY KEY, value TEXT) TABLEGROUP $0";
  LOG(INFO) << Format("Create table 'test_table' under '$0'", kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat(query, kTablegroupName));

  LOG(INFO) << "Create table 'test_table_2' under 'test_tg'";
  ASSERT_NOK(conn.ExecuteFormat(query, "test_tg"));
}

TEST_F_EX(YbAdminSnapshotScheduleTest, TablegroupGCAfterRestore,
          YbAdminSnapshotScheduleTestWithYsqlParam) {
  // Verify the tablegroup gets cleaned up if it is marked as HIDDEN after PITR restore and
  // goes out of retention period.
  auto schedule_id = ASSERT_RESULT(PreparePg(
      YsqlColocationConfig::kNotColocated, kInterval, kInterval * 2));
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time noted to restore the database back: " << time;

  LOG(INFO) << Format("Create tablegroup '$0'", kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLEGROUP $0", kTablegroupName));

  std::string child_table_name = "test_table_gc";
  LOG(INFO) << Format("Create table '$0' under '$1'", child_table_name, kTablegroupName);
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key INT, value TEXT) TABLEGROUP $1", child_table_name, kTablegroupName));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 'before')", child_table_name));

  auto tablegroups = ASSERT_RESULT(client_->ListTablegroups(client::kTableName.namespace_name()));
  ASSERT_EQ(tablegroups.size(), 1);
  TableId parent_table_id = GetTablegroupParentTableId(tablegroups[0].id());
  LOG(INFO) << "Tablegroup parent table name: " << parent_table_id;

  LOG(INFO) << "Perform a Restore to the time noted above";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // We give 2 rounds of retention period for cleanup.
  auto deadline = CoarseMonoClock::now() + kInterval * 4;
  ASSERT_OK(Wait([this, child_table_name, parent_table_id, deadline]() -> Result<bool> {
    bool tablegroup_deleted_cascade = true;
    auto proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();
    master::ListTablesRequestPB req;
    master::ListTablesResponsePB resp;
    rpc::RpcController controller;
    controller.set_deadline(deadline);
    req.mutable_namespace_()->set_name(client::kTableName.namespace_name());
    req.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
    req.set_include_not_running(true);
    RETURN_NOT_OK(proxy.ListTables(req, &resp, &controller));
    for (const auto& table : resp.tables()) {
      if (table.name() == child_table_name || table.id() == parent_table_id) {
        tablegroup_deleted_cascade = tablegroup_deleted_cascade &&
            table.state() == master::SysTablesEntryPB::DELETED;
      }
    }
    return tablegroup_deleted_cascade;
  }, deadline, "Wait for tablegroup and its child table marking as DELETED."));

  for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
    auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
    tserver::ListTabletsRequestPB req;
    tserver::ListTabletsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(30s);
    ASSERT_OK(proxy.ListTablets(req, &resp, &controller));
    for (const auto& tablet : resp.status_and_schema()) {
      // We only remove tablet::TABLET_DATA_DELETED tablets from the tablet map, so no tablet in the
      // namespace means the GC has been done.
      ASSERT_FALSE(tablet.tablet_status().namespace_name() == client::kTableName.namespace_name());
    }
  }
}

class YbAdminSnapshotScheduleTestWithYsqlRetention : public YbAdminSnapshotScheduleTestWithYsql {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    YbAdminSnapshotScheduleTestWithYsql::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back("--timestamp_history_retention_interval_sec=0");
    opts->extra_master_flags.emplace_back("--timestamp_history_retention_interval_sec=0");
    opts->extra_tserver_flags.emplace_back(
        "--timestamp_syscatalog_history_retention_interval_sec=0");
    opts->extra_master_flags.emplace_back(
        "--timestamp_syscatalog_history_retention_interval_sec=0");
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, SysCatalogRetention,
          YbAdminSnapshotScheduleTestWithYsqlRetention) {
  auto schedule_id = ASSERT_RESULT(PreparePg(YsqlColocationConfig::kNotColocated, 300s, 1200s));
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  for (int i = 1; i <= 50; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0(id INT PRIMARY KEY, name TEXT)", i));
  }
  // Note down the time.
  auto time = ASSERT_RESULT(GetCurrentTime());
  for (int i = 1; i <= 50; i++) {
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE t$0", i));
  }
  // Flush and compact sys catalog. The original create table entries should not be
  // removed.
  ASSERT_OK(FlushAndCompactSysCatalog(cluster_.get(), 300s));
  // Restore to time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Tables should exist now.
  for (int i = 1; i <= 50; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t$0 (id, name) VALUES (1, 'after')", i));
  }
}

TEST_F_EX(YbAdminSnapshotScheduleTest, SysCatalogRetentionWithFastPitr,
          YbAdminSnapshotScheduleTestWithYsqlRetention) {
  auto schedule_id = ASSERT_RESULT(PreparePg(YsqlColocationConfig::kNotColocated, 30s, 600s));
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  // Create 10 tables.
  for (int i = 1; i <= 10; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0(id INT PRIMARY KEY, name TEXT)", i));
  }
  // Enable fast pitr.
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_fast_pitr", "true"));
  // Note down the time.
  auto time = ASSERT_RESULT(GetCurrentTime());

  for (int i = 1; i <= 10; i++) {
    ASSERT_OK(conn.ExecuteFormat("DROP TABLE t$0", i));
  }

  // Wait for at least one more snapshot.
  ASSERT_OK(WaitForMoreSnapshots(schedule_id, 300s, 2, "Wait for 2 more snapshots"));

  // Flush and compact sys catalog. The original create table entries should not be
  // removed.
  ASSERT_OK(FlushAndCompactSysCatalog(cluster_.get(), 300s));
  // Restore to time noted above.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Tables should exist now.
  for (int i = 1; i <= 10; i++) {
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO t$0 (id, name) VALUES (1, 'after')", i));
  }
}

class YbAdminSnapshotScheduleUpgradeTestWithYsql : public YbAdminSnapshotScheduleTestWithYsql {
  std::vector<std::string> ExtraMasterFlags() override {
    // To speed up tests.
    std::string build_type;
    if (DEBUG_MODE) {
      build_type = "debug";
    } else {
      build_type = "release";
    }
    std::string old_sys_catalog_snapshot_full_path =
        old_sys_catalog_snapshot_path + old_sys_catalog_snapshot_name + "_" + build_type;
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500",
             "--enable_automatic_tablet_splitting=true",
             "--enable_transactional_ddl_gc=false",
             "--initial_sys_catalog_snapshot_path="+old_sys_catalog_snapshot_full_path };
  }
};

TEST_F(YbAdminSnapshotScheduleUpgradeTestWithYsql,
       PgsqlTestOldSysCatalogSnapshot) {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.Execute("CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));

  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  ASSERT_OK(conn.Execute("DROP TABLE test_table"));

  auto restore_status = RestoreSnapshotSchedule(schedule_id, time);
  ASSERT_OK(restore_status);

  auto res = ASSERT_RESULT(conn.FetchValue<std::string>(
      "SELECT value FROM test_table WHERE key = 1"));
  ASSERT_EQ(res, "before");
  ASSERT_OK(conn.Execute("UPDATE test_table SET value = 'after'"));
  res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table WHERE key = 1"));
  ASSERT_EQ(res, "after");
}

TEST_F(
    YbAdminSnapshotScheduleUpgradeTestWithYsql,
    YB_DISABLE_TEST_IN_SANITIZERS(PgsqlTestMigrationFromEarliestSysCatalogSnapshot)) {
  auto schedule_id = ASSERT_RESULT(PreparePg());
  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
  LOG(INFO) << "Assert pg_yb_migration table does not exist.";
  std::string query = "SELECT count(*) FROM pg_yb_migration LIMIT 1";
  auto query_status = conn.Execute(query);
  ASSERT_NOK(query_status);
  ASSERT_STR_CONTAINS(query_status.message().ToBuffer(), "does not exist");
  LOG(INFO) << "Save time to restore to.";
  Timestamp time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Run upgrade_ysql to create and populate pg_yb_migration table.";
  auto result = ASSERT_RESULT(CallAdmin("-timeout_ms", 19 * 60 * 1000, "upgrade_ysql"));
  LOG(INFO) << "Assert pg_yb_migration table exists.";
  ASSERT_RESULT(conn.FetchValue<int64_t>(query));
  auto restore_status = RestoreSnapshotSchedule(schedule_id, time);
  LOG(INFO) << "Assert restore fails because of system catalog changes.";
  ASSERT_NOK(restore_status);
  ASSERT_STR_CONTAINS(
      restore_status.message().ToBuffer(),
      "Snapshot state and current state have different system catalogs");
}

TEST_F(YbAdminSnapshotScheduleTest, UndeleteIndex) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
      "WITH transactions = { 'enabled' : true }"));
  ASSERT_OK(conn.ExecuteQuery("CREATE UNIQUE INDEX test_table_idx ON test_table (value)"));

  // Wait for backfill to complete.
  // TODO(Sanket): We should remove this sleep once
  // https://github.com/yugabyte/yugabyte-db/issues/13744 is fixed.
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));

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

TEST_F(YbAdminSnapshotScheduleTest, UndeleteIndexToBackfillTime) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
      "WITH transactions = { 'enabled' : true }"));

  // Insert enough data so as to have a non-trivial backfill.
  for (int i = 0; i < 1000; i++) {
    ASSERT_OK(conn.ExecuteQuery(
        Format("INSERT INTO test_table (key, value) VALUES ($0, 'before$1')", i, i)));
  }

  // Leave the index in an inconsistent state.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_skip_index_backfill", "true"));
  ASSERT_OK(conn.ExecuteQuery("CREATE UNIQUE INDEX test_table_idx ON test_table (value)"));

  auto time = ASSERT_RESULT(GetCurrentTime());
  LOG(INFO) << "Time to restore " << time.ToHumanReadableTime();

  ASSERT_OK(conn.ExecuteQuery("DROP INDEX test_table_idx"));

  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (key, value) VALUES (1001, 'after')"));

  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_skip_index_backfill", "false"));

  LOG(INFO) << "Restoring to a time when backfill was in progress";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Wait for backfill to complete.
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));

  // Check for unique constraint.
  for (int i = 0; i < 1000; i++) {
    auto err_msg = conn.ExecuteQuery(
        Format("INSERT INTO test_table (key, value) VALUES ($0, 'before$1')", i + 2000, i));
    ASSERT_FALSE(err_msg.ok());
    ASSERT_STR_CONTAINS(err_msg.ToString(), "Duplicate value disallowed by unique index");
  }

  auto res = ASSERT_RESULT(conn.FetchValue<int32_t>(
      "SELECT key FROM test_table WHERE value = 'before1'"));

  ASSERT_EQ(res, 1);
}

TEST_F(YbAdminSnapshotScheduleTest, RestoreDuringBackfill) {
  // While a backfill of a YCQL table in one keyspace is in progress, do a PITR on a separate YCQL
  // keyspace. This test checks the backfill completes successfully, regardless of the PITR.
  auto schedule_id = ASSERT_RESULT(PrepareCql());
  constexpr int pitr_row_cnt = 100;
  constexpr int backfill_row_cnt = 250;
  std::string pitr_table_name = "test_table";
  const auto backfill_yb_table =
      client::YBTableName(YQL_DATABASE_CQL, "backfill_kspace", "test_host_table");
  auto get_pitr_table_row_cnt = [&pitr_table_name](CassandraSession* conn) -> Result<int> {
    auto count = VERIFY_RESULT(
        conn->ExecuteAndRenderToString(Format("SELECT COUNT(*) from $0", pitr_table_name)));
    return std::stoi(count);
  };
  auto initialize_table = [](CassandraSession* conn, const std::string& table_name,
                             int row_cnt) -> Status {
    RETURN_NOT_OK(conn->ExecuteQuery(Format(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH transactions = { 'enabled' : true "
        "}",
        table_name)));
    auto prepared = VERIFY_RESULT(
        conn->Prepare(Format("INSERT INTO $0 (key, value) VALUES (?, ?)", table_name)));
    auto batch = CassandraBatch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
    int cnt = 1;
    for (int i = 0; i < row_cnt; ++i, ++cnt) {
      auto stmt = prepared.Bind();
      stmt.Bind(0, i);
      stmt.Bind(1, Format("$0", i));
      batch.Add(&stmt);
    }
    return conn->ExecuteBatch(batch);
  };
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));
  ASSERT_OK(initialize_table(&conn, pitr_table_name, pitr_row_cnt));
  ASSERT_EQ(ASSERT_RESULT(get_pitr_table_row_cnt(&conn)), pitr_row_cnt);
  ASSERT_OK(conn.ExecuteQuery(
      Format("CREATE KEYSPACE IF NOT EXISTS $0", backfill_yb_table.namespace_name())));

  auto backfill_conn = ASSERT_RESULT(CqlConnect(backfill_yb_table.namespace_name()));
  ASSERT_OK(initialize_table(&backfill_conn, backfill_yb_table.table_name(), backfill_row_cnt));

  auto time = ASSERT_RESULT(GetCurrentTime());
  ASSERT_OK(conn.ExecuteQuery(Format(
      "INSERT INTO $0 (key, value) VALUES ($1, '$2')", pitr_table_name, pitr_row_cnt + 1,
      pitr_row_cnt + 1)));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "true"));
  ASSERT_EQ(ASSERT_RESULT(get_pitr_table_row_cnt(&conn)), pitr_row_cnt + 1);
  ASSERT_OK(
      backfill_conn.ExecuteQuery("CREATE UNIQUE INDEX test_table_idx ON test_host_table (value)"));
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_block_do_backfill", "false"));
  ASSERT_EQ(ASSERT_RESULT(get_pitr_table_row_cnt(&conn)), pitr_row_cnt);
  auto proxy = cluster_->GetLeaderMasterProxy<master::MasterDdlProxy>();

  LOG(INFO) << "Wait for index backfill to complete.";
  ASSERT_OK(WaitFor(
      [&proxy, &backfill_yb_table]() -> Result<bool> {
        master::GetTableSchemaRequestPB req;
        master::GetTableSchemaResponsePB resp;
        rpc::RpcController controller;
        backfill_yb_table.SetIntoTableIdentifierPB(req.mutable_table());
        RETURN_NOT_OK(proxy.GetTableSchema(req, &resp, &controller));
        if (resp.indexes_size() != 1) {
          return STATUS_FORMAT(
              IllegalState, "Expected to see 1 index for table, got: $0", resp.indexes_size());
        }
        return resp.indexes(0).index_permissions() ==
               IndexPermissions::INDEX_PERM_READ_WRITE_AND_DELETE;
      },
      200s * kTimeMultiplier, "Timed out waiting for index backfill to complete."));
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
  TestGCHiddenTables();
}

class YbAdminSnapshotScheduleTestWithYcqlPackedRow : public YbAdminSnapshotScheduleTestWithYsql {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    YbAdminSnapshotScheduleTestWithYsql::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back("--ycql_enable_packed_row=true");
    opts->extra_master_flags.emplace_back("--ycql_enable_packed_row=true");
  }
};

TEST_F(YbAdminSnapshotScheduleTestWithYcqlPackedRow, GCHiddenTables) {
  TestGCHiddenTables();
}

void YbAdminSnapshotScheduleTest::TestGCHiddenTables() {
  const auto interval = 15s;
  const auto retention = 30s * kTimeMultiplier;
  auto schedule_id = ASSERT_RESULT(PrepareQl(interval, retention));

  auto session = client_->NewSession(60s);
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

class YbAdminSnapshotConsistentRestoreTest : public YbAdminSnapshotScheduleTest {
 public:
  virtual std::vector<std::string> ExtraTSFlags() { return {"--TEST_tablet_delay_restore_ms=0"}; }
};

Status WaitWrites(int num, std::atomic<int>* current) {
  auto stop = current->load() + num;
  return WaitFor([current, stop] { return current->load() >= stop; },
                 20s, Format("Wait $0 ($1) writes", stop, num));
}

YB_DEFINE_ENUM(KeyState, (kNone)(kMissing)(kBeforeMissing)(kAfterMissing));

// Check that restoration is consistent across tablets.
// Concurrently write keys and store event stream, i.e when we started or finished to write key.
// After restore we fetch all keys and see what keys were removed during restore.
// So for each such key we could find what keys were written strictly before it,
// i.e. before restore. Or strictly after, i.e. after restore.
// Then we check that key is not marked as before and after restore simultaneously.
TEST_F_EX(YbAdminSnapshotScheduleTest, ConsistentRestore, YbAdminSnapshotConsistentRestoreTest) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery("CREATE TABLE test_table (k1 INT PRIMARY KEY)"));

  struct EventData {
    int key;
    bool finished;
  };

  std::atomic<int> written{0};
  TestThreadHolder thread_holder;

  std::vector<EventData> events;

  thread_holder.AddThreadFunctor([&conn, &written, &events, &stop = thread_holder.stop_flag()] {
    auto prepared = ASSERT_RESULT(conn.Prepare("INSERT INTO test_table (k1) VALUES (?)"));
    std::vector<std::pair<int, CassandraFuture>> futures;
    int key = 0;
    constexpr int kBlock = 10;
    while (!stop.load() || !futures.empty()) {
      auto filter = [&events, &written](auto& key_and_future) {
        if (!key_and_future.second.Ready()) {
          return false;
        }
        auto write_status = key_and_future.second.Wait();
        if (write_status.ok()) {
          events.push_back(EventData{.key = key_and_future.first, .finished = true});
          ++written;
        } else {
          LOG(WARNING) << "Write failed: " << write_status;
        }
        return true;
      };
      ASSERT_NO_FATALS(EraseIf(filter, &futures));
      if (futures.size() < kBlock && !stop.load()) {
        auto write_key = ++key;
        auto stmt = prepared.Bind();
        stmt.Bind(0, write_key);
        futures.emplace_back(write_key, conn.ExecuteGetFuture(stmt));
        events.push_back(EventData{.key = write_key, .finished = false});
      }
      std::this_thread::sleep_for(10ms);
    }
  });

  {
    auto se = ScopeExit([&thread_holder] {
      thread_holder.Stop();
    });
    ASSERT_OK(WaitWrites(50, &written));

    Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

    ASSERT_OK(WaitWrites(10, &written));

    ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

    ASSERT_OK(WaitWrites(10, &written));
  }

  struct KeyData {
    KeyState state;
    ssize_t start = -1;
    ssize_t finish = -1;
    ssize_t set_by = -1;
  };

  std::vector<KeyData> keys;

  for (;;) {
    keys.clear();
    auto result = conn.ExecuteWithResult("SELECT * FROM test_table");
    if (!result.ok()) {
      LOG(WARNING) << "Select failed: " << result.status();
      continue;
    }

    auto iter = result->CreateIterator();
    while (iter.Next()) {
      auto row = iter.Row();
      int key = row.Value(0).As<int32_t>();
      keys.resize(std::max<size_t>(keys.size(), key + 1), {.state = KeyState::kMissing});
      keys[key].state = KeyState::kNone;
    }
    break;
  }

  for (size_t i = 0; i != events.size(); ++i) {
    auto& event = events[i];
    auto& key_data = keys[event.key];
    if (key_data.state == KeyState::kMissing) {
      (event.finished ? key_data.finish : key_data.start) = i;
    }
  }

  for (size_t key = 1; key != keys.size(); ++key) {
    if (keys[key].state != KeyState::kMissing || keys[key].finish == -1) {
      continue;
    }
    for (auto set_state : {KeyState::kBeforeMissing, KeyState::kAfterMissing}) {
      auto begin = set_state == KeyState::kBeforeMissing ? 0 : keys[key].finish + 1;
      auto end = set_state == KeyState::kBeforeMissing ? keys[key].start : events.size();
      for (size_t i = begin; i != end; ++i) {
        auto& event = events[i];
        if (keys[event.key].state == KeyState::kMissing ||
            (event.finished != (set_state == KeyState::kBeforeMissing))) {
          continue;
        }
        if (keys[event.key].state == KeyState::kNone) {
          keys[event.key].state = set_state;
          keys[event.key].set_by = key;
        } else if (keys[event.key].state != set_state) {
          FAIL() << "Key " << event.key << " already marked as " << keys[event.key].state
                 << ", while trying to set: " << set_state << " with " << key << ", prev set: "
                 << keys[event.key].set_by;
        }
      }
    }
  }
}

// Write multiple transactions and restore.
// Then check that we don't have partially applied transaction.
TEST_F_EX(YbAdminSnapshotScheduleTest, ConsistentTxnRestore, YbAdminSnapshotConsistentRestoreTest) {
  constexpr int kBatchSize = 10;
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery("CREATE TABLE test_table (k1 INT PRIMARY KEY)"
                              "WITH transactions = { 'enabled' : true }"));
  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&stop = thread_holder.stop_flag(), &conn] {
    const int kConcurrency = 10;
    std::string expr = "BEGIN TRANSACTION ";
    for (ATTRIBUTE_UNUSED int i : Range(kBatchSize)) {
      expr += "INSERT INTO test_table (k1) VALUES (?); ";
    }
    expr += "END TRANSACTION;";
    auto prepared = ASSERT_RESULT(conn.Prepare(expr));
    int base = 0;
    std::vector<CassandraFuture> futures;
    while (!stop.load(std::memory_order_acquire)) {
      auto filter = [](CassandraFuture& future) {
        if (!future.Ready()) {
          return false;
        }
        auto status = future.Wait();
        if (!status.ok() && !status.IsTimedOut()) {
          EXPECT_OK(status);
        }
        return true;
      };
      ASSERT_NO_FATALS(EraseIf(filter, &futures));
      if (futures.size() < kConcurrency) {
        auto stmt = prepared.Bind();
        for (int i : Range(kBatchSize)) {
          stmt.Bind(i, base + i);
        }
        base += kBatchSize;
        futures.push_back(conn.ExecuteGetFuture(stmt));
      }
    }
  });

  std::this_thread::sleep_for(250ms);

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  std::this_thread::sleep_for(250ms);

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  thread_holder.WaitAndStop(250ms);

  std::vector<int> keys;
  for (;;) {
    keys.clear();
    auto result = conn.ExecuteWithResult("SELECT * FROM test_table");
    if (!result.ok()) {
      LOG(WARNING) << "Select failed: " << result.status();
      continue;
    }

    auto iter = result->CreateIterator();
    while (iter.Next()) {
      auto row = iter.Row();
      int key = row.Value(0).As<int32_t>();
      keys.push_back(key);
    }
    break;
  }

  std::sort(keys.begin(), keys.end());
  // Check that we have whole batches only.
  // Actually this check is little bit relaxed, but it is enough to catch the bug.
  for (size_t i : Range(keys.size())) {
    ASSERT_EQ(keys[i] % kBatchSize, i % kBatchSize)
        << "i: " << i << ", key: " << keys[i] << ", batch: "
        << AsString(RangeOfSize<size_t>((i / kBatchSize - 1) * kBatchSize, kBatchSize * 3)[keys]);
  }
}

// Tests that DDLs are blocked during restore.
TEST_F_EX(YbAdminSnapshotScheduleTest, DDLsDuringRestore, YbAdminSnapshotConsistentRestoreTest) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery("CREATE TABLE test_table (k1 INT PRIMARY KEY)"));
  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table (k1) VALUES (1)"));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Created table test_table";

  // Drop the table.
  ASSERT_OK(conn.ExecuteQuery("DROP TABLE test_table"));
  LOG(INFO) << "Dropped table test_table";

  // Introduce a delay between catalog patching and loading into memory.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_sys_catalog_reload_secs", "4"));

  // Now start restore.
  auto restoration_id = ASSERT_RESULT(StartRestoreSnapshotSchedule(schedule_id, time));
  LOG(INFO) << "Restored sys catalog metadata";

  // Issue DDLs in-between.
  ASSERT_OK(conn.ExecuteQuery("CREATE TABLE test_table2 (k1 INT PRIMARY KEY)"));
  ASSERT_OK(conn.ExecuteQuery("INSERT INTO test_table2 (k1) VALUES (1)"));
  LOG(INFO) << "Created table test_table2";

  ASSERT_OK(WaitRestorationDone(restoration_id, 40s));

  // Validate data.
  auto out = ASSERT_RESULT(conn.ExecuteAndRenderToString("SELECT * from test_table"));
  LOG(INFO) << "test_table entry: " << out;
  ASSERT_EQ(out, "1");

  out = ASSERT_RESULT(conn.ExecuteAndRenderToString("SELECT * from test_table2"));
  LOG(INFO) << "test_table2 entry: " << out;
  ASSERT_EQ(out, "1");
}

class YbAdminSnapshotConsistentRestoreFailoverTest : public YbAdminSnapshotScheduleTest {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    return { "--TEST_skip_sending_restore_finished=true" };
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, ConsistentRestoreFailover,
          YbAdminSnapshotConsistentRestoreFailoverTest) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteQuery("CREATE TABLE test_table (k1 INT PRIMARY KEY)"));
  auto expr = "INSERT INTO test_table (k1) VALUES ($0)";
  ASSERT_OK(conn.ExecuteQueryFormat(expr, 1));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  ASSERT_OK(conn.ExecuteQueryFormat(expr, 2));

  auto restoration_id = ASSERT_RESULT(StartRestoreSnapshotSchedule(schedule_id, time));

  ASSERT_OK(WaitRestorationDone(restoration_id, 40s));

  for (auto* master : cluster_->master_daemons()) {
    master->Shutdown();
    ASSERT_OK(master->Restart());
  }

  ASSERT_OK(conn.ExecuteQueryFormat(expr, 3));

  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString("SELECT * FROM test_table"));
  ASSERT_EQ(rows, "1;3");
}

TEST_F(YbAdminSnapshotScheduleTest, DropKeyspaceAndSchedule) {
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, kInterval + 1s));
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

class YbAdminRestoreAfterSplitTest : public YbAdminSnapshotScheduleTest {
  std::vector<std::string> ExtraMasterFlags() override {
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
            "--snapshot_coordinator_poll_interval_ms=500",
            "--enable_automatic_tablet_splitting=false",
            "--enable_transactional_ddl_gc=false",
            "--vmodule=restore_sys_catalog_state=3",
            "--leader_lease_duration_ms=6000",
            "--leader_failure_max_missed_heartbeat_periods=12" };
  }

  std::vector<std::string> ExtraTSFlags() override {
    return { "--cleanup_split_tablets_interval_sec=1",
             "--leader_lease_duration_ms=6000",
             "--leader_failure_max_missed_heartbeat_periods=12",
             "--retryable_request_timeout_secs=5" };
  }

 public:
  Status InsertBatch(CassandraSession* conn, int start, int end) {
    CassandraBatch batch(CassBatchType::CASS_BATCH_TYPE_LOGGED);
    std::string query = Format(
        "INSERT INTO $0 (key, value) VALUES (?, ?);", client::kTableName.table_name());
    auto prepared = VERIFY_RESULT(conn->Prepare(query));
    for (int i = start; i <= end; i++) {
      auto statement = prepared.Bind();
      statement.Bind(0, i);
      statement.Bind(1, Format("before$0", i));
      batch.Add(&statement);
    }
    RETURN_NOT_OK(conn->ExecuteBatch(batch));
    LOG(INFO) << "Inserted " << end-start+1 << " entries";
    return Status::OK();
  }

  Result<int> CreateTableAndInsertData(CassandraSession* conn, int num_rows) {
    RETURN_NOT_OK(conn->ExecuteQueryFormat(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) "
        "WITH tablets = 1 AND transactions = { 'enabled' : true }",
        client::kTableName.table_name()));

    // Insert enough data suitable for splitting.
    RETURN_NOT_OK(InsertBatch(conn, 0, num_rows - 1));
    return num_rows;
  }

  void SetRf1Flags() {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;
  }

  Result<int> GetRowCount(CassandraSession* conn) {
    LOG(INFO) << "Reading rows";
    auto select_query = Format("SELECT count(*) FROM $0", client::kTableName.table_name());
    auto rows = VERIFY_RESULT(conn->ExecuteAndRenderToString(select_query));
    LOG(INFO) << "Found #rows " << rows;
    return stoi(rows);
  }
};

class YbAdminRestoreDuringSplit : public YbAdminRestoreAfterSplitTest {
 public:
  void SetDelayFlag(std::string delay_flag_name, bool set_on_master) {
    delay_flag_name_ = delay_flag_name;
    set_on_master_ = set_on_master;
  }

  Status RunTest(size_t expected_num_restored_tablets) {
    const int kNumRows = 10000;

    // Create exactly one tserver so that we only have to invalidate one cache.
    SetRf1Flags();

    auto schedule_id = VERIFY_RESULT(PrepareCql());

    auto conn = VERIFY_RESULT(CqlConnect(client::kTableName.namespace_name()));

    // Insert enough data to cause splitting.
    int i = VERIFY_RESULT(CreateTableAndInsertData(&conn, kNumRows));

    // Set pause split flag.
    if (set_on_master_) {
      RETURN_NOT_OK(cluster_->SetFlagOnMasters(delay_flag_name_, "true"));
    } else {
      RETURN_NOT_OK(cluster_->SetFlagOnTServers(delay_flag_name_, "true"));
    }

    RETURN_NOT_OK(test_admin_client_->SplitTablet(client::kTableName.namespace_name(),
                                                  client::kTableName.table_name()));

    // Sleep for a couple of seconds to ensure that we are in the phase
    // where operation is paused.
    SleepFor(MonoDelta::FromSeconds(2));

    // Capture restore time at a paused split.
    Timestamp time(VERIFY_RESULT(WallClock()->Now()).time_point);
    LOG(INFO) << "Restore time " << time.ToHumanReadableTime();

    // Let the split complete.
    if (set_on_master_) {
      RETURN_NOT_OK(cluster_->SetFlagOnMasters(delay_flag_name_, "false"));
    } else {
      RETURN_NOT_OK(cluster_->SetFlagOnTServers(delay_flag_name_, "false"));
    }

    // Inserting another row. This row should be absent after restoration.
    RETURN_NOT_OK(conn.ExecuteQueryFormat(
        "INSERT INTO $0 (key, value) VALUES ($1, 'after')",
        client::kTableName.table_name(), i));

    std::this_thread::sleep_for(kCleanupSplitTabletsInterval * 5);

    // Read data so that the partitions in the cache get updated to the
    // post-split values.
    int rows = VERIFY_RESULT(GetRowCount(&conn));
    if (rows != kNumRows + 1) {
      return STATUS_FORMAT(IllegalState, "Expected $0 rows, got $1 rows", kNumRows + 1, rows);
    }

    // There should be 2 tablets since we split 1 to 2.
    auto tablet_count = VERIFY_RESULT(GetTabletCount(test_admin_client_.get()));
    if (tablet_count != 2) {
      return STATUS_FORMAT(IllegalState, "Expected $0 tablets, got $1 tablets", 2, tablet_count);
    }

    // Perform a restoration.
    LOG(INFO) << "Restoring to time " << time.ToHumanReadableTime();
    RETURN_NOT_OK(RestoreSnapshotSchedule(schedule_id, time));

    // Verification stage.
    rows = VERIFY_RESULT(GetRowCount(&conn));
    if (rows != kNumRows) {
      return STATUS_FORMAT(IllegalState, "Expected $0 rows, got $1 rows", kNumRows, rows);
    }

    // Verify tablet count matches expectation.
    tablet_count = VERIFY_RESULT(GetTabletCount(test_admin_client_.get()));
    if (tablet_count != expected_num_restored_tablets) {
      return STATUS_FORMAT(IllegalState, "Expected $0 tablets, got $1 tablets",
                           expected_num_restored_tablets, tablet_count);
    }

    // Further inserts to the table should succeed.
    RETURN_NOT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

    rows = VERIFY_RESULT(GetRowCount(&conn));
    if (rows != kNumRows + 5) {
      return STATUS_FORMAT(IllegalState, "Expected $0 rows, got $1 rows", kNumRows + 5, rows);
    }

    return Status::OK();
  }
 private:
  std::string delay_flag_name_;
  bool set_on_master_;
};

// Restore to a time just before split key is fetched by the master.
TEST_F(YbAdminRestoreDuringSplit, RestoreBeforeGetSplitKey) {
  SetDelayFlag("TEST_pause_tserver_get_split_key", /* set on master */ false);
  ASSERT_OK(RunTest(1 /* expected num tablets after restore */));
}

// Restore to a time after one of the child tablets is registered by the master.
TEST_F(YbAdminRestoreDuringSplit, RestoreAfterOneChildRegistered) {
  SetDelayFlag("TEST_pause_split_child_registration", /* set on master */ true);
  ASSERT_OK(RunTest(1 /* expected num tablets after restore */));
}

// Restore to a time after both the child tablets are registered by the master but
// before the SPLIT_OP is applied.
TEST_F(YbAdminRestoreDuringSplit, RestoreBeforeSplitOpIsApplied) {
  SetDelayFlag("TEST_pause_apply_tablet_split", /* set on master */ false);
  ASSERT_OK(RunTest(2 /* expected num tablets after restore */));
}

// Restore to a time after both the child tablets are running but the parent isn't
// hidden yet.
TEST_F(YbAdminRestoreDuringSplit, RestoreBeforeParentHidden) {
  const int kNumRows = 10000;

  // Create exactly one tserver so that we only have to invalidate one cache.
  SetRf1Flags();

  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Insert enough data to cause splitting.
  ASSERT_RESULT(CreateTableAndInsertData(&conn, kNumRows));

  // Set flag to disable hiding parent.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_deleting_split_tablets", "true"));

  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      client::kTableName.namespace_name(), client::kTableName.table_name(),
      /* wait_for_parent_deletion */ false));

  // Restore time is after split completes but parent does not hide.
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Restore time " << time.ToHumanReadableTime();

  // Inserting rows. These should be absent after restoration.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

  // Read data so that the partitions in the cache get updated to the
  // post-split values.
  int rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 5);

  // There should be 2 tablets since we split 1 to 2.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 2);

  // Set flag to enable hiding parent.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_deleting_split_tablets", "false"));

  // Perform a restoration.
  LOG(INFO) << "Restoring to time " << time.ToHumanReadableTime();
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Verification stage.
  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows);

  // There should be 2 tablets.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 2);

  // Further inserts to the table should succeed.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 5);
}

TEST_F_EX(YbAdminSnapshotScheduleTest, RestoreAfterSplit, YbAdminRestoreAfterSplitTest) {
  const int kNumRows = 10000;

  // Create exactly one tserver so that we only have to invalidate one cache.
  SetRf1Flags();

  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Insert enough data to cause splitting.
  int i = ASSERT_RESULT(CreateTableAndInsertData(&conn, kNumRows));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // This row should be absent after restoration.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "INSERT INTO $0 (key, value) VALUES ($1, 'after')", client::kTableName.table_name(), i));

  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      client::kTableName.namespace_name(), client::kTableName.table_name(),
      /* wait_for_parent_deletion */ true));

  // Read data so that the partitions in the cache get updated to the
  // post-split values.
  int rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 1);

  // There should be 2 tablets since we split 1 to 2.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 2);

  // Perform a restoration.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows);

  // There should be 1 tablet.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 1);

  // Further inserts to the table should succeed.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 5);
}

TEST_F_EX(YbAdminSnapshotScheduleTest, VerifyRestoreWithDeletedTablets,
          YbAdminRestoreAfterSplitTest) {
  const int kNumRows = 10000;

  // Create exactly one tserver so that we only have to invalidate one cache.
  SetRf1Flags();

  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Insert enough data to cause to splitting.
  ASSERT_RESULT(CreateTableAndInsertData(&conn, kNumRows));

  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      client::kTableName.namespace_name(), client::kTableName.table_name(),
      /* wait_for_parent_deletion */ true));

  // Read data so that the partitions in the cache get updated to the
  // post-split values.
  int rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows);

  // There should be 2 tablets since we split 1 to 2.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 2);

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // These rows will be undone post restore.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

  // Perform a restoration.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Reading rows after restoration";
  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows);

  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 2);

  // Further inserts to the table should succeed.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 5);
}

TEST_F(YbAdminRestoreAfterSplitTest, TestRestoreUncompactedChildTabletAndSplit) {
  SetRf1Flags();
  auto schedule_id = ASSERT_RESULT(PrepareCql());
  // Skip the post split compaction so we can force the snapshot used to restore from to include the
  // pre-compaction data of the child tablets.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "true"));
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));
  ASSERT_RESULT(CreateTableAndInsertData(&conn, 500));
  auto table_name = ASSERT_RESULT(test_admin_client_->GetTableName(
      client::kTableName.namespace_name(), client::kTableName.table_name()));
  auto tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(table_name));
  ASSERT_EQ(tablets.size(), 1);
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      table_name, /* wait_for_parent_deletion */ false, tablets[0].tablet_id()));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(table_name));
  ASSERT_EQ(tablets.size(), 2);
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  // Create a snapshot to ensure the snapshot we restore to has the child tablets uncompacted.
  ASSERT_RESULT(test_admin_client_->CreateSnapshotAndWait(
      ASSERT_RESULT(SnapshotScheduleId::FromString(schedule_id))));
  // Read data so that the partitions in the cache get updated to the
  // post-split values.
  ASSERT_EQ(ASSERT_RESULT(GetRowCount(&conn)), 500);
  // Delete a row so the restore has some work to do.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "DELETE FROM $0.$1 where key = $2", client::kTableName.namespace_name(),
      client::kTableName.table_name(), "1"));
  ASSERT_EQ(499, ASSERT_RESULT(GetRowCount(&conn)));
  // Force a compaction and wait for the child tablets to be fully compacted.
  ASSERT_OK(CompactTablets(cluster_.get(), 300s * kTimeMultiplier));
  for (const auto& tablet : tablets) {
    const auto leader_idx = ASSERT_RESULT(cluster_->GetTabletLeaderIndex(tablet.tablet_id()));
    ASSERT_OK(test_admin_client_->WaitForTabletPostSplitCompacted(leader_idx, tablet.tablet_id()));
  }
  // Reset the flag so we compact the children after the restore.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_skip_post_split_compaction", "false"));
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
  ASSERT_EQ(500, ASSERT_RESULT(GetRowCount(&conn)));
  tablets = ASSERT_RESULT(test_admin_client_->GetTabletLocations(table_name));
  ASSERT_EQ(tablets.size(), 2);
  for (const auto& tablet : tablets) {
    const auto leader_idx = ASSERT_RESULT(cluster_->GetTabletLeaderIndex(tablet.tablet_id()));
    ASSERT_OK(test_admin_client_->WaitForTabletPostSplitCompacted(leader_idx, tablet.tablet_id()));
  }
  // Try to split the first tablet to sanity check it was compacted after the restore.
  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      table_name, /* wait_for_parent_deletion */ false, tablets[0].tablet_id()));
  // SplitTablet does most work asynchronously, so sanity check the tablet count here.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 3);
}

TEST_F(YbAdminRestoreDuringSplit, VerifyParentNotHiddenPostRestore) {
  const int kNumRows = 10000;

  // Create exactly one tserver so that we only have to invalidate one cache.
  SetRf1Flags();

  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Insert enough data to cause splitting.
  ASSERT_RESULT(CreateTableAndInsertData(&conn, kNumRows));

  // Restore should unhide the parent and hide both the children.
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Restore time " << time.ToHumanReadableTime();

  ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      client::kTableName.namespace_name(), client::kTableName.table_name(),
      /* wait_for_parent_deletion */ true));
  LOG(INFO) << "Done splitting";

  // Read data so that the partitions in the cache get updated to the
  // post-split values.
  int rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows);

  // These should be absent after restoration.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));
  LOG(INFO) << "Inserted " << kNumRows + 5 << " entries";

  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 5);

  // There should be 2 tablets since we split 1 to 2.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 2);

  // Set flag to delay tablet split metadata restore.
  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_delay_tablet_split_metadata_restore_secs", "2"));

  // Perform a restoration.
  LOG(INFO) << "Restoring to time " << time.ToHumanReadableTime();
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Verification stage.
  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows);

  // There should be 1 tablet.
  ASSERT_EQ(ASSERT_RESULT(GetTabletCount(test_admin_client_.get())), 1);

  // Further inserts to the table should succeed.
  ASSERT_OK(InsertBatch(&conn, kNumRows, kNumRows + 4));

  rows = ASSERT_RESULT(GetRowCount(&conn));
  ASSERT_EQ(rows, kNumRows + 5);
}

class YbAdminSnapshotScheduleAutoSplitting : public YbAdminSnapshotScheduleTestWithYsql {
  std::vector<std::string> ExtraMasterFlags() override {
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
            "--snapshot_coordinator_poll_interval_ms=500",
            "--enable_automatic_tablet_splitting=true",
            "--tablet_split_low_phase_size_threshold_bytes=13421",
            "--tablet_split_low_phase_shard_count_per_node=16"
    };
  }
  std::vector<std::string> ExtraTSFlags() override {
    return { "--yb_num_shards_per_tserver=1" };
  }

 public:

  Status InsertDataForSplitting(pgwrapper::PGConn* conn, int num_rows = 5000) {
    std::string value =
      "Engineering has existed since ancient times, when humans devised inventions"
      " such as the wedge, lever, wheel and pulley, etc. "
      "The term engineering is derived from the word engineer, which itself dates"
      " back to the 14th century when an engineer referred to a constructor of"
      " military engines. "
      "In this context, now obsolete, an engine referred to a military machine i.e."
      ", a mechanical contraption used in war Notable examples of the obsolete usage"
      " which have survived to the present day are military engineering corps. "
      "The word engine itself is of even older origin, ultimately deriving from the Latin"
      " ingenium meaning innate quality, especially mental power, hence a clever invention. "
      "Later, as the design of civilian structures, such as bridges and buildings,"
      " matured as a technical discipline, the term civil engineering entered the"
      " lexicon as a way to distinguish between those specializing in the construction of"
      " such non-military projects and those involved in the discipline of military"
      " engineering. ";
    std::string query = "INSERT INTO $0 SELECT generate_series($1, $2), '$3'";
    // Batch insert using generate_series.
    RETURN_NOT_OK(conn->ExecuteFormat(
        query, client::kTableName.table_name(), 1, num_rows, value));
    LOG(INFO) << "Inserted " << num_rows << " data";
    return Status::OK();
  }

  Result<bool> VerifyData(pgwrapper::PGConn* conn, size_t prev_tablets_count) {
    auto select_query = Format(
        "SELECT count(*) FROM $0", client::kTableName.table_name());
    auto rows = VERIFY_RESULT(conn->FetchRowAsString(select_query));
    LOG(INFO) << "Found #rows " << rows;
    if(rows != "1") {
      return false;
    }

    select_query = Format(
        "SELECT value FROM $0 WHERE key=0", client::kTableName.table_name());
    auto val = VERIFY_RESULT(conn->FetchValue<std::string>(select_query));
    LOG(INFO) << "key = 0, Value = " << val;
    if(val != "after") {
      return false;
    }

    return true;
  }
};

TEST_F_EX(
    YbAdminSnapshotScheduleTest, SplitDisabledDuringRestore,
    YbAdminSnapshotScheduleAutoSplitting) {
  auto schedule_id = ASSERT_RESULT(PreparePg());

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) "
      "SPLIT INTO 3 tablets",
      client::kTableName.table_name()));

  // Only this row should be present after restoration.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 (key, value) VALUES ($1, 'after')", client::kTableName.table_name(), 0));

  auto prev_tablets_count = ASSERT_RESULT(GetTabletCount(test_admin_client_.get()));
  LOG(INFO) << prev_tablets_count << " tablets present before restore";

  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  // Insert enough data conducive to splitting.
  ASSERT_OK(InsertDataForSplitting(&conn));
  LOG(INFO) << "Inserted 5000 rows";

  // Perform a restoration.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  LOG(INFO) << "Reading rows after restoration";
  ASSERT_TRUE(ASSERT_RESULT(VerifyData(&conn, prev_tablets_count)));

  // Note down the time and perform a restore again.
  Timestamp time2(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Insert enough data conducive to splitting.
  ASSERT_OK(InsertDataForSplitting(&conn));
  LOG(INFO) << "Inserted 5000 rows again";

  // Perform a restoration.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time2));

  LOG(INFO) << "Reading rows after restoration the second time";
  ASSERT_TRUE(ASSERT_RESULT(VerifyData(&conn, prev_tablets_count)));
}

TEST_F_EX(
    YbAdminSnapshotScheduleTest, DeadlockWithSplitting,
    YbAdminSnapshotScheduleAutoSplitting) {
  // Create an aggressive snapshot schedule that takes a snapshot every second.
  auto schedule_id = ASSERT_RESULT(
      PreparePg(YsqlColocationConfig::kNotColocated, 1s /* interval */, 6s /* retention */));

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) "
      "SPLIT INTO 3 tablets",
      client::kTableName.table_name()));

  // Only this row should be present after restoration.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 (key, value) VALUES ($1, 'after')", client::kTableName.table_name(), 0));

  auto prev_tablets_count = ASSERT_RESULT(GetTabletCount(test_admin_client_.get()));
  LOG(INFO) << prev_tablets_count << " tablets present before restore";

  // Splitting should be delayed for 2 secs with the lock held. At least one snapshot
  // should take place since the frequency is 1 every second.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_delay_split_registration_secs", "2"));

  // Insert enough data conducive to splitting.
  ASSERT_OK(InsertDataForSplitting(&conn, 15000));
  LOG(INFO) << "Inserted 15000 rows";

  // Now split and wait for them. If deadlocked this should time out.
  ASSERT_OK(WaitFor(
      [this, prev_tablets_count]() -> Result<bool> {
        auto tablets_count = VERIFY_RESULT(GetTabletCount(test_admin_client_.get()));
        LOG(INFO) << tablets_count << " tablets thus far after inserting 5000 rows";
        return tablets_count >= prev_tablets_count + 5;
      },
      120s, "Wait for tablets to be split"));
}

TEST_F_EX(YbAdminSnapshotScheduleTest, CacheRefreshOnNewConnection,
          YbAdminSnapshotScheduleAutoSplitting) {
  // Setup an RF1 so that we are only dealing with one tserver and its cache.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;

  auto schedule_id = ASSERT_RESULT(PreparePg());

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) "
      "SPLIT INTO 1 tablets",
      client::kTableName.table_name()));

  // Only this row should be present after restoration.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO $0 (key, value) VALUES ($1, 'after')",
      client::kTableName.table_name(), 0));

  auto prev_tablets_count = ASSERT_RESULT(GetTabletCount(test_admin_client_.get()));
  LOG(INFO) << prev_tablets_count << " tablets present before restore";

  Timestamp time = ASSERT_RESULT(GetCurrentTime());

  // Insert enough data conducive to splitting.
  ASSERT_OK(InsertDataForSplitting(&conn));
  LOG(INFO) << "Inserted 5000 rows";

  // Wait for at least one round of splitting.
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        auto tablets_count = VERIFY_RESULT(GetTabletCount(test_admin_client_.get()));
        LOG(INFO) << tablets_count << " tablets thus far after inserting 5000 rows";
        return tablets_count >= 2;
      },
      120s, "Wait for tablets to be split"));

  // Read data so that cache gets updated to latest partitions.
  auto select_query = Format(
       "SELECT count(*) FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.FetchRowAsString(select_query));
  LOG(INFO) << "Found #rows " << rows;
  ASSERT_EQ(rows, "5001");

  // Perform a restoration.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Give some time for catalog version to be propagated.
  // Two times the HB frequency should be enough.
  SleepFor(MonoDelta::FromSeconds(2));

  // Read data using a new connection.
  auto new_conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  LOG(INFO) << "Reading rows after restoration";
  ASSERT_TRUE(ASSERT_RESULT(VerifyData(&new_conn, prev_tablets_count)));
}

class YbAdminSnapshotScheduleTestWithYsqlAndManualSplitting
    : public YbAdminSnapshotScheduleAutoSplitting {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
            "--snapshot_coordinator_poll_interval_ms=500",
            "--enable_automatic_tablet_splitting=false"
    };
  }
  std::vector<std::string> ExtraTSFlags() override {
    return { "--yb_num_shards_per_tserver=1",
             "--cleanup_split_tablets_interval_sec=1"
    };
  }

  void TestIOWithSnapshotScheduleAndSplit(bool test_write, bool perform_restore) {
    // Setup an RF1 so that we are only dealing with one tserver and its cache.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_tablet_servers) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_num_replicas) = 1;

    auto schedule_id = ASSERT_RESULT(PreparePg());

    auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) "
        "SPLIT INTO 1 tablets",
        client::kTableName.table_name()));

    // Note down time to restore.
    Timestamp time = ASSERT_RESULT(GetCurrentTime());

    // Insert enough data conducive to splitting.
    ASSERT_OK(InsertDataForSplitting(&conn));
    LOG(INFO) << "Inserted 5000 rows";

    // Read data so that partitions get updated.
    auto select_query = Format(
        "SELECT count(*) FROM $0", client::kTableName.table_name());
    auto rows = ASSERT_RESULT(conn.FetchRowAsString(select_query));
    LOG(INFO) << "Before split found #rows " << rows;
    ASSERT_EQ(rows, "5000");

    // Trigger a manual split.
    ASSERT_OK(test_admin_client_->SplitTabletAndWait(
      client::kTableName.namespace_name(), client::kTableName.table_name(),
      /* wait_for_parent_deletion */ true));

    // Verify that cache gets refreshed post split.
    if (test_write) {
      auto insert_query = Format(
          "INSERT INTO $0 (key, value) VALUES (5001, 'after')", client::kTableName.table_name());
      ASSERT_OK(conn.Execute(insert_query));
    } else {
      select_query = Format(
          "SELECT count(*) FROM $0", client::kTableName.table_name());
      rows = ASSERT_RESULT(conn.FetchRowAsString(select_query));
      LOG(INFO) << "After split found #rows " << rows;
      ASSERT_EQ(rows, "5000");
    }

    if (perform_restore) {
      ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
      // TODO(Sanket): Remove this sleep and query the rpc
      // once GH#14601 is fixed.
      // Wait for 2 HBs for catalog version to be propagated.
      SleepFor(MonoDelta::FromSeconds(2));
      auto new_conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));
      if (test_write) {
        auto insert_query = Format(
            "INSERT INTO $0 (key, value) VALUES (1, 'after')", client::kTableName.table_name());
        ASSERT_OK(new_conn.Execute(insert_query));
      } else {
        select_query = Format(
            "SELECT count(*) FROM $0", client::kTableName.table_name());
        rows = ASSERT_RESULT(new_conn.FetchRowAsString(select_query));
        LOG(INFO) << "After restore found #rows " << rows;
        ASSERT_EQ(rows, "0");
      }
    }
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, ReadsWithSnapshotScheduleAndSplit,
          YbAdminSnapshotScheduleTestWithYsqlAndManualSplitting) {
  TestIOWithSnapshotScheduleAndSplit(/* test_write = */ false, /* perform_restore = */ false);
}

TEST_F_EX(YbAdminSnapshotScheduleTest, WritesWithSnapshotScheduleAndSplit,
          YbAdminSnapshotScheduleTestWithYsqlAndManualSplitting) {
  TestIOWithSnapshotScheduleAndSplit(/* test_write = */ true, /* perform_restore = */ false);
}

TEST_F_EX(YbAdminSnapshotScheduleTest, WritesAfterRestoreWithSplit,
          YbAdminSnapshotScheduleTestWithYsqlAndManualSplitting) {
  TestIOWithSnapshotScheduleAndSplit(/* test_write = */ true, /* perform_restore = */ true);
}

TEST_F_EX(YbAdminSnapshotScheduleTest, ReadsAfterRestoreWithSplit,
          YbAdminSnapshotScheduleTestWithYsqlAndManualSplitting) {
  TestIOWithSnapshotScheduleAndSplit(/* test_write = */ false, /* perform_restore = */ true);
}

TEST_F(YbAdminSnapshotScheduleTest, RestoreToBeforePreviousRestoreAt) {
  const auto retention = kInterval * 5 * kTimeMultiplier;
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, retention));
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  std::this_thread::sleep_for(FLAGS_max_clock_skew_usec * 1us);
  Timestamp time1(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time1: " << time1;

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));

  auto insert_pattern = Format(
      "INSERT INTO $0 (key, value) VALUES (1, '$$0')", client::kTableName.table_name());
  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "before"));

  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "1,before");

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time1));
  std::this_thread::sleep_for(3s * kTimeMultiplier);

  auto s = conn.ExecuteAndRenderToString(select_expr);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.status().message().ToBuffer(), "Object Not Found");

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));
  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "new"));

  // Restore to time0 which is 1 us before time1.
  Timestamp time0(time1.value() - 1);
  LOG(INFO) << "Time0: " << time0;
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time0));
  std::this_thread::sleep_for(3s * kTimeMultiplier);
  ASSERT_OK(WaitTabletsCleaned(CoarseMonoClock::now() + retention + kInterval));
}

TEST_F(YbAdminSnapshotScheduleTest, RestoreToAfterPreviousCompleteAt) {
  const auto retention = kInterval * 5 * kTimeMultiplier;
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, retention));
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  std::this_thread::sleep_for(FLAGS_max_clock_skew_usec * 1us);
  Timestamp time1(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time1: " << time1;

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));

  auto insert_pattern = Format(
      "INSERT INTO $0 (key, value) VALUES (1, '$$0')", client::kTableName.table_name());
  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "before"));

  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "1,before");

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time1));
  std::this_thread::sleep_for(3s * kTimeMultiplier);

  auto s = conn.ExecuteAndRenderToString(select_expr);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.status().message().ToBuffer(), "Object Not Found");

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));

  Timestamp time2(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time2: " << time2;

  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "new"));

  rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "1,new");

  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time2));
  std::this_thread::sleep_for(3s * kTimeMultiplier);

  rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "");
}

TEST_F(YbAdminSnapshotScheduleTest, DisallowForwardRestore) {
  const auto retention = kInterval * 5 * kTimeMultiplier;
  auto schedule_id = ASSERT_RESULT(PrepareCql(kInterval, retention));
  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  std::this_thread::sleep_for(FLAGS_max_clock_skew_usec * 1us);
  Timestamp time1(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time1: " << time1;

  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
      client::kTableName.table_name()));

  auto insert_pattern = Format(
      "INSERT INTO $0 (key, value) VALUES (1, '$$0')", client::kTableName.table_name());
  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "before"));

  Timestamp time2(ASSERT_RESULT(WallClock()->Now()).time_point);
  LOG(INFO) << "Time2: " << time2;

  ASSERT_OK(conn.ExecuteQueryFormat(insert_pattern, "after"));

  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  ASSERT_EQ(rows, "1,after");

  LOG(INFO) << "Start restoring to Time1";
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time1));
  std::this_thread::sleep_for(3s * kTimeMultiplier);

  auto s = conn.ExecuteAndRenderToString(select_expr);
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.status().message().ToBuffer(), "Object Not Found");

  Status s2 = RestoreSnapshotSchedule(schedule_id, time2);
  ASSERT_NOK(s2);
  ASSERT_STR_CONTAINS(
      s2.message().ToBuffer(), "Cannot perform a forward restore.");
}

TEST_F(YbAdminSnapshotScheduleTest, CatalogLoadRace) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Change the snapshot throttling flags.
  ASSERT_OK(cluster_->SetFlagOnMasters("max_concurrent_snapshot_rpcs", "-1"));
  ASSERT_OK(cluster_->SetFlagOnMasters("max_concurrent_snapshot_rpcs_per_tserver", "1"));
  ASSERT_OK(cluster_->SetFlagOnMasters("schedule_snapshot_rpcs_out_of_band", "true"));
  // Delay loading of cluster config by 2 secs (i.e. 4 cycles of snapshot coordinator).
  // This ensures that the snapshot coordinator accesses an empty cluster config at least once
  // and thus triggers the codepath where the case is handled
  // and a default value is used for throttling.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_slow_cluster_config_load_secs", "2"));

  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);
  // Restore to trigger loading cluster config.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
}

class YbAdminSnapshotScheduleFlushTest : public YbAdminSnapshotScheduleTest {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    // To speed up tests.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500",
             "--enable_automatic_tablet_splitting=true",
             "--enable_transactional_ddl_gc=false",
             "--flush_rocksdb_on_shutdown=false",
             "--vmodule=tablet_bootstrap=3" };
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, TestSnapshotBootstrap, YbAdminSnapshotScheduleFlushTest) {
  LOG(INFO) << "Create cluster";
  CreateCluster(kClusterName, ExtraTSFlags(), ExtraMasterFlags());

  // Disable modifying flushed frontier when snapshot is created.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_modify_flushed_frontier_snapshot_op", "false"));

  {
    // Create a database and a table.
    auto conn = ASSERT_RESULT(CqlConnect());
    ASSERT_OK(conn.ExecuteQuery(
        Format("CREATE KEYSPACE IF NOT EXISTS $0", client::kTableName.namespace_name())));

    conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

    ASSERT_OK(conn.ExecuteQueryFormat(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 1",
        client::kTableName.table_name()));
    LOG(INFO) << "Created Keyspace and table";

    // Create a CREATE_ON_MASTER op in WALs without flushing frontier.
    ASSERT_OK(CallAdmin(
        "create_keyspace_snapshot", Format("ycql.$0", client::kTableName.namespace_name())));
    SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));
    LOG(INFO) << "Created snapshot on keyspace";

    // Enable modifying flushed frontier when snapshot is replayed.
    LOG(INFO) << "Resetting test flag to modify flushed frontier";
  }

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

class YbAdminSnapshotScheduleFailoverTests : public YbAdminSnapshotScheduleTest {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    // Slow down restoration rpcs.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=5000",
             "--enable_automatic_tablet_splitting=true",
             "--max_concurrent_restoration_rpcs=1",
             "--schedule_restoration_rpcs_out_of_band=false",
             "--vmodule=tablet_bootstrap=4",
             "--TEST_fatal_on_snapshot_verify=false",
             Format("--TEST_play_pending_uncommitted_entries=$0",
                    replay_uncommitted_ ? "true" : "false")};
  }

  Status ClusterRestartTest(bool replay_uncommitted);

  void SetReplayUncommitted(bool replay_uncommitted) {
    replay_uncommitted_ = replay_uncommitted;
  }

 private:
  bool replay_uncommitted_ = false;
};

Status YbAdminSnapshotScheduleFailoverTests::ClusterRestartTest(bool replay_uncommitted) {
  SetReplayUncommitted(replay_uncommitted);
  auto schedule_id = VERIFY_RESULT(PrepareCql());
  LOG(INFO) << "Snapshot schedule id " << schedule_id;

  std::string restoration_id;
  {
    // Don't keep the CassandraSession open during the restart. Doing so exposes a data race inside
    // the cassandra-cpp-driver library.
    auto conn = VERIFY_RESULT(CqlConnect(client::kTableName.namespace_name()));

    // Create a table with large number of tablets.
    RETURN_NOT_OK(conn.ExecuteQueryFormat(
        "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH TABLETS = 24",
        client::kTableName.table_name()));

    // Insert some data.
    RETURN_NOT_OK(conn.ExecuteQueryFormat(
        "INSERT INTO $0 (key, value) values (1, 'before')", client::kTableName.table_name()));

    LOG(INFO) << "Created Keyspace and table";

    // Record time for restoring.
    Timestamp time(VERIFY_RESULT(WallClock()->Now()).time_point);

    // Drop the table.
    RETURN_NOT_OK(conn.ExecuteQueryFormat("DROP TABLE $0", client::kTableName.table_name()));
    LOG(INFO) << "Dropped the table";

    // Now start restore to the noted time. Since the RPCs are slow, we can restart
    // the cluster in the meantime.
    RETURN_NOT_OK(
        cluster_->SetFlagOnMasters("TEST_delay_sys_catalog_restore_on_followers_secs", "2"));
    restoration_id = VERIFY_RESULT(StartRestoreSnapshotSchedule(schedule_id, time));

    // Wait for a second to flush.
    SleepFor(MonoDelta::FromSeconds(1));
  }

  LOG(INFO) << "Now restarting cluster";
  cluster_->Shutdown();
  RETURN_NOT_OK(cluster_->Restart());
  LOG(INFO) << "Cluster restarted";

  // Now speed up rpcs.
  RETURN_NOT_OK(cluster_->SetFlagOnMasters("schedule_restoration_rpcs_out_of_band", "true"));
  RETURN_NOT_OK(cluster_->SetFlagOnMasters("max_concurrent_restoration_rpcs", "9"));
  RETURN_NOT_OK(cluster_->SetFlagOnMasters("snapshot_coordinator_poll_interval_ms", "500"));

  RETURN_NOT_OK(WaitRestorationDone(restoration_id, 120s * kTimeMultiplier));

  // Validate data.
  auto conn = VERIFY_RESULT(CqlConnect(client::kTableName.namespace_name()));
  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = VERIFY_RESULT(conn.ExecuteAndRenderToString(select_expr));
  LOG(INFO) << "Data after restoration: " << rows;
  if (rows != "1,before") {
    return STATUS_FORMAT(IllegalState, "Expected 1,before, found $0", rows);
  }
  return Status::OK();
}

TEST_F(YbAdminSnapshotScheduleFailoverTests, LeaderFailoverDuringRestorationRpcs) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());
  LOG(INFO) << "Snapshot schedule id " << schedule_id;

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Create a table with large number of tablets.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH TABLETS = 24",
      client::kTableName.table_name()));

  // Insert some data.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "INSERT INTO $0 (key, value) values (1, 'before')",
      client::kTableName.table_name()));

  LOG(INFO) << "Created Keyspace and table";

  // Record time for restoring.
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Drop the table.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "DROP TABLE $0", client::kTableName.table_name()));
  LOG(INFO) << "Dropped the table";

  // Now start restore to the noted time. Since the RPCs are slow, we can failover the master
  // leader when they are in progress to see if restoration still completes.
  auto restoration_id = ASSERT_RESULT(StartRestoreSnapshotSchedule(schedule_id, time));

  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  LOG(INFO) << "Master leader changed";

  // Now speed up rpcs.
  ASSERT_OK(cluster_->SetFlagOnMasters("schedule_restoration_rpcs_out_of_band", "true"));
  ASSERT_OK(cluster_->SetFlagOnMasters("max_concurrent_restoration_rpcs", "9"));
  ASSERT_OK(cluster_->SetFlagOnMasters("snapshot_coordinator_poll_interval_ms", "500"));

  ASSERT_OK(WaitRestorationDone(restoration_id, 120s * kTimeMultiplier));

  // Validate data.
  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  LOG(INFO) << "Data after restoration: " << rows;
  ASSERT_EQ(rows, "1,before");
}

TEST_F(YbAdminSnapshotScheduleFailoverTests, ClusterRestartDuringRestore) {
  ASSERT_OK(ClusterRestartTest(false));
}

TEST_F(YbAdminSnapshotScheduleFailoverTests, ClusterRestartDuringRestoreWithReplayUncommitted) {
  ASSERT_OK(ClusterRestartTest(true));
}

TEST_F(YbAdminSnapshotScheduleFailoverTests, LeaderFailoverDuringSysCatalogRestorationPhase) {
  auto schedule_id = ASSERT_RESULT(PrepareCql());
  LOG(INFO) << "Snapshot schedule id " << schedule_id;

  auto conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Create table with large number of tablets.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH TABLETS = 24",
      client::kTableName.table_name()));

  // Insert some data.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "INSERT INTO $0 (key, value) values (1, 'before')",
      client::kTableName.table_name()));

  LOG(INFO) << "Created Keyspace and table";

  // Set the crash flag only on the leader master.
  auto* leader = cluster_->GetLeaderMaster();
  ASSERT_OK(cluster_->SetFlag(leader, "TEST_crash_during_sys_catalog_restoration", "1.0"));
  LOG(INFO) << "Crash flag set on the leader master";

  // Record time for restoring.
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Drop the table.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "DROP TABLE $0", client::kTableName.table_name()));
  LOG(INFO) << "Table dropped";

  // Now start restore to the noted time. Because of the test flag, the master leader
  // will crash during the sys catalog restoration phase. We can then validate if
  // restoration still succeeds.
  auto restoration_id_result = StartRestoreSnapshotSchedule(schedule_id, time);
  ASSERT_NOK(restoration_id_result);

  // Since we don't have the restoration id, query the new master leader for it.
  auto restoration_ids = ASSERT_RESULT(GetAllRestorationIds());
  ASSERT_EQ(restoration_ids.size(), 1);
  LOG(INFO) << "Restoration id " << restoration_ids[0];

  // Now speed up rpcs.
  auto* new_leader = cluster_->GetLeaderMaster();
  ASSERT_OK(cluster_->SetFlag(new_leader, "schedule_restoration_rpcs_out_of_band", "true"));
  ASSERT_OK(cluster_->SetFlag(new_leader, "max_concurrent_restoration_rpcs", "9"));
  ASSERT_OK(cluster_->SetFlag(new_leader, "snapshot_coordinator_poll_interval_ms", "500"));

  ASSERT_OK(WaitRestorationDone(restoration_ids[0], 120s * kTimeMultiplier));

  // Validate data.
  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  LOG(INFO) << "Rows: " << rows;
  ASSERT_EQ(rows, "1,before");

  // Restart all masters just so that the test doesn't fail during teardown().
  ASSERT_OK(RestartAllMasters(cluster_.get()));
}

TEST_F(YbAdminSnapshotScheduleFailoverTests, LeaderFailoverRestoreSnapshot) {
  LOG(INFO) << "Create cluster";
  CreateCluster(kClusterName, ExtraTSFlags(), ExtraMasterFlags());

  // Create a database and a table.
  auto conn = ASSERT_RESULT(CqlConnect());
  ASSERT_OK(conn.ExecuteQuery(Format(
      "CREATE KEYSPACE IF NOT EXISTS $0", client::kTableName.namespace_name())));

  conn = ASSERT_RESULT(CqlConnect(client::kTableName.namespace_name()));

  // Create table with large number of tablets.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "CREATE TABLE $0 (key INT PRIMARY KEY, value TEXT) WITH tablets = 24",
      client::kTableName.table_name()));

  // Insert some data.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "INSERT INTO $0 (key, value) values (1, 'before')",
      client::kTableName.table_name()));
  LOG(INFO) << "Created Keyspace and table";

  // Create a snapshot.
  auto out = ASSERT_RESULT(
      CallAdmin("create_keyspace_snapshot",
                Format("ycql.$0", client::kTableName.namespace_name())));

  vector<string> admin_result = strings::Split(out, ": ");
  std::string snapshot_id = admin_result[1].substr(0, admin_result[1].size() - 1);
  LOG(INFO) << "Snapshot id " << snapshot_id;

  // Wait for snapshot to be created.
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    std::string out = VERIFY_RESULT(CallAdmin("list_snapshots"));
    LOG(INFO) << out;
    return out.find("COMPLETE") != std::string::npos;
  }, 120s, "Wait for snapshot to be created"));

  // Update the entry.
  ASSERT_OK(conn.ExecuteQueryFormat(
      "UPDATE $0 SET value='after' where key=1",
      client::kTableName.table_name()));
  auto select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  auto rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  LOG(INFO) << "Rows after update: " << rows;
  ASSERT_EQ(rows, "1,after");

  // Restore this snapshot now.
  out = ASSERT_RESULT(CallAdmin("restore_snapshot", snapshot_id));

  // Failover the leader.
  ASSERT_OK(cluster_->StepDownMasterLeaderAndWaitForNewLeader());
  LOG(INFO) << "Failed over the master leader";

  // Now speed up rpcs.
  ASSERT_OK(cluster_->SetFlagOnMasters("schedule_restoration_rpcs_out_of_band", "true"));
  ASSERT_OK(cluster_->SetFlagOnMasters("max_concurrent_restoration_rpcs", "9"));
  ASSERT_OK(cluster_->SetFlagOnMasters("snapshot_coordinator_poll_interval_ms", "500"));

  // Wait for restoration to finish.
  ASSERT_OK(WaitFor([this]() -> Result<bool> {
    std::string out = VERIFY_RESULT(CallAdmin("list_snapshots"));
    LOG(INFO) << out;
    return out.find("RESTORED") != std::string::npos;
  }, 120s, "Wait for restoration to complete"));

  // Validate data.
  select_expr = Format("SELECT * FROM $0", client::kTableName.table_name());
  rows = ASSERT_RESULT(conn.ExecuteAndRenderToString(select_expr));
  LOG(INFO) << "Rows after restoration: " << rows;
  ASSERT_EQ(rows, "1,before");
}

class YbAdminSnapshotScheduleTestWithLB : public YbAdminSnapshotScheduleTest {
  std::vector<std::string> ExtraMasterFlags() override {
    std::vector<std::string> flags;
    flags = YbAdminSnapshotScheduleTest::ExtraMasterFlags();
    flags.push_back("--enable_load_balancing=true");

    return flags;
  }

 public:
  void WaitForLoadBalanceCompletion(yb::MonoDelta timeout) {
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      bool is_idle = VERIFY_RESULT(client_->IsLoadBalancerIdle());
      return !is_idle;
    }, timeout, "IsLoadBalancerActive"));

    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      return client_->IsLoadBalancerIdle();
    }, timeout, "IsLoadBalancerIdle"));
  }

  Result<std::vector<uint32_t>> GetTServerLoads(yb::MonoDelta timeout) {
    std::vector<uint32_t> tserver_loads;
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      auto proxy = cluster_->GetTServerProxy<tserver::TabletServerServiceProxy>(i);
      tserver::ListTabletsRequestPB req;
      tserver::ListTabletsResponsePB resp;
      rpc::RpcController controller;
      controller.set_timeout(timeout);
      RETURN_NOT_OK(proxy.ListTablets(req, &resp, &controller));
      int tablet_count = 0;
      for (const auto& tablet : resp.status_and_schema()) {
        if (tablet.tablet_status().table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
          continue;
        }
        if (tablet.tablet_status().namespace_name() == client::kTableName.namespace_name()) {
          if (tablet.tablet_status().tablet_data_state() != tablet::TABLET_DATA_TOMBSTONED) {
            ++tablet_count;
          }
        }
      }
      LOG(INFO) << "For TS " << cluster_->tablet_server(i)->id() << ", load: " << tablet_count;
      tserver_loads.push_back(tablet_count);
    }
    return tserver_loads;
  }

  void WaitForLoadToBeBalanced(yb::MonoDelta timeout) {
    ASSERT_OK(WaitFor([&]() -> Result<bool> {
      auto tserver_loads = VERIFY_RESULT(GetTServerLoads(timeout));
      return integration_tests::AreLoadsBalanced(tserver_loads);
    }, timeout, "Are loads balanced"));
  }
};

TEST_F(YbAdminSnapshotScheduleTestWithLB, TestLBHiddenTables) {
  // Create a schedule.
  auto schedule_id = ASSERT_RESULT(PrepareCql());

  // Create a table with 8 tablets.
  LOG(INFO) << "Create table " << client::kTableName.table_name() << " with 8 tablets";
  ASSERT_NO_FATALS(client::kv_table_test::CreateTable(
      client::Transactional::kTrue, 8, client_.get(), &table_));

  // Drop the table so that it becomes Hidden.
  LOG(INFO) << "Hiding table " << client::kTableName.table_name();
  ASSERT_OK(client_->DeleteTable(client::kTableName));

  // Add a tserver and wait for LB to balance the load.
  LOG(INFO) << "Adding a fourth tablet server";
  std::vector<std::string> ts_flags = ExtraTSFlags();
  ASSERT_OK(cluster_->AddTabletServer(true, ts_flags));
  ASSERT_OK(cluster_->WaitForTabletServerCount(4, 30s));

  // Wait for LB to be idle.
  WaitForLoadBalanceCompletion(30s * kTimeMultiplier * 10);

  // Validate loads are balanced.
  WaitForLoadToBeBalanced(30s * kTimeMultiplier * 10);
}

class YbAdminSnapshotScheduleTestWithLBYsql : public YbAdminSnapshotScheduleTestWithLB {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->enable_ysql = true;
    opts->extra_tserver_flags.emplace_back("--ysql_num_shards_per_tserver=1");
    opts->num_masters = 3;
    opts->extra_master_flags.emplace_back("--enable_ysql_tablespaces_for_placement=true");
  }

  // Adds tserver in c1.r1 and specified zone.
  Status AddTServerInZone(const std::string& zone, int count) {
    std::vector<std::string> ts_flags = ExtraTSFlags();
    ts_flags.push_back("--placement_cloud=c1");
    ts_flags.push_back("--placement_region=r1");
    ts_flags.push_back(Format("--placement_zone=$0", zone));
    RETURN_NOT_OK(cluster_->AddTabletServer(true, ts_flags));
    RETURN_NOT_OK(cluster_->WaitForTabletServerCount(count, 30s));
    return Status::OK();
  }

  std::string GetCreateTablespaceCommand() {
    return "create tablespace demo_ts with (replica_placement='{\"num_replicas\": 3, "
           "\"placement_blocks\": [{\"cloud\":\"c1\", \"region\":\"r1\", "
           "\"zone\":\"z1\", \"min_num_replicas\":1}, "
           "{\"cloud\":\"c1\", \"region\":\"r1\", \"zone\":\"z2\", "
           "\"min_num_replicas\":1}, {\"cloud\":\"c1\", \"region\":\"r1\", "
           "\"zone\":\"z3\", \"min_num_replicas\":1}]}')";
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, PgsqlPreventTablespaceDrop,
          YbAdminSnapshotScheduleTestWithLBYsql) {
  // Start a cluster with 3 nodes. Create a snapshot schedule on a db.
  auto schedule_id = ASSERT_RESULT(PreparePg());
  LOG(INFO) << "Cluster started with 3 nodes";

  // Add 3 more tablet servers in custom placments.
  ASSERT_OK(AddTServerInZone("z1", 4));
  ASSERT_OK(AddTServerInZone("z2", 5));
  ASSERT_OK(AddTServerInZone("z3", 6));
  LOG(INFO) << "Added 3 more tservers in z1, z2 and z3";

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  // Create a tablespace and an associated table.
  std::string tblspace_command = GetCreateTablespaceCommand();

  LOG(INFO) << "Tablespace command: " << tblspace_command;
  ASSERT_OK(conn.Execute(tblspace_command));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) TABLESPACE demo_ts"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));
  LOG(INFO) << "Created tablespace and table";

  // Now drop the table.
  ASSERT_OK(conn.Execute("DROP TABLE test_table"));
  LOG(INFO) << "Dropped the table test_table";

  // Try dropping the tablespace, it should fail.
  auto res = conn.Execute("DROP TABLESPACE demo_ts");
  LOG(INFO) << res.ToString();
  ASSERT_FALSE(res.ok());
  ASSERT_STR_CONTAINS(
      res.ToString(), "Dropping tablespaces is not allowed on clusters "
                      "with Point in Time Restore activated");

  // Delete the schedule.
  ASSERT_OK(DeleteSnapshotSchedule(schedule_id));
  LOG(INFO) << "Deleted snapshot schedule successfully";

  // Now drop the tablespace, it should succeed.
  ASSERT_OK(conn.Execute("DROP TABLESPACE demo_ts"));
  LOG(INFO) << "Successfully dropped the tablespace";
}

TEST_F_EX(
    YbAdminSnapshotScheduleTest, PgsqlRestoreDroppedTableWithTablespace,
    YbAdminSnapshotScheduleTestWithLBYsql) {
  // Start a cluster with 3 nodes. Create a snapshot schedule on a db.
  auto schedule_id = ASSERT_RESULT(PreparePg());
  LOG(INFO) << "Cluster started with 3 nodes";

  // Add 3 more tablet servers in custom placments.
  ASSERT_OK(AddTServerInZone("z1", 4));
  ASSERT_OK(AddTServerInZone("z2", 5));
  ASSERT_OK(AddTServerInZone("z3", 6));
  LOG(INFO) << "Added 3 more tservers in z1, z2 and z3";

  auto conn = ASSERT_RESULT(PgConnect(client::kTableName.namespace_name()));

  // Create a tablespace and an associated table.
  std::string tblspace_command = GetCreateTablespaceCommand();

  LOG(INFO) << "Tablespace command: " << tblspace_command;
  ASSERT_OK(conn.Execute(tblspace_command));

  ASSERT_OK(conn.Execute(
      "CREATE TABLE test_table (key INT PRIMARY KEY, value TEXT) "
      "TABLESPACE demo_ts SPLIT INTO 24 TABLETS"));
  ASSERT_OK(conn.Execute("INSERT INTO test_table VALUES (1, 'before')"));
  LOG(INFO) << "Created tablespace and table with 24 tablets";

  // Wait for some time before noting down the time.
  SleepFor(MonoDelta::FromSeconds(5 * kTimeMultiplier));

  // Note down the time.
  Timestamp time(ASSERT_RESULT(WallClock()->Now()).time_point);

  // Now drop the table.
  ASSERT_OK(conn.Execute("DROP TABLE test_table"));
  LOG(INFO) << "Dropped the table test_table";

  // Restore to the time when the table existed.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));

  // Verify data.
  auto res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table"));
  LOG(INFO) << "Got value " << res;
  ASSERT_EQ(res, "before");

  // Add another tserver in z1, the load should get evenly balanced.
  ASSERT_OK(AddTServerInZone("z1", 7));
  LOG(INFO) << "Added tserver 7";

  // Validate loads are balanced.
  ASSERT_OK(WaitFor([&]() -> Result<bool> {
    auto tserver_loads = VERIFY_RESULT(GetTServerLoads(30s * kTimeMultiplier * 10));
    for (size_t i = 0; i < cluster_->num_tablet_servers(); i++) {
      if (i < 3 && tserver_loads[i] != 0) {
        return false;
      }
      if ((i == 3 || i == 6) && tserver_loads[i] != 12) {
        return false;
      }
      if ((i == 4 || i == 5) && tserver_loads[i] != 24) {
        return false;
      }
    }
    return true;
  }, 30s * kTimeMultiplier * 10, "Are loads balanced"));
  LOG(INFO) << "Loads are now balanced";

  // Verify table is still functional.
  ASSERT_OK(conn.Execute("INSERT INTO test_table (key, value) VALUES (2, 'after')"));
  res = ASSERT_RESULT(conn.FetchValue<std::string>("SELECT value FROM test_table WHERE key=2"));
  LOG(INFO) << "Got value " << res;
  ASSERT_EQ(res, "after");
}

TEST_F_EX(YbAdminSnapshotScheduleTest, CreateDuplicateSchedules,
          YbAdminSnapshotScheduleTestWithYsql) {
  ASSERT_OK(PrepareCommon());

  auto conn = ASSERT_RESULT(PgConnect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", client::kTableName.namespace_name()));

  ASSERT_OK(CreateSnapshotScheduleAndWaitSnapshot(
      "ysql." + client::kTableName.namespace_name(), kInterval, kRetention));

  // Try and fail to create a snapshot in the same keyspace.
  auto res = CreateSnapshotSchedule(kInterval, kRetention, "ysql." + kTableName.namespace_name());
  ASSERT_FALSE(res.ok());
  ASSERT_STR_CONTAINS(res.ToString(),
    "already exists for the given keyspace ysql." + kTableName.namespace_name());
}

class YbAdminSnapshotScheduleTestWithYsqlTransactionalDDL
    : public YbAdminSnapshotScheduleTestWithYsql {
 public:
  std::vector<std::string> ExtraMasterFlags() override {
    // To speed up tests.
    return { "--snapshot_coordinator_cleanup_delay_ms=1000",
             "--snapshot_coordinator_poll_interval_ms=500",
             "--enable_transactional_ddl_gc=true" };
  }
};

TEST_F_EX(YbAdminSnapshotScheduleTest, BeforeCreateFinishes,
          YbAdminSnapshotScheduleTestWithYsqlTransactionalDDL) {
  auto schedule_id = ASSERT_RESULT(PreparePg(YsqlColocationConfig::kDBColocated));

  auto conn = ASSERT_RESULT(PgConnect(kTableName.namespace_name()));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE demo_new (id INT, name TEXT)"));

  // We want to restore to time before table was created.
  auto time = ASSERT_RESULT(GetCurrentTime());
  // Skip txn verification at the master so that txn metadata remains.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_skip_transaction_verification", "true"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE demo (id INT, name TEXT)"));

  // Reset the flag.
  ASSERT_OK(cluster_->SetFlagOnMasters("TEST_skip_transaction_verification", "false"));
  // Restore to this time. There should be no fatals.
  ASSERT_OK(RestoreSnapshotSchedule(schedule_id, time));
}

}  // namespace tools
}  // namespace yb
