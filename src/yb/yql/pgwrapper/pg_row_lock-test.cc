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

#include "yb/common/pgsql_error.h"

#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_wait_queues);
DECLARE_bool(enable_deadlock_detection);

using namespace std::literals;

DECLARE_bool(yb_enable_read_committed_isolation);

namespace yb::pgwrapper {

YB_DEFINE_ENUM(TestStatement, (kInsert)(kDelete));

class PgRowLockTest : public PgMiniTestBase {
 public:
  // Test an INSERT/DELETE in a concurrent transaction but before a SELECT  with specified isolation
  // level and row mark.
  void TestStmtBeforeRowLock(
      IsolationLevel isolation, RowMarkType row_mark, TestStatement statement);
  void TestStmtBeforeRowLockImpl(TestStatement statement);

 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    // This test depends on fail-on-conflict concurrency control to perform its validation.
    // TODO(wait-queues): https://github.com/yugabyte/yugabyte-db/issues/17871
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_deadlock_detection) = false;
    PgMiniTestBase::SetUp();
  }
};

std::string RowMarkTypeToPgsqlString(const RowMarkType row_mark_type) {
  switch (row_mark_type) {
    case RowMarkType::ROW_MARK_EXCLUSIVE:
      return "UPDATE";
    case RowMarkType::ROW_MARK_NOKEYEXCLUSIVE:
      return "NO KEY UPDATE";
    case RowMarkType::ROW_MARK_SHARE:
      return "SHARE";
    case RowMarkType::ROW_MARK_KEYSHARE:
      return "KEY SHARE";
    default:
      // We shouldn't get here because other row lock types are disabled at the postgres level.
      LOG(DFATAL) << "Unsupported row lock of type " << RowMarkType_Name(row_mark_type);
      return "";
  }
}

template<IsolationLevel level>
class TxnHelper {
 public:
  static Status StartTxn(PGConn* connection) {
    return connection->StartTransaction(level);
  }

  static Status ExecuteInTxn(PGConn* connection, const std::string& query) {
    const auto guard = CreateTxnGuard(connection);
    return connection->Execute(query);
  }

  static Result<PGResultPtr> FetchInTxn(PGConn* connection, const std::string& query) {
    const auto guard = CreateTxnGuard(connection);
    return connection->Fetch(query);
  }

 private:
  static auto CreateTxnGuard(PGConn* connection) {
    EXPECT_OK(StartTxn(connection));
    return ScopeExit([connection]() {
      // Event in case some operations in transaction failed the COMMIT command
      // will complete successfully as ROLLBACK will be performed by postgres.
      EXPECT_OK(connection->Execute("COMMIT"));
    });
  }
};


void PgRowLockTest::TestStmtBeforeRowLock(
    IsolationLevel isolation, RowMarkType row_mark, TestStatement statement) {
  const std::string row_mark_str = RowMarkTypeToPgsqlString(row_mark);
  constexpr auto kSleepTime = 1s;
  constexpr int kKeys = 3;
  PGConn read_conn = ASSERT_RESULT(Connect());
  PGConn misc_conn = ASSERT_RESULT(Connect());
  PGConn write_conn = ASSERT_RESULT(Connect());

  // Set up table
  ASSERT_OK(misc_conn.Execute("CREATE TABLE t (i INT PRIMARY KEY, j INT)"));
  // TODO: remove this when issue #2857 is fixed.
  std::this_thread::sleep_for(kSleepTime);
  for (int i = 0; i < kKeys; ++i) {
    ASSERT_OK(misc_conn.ExecuteFormat("INSERT INTO t (i, j) VALUES ($0, $0)", i));
  }

  ASSERT_OK(read_conn.StartTransaction(isolation));
  ASSERT_OK(read_conn.FetchFormat("SELECT * FROM t WHERE i = $0", -1));

  // Sleep to ensure that read done in snapshot isolation txn doesn't face kReadRestart after INSERT
  // (a sleep will ensure sufficient gap between write time and read point - more than clock skew).
  //
  // kReadRestart errors can't occur in read committed and serializable isolation levels.
  if (isolation == IsolationLevel::SNAPSHOT_ISOLATION) {
    std::this_thread::sleep_for(kSleepTime);
  }

  if (statement == TestStatement::kInsert) {
    ASSERT_OK(write_conn.ExecuteFormat("INSERT INTO t (i, j) VALUES ($0, $0)", kKeys));
  } else {
    ASSERT_OK(write_conn.ExecuteFormat(
        "DELETE FROM t WHERE i = $0", RandomUniformInt(0, kKeys - 1)));
  }
  auto result = read_conn.FetchFormat("SELECT * FROM t FOR $0", row_mark_str);
  if (isolation == IsolationLevel::SNAPSHOT_ISOLATION && statement == TestStatement::kDelete) {
    ASSERT_NOK(result);
    ASSERT_TRUE(result.status().IsNetworkError()) << result.status();
    ASSERT_EQ(PgsqlError(result.status()), YBPgErrorCode::YB_PG_T_R_SERIALIZATION_FAILURE)
        << result.status();
    ASSERT_STR_CONTAINS(result.status().ToString(),
                        "could not serialize access due to concurrent update");
    ASSERT_OK(read_conn.Execute("ABORT"));
  } else {
    ASSERT_OK(result);
    // NOTE: vanilla PostgreSQL expects kKeys rows, but kKeys +/- 1 rows are expected for
    // YugabyteDB.
    auto expected_keys = kKeys;
    if (isolation != IsolationLevel::SNAPSHOT_ISOLATION) {
      expected_keys += statement == TestStatement::kInsert ? 1 : -1;
    }
    ASSERT_EQ(PQntuples(result.get().get()), expected_keys);
    ASSERT_OK(read_conn.Execute("COMMIT"));
  }
  ASSERT_OK(read_conn.CommitTransaction());
  ASSERT_OK(misc_conn.Execute("DROP TABLE t"));
}

void PgRowLockTest::TestStmtBeforeRowLockImpl(TestStatement statement) {
  for (const auto& isolation : {
          IsolationLevel::READ_COMMITTED, IsolationLevel::SNAPSHOT_ISOLATION,
          IsolationLevel::SERIALIZABLE_ISOLATION}) {
    for (const auto& row_mark : {
            RowMarkType::ROW_MARK_EXCLUSIVE, RowMarkType::ROW_MARK_NOKEYEXCLUSIVE,
            RowMarkType::ROW_MARK_SHARE, RowMarkType::ROW_MARK_KEYSHARE}) {
      TestStmtBeforeRowLock(isolation, row_mark, statement);
    }
  }
}

TEST_F(PgRowLockTest, YB_DISABLE_TEST_IN_SANITIZERS(InsertBeforeExplicitRowLock)) {
  TestStmtBeforeRowLockImpl(TestStatement::kInsert);
}

TEST_F(PgRowLockTest, YB_DISABLE_TEST_IN_SANITIZERS(DeleteBeforeExplicitRowLock)) {
  TestStmtBeforeRowLockImpl(TestStatement::kDelete);
}

TEST_F(PgRowLockTest, RowLockWithoutTransaction) {
  auto conn = ASSERT_RESULT(Connect());

  auto status = conn.Execute(
      "SELECT tmplinline FROM pg_catalog.pg_pltemplate WHERE tmplname !~ tmplhandler FOR SHARE");
  ASSERT_NOK(status);
  ASSERT_STR_CONTAINS(status.message().ToBuffer(),
                      "Read request with row mark types must be part of a transaction");
}

TEST_F(PgRowLockTest, SelectForKeyShareWithRestart) {
  const auto table = "foo";
  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT, v INT)", table));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 SELECT generate_series(1, 100), 1", table));
  ASSERT_OK(cluster_->FlushTablets());

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.FetchFormat("SELECT * FROM $0 WHERE k=1 FOR KEY SHARE", table));

  ASSERT_OK(cluster_->RestartSync());
}

class PgMiniTestNoTxnRetry : public PgRowLockTest {
 protected:
  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_sleep_before_retry_on_txn_conflict) = false;
  }
};

// ------------------------------------------------------------------------------------------------
// A test performing manual transaction control on system tables.
// ------------------------------------------------------------------------------------------------

TEST_F_EX(PgRowLockTest, SystemTableTxnTest, PgMiniTestNoTxnRetry) {

  // Resolving conflicts between transactions on a system table.
  //
  // postgres=# \d pg_ts_dict;
  //
  //              Table "pg_catalog.pg_ts_dict"
  //      Column     | Type | Collation | Nullable | Default
  // ----------------+------+-----------+----------+---------
  //  dictname       | name |           | not null |
  //  dictnamespace  | oid  |           | not null |
  //  dictowner      | oid  |           | not null |
  //  dicttemplate   | oid  |           | not null |
  //  dictinitoption | text |           |          |
  // Indexes:
  //     "pg_ts_dict_oid_index" PRIMARY KEY, lsm (oid)
  //     "pg_ts_dict_dictname_index" UNIQUE, lsm (dictname, dictnamespace)

  auto conn1 = ASSERT_RESULT(Connect());
  auto conn2 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=1"));
  ASSERT_OK(conn2.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=1"));

  size_t commit1_fail_count = 0;
  size_t commit2_fail_count = 0;
  size_t insert2_fail_count = 0;

  const auto kStartTxnStatementStr = "START TRANSACTION ISOLATION LEVEL REPEATABLE READ";
  const int iterations = 48;
  for (int i = 1; i <= iterations; ++i) {
    std::string dictname = Format("contendedkey$0", i);
    const int dictnamespace = i;
    ASSERT_OK(conn1.Execute(kStartTxnStatementStr));
    ASSERT_OK(conn2.Execute(kStartTxnStatementStr));

    // Insert a row in each transaction. The first insert should always succeed.
    ASSERT_OK(conn1.Execute(
        Format("INSERT INTO pg_ts_dict VALUES ('$0', $1, 1, 2, 'b')", dictname, dictnamespace)));
    Status insert_status2 = conn2.Execute(
        Format("INSERT INTO pg_ts_dict VALUES ('$0', $1, 3, 4, 'c')", dictname, dictnamespace));
    if (!insert_status2.ok()) {
      LOG(INFO) << "MUST BE A CONFLICT: Insert failed: " << insert_status2;
      insert2_fail_count++;
    }

    Status commit_status1;
    Status commit_status2;
    if (RandomUniformBool()) {
      commit_status1 = conn1.Execute("COMMIT");
      commit_status2 = conn2.Execute("COMMIT");
    } else {
      commit_status2 = conn2.Execute("COMMIT");
      commit_status1 = conn1.Execute("COMMIT");
    }
    if (!commit_status1.ok()) {
      commit1_fail_count++;
    }
    if (!commit_status2.ok()) {
      commit2_fail_count++;
    }

    auto get_commit_statuses_str = [&commit_status1, &commit_status2]() {
      return Format("commit_status1=$0, commit_status2=$1", commit_status1, commit_status2);
    };

    bool succeeded1 = commit_status1.ok();
    bool succeeded2 = insert_status2.ok() && commit_status2.ok();

    ASSERT_TRUE(!succeeded1 || !succeeded2)
        << "Both transactions can't commit. " << get_commit_statuses_str();
    ASSERT_TRUE(succeeded1 || succeeded2)
        << "We expect one of the two transactions to succeed. " << get_commit_statuses_str();
    if (!commit_status1.ok()) {
      ASSERT_OK(conn1.Execute("ROLLBACK"));
    }
    if (!commit_status2.ok()) {
      ASSERT_OK(conn2.Execute("ROLLBACK"));
    }

    if (RandomUniformBool()) {
      std::swap(conn1, conn2);
    }
  }
  LOG(INFO) << "Test stats: "
            << EXPR_VALUE_FOR_LOG(commit1_fail_count) << ", "
            << EXPR_VALUE_FOR_LOG(insert2_fail_count) << ", "
            << EXPR_VALUE_FOR_LOG(commit2_fail_count);
  ASSERT_GE(commit1_fail_count, iterations / 4);
  ASSERT_GE(insert2_fail_count, iterations / 4);
  ASSERT_EQ(commit2_fail_count, 0);
}

template<IsolationLevel level>
class PgMiniTestTxnHelper : public PgMiniTestNoTxnRetry {
 protected:

  // Check possibility of updating column in case row is referenced by foreign key from another txn.
  void TestReferencedTableUpdate() {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE TABLE pktable (k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(conn.Execute("CREATE TABLE fktable (k INT PRIMARY KEY, fk_p INT, v INT, "
                           "FOREIGN KEY(fk_p) REFERENCES pktable(k))"));
    ASSERT_OK(conn.Execute("INSERT INTO pktable VALUES(1, 2)"));
    auto extra_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_OK(extra_conn.Execute("INSERT INTO fktable VALUES(1, 1, 2)"));
    ASSERT_OK(conn.Execute("UPDATE pktable SET v = 20 WHERE k = 1"));
    // extra_conn creates weak read intent on (1) due to foreign key check.
    // conn UPDATE created strong write intent on (1, v).
    // As a result weak write intent is created for (1).
    // Weak read + weak write on (1) has no conflicts.
    ASSERT_OK(extra_conn.Execute("COMMIT"));
    auto res = ASSERT_RESULT(
        conn.template FetchValue<PGUint64>("SELECT COUNT(*) FROM pktable WHERE v = 20"));
    ASSERT_EQ(res, 1);
  }

  // Check that `FOR KEY SHARE` prevents rows from being deleted even in case not all key
  // components are specified (for SERIALIZABLE isolation level). For REPEATABLE READ, only rows
  // returned to the user are locked.
  void TestRowKeyShareLock(const std::string& cur_name = "") {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

    ASSERT_OK(conn.Execute(
        "CREATE TABLE t (h INT, r1 INT, r2 INT, v INT, PRIMARY KEY(h, r1, r2))"));
    ASSERT_OK(conn.Execute(
        "INSERT INTO t VALUES (1, 2, 3, 4), (1, 2, 30, 40), (1, 3, 4, 5), (10, 2, 3, 4)"));

    // Transaction 1.
    // For SERIALIZABLE level:
    //   SELECT FOR KEY SHARE locks the sub doc key prefix (1, 2) as not all key components are
    //   specified. This also means that no new rows with this matching prefix can be inserted.
    // For REPEATABLE READ level:
    //   Only tuples returned by the SELECT statement are locked.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE h = 1 AND r1 = 2 FOR KEY SHARE", cur_name);

    ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3"));
    ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 30"));

    // Doc key (1, 3, 4) is not locked in both isolation levels.
    ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 3 AND r2 = 4"));

    // New rows with prefix that matches (1, 2) can't be inserted in SERIALIZABLE level.
    if (level == IsolationLevel::SERIALIZABLE_ISOLATION) {
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 100, 100)"));
      // Doc key (1, 2, 2) doesn't exist. But still it conflicts beause of a kStrongRead intent
      // taken on (1, 2) as part of the "fetching" the row when taking for key share locks (which
      // only requires kWeakRead).
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));
    } else {
      ASSERT_OK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 100, 100)"));
      // Delete the row again
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 100"));

      // Doc key (1, 2, 2) doesn't exist.
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));
    }

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 2.
    // For SERIALIZABLE level:
    //   SELECT FOR KEY SHARE locks the sub doc key prefix () as no prefix of the pk is specified.
    // For REPEATABLE READ level:
    //   Only tuples returned by the SELECT statement are locked.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE r2 = 2 FOR KEY SHARE", cur_name);

    if (level == IsolationLevel::SERIALIZABLE_ISOLATION) {
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3"));
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 10 AND r1 = 2 AND r2 = 3"));
      // Doc key (1, 2, 2) doesn't exist.
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));
    } else {
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3"));
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 10 AND r1 = 2 AND r2 = 3"));
      // Re-add the rows
      ASSERT_OK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 3, 4), (10, 2, 3, 4)"));

      ASSERT_OK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 100, 100)"));
      // Delete the row again
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 100"));

      // Doc key (1, 2, 2) doesn't exist.
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));
    }

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 3.
    // For SERIALIZABLE level:
    //   SELECT FOR KEY SHARE locks the sub doc key prefix (1) as not all key components are
    //   specified.
    // For REPEATABLE READ level:
    //   Only tuples returned by the SELECT statement are locked.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE h = 1 AND r2 = 2 FOR KEY SHARE", cur_name);

    // Doc key (10, 2, 3) is not locked.
    ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 10 AND r1 = 2 AND r2 = 3"));

    if (level == IsolationLevel::SERIALIZABLE_ISOLATION) {
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3"));
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 100, 100)"));

      // Doc key (1, 2, 2) doesn't exist.
      ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));
    } else {
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3"));
      // Re-add deleted row
      ASSERT_OK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 3, 4)"));

      ASSERT_OK(ExecuteInTxn(&extra_conn, "INSERT INTO t VALUES (1, 2, 100, 100)"));
      // Delete the row again
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 100"));

      // Doc key (1, 2, 2) doesn't exist.
      ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));
    }

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 4.
    // SELECT FOR KEY SHARE locks one specific row with doc key (1, 2, 3) only.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3 FOR KEY SHARE", cur_name);

    ASSERT_NOK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 3"));
    ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 30"));

    // Doc key (1, 2, 2) doesn't exist.
    ASSERT_OK(ExecuteInTxn(&extra_conn, "DELETE FROM t WHERE h = 1 AND r1 = 2 AND r2 = 2"));

    ASSERT_OK(conn.Execute("COMMIT"));

    auto res = ASSERT_RESULT(conn.template FetchValue<PGUint64>("SELECT COUNT(*) FROM t"));
    ASSERT_EQ(res, 1);
  }

  // Check conflicts according to the following matrix (X - conflict, O - no conflict):
  //                   | FOR KEY SHARE | FOR SHARE | FOR NO KEY UPDATE | FOR UPDATE
  // ------------------+---------------+-----------+-------------------+-----------
  // FOR KEY SHARE     |       O       |     O     |         O         |     X
  // FOR SHARE         |       O       |     O     |         X         |     X
  // FOR NO KEY UPDATE |       O       |     X     |         X         |     X
  // FOR UPDATE        |       X       |     X     |         X         |     X
  void TestRowLockConflictMatrix(const std::string& cur_name = "") {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 1)"));

    // Transaction 1.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE k = 1 FOR UPDATE", cur_name);

    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR UPDATE"));
    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR NO KEY UPDATE"));
    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR SHARE"));
    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR KEY SHARE"));

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 2.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE k = 1 FOR NO KEY UPDATE", cur_name);

    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR UPDATE"));
    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR NO KEY UPDATE"));
    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR SHARE"));
    ASSERT_RESULT(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR KEY SHARE"));

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 3.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE k = 1 FOR SHARE", cur_name);

    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR UPDATE"));
    ASSERT_NOK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR NO KEY UPDATE"));
    ASSERT_RESULT(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR SHARE"));
    ASSERT_RESULT(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR KEY SHARE"));

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 4.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE k = 1 FOR KEY SHARE", cur_name);

    ASSERT_RESULT(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR NO KEY UPDATE"));
    ASSERT_RESULT(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR SHARE"));
    ASSERT_RESULT(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR KEY SHARE"));

    ASSERT_OK(conn.Execute("COMMIT"));

    // Transaction 5.
    // Check FOR KEY SHARE + FOR UPDATE conflict separately
    // as FOR KEY SHARE uses regular and FOR UPDATE uses high txn priority.
    ASSERT_OK(StartTxn(&conn));
    RowLock(&conn, "SELECT * FROM t WHERE k = 1 FOR KEY SHARE", cur_name);

    ASSERT_OK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE k = 1 FOR UPDATE"));

    ASSERT_NOK(conn.Execute("COMMIT"));
  }

  void RowLock(PGConn* connection, const std::string& query, const std::string& cur_name) {
    std::string lock_stmt = query;
    if (!cur_name.empty()) {
      const std::string declare_stmt = Format("DECLARE $0 CURSOR FOR $1", cur_name, query);
      ASSERT_OK(connection->Execute(declare_stmt));

      lock_stmt = Format("FETCH ALL $0", cur_name);
    }
    ASSERT_RESULT(connection->Fetch(lock_stmt));
  }

  void TestInOperatorLock() {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    ASSERT_OK(conn.Execute(
        "CREATE TABLE t (h INT, r1 INT, r2 INT, PRIMARY KEY(h, r1 ASC, r2 ASC))"));
    ASSERT_OK(conn.Execute(
        "INSERT INTO t VALUES (1, 11, 1),(1, 12, 1),(1, 13, 1),(2, 11, 2),(2, 12, 2),(2, 13, 2)"));
    ASSERT_OK(StartTxn(&conn));
    auto res = ASSERT_RESULT(conn.Fetch(
        "SELECT * FROM t WHERE h = 1 AND r1 IN (11, 12) AND r2 = 1 FOR KEY SHARE"));

    auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_NOK(extra_conn.Execute("DELETE FROM t WHERE h = 1 AND r1 = 11 AND r2 = 1"));
    ASSERT_OK(extra_conn.Execute("ROLLBACK"));
    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_OK(extra_conn.Execute("DELETE FROM t WHERE h = 1 AND r1 = 13 AND r2 = 1"));
    ASSERT_OK(extra_conn.Execute("COMMIT"));

    ASSERT_OK(conn.Execute("COMMIT;"));

    ASSERT_OK(conn.Execute("BEGIN;"));
    res = ASSERT_RESULT(conn.Fetch(
        "SELECT * FROM t WHERE h IN (1, 2) AND r1 = 11 FOR KEY SHARE"));

    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_NOK(extra_conn.Execute("DELETE FROM t WHERE h = 1 AND r1 = 11 AND r2 = 1"));
    ASSERT_OK(extra_conn.Execute("ROLLBACK"));
    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_NOK(extra_conn.Execute("DELETE FROM t WHERE h = 2 AND r1 = 11 AND r2 = 2"));
    ASSERT_OK(extra_conn.Execute("ROLLBACK"));
    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_OK(extra_conn.Execute("DELETE FROM t WHERE h = 2 AND r1 = 12 AND r2 = 2"));
    ASSERT_OK(extra_conn.Execute("COMMIT"));

    ASSERT_OK(conn.Execute("COMMIT;"));
    const auto count = ASSERT_RESULT(conn.template FetchValue<PGUint64>("SELECT COUNT(*) FROM t"));
    ASSERT_EQ(4, count);
  }

  // Check that 2 rows with same PK can't be inserted from different txns.
  void TestDuplicateInsert() {
    DuplicateInsertImpl(IndexRequirement::NO, true /* low_pri_txn_insert_same_key */);
  }

  // Check that 2 rows with identical unique index can't be inserted from different txns.
  void TestDuplicateUniqueIndexInsert() {
    DuplicateInsertImpl(IndexRequirement::UNIQUE, false /* low_pri_txn_insert_same_key */);
  }

  // Check that 2 rows with identical non-unique index can be inserted from different txns.
  void TestDuplicateNonUniqueIndexInsert() {
    DuplicateInsertImpl(IndexRequirement::NON_UNIQUE,
    false /* low_pri_txn_insert_same_key */,
    true /* low_pri_txn_succeed */);
  }

  static Status StartTxn(PGConn* connection) {
    return TxnHelper<level>::StartTxn(connection);
  }

  static Status ExecuteInTxn(PGConn* connection, const std::string& query) {
    return TxnHelper<level>::ExecuteInTxn(connection, query);
  }

  static Result<PGResultPtr> FetchInTxn(PGConn* connection, const std::string& query) {
    return TxnHelper<level>::FetchInTxn(connection, query);
  }

 private:
  enum class IndexRequirement {
    NO,
    UNIQUE,
    NON_UNIQUE
  };

  void DuplicateInsertImpl(
    IndexRequirement index, bool low_pri_txn_insert_same_key, bool low_pri_txn_succeed = false) {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
    if (index != IndexRequirement::NO) {
      ASSERT_OK(conn.Execute(Format("CREATE $0 INDEX ON t(v)",
                                    index == IndexRequirement::UNIQUE ? "UNIQUE" : "")));
    }
    ASSERT_OK(StartTxn(&conn));
    auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
    ASSERT_OK(StartTxn(&extra_conn));
    ASSERT_OK(extra_conn.Execute(Format("INSERT INTO t VALUES($0, 10)",
                                        low_pri_txn_insert_same_key ? "1" : "2")));
    ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 10)"));
    ASSERT_OK(conn.Execute("COMMIT"));
    const auto low_pri_txn_commit_status = extra_conn.Execute("COMMIT");
    if (low_pri_txn_succeed) {
      ASSERT_OK(low_pri_txn_commit_status);
    } else {
      ASSERT_NOK(low_pri_txn_commit_status);
    }
    const auto count = ASSERT_RESULT(
      extra_conn.template FetchValue<PGUint64>("SELECT COUNT(*) FROM t WHERE v = 10"));
    ASSERT_EQ(low_pri_txn_succeed ? 2 : 1, count);
  }
};

class PgMiniTestTxnHelperSerializable
    : public PgMiniTestTxnHelper<IsolationLevel::SERIALIZABLE_ISOLATION> {
 protected:
  // Check two SERIALIZABLE txns has no conflict in case of updating same column in same row.
  void TestSameColumnUpdate(bool enable_expression_pushdown) {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v1 INT, v2 INT)"));
    ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 2, 3)"));

    if (!enable_expression_pushdown) {
      ASSERT_OK(conn.Execute("SET yb_enable_expression_pushdown TO false"));
      ASSERT_OK(extra_conn.Execute("SET yb_enable_expression_pushdown TO false"));
    }

    ASSERT_OK(StartTxn(&conn));
    ASSERT_OK(conn.Execute("UPDATE t SET v1 = 20 WHERE k = 1"));

    ASSERT_OK(ExecuteInTxn(&extra_conn, "UPDATE t SET v1 = 40 WHERE k = 1"));

    ASSERT_OK(conn.Execute("COMMIT"));

    auto res = ASSERT_RESULT(conn.FetchValue<PGUint64>("SELECT COUNT(*) FROM t WHERE v1 = 20"));
    ASSERT_EQ(res, 1);

    ASSERT_OK(StartTxn(&conn));
    // Next statement will lock whole row for both read and write due to expression
    ASSERT_OK(conn.Execute("UPDATE t SET v2 = v2 * 2 WHERE k = 1"));

    ASSERT_NOK(ExecuteInTxn(&extra_conn, "UPDATE t SET v2 = 10 WHERE k = 1"));
    ASSERT_NOK(ExecuteInTxn(&extra_conn, "UPDATE t SET v1 = 10 WHERE k = 1"));

    ASSERT_OK(conn.Execute("COMMIT"));

    res = ASSERT_RESULT(conn.FetchValue<PGUint64>("SELECT COUNT(*) FROM t WHERE v2 = 6"));
    ASSERT_EQ(res, 1);
  }
};

class PgRowLockTxnHelperSnapshotTest
    : public PgMiniTestTxnHelper<IsolationLevel::SNAPSHOT_ISOLATION> {
 protected:
  // Check two SNAPSHOT txns has a conflict in case of updating same column in same row.
  void TestSameColumnUpdate(bool enable_expression_pushdown) {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

    if (!enable_expression_pushdown) {
      ASSERT_OK(conn.Execute("SET yb_enable_expression_pushdown TO false"));
      ASSERT_OK(extra_conn.Execute("SET yb_enable_expression_pushdown TO false"));
    }

    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 2)"));

    ASSERT_OK(StartTxn(&conn));
    ASSERT_OK(conn.Execute("UPDATE t SET v = 20 WHERE k = 1"));

    ASSERT_NOK(ExecuteInTxn(&extra_conn, "UPDATE t SET v = 40 WHERE k = 1"));

    ASSERT_OK(conn.Execute("COMMIT"));

    const auto res = ASSERT_RESULT(conn.FetchValue<PGUint64>(
        "SELECT COUNT(*) FROM t WHERE v = 20"));
    ASSERT_EQ(res, 1);
  }
};

TEST_F_EX(PgRowLockTest,
          ReferencedTableUpdateSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestReferencedTableUpdate();
}

TEST_F_EX(PgRowLockTest,
          ReferencedTableUpdateSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestReferencedTableUpdate();
}

TEST_F_EX(PgRowLockTest,
          ReferencedTableUpdateReadCommitted,
          PgMiniTestTxnHelper<IsolationLevel::READ_COMMITTED>) {
  TestReferencedTableUpdate();
}

TEST_F_EX(PgRowLockTest,
          RowKeyShareLockSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestRowKeyShareLock();
}

TEST_F_EX(PgRowLockTest,
          RowKeyShareLockSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestRowKeyShareLock();
}

TEST_F_EX(PgRowLockTest,
          RowLockConflictMatrixSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestRowLockConflictMatrix();
}

TEST_F_EX(PgRowLockTest,
          RowLockConflictMatrixSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestRowLockConflictMatrix();
}

TEST_F_EX(PgRowLockTest,
          CursorRowKeyShareLockSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestRowKeyShareLock("cur_name");
}

TEST_F_EX(PgRowLockTest,
          CursorRowKeyShareLockSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestRowKeyShareLock("cur_name");
}

TEST_F_EX(PgRowLockTest,
          CursorRowLockConflictMatrixSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestRowLockConflictMatrix("cur_name");
}

TEST_F_EX(PgRowLockTest,
          CursorRowLockConflictMatrixSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestRowLockConflictMatrix("cur_name");
}

TEST_F_EX(PgRowLockTest,
          SameColumnUpdateSerializableWithPushdown,
          PgMiniTestTxnHelperSerializable) {
  TestSameColumnUpdate(true /* enable_expression_pushdown */);
}

TEST_F_EX(PgRowLockTest,
          SameColumnUpdateSnapshotWithPushdown,
          PgRowLockTxnHelperSnapshotTest) {
  TestSameColumnUpdate(true /* enable_expression_pushdown */);
}

TEST_F_EX(PgRowLockTest,
          SameColumnUpdateSerializableWithoutPushdown,
          PgMiniTestTxnHelperSerializable) {
  TestSameColumnUpdate(false /* enable_expression_pushdown */);
}

TEST_F_EX(PgRowLockTest,
          SameColumnUpdateSnapshotWithoutPushdown,
          PgRowLockTxnHelperSnapshotTest) {
  TestSameColumnUpdate(false /* enable_expression_pushdown */);
}

TEST_F_EX(PgRowLockTest,
          DuplicateInsertSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestDuplicateInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateInsertSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestDuplicateInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateInsertReadCommitted,
          PgMiniTestTxnHelper<IsolationLevel::READ_COMMITTED>) {
  TestDuplicateInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateUniqueIndexInsertSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestDuplicateUniqueIndexInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateUniqueIndexInsertSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestDuplicateUniqueIndexInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateUniqueIndexInsertReadCommitted,
          PgMiniTestTxnHelper<IsolationLevel::READ_COMMITTED>) {
  TestDuplicateUniqueIndexInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateNonUniqueIndexInsertSerializable,
          PgMiniTestTxnHelperSerializable) {
  TestDuplicateNonUniqueIndexInsert();
}

TEST_F_EX(PgRowLockTest,
          DuplicateNonUniqueIndexInsertSnapshot,
          PgRowLockTxnHelperSnapshotTest) {
  TestDuplicateNonUniqueIndexInsert();
}

TEST_F_EX(PgRowLockTest,
          YB_DISABLE_TEST_IN_TSAN(DuplicateNonUniqueIndexInsertReadCommitted),
          PgMiniTestTxnHelper<IsolationLevel::READ_COMMITTED>) {
  TestDuplicateNonUniqueIndexInsert();
}

TEST_F_EX(
  PgRowLockTest, SnapshotInOperatorLock,
  PgRowLockTxnHelperSnapshotTest) {
  TestInOperatorLock();
}

TEST_F_EX(
    PgRowLockTest, SerializableInOperatorLock,
    PgMiniTestTxnHelperSerializable) {
  TestInOperatorLock();
}

TEST_F_EX(
    PgRowLockTest, PartialKeyRowLockConflict,
    PgMiniTestTxnHelperSerializable) {
  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto extra_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

  ASSERT_OK(conn.Execute("CREATE TABLE t (h INT, r INT, v INT, PRIMARY KEY(h, r))"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (1, 2, 3)"));

  ASSERT_OK(StartTxn(&conn));
  ASSERT_OK(conn.Fetch("SELECT * FROM t WHERE h = 1 AND r = 2 FOR KEY SHARE"));

  // Check that FOR KEY SHARE + FOR UPDATE conflicts.
  // FOR KEY SHARE uses regular and FOR UPDATE uses high txn priority.
  ASSERT_OK(FetchInTxn(&extra_conn, "SELECT * FROM t WHERE h = 1 FOR UPDATE"));
  ASSERT_NOK(conn.Execute("COMMIT"));
}

}  // namespace yb::pgwrapper
