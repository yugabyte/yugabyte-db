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

#include <fstream>
#include <thread>

#ifdef __APPLE__
#include <errno.h>
#include <libproc.h>
#include <string.h>
#endif

#include <gtest/gtest.h>

#include "yb/integration-tests/mini_cluster.h"

#include "yb/server/skewed_clock.h"

#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_string(ysql_pg_conf_csv);
DECLARE_uint64(max_clock_skew_usec);
DECLARE_string(time_source);

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

Result<int64_t> GetProcessRssKb(int pid) {
#ifdef __APPLE__
  struct proc_taskinfo pti;
  int ret = proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &pti, sizeof(pti));
  if (ret != static_cast<int>(sizeof(pti))) {
    return STATUS_FORMAT(IOError, "proc_pidinfo failed for pid $0: $1", pid, strerror(errno));
  }
  return static_cast<int64_t>(pti.pti_resident_size / 1024);
#else
  std::ifstream status("/proc/" + std::to_string(pid) + "/status");
  if (!status) {
    return STATUS_FORMAT(IOError, "Cannot open /proc/$0/status", pid);
  }
  std::string line;
  while (std::getline(status, line)) {
    if (line.compare(0, 6, "VmRSS:") == 0) {
      int64_t kb = 0;
      if (sscanf(line.c_str(), "VmRSS: %ld kB", &kb) == 1) {
        return kb;
      }
    }
  }
  return STATUS(NotFound, "VmRSS not found in /proc/status");
#endif
}

} // namespace

// Regression test for memory leak during transparent read-restart.
//
// When a snapshot-isolation transaction hits a read-restart conflict,
// YugabyteDB transparently retries by clearing and re-executing the portal.
// Before the fix (MemoryContextDeleteChildren in yb_clear_portal_before_restart),
// each retry orphaned the old ExecutorState and its YB-side objects under
// portal->portalContext.
//
// IMPORTANT: The leak only manifests with the extended query protocol
// (PQexecParams / JDBC), NOT the simple query protocol (PQexec).
// With the simple query protocol, each retry re-enters exec_simple_query
// which creates a brand new portal (dropping the old one), so children
// never accumulate.  With the extended protocol, exec_execute_message
// reuses the existing (restarted) portal, so old ExecutorState contexts
// pile up under portalContext.
//
// The test query is a full-table UPDATE (no RETURNING).  The UPDATE touches
// every row, reading and writing intents for all kNumRows rows before
// producing any output.  This accumulates write-side EState -- write intent
// buffers, PgDml/PgDocOp write state -- making each leaked executor context
// large (~6 MB empirically) and the RSS signal clear even with a moderate
// number of restarts.  Because the UPDATE produces no data output (only a
// command tag), YBIsDataSent() stays false throughout the entire execution,
// so every read-restart conflict can be transparently retried.
//
// yb_debug_log_internal_restarts=true causes postgres to log each
// "performing query layer retry, attempt number N" line; check the test log
// to confirm restarts are actually happening.
class PgReadRestartMemoryTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    server::SkewedClock::Register();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_time_source) = server::SkewedClock::kName;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_clock_skew_usec) = 250000ULL;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_pg_conf_csv) =
        "yb_max_query_layer_retries=60,"
        "yb_debug_log_internal_restarts=true";
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override {
    return 3;
  }
};

TEST_F(PgReadRestartMemoryTest, YB_DISABLE_TEST_IN_SANITIZERS(NoLeakedChildContexts)) {
  constexpr int kNumRows = 10000;
  static constexpr int kRowSize = 1000;
  constexpr auto kClockSkew = -100ms;
  // Threshold is set after measuring empirically with and without the fix.
  // Update this constant if hardware or query parameters change significantly.
  constexpr int64_t kMaxRssGrowthKb = 50 * 1024;

  auto setup_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(setup_conn.Execute(
      "CREATE TABLE restart_mem_test (k INT PRIMARY KEY, v TEXT)"));
  ASSERT_OK(setup_conn.ExecuteFormat(
      "INSERT INTO restart_mem_test "
      "SELECT i, repeat('x', $0) FROM generate_series(1, $1) i",
      kRowSize, kNumRows));

  auto delta_changers = SkewClocks(cluster_.get(), kClockSkew);

  auto update_conn = ASSERT_RESULT(Connect());

  auto backend_pid = ASSERT_RESULT(
      update_conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  auto initial_rss_kb = ASSERT_RESULT(GetProcessRssKb(backend_pid));
  LOG(INFO) << "Backend PID: " << backend_pid
            << ", initial RSS: " << initial_rss_kb << " KB";

  // Start concurrent writers to trigger read-restart conflicts.
  TestThreadHolder writer_threads;
  for (int i = 0; i < 4; ++i) {
    writer_threads.AddThreadFunctor(
        [this, i, &stop = writer_threads.stop_flag()] {
      auto conn = ASSERT_RESULT(Connect());
      int counter = 0;
      while (!stop.load(std::memory_order_acquire)) {
        auto key = (counter * 4 + i) % kNumRows + 1;
        auto s = conn.ExecuteFormat(
            "UPDATE restart_mem_test SET v = repeat('y', $0) WHERE k = $1",
            kRowSize, key);
        if (!s.ok()) {
          conn = ASSERT_RESULT(Connect());
        }
        ++counter;
        std::this_thread::sleep_for(2ms);
      }
    });
  }

  // Fetch() uses the extended query protocol (PQexecParams), which reuses the
  // same portal across transparent retries.  Without the fix, each retry
  // orphans the old ExecutorState under portalContext.  See the class comment
  // above for why this query form is chosen.
  std::atomic<bool> query_done{false};
  Status query_status;
  std::jthread query_thread([&] {
    auto result = update_conn.Fetch(
        "UPDATE restart_mem_test SET v = v || '.'");
    if (!result.ok()) {
      query_status = result.status();
    }
    query_done.store(true, std::memory_order_release);
  });

  int64_t peak_rss_kb = initial_rss_kb;
  while (!query_done.load(std::memory_order_acquire)) {
    auto rss = GetProcessRssKb(backend_pid);
    if (rss.ok() && *rss > peak_rss_kb) {
      peak_rss_kb = *rss;
    }
    std::this_thread::sleep_for(50ms);
  }
  {
    auto rss = GetProcessRssKb(backend_pid);
    if (rss.ok() && *rss > peak_rss_kb) {
      peak_rss_kb = *rss;
    }
  }

  writer_threads.Stop();

  if (!query_status.ok()) {
    LOG(WARNING) << "Query did not complete (retry limit likely exhausted): " << query_status;
  }

  int64_t rss_growth_kb = peak_rss_kb - initial_rss_kb;
  LOG(INFO) << "Peak RSS: " << peak_rss_kb << " KB"
            << ", growth: " << rss_growth_kb << " KB"
            << " (" << rss_growth_kb / 1024 << " MB)";

  if (rss_growth_kb < 1024) {
    LOG(WARNING) << "RSS grew by less than 1 MB -- transparent restarts may not have "
                 << "occurred. Check the log above for "
                 << "'performing query layer retry' lines from "
                 << "yb_debug_log_internal_restarts. The test may be vacuously passing.";
  }

  ASSERT_LT(rss_growth_kb, kMaxRssGrowthKb)
      << "Postgres backend RSS grew by " << rss_growth_kb / 1024
      << " MB during full-table UPDATE -- likely an ExecutorState leak "
         "during transparent read-restart.";
}

} // namespace yb::pgwrapper
