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
#include "yb/util/test_thread_holder.h"
#include "yb/yql/pgwrapper/libpq_test_base.h"

using namespace std::chrono_literals;

namespace yb {
namespace pgwrapper {

class PgStatStatementsTest : public LibPqTestBase {
 public:
  int GetNumMasters() const override { return 1; }
  int GetNumTabletServers() const override { return 1; }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    options->extra_tserver_flags.push_back("--ysql_enable_auto_analyze=false");
  }
};

// Verify that pg_stat_statements never stores unnormalized query text
// when a pgss entry is reset between pgss_post_parse_analyze and
// pgss_ExecutorEnd.
TEST_F(PgStatStatementsTest, NormalizationAfterReset) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([&conn2] {
    ASSERT_OK(conn2.Fetch("SELECT pg_sleep(5), 'secret'"));
  });

  ASSERT_OK(LoggedWaitFor(
      [&conn1]() -> Result<bool> {
        auto n = VERIFY_RESULT(conn1.FetchRow<int64_t>(
            "SELECT count(*) FROM pg_stat_activity "
            "WHERE state = 'active' AND query LIKE '%secret%' "
            "AND query NOT LIKE '%pg_stat_activity%'"));
        return n > 0;
      },
      10s, "waiting for slow query to start"));

  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  thread_holder.JoinAll();

  // No unnormalized entry should be present.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%secret%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];
}

// Verify the fix also works with the extended query protocol, where the second
// and subsequent executions of a named prepared statement skip the Parse message.
// After a pgss reset, the Parse-created entry is gone but the queryId is still
// present in yb_pgss_queries_needing_normalization from the original Parse.
// pgss_store must not fall through to storing unnormalized text.
TEST_F(PgStatStatementsTest, NormalizationAfterResetExtendedProtocol) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  auto conn_str = Format("host=$0 port=$1 user=$2",
                         pg_ts->bind_host(), pg_ts->ysql_port(),
                         PGConnSettings::kDefaultUser);
  PGConnPtr raw_conn(PQconnectdb(conn_str.c_str()));
  ASSERT_EQ(PQstatus(raw_conn.get()), CONNECTION_OK);

  // Parse message: pgss_post_parse_analyze creates a normalized entry and
  // records the queryId in yb_pgss_queries_needing_normalization.
  PGResultPtr prep_result(PQprepare(
      raw_conn.get(), "test_stmt", "SELECT 1, 'secret'", 0, nullptr));
  ASSERT_EQ(PQresultStatus(prep_result.get()), PGRES_COMMAND_OK);

  // First Bind + Execute (no new Parse).
  PGResultPtr exec1(PQexecPrepared(
      raw_conn.get(), "test_stmt", 0, nullptr, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec1.get()), PGRES_TUPLES_OK);

  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Second Bind + Execute: Parse is skipped, so pgss_post_parse_analyze does
  // not run.  The queryId is still in yb_pgss_queries_needing_normalization from the
  // original Parse, but jstate is NULL.  The fix must discard the stats
  // rather than storing the unnormalized source text.
  PGResultPtr exec2(PQexecPrepared(
      raw_conn.get(), "test_stmt", 0, nullptr, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec2.get()), PGRES_TUPLES_OK);

  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%secret%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];
}

// Test interleaved Parse/Execute of multiple prepared statements with different
// normalization requirements.  The per-queryId hash set
// (yb_pgss_queries_needing_normalization) must correctly track each query independently
// so that after a pgss reset:
//   - Queries that needed normalization are discarded (not stored with raw
//     literal text).
//   - Queries that never needed normalization are stored normally.
TEST_F(PgStatStatementsTest, InterleavedExtendedProtocolAfterReset) {
  auto ctrl = ASSERT_RESULT(Connect());
  auto conn_str = Format("host=$0 port=$1 user=$2",
                         pg_ts->bind_host(), pg_ts->ysql_port(),
                         PGConnSettings::kDefaultUser);

  // ---------------------------------------------------------------------------
  // Scenario A: Parse [plain, norm], reset, execute plain.
  // The plain query has no constants and must be stored after reset.
  // A single-bool implementation would see the bool as true (set by norm's
  // Parse) and incorrectly discard the plain query's stats.
  // ---------------------------------------------------------------------------
  {
    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    ASSERT_EQ(PQstatus(conn.get()), CONNECTION_OK);

    PGResultPtr p1(PQprepare(
        conn.get(), "plain", "SELECT version()", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p1.get()), PGRES_COMMAND_OK);

    PGResultPtr p2(PQprepare(
        conn.get(), "norm", "SELECT 42, 'scenA_secret'", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p2.get()), PGRES_COMMAND_OK);

    // Initial execution to populate pgss entries.
    PGResultPtr e1(PQexecPrepared(
        conn.get(), "plain", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e1.get()), PGRES_TUPLES_OK);
    PGResultPtr e2(PQexecPrepared(
        conn.get(), "norm", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e2.get()), PGRES_TUPLES_OK);

    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));

    // Re-execute plain (no new Parse, jstate is NULL, entry is missing).
    PGResultPtr re(PQexecPrepared(
        conn.get(), "plain", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(re.get()), PGRES_TUPLES_OK);

    auto rows = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%version()%' "
        "AND query NOT LIKE '%pg_stat_statements%'"));
    ASSERT_FALSE(rows.empty())
        << "Scenario A: plain query should be stored after reset";
  }

  // ---------------------------------------------------------------------------
  // Scenario B: Parse [norm, plain], reset, execute norm.
  // The normalized query must NOT appear with unnormalized literal text.
  // ---------------------------------------------------------------------------
  {
    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    ASSERT_EQ(PQstatus(conn.get()), CONNECTION_OK);

    PGResultPtr p1(PQprepare(
        conn.get(), "norm", "SELECT 42, 'scenB_secret'", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p1.get()), PGRES_COMMAND_OK);

    PGResultPtr p2(PQprepare(
        conn.get(), "plain", "SELECT version()", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p2.get()), PGRES_COMMAND_OK);

    PGResultPtr e1(PQexecPrepared(
        conn.get(), "norm", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e1.get()), PGRES_TUPLES_OK);
    PGResultPtr e2(PQexecPrepared(
        conn.get(), "plain", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e2.get()), PGRES_TUPLES_OK);

    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));

    PGResultPtr re(PQexecPrepared(
        conn.get(), "norm", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(re.get()), PGRES_TUPLES_OK);

    auto rows = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%scenB_secret%'"));
    ASSERT_TRUE(rows.empty())
        << "Scenario B: normalized query must not be stored with "
        << "unnormalized text: " << rows[0];
  }

  // ---------------------------------------------------------------------------
  // Scenario C: Parse [norm1, plain, norm2], reset, execute all three in a
  // scrambled order (norm2, plain, norm1).
  // Both normalized queries must be discarded; the plain query must be stored.
  // ---------------------------------------------------------------------------
  {
    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    ASSERT_EQ(PQstatus(conn.get()), CONNECTION_OK);

    PGResultPtr p1(PQprepare(
        conn.get(), "norm1", "SELECT 42, 'scenC_secret1'", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p1.get()), PGRES_COMMAND_OK);

    PGResultPtr p2(PQprepare(
        conn.get(), "plain", "SELECT current_database()", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p2.get()), PGRES_COMMAND_OK);

    PGResultPtr p3(PQprepare(
        conn.get(), "norm2", "SELECT length('scenC_secret2')", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p3.get()), PGRES_COMMAND_OK);

    PGResultPtr e1(PQexecPrepared(
        conn.get(), "norm1", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e1.get()), PGRES_TUPLES_OK);
    PGResultPtr e2(PQexecPrepared(
        conn.get(), "plain", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e2.get()), PGRES_TUPLES_OK);
    PGResultPtr e3(PQexecPrepared(
        conn.get(), "norm2", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e3.get()), PGRES_TUPLES_OK);

    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));

    // Execute in a different order than they were parsed.
    PGResultPtr re3(PQexecPrepared(
        conn.get(), "norm2", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(re3.get()), PGRES_TUPLES_OK);
    PGResultPtr re2(PQexecPrepared(
        conn.get(), "plain", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(re2.get()), PGRES_TUPLES_OK);
    PGResultPtr re1(PQexecPrepared(
        conn.get(), "norm1", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(re1.get()), PGRES_TUPLES_OK);

    auto rows1 = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%scenC_secret1%'"));
    ASSERT_TRUE(rows1.empty())
        << "Scenario C: norm1 must not be stored with unnormalized text: "
        << rows1[0];

    auto rows2 = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%scenC_secret2%'"));
    ASSERT_TRUE(rows2.empty())
        << "Scenario C: norm2 must not be stored with unnormalized text: "
        << rows2[0];

    auto rows_p = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%current_database()%' "
        "AND query NOT LIKE '%pg_stat_statements%'"));
    ASSERT_FALSE(rows_p.empty())
        << "Scenario C: plain query should be stored after reset";
  }

  // ---------------------------------------------------------------------------
  // Scenario D: Repeated resets with the same prepared statement.
  // The per-queryId tracking must survive across multiple pgss resets within
  // one session: every re-execution after reset must discard stats.
  // ---------------------------------------------------------------------------
  {
    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    ASSERT_EQ(PQstatus(conn.get()), CONNECTION_OK);

    PGResultPtr p(PQprepare(
        conn.get(), "stmt", "SELECT 42, 'scenD_secret'", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p.get()), PGRES_COMMAND_OK);

    PGResultPtr e(PQexecPrepared(
        conn.get(), "stmt", 0, nullptr, nullptr, nullptr, 0));
    ASSERT_EQ(PQresultStatus(e.get()), PGRES_TUPLES_OK);

    for (int i = 0; i < 3; ++i) {
      ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));

      PGResultPtr re(PQexecPrepared(
          conn.get(), "stmt", 0, nullptr, nullptr, nullptr, 0));
      ASSERT_EQ(PQresultStatus(re.get()), PGRES_TUPLES_OK);

      auto rows = ASSERT_RESULT(ctrl.FetchRows<std::string>(
          "SELECT query FROM pg_stat_statements "
          "WHERE query LIKE '%scenD_secret%'"));
      ASSERT_TRUE(rows.empty())
          << "Scenario D iteration " << i
          << ": normalized query must not be stored with unnormalized text: "
          << rows[0];
    }
  }

  // ---------------------------------------------------------------------------
  // Scenario E: Interleave two normalized queries with a plain query, reset,
  // then execute each one multiple times.  Verifies that repeated executions
  // of multiple prepared statements all remain correct after reset.
  // ---------------------------------------------------------------------------
  {
    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));
    PGConnPtr conn(PQconnectdb(conn_str.c_str()));
    ASSERT_EQ(PQstatus(conn.get()), CONNECTION_OK);

    PGResultPtr p1(PQprepare(
        conn.get(), "n1", "SELECT 42, 'scenE_secret1'", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p1.get()), PGRES_COMMAND_OK);
    PGResultPtr p2(PQprepare(
        conn.get(), "n2", "SELECT length('scenE_secret2')", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p2.get()), PGRES_COMMAND_OK);
    PGResultPtr p3(PQprepare(
        conn.get(), "p1", "SELECT pg_backend_pid()", 0, nullptr));
    ASSERT_EQ(PQresultStatus(p3.get()), PGRES_COMMAND_OK);

    // Initial execution.
    for (const char *name : {"n1", "n2", "p1"}) {
      PGResultPtr e(PQexecPrepared(
          conn.get(), name, 0, nullptr, nullptr, nullptr, 0));
      ASSERT_EQ(PQresultStatus(e.get()), PGRES_TUPLES_OK);
    }

    ASSERT_OK(ctrl.Fetch("SELECT pg_stat_statements_reset()"));

    // Execute each statement three times in a round-robin pattern.
    for (int round = 0; round < 3; ++round) {
      for (const char *name : {"p1", "n2", "n1"}) {
        PGResultPtr re(PQexecPrepared(
            conn.get(), name, 0, nullptr, nullptr, nullptr, 0));
        ASSERT_EQ(PQresultStatus(re.get()), PGRES_TUPLES_OK);
      }
    }

    auto rows1 = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%scenE_secret1%'"));
    ASSERT_TRUE(rows1.empty())
        << "Scenario E: n1 must not be stored with unnormalized text: "
        << rows1[0];

    auto rows2 = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%scenE_secret2%'"));
    ASSERT_TRUE(rows2.empty())
        << "Scenario E: n2 must not be stored with unnormalized text: "
        << rows2[0];

    auto rows_p = ASSERT_RESULT(ctrl.FetchRows<std::string>(
        "SELECT query FROM pg_stat_statements "
        "WHERE query LIKE '%pg_backend_pid()%' "
        "AND query NOT LIKE '%pg_stat_statements%'"));
    ASSERT_FALSE(rows_p.empty())
        << "Scenario E: plain query should be stored after reset";
  }
}

// Verify that SET ROLE and SET SESSION AUTHORIZATION are recorded with their
// original text under the role they switch to, as upstream records them.
// Because they change the user, their stats must not be discarded as if their
// entry had been reset.
TEST_F(PgStatStatementsTest, SetRoleRecordedWithOriginalText) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Execute("CREATE ROLE test_role"));
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Each SET switches the current user to test_role, so its stats are stored
  // under test_role.
  ASSERT_OK(conn2.Execute("SET ROLE test_role"));
  ASSERT_OK(conn2.Execute("RESET ROLE"));
  ASSERT_OK(conn2.Execute("SET SESSION AUTHORIZATION test_role"));
  ASSERT_OK(conn2.Execute("RESET SESSION AUTHORIZATION"));

  auto rows = ASSERT_RESULT((conn1.FetchRows<std::string, int64_t>(
      "SELECT query, calls FROM pg_stat_statements "
      "WHERE userid = 'test_role'::regrole ORDER BY query")));
  ASSERT_EQ(rows, (decltype(rows){
      {"SET ROLE test_role", 1},
      {"SET SESSION AUTHORIZATION test_role", 1}}));

  ASSERT_OK(conn1.Execute("DROP ROLE test_role"));
}

// Verify that a prepared statement with constants is not stored with its
// literals when executed under a different user or nesting level than it was
// prepared under.  Its stats are discarded instead, a known limitation.
TEST_F(PgStatStatementsTest, PreparedStatementRunUnderAnotherKeyNotRecorded) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Execute("CREATE ROLE test_role"));
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Prepare under the current user, at top level.
  ASSERT_OK(conn2.Execute("PREPARE test_stmt AS SELECT 'secret'"));

  // Execute under a different user.
  ASSERT_OK(conn2.Execute("SET ROLE test_role"));
  ASSERT_OK(conn2.Fetch("EXECUTE test_stmt"));
  ASSERT_OK(conn2.Execute("RESET ROLE"));

  // Execute under the same user, but nested under EXPLAIN.
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'all'"));
  ASSERT_OK(conn2.Fetch(
      "EXPLAIN (ANALYZE, COSTS OFF, SUMMARY OFF, TIMING OFF) EXECUTE test_stmt"));
  ASSERT_OK(conn2.Execute("RESET pg_stat_statements.track"));

  // No unnormalized entry should be present.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%secret%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];

  // Known limitation: the stats are discarded rather than stored with the
  // normalized text.
  rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE 'PREPARE test_stmt%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an entry for the prepared statement: "
      << rows[0];

  ASSERT_OK(conn2.Execute("DEALLOCATE test_stmt"));
  ASSERT_OK(conn1.Execute("DROP ROLE test_role"));
}

// Verify that a statement with constants that switches the user through
// set_config is not stored with its literals.  Its execution stats are
// discarded instead, a known limitation.
TEST_F(PgStatStatementsTest, SetConfigRoleSwitchNotRecorded) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());

  ASSERT_OK(conn1.Execute("CREATE ROLE test_role"));
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  ASSERT_OK(conn2.Fetch("SELECT set_config('role', 'test_role', false), 'secret'"));
  ASSERT_OK(conn2.Execute("RESET ROLE"));

  // No unnormalized entry should be present.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%secret%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];

  // Known limitation: the execution stats are discarded.  Planning ran before
  // the switch, so the entry under the original user may still be shown, but
  // with no calls.
  auto num_executed = ASSERT_RESULT(conn1.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_stat_statements "
      "WHERE query LIKE '%set_config%' AND calls > 0 "
      "AND query NOT LIKE '%pg_stat_statements%'"));
  ASSERT_EQ(num_executed, 0);

  ASSERT_OK(conn1.Execute("DROP ROLE test_role"));
}

// Verify that a prepared statement whose only constants are parameters is still
// recorded after a reset, since its text has no literals to leak.
TEST_F(PgStatStatementsTest, ParamOnlyStatementRecordedAfterReset) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Use a raw libpq connection so the statement can be prepared once and run
  // again with Bind + Execute only, as drivers do.
  auto conn_str = Format("host=$0 port=$1 user=$2",
                         pg_ts->bind_host(), pg_ts->ysql_port(),
                         PGConnSettings::kDefaultUser);
  PGConnPtr raw_conn(PQconnectdb(conn_str.c_str()));
  ASSERT_EQ(PQstatus(raw_conn.get()), CONNECTION_OK);

  PGResultPtr prep_result(PQprepare(
      raw_conn.get(), "test_stmt", "SELECT $1::int AS param_probe", 1, nullptr));
  ASSERT_EQ(PQresultStatus(prep_result.get()), PGRES_COMMAND_OK);

  // First Bind + Execute (no new Parse).
  const char* values[] = {"1"};
  PGResultPtr exec1(PQexecPrepared(
      raw_conn.get(), "test_stmt", 1, values, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec1.get()), PGRES_TUPLES_OK);

  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Second Bind + Execute (no new Parse), after the reset.
  PGResultPtr exec2(PQexecPrepared(
      raw_conn.get(), "test_stmt", 1, values, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec2.get()), PGRES_TUPLES_OK);

  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%param_probe%' "
      "AND query NOT LIKE '%pg_stat_statements%'"));
  ASSERT_EQ(rows, std::vector<std::string>{"SELECT $1::int AS param_probe"});
}

// Verify that a prepared statement whose parameters form a squashed list is not
// recorded with its raw text after a reset.  Normalization collapses the list,
// so the raw text differs from the normalized text.
TEST_F(PgStatStatementsTest, SquashedParamListNotRecordedAfterReset) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Use a raw libpq connection so the statement can be prepared once and run
  // again with Bind + Execute only, as drivers do.
  auto conn_str = Format("host=$0 port=$1 user=$2",
                         pg_ts->bind_host(), pg_ts->ysql_port(),
                         PGConnSettings::kDefaultUser);
  PGConnPtr raw_conn(PQconnectdb(conn_str.c_str()));
  ASSERT_EQ(PQstatus(raw_conn.get()), CONNECTION_OK);

  PGResultPtr prep_result(PQprepare(
      raw_conn.get(), "test_stmt",
      "SELECT $1::int AS squash_probe WHERE $1::int IN ($2::int, $3::int, $4::int)",
      4, nullptr));
  ASSERT_EQ(PQresultStatus(prep_result.get()), PGRES_COMMAND_OK);

  // First Bind + Execute (no new Parse).
  const char* values[] = {"1", "1", "2", "3"};
  PGResultPtr exec1(PQexecPrepared(
      raw_conn.get(), "test_stmt", 4, values, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec1.get()), PGRES_TUPLES_OK);

  const std::string probe_query =
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%squash_probe%' "
      "AND query NOT LIKE '%pg_stat_statements%'";

  // Before the reset, the entry created at Parse holds the squashed text.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(probe_query));
  ASSERT_EQ(rows.size(), 1);
  ASSERT_STR_CONTAINS(rows[0], "/*, ... */");

  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Second Bind + Execute (no new Parse), after the reset: the stats must be
  // discarded rather than stored with the raw text.
  PGResultPtr exec2(PQexecPrepared(
      raw_conn.get(), "test_stmt", 4, values, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec2.get()), PGRES_TUPLES_OK);

  rows = ASSERT_RESULT(conn1.FetchRows<std::string>(probe_query));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];
}

// Verify that a statement prepared while it is not tracked is not stored with
// its literals when it is executed later at top level, where it is tracked.
TEST_F(PgStatStatementsTest, PreparedWhileUntrackedNotLeaked) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  // Track only top-level statements, so nested statements are not tracked.
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'top'"));

  ASSERT_OK(conn1.Execute("CREATE TABLE vault (v text)"));
  ASSERT_OK(conn1.Execute("INSERT INTO vault VALUES ('secret_1')"));
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Each statement below has a different shape, so it gets its own queryId.
  // With a shared queryId, the first statement's entry or mark would also cover
  // the later ones, and they would not test their own path.

  // Prepared inside a DO block, which is nested and so not tracked.  The value
  // comes from the table, so it is not in the DO statement's own text.
  ASSERT_OK(conn2.Execute(
      "DO $$ BEGIN EXECUTE format('PREPARE stmt1 AS SELECT %L', "
      "(SELECT v FROM vault)); END $$"));
  ASSERT_OK(conn2.Fetch("EXECUTE stmt1"));

  // Prepared while tracking is off.
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'none'"));
  ASSERT_OK(conn2.Execute("PREPARE stmt2 AS SELECT length('secret_2')"));
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'top'"));
  ASSERT_OK(conn2.Fetch("EXECUTE stmt2"));

  // Prepared with Parse while tracking is off, then run with Bind + Execute,
  // as drivers do.
  auto conn_str = Format("host=$0 port=$1 user=$2",
                         pg_ts->bind_host(), pg_ts->ysql_port(),
                         PGConnSettings::kDefaultUser);
  PGConnPtr raw_conn(PQconnectdb(conn_str.c_str()));
  ASSERT_EQ(PQstatus(raw_conn.get()), CONNECTION_OK);

  PGResultPtr set_none(PQexec(raw_conn.get(), "SET pg_stat_statements.track = 'none'"));
  ASSERT_EQ(PQresultStatus(set_none.get()), PGRES_COMMAND_OK);
  PGResultPtr prep_result(PQprepare(
      raw_conn.get(), "stmt3", "SELECT upper('secret_3')", 0, nullptr));
  ASSERT_EQ(PQresultStatus(prep_result.get()), PGRES_COMMAND_OK);
  PGResultPtr set_top(PQexec(raw_conn.get(), "SET pg_stat_statements.track = 'top'"));
  ASSERT_EQ(PQresultStatus(set_top.get()), PGRES_COMMAND_OK);
  PGResultPtr exec_result(PQexecPrepared(
      raw_conn.get(), "stmt3", 0, nullptr, nullptr, nullptr, 0));
  ASSERT_EQ(PQresultStatus(exec_result.get()), PGRES_TUPLES_OK);

  // No unnormalized entry should be present.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%secret%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];
}

// Verify that a statement inside a function is not stored with its literals
// when its plan, cached while nested statements were not tracked, runs again
// after they are.
TEST_F(PgStatStatementsTest, FunctionPlanCachedWhileUntrackedNotLeaked) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  // Track only top-level statements, so nested statements are not tracked.
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'top'"));

  ASSERT_OK(conn1.Execute("CREATE TABLE test_table (a text)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test_table VALUES ('x')"));
  ASSERT_OK(conn1.Execute(
      "CREATE FUNCTION test_func() RETURNS text LANGUAGE plpgsql AS $$ "
      "DECLARE v text; "
      "BEGIN SELECT a INTO v FROM test_table WHERE a <> 'secret' LIMIT 1; "
      "RETURN v; END $$"));
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  // Only top-level statements are tracked, so the statement inside the
  // function is parsed and cached without being tracked.
  ASSERT_OK(conn2.Fetch("SELECT test_func()"));

  // Track nested statements too.  The cached plan runs without a new parse.
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'all'"));
  ASSERT_OK(conn2.Fetch("SELECT test_func()"));
  ASSERT_OK(conn2.Execute("RESET pg_stat_statements.track"));

  // No unnormalized entry should be present.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%secret%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];

  // The top-level calls are still recorded.
  auto calls = ASSERT_RESULT(conn1.FetchRow<int64_t>(
      "SELECT calls FROM pg_stat_statements WHERE query = 'SELECT test_func()'"));
  ASSERT_EQ(calls, 2);
}

// Verify that a prepared statement is not stored with its literals when a
// table change makes it be parsed again as a nested, untracked statement, and
// it is then executed at top level.
TEST_F(PgStatStatementsTest, ReparsedWhileUntrackedNotLeaked) {
  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  // Track only top-level statements, so nested statements are not tracked.
  ASSERT_OK(conn2.Execute("SET pg_stat_statements.track = 'top'"));

  ASSERT_OK(conn1.Execute("CREATE TABLE test_table (a text)"));
  ASSERT_OK(conn1.Execute("INSERT INTO test_table VALUES ('424242')"));
  ASSERT_OK(conn1.Fetch("SELECT pg_stat_statements_reset()"));

  ASSERT_OK(conn2.Execute(
      "PREPARE test_stmt AS SELECT count(*) FROM test_table WHERE a = '424242'"));
  ASSERT_OK(conn2.Fetch("EXECUTE test_stmt"));

  // The new column type changes the statement's shape.  EXPLAIN parses it again
  // as a nested statement, which is not tracked.
  ASSERT_OK(conn2.Execute(
      "ALTER TABLE test_table ALTER COLUMN a TYPE int USING a::int"));
  ASSERT_OK(conn2.Fetch("EXPLAIN (COSTS OFF) EXECUTE test_stmt"));
  ASSERT_OK(conn2.Fetch("EXECUTE test_stmt"));

  // No unnormalized entry should be present.
  auto rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query FROM pg_stat_statements "
      "WHERE query LIKE '%424242%'"));
  ASSERT_TRUE(rows.empty())
      << "pg_stat_statements contains an unnormalized entry: " << rows[0];

  // The execution before the table change is recorded with normalized text.
  rows = ASSERT_RESULT(conn1.FetchRows<std::string>(
      "SELECT query || ' calls=' || calls FROM pg_stat_statements "
      "WHERE query LIKE 'PREPARE test_stmt%'"));
  ASSERT_EQ(rows, std::vector<std::string>{
      "PREPARE test_stmt AS SELECT count(*) FROM test_table WHERE a = $1 calls=1"});
}

}  // namespace pgwrapper
}  // namespace yb
