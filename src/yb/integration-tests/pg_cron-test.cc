// Copyright (c) YugabyteDB, Inc.
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

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/tserver/tablet_server.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::chrono_literals;

namespace yb {

constexpr auto kTableName = "tbl1";
constexpr auto kDefaultJobName = "Job1";
constexpr auto kobListRefreshInterval = 10;

class PgCronTest : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  PgCronTest() = default;

  void SetUp() override {
    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.replication_factor = 3;
    opts.num_masters = 1;
    opts.enable_ysql = true;

    opts.extra_tserver_flags.push_back("--vmodule=pg_cron*=4");
    opts.extra_tserver_flags.push_back("--TEST_enable_pg_cron=true");
    opts.extra_tserver_flags.push_back(
        Format("--ysql_pg_conf_csv=cron.yb_job_list_refresh_interval=$0", kobListRefreshInterval));

    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(MiniClusterTestWithClient<ExternalMiniCluster>::CreateClient());

    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(Connect()));

    ASSERT_OK(CreateTable());
    ASSERT_OK(CreateCronExtension());

    // Make the first tserver the cron leader. #22360 will make this unnecessary.
    ASSERT_OK(cluster_->SetFlag(cluster_->tablet_server(0), "TEST_is_ysql_cron_leader", "true"));
  }

  Result<pgwrapper::PGConn> Connect() { return cluster_->ConnectToDB(); }

  Status CreateTable(const std::string& table_name = kTableName) {
    return conn_->Execute(Format(
        "CREATE TABLE $0 (a INT, node TEXT, insert_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",
        table_name));
  }

  Status CreateCronExtension() { return conn_->Execute("CREATE EXTENSION pg_cron"); }

  Result<int64_t> Schedule1SecInsertJob(const std::string& job_name = kDefaultJobName) {
    auto insert_query =
        Format("INSERT INTO $0(a, node) VALUES (1, host(inet_server_addr()))", kTableName);
    return ScheduleJob(job_name, "1 second", insert_query);
  }

  // Returns the JobId
  Result<int64_t> ScheduleJob(
      const std::string& job_name, const std::string& schedule, const std::string& query) {
    LOG(INFO) << Format(
        "Scheduling job $0 with schedule $1 and query $2", job_name, schedule, query);
    return conn_->FetchRow<int64_t>(
        Format("SELECT cron.schedule('$0', '$1', '$2')", job_name, schedule, query));
  }

  Status UnscheduleJob(const int64_t& job_id) {
    LOG(INFO) << Format("Unscheduling job $0", job_id);

    // Returns a bool indicating if the job was unscheduled.
    auto result =
        VERIFY_RESULT(conn_->FetchRow<bool>(Format("SELECT cron.unschedule($0)", job_id)));
    SCHECK(result, IllegalState, "Failed to unschedule job");
    return Status::OK();
  }

  Result<int64_t> GetRowCount(const std::string& table_name = kTableName) {
    return conn_->FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", table_name));
  }

  std::unique_ptr<pgwrapper::PGConn> conn_;
  TabletId tablet_id_;
};

// Make sure cron jobs are run only once in the provided interval.
TEST_F(PgCronTest, AtMostOnceTest) {
  // Schedule a 1sec job.
  const auto job_id = ASSERT_RESULT(Schedule1SecInsertJob());

  // Wait for the job to get picked up and run for a while.
  const auto default_sleep_sec = 10;
  const auto buffer_sec = 3;
  const auto sleep_time_sec = kobListRefreshInterval + default_sleep_sec + buffer_sec;
  SleepFor(sleep_time_sec * 1s);

  // Stop the job.
  ASSERT_OK(UnscheduleJob(job_id));
  SleepFor(kobListRefreshInterval * 1s);

  // Make sure we inserted the correct number of rows.
  auto row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(row_count, default_sleep_sec);
  // Max rows is the total amount of time we slept.
  ASSERT_LE(row_count, sleep_time_sec + kobListRefreshInterval);

  // Wait a little bit longer to make sure job is still not running.
  SleepFor(default_sleep_sec * 1s);
  auto row_count2 = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(row_count, row_count2);

  // Make sure it was cron that inserted all the rows.
  auto job_run_count =
      ASSERT_RESULT(conn_->FetchRow<int64_t>("SELECT COUNT(*) FROM cron.job_run_details"));
  ASSERT_EQ(row_count, job_run_count);
}

TEST_F(PgCronTest, PerMinuteTask) {
  auto insert_query =
      Format("INSERT INTO $0(a, node) VALUES (1, host(inet_server_addr()))", kTableName);
  ASSERT_OK(ScheduleJob("job1", "* * * * *", insert_query));

  // Wait 2 minutes.
  const auto default_sleep_min = 2;
  const auto buffer_sec = 3;
  const auto sleep_time_sec = kobListRefreshInterval + (60 * default_sleep_min) + buffer_sec;
  SleepFor(sleep_time_sec * 1s);

  // We should have (default_sleep_min, default_sleep_min+1) rows.
  auto row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(row_count, default_sleep_min);
  ASSERT_LE(row_count, default_sleep_min + 1);
}

TEST_F(PgCronTest, RecreatejobWithSameName) {}

}  // namespace yb
