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

// Tests that writes and commits issued on behalf of a YSQL backend carry the right origination
// stamp (database, origination time).  The stamp is observed through
// TEST_persistence_reject_stamped_before_ht, which rejects stamped ops whose origination time is
// below a threshold and passes unstamped ones: with the origination time pinned to P by
// yb_origination_time_override, a threshold of P + 1us rejects an op stamped with P, and a
// threshold of P lets it through.

#include <fstream>
#include <regex>

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/table_handle.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/pg_types.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/path_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_uint64(TEST_persistence_reject_stamped_before_ht);
DECLARE_bool(TEST_persistence_dfatal_unstamped_pgsql_write);
DECLARE_bool(ysql_yb_enable_listen_notify);
DECLARE_bool(ysql_disable_index_backfill);

namespace yb::pgwrapper {

namespace {

// Arbitrary time in the past (2023-11-14), used as the pinned origination time.
constexpr MicrosTime kPinnedOriginationTime = 1'700'000'000'000'000;
constexpr MicrosTime kLaterOriginationTime = kPinnedOriginationTime + 1'000'000;

}  // namespace

class PgPersistenceStampingTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
    // Set after cluster start so initdb, which runs unstamped, is not checked.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_persistence_dfatal_unstamped_pgsql_write) = true;
  }

  void DoTearDown() override {
    RejectBelow(0);
    PgMiniTestBase::DoTearDown();
  }

  // Rejects stamped ops whose origination time is below threshold; 0 turns rejection off.
  static void RejectBelow(MicrosTime threshold) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_persistence_reject_stamped_before_ht) =
        threshold ? HybridTime::FromMicros(threshold).ToUint64() : 0;
  }

  static Status PinOriginationTime(
      PGConn& conn, MicrosTime origination_time = kPinnedOriginationTime) {
    return conn.ExecuteFormat("SET yb_origination_time_override = $0", origination_time);
  }

  // Rejects ops stamped with kPinnedOriginationTime (or earlier).
  static void RejectPinned() { RejectBelow(kPinnedOriginationTime + 1); }
  // Accepts ops stamped with kPinnedOriginationTime (still rejects earlier ones).
  static void AcceptPinned() { RejectBelow(kPinnedOriginationTime); }
  static void RejectEverythingStamped() { RejectBelow(GetCurrentTimeMicros() + 3600'000'000); }
  static void RejectStampedBeforeNow() { RejectBelow(GetCurrentTimeMicros()); }

  static Result<PgOid> DatabaseOid(PGConn& conn) {
    return conn.FetchRow<PGOid>("SELECT oid FROM pg_database WHERE datname = current_database()");
  }

  // Expects status to be the TEST rejection of an op stamped with database db_oid.
  static void ExpectRejected(const Status& status, std::optional<PgOid> db_oid = std::nullopt) {
    ASSERT_NOK(status);
    ASSERT_STR_CONTAINS(status.ToString(), "TEST: persistence rejected");
    if (db_oid) {
      ASSERT_STR_CONTAINS(status.ToString(), Format("connected database $0 ", *db_oid));
    }
  }

  static Status CreateTable(PGConn& conn, const std::string& name = "t") {
    return conn.ExecuteFormat(
        "CREATE TABLE $0 (k INT PRIMARY KEY, v INT) SPLIT INTO 3 TABLETS", name);
  }

  // Runs statement on conn in a separate thread.  Once the statement has committed at least
  // min_row_count rows of t (as seen from another connection), rejects every stamped op originated
  // before now.  Returns the statement's status and t's final row count.  A statement that
  // keeps its origination time across its internal commits is rejected at the next commit; one
  // that refreshed it would carry a time after the threshold and run to completion.
  Result<std::pair<Status, int64_t>> RunAndRejectMidway(
      PGConn& conn, const std::string& statement, int64_t min_row_count) {
    auto observer_conn = VERIFY_RESULT(Connect());
    Status statement_status;
    TestThreadHolder thread_holder;
    thread_holder.AddThreadFunctor(
        [&conn, &statement, &statement_status] { statement_status = conn.Execute(statement); });
    RETURN_NOT_OK(WaitFor(
        [&observer_conn, min_row_count]() -> Result<bool> {
          return VERIFY_RESULT(observer_conn.FetchRow<int64_t>("SELECT count(*) FROM t")) >=
                 min_row_count;
        },
        30s, "statement committed some rows"));
    RejectStampedBeforeNow();
    thread_holder.JoinAll();
    RejectBelow(0);
    auto row_count = VERIFY_RESULT(observer_conn.FetchRow<int64_t>("SELECT count(*) FROM t"));
    return std::make_pair(statement_status, row_count);
  }
};

// Checks that a single-row write, which runs as a single-shard (fast-path) write, is stamped.
TEST_F(PgPersistenceStampingTest, FastPathWrite) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  RejectPinned();
  ExpectRejected(conn.Execute("INSERT INTO t VALUES (1, 1)"), db_oid);
  AcceptPinned();
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
}

// Checks that the writes of a multi-tablet INSERT, run as a distributed transaction, are stamped.
TEST_F(PgPersistenceStampingTest, DistributedTransactionWrite) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  // The rows span several of t's tablets, so the INSERT runs in a distributed transaction rather
  // than as a single-shard write.
  RejectPinned();
  ExpectRejected(
      conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 10) AS i"), db_oid);
  AcceptPinned();
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 10) AS i"));
}

// Checks that a transaction's commit is stamped.
TEST_F(PgPersistenceStampingTest, RowLockOnlyTransactionCommit) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  // The transaction's only intents are row locks, which the threshold does not check, so its
  // commit is the only op that can be refused.
  RejectPinned();
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Fetch("SELECT * FROM t WHERE k = 1 FOR UPDATE"));
  ExpectRejected(conn.CommitTransaction(), db_oid);

  AcceptPinned();
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Fetch("SELECT * FROM t WHERE k = 1 FOR UPDATE"));
  ASSERT_OK(conn.CommitTransaction());
}

// Checks that a transaction block's writes and commit carry its BEGIN's origination time, even if
// the override changes inside the block.
TEST_F(PgPersistenceStampingTest, BlockCarriesBeginTime) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(PinOriginationTime(conn));

  // The write.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(PinOriginationTime(conn, kLaterOriginationTime));
  RejectPinned();
  ExpectRejected(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  // The commit.
  RejectBelow(0);
  ASSERT_OK(PinOriginationTime(conn));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(PinOriginationTime(conn, kLaterOriginationTime));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (2, 2)"));
  RejectPinned();
  ExpectRejected(conn.Execute("COMMIT"));
}

// Checks that a nested BEGIN and savepoints keep the outer BEGIN's origination time.
TEST_F(PgPersistenceStampingTest, NestedBeginAndSavepoints) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(PinOriginationTime(conn));

  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(PinOriginationTime(conn, kLaterOriginationTime));
  // A nested BEGIN is a no-op inside a block, and a savepoint starts a subtransaction rather than
  // a transaction.
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT s1"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT s1"));
  ASSERT_OK(conn.Execute("SAVEPOINT s2"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (2, 2)"));
  ASSERT_OK(conn.Execute("ROLLBACK TO SAVEPOINT s2"));
  RejectPinned();
  ExpectRejected(conn.Execute("INSERT INTO t VALUES (3, 3)"));
  ASSERT_OK(conn.Execute("ROLLBACK"));

  RejectBelow(0);
  ASSERT_OK(PinOriginationTime(conn));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(PinOriginationTime(conn, kLaterOriginationTime));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("SAVEPOINT s1"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));
  ASSERT_OK(conn.Execute("RELEASE SAVEPOINT s1"));
  RejectPinned();
  ExpectRejected(conn.Execute("COMMIT"));
}

// Checks that a statement sent through the extended protocol is stamped.
TEST_F(PgPersistenceStampingTest, ExtendedProtocol) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(PinOriginationTime(conn));

  // Fetch with parameters goes through Parse/Bind/Execute/Sync.
  RejectPinned();
  ExpectRejected(ResultToStatus(conn.Fetch("INSERT INTO t VALUES ($1, 1) RETURNING k", {}, {"1"})));
  AcceptPinned();
  ASSERT_OK(conn.Fetch("INSERT INTO t VALUES ($1, 1) RETURNING k", {}, {"1"}));
}

// Checks that every statement of a multi-statement query carries the message's origination time.
TEST_F(PgPersistenceStampingTest, MultiStatementQuery) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(PinOriginationTime(conn));

  // Changing the override before the INSERT, in the same message, would change the INSERT's
  // stamp if the origination time were taken per statement.
  RejectPinned();
  ExpectRejected(conn.ExecuteFormat(
      "SET yb_origination_time_override = $0; INSERT INTO t VALUES (1, 1)", kLaterOriginationTime));
}

// Checks that every transaction committed by COPY with ROWS_PER_TRANSACTION carries the COPY
// message's origination time.
TEST_F(PgPersistenceStampingTest, CopyBatches) {
  constexpr int kRowCount = 50;
  auto conn = ASSERT_RESULT(Connect());
  // A volatile column default slows the COPY; a trigger would disable batching.
  ASSERT_OK(conn.Execute(
      "CREATE FUNCTION slow_one() RETURNS INT LANGUAGE plpgsql AS "
      "$$ BEGIN PERFORM pg_sleep(0.1); RETURN 1; END $$"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (k INT PRIMARY KEY, v INT DEFAULT slow_one()) SPLIT INTO 3 TABLETS"));
  const auto path = JoinPathSegments(GetTestDataDirectory(), "copy_input.txt");
  {
    std::ofstream out(path);
    for (int i = 1; i <= kRowCount; ++i) {
      out << i << "\n";
    }
  }

  auto [status, row_count] = ASSERT_RESULT(RunAndRejectMidway(
      conn, Format("COPY t (k) FROM '$0' WITH (ROWS_PER_TRANSACTION 1)", path), 3));
  ExpectRejected(status);
  ASSERT_LT(row_count, kRowCount);
}

// Checks that every transaction started by a procedure that commits carries the CALL message's
// origination time.
TEST_F(PgPersistenceStampingTest, ProcedureWithCommit) {
  constexpr int kRowCount = 50;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(conn.Execute(
      "CREATE PROCEDURE fill(n INT) LANGUAGE plpgsql AS $$ BEGIN "
      "FOR i IN 1..n LOOP INSERT INTO t VALUES (i, i); COMMIT; PERFORM pg_sleep(0.1); END LOOP; "
      "END $$"));

  auto [status, row_count] =
      ASSERT_RESULT(RunAndRejectMidway(conn, Format("CALL fill($0)", kRowCount), 3));
  ExpectRejected(status);
  ASSERT_LT(row_count, kRowCount);
}

// Checks that nextval's sequence write is stamped.
TEST_F(PgPersistenceStampingTest, SequenceNextval) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SEQUENCE s CACHE 1"));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  RejectPinned();
  ExpectRejected(ResultToStatus(conn.Fetch("SELECT nextval('s')")), db_oid);
  AcceptPinned();
  ASSERT_OK(conn.Fetch("SELECT nextval('s')"));
}

// Checks that setval's sequence write is stamped.
TEST_F(PgPersistenceStampingTest, SequenceSetval) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SEQUENCE s CACHE 1"));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  RejectPinned();
  ExpectRejected(ResultToStatus(conn.Fetch("SELECT setval('s', 100)")), db_oid);
  // The sequence is unchanged, so setval's own write was refused.
  RejectBelow(0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('s')")), 1);

  AcceptPinned();
  ASSERT_OK(conn.Fetch("SELECT setval('s', 100)"));
  RejectBelow(0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('s')")), 101);
}

// Checks that ALTER SEQUENCE ... RESTART stamps its sequence write.
TEST_F(PgPersistenceStampingTest, AlterSequenceRestart) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SEQUENCE s CACHE 1"));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  RejectPinned();
  ExpectRejected(conn.Execute("ALTER SEQUENCE s RESTART WITH 100"), db_oid);
  // ALTER SEQUENCE updates the sequence before its catalog entry, and the sequence update is not
  // transactional, so an unchanged sequence shows the sequence update itself was refused.
  RejectBelow(0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('s')")), 1);

  AcceptPinned();
  ASSERT_OK(conn.Execute("ALTER SEQUENCE s RESTART WITH 100"));
  RejectBelow(0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT nextval('s')")), 100);
}

// Checks that DROP SEQUENCE's delete of the sequence data is stamped.
TEST_F(PgPersistenceStampingTest, YB_DEBUG_ONLY_TEST(DropSequence)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE SEQUENCE s CACHE 1"));
  // A DFATAL here would mean the sequence data delete went out unstamped.
  ASSERT_OK(conn.Execute("DROP SEQUENCE s"));
}

// Checks that a DDL is stamped with the origination time of its statement.
TEST_F(PgPersistenceStampingTest, Ddl) {
  auto conn = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  // The catalog writes go to master's sys catalog tablet, which applies the threshold as well.
  RejectPinned();
  ExpectRejected(CreateTable(conn), db_oid);
  AcceptPinned();
  ASSERT_OK(CreateTable(conn));
}

// Checks that TRUNCATE is stamped, so that a refused TRUNCATE leaves the table's rows in place.
TEST_F(PgPersistenceStampingTest, Truncate) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 10) AS i"));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  ASSERT_OK(PinOriginationTime(conn));

  RejectPinned();
  ExpectRejected(conn.Execute("TRUNCATE t"), db_oid);
  RejectBelow(0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t")), 10);

  AcceptPinned();
  ASSERT_OK(conn.Execute("TRUNCATE t"));
  RejectBelow(0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM t")), 0);
}

class PgPersistenceStampingBackfillTest : public PgPersistenceStampingTest {
 protected:
  // Backfill runs through a Postgres local to each tablet's TServer, and only one TServer has one.
  size_t NumTabletServers() override { return 1; }

  // PgMiniTestBase turns online index backfill off; production has it on.
  void BeforePgProcessStart() override {
    PgPersistenceStampingTest::BeforePgProcessStart();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_disable_index_backfill) = false;
  }
};

// Checks that CREATE INDEX with an online backfill stamps the writes and commits of all its
// transactions, including the one it holds open across the backfill.
TEST_F(PgPersistenceStampingBackfillTest, YB_DEBUG_ONLY_TEST(CreateIndex)) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT i, i FROM generate_series(1, 100) AS i"));
  // In debug builds, a DFATAL here would mean one of CREATE INDEX's writes or commits went out
  // unstamped.
  ASSERT_OK(conn.Execute("CREATE INDEX ON t (v)"));
}

// Checks that a transaction starting with a parallel scan commits with the leader's origination
// time.
TEST_F(PgPersistenceStampingTest, ParallelQuery) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(CreateTable(conn));
  ASSERT_OK(conn.Execute("INSERT INTO t SELECT generate_series(1, 1000), 0"));

  // Set things up so that even a scan of this small table runs with parallel workers (recipe
  // from pg_read_time-test):
  ASSERT_OK(conn.Execute("ANALYZE t"));
  ASSERT_OK(conn.Execute("SET max_parallel_workers_per_gather = 2"));
  ASSERT_OK(conn.Execute("SET parallel_setup_cost = 0"));
  ASSERT_OK(conn.Execute("SET parallel_tuple_cost = 0"));
  ASSERT_OK(conn.Execute("SET yb_parallel_range_rows = 1"));
  ASSERT_OK(conn.Execute("SET yb_enable_cbo = on"));
  ASSERT_OK(conn.Execute("SET yb_test_force_parallel = force"));

  ASSERT_OK(PinOriginationTime(conn));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));

  // Runs the parallel scan as the transaction's first statement, and checks that parallel workers
  // really ran.
  const auto explain_string =
      ASSERT_RESULT(conn.FetchAllAsString("EXPLAIN (ANALYZE, COSTS OFF) SELECT count(*) FROM t"));
  std::smatch match;
  ASSERT_TRUE(std::regex_search(explain_string, match, std::regex("Workers Launched: (\\d+)")))
      << explain_string;
  const auto num_workers_launched = std::stoi(match[1]);
  ASSERT_GT(num_workers_launched, 0) << explain_string;

  // A write, so the transaction has a commit to stamp.
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (0, 0)"));
  // Workers share the leader's PG client service session and get its origination time through the
  // parallel DSM.  Today their Performs are non-transactional reads, which never create the
  // transaction, so this passes regardless; it fails if workers ever create the transaction and
  // stamp it with a time other than the leader's.
  RejectPinned();
  ExpectRejected(conn.CommitTransaction());
}

// Checks that temp-table cleanup at backend exit takes a fresh origination time instead of the
// backend's last one, so cleanup is not refused for work that predates it.
TEST_F(PgPersistenceStampingTest, ExitCleanup) {
  auto temp_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(temp_conn.Execute("CREATE TEMP TABLE tt (k INT)"));
  const auto temp_schema = ASSERT_RESULT(temp_conn.FetchRow<std::string>(
      "SELECT nspname::text FROM pg_namespace WHERE oid = pg_my_temp_schema()"));

  // End the backend with an idle-session timeout.  A disconnect would not do: it is a message,
  // which refreshes the origination time on its own and would let the test pass without the
  // exit-time refresh.  Nor would pg_terminate_backend: after it, the cleanup cannot reach PG
  // client service at all, so the temp table would never be removed.
  ASSERT_OK(temp_conn.Execute("SET idle_session_timeout = '2s'"));
  // Refuses work stamped before now, which includes the backend's last origination time.
  RejectStampedBeforeNow();

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(WaitFor(
      [&conn, &temp_schema]() -> Result<bool> {
        return VERIFY_RESULT(conn.FetchRow<int64_t>(Format(
                   "SELECT count(*) FROM pg_class c JOIN pg_namespace n ON c.relnamespace = n.oid "
                   "WHERE n.nspname = '$0'",
                   temp_schema))) == 0;
      },
      30s, "temp table removed at backend exit"));
}

// Checks that a backend connected to template1 writes and commits unstamped.
TEST_F(PgPersistenceStampingTest, Template1Unstamped) {
  auto conn = ASSERT_RESULT(ConnectToDB("template1"));
  RejectEverythingStamped();
  // YB supports few writes from template1; CREATE ROLE is one, writing only shared catalogs.
  ASSERT_OK(conn.Execute("CREATE ROLE r"));
}

// Checks that a backend on a system database other than template1 is stamped with that database.
TEST_F(PgPersistenceStampingTest, SystemDatabaseStamped) {
  auto conn = ASSERT_RESULT(ConnectToDB("system_platform"));
  ASSERT_OK(CreateTable(conn));
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  RejectEverythingStamped();
  ExpectRejected(conn.Execute("INSERT INTO t VALUES (1, 1)"), db_oid);
}

// Checks that lock ops are not stamped, so taking an advisory lock is never refused.
TEST_F(PgPersistenceStampingTest, AdvisoryLockUnstamped) {
  auto conn = ASSERT_RESULT(Connect());
  RejectEverythingStamped();
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_lock(1)"));
  ASSERT_OK(conn.Fetch("SELECT pg_advisory_unlock(1)"));
}

// Checks that YCQL writes are not stamped.
TEST_F(PgPersistenceStampingTest, YcqlUnstamped) {
  client::TableHandle table;
  client::kv_table_test::CreateTable(
      client::Transactional::kFalse, /*num_tablets=*/1, client_.get(), &table);
  RejectEverythingStamped();
  auto session = client_->NewSession(60s);
  ASSERT_OK(client::kv_table_test::WriteRow(&table, session, /*key=*/1, /*value=*/1));
}

class PgPersistenceStampingNotifyTest : public PgPersistenceStampingTest {
 protected:
  void BeforePgProcessStart() override {
    PgPersistenceStampingTest::BeforePgProcessStart();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_listen_notify) = true;
  }
};

// Checks that NOTIFY, which writes a notification for the backend's database, is stamped.
TEST_F(PgPersistenceStampingNotifyTest, Notify) {
  auto conn = ASSERT_RESULT(Connect());
  const auto db_oid = ASSERT_RESULT(DatabaseOid(conn));
  // The first NOTIFY in the cluster fails while LISTEN/NOTIFY creates its internal objects.
  ASSERT_OK(WaitFor(
      [&conn]() -> Result<bool> { return conn.Execute("NOTIFY ch, 'x'").ok(); }, 60s,
      "LISTEN/NOTIFY ready"));
  ASSERT_OK(PinOriginationTime(conn));

  RejectPinned();
  ExpectRejected(conn.Execute("NOTIFY ch, 'x'"), db_oid);
  AcceptPinned();
  ASSERT_OK(conn.Execute("NOTIFY ch, 'x'"));
}

}  // namespace yb::pgwrapper
