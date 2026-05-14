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

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug.h"
#include "yb/util/monotime.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pgwrapper {

namespace {

class PgGlobalViewsTest : public LibPqTestBase {
 public:
  virtual ~PgGlobalViewsTest() = default;

  void SetUp() override {
    LibPqTestBase::SetUp();
    conn_ = ASSERT_RESULT(ConnectToDB(kInitialDB));
    ASSERT_OK(conn_->Execute("CREATE EXTENSION postgres_fdw"));
    ASSERT_OK(conn_->Execute(
        "CREATE SERVER IF NOT EXISTS gv_server FOREIGN DATA WRAPPER postgres_fdw "
        "OPTIONS (server_type 'federatedYugabyteDB')"));
    ASSERT_OK(SetupGlobalView());
    ASSERT_OK(LoadData());
  }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=yb_enable_global_views=true");
    options->extra_tserver_flags.push_back(Format(
        "--remote_pg_query_execution_rpc_timeout_ms=$0", kGlobalViewsRpcTimeoutMs));
    options->extra_tserver_flags.push_back("--vmodule=tablet_service=1");
  }

 protected:
  Status SetupGlobalView() {
    RETURN_NOT_OK(conn_->Execute(R"(
        CREATE VIEW partial_pgss_with_tserver_uuid AS
            SELECT
                yb_get_local_tserver_uuid() AS tserver_uuid,
                queryid,
                query,
                calls
            FROM pg_stat_statements)"));
    RETURN_NOT_OK(conn_->Execute(R"(
        CREATE FOREIGN TABLE IF NOT EXISTS "gv$partial_pg_stat_statements" (
            tserver_uuid UUID,
            queryid BIGINT,
            query TEXT,
            calls BIGINT
        )
        SERVER gv_server
        OPTIONS (schema_name 'public', table_name 'partial_pgss_with_tserver_uuid'))"));
    RETURN_NOT_OK(conn_->Execute(R"(
        CREATE FOREIGN TABLE IF NOT EXISTS "gv$partial_ash" (
            sample_time TIMESTAMPTZ,
            root_request_id UUID,
            rpc_request_id BIGINT,
            wait_event_component TEXT,
            wait_event_class TEXT,
            wait_event TEXT,
            top_level_node_id UUID,
            query_id BIGINT,
            pid INT,
            client_node_ip TEXT,
            wait_event_aux TEXT,
            sample_weight REAL,
            wait_event_type TEXT,
            ysql_dbid OID,
            wait_event_code BIGINT,
            pss_mem_bytes BIGINT,
            ysql_userid OID
        )
        SERVER gv_server
        OPTIONS (schema_name 'pg_catalog', table_name 'yb_active_session_history'))"));
    RETURN_NOT_OK(conn_->Execute(R"(
        CREATE VIEW partial_pg_stat_all_tables AS
            SELECT
                schemaname,
                relname
            FROM pg_stat_all_tables)"));
    return conn_->Execute(R"(
        CREATE FOREIGN TABLE IF NOT EXISTS "gv$partial_pg_stat_all_tables" (
            schemaname NAME,
            relname NAME
        )
        SERVER gv_server
        OPTIONS (schema_name 'public', table_name 'partial_pg_stat_all_tables'))");
  }

  Status LoadData() {
    {
      auto conn = VERIFY_RESULT(Connect());
      RETURN_NOT_OK(conn.Execute("CREATE TABLE tbl (k INT)"));
    }
    int tserver_idx = 1;
    for (auto* ts : cluster_->tserver_daemons()) {
      auto conn = VERIFY_RESULT(ConnectToTs(*ts));
      for (int j = 0; j < tserver_idx; ++j) {
        RETURN_NOT_OK(conn.ExecuteFormat(kInsertQuery, 1));
      }
      ++tserver_idx;
    }
    return Status::OK();
  }

  // Verify the global view returns expected results, optionally excluding
  // specific tserver indices (e.g., one that is down or timing out).
  Status VerifyGlobalViewResultsForPgss(
      const std::unordered_set<int>& excluded_tservers = {}) {
    return VerifyGlobalViewResultsForPgss(*conn_, excluded_tservers);
  }

  Status VerifyGlobalViewResultsForPgss(
      PGConn& conn, const std::unordered_set<int>& excluded_tservers = {}) {
    std::vector<std::tuple<Uuid, std::string, int64_t>> expected_result;
    for (int i = 0; i < GetNumTabletServers(); ++i) {
      if (excluded_tservers.contains(i)) {
        continue;
      }
      expected_result.emplace_back(
          VERIFY_RESULT(Uuid::FromHexStringBigEndian(cluster_->tablet_server(i)->uuid())),
          Format(kInsertQuery, "$1"),
          i + 1);
    }

    auto res = VERIFY_RESULT((conn.FetchRows<Uuid, std::string, int64_t>(R"(
        SELECT tserver_uuid, query, calls FROM gv$partial_pg_stat_statements
        WHERE query LIKE 'INSERT INTO tbl %'
        ORDER BY calls ASC)")));
    SCHECK_EQ(expected_result, res, IllegalState,
        "Global view results do not match expected results");
    return Status::OK();
  }

  void VerifyQueryPushdowns(
      TestThreadHolder* thread_holder, const std::string& wait_string) {
    // Append "\n" so the substring match only succeeds when wait_string is the
    // exact suffix of the log line, i.e. the complete pushed-down query.
    auto exact_wait_string = Format("$0\n", wait_string);
    for (int i = 0; i < GetNumTabletServers(); ++i) {
      thread_holder->AddThreadFunctor([this, exact_wait_string, i]() {
        ASSERT_OK(LogWaiter(cluster_->tablet_server(i), exact_wait_string).WaitFor(30s));
      });
    }
  }

  static constexpr auto kInsertQuery = "INSERT INTO tbl (k) VALUES ($0)";
  static constexpr auto kInitialDB = "template1";
  static constexpr auto kGlobalViewsRpcTimeoutMs = 2000 * kTimeMultiplier;
  std::optional<PGConn> conn_;
};

class PgGlobalViewsExceedRpcMaxSizeTest : public PgGlobalViewsTest {
 public:
  virtual ~PgGlobalViewsExceedRpcMaxSizeTest() = default;

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    // rpc_max_message_size must be at least 3.5 MB for queries to work properly.
    // Use a smaller ASH buffer so it fills and wraps faster (important for
    // debug builds), while keeping the RPC limit at the required minimum.
    constexpr auto kAshBufferSizeKiB = 2048;
    constexpr auto kRpcMaxMessageSize = 3584 * 1024;
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=yb_enable_global_views=true");
    options->extra_tserver_flags.push_back(Format(
        "--ysql_yb_ash_circular_buffer_size=$0", kAshBufferSizeKiB));
    options->extra_tserver_flags.push_back(
        "--ysql_yb_ash_sampling_interval_ms=50");
    options->extra_tserver_flags.push_back(Format(
        "--rpc_max_message_size=$0", kRpcMaxMessageSize));
    options->extra_tserver_flags.push_back(Format(
        "--consensus_max_batch_size_bytes=$0", kRpcMaxMessageSize - 2048));
  }
};

} // anonymous namespace

TEST_F(PgGlobalViewsTest, TestDataFromEachNode) {
  ASSERT_OK(VerifyGlobalViewResultsForPgss());
}

TEST_F(PgGlobalViewsTest, TestLimitIsNotPushedDown) {
  TestThreadHolder log_waiter_threads;
  VerifyQueryPushdowns(&log_waiter_threads,
    "SELECT query, calls "
    "FROM public.partial_pgss_with_tserver_uuid "
    "WHERE ((query ~~ 'INSERT INTO tbl %')) ORDER BY calls DESC NULLS FIRST");

  auto res = ASSERT_RESULT((conn_->FetchRow<std::string, int64_t>(R"(
      SELECT query, calls FROM gv$partial_pg_stat_statements
      WHERE query LIKE 'INSERT INTO tbl %'
      ORDER BY calls DESC LIMIT 1)")));
  ASSERT_EQ(res, (decltype(res){Format(kInsertQuery, "$1"), 3}));
}

TEST_F(PgGlobalViewsTest, TestDistinctIsNotPushedDown) {
  TestThreadHolder log_waiter_threads;
  VerifyQueryPushdowns(&log_waiter_threads,
    "SELECT query "
    "FROM public.partial_pgss_with_tserver_uuid");

  auto res = ASSERT_RESULT((conn_->FetchRows<std::string>(
      "SELECT DISTINCT(query) FROM gv$partial_pg_stat_statements")));
  std::unordered_set<std::string> distinct_set;
  for (const auto& query : res) {
    distinct_set.insert(query);
  }
  ASSERT_EQ(distinct_set.size(), res.size());
}

TEST_F(PgGlobalViewsTest, TestGroupByIsNotPushedDown) {
  TestThreadHolder log_waiter_threads;
  VerifyQueryPushdowns(&log_waiter_threads,
    "SELECT query, calls "
    "FROM public.partial_pgss_with_tserver_uuid "
    "WHERE ((query ~~ 'INSERT INTO tbl %'))");

  auto res = ASSERT_RESULT((conn_->FetchRow<std::string, int64_t>(R"(
      SELECT query, SUM(calls)::BIGINT
      FROM gv$partial_pg_stat_statements
      WHERE query LIKE 'INSERT INTO tbl %'
      GROUP BY query)")));
  ASSERT_EQ(res, (decltype(res){Format(kInsertQuery, "$1"), 6}));
}

TEST_F(PgGlobalViewsTest, TestJoinsAreNotPushedDown) {
  // add some samples in buffer of each tserver
  for (auto* ts : cluster_->tserver_daemons()) {
    auto conn = ASSERT_RESULT(ConnectToTs(*ts));
    for (int j = 0; j < 100; ++j) {
      ASSERT_OK(conn.Fetch("SELECT * FROM tbl"));
      SleepFor(10ms);
    }
  }

  // there can only be one log waiter at a time, so we need to run the tests sequentially
  for (const auto& log_pattern : {
      "SELECT queryid, query FROM public.partial_pgss_with_tserver_uuid",
      "SELECT sample_time, wait_event_component, query_id, wait_event_type "
      "FROM pg_catalog.yb_active_session_history" }) {
    TestThreadHolder log_waiter_threads;
    VerifyQueryPushdowns(&log_waiter_threads, log_pattern);
    auto res = ASSERT_RESULT((
        conn_->FetchRows<std::string, std::string, std::string, int64_t>(R"(
      SELECT query, wait_event_component, wait_event_type, COUNT(*)
      FROM gv$partial_ash
      JOIN gv$partial_pg_stat_statements
      ON query_id = queryid
      WHERE sample_time >= current_timestamp - interval '20 minutes'
      GROUP BY query, wait_event_component, wait_event_type
      ORDER BY query, wait_event_component, wait_event_type
      LIMIT 10)")));
  }
}

// Test that the result from any db should be same
TEST_F(PgGlobalViewsTest, TestQueryingFromAnyDb) {
  constexpr auto kDbName = "db";
  constexpr auto kColocatedDbName = "colocated_db";
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", kDbName));
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0 COLOCATED = TRUE", kColocatedDbName));

  std::optional<std::tuple<std::string, int64_t>> prev_result;
  for (const auto& db : {kDbName, kColocatedDbName}) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db));
    auto result = ASSERT_RESULT((conn.FetchRow<std::string, int64_t>(R"(
        SELECT query, calls FROM gv$partial_pg_stat_statements
        WHERE query LIKE 'INSERT INTO tbl %'
        ORDER BY calls DESC LIMIT 1)")));
    if (prev_result) {
      ASSERT_EQ(*prev_result, result);
    }
    prev_result = result;
  }
}

TEST_F(PgGlobalViewsTest, TestPgStatAllTablesIsDbSpecific) {
  constexpr auto kDb1Name = "db1";
  constexpr auto kDb2Name = "db2";
  constexpr auto kDb1TableName = "db1_only_tbl";
  constexpr auto kDb2TableName = "db2_only_tbl";

  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", kDb1Name));
  ASSERT_OK(conn_->ExecuteFormat("CREATE DATABASE $0", kDb2Name));

  auto db1_conn = ASSERT_RESULT(cluster_->ConnectToDB(kDb1Name));
  auto db2_conn = ASSERT_RESULT(cluster_->ConnectToDB(kDb2Name));
  ASSERT_OK(db1_conn.ExecuteFormat("CREATE TABLE $0 (k INT)", kDb1TableName));
  ASSERT_OK(db2_conn.ExecuteFormat("CREATE TABLE $0 (k INT)", kDb2TableName));

  const auto count_table_rows = [](PGConn& conn, const std::string& relname) -> Result<int64_t> {
    return conn.FetchRow<int64_t>(Format(R"#(
        SELECT COUNT(*) FROM "gv$$partial_pg_stat_all_tables"
        WHERE relname = '$0')#", relname));
  };

  ASSERT_EQ(ASSERT_RESULT(count_table_rows(db1_conn, kDb1TableName)), GetNumTabletServers());
  ASSERT_EQ(ASSERT_RESULT(count_table_rows(db1_conn, kDb2TableName)), 0);
  ASSERT_EQ(ASSERT_RESULT(count_table_rows(db2_conn, kDb2TableName)), GetNumTabletServers());
  ASSERT_EQ(ASSERT_RESULT(count_table_rows(db2_conn, kDb1TableName)), 0);
}

TEST_F(PgGlobalViewsTest, TestOneTServerTimingOut) {
  constexpr auto kTserverIdxTimingOut = 1;
  ASSERT_OK(cluster_->SetFlag(
      cluster_->tablet_server(kTserverIdxTimingOut),
      "TEST_pause_remote_pg_query_execution_ms",
      std::to_string(2 * kGlobalViewsRpcTimeoutMs)));

  ASSERT_OK(VerifyGlobalViewResultsForPgss({kTserverIdxTimingOut}));

  // Without this sleep, the test takes longer to end because some
  // tserver threads wait for more time than this sleep duration.
  SleepFor(2ms * kGlobalViewsRpcTimeoutMs);
}

TEST_F(PgGlobalViewsTest, TestOneTServerDown) {
  constexpr auto kTserverIdxDown = 1;
  cluster_->tablet_server(kTserverIdxDown)->Shutdown();
  ASSERT_OK(VerifyGlobalViewResultsForPgss({kTserverIdxDown}));
}

TEST_F(PgGlobalViewsTest, TestPermissions) {
  constexpr auto kUser = "test_gv_user";
  constexpr auto kQuery =
      "SELECT query, calls FROM gv$partial_pg_stat_statements "
      "WHERE query LIKE 'INSERT INTO tbl %' LIMIT 1";

  ASSERT_OK(conn_->ExecuteFormat("CREATE USER $0", kUser));
  ASSERT_OK(conn_->ExecuteFormat(
      "GRANT SELECT ON \"gv$$partial_pg_stat_statements\" TO $0", kUser));

  // User has doesn't have pg_read_all_stats
  auto user_conn = ASSERT_RESULT(ConnectToDBAsUser(kInitialDB, kUser));
  auto result = user_conn.Fetch(kQuery);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().message().ToBuffer(), "permission denied");

  // Grant pg_read_all_stats and verify access is allowed.
  ASSERT_OK(conn_->ExecuteFormat("GRANT pg_read_all_stats TO $0", kUser));
  user_conn = ASSERT_RESULT(ConnectToDBAsUser(kInitialDB, kUser));
  ASSERT_OK(user_conn.Fetch(kQuery));

  // Revoke and verify access is denied again.
  ASSERT_OK(conn_->ExecuteFormat("REVOKE pg_read_all_stats FROM $0", kUser));
  user_conn = ASSERT_RESULT(ConnectToDBAsUser(kInitialDB, kUser));
  result = user_conn.Fetch(kQuery);
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().message().ToBuffer(), "permission denied");
}

TEST_F(PgGlobalViewsTest, TestTserverDownBetweenPrepareAndExecute) {
  ASSERT_OK(conn_->Execute(R"(
      PREPARE gv_stmt AS
      SELECT tserver_uuid, query, calls FROM gv$partial_pg_stat_statements
      WHERE query LIKE 'INSERT INTO tbl %'
      ORDER BY calls ASC)"));

  constexpr auto kTserverIdxDown = 1;
  cluster_->tablet_server(kTserverIdxDown)->Shutdown();

  std::vector<std::string> warnings;
  conn_->SetNoticeProcessor(
      [](void* arg, const char* message) {
        static_cast<std::vector<std::string>*>(arg)->emplace_back(message);
      },
      &warnings);

  auto res = ASSERT_RESULT(
      (conn_->FetchRows<Uuid, std::string, int64_t>("EXECUTE gv_stmt")));

  // Verify partial results: only surviving tservers return rows.
  std::vector<std::tuple<Uuid, std::string, int64_t>> expected;
  for (int i = 0; i < GetNumTabletServers(); ++i) {
    if (i == kTserverIdxDown) {
      continue;
    }
    expected.emplace_back(
        ASSERT_RESULT(Uuid::FromHexStringBigEndian(cluster_->tablet_server(i)->uuid())),
        Format(kInsertQuery, "$1"),
        i + 1);
  }
  ASSERT_EQ(expected, res);

  // Verify exactly one warning was emitted for the downed tserver.
  ASSERT_EQ(warnings.size(), 1);
  auto down_uuid = cluster_->tablet_server(kTserverIdxDown)->uuid();
  ASSERT_STR_CONTAINS(warnings[0], Format("global view: skipping tserver $0", down_uuid));
}

TEST_F(PgGlobalViewsExceedRpcMaxSizeTest, YB_DISABLE_TEST_IN_SANITIZERS(TestDataExceedsRpcSize)) {
  const auto kNumTservers = GetNumTabletServers();

  // Run multiple concurrent connections per tserver to generate enough ASH samples
  // to fill and wrap the circular buffer quickly.
  constexpr auto kConnectionsPerTserver = 10;

  static constexpr auto kSleepDuration = 300;

  TestThreadHolder thread_holder;
  for (int i = 0; i < kNumTservers; ++i) {
    for (int j = 0; j < kConnectionsPerTserver; ++j) {
      thread_holder.AddThreadFunctor([this,  &stop = thread_holder.stop_flag(), i] {
        auto conn = ASSERT_RESULT(ConnectToTs(*cluster_->tablet_server(i)));
        ASSERT_OK(conn.FetchFormat("SELECT pg_sleep($0)", kSleepDuration));
      });
    }
  }
  // Wait for the ASH circular buffer to fill and wrap around on each tserver.
  // We detect wrapping by observing that MIN(sample_time) increases - once the
  // buffer is full, new samples overwrite the oldest ones, causing the minimum
  // to shift forward.
  std::vector<MonoDelta> initial_min_times(kNumTservers);
  std::vector<PGConn> ts_conns;
  for (int i = 0; i < kNumTservers; ++i) {
    ts_conns.push_back(ASSERT_RESULT(ConnectToTsForDB(*cluster_->tablet_server(i), kInitialDB)));
    ASSERT_OK(LoggedWaitFor([&, i]() -> Result<bool> {
      auto count = VERIFY_RESULT(ts_conns[i].FetchRow<PGUint64>(
          "SELECT COUNT(*) FROM yb_active_session_history"));
      return count > 0;
    }, 60s, Format("Waiting for initial ASH samples on tserver $0", i)));

    initial_min_times[i] = ASSERT_RESULT(ts_conns[i].FetchRow<MonoDelta>(
        "SELECT MIN(sample_time) FROM yb_active_session_history"));
  }

  // Now wait for MIN(sample_time) to advance on each tserver, meaning old samples
  // were overwritten and the circular buffer has wrapped around.
  for (int i = 0; i < kNumTservers; ++i) {
    ASSERT_OK(LoggedWaitFor([&, i]() -> Result<bool> {
      auto min_time = VERIFY_RESULT(ts_conns[i].FetchRow<MonoDelta>(
          "SELECT MIN(sample_time) FROM yb_active_session_history"));
      return min_time > initial_min_times[i];
    }, MonoDelta::FromSeconds(kSleepDuration),
       Format("Waiting for ASH buffer to wrap on tserver $0", i)));
  }

  TestThreadHolder log_waiter_threads;
  constexpr auto kWaitString = "Reached max RPC size limit for remote pg exec query. "
      "Received truncated response.";

  for (int i = 0; i < kNumTservers; ++i) {
    log_waiter_threads.AddThreadFunctor([this, i]() {
      ASSERT_OK(LogWaiter(cluster_->tablet_server(i), kWaitString).WaitFor(30s));
    });
  }

  for (auto& conn : ts_conns) {
    ASSERT_OK(conn.Fetch("SELECT * FROM gv$partial_ash"));
  }
}

} // namespace yb::pgwrapper
