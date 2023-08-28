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

#include <thread>

#include <gtest/gtest.h>

#include "yb/gutil/dynamic_annotations.h"

#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(enable_wait_queues);
DECLARE_int32(ysql_max_read_restart_attempts);
DECLARE_int32(ysql_max_write_restart_attempts);

namespace yb::pgwrapper {

class PgMiscConflictsTest : public PgMiniTestBase {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_wait_queues) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_write_restart_attempts) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_max_read_restart_attempts) = 0;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 1;
  }
};

// Test checks conflict detection on colocated table in case of explicit row lock
TEST_F(PgMiscConflictsTest, YB_DISABLE_TEST_IN_TSAN(TablegroupRowLock)) {
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
TEST_F(PgMiscConflictsTest, YB_DISABLE_TEST_IN_TSAN(TablegroupFKDelete)) {
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

} // namespace yb::pgwrapper
