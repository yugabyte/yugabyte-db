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

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "yb/gutil/thread_annotations.h"

#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::literals;

namespace yb::pgwrapper {

namespace {

constexpr auto kWaitTimeout = 30s * kTimeMultiplier;

// Builds and frees a ~100 MB string, so it raises the backend's peak RSS without holding on to it.
constexpr auto kSpikeQuery =
    "SELECT length(string_agg(repeat('x', 1000), '')) FROM generate_series(1, 100000)";

// Records the daemon's output lines. An ExternalDaemon holds a single log listener, so one of these
// replaces separate LogWaiters for the lines a check needs.
class LogLines : public ExternalDaemon::StringListener {
 public:
  explicit LogLines(ExternalDaemon* daemon) : daemon_(daemon) { daemon_->SetLogListener(this); }
  ~LogLines() { daemon_->RemoveLogListener(this); }

  void Handle(const GStringPiece& s) override {
    std::lock_guard lock(mutex_);
    lines_.push_back(s.as_string());
  }

  Status WaitFor(const std::vector<std::string>& texts) {
    return yb::WaitFor(
        [this, &texts] {
          std::lock_guard lock(mutex_);
          return std::ranges::any_of(lines_, [&texts](const auto& line) {
            return std::ranges::all_of(
                texts, [&line](const auto& text) { return line.find(text) != std::string::npos; });
          });
        },
        kWaitTimeout, Format("a log line with all of $0", texts));
  }

 private:
  ExternalDaemon* const daemon_;
  std::mutex mutex_;
  std::vector<std::string> lines_ GUARDED_BY(mutex_);
};

}  // namespace

class PgMemoryWatcherTest : public LibPqTestBase {
 protected:
  int GetNumTabletServers() const override { return 1; }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->replication_factor = 1;
    options->extra_tserver_flags.push_back("--ysql_yb_enable_memory_watcher=true");
    // Available memory is always below 100% of the total, so every check finds memory low.
    options->extra_tserver_flags.push_back("--ysql_yb_memory_watcher_threshold_pct=100");
    options->extra_tserver_flags.push_back(
        "--ysql_pg_conf_csv=yb_memory_watcher_report_interval=1");
  }
};

TEST_F(PgMemoryWatcherTest, ReportsBackendWithMemorySpike) {
  LogLines log(cluster_->tablet_server(0));

  auto conn = ASSERT_RESULT(Connect());
  const auto pid = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  ASSERT_RESULT(conn.FetchRow<int32_t>(kSpikeQuery));

  // The report lists the backend with its last query.
  ASSERT_OK(log.WaitFor({Format(" $0 ", pid), "generate_series(1, 100000)"}));
  // The spike makes it the backend whose peak grew the most, and it logs its peak heap snapshot.
  ASSERT_OK(log.WaitFor({Format("asking PID $0, whose peak RSS grew the most", pid)}));
  ASSERT_OK(log.WaitFor({Format("logging heap snapshot of PID $0", pid)}));
}

}  // namespace yb::pgwrapper
