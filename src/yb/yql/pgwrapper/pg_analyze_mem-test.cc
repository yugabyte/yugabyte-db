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

#include "yb/util/result.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_enable_auto_analyze);

namespace yb::pgwrapper {

class PgAnalyzeMemTest : public PgMiniTestBase {
 protected:
  // kRows is ANALYZE's default targrows, so the whole table is sampled.
  static constexpr int kRows = 30000;
  static constexpr int kValueBytes = 4000;

  void SetUp() override {
    // RSS thresholds are meaningless under ASAN/TSAN.
    YB_SKIP_TEST_IN_SANITIZERS();
    // Auto-analyze opens its own connections and adds noise.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override { return 1; }

  Result<std::pair<int64_t, int64_t>> AnalyzeRssGrowthMbAndStats() {
    auto setup = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(setup.Execute("CREATE TABLE wide (k int, v text)"));
    constexpr int kBatch = 2000;
    for (int start = 1; start <= kRows; start += kBatch) {
      RETURN_NOT_OK(setup.ExecuteFormat(
          "INSERT INTO wide SELECT g, repeat('x', $0) FROM generate_series($1, $2) g",
          kValueBytes, start, std::min(start + kBatch - 1, kRows)));
    }

    auto conn = VERIFY_RESULT(Connect());
    const auto pid = VERIFY_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
    const auto before_mb = VERIFY_RESULT(PeakRssMb(pid));
    RETURN_NOT_OK(conn.Execute("ANALYZE wide"));
    const auto after_mb = VERIFY_RESULT(PeakRssMb(pid));

    auto logged = conn.FetchRow<bool>(
        "SELECT pg_log_backend_memory_contexts(pg_backend_pid())");
    if (!logged.ok()) {
      LOG(WARNING) << "Failed to log backend memory contexts: " << logged.status();
    }

    const auto stat_rows = VERIFY_RESULT(conn.FetchRow<int64_t>(
        "SELECT count(*) FROM pg_stats WHERE tablename = 'wide'"));
    return std::make_pair(after_mb - before_mb, stat_rows);
  }
};

// the memory growth should be < 175 MB on 114 MB sample
constexpr int64_t kMaxAnalyzeGrowthMb = 175;

TEST_F(PgAnalyzeMemTest, YB_DISABLE_TEST_ON_MACOS(AnalyzeWideTableStaysBounded)) {
  const auto [growth_mb, stat_rows] = ASSERT_RESULT(AnalyzeRssGrowthMbAndStats());
  LOG(INFO) << "ANALYZE of " << kRows << " x " << kValueBytes << " byte rows grew peak RSS by "
            << growth_mb << " MB";

  ASSERT_GT(stat_rows, 0)
      << "ANALYZE produced no statistics -- the workload did not run, so the "
         "bound below would pass without testing anything";
  ASSERT_LT(growth_mb, kMaxAnalyzeGrowthMb)
      << "ANALYZE spiked -- fetched sample values are not released per row";
}

}  // namespace yb::pgwrapper
