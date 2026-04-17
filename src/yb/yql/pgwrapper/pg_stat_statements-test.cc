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

}  // namespace pgwrapper
}  // namespace yb
