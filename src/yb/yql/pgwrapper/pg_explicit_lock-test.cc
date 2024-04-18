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

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(enable_wait_queues);

namespace yb {
namespace pgwrapper {

template<IsolationLevel level>
class PgExplicitLockTest : public PgMiniTestBase {
 protected:
  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_sleep_before_retry_on_txn_conflict) = false;
  }

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
    : public PgExplicitLockTest<IsolationLevel::SERIALIZABLE_ISOLATION> {
 protected:
  void BeforePgProcessStart() override {
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    EnableFailOnConflict();
    PgExplicitLockTest::BeforePgProcessStart();
  }
};

class PgExplicitLockTestSnapshot : public PgExplicitLockTest<IsolationLevel::SNAPSHOT_ISOLATION> {
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

} // namespace pgwrapper
} // namespace yb
