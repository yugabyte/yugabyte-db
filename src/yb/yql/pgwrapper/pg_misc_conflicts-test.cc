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

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

YB_DEFINE_ENUM(ExplicitRowLock, (kNoLock)(kForUpdate)(kForNoKeyUpdate)(kForShare)(kForKeyShare));

template<class T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
}

Status SetExplicitRowLockingBatchSize(PGConn& conn, size_t value) {
  return conn.ExecuteFormat("SET yb_explicit_row_locking_batch_size = $0", value);
}

} // namespace

class PgMiscConflictsTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    EnableFailOnConflict();
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

// Test checks conflict detection on colocated table in case of explicit row lock
TEST_F(PgMiscConflictsTest, TablegroupRowLock) {
  constexpr auto* kTable = "tbl";

  auto conn = ASSERT_RESULT(Connect());
  auto aux_conn = ASSERT_RESULT(Connect());
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

// Test checks conflict detection on colocated table with FK
TEST_F(PgMiscConflictsTest, TablegroupFKDelete) {
  constexpr auto* kRefTable = "ref_tbl";
  constexpr auto* kTable = "tbl";

  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

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
TEST_F(PgMiscConflictsTest, SerializableIsolationWeakReadIntent) {
  constexpr auto* kTable = "tbl";

  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));

  ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES(1, 10)", kTable));

  ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  ASSERT_OK(conn.ExecuteFormat("UPDATE $0 SET v = v + 1 WHERE k = 1", kTable));

  ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
  // No conflict. Because only key column is read and StrongRead intent is created on
  // kLivenessColumn of k = 1 row
  ASSERT_OK(aux_conn.FetchFormat("SELECT k FROM $0 WHERE k = 1", kTable));
  ASSERT_OK(aux_conn.CommitTransaction());

  auto serialize_access_conflict_checker = [&aux_conn](auto&&... args) {
    ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    auto status = aux_conn.ExecuteFormat(std::forward<decltype(args)>(args)...);
    ASSERT_TRUE(IsSerializeAccessError(status)) << status;
    ASSERT_OK(aux_conn.RollbackTransaction());
  };

  // Conflict. Because not only key columns are read and StrongRead intent is created on
  // entire k = 1 row
  serialize_access_conflict_checker("SELECT * FROM $0 WHERE k = 1", kTable);

  // Conflict due to scan (non single row read).
  serialize_access_conflict_checker("SELECT * FROM $0", kTable);

  // Conflict due to scan (non single row read).
  serialize_access_conflict_checker("SELECT k FROM $0", kTable);

  ASSERT_OK(conn.CommitTransaction());
}

class PgRowLockTest : public PgMiscConflictsTest,
                      public testing::WithParamInterface<ExplicitRowLock> {
 protected:
  void SetUp() override {
    PgMiscConflictsTest::SetUp();
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", kTable));
    ASSERT_OK(conn.ExecuteFormat("INSERT INTO $0 VALUES (1, 10)", kTable));
  }

  // Test checks that that serializable read locks are taken even when an explicit row locking
  // clause is specified.
  void DoSerializableTxnUpdate() {
    auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
    auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
    ASSERT_OK(conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    ASSERT_OK(conn.FetchFormat(
        "SELECT * FROM $0 WHERE k = 1 $1", kTable, ToQueryString(GetParam())));
    ASSERT_OK(aux_conn.StartTransaction(IsolationLevel::SERIALIZABLE_ISOLATION));
    const auto status = aux_conn.ExecuteFormat("UPDATE $0 SET v = 100 WHERE k = 1", kTable);
    ASSERT_TRUE(IsSerializeAccessError(status)) << "Unexpected status " << status;
    ASSERT_OK(aux_conn.RollbackTransaction());
    ASSERT_OK(conn.CommitTransaction());
  }

  static constexpr auto kTable = "tbl"sv;

 private:
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

INSTANTIATE_TEST_CASE_P(
    PgMiscConflictsTest, PgRowLockTest,
    testing::ValuesIn(kExplicitRowLockArray), TestParamToString<ExplicitRowLock>);

TEST_P(PgRowLockTest, SerializableTxnUpdate) {
  DoSerializableTxnUpdate();
}

TEST_F(PgMiscConflictsTest, DifferentColumnsConcurrentUpdate) {
  auto conn = ASSERT_RESULT(SetHighPriTxn(Connect()));
  auto aux_conn = ASSERT_RESULT(SetLowPriTxn(Connect()));
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
  void SetUp() override {
    PgMiscConflictsTest::SetUp();
    CHECK_OK(Prepare());
  }

  Status CreateKVTableWithValues(std::string_view name) {
    RETURN_NOT_OK(conn_->ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v INT)", name));
    return conn_->ExecuteFormat("INSERT INTO $0 SELECT s, s FROM generate_series(1, 3) AS s", name);
  }

  // Helper function to check that error message in batched and non batched mode matches.
  Status CheckRowLockErrorMessage(std::string_view table_name, const std::string& lock_query) {
    static const auto kErrorPattern = "could not obtain lock on row in relation \"$0\""s;

    auto row_locker =
        [&conn = *low_pri_conn_, &lock_query](size_t batch_size) mutable -> Result<std::string> {
          RETURN_NOT_OK(SetExplicitRowLockingBatchSize(conn, batch_size));
          return TryLockRowWithFailure(conn, lock_query);
        };
    RETURN_NOT_OK(conn_->StartTransaction(IsolationLevel::READ_COMMITTED));
    RETURN_NOT_OK(conn_->Fetch(Format("SELECT * FROM $0 WHERE k = 1 FOR UPDATE", table_name)));

    const auto non_batched_mode_error_msg = VERIFY_RESULT(row_locker(/*batch_size =*/1));
    const auto expected_substring = Format(kErrorPattern, table_name);
    SCHECK_STR_CONTAINS(non_batched_mode_error_msg, expected_substring);
    const auto batched_mode_error_msg = VERIFY_RESULT(row_locker(/*batch_size =*/1024));
    SCHECK_EQ(
        batched_mode_error_msg, non_batched_mode_error_msg, IllegalState,
        "Error messages in batched and non-batched mode expected to be equal");

    return conn_->CommitTransaction();
  }

  static constexpr auto kTableName_1 = "very_very_long_name_1"sv;
  static constexpr auto kTableName_2 = "very_very_long_name_2"sv;

  std::optional<PGConn> conn_;
  std::optional<PGConn> low_pri_conn_;

 private:
  static Result<std::string> TryLockRowWithFailure(PGConn& conn, const std::string& lock_query) {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::READ_COMMITTED));
    const auto status = ResultToStatus(conn.Fetch(lock_query));
    SCHECK(!status.ok(), IllegalState, "Row lock query expected to fail");
    RETURN_NOT_OK(conn.RollbackTransaction());
    return AuxilaryMessage(status).value();
  }

  Status Prepare() {
    DCHECK(!conn_ && !low_pri_conn_);
    conn_.emplace(VERIFY_RESULT(SetHighPriTxn(Connect())));
    low_pri_conn_.emplace(VERIFY_RESULT(SetLowPriTxn(Connect())));
    return CreateKVTableWithValues(kTableName_1);
  }

};

// The test checks error messages in case of using batched and non batched mode for explicit
// row locking match and both contains expected table name.
TEST_F_EX(
    PgMiscConflictsTest, ExplicitRowLockingSingleTableErrorMessage, PgExplicitRowLockingTest) {
  constexpr auto kTableName = kTableName_1;

  ASSERT_OK(CheckRowLockErrorMessage(
      kTableName, Format("SELECT * FROM $0 WHERE k = 1 FOR SHARE NOWAIT", kTableName)));
}

// The test checks error messages in case of using batched and non batched mode for explicit
// row locking match and both contains expected table name.
TEST_F_EX(
    PgMiscConflictsTest, ExplicitRowLockingMultipleTablesErrorMessage, PgExplicitRowLockingTest) {
  ASSERT_OK(CreateKVTableWithValues(kTableName_2));

  const auto lock_query = Format(
      "SELECT * FROM $0 INNER JOIN $1 ON $0.k = $1.k WHERE $0.k = 1 FOR SHARE NOWAIT",
      kTableName_1, kTableName_2);
  ASSERT_OK(CheckRowLockErrorMessage(kTableName_1, lock_query));
  ASSERT_OK(CheckRowLockErrorMessage(kTableName_2, lock_query));
}

} // namespace yb::pgwrapper
