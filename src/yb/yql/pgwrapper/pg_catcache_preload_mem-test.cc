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

#include <string>

#include "yb/util/result.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_catalog_preload_additional_tables);
DECLARE_string(ysql_catalog_preload_additional_table_list);
DECLARE_bool(ysql_enable_auto_analyze);

namespace yb::pgwrapper {

class PgCatcachePreloadMemTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    // The tcmalloc heap profile is unavailable under ASAN/TSAN.
    YB_SKIP_TEST_IN_SANITIZERS();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_tables) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_table_list) = "pg_statistic";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override { return 1; }
};

// Catalog cache preload decodes every row of every preloaded catalog and copies it into the
// catcache. The decoded rows must be freed as preload goes rather than kept until the end of
// connection startup, where they add about as much to the backend's peak memory as the catcache
// entries themselves.
TEST_F(PgCatcachePreloadMemTest, YB_DISABLE_TEST_ON_MACOS(ScannedRowsFreedDuringPreload)) {
  constexpr int kNumTables = 4;
  constexpr int kNumColumns = 1000;
  constexpr int kNumRows = 50;

  std::string columns;
  std::string values;
  for (int i = 0; i < kNumColumns; ++i) {
    columns += Format("$0c$1 text", i ? ", " : "", i);
    values += Format("$0md5((i * $1)::text)", i ? ", " : "", i + 1);
  }

  // ANALYZE fills pg_statistic with one row per column, each with a histogram of kNumRows md5
  // strings.
  auto setup = ASSERT_RESULT(Connect());
  for (int t = 0; t < kNumTables; ++t) {
    ASSERT_OK(setup.ExecuteFormat("CREATE TABLE t$0 ($1)", t, columns));
    ASSERT_OK(setup.ExecuteFormat(
        "INSERT INTO t$0 SELECT $1 FROM generate_series(1, $2) i", t, values, kNumRows));
    ASSERT_OK(setup.ExecuteFormat("ANALYZE t$0", t));
  }

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
                "SELECT count(*) FROM pg_stats WHERE tablename = 't0'")),
            kNumColumns);
  const auto pid = ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  // Of what preload allocated, only the catcache insert paths (SetCatCacheTuple, SetCatCacheList)
  // should still be live at the startup peak; the rest is scan output.
  const auto [scanned_rows_bytes, catcache_entries_bytes, peak_bytes] =
      ASSERT_RESULT((conn.FetchRow<int64_t, int64_t, int64_t>(
          "SELECT "
          "coalesce(sum(estimated_bytes) FILTER ("
          "  WHERE call_stack LIKE '%YbPreloadCatalogCache%'"
          "  AND call_stack NOT LIKE '%SetCatCache%'), 0)::bigint, "
          "coalesce(sum(estimated_bytes) FILTER ("
          "  WHERE call_stack LIKE '%YbPreloadCatalogCache%'"
          "  AND call_stack LIKE '%CatalogCacheCreateEntry%'), 0)::bigint, "
          "sum(estimated_bytes)::bigint "
          "FROM yb_backend_heap_snapshot_peak()")));
  const auto current_bytes = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT sum(estimated_bytes)::bigint FROM yb_backend_heap_snapshot()"));
  const auto vm_hwm_kb = ASSERT_RESULT(ProcFileValue(Format("/proc/$0/status", pid), "VmHWM:"));
  const auto vm_rss_kb = ASSERT_RESULT(ProcFileValue(Format("/proc/$0/status", pid), "VmRSS:"));

  constexpr int64_t kMiB = 1024 * 1024;
  LOG(INFO) << "Fresh backend heap at peak: total " << peak_bytes / kMiB
            << " MiB, preload catcache entries " << catcache_entries_bytes / kMiB
            << " MiB, preload scanned rows " << scanned_rows_bytes / kMiB
            << " MiB; heap after startup " << current_bytes / kMiB
            << " MiB; VmHWM " << vm_hwm_kb / 1024 << " MiB, VmRSS " << vm_rss_kb / 1024 << " MiB";

  // Guards against the stack match silently failing (e.g. unsymbolized frames).
  ASSERT_GT(catcache_entries_bytes, 10 * kMiB);
  ASSERT_LT(scanned_rows_bytes, 4 * kMiB);
}

}  // namespace yb::pgwrapper
