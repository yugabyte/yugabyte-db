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

#include "yb/util/logging_test_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_int32(TEST_fetch_next_delay_ms);
DECLARE_string(TEST_fetch_next_delay_column);

namespace yb::pgwrapper {

class PgTabletShutdownTest : public PgMiniTestBase {
 protected:
  // Make sure long reads are aborted by operation.
  void TestLongReadAbort(std::function<Status()> operation) {
    constexpr auto kNumRows = 1000;

    auto conn = ASSERT_RESULT(Connect());

    ASSERT_OK(conn.Execute("CREATE TABLE t(test_key INT, v INT) SPLIT INTO 1 TABLETS;"));

    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT i, 1 FROM (SELECT generate_series(1, $0) i) t2;", kNumRows));

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_ms) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_fetch_next_delay_column) = "test_key";

    std::thread counter([&] {
      LOG(INFO) << "Starting scan...";

      const auto start_time = CoarseMonoClock::now();

      const auto rows_count_result = conn.FetchValue<PGUint64>("SELECT COUNT(*) FROM t");

      // Request should be aborted during tablet shutdown.
      ASSERT_FALSE(rows_count_result.ok());
      LOG(INFO) << "Scan result: " << rows_count_result.ToString();

      const auto time_elapsed = CoarseMonoClock::now() - start_time;
      // Abort should be fast.
      ASSERT_LE(time_elapsed, 5s * kTimeMultiplier);
    });

    // Wait for test tablet scan start.
    RegexWaiterLogSink log_waiter(R"#(.*Delaying read for.*test_key.*)#");
    ASSERT_OK(log_waiter.WaitFor(30s));

    ASSERT_OK(operation());

    counter.join();
  }
};

TEST_F(PgTabletShutdownTest, DeleteTableDuringLongRead) {
  TestLongReadAbort([&]{
    LOG(INFO) << "Dropping table...";
    auto conn = VERIFY_RESULT(Connect());
    return conn.Execute("DROP TABLE t");
  });
}

TEST_F(PgTabletShutdownTest, TruncateTableDuringLongRead) {
  TestLongReadAbort([&]{
    LOG(INFO) << "Truncating table...";
    auto conn = VERIFY_RESULT(Connect());
    return conn.Execute("TRUNCATE TABLE t");
  });
}

} // namespace yb::pgwrapper
