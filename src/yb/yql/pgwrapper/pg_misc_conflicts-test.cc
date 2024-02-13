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

#include <string_view>

#include <gtest/gtest.h>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/tostring.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_bool(enable_wait_queues);
DECLARE_string(ysql_pg_conf_csv);

namespace yb::pgwrapper {

using namespace std::literals;

namespace {

YB_DEFINE_ENUM(ExplicitRowLock, (kNoLock)(kForUpdate)(kForNoKeyUpdate)(kForShare)(kForKeyShare));

template<class T>
std::string TestParamToString(const testing::TestParamInfo<T>& param_info) {
  return ToString(param_info.param);
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

} // namespace yb::pgwrapper
