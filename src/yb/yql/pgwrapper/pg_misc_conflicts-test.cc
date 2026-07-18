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

#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <gtest/gtest.h>

#include "yb/common/pgsql_error.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/tostring.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(skip_prefix_locks);
DECLARE_bool(ysql_enable_packed_row);

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

YB_DEFINE_ENUM(ExplicitRowLock, (kNoLock)(kForUpdate)(kForNoKeyUpdate)(kForShare)(kForKeyShare));
YB_DEFINE_ENUM(SkipPrefixLocks, (kFalse)(kTrue));

template<class T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}

Status SetExplicitRowLockingBatchSize(PGConn& conn, size_t value) {
  return conn.ExecuteFormat("SET yb_explicit_row_locking_batch_size = $0", value);
}

} // namespace

class PgMiscConflictsTestBase : public PgMiniTestBase {
 protected:
  void SetUp() override {
    auto mode = GetSkipPrefixLocks();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_skip_prefix_locks) = mode == SkipPrefixLocks::kTrue;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_packed_row) = true;
    EnableFailOnConflict();
    PgMiniTestBase::SetUp();
  }

  virtual SkipPrefixLocks GetSkipPrefixLocks() const = 0;

  size_t NumTabletServers() override {
    return 1;
  }
};

// Parameterized on SkipPrefixLocks so every test automatically runs in both modes.
class PgMiscConflictsTest : public PgMiscConflictsTestBase,
                            public testing::WithParamInterface<SkipPrefixLocks> {
 protected:
  SkipPrefixLocks GetSkipPrefixLocks() const override { return GetParam(); }
};

INSTANTIATE_TEST_SUITE_P(
    , PgMiscConflictsTest,
    testing::ValuesIn(kSkipPrefixLocksArray),
    TestParamToString<SkipPrefixLocks>);

TEST_P(PgMiscConflictsTest, TablegroupRowLock) {
  constexpr auto* kTable = "tbl";

  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTable));
  ASSERT_OK(conn.Execute("DROP TABLEGROUP IF EXISTS tg"));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg"));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT, v INT) TABLEGROUP tg", kTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 11)", kTable));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_RESULT(conn.FetchFormat("SELECT v FROM $0 WHERE k = 1 FOR SHARE", kTable));

  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  auto status = aux_conn.ExecuteFormat("UPDATE $0 SET v = 111 WHERE k = 1", kTable);
  ASSERT_NOK(status);
  ASSERT_TRUE(HasTransactionError(status));
  ASSERT_OK(aux_conn.CommitTransaction());

  ASSERT_OK(conn.CommitTransaction());
}

TEST_P(PgMiscConflictsTest, TablegroupFKDelete) {
  constexpr auto* kRefTable = "ref_tbl";
  constexpr auto* kTable = "tbl";

  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTable));
  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kRefTable));
  ASSERT_OK(conn.Execute("DROP TABLEGROUP IF EXISTS tg"));
  ASSERT_OK(conn.Execute("CREATE TABLEGROUP tg"));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0(k INT PRIMARY KEY, v INT) TABLEGROUP tg", kRefTable));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0(k INT PRIMARY KEY, fk INT REFERENCES $1(k)) TABLEGROUP tg",
      kTable, kRefTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 1)", kRefTable));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0 WHERE k = 1", kRefTable));

  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_NOK(aux_conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 1)", kTable));
  ASSERT_OK(aux_conn.RollbackTransaction());

  ASSERT_OK(conn.CommitTransaction());
}

// Test checks absence of conflict with concurrent update in case only key columns are read by the
// query and all key columns are specified.
TEST_P(PgMiscConflictsTest, SerializableIsolationWeakReadIntent) {
  const bool skip_prefix_locks = ANNOTATE_UNPROTECTED_READ(FLAGS_skip_prefix_locks);
  constexpr auto* kTable = "tbl";

  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

  ASSERT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTable));
  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 10)", kTable));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v = v + 1 WHERE k = 1", kTable));

  auto serialize_access_conflict_checker = [&aux_conn](auto&&... args) {
    ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    auto status = aux_conn.ExecuteFormat(std::forward<decltype(args)>(args)...);
    ASSERT_TRUE(IsSerializeAccessError(status)) << status;
    ASSERT_OK(aux_conn.RollbackTransaction());
  };

  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  if (skip_prefix_locks) {
    serialize_access_conflict_checker("SELECT k FROM $0 WHERE k = 1", kTable);
  } else {
    // No conflict. Because only key column is read and StrongRead intent is created on
    // kLivenessColumn of k = 1 row
    ASSERT_OK(aux_conn.FetchFormat("SELECT k FROM $0 WHERE k = 1", kTable));
    ASSERT_OK(aux_conn.CommitTransaction());
  }

  // Conflict. Because not only key columns are read and StrongRead intent is created on
  // entire k = 1 row
  serialize_access_conflict_checker("SELECT * FROM $0 WHERE k = 1", kTable);

  // Conflict due to scan (non single row read).
  serialize_access_conflict_checker("SELECT * FROM $0", kTable);

  // Conflict due to scan (non single row read).
  serialize_access_conflict_checker("SELECT k FROM $0", kTable);

  ASSERT_OK(conn.CommitTransaction());
}

using PgRowLockTestParam = std::tuple<SkipPrefixLocks, ExplicitRowLock>;

namespace {

std::string PgRowLockTestParamToString(const testing::TestParamInfo<PgRowLockTestParam>& info) {
  return Format("$0_$1", std::get<0>(info.param), std::get<1>(info.param));
}

} // namespace

class PgRowLockTest : public PgMiscConflictsTestBase,
                      public testing::WithParamInterface<PgRowLockTestParam> {
 protected:
  SkipPrefixLocks GetSkipPrefixLocks() const override { return std::get<0>(GetParam()); }

  // Test checks that that serializable read locks are taken even when an explicit row locking
  // clause is specified.
  void DoSerializableTxnUpdate() {
    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(setup_conn.ExecuteFormat("DROP TABLE IF EXISTS $0", kTable));
    ASSERT_OK(setup_conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
    ASSERT_OK(setup_conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10)", kTable));

    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK(conn.FetchFormat(
        "SELECT * FROM $0 WHERE k = 1 $1", kTable, ToQueryString(GetRowLock())));
    ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    const auto status = aux_conn.ExecuteFormat("UPDATE $0 SET v = 100 WHERE k = 1", kTable);
    ASSERT_TRUE(IsSerializeAccessError(status)) << "Unexpected status " << status;
    ASSERT_OK(aux_conn.RollbackTransaction());
    ASSERT_OK(conn.CommitTransaction());
  }

  static constexpr auto kTable = "tbl"sv;

 private:
  ExplicitRowLock GetRowLock() const { return std::get<1>(GetParam()); }

  std::string_view ToQueryString(ExplicitRowLock row_lock) {
    switch(row_lock) {
      case ExplicitRowLock::kNoLock: return std::string_view();
      case ExplicitRowLock::kForUpdate: return "FOR UPDATE"sv;
      case ExplicitRowLock::kForNoKeyUpdate: return "FOR NO KEY UPDATE"sv;
      case ExplicitRowLock::kForShare: return "FOR SHARE"sv;
      case ExplicitRowLock::kForKeyShare: return "FOR KEY SHARE"sv;
    }
    FATAL_INVALID_ENUM_VALUE(ExplicitRowLock, row_lock);
  }
};

INSTANTIATE_TEST_SUITE_P(
    PgMiscConflictsTest, PgRowLockTest,
    testing::Combine(
        testing::ValuesIn(kSkipPrefixLocksArray),
        testing::ValuesIn(kExplicitRowLockArray)),
    PgRowLockTestParamToString);

TEST_P(PgRowLockTest, SerializableTxnUpdate) {
  DoSerializableTxnUpdate();
}

TEST_P(PgMiscConflictsTest, DifferentColumnsConcurrentUpdate) {
  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
  ASSERT_OK(conn.Execute("DROP TABLE IF EXISTS t"));
  ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v1 INT, v2 INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES(1, 1, 1)"));
  ASSERT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_OK(conn.Execute("UPDATE t SET v2 = 20 WHERE k = 1"));
  ASSERT_TRUE(IsSerializeAccessError(aux_conn.Execute("UPDATE t SET v1 = 10 WHERE k = 1")));
  ASSERT_OK(conn.CommitTransaction());
  ASSERT_OK(aux_conn.RollbackTransaction());
}

class PgExplicitRowLockingTest : public PgMiscConflictsTest {
 protected:
  Status CreateKVTableWithValues(PGConn& conn, std::string_view name) {
    RETURN_NOT_OK(conn.ExecuteFormat("DROP TABLE IF EXISTS $0", name));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", name));
    return conn.ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 3) AS s", name);
  }

  // Helper function to check that error message in batched and non batched mode matches.
  Status CheckRowLockErrorMessage(
      PGConn& conn, PGConn& low_pri_conn,
      std::string_view table_name, const std::string& lock_query) {
    static const auto kErrorPattern = "could not obtain lock on row in relation \"$0\""s;

    auto row_locker =
        [&low_pri_conn, &lock_query](size_t batch_size) mutable -> Result<std::string> {
          RETURN_NOT_OK(SetExplicitRowLockingBatchSize(low_pri_conn, batch_size));
          return TryLockRowWithFailure(low_pri_conn, lock_query);
        };
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
    RETURN_NOT_OK(conn.Fetch(Format("SELECT * FROM $0 WHERE k = 1 FOR UPDATE", table_name)));

    const auto non_batched_mode_error_msg = VERIFY_RESULT(row_locker(/*batch_size =*/1));
    const auto expected_substring = Format(kErrorPattern, table_name);
    SCHECK_STR_CONTAINS(non_batched_mode_error_msg, expected_substring);
    const auto batched_mode_error_msg = VERIFY_RESULT(row_locker(/*batch_size =*/1024));
    SCHECK_EQ(
        batched_mode_error_msg, non_batched_mode_error_msg, IllegalState,
        "Error messages in batched and non-batched mode expected to be equal");

    return conn.CommitTransaction();
  }

  static constexpr auto kTableName_1 = "very_very_long_name_1"sv;
  static constexpr auto kTableName_2 = "very_very_long_name_2"sv;

 private:
  static Result<std::string> TryLockRowWithFailure(PGConn& conn, const std::string& lock_query) {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
    const auto status = ResultToStatus(conn.Fetch(lock_query));
    SCHECK(!status.ok(), IllegalState, "Row lock query expected to fail");
    RETURN_NOT_OK(conn.RollbackTransaction());
    return AuxilaryMessage(status).value();
  }
};

INSTANTIATE_TEST_SUITE_P(
    , PgExplicitRowLockingTest,
    testing::ValuesIn(kSkipPrefixLocksArray),
    TestParamToString<SkipPrefixLocks>);

// The test checks error messages in case of using batched and non batched mode for explicit
// row locking match and both contains expected table name.
TEST_P(PgExplicitRowLockingTest, ExplicitRowLockingSingleTableErrorMessage) {
  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto low_pri_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
  ASSERT_OK(CreateKVTableWithValues(conn, kTableName_1));

  ASSERT_OK(CheckRowLockErrorMessage(
      conn, low_pri_conn, kTableName_1,
      Format("SELECT * FROM $0 WHERE k = 1 FOR SHARE NOWAIT", kTableName_1)));
}

// The test checks error messages in case of using batched and non batched mode for explicit
// row locking match and both contains expected table name.
TEST_P(PgExplicitRowLockingTest, ExplicitRowLockingMultipleTablesErrorMessage) {
  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto low_pri_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
  ASSERT_OK(CreateKVTableWithValues(conn, kTableName_1));
  ASSERT_OK(CreateKVTableWithValues(conn, kTableName_2));

  const auto lock_query = Format(
      "SELECT * FROM $0 INNER JOIN $1 ON $0.k = $1.k WHERE $0.k = 1 FOR SHARE NOWAIT",
      kTableName_1, kTableName_2);
  ASSERT_OK(CheckRowLockErrorMessage(conn, low_pri_conn, kTableName_1, lock_query));
  ASSERT_OK(CheckRowLockErrorMessage(conn, low_pri_conn, kTableName_2, lock_query));
}

} // namespace yb::pgwrapper
