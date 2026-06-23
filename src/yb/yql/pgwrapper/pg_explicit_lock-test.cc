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

#include "yb/util/json_document.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_int32(TEST_transactional_read_delay_ms);
DECLARE_bool(TEST_force_use_explicit_row_lock_skip_locked_read_ahead_optimization);

namespace yb::pgwrapper {
namespace {

Status SetExplicitRowLockMaxReadAhead(PGConn& conn, size_t value) {
  return conn.ExecuteFormat("SET yb_explicit_row_lock_skip_locked_max_read_ahead = $0", value);
}

Status DisableExplicitRowLockReadAhead(PGConn& conn) {
  return SetExplicitRowLockMaxReadAhead(conn, 1);
}

Result<JsonDocument> ExplainForReadAhead(PGConn& conn, const std::string& query) {
  return conn.FetchRow<JsonDocument>(Format(
      "EXPLAIN (ANALYZE, DIST, DEBUG, FORMAT JSON) $0", query));
}

Result<JsonObject> ExplainTopPlanForReadAhead(PGConn& conn, const std::string& query) {
  return VERIFY_RESULT(ExplainForReadAhead(conn, query)).Root()[0]["Plan"].GetObject();
}

Result<std::optional<uint32_t>> GetMaxReadAhead(
    const JsonValue& node, const std::string& expected_node_type) {
  const auto obj = VERIFY_RESULT(node.GetObject());
  const auto node_type = VERIFY_RESULT(obj["Node Type"].GetString());
  SCHECK_EQ(node_type, expected_node_type, IllegalState, "Bad node type");
  const auto max_read_ahead = obj["Max Read Ahead"];
  return max_read_ahead.IsValid()
      ? std::optional(VERIFY_RESULT(max_read_ahead.GetUint32())) : std::nullopt;
}

Result<std::optional<uint32_t>> GetLockRowsMaxReadAhead(const JsonValue& object) {
  return GetMaxReadAhead(object, "LockRows");
}

} // namespace

class PgExplicitLockTest : public PgMiniTestBase {
 protected:
  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_sleep_before_retry_on_txn_conflict) = false;
  }
};

template<IsolationLevel level>
class PgExplicitLockTestTxnBase : public PgExplicitLockTest {
 protected:
  void TestRowLockInJoin() {
    auto join_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
    auto misc_conn = ASSERT_RESULT(Connect());
    auto select_conn = ASSERT_RESULT(SetHighPriTxn(Connect()));

    // Set up tables
    ASSERT_OK(misc_conn.Execute(
        "create table employees (k int primary key, profession text, email text)"));
    ASSERT_OK(misc_conn.Execute("create table physicians (k int primary key, email text)"));
    ASSERT_OK(misc_conn.Execute("insert into employees values (1, 'sales', 'salesman1@xyz.com')"));
    ASSERT_OK(misc_conn.Execute("insert into employees values (2, 'sales', 'salesman2@xyz.com')"));
    ASSERT_OK(misc_conn.Execute("insert into employees values (3, 'physician', 'phy1@xyz.com')"));
    ASSERT_OK(misc_conn.Execute("insert into employees values (4, 'physician', 'phy2@xyz.com')"));
    ASSERT_OK(misc_conn.Execute("insert into physicians values (1, 'phy1@xyz.com')"));
    ASSERT_OK(misc_conn.Execute("insert into physicians values (2, 'phy2@xyz.com')"));

    // Test case 1: Join returns no rows.
    ASSERT_OK(StartTxn(&join_conn));

    // 1. For SERIALIZABLE level: all tablets of the table are locked since we need to lock the
    //    whole predicate.
    // 2. For REPEATABLE READ level: No rows are locked since none match the conditions.
    auto res = ASSERT_RESULT(
        join_conn.Fetch(
          "select * from physicians, employees where employees.profession = 'sales' and "
          "employees.email = physicians.email for update"));
    ASSERT_EQ(PQntuples(res.get()), 0);

    // The below statement will have a higher priority than the above txn.
    // Given this, the join txn will face following fate based on isolation level -
    //   1. SERIALIZABLE level: aborted due to conflicting locks.
    //   2. REPEATABLE READ level: not aborted since no locks taken.
    res = ASSERT_RESULT(select_conn.Fetch("select * from employees for update"));
    ASSERT_EQ(PQntuples(res.get()), 4);
    if (level == IsolationLevel::SERIALIZABLE_ISOLATION) {
      ASSERT_NOK(join_conn.Execute("COMMIT"));
    } else {
      ASSERT_OK(join_conn.Execute("COMMIT"));
    }

    // Test case 2: Join returns 2 rows (but differernt from those returned later a by singular
    // select statement).
    ASSERT_OK(StartTxn(&join_conn));

    // 1. For SERIALIZABLE level: all tablets of the table are locked since we need to lock the
    //    whole predicate.
    // 2. For REPEATABLE READ level: 2 rows are locked (the 'physician' ones).
    res = ASSERT_RESULT(
        join_conn.Fetch(
          "select * from physicians, employees where employees.profession = 'physician' and "
          "employees.email = physicians.email for update;"));
    ASSERT_EQ(PQntuples(res.get()), 2);

    // The below statement will have a higher priority than the above txn.
    // Given this, the join txn will face following fate based on isolation level -
    //   1. SERIALIZABLE level: aborted due to conflicting locks.
    //   2. REPEATABLE READ level: not aborted since locks are on different sets of rows.
    res = ASSERT_RESULT(select_conn.Fetch(
        "select * from employees where employees.profession = 'sales' for update;"));
    ASSERT_EQ(PQntuples(res.get()), 2);
    if (level == IsolationLevel::SERIALIZABLE_ISOLATION) {
      ASSERT_NOK(join_conn.Execute("COMMIT"));
    } else {
      ASSERT_OK(join_conn.Execute("COMMIT"));
    }
  }

  static Status StartTxn(PGConn* connection) {
    return connection->StartTransaction(level);
  }
};

class PgExplicitLockTestSerializable
    : public PgExplicitLockTestTxnBase<IsolationLevel::SERIALIZABLE_ISOLATION> {
 protected:
  void BeforePgProcessStart() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    EnableFailOnConflict();
    PgExplicitLockTestTxnBase::BeforePgProcessStart();
  }
};

class PgExplicitLockTestSnapshot
    : public PgExplicitLockTestTxnBase<IsolationLevel::SNAPSHOT_ISOLATION> {
 protected:
  void TestSkipLocked();
};

// Currently SKIP LOCKED is supported only SELECT statements in REPEATABLE READ isolation level.
void PgExplicitLockTestSnapshot::TestSkipLocked() {
  auto misc_conn = ASSERT_RESULT(Connect());

  // Set up table
  ASSERT_OK(misc_conn.Execute("create table test (k int primary key, v int)"));
  ASSERT_OK(misc_conn.Execute("insert into test values (1, 10), (2, 20), (3, 30)"));

  // Test case 1: 2 REPEATABLE READ txns skipping rows locked by each other.
  auto txn1_conn = ASSERT_RESULT(Connect());
  auto txn2_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(StartTxn(&txn1_conn));
  ASSERT_OK(StartTxn(&txn2_conn));

  auto row = ASSERT_RESULT((txn1_conn.FetchRow<int32_t, int32_t>(
      "select * from test for update skip locked limit 1")));
  ASSERT_EQ(row, (decltype(row){1, 10}));

  row = ASSERT_RESULT((txn2_conn.FetchRow<int32_t, int32_t>(
      "select * from test for update skip locked limit 1")));
  ASSERT_EQ(row, (decltype(row){2, 20}));

  auto rows = ASSERT_RESULT((txn1_conn.FetchRows<int32_t, int32_t>(
      "select * from test for update skip locked limit 2")));
  ASSERT_EQ(rows, (decltype(rows){{1, 10}, {3, 30}}));

  row = ASSERT_RESULT((txn2_conn.FetchRow<int32_t, int32_t>(
      "select * from test for update skip locked limit 2")));
  ASSERT_EQ(row, (decltype(row){2, 20}));

  ASSERT_OK(txn1_conn.Execute("COMMIT"));
  ASSERT_OK(txn2_conn.Execute("COMMIT"));

  // Test case 2: A txn holds lock on some rows. A single statement then skips the locked rows.
  ASSERT_OK(StartTxn(&txn1_conn));
  row = ASSERT_RESULT((txn1_conn.FetchRow<int32_t, int32_t>(
      "select * from test for update skip locked limit 1")));
  ASSERT_EQ(row, (decltype(row){1, 10}));

  auto single_stmt_conn = ASSERT_RESULT(Connect());
  row = ASSERT_RESULT((single_stmt_conn.FetchRow<int32_t, int32_t>(
      "select * from test for update skip locked limit 1")));
  ASSERT_EQ(row, (decltype(row){2, 20}));

  rows = ASSERT_RESULT((txn1_conn.FetchRows<int32_t, int32_t>(
      "select * from test for update skip locked limit 2")));
  ASSERT_EQ(rows, (decltype(rows){{1, 10}, {2, 20}}));

  ASSERT_OK(txn1_conn.Execute("COMMIT"));

  // Test case 3:
  // Use a join (involving 2 tables) that tries to lock rows based on some join predicate i.e.,
  // locks 2 rows, one from each table (say r1 and r2) and also has SKIP LOCKED clause. But the join
  // finds that one of those rows (say r1) is already locked by some other txn. In this case assert
  // two things -
  //   1. the join should move on to the next set of rows that satisfy the predicate and lock those.
  //   2. r2 should still be available for locking
  ASSERT_OK(misc_conn.Execute("create table test2 (k int primary key, v int)"));
  ASSERT_OK(misc_conn.Execute("insert into test2 values (4, 10), (5, 20), (6, 30)"));

  ASSERT_OK(StartTxn(&txn1_conn));
  ASSERT_OK(StartTxn(&txn2_conn));
  row = ASSERT_RESULT((txn1_conn.FetchRow<int32_t, int32_t>(
      "select * from test where k=1 for update")));
  ASSERT_EQ(row, (decltype(row){1, 10}));

  auto row2 = ASSERT_RESULT((txn2_conn.FetchRow<int32_t, int32_t, int32_t, int32_t>(
      "select * from test, test2 where test.v=test2.v for update skip locked limit 1")));
  ASSERT_EQ(row2, (decltype(row2){2, 20, 5, 20}));

  row = ASSERT_RESULT((txn1_conn.FetchRow<int32_t, int32_t>(
      "select * from test2 where k=4 for update")));
  ASSERT_EQ(row, (decltype(row){4, 10}));

  ASSERT_OK(txn1_conn.Execute("COMMIT"));
  ASSERT_OK(txn2_conn.Execute("COMMIT"));
}

TEST_F(PgExplicitLockTestSnapshot, YB_DISABLE_TEST_IN_SANITIZERS(RowLockInJoin)) {
  TestRowLockInJoin();
}

TEST_F(PgExplicitLockTestSerializable, YB_DISABLE_TEST_IN_SANITIZERS(RowLockInJoin)) {
  TestRowLockInJoin();
}

TEST_F(PgExplicitLockTestSnapshot, YB_DISABLE_TEST_IN_SANITIZERS(SkipLocked)) {
  TestSkipLocked();
}

class PgSkipLockedOptimizationTest : public PgExplicitLockTest {
 protected:
  void SetUp() override {
    EnableFailOnConflict();
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_TEST_force_use_explicit_row_lock_skip_locked_read_ahead_optimization) = true;
    PgExplicitLockTest::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

// The test checks basic correctness of batching rows locks with SKIP LOCKED clause
TEST_F_EX(PgExplicitLockTest, BatchedSkipLockedBasic, PgSkipLockedOptimizationTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE pk(k INT, v INT, PRIMARY KEY(k ASC))"));
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t(k INT, v INT REFERENCES pk(k), v2 INT, PRIMARY KEY(k ASC))"));
  ASSERT_OK(conn.Execute("INSERT INTO pk VALUES (1, 10), (2, 20), (3, 30)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1, 100), (2, 2, 200), (3, 3, 300)"));
  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_RESULT(aux_conn.FetchRow<int32_t>("SELECT k FROM t WHERE k = 2 FOR UPDATE"));
  constexpr auto kQuery = "SELECT * FROM t INNER JOIN pk ON (t.v = pk.k) FOR UPDATE SKIP LOCKED"sv;
  ASSERT_OK(SetExplicitRowLockMaxReadAhead(conn, 10));
  auto rows = ASSERT_RESULT((conn.FetchRows<int32_t, int32_t, int32_t, int32_t, int32_t>(
      std::string(kQuery))));
  ASSERT_EQ(rows, (decltype(rows){{1, 1, 100, 1, 10}, {3, 3, 300, 3, 30}}));
}

// The test checks absence of undesired row locks in case of SKIP LOCKED with LIMIT clause
TEST_F_EX(PgExplicitLockTest, BatchedSkipLockedWithLimit, PgSkipLockedOptimizationTest) {
  auto conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto extra_conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT, PRIMARY KEY(k ASC))"));
  constexpr size_t kCount = 10000;
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT s, s FROM generate_series(1, $0) AS s", kCount));
  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto aux_rows = ASSERT_RESULT(aux_conn.FetchRows<int32_t>(
      "SELECT k FROM t WHERE k % 2 = 0 FOR UPDATE"));
  auto checker = [&conn, &extra_conn](size_t read_ahead) -> Status {
    RETURN_NOT_OK(SetExplicitRowLockMaxReadAhead(conn, read_ahead));
    constexpr size_t kLimit = 1000;
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    auto rows = VERIFY_RESULT((conn.FetchRows<int32_t, int32_t>(Format(
      "SELECT * FROM t LOCK FOR UPDATE SKIP LOCKED LIMIT $0", kLimit))));
    SCHECK_EQ(rows.size(), kLimit, IllegalState, "Unexpected number of fetched rows");
    auto extra_rows = VERIFY_RESULT(extra_conn.FetchRows<int32_t>(Format(
        "SELECT k FROM t WHERE k % 2 = 1 AND k > $0 FOR UPDATE", kLimit * 2)));
    SCHECK_EQ(
        extra_rows.size(), kCount - kCount / 2 - kLimit, IllegalState,
        "Unexpected number of fetched extra rows ");
    RETURN_NOT_OK(conn.CommitTransaction());
    return Status::OK();
  };

  for (auto read_ahead : {1, 2, 3, 5, 7, 11, 13, 17, 19}) {
    ASSERT_OK(checker(read_ahead));
  }
  ASSERT_OK(aux_conn.CommitTransaction());
}

// The unit test check that SKIP LOCKED requests are sent in parallel
TEST_F_EX(PgExplicitLockTest, BatchedSkipLockedPerformance, PgSkipLockedOptimizationTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT, v INT, PRIMARY KEY(k ASC))"));
  constexpr auto kCount = 4;
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO t SELECT s, s FROM generate_series(1, $0) AS s", kCount));
  auto execute_with_duration = [&conn](size_t read_ahead) -> Result<uint64_t> {
    RETURN_NOT_OK(SetExplicitRowLockMaxReadAhead(conn, read_ahead));
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    const auto start = std::chrono::steady_clock::now();
    const auto res = VERIFY_RESULT((conn.FetchRows<int32_t, int32_t>(
        "SELECT * FROM t LOCK FOR UPDATE SKIP LOCKED")));
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    SCHECK_EQ(
        res, (decltype(res){{1, 1}, {2, 2}, {3, 3}, {4, 4}}), IllegalState, "Unexpected values");
    RETURN_NOT_OK(conn.CommitTransaction());
    return duration;
  };
  constexpr auto kDelay = 500;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transactional_read_delay_ms) = kDelay;
  {
    const auto dur = ASSERT_RESULT(execute_with_duration(/* read_ahead = */1));
    // Without read ahead (i.e. yb_explicit_row_lock_skip_locked_max_read_ahead = 1) it is expected
    // that all requests are sent sequentially, so each request will spent more than kDelay time
    ASSERT_GT(dur, kCount * kDelay);
  }

  {
    const auto dur = ASSERT_RESULT(execute_with_duration(/* read_ahead = */kCount));
    // With read ahead more that required number of column it is expected that all
    // requests are sent simultaneously, so total time is a little bit more that kDelay
    ASSERT_LT(dur, 2 * kDelay);
  }
}

// The test checks correctness and absence of undesired locks in case of JOIN
TEST_F_EX(PgExplicitLockTest, BatchedSkipLockedJoinLockOrder, PgSkipLockedOptimizationTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
  for(auto table : {"p1"sv, "p2"sv, "p3"sv}) {
    ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0(k INT, v INT, PRIMARY KEY(k ASC));"
      "INSERT INTO $0 SELECT s, s FROM generate_series(1, 9) AS s", table));
  }

  ASSERT_OK(conn.Execute(
      "CREATE TABLE t(k INT, p1_fk INT REFERENCES p1(k),"
      "                      p2_fk INT REFERENCES p2(k),"
      "                      p3_fk INT REFERENCES p3(k), PRIMARY KEY(k ASC));"
      "INSERT INTO t SELECT s, s, s, s FROM generate_series(1, 9) AS s"));

  auto checker = [&conn, &aux_conn](bool batching) -> Status {
    RETURN_NOT_OK(
        batching ? SetExplicitRowLockMaxReadAhead(conn, 5) : DisableExplicitRowLockReadAhead(conn));
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    using TableRows = std::pair<std::string_view, std::initializer_list<int32_t>>;
    for (const auto& trow : std::initializer_list<TableRows>{{"p1"sv, {1, 3, 5}},
                                                             {"p2"sv, {2, 4, 8}},
                                                             {"p3"sv, {3, 6, 9}}}) {
      const auto& [table_name, rows] = trow;
      VERIFY_RESULT(aux_conn.FetchRows<int32_t>(Format(
          "SELECT k FROM $0 WHERE k = ANY(ARRAY$1) FOR UPDATE",
          table_name, CollectionToString(rows))));
    }
    const auto t_rows = VERIFY_RESULT((conn.FetchRows<int32_t, int32_t, int32_t, int32_t>(
        "SELECT t.k, p1.v, p2.v, p3.v FROM t JOIN p1 ON (t.p1_fk = p1.k)"
        "                                    JOIN p2 ON (t.p2_fk = p2.k)"
        "                                    JOIN p3 ON (t.p3_fk = p3.k) FOR UPDATE SKIP LOCKED")));
    SCHECK_EQ(t_rows, (decltype(t_rows){{7, 7, 7, 7}}), IllegalState, "Unexpected values");
    RETURN_NOT_OK(aux_conn.CommitTransaction());

    // Check non-locked rows in all the tables
    RETURN_NOT_OK(DisableExplicitRowLockReadAhead(aux_conn));
    for (const auto& trow : std::initializer_list<TableRows>{{"p1"sv, {1, 3, 5}},
                                                             {"p2"sv, {1, 2, 3, 4, 5, 8}},
                                                             {"p3"sv, {1, 2, 3, 4, 5, 6, 8, 9}},
                                                             {"t"sv,  {}}}) {
      const auto& [table_name, expected_rows] = trow;
      const auto rows = VERIFY_RESULT(aux_conn.FetchRows<int32_t>(Format(
          "SELECT k FROM $0 FOR UPDATE SKIP LOCKED", table_name)));
      SCHECK_EQ(
          rows, (decltype(rows){expected_rows}),
          IllegalState, Format("Unexpected rows in table $0", table_name));
    }
    return conn.CommitTransaction();
  };
  ASSERT_OK(checker(/* batching = */ false));
  ASSERT_OK(checker(/* batching = */ true));
}

// The test checks correctness and absence of undesired locks in case of nested LockRows nodes
TEST_F_EX(PgExplicitLockTest, BatchedSkipLockedNestedLockRowsNodes, PgSkipLockedOptimizationTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (k INT, v INT, PRIMARY KEY(k ASC));" \
      "CREATE TABLE t2 (k INT, v INT, PRIMARY KEY(k ASC));" \
      "INSERT INTO t1 VALUES(1, 1), (2, 2), (3, 3), (4, 4), (5, 5);" \
      "INSERT INTO t2 VALUES(1, 10), (2, 20), (3, 30), (4, 40), (5, 50);"));
  auto checker = [&conn, &aux_conn](bool batching) -> Status {
    RETURN_NOT_OK(
        batching ? SetExplicitRowLockMaxReadAhead(conn, 3) : DisableExplicitRowLockReadAhead(conn));
    RETURN_NOT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    VERIFY_RESULT(aux_conn.FetchRow<int32_t>("SELECT k FROM t1 WHERE k = 2 FOR UPDATE"));
    VERIFY_RESULT(aux_conn.FetchRow<int32_t>("SELECT k FROM t2 WHERE k = 4 FOR UPDATE"));

    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    auto rows = VERIFY_RESULT((conn.FetchRows<int32_t, int32_t>(
        "SELECT t1.k, sub.v FROM " \
        "    t1 INNER JOIN " \
        "    (SELECT * FROM t2 FOR UPDATE SKIP LOCKED) AS sub ON (t1.k = sub.k) " \
        "FOR UPDATE SKIP LOCKED")));
    SCHECK_EQ(rows, (decltype(rows){{1, 10}, {3, 30}, {5, 50}}), IllegalState, "Unexpected values");
    RETURN_NOT_OK(aux_conn.CommitTransaction());

    auto t1_non_locked_rows =
        VERIFY_RESULT(aux_conn.FetchRows<int32_t>("SELECT k FROM t1 FOR UPDATE SKIP LOCKED"));
    SCHECK_EQ(
        t1_non_locked_rows, (decltype(t1_non_locked_rows){2, 4}),
        IllegalState, "Unexpected row locks state in table t1");

    auto t2_non_locked_rows =
        VERIFY_RESULT(aux_conn.FetchRows<int32_t>("SELECT k FROM t2 FOR UPDATE SKIP LOCKED"));
    SCHECK_EQ(
        t2_non_locked_rows, (decltype(t1_non_locked_rows){4}),
        IllegalState, "Unexpected row locks state in table t2");
    return conn.CommitTransaction();
  };
  ASSERT_OK(checker(/* batching = */ false));
  ASSERT_OK(checker(/* batching = */ true));
}

// The test checks when skip locked batching can be applied based on query plan
TEST_F_EX(PgExplicitLockTest, SkipLockedBatchingApplicability, PgExplicitLockTest) {
  constexpr uint32_t kMaxReadAhead = 5;
  constexpr uint32_t kLimit = 3;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t1 (k INT PRIMARY KEY);" \
      "CREATE TABLE t2 (k INT PRIMARY KEY);" \

      "INSERT INTO t1 VALUES(1), (2), (3), (4), (5);" \
      "INSERT INTO t2 VALUES(1), (2), (3), (4), (5)"));

  ASSERT_OK(SetExplicitRowLockMaxReadAhead(conn, kMaxReadAhead));
  auto top_plan = [&conn](const std::string& query) {
    return ExplainTopPlanForReadAhead(conn, query);
  };
  // Test queries with single LockRows
  const auto read_ahead_no_limit = ASSERT_RESULT(GetLockRowsMaxReadAhead(
      ASSERT_RESULT(top_plan("SELECT * FROM (SELECT * FROM t1 FOR UPDATE SKIP LOCKED) AS sub"))
          ["Plans"][0]));
  ASSERT_EQ(read_ahead_no_limit, std::optional(kMaxReadAhead));
  const auto read_ahead_outer_limit = ASSERT_RESULT(GetLockRowsMaxReadAhead(
      ASSERT_RESULT(top_plan(
          "SELECT * FROM (SELECT * FROM t1 FOR UPDATE SKIP LOCKED) AS sub LIMIT 3"))
          ["Plans"][0]["Plans"][0]));
  ASSERT_EQ(read_ahead_outer_limit, std::nullopt);

  // Test query with single LockRows
  const auto join_lock = ASSERT_RESULT(top_plan(
      Format(
          "SELECT * FROM " \
          "    t1 INNER JOIN " \
          "    (SELECT * FROM t2 FOR UPDATE SKIP LOCKED) AS sub ON (t1.k = sub.k) "\
          "FOR UPDATE SKIP LOCKED LIMIT $0", kLimit)))["Plans"][0];
  const auto read_ahead_join = ASSERT_RESULT(GetLockRowsMaxReadAhead(join_lock));
  ASSERT_EQ(read_ahead_join, std::optional(kLimit));
  const auto read_ahead_sub = ASSERT_RESULT(GetLockRowsMaxReadAhead(
      join_lock["Plans"][0]["Plans"][0]["Plans"][0]));
  ASSERT_EQ(read_ahead_sub, std::nullopt);
}

} // namespace yb::pgwrapper
