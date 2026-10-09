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

  // Peak RSS growth, in MB, of "ANALYZE t" in a new connection.
  Result<int64_t> AnalyzeGrowthMb(bool width_skip) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.ExecuteFormat(
        "SET yb_enable_analyze_width_skip = $0", width_skip ? "on" : "off"));
    // A fetch batch holds its rows in full, so keep it small next to the sample.
    RETURN_NOT_OK(conn.Execute("SET yb_fetch_row_limit = 10"));
    const auto pid = VERIFY_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
    const auto before_mb = VERIFY_RESULT(PeakRssMb(pid));
    RETURN_NOT_OK(conn.Execute("ANALYZE t"));
    return VERIFY_RESULT(PeakRssMb(pid)) - before_mb;
  }
};

// On the 114 MB sample above, measured growth is ~24 MB with width skipping and
// ~136 MB without, so this bound also fails if ANALYZE stops skipping wide values.
constexpr int64_t kMaxAnalyzeGrowthMb = 75;

TEST_F(PgAnalyzeMemTest, YB_DISABLE_TEST_ON_MACOS(AnalyzeWideTableStaysBounded)) {
  const auto [growth_mb, stat_rows] = ASSERT_RESULT(AnalyzeRssGrowthMbAndStats());
  LOG(INFO) << "ANALYZE of " << kRows << " x " << kValueBytes << " byte rows grew peak RSS by "
            << growth_mb << " MB";

  ASSERT_GT(stat_rows, 0)
      << "ANALYZE produced no statistics -- the workload did not run, so the "
         "bound below would pass without testing anything";
  ASSERT_LT(growth_mb, kMaxAnalyzeGrowthMb)
      << "ANALYZE spiked -- fetched sample values are not released per row, or "
         "wide values are no longer skipped";
}

// The "skip applies" rows of the #31506 behavior table: ANALYZE keeps only the
// size of each wide value, so skipping must save most of the values' memory.
TEST_F(PgAnalyzeMemTest, YB_DISABLE_TEST_ON_MACOS(WidthSkipSavesMemory)) {
  struct Case {
    const char* name;
    const char* type;   // of the wide column v
    const char* value;  // of row g
    int rows;           // ~40 MB of values
    const char* ddl;    // an index or statistics object that must not stop the skip
  };
  const Case kCases[] = {
      {"text, has = and <", "text", "repeat('x', 4000) || g", 10000, nullptr},
      {"xid[], has = but no <", "xid[]", "array_fill(g::text::xid, ARRAY[1000])", 10000,
       nullptr},
      {"json, has no = or <", "json", "to_json(repeat('x', 4000) || g)", 10000, nullptr},
      {"text[] over 64 KB", "text[]", "ARRAY[repeat('x', 70000) || g]", 600, nullptr},
      {"text in mcv and dependencies statistics", "text", "repeat('x', 4000) || g", 10000,
       "CREATE STATISTICS t_s (mcv, dependencies) ON k, v FROM t"},
      {"text with a plain index", "text", "repeat('x', 4000) || g", 10000,
       "CREATE INDEX ON t (v)"},
  };

  auto conn = ASSERT_RESULT(Connect());
  for (const auto& c : kCases) {
    SCOPED_TRACE(c.name);
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t (k int PRIMARY KEY, v $0)", c.type));
    if (c.ddl) {
      ASSERT_OK(conn.Execute(c.ddl));
    }
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT g, $0 FROM generate_series(1, $1) g", c.value, c.rows));
    const auto data_mb = ASSERT_RESULT(conn.FetchRow<int64_t>(
        "SELECT sum(pg_column_size(v)) / (1024 * 1024) FROM t"));
    const auto off_mb = ASSERT_RESULT(AnalyzeGrowthMb(false));
    const auto on_mb = ASSERT_RESULT(AnalyzeGrowthMb(true));
    LOG(INFO) << c.name << ": ANALYZE of " << data_mb << " MB of values grew peak RSS by "
              << off_mb << " MB with width skipping off, " << on_mb << " MB with it on";
    // Skipping must save more than half the values' size. On ~38 MB of values,
    // measured growth is ~50 MB with width skipping off and ~12 MB on: ~38 MB
    // saved against a ~19 MB bar.
    EXPECT_GT(off_mb - on_mb, data_mb / 2) << "ANALYZE kept wide values it should skip";
    ASSERT_OK(conn.Execute("DROP TABLE t"));
  }
}

}  // namespace yb::pgwrapper
