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

#include "yb/gutil/casts.h"

#include "yb/tserver/tserver.messages.h"

#include "yb/util/debug.h"
#include "yb/util/flags.h"
#include "yb/util/sync_point.h"

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

// Tests for https://github.com/yugabyte/yugabyte-db/issues/27437: block-based sampling ANALYZE
// used to pin a single read time for the whole operation, so once a compaction advanced the
// history cutoff past it, the remaining sampling requests failed with "Snapshot too old" error.
class PgAnalyzeBlockSamplingRetentionTest : public PgAnalyzeReadTimeTest {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_timestamp_history_retention_interval_sec) = 0;
    PgAnalyzeReadTimeTest::SetUp();
  }

 protected:
  static constexpr auto kNumRows = 10000;
  static constexpr size_t kNumTablets = 3;

  // Runs ANALYZE with block-based sampling on a kNumTablets-tablet table and injects a rows
  // update followed by a full compaction at the first block-sampling stage response for which
  // is_trigger_response returns true. With zero history retention interval the compaction
  // invalidates all read points older than the compaction time. ANALYZE is expected to succeed.
  void TestCompactionDuringAnalyze(
      std::function<bool(const LWPgsqlResponsePB&)> is_trigger_response) {
    if (!kIsDebug) {
      GTEST_SKIP() << "ReadRpc sync points are compiled out in release builds";
    }

    auto setup_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(setup_conn.ExecuteFormat(
        "CREATE TABLE keys (k INT PRIMARY KEY, v INT) SPLIT INTO $0 TABLETS", kNumTablets));
    ASSERT_OK(setup_conn.ExecuteFormat(
        "INSERT INTO keys SELECT i, i FROM GENERATE_SERIES(1, $0) i", kNumRows));
    // Block-based sampling picks blocks from SST files, so make sure data is flushed.
    ASSERT_OK(cluster_->FlushTablets());

    auto compact_conn = ASSERT_RESULT(Connect());
    std::atomic<bool> injected{false};
    auto& sync_point = *SyncPoint::GetInstance();
    sync_point.SetCallBack(
        "ReadRpc::NotifyBatcher",
        [this, &compact_conn, &injected, &is_trigger_response](void* arg) {
          const auto* read_resp = CHECK_NOTNULL(pointer_cast<tserver::ReadResponseMsg*>(arg));
          if (read_resp->pgsql_batch().empty() ||
              !read_resp->pgsql_batch().front().has_sampling_state() ||
              !read_resp->pgsql_batch().front().sampling_state().is_blocks_sampling_stage() ||
              !is_trigger_response(read_resp->pgsql_batch().front()) ||
              injected.exchange(true)) {
            return;
          }
          LOG(INFO) << "Reached the trigger block sampling response, compacting all tablets";
          // Response processing is blocked until we return, so subsequent sampling requests are
          // neither sent nor have their read times set until the compaction is complete. Rewrite
          // some rows and compact away the history older read points refer to.
          ASSERT_OK(compact_conn.Execute("UPDATE keys SET v = v + 1 WHERE k <= 1000"));
          FlushAndCompactTablets();
          LOG(INFO) << "Compaction done";
        });
    sync_point.EnableProcessing();

    auto analyze_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(analyze_conn.Execute("SET yb_allow_separate_requests_for_sampling_stages = true"));
    ASSERT_OK(analyze_conn.Execute("SET yb_sampling_algorithm = block_based_sampling"));
    ASSERT_OK(analyze_conn.Execute("ANALYZE keys"));

    sync_point.DisableProcessing();
    sync_point.ClearAllCallBacks();

    // Make sure the test exercised the intended scenario and the compaction was injected.
    ASSERT_TRUE(injected.load());
  }
};

TEST_F_EX(
    PgAnalyzeReadTimeTest, SnapshotTooOldDuringBlockSamplingStage,
    PgAnalyzeBlockSamplingRetentionTest) {
  // Compact right after the first block-sampling response. Unless block-sampling requests to
  // the remaining tablets use a fresh read time, they fail with "Snapshot too old" error.
  TestCompactionDuringAnalyze([](const LWPgsqlResponsePB&) { return true; });
}

TEST_F_EX(
    PgAnalyzeReadTimeTest, SnapshotTooOldDuringRowsSamplingStage,
    PgAnalyzeBlockSamplingRetentionTest) {
  // Compact right after the block-sampling stage (1st stage) completion, detected as the last
  // per-tablet final block-sampling response (responses with paging state are not final for the
  // tablet). Unless the rows-sampling stage (2nd stage) and sampled rows fetch use a read time
  // anchored after the 1st stage completion, they fail with "Snapshot too old" error.
  TestCompactionDuringAnalyze(
      [tablets_completed = size_t{0}](const LWPgsqlResponsePB& resp) mutable {
        return !resp.has_paging_state() && ++tablets_completed >= kNumTablets;
      });
}

} // namespace yb::pgwrapper
