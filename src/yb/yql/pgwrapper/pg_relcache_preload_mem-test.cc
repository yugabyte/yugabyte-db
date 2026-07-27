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
#include <chrono>
#include <thread>

#include "yb/util/result.h"
#include "yb/util/test_macros.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(ysql_minimal_catalog_caches_preload);
DECLARE_bool(ysql_use_relcache_file);
DECLARE_bool(ysql_enable_auto_analyze);

namespace yb::pgwrapper {

// Checks that there is not a huge PG-backend memory spike during preloading.
class PgRelcachePreloadMemTest : public PgMiniTestBase {
 protected:
  virtual bool UseMinimalCatalogCachesPreload() const = 0;

  void SetUp() override {
    // RSS thresholds are meaningless under ASAN/TSAN (shadow memory + redzones
    // inflate every backend's RSS), so skip there before bringing up the cluster.
    YB_SKIP_TEST_IN_SANITIZERS();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_minimal_catalog_caches_preload) =
        UseMinimalCatalogCachesPreload();
    // Force every connection to rebuild the relcache from the catalog (no init
    // file), so the catcache-list code path runs on connection startup the way
    // the relcache-init builder connection does on the affected clusters.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_relcache_file) = false;
    // Auto-analyze opens its own internal connections and adds noise.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override { return 1; }

  // Open a brand-new backend against a freshly created EMPTY database (no user
  // functions / views / tables), run a probe that forces a function-by-name
  // lookup (SearchCatCacheList over PROCNAMEARGSNSP), and return that backend's
  // peak RSS in MB. ysql_use_relcache_file=false forces a full relcache build
  // on connect, exercising the YbPreloadCatalogCache list path the way the
  // relcache-init builder connection does on a fresh database in production.
  Result<int64_t> PeakRssOfFreshEmptyDbBackend() {
    auto setup = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(setup.Execute("CREATE DATABASE empty_db"));
    auto conn = VERIFY_RESULT(ConnectToDB("empty_db"));
    RETURN_NOT_OK(conn.FetchRow<int32_t>("SELECT length('x')"));
    auto pid = VERIFY_RESULT(conn.FetchRow<int32_t>("SELECT pg_backend_pid()"));
    return PeakRssMb(pid);
  }
};

class PgRelcachePreloadMemMinimalTest : public PgRelcachePreloadMemTest {
 protected:
  bool UseMinimalCatalogCachesPreload() const override { return true; }
};

class PgRelcachePreloadMemNormalTest : public PgRelcachePreloadMemTest {
 protected:
  bool UseMinimalCatalogCachesPreload() const override { return false; }
};

constexpr int64_t kSingleConnMaxRssMb = 100;
constexpr int64_t kAllConnsMaxRssMb = 250;

// Regression guard: a minimal-preload backend must preload the pg_rewrite
// (RULERELNAME) list so it does not re-materialize the system-view rule trees
// on demand. If that preload is dropped, this backend spikes (~560 MB) and fails.
TEST_F(PgRelcachePreloadMemMinimalTest, YB_DISABLE_TEST_ON_MACOS(FreshEmptyDbStaysBounded)) {
  const auto peak_mb = ASSERT_RESULT(PeakRssOfFreshEmptyDbBackend());
  LOG(INFO) << "minimal preload, fresh empty db, peak RSS = " << peak_mb << " MB";
  ASSERT_LT(peak_mb, kSingleConnMaxRssMb)
      << "minimal-preload backend spiked -- catcache-list skip is too broad";
}

// Baseline: normal preload builds the lists up front and stays bounded.
TEST_F(PgRelcachePreloadMemNormalTest, YB_DISABLE_TEST_ON_MACOS(FreshEmptyDbStaysBounded)) {
  const auto peak_mb = ASSERT_RESULT(PeakRssOfFreshEmptyDbBackend());
  LOG(INFO) << "normal preload, fresh empty db, peak RSS = " << peak_mb << " MB";
  ASSERT_LT(peak_mb, kSingleConnMaxRssMb);
}

// Ensure that the total memory consumed by all PG processes does not spike too high
// during preloading.
TEST_F(PgRelcachePreloadMemMinimalTest, YB_DISABLE_TEST_ON_MACOS(TotalPgMemoryStaysBounded)) {
  auto setup = ASSERT_RESULT(Connect());
  ASSERT_OK(setup.Execute("CREATE DATABASE empty_db"));
  const auto setup_pid = ASSERT_RESULT(setup.FetchRow<int32_t>("SELECT pg_backend_pid()"));
  const auto postmaster_pid =
      static_cast<int>(ASSERT_RESULT(ProcFileValue(Format("/proc/$0/status", setup_pid), "PPid:")));

  const auto baseline_mb = ASSERT_RESULT(ClusterPgPssMb(postmaster_pid));

  // Sample the cluster's total PSS while the fresh connection spikes.
  std::atomic<int64_t> peak_total_mb{baseline_mb};
  std::atomic<bool> stop{false};
  std::thread sampler([&] {
    while (!stop.load(std::memory_order_relaxed)) {
      auto total = ClusterPgPssMb(postmaster_pid);
      if (total.ok()) {
        int64_t cur = *total;
        int64_t prev = peak_total_mb.load(std::memory_order_relaxed);
        while (cur > prev &&
               !peak_total_mb.compare_exchange_weak(prev, cur, std::memory_order_relaxed)) {}
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });

  auto conn = ASSERT_RESULT(ConnectToDB("empty_db"));  // relcache-build spike here
  ASSERT_RESULT(conn.FetchRow<int32_t>("SELECT length('x')"));
  stop.store(true, std::memory_order_relaxed);
  sampler.join();

  const auto peak = peak_total_mb.load();
  const auto delta = peak - baseline_mb;
  LOG(INFO) << "total PG memory (PSS, all processes; postmaster=" << postmaster_pid
            << "): baseline=" << baseline_mb << " MB, peak=" << peak
            << " MB; one fresh connection added " << delta << " MB";
  ASSERT_LT(delta, kAllConnsMaxRssMb)
      << "a single fresh connection added too much to total PG memory: added " << delta
      << " MB; limit is " << kAllConnsMaxRssMb << " MB";
}

}  // namespace yb::pgwrapper
