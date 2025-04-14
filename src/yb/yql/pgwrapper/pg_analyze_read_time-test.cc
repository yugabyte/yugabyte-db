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

#include <atomic>
#include <thread>

#include "yb/common/pgsql_error.h"
#include "yb/util/flags.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

DECLARE_string(ysql_pg_conf_csv);
DECLARE_string(ysql_log_statement);
DECLARE_bool(ysql_beta_features);
DECLARE_string(vmodule);
DECLARE_int32(timestamp_history_retention_interval_sec);
DECLARE_int64(TEST_delay_after_table_analyze_ms);

namespace yb::pgwrapper {

class PgAnalyzeReadTimeTest : public PgMiniTestBase {
 public:
  void SetUp() override {
    // ANALYZE is a beta feature.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_beta_features) = true;
    // Easier debugging.
    // google::SetVLOGLevel("read_query", 1);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_log_statement) = "all";
    PgMiniTestBase::SetUp();
  }
};

class PgAnalyzeNoReadRestartsTest : public PgAnalyzeReadTimeTest {
 public:
  void SetUp() override {
    // So that read restart errors are not retried internally.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        MaxQueryLayerRetriesConf(0);
    PgAnalyzeReadTimeTest::SetUp();
  }
};

TEST_F_EX(PgAnalyzeReadTimeTest, InsertRowsConcurrentlyWithAnalyze, PgAnalyzeNoReadRestartsTest) {
  constexpr auto kNumInitialRows = RegularBuildVsSanitizers(100000, 10000);

  // Create table with keys from 1 to kNumInitialRows.
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE keys (k INT) SPLIT INTO 3 TABLETS"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO keys(k) SELECT GENERATE_SERIES(1, $0)", kNumInitialRows));

  // Warm the catalog cache so that subsequent inserts are fast.
  // Unfortunately, this is necessary because this test depends on timing.
  auto insert_conn = ASSERT_RESULT(Connect());
  auto key = kNumInitialRows;
  // Populates catalog cache.
  key++;
  ASSERT_OK(insert_conn.ExecuteFormat(
      "INSERT INTO keys(k) VALUES ($0)", key));

  std::atomic<bool> stop{false};
  CountDownLatch begin_analyze(1);
  auto analyze_conn = ASSERT_RESULT(Connect());
  auto analyze_status_future = std::async(std::launch::async, [&] {
    begin_analyze.Wait();
    auto status = analyze_conn.Execute("ANALYZE keys");
    stop.store(true);
    return status;
  });

  begin_analyze.CountDown();
  while (!stop.load() && key < kNumInitialRows + 100) {
    key++;
    ASSERT_OK(insert_conn.ExecuteFormat(
        "INSERT INTO keys(k) VALUES ($0)", key));

    // Throttle inserts to avoid overloading the system.
    std::this_thread::sleep_for(10ms);
  }

  ASSERT_OK(analyze_status_future.get());
}

class PgAnalyzeMultiTableTest : public PgAnalyzeReadTimeTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_timestamp_history_retention_interval_sec) = 0;
    // This test is timing based and 10s provides enough time for compaction.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_after_table_analyze_ms) = 10000;
    PgAnalyzeReadTimeTest::SetUp();
  }
};

TEST_F_EX(PgAnalyzeReadTimeTest, AnalyzeMultipleTables, PgAnalyzeMultiTableTest) {
  constexpr auto kNumInitialRows = RegularBuildVsSanitizers(10000, 1000);

  // Create table with keys from 1 to kNumInitialRows.
  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute("CREATE TABLE keys (k INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO keys(k) SELECT GENERATE_SERIES(1, $0)", kNumInitialRows));
  ASSERT_OK(setup_conn.Execute("CREATE TABLE values (v INT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO values(v) SELECT GENERATE_SERIES(1, $0)", kNumInitialRows));

  auto update_conn = ASSERT_RESULT(Connect());
  auto analyze_conn = ASSERT_RESULT(Connect());

  CountDownLatch update_thread_started(1);
  auto update_status_future = std::async(std::launch::async, [&] {
    update_thread_started.CountDown();
    auto status = update_conn.Execute("UPDATE values SET v = v + 1");
    FlushAndCompactTablets();
    LOG(INFO) << "Compaction done!";
    return status;
  });

  update_thread_started.Wait();
  auto analyze_status = analyze_conn.Execute("ANALYZE keys, values");
  ASSERT_OK(analyze_status);
  LOG(INFO) << "Analyze done!";

  ASSERT_OK(update_status_future.get());
}

} // namespace yb::pgwrapper
