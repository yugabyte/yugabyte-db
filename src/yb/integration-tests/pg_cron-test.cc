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

#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/master_client.pb.h"

#include "yb/tserver/stateful_services/pg_cron_leader_service.h"
#include "yb/tserver/stateful_services/stateful_service_base.h"

#include "yb/util/backoff_waiter.h"

#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {

constexpr auto kTableName = "tbl1";
constexpr auto kDefaultJobName = "Job1";
constexpr auto kJobListRefreshInterval = 10;
constexpr auto kFailedJobStatus = "failed";
constexpr auto kRunningJobStatus = "running";
const client::YBTableName service_table_name =
    stateful_service::GetStatefulServiceTableName(StatefulServiceKind::PG_CRON_LEADER);
const auto kTimeout = 60s * kTimeMultiplier;

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

    opts.extra_master_flags.push_back("--enable_pg_cron=true");

    opts.extra_tserver_flags.push_back("--enable_pg_cron=true");

    opts.extra_tserver_flags.push_back(
        Format("--ysql_pg_conf_csv=cron.yb_job_list_refresh_interval=$0", kJobListRefreshInterval));
    opts.extra_tserver_flags.push_back("--pg_cron_leadership_refresh_sec=1");
    opts.extra_tserver_flags.push_back(
        Format("--pg_cron_leader_lease_sec=$0", kJobListRefreshInterval));

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

  std::string GetInsertQuery() {
    return Format("INSERT INTO $0(a, node) VALUES (1, host(inet_server_addr()))", kTableName);
  }

  Result<int64_t> Schedule1SecInsertJob(
      pgwrapper::PGConn* conn = nullptr, const std::string& job_name = kDefaultJobName) {
    return ScheduleJob(job_name, "1 second", GetInsertQuery(), conn);
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

  Status WaitForRowCountAbove(int64_t min_count) {
    return WaitFor(
        [this, &min_count]() -> Result<bool> {
          const auto row_count = VERIFY_RESULT(GetRowCount());
          return row_count > min_count;
        },
        kTimeout, Format("Wait for row count to be above $0", min_count));
  }

  Status WaitForJobStatus(int64 job_id, const std::string job_name, const std::string job_status) {
    Status s = LoggedWaitFor(
        [this, job_id, job_status]() -> Result<bool> {
          /*
           * This query could be written as
           * `SELECT status FROM ... WHERE ... ORDER BY start_time DESC LIMIT 1`
           * but that would require a special case for when there are 0 entries for this jobid.
           */
          return VERIFY_RESULT(conn_->FetchRow<pgwrapper::PGUint64>(Format(
              "SELECT COUNT(*) FROM ("
              "    SELECT * FROM cron.job_run_details WHERE jobid = $0 "
              "    ORDER BY start_time DESC LIMIT 1 "
              ") AS most_recent "
              "WHERE status = '$1';",
              job_id, job_status))) > 0;
        },
        kTimeout,
        Format("Wait for most recent run of job '$0' to have status '$1'", job_name, job_status));
    if (!s.ok())
      LOG(INFO) << conn_->FetchAllAsString(Format(
          "SELECT * FROM cron.job_run_details WHERE jobid = $0 ORDER BY start_time DESC", job_id),
          ",", "\n");
    return s;
  }

  Status WaitForDataInPgCronLeaderTable() {
    auto table_name =
        stateful_service::GetStatefulServiceTableName(StatefulServiceKind::PG_CRON_LEADER);
    client::TableHandle table;
    RETURN_NOT_OK(table.Open(table_name, client_.get()));
    auto session = client_->NewSession(kTimeout);

    return LoggedWait(
        [&]() -> Result<bool> {
          auto read_op = table.NewReadOp(session->arena());
          auto* read_req = read_op->mutable_request();
          table.AddColumns(
              {stateful_service::kPgCronIdColName, stateful_service::kPgCronDataColName}, read_req);

          RETURN_NOT_OK(session->TEST_ApplyAndFlush(read_op));
          const auto row_block = ql::RowsResult(read_op.get()).GetRowBlock();

          return row_block->row_count() > 0;
        },
        CoarseMonoClock::Now() + kTimeout, "Waiting for data in pg cron leader table");
  }

  Result<int64_t> GetPersistedLastMinute() {
    auto table_name =
        stateful_service::GetStatefulServiceTableName(StatefulServiceKind::PG_CRON_LEADER);
    client::TableHandle table;
    RETURN_NOT_OK(table.Open(table_name, client_.get()));
    auto session = client_->NewSession(kTimeout);

    auto read_op = table.NewReadOp(session->arena());
    auto* read_req = read_op->mutable_request();
    table.AddColumns(
        {stateful_service::kPgCronIdColName, stateful_service::kPgCronDataColName}, read_req);

    RETURN_NOT_OK(session->TEST_ApplyAndFlush(read_op));
    const auto row_block = ql::RowsResult(read_op.get()).GetRowBlock();
    SCHECK_EQ(row_block->row_count(), 1, IllegalState, "Unexpected row count");

    const auto& row = row_block->row(0);
    LOG(INFO) << "Read row from Pg Cron leader table: " << row.ToString();

    SCHECK_EQ(row.column_count(), 2, IllegalState, "More columns than expected");

    const auto& key_value = row.column(0).value();
    SCHECK(key_value.has_int64_value(), IllegalState, key_value.ShortDebugString());
    SCHECK_EQ(
        key_value.int64_value(), stateful_service::kPgCronDataKey, IllegalState,
        "Unexpected key value");

    return stateful_service::PgCronLeaderService::ExtractLastMinute(row.column(1));
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
  const auto sleep_time_sec = kJobListRefreshInterval + default_sleep_sec + buffer_sec;
  SleepFor(sleep_time_sec * 1s);

  // Stop the job.
  ASSERT_OK(UnscheduleJob(job_id));
  SleepFor(kJobListRefreshInterval * 1s);

  // Make sure we inserted the correct number of rows.
  auto row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(row_count, default_sleep_sec);
  // Max rows is the total amount of time we slept.
  ASSERT_LE(row_count, sleep_time_sec + kJobListRefreshInterval);

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
  ASSERT_OK(ScheduleJob("job1", "* * * * *", GetInsertQuery()));

  // Wait 2 minutes.
  const auto default_sleep_min = 2;
  const auto buffer_sec = 3;
  const auto sleep_time_sec = kJobListRefreshInterval + (60 * default_sleep_min) + buffer_sec;
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
  const auto sleep_time =
      MonoDelta::FromSeconds(kJobListRefreshInterval + (60 * default_sleep_min));
  const auto start = MonoTime::Now();

  for (int i = 0; MonoTime::Now() - start < sleep_time; i++) {
    LOG(INFO) << Format("Creating table $0", i);
    auto status = new_db_conn.ExecuteFormat("CREATE TABLE tbl$0(a INT)", i);
    if (!status.ok()) {
      if (status.message().Contains("Catalog Version Mismatch")) {
        LOG(INFO) << Format("Ignoring Catalog Version Mismatch: $0", status.ToString());
        SleepFor(200ms);
        continue;
      }
      ASSERT_OK(status);
    }
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
TEST_F(PgCronTest, LeaderCrash1) {
  // Schedule a 1sec job.
  const auto job_id = ASSERT_RESULT(Schedule1SecInsertJob());

  ASSERT_OK(WaitForDataInPgCronLeaderTable());

  // Let the job run a few times.
  // Wait for the job to get picked up and run for a while.
  const auto default_sleep_sec = 10;
  const auto kSleepBuffer = 3s;
  const auto sleep_time = (kJobListRefreshInterval + default_sleep_sec) * 1s + kSleepBuffer;
  SleepFor(sleep_time);

  auto row_count1 = ASSERT_RESULT(GetRowCount());
  ASSERT_GT(row_count1, 0);

  const auto start_last_minute = ASSERT_RESULT(GetPersistedLastMinute());

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
  auto row_count2 = ASSERT_RESULT(GetRowCount());
  // Wait for the minute to change.
  const auto sleep_time_sec = 70;
  SleepFor(sleep_time_sec * 1s);
  auto row_count3 = ASSERT_RESULT(GetRowCount());
  ASSERT_GT(row_count3, row_count2);
  auto last_minute = ASSERT_RESULT(GetPersistedLastMinute());
  ASSERT_GT(last_minute, start_last_minute);

  // Stop the job.
  ASSERT_OK(UnscheduleJob(job_id));
  SleepFor(kJobListRefreshInterval * 1s);

  // Final row count.
  auto row_count4 = ASSERT_RESULT(GetRowCount());

  // Wait a little bit longer to make sure job is not running.
  SleepFor(kJobListRefreshInterval * 1s);
  auto row_count5 = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(row_count4, row_count5);

  // Make it was cron that inserted all the rows inserted rows.
  auto job_run_count = ASSERT_RESULT(
      conn_->FetchRow<pgwrapper::PGUint64>("SELECT COUNT(*) FROM cron.job_run_details"));
  // May not be equal as some run might have failed midway.
  ASSERT_LE(row_count4, job_run_count);

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
  const auto sleep_time = (kJobListRefreshInterval + default_sleep_sec) * 1s + kSleepBuffer;
  SleepFor(sleep_time);

  ASSERT_OK(WaitForDataInPgCronLeaderTable());
  const auto start_last_minute = ASSERT_RESULT(GetPersistedLastMinute());

  ASSERT_OK(cluster_->MoveTabletLeader(tablet_id_));
  ASSERT_OK(cluster_->WaitForLoadBalancerToBecomeIdle(client_, kTimeout));

  // Make sure new leader is running jobs.
  auto row_count = ASSERT_RESULT(GetRowCount());
  SleepFor(sleep_time);
  auto row_count2 = ASSERT_RESULT(GetRowCount());
  ASSERT_GT(row_count2, row_count);
  auto last_minute = ASSERT_RESULT(GetPersistedLastMinute());
  ASSERT_GE(last_minute, start_last_minute);

  // Stop the job.
  ASSERT_OK(UnscheduleJob(job_id));
  SleepFor(kJobListRefreshInterval * 1s);

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
  const auto sleep_time = (kJobListRefreshInterval + default_sleep_sec) * 1s + kSleepBuffer;
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

TEST_F(PgCronTest, ValidateStoredLastMinute) {
  // Wait till the cron leader has picked up the extension create.
  ASSERT_OK(WaitForDataInPgCronLeaderTable());

  const auto start_last_minute = ASSERT_RESULT(GetPersistedLastMinute());

  const auto kSleepBuffer = 3s;
  SleepFor(1min + kSleepBuffer);

  auto last_minute1 = ASSERT_RESULT(GetPersistedLastMinute());
  ASSERT_GT(last_minute1, start_last_minute);

  ASSERT_OK(conn_->Execute("DROP EXTENSION pg_cron"));
  SleepFor(kJobListRefreshInterval * 1s + kSleepBuffer);
  const auto last_minute2 = ASSERT_RESULT(GetPersistedLastMinute());

  // Make sure the last minute is not updated.
  SleepFor(1min + kSleepBuffer);
  auto last_minute3 = ASSERT_RESULT(GetPersistedLastMinute());
  ASSERT_GT(last_minute3, last_minute2);

  ASSERT_OK(CreateCronExtension());
  SleepFor(1min + kSleepBuffer);
  auto last_minute4 = ASSERT_RESULT(GetPersistedLastMinute());
  ASSERT_GT(last_minute4, last_minute3);
}

// Make sure the per minute job runs every minute even if the cron leader moves.
TEST_F(PgCronTest, PerMinuteTaskWithLeaderMove) {
  ASSERT_OK(ScheduleJob("job1", "* * * * *", GetInsertQuery()));
  const auto kSleepBuffer = 3s;
  SleepFor(kJobListRefreshInterval * 1s + kSleepBuffer + 60s);
  const auto initial_row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(initial_row_count, 1);
  ASSERT_LE(initial_row_count, 2);

  ASSERT_OK(cluster_->MoveTabletLeader(tablet_id_));

  // Wait 2 minutes.
  const auto default_sleep_min = 2;
  const auto buffer_sec = 3;
  const auto sleep_time_sec = kJobListRefreshInterval + (60 * default_sleep_min) + buffer_sec;
  SleepFor(sleep_time_sec * 1s);

  // We should have (default_sleep_min, default_sleep_min+1) rows.
  auto row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_GE(row_count, default_sleep_min + initial_row_count);
  ASSERT_LE(row_count, default_sleep_min + initial_row_count + 1);
}

TEST_F(PgCronTest, FailBeforeStoringLastMinute) {
  ASSERT_OK(WaitForDataInPgCronLeaderTable());

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_pg_cron_fail_setting_last_minute", "true"));

  ASSERT_OK(ScheduleJob("job1", "* * * * *", GetInsertQuery()));

  SleepFor(70s);

  auto succeeded_runs = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGUint64>(
      "SELECT COUNT(*) FROM cron.job_run_details WHERE status = 'succeeded' "));
  ASSERT_EQ(succeeded_runs, 0);

  auto failed_runs = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGUint64>(
      "SELECT COUNT(*) FROM cron.job_run_details WHERE status = 'failed' "));
  ASSERT_GT(failed_runs, 0);

  auto row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_EQ(row_count, 0);

  ASSERT_OK(cluster_->SetFlagOnTServers("TEST_pg_cron_fail_setting_last_minute", "false"));
  SleepFor(10s * kTimeMultiplier);

  row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_GT(row_count, 0);
}

TEST_F(PgCronTest, DeactivateRunningJob) {
  ASSERT_OK(Schedule1SecInsertJob());
  // Start a job that will run for a long time.
  const auto sleep_job_name = "Sleep Job";
  const auto sleep_job_id = ASSERT_RESULT(
      ScheduleJob(sleep_job_name, "1 second", "SELECT pg_sleep(1000)"));

  ASSERT_OK(WaitForRowCountAbove(0));
  ASSERT_OK(WaitForJobStatus(sleep_job_id, sleep_job_name, kRunningJobStatus));

  // Deactivating the sleep job should stop it immediately.
  ASSERT_OK(conn_->FetchFormat("SELECT cron.alter_job($0, active:=false)", sleep_job_id));

  // Wait for the change to get picked up and sleep job to get canceled.
  ASSERT_OK(WaitForJobStatus(sleep_job_id, sleep_job_name, kFailedJobStatus));

  // Make sure the other job is still running.
  const auto initial_row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_OK(WaitForRowCountAbove(initial_row_count));
}

// Test that a job that receives a SIGTERM doesn't affect other jobs, but a job that receives a
// SIGKILL causes a postmaster restart, interrupting all running jobs.
TEST_F(PgCronTest, KillRunningJob) {
  ASSERT_OK(Schedule1SecInsertJob());

  // Start jobs that will run for a long time.
  const auto sleep_job_1_name = "sleep job 1";
  const auto sleep_job_2_name = "sleep job 2";
  const auto sleep_job_1_id = ASSERT_RESULT(
      ScheduleJob(sleep_job_1_name, "3 seconds", "SELECT pg_sleep(1000)"));
  const auto sleep_job_2_id = ASSERT_RESULT(
      ScheduleJob(sleep_job_2_name, "3 seconds", "SELECT pg_sleep(999)"));

  auto wait_for_jobs_to_run =
      [this, sleep_job_1_id, sleep_job_2_id, sleep_job_1_name, sleep_job_2_name] () {
    ASSERT_OK(WaitForRowCountAbove(ASSERT_RESULT(GetRowCount())));
    ASSERT_OK(WaitForJobStatus(sleep_job_1_id, sleep_job_1_name, kRunningJobStatus));
    ASSERT_OK(WaitForJobStatus(sleep_job_2_id, sleep_job_2_name, kRunningJobStatus));
  };

  auto wait_for_failed_job_count = [this] (const int64 job_id, const std::string &job_name,
                                           const uint64 count) {
    return LoggedWaitFor(
        [this, job_id, count]() -> Result<bool> {
          return VERIFY_RESULT(conn_->FetchRow<pgwrapper::PGUint64>(Format(
              "SELECT COUNT(*) FROM cron.job_run_details WHERE jobid = $0 AND status = '$1'",
              job_id, kFailedJobStatus))) == count;
        },
        kTimeout,
        Format("Wait for job '$0' to have $1 '$2' entries", job_name, count, kFailedJobStatus));
  };

  wait_for_jobs_to_run();

  auto send_signal_to_job = [this] (int64 jobid, int signal) {
    const auto sleep_job_pid = ASSERT_RESULT(conn_->FetchRow<int>(Format(
        "SELECT job_pid FROM cron.job_run_details WHERE jobid = $0 AND status = 'running'",
        jobid)));

    LOG(INFO) << "Sending signal " << signal << " to the sleep job with pid " << sleep_job_pid;
    kill(sleep_job_pid, signal);
  };

  {
    send_signal_to_job(sleep_job_1_id, SIGTERM);

    // The killed job should fail and restart, but the other jobs should be uninterrupted
    ASSERT_OK(wait_for_failed_job_count(sleep_job_1_id, sleep_job_1_name, 1));
    ASSERT_OK(wait_for_failed_job_count(sleep_job_2_id, sleep_job_1_name, 0));

    wait_for_jobs_to_run();
  }

  {
    send_signal_to_job(sleep_job_1_id, SIGKILL);

    // Reconnect to the database after the failure.
    conn_ = std::make_unique<pgwrapper::PGConn>(ASSERT_RESULT(cluster_->ConnectToDB()));

    // Each running job should fail, and then restart. We don't check for a failure of the insert
    // job because it executes too quickly to guarantee that the SIGKILL occurs during its
    // execution.
    ASSERT_OK(wait_for_failed_job_count(sleep_job_1_id, sleep_job_1_name, 2));
    ASSERT_OK(wait_for_failed_job_count(sleep_job_2_id, sleep_job_1_name, 1));

    wait_for_jobs_to_run();
  }
}

TEST_F(PgCronTest, CancelJobOnLeaderChange) {
  // Disable load balancing to prevent interference from new system tablets.
  // When additional system tablets are added, the load balancer may move
  // the tablet leader back to the original node after an explicit leader move,
  // causing unexpected test failures.
  ASSERT_OK(cluster_->SetFlagOnMasters("enable_load_balancing", "false"));
  // Start a job that will run for a long time.
  ASSERT_OK(ScheduleJob("Sleep Job", "1 second", "SELECT pg_sleep(1000)"));
  ASSERT_OK(Schedule1SecInsertJob());

  ASSERT_OK(WaitForRowCountAbove(0));

  auto nodes_running_sleep_jobs = [this]() -> Result<std::set<size_t>> {
    std::set<size_t> nodes_running_job;
    for (size_t idx = 0; idx < cluster_->num_tablet_servers(); ++idx) {
      auto conn = VERIFY_RESULT(cluster_->ConnectToDB("yugabyte", idx));
      if (VERIFY_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
              "SELECT COUNT(*) FROM pg_stat_activity WHERE query = 'SELECT pg_sleep(1000)'")) > 0) {
        nodes_running_job.insert(idx);
      }
    }
    return nodes_running_job;
  };

  const auto initial_nodes_running_job = ASSERT_RESULT(nodes_running_sleep_jobs());
  ASSERT_EQ(initial_nodes_running_job.size(), 1);

  ASSERT_OK(cluster_->MoveTabletLeader(tablet_id_));

  // Wait for the jobs to get killed.
  SleepFor(kJobListRefreshInterval * 1s);

  // Wait for the new leader to start running.
  const auto initial_row_count = ASSERT_RESULT(GetRowCount());
  ASSERT_OK(WaitForRowCountAbove(initial_row_count));

  const auto final_nodes_running_job = ASSERT_RESULT(nodes_running_sleep_jobs());
  ASSERT_EQ(final_nodes_running_job.size(), 1);
  ASSERT_NE(*final_nodes_running_job.begin(), *initial_nodes_running_job.begin());

  const auto count_killed = ASSERT_RESULT(conn_->FetchRow<pgwrapper::PGUint64>(
      "SELECT COUNT(*) FROM cron.job_run_details WHERE return_message = 'pg_cron leader changed'"));
  ASSERT_TRUE(count_killed == 1 || count_killed == 2)
      << count_killed << " rows found when only 1 or 2 is expected";
}

}  // namespace yb
