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

#include <gtest/gtest.h>

#include "yb/common/transaction.pb.h"

#include "yb/util/flags.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(yb_enable_read_committed_isolation);
DECLARE_double(TEST_inject_read_restart_with_probability);

namespace yb::pgwrapper {

// Read restarts can also be injected artificially, without a concurrent writer,
// by asking the tablet server to report a spurious one.
class PgInjectReadRestartTest : public PgMiniTestBase {
 protected:
  static constexpr auto kSingleTabletQuery = "SELECT k FROM keys";
  static constexpr auto kFanOutQuery = "SELECT COUNT(*) FROM hashed_keys";

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_enable_read_committed_isolation) = true;
    PgMiniTestBase::SetUp();
  }

  Result<PGConn> SetUpKeysTable() {
    auto setup_conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(setup_conn.Execute("CREATE TABLE keys (k INT, PRIMARY KEY(k ASC))"));
    RETURN_NOT_OK(setup_conn.Execute("INSERT INTO keys(k) SELECT GENERATE_SERIES(1, 10)"));
    RETURN_NOT_OK(setup_conn.Execute(
        "CREATE TABLE hashed_keys (k INT, PRIMARY KEY(k HASH)) SPLIT INTO 3 TABLETS"));
    RETURN_NOT_OK(setup_conn.Execute("INSERT INTO hashed_keys(k) SELECT GENERATE_SERIES(1, 10)"));

    auto read_conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(read_conn.Execute("SET yb_max_query_layer_retries = 0"));
    return read_conn;
  }
};

TEST_F(PgInjectReadRestartTest, RepeatableReadRestart) {
  auto read_conn = ASSERT_RESULT(SetUpKeysTable());
  ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  // Pins the read time, so subsequent statements send it explicitly.
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM keys")), 10);

  ASSERT_OK(SET_FLAG(TEST_inject_read_restart_with_probability, 1.0));
  ASSERT_NOK_STR_CONTAINS(
      read_conn.FetchRows<int32_t>(kSingleTabletQuery), "Restart read required");

  // The flag is runtime, so it can be scoped to individual statements.
  ASSERT_OK(SET_FLAG(TEST_inject_read_restart_with_probability, 0.0));
  ASSERT_OK(read_conn.RollbackTransaction());

  // No restart is injected while the flag is unset.
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRows<int32_t>(kSingleTabletQuery)).size(), 10);
}

TEST_F(PgInjectReadRestartTest, RepeatableReadRestartOnWrite) {
  auto read_conn = ASSERT_RESULT(SetUpKeysTable());
  ASSERT_OK(read_conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRow<int64_t>("SELECT COUNT(*) FROM keys")), 10);

  ASSERT_OK(SET_FLAG(TEST_inject_read_restart_with_probability, 1.0));
  ASSERT_NOK_STR_CONTAINS(
      read_conn.Execute("DELETE FROM keys WHERE k = 1"), "Restart read required");

  ASSERT_OK(SET_FLAG(TEST_inject_read_restart_with_probability, 0.0));
  ASSERT_OK(read_conn.RollbackTransaction());
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRows<int32_t>(kSingleTabletQuery)).size(), 10);
}

TEST_F(PgInjectReadRestartTest, DocDbPickedReadTimeRestartIsRetriedInDocDb) {
  auto read_conn = ASSERT_RESULT(SetUpKeysTable());
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRows<int32_t>("SELECT k FROM keys LIMIT 1")).size(), 1);

  ASSERT_OK(SET_FLAG(TEST_inject_read_restart_with_probability, 1.0));
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRows<int32_t>(kSingleTabletQuery)).size(), 10);
}

TEST_F(PgInjectReadRestartTest, FanOutReadRestart) {
  auto read_conn = ASSERT_RESULT(SetUpKeysTable());
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRow<int64_t>(kFanOutQuery)), 10);

  ASSERT_OK(SET_FLAG(TEST_inject_read_restart_with_probability, 1.0));
  ASSERT_NOK_STR_CONTAINS(read_conn.FetchRow<int64_t>(kFanOutQuery), "Restart read required");

  // No read restart error when one is not expected.
  ASSERT_OK(read_conn.Execute("SET yb_read_after_commit_visibility = relaxed"));
  ASSERT_EQ(ASSERT_RESULT(read_conn.FetchRow<int64_t>(kFanOutQuery)), 10);
}

} // namespace yb::pgwrapper
