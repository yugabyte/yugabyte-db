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
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/master_client.pb.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"
#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {

constexpr auto kTableName = "tbl1";
constexpr auto kDefaultJobName = "Job1";
constexpr auto kobListRefreshInterval = 10;
const client::YBTableName service_table_name =
    stateful_service::GetStatefulServiceTableName(StatefulServiceKind::PG_CRON_LEADER);
const auto kTimeout = 60s * kTimeMultiplier;

class PgCronTest : public MiniClusterTestWithClient<ExternalMiniCluster> {
 public:
  PgCronTest() = default;

  void SetUp() override {
    // #22462. Skip in TSAN as starting the pg_cron background worker gets into a deadlock with
    // LLVMSymbolizer.
    YB_SKIP_TEST_IN_TSAN();

    YBMiniClusterTestBase<ExternalMiniCluster>::SetUp();
    ExternalMiniClusterOptions opts;
    opts.num_tablet_servers = 3;
    opts.replication_factor = 3;
    opts.num_masters = 1;
    opts.enable_ysql = true;

    opts.extra_master_flags.push_back("--allowed_preview_flags_csv=enable_pg_cron");
    opts.extra_master_flags.push_back("--enable_pg_cron=true");

    opts.extra_tserver_flags.push_back("--vmodule=pg_cron*=4");
    opts.extra_tserver_flags.push_back("--allowed_preview_flags_csv=enable_pg_cron");
    opts.extra_tserver_flags.push_back("--enable_pg_cron=true");
    opts.extra_tserver_flags.push_back(
        Format("--ysql_pg_conf_csv=cron.yb_job_list_refresh_interval=$0", kobListRefreshInterval));
    opts.extra_tserver_flags.push_back(
        Format("--pg_cron_leader_lease_sec=$0", kobListRefreshInterval));
    opts.extra_tserver_flags.push_back("--pg_cron_leadership_refresh_sec=1");

    cluster_.reset(new ExternalMiniCluster(opts));
    ASSERT_OK(cluster_->Start());
    ASSERT_OK(MiniClusterTestWithClient<ExternalMiniCluster>::CreateClient());

    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(cluster_->ConnectToDB()));

    ASSERT_OK(CreateTable());
    ASSERT_OK(CreateCronExtension());

    ASSERT_OK(client_->WaitForCreateTableToFinish(service_table_name));
    std::vector<TabletId> tablet_ids;
    ASSERT_OK(client_->GetTablets(service_table_name, 0 /* max_tablets */, &tablet_ids, NULL));
    ASSERT_EQ(tablet_ids.size(), 1);
    tablet_id_ = tablet_ids.front();
    ASSERT_OK(cluster_->WaitForLoadBalancerToBecomeIdle(client_, kTimeout));
  }

  pgwrapper::PGConn* UseDefaultConnIfNull(pgwrapper::PGConn* conn) {
    return conn == nullptr ? conn_.get() : conn;
  }

  Status CreateTable(
      pgwrapper::PGConn* conn = nullptr, const std::string& table_name = kTableName) {
    conn = UseDefaultConnIfNull(conn);
    return conn->Execute(Format(
        "CREATE TABLE $0 (a INT, node TEXT, insert_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP)",
        table_name));
  }

  Status CreateCronExtension(pgwrapper::PGConn* conn = nullptr) {
    conn = UseDefaultConnIfNull(conn);
    return conn->Execute("CREATE EXTENSION pg_cron");
  }

  Result<int64_t> Schedule1SecInsertJob(
      pgwrapper::PGConn* conn = nullptr, const std::string& job_name = kDefaultJobName) {
    auto insert_query =
        Format("INSERT INTO $0(a, node) VALUES (1, host(inet_server_addr()))", kTableName);
    return ScheduleJob(job_name, "1 second", insert_query, conn);
  }

  // Returns the JobId
  Result<int64_t> ScheduleJob(
      const std::string& job_name, const std::string& schedule, const std::string& query,
      pgwrapper::PGConn* conn = nullptr) {
    conn = UseDefaultConnIfNull(conn);
    LOG(INFO) << Format(
        "Scheduling job $0 with schedule $1 and query $2", job_name, schedule, query);
    return conn->FetchRow<pgwrapper::PGUint64>(
        Format("SELECT cron.schedule('$0', '$1', '$2')", job_name, schedule, query));
  }

  // Returns the JobId
  Result<int64_t> ScheduleJobInDatabase(
      const std::string& job_name, const std::string& schedule, const std::string& query,
      const std::string& db_name) {
    LOG(INFO) << Format(
        "Scheduling job $0 with schedule $1 and query $2", job_name, schedule, query);
    return conn_->FetchRow<pgwrapper::PGUint64>(Format(
        "SELECT cron.schedule_in_database('$0', '$1', '$2', '$3')", job_name, schedule, query,
        db_name));
  }

  Status UnscheduleJob(const int64_t& job_id, pgwrapper::PGConn* conn = nullptr) {
    conn = UseDefaultConnIfNull(conn);
    LOG(INFO) << Format("Unscheduling job $0", job_id);

    // Returns a bool indicating if the job was unscheduled.
    auto result = VERIFY_RESULT(conn->FetchRow<bool>(Format("SELECT cron.unschedule($0)", job_id)));
    SCHECK(result, IllegalState, "Failed to unschedule job");
    return Status::OK();
  }

  Result<int64_t> GetRowCount(
      pgwrapper::PGConn* conn = nullptr, const std::string& table_name = kTableName) {
    conn = UseDefaultConnIfNull(conn);
    return conn->FetchRow<pgwrapper::PGUint64>(Format("SELECT COUNT(*) FROM $0", table_name));
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

  // Wait a little bit longer to make sure job is not running.
  SleepFor(default_sleep_sec * 1s);
  auto row_count2 = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(row_count, row_count2);

  // Make sure it was cron that inserted all the rows.
  auto job_run_count = ASSERT_RESULT(
      conn_->FetchRow<pgwrapper::PGUint64>("SELECT COUNT(*) FROM cron.job_run_details"));
  ASSERT_EQ(row_count, job_run_count);
}

// Run a job every minute and make sure it runs at most once.
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

// Schedule a job an a database other than the cron database and make sure it runs.
TEST_F(PgCronTest, JobOnDifferentDB) {
  constexpr auto kDropTablesFunc = R"(
    CREATE OR REPLACE FUNCTION drop_all_tables() RETURNS VOID AS $$
    DECLARE
        table_name text;
    BEGIN
        FOR table_name IN (
            SELECT tablename
            FROM pg_tables
            WHERE schemaname = 'public'
        )
        LOOP
            EXECUTE 'DROP TABLE IF EXISTS ' || quote_ident(table_name);
        END LOOP;
    END;
    $$ LANGUAGE plpgsql;
  )";

  constexpr auto db_name = "db1";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", db_name));

  auto new_db_conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));
  // Multi line query.
  ASSERT_OK(new_db_conn.Execute(kDropTablesFunc));

  ASSERT_OK(ScheduleJobInDatabase("drop tables", "1 second", "SELECT drop_all_tables()", db_name));

  // Wait 2 minutes.
  const auto default_sleep_min = 2;
  const auto sleep_time = MonoDelta::FromSeconds(kobListRefreshInterval + (60 * default_sleep_min));
  const auto start = MonoTime::Now();

  for (int i = 0; MonoTime::Now() - start < sleep_time; i++) {
    LOG(INFO) << Format("Creating table $0", i);
    ASSERT_OK(new_db_conn.ExecuteFormat("CREATE TABLE tbl$0(a INT)", i));
    SleepFor(1s);
  }

  // Wait for the job to get to all the tables.
  SleepFor(3s * kTimeMultiplier);

  // We should have (default_sleep_min, default_sleep_min+1) rows.
  auto table_count = ASSERT_RESULT(new_db_conn.FetchRow<pgwrapper::PGUint64>(
      "SELECT COUNT(*) FROM pg_tables WHERE schemaname = 'public'"));
  ASSERT_EQ(table_count, 0);
}

// Make sure pg_cron can survive the crash of the leader tserver/postgres process.
TEST_F(PgCronTest, LeaderCrash) {
  // Schedule a 1sec job.
  const auto job_id = ASSERT_RESULT(Schedule1SecInsertJob());

  // Wait for the job to get picked up and run for a while.
  const auto default_sleep_sec = 10;
  const auto kSleepBuffer = 3s;
  const auto sleep_time = (kobListRefreshInterval + default_sleep_sec) * 1s + kSleepBuffer;
  SleepFor(sleep_time);

  // Verify GetStatefulServiceLocation returns the correct location.
  auto initial_leader_idx = ASSERT_RESULT(cluster_->GetTabletLeaderIndex(tablet_id_));
  auto location =
      ASSERT_RESULT(client_->GetStatefulServiceLocation(StatefulServiceKind::PG_CRON_LEADER));
  ASSERT_EQ(location.permanent_uuid(), cluster_->tablet_server(initial_leader_idx)->uuid());

  cluster_->tablet_server(initial_leader_idx)->Shutdown();

  // Connect to some other node.
  conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(cluster_->ConnectToDB(
      /*db_name=*/"yugabyte",
      /*node_index=*/(initial_leader_idx + 1) % cluster_->num_tablet_servers())));

  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto leader = cluster_->GetTabletLeaderIndex(tablet_id_);
        return leader.ok();
      },
      kTimeout, "Wait for new leader"));

  auto final_leader_idx = ASSERT_RESULT(cluster_->GetTabletLeaderIndex(tablet_id_));
  ASSERT_NE(final_leader_idx, initial_leader_idx);

  // Make sure new leader is running jobs.
  auto row_count = ASSERT_RESULT(GetRowCount());
  SleepFor(sleep_time);
  auto row_count2 = ASSERT_RESULT(GetRowCount());
  ASSERT_GT(row_count2, row_count);

  // Stop the job.
  ASSERT_OK(UnscheduleJob(job_id));
  SleepFor(kobListRefreshInterval * 1s);

  // Make sure we inserted the minumum number of rows.
  auto row_count3 = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(row_count3, 2 * default_sleep_sec);

  // Wait a little bit longer to make sure job is not running.
  SleepFor(default_sleep_sec * 1s);
  auto row_count4 = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(row_count3, row_count4);

  // Make it was cron that inserted all the rows inserted rows.
  auto job_run_count = ASSERT_RESULT(
      conn_->FetchRow<pgwrapper::PGUint64>("SELECT COUNT(*) FROM cron.job_run_details"));
  // May not be equal as some run might have failed midway.
  ASSERT_LE(row_count3, job_run_count);

  // YBMiniClusterTestBase test-end verification will fail if the cluster is up with stopped nodes.
  cluster_->Shutdown();
}

// Make sure pg_cron handles graceful leader movement of the Stateful service.
TEST_F(PgCronTest, GracefulLeaderMove) {
  // Schedule a 1sec job.
  const auto job_id = ASSERT_RESULT(Schedule1SecInsertJob());

  // Wait for the job to get picked up and run for a while.
  const auto default_sleep_sec = 10;
  const auto kSleepBuffer = 3s;
  const auto sleep_time = (kobListRefreshInterval + default_sleep_sec) * 1s + kSleepBuffer;
  SleepFor(sleep_time);

  ASSERT_OK(cluster_->MoveTabletLeader(tablet_id_));
  ASSERT_OK(cluster_->WaitForLoadBalancerToBecomeIdle(client_, kTimeout));

  // Make sure new leader is running jobs.
  auto row_count = ASSERT_RESULT(GetRowCount());
  SleepFor(sleep_time);
  auto row_count2 = ASSERT_RESULT(GetRowCount());
  ASSERT_GT(row_count2, row_count);

  // Stop the job.
  ASSERT_OK(UnscheduleJob(job_id));
  SleepFor(kobListRefreshInterval * 1s);

  // Make sure we inserted the minumum number of rows.
  auto row_count3 = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(row_count3, 2 * default_sleep_sec);

  // Wait a little bit longer to make sure job is not running.
  SleepFor(default_sleep_sec * 1s);
  auto row_count4 = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(row_count3, row_count4);

  // Make it was cron that inserted all the rows inserted rows.
  auto job_run_count = ASSERT_RESULT(
      conn_->FetchRow<pgwrapper::PGUint64>("SELECT COUNT(*) FROM cron.job_run_details"));
  // May not be equal as some run might have failed midway.
  ASSERT_LE(row_count3, job_run_count);
}

// Verify the ysql_cron_database_name flag works as expected.
TEST_F(PgCronTest, ChangeCronDB) {
  constexpr auto db_name = "db1";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", db_name));

  // Schedule a 1sec job.
  ASSERT_OK(Schedule1SecInsertJob());

  // Wait for the job to get picked up and run for a while.
  const auto default_sleep_sec = 10;
  const auto kSleepBuffer = 3s;
  const auto sleep_time = (kobListRefreshInterval + default_sleep_sec) * 1s + kSleepBuffer;
  SleepFor(sleep_time);

  // Change the cron database with a restart.
  cluster_->AddExtraFlagOnTServers("ysql_cron_database_name", db_name);
  cluster_->Shutdown(ExternalMiniCluster::NodeSelectionMode::TS_ONLY);
  ASSERT_OK(cluster_->Restart());
  conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(cluster_->ConnectToDB()));
  auto new_db_conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));

  auto old_db_row_count = ASSERT_RESULT(GetRowCount());

  ASSERT_OK(CreateCronExtension(&new_db_conn));
  ASSERT_OK(CreateTable(&new_db_conn));
  auto job_id = ASSERT_RESULT(Schedule1SecInsertJob(&new_db_conn));

  SleepFor(sleep_time);
  auto new_db_row_count1 = ASSERT_RESULT(GetRowCount(&new_db_conn));
  ASSERT_GT(new_db_row_count1, 0);

  // Make sure no new jobs are run on the old db.
  auto old_db_row_count2 = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(old_db_row_count2, old_db_row_count);

  ASSERT_OK(UnscheduleJob(job_id, &new_db_conn));

  // Wait a little bit longer to make sure job is not running.
  SleepFor(sleep_time);
  auto new_db_row_count2 = ASSERT_RESULT(GetRowCount(&new_db_conn));

  // Make sure it was cron from the new DB that inserted all the rows.
  auto new_db_job_run_count = ASSERT_RESULT(
      new_db_conn.FetchRow<pgwrapper::PGUint64>("SELECT COUNT(*) FROM cron.job_run_details"));
  // May not be equal as some run might have failed midway.
  ASSERT_LE(new_db_row_count2, new_db_job_run_count);
}

}  // namespace yb
