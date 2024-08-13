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

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/flags.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);
METRIC_DECLARE_counter(pg_response_cache_disable_calls);
METRIC_DECLARE_counter(pg_response_cache_entries_removed_by_gc);
METRIC_DECLARE_counter(pg_response_cache_hits);
METRIC_DECLARE_counter(pg_response_cache_gc_calls);
METRIC_DECLARE_counter(pg_response_cache_queries);
METRIC_DECLARE_counter(pg_response_cache_renew_hard);
METRIC_DECLARE_counter(pg_response_cache_renew_soft);

DECLARE_bool(ysql_enable_read_request_caching);
DECLARE_bool(ysql_minimal_catalog_caches_preload);
DECLARE_bool(ysql_catalog_preload_additional_tables);
DECLARE_bool(ysql_use_relcache_file);
DECLARE_string(ysql_catalog_preload_additional_table_list);
DECLARE_uint64(TEST_pg_response_cache_catalog_read_time_usec);
DECLARE_uint64(TEST_committed_history_cutoff_initial_value_usec);
DECLARE_uint32(pg_cache_response_renew_soft_lifetime_limit_ms);
DECLARE_uint64(pg_response_cache_size_bytes);
DECLARE_uint32(pg_response_cache_size_percentage);

using namespace std::literals;

namespace yb::pgwrapper {
namespace {

Status EnableCatCacheEventLogging(PGConn* conn) {
  return conn->Execute("SET yb_debug_log_catcache_events = ON");
}

struct Configuration {
  bool enable_read_request_caching() const { return response_cache_size_bytes.has_value(); }

  const bool minimal_catalog_caches_preload = false;
  const std::optional<uint64_t> response_cache_size_bytes = std::nullopt;
  const std::string_view preload_additional_catalog_list = {};
  const bool preload_additional_catalog_tables = false;
  const bool use_relcache_file = true;
};

struct MetricCounters {
  struct ResponseCache {
    size_t queries;
    size_t hits;
    size_t renew_soft;
    size_t renew_hard;
    size_t gc_calls;
    size_t entries_removed_by_gc;
    size_t disable_calls;
  };

  ResponseCache cache;
  size_t master_read_rpc;
};

struct MetricCountersDescriber : public MetricWatcherDeltaDescriberTraits<MetricCounters, 8> {
  MetricCountersDescriber(
      std::reference_wrapper<const MetricEntity> master_metric,
      std::reference_wrapper<const MetricEntity> tserver_metric)
      : descriptors{
          Descriptor{
              &delta.master_read_rpc, master_metric,
              METRIC_handler_latency_yb_tserver_TabletServerService_Read},
          Descriptor{
              &delta.cache.queries, tserver_metric, METRIC_pg_response_cache_queries},
          Descriptor{
              &delta.cache.hits, tserver_metric, METRIC_pg_response_cache_hits},
          Descriptor{
              &delta.cache.renew_soft, tserver_metric, METRIC_pg_response_cache_renew_soft},
          Descriptor{
              &delta.cache.renew_hard, tserver_metric, METRIC_pg_response_cache_renew_hard},
          Descriptor{
              &delta.cache.gc_calls, tserver_metric, METRIC_pg_response_cache_gc_calls},
          Descriptor{
              &delta.cache.entries_removed_by_gc,
              tserver_metric, METRIC_pg_response_cache_entries_removed_by_gc},
          Descriptor{
              &delta.cache.disable_calls,
              tserver_metric, METRIC_pg_response_cache_disable_calls}}
  {}

  DeltaType delta;
  Descriptors descriptors;
};

class PgCatalogPerfTestBase : public PgMiniTestBase {
 public:
  virtual ~PgCatalogPerfTestBase() = default;

 protected:
  void SetUp() override {
    auto config = GetConfig();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_read_request_caching) =
        config.enable_read_request_caching();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_minimal_catalog_caches_preload) =
        config.minimal_catalog_caches_preload;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_response_cache_size_percentage) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_tables) =
        config.preload_additional_catalog_tables;
    if (config.response_cache_size_bytes.has_value()) {
      ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_response_cache_size_bytes) =
          *config.response_cache_size_bytes;
    }
    if (!config.preload_additional_catalog_list.empty()) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_table_list) =
        std::string(config.preload_additional_catalog_list);
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_use_relcache_file) = config.use_relcache_file;
    PgMiniTestBase::SetUp();
    metrics_.emplace(*cluster_->mini_master()->master()->metric_entity(),
                     *cluster_->mini_tablet_server(0)->server()->metric_entity());
  }

  size_t NumTabletServers() override {
    return 1;
  }

  Result<uint64_t> CacheRefreshRPCCount() {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableCatCacheEventLogging(&conn));
    auto conn_aux = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableCatCacheEventLogging(&conn_aux));
    RETURN_NOT_OK(conn_aux.Execute("CREATE TABLE t (k INT)"));
    RETURN_NOT_OK(conn_aux.Execute("ALTER TABLE t ADD COLUMN v INT"));
    // Catalog version was increased by the conn_aux but conn may not detect this immediately.
    // So run simplest possible query which doesn't produce RPC in a loop until number of
    // RPC will be greater than 0.
    for (;;) {
      const auto result = VERIFY_RESULT(metrics_->Delta([&conn] {
        return conn.Execute("ROLLBACK");
      })).master_read_rpc;
      if (result) {
        return result;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    return STATUS(RuntimeError, "Unreachable statement");
  }

  using AfterCacheRefreshFunctor = std::function<Status(PGConn*)>;

  Result<uint64_t> RPCCountAfterCacheRefresh(const AfterCacheRefreshFunctor& functor) {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute("CREATE TABLE cache_refresh_trigger (k INT)"));
    // Force version increment. Next new connection will do cache refresh on start.
    RETURN_NOT_OK(conn.Execute("ALTER TABLE cache_refresh_trigger ADD COLUMN v INT"));
    auto aux_conn = VERIFY_RESULT(Connect());
    auto res = VERIFY_RESULT(metrics_->Delta([&functor, &aux_conn] {
      return functor(&aux_conn);
    })).master_read_rpc;
    RETURN_NOT_OK(conn.Execute("DROP TABLE cache_refresh_trigger"));
    return res;
  }

  Result<uint64_t> RPCCountOnStartUp(const std::string& db_name = {}) {
    auto res = VERIFY_RESULT(metrics_->Delta([this, &db_name] {
      return ResultToStatus(ConnectToDB(db_name));
    })).master_read_rpc;
    return res;
  }

  std::optional<MetricWatcher<MetricCountersDescriber>> metrics_;

 private:
  virtual Configuration GetConfig() const = 0;
};

class PgCatalogPerfBasicTest : public PgCatalogPerfTestBase {
 protected:
  // Test checks number of RPC to a master caused by the first INSERT stmt into a table with primary
  // key after cache refresh.
  void TestAfterCacheRefreshRPCCountOnInsert(size_t expected_master_rpc_count) {
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
    auto master_rpc_count_for_insert = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
      return conn->Execute("INSERT INTO t VALUES(0)");
    }));
    ASSERT_EQ(master_rpc_count_for_insert, expected_master_rpc_count);
  }

  // Test checks number of RPC to a master caused by the first SELECT stmt from a table with primary
  // key after cache refresh.
  void TestAfterCacheRefreshRPCCountOnSelect(size_t expected_master_rpc_count) {
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
    auto master_rpc_count_for_select = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
      VERIFY_RESULT(conn->Fetch("SELECT * FROM t"));
      return static_cast<Status>(Status::OK());
    }));
    ASSERT_EQ(master_rpc_count_for_select, expected_master_rpc_count);
  }
};

constexpr auto kResponseCacheSize5MB = 5 * 1024 * 1024;
constexpr auto kPreloadCatalogList =
    "pg_cast,pg_inherits,pg_policy,pg_proc,pg_tablespace,pg_trigger"sv;
constexpr auto kExtendedTableList =
    "pg_cast,pg_inherits,pg_policy,pg_proc,pg_tablespace,pg_trigger,pg_statistic,pg_invalid"sv;
constexpr auto kShortTableList = "pg_inherits"sv;

constexpr Configuration kConfigDefault;

constexpr Configuration kConfigMinPreload{.minimal_catalog_caches_preload = true};

constexpr Configuration kConfigWithUnlimitedCache{
    .response_cache_size_bytes = 0, .preload_additional_catalog_list = kPreloadCatalogList};

constexpr Configuration kConfigWithLimitedCache{
    .response_cache_size_bytes = kResponseCacheSize5MB,
    .preload_additional_catalog_list = kPreloadCatalogList};

constexpr Configuration kConfigWithPreloadAdditionalCatList{
    .response_cache_size_bytes = kResponseCacheSize5MB,
    .preload_additional_catalog_list = kExtendedTableList};

constexpr Configuration kConfigWithPreloadAdditionalCatTables{
    .response_cache_size_bytes = kResponseCacheSize5MB,
    .preload_additional_catalog_list = kPreloadCatalogList,
    .preload_additional_catalog_tables = true};

constexpr Configuration kConfigWithPreloadAdditionalCatBoth{
    .response_cache_size_bytes = kResponseCacheSize5MB,
    .preload_additional_catalog_list = kExtendedTableList,
    .preload_additional_catalog_tables = true};

constexpr Configuration kConfigPredictableMemoryUsage{
    .response_cache_size_bytes = 0,
    .use_relcache_file = false};

constexpr Configuration kConfigSmallPreload{
    .preload_additional_catalog_list = kShortTableList};

template<class Base, const Configuration& Config>
class ConfigurableTest : public Base {
 private:
  Configuration GetConfig() const override {
    return Config;
  }
};

using PgCatalogPerfTest = ConfigurableTest<PgCatalogPerfBasicTest, kConfigDefault>;
using PgCatalogMinPreloadTest = ConfigurableTest<PgCatalogPerfBasicTest, kConfigMinPreload>;
using PgCatalogWithUnlimitedCachePerfTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithUnlimitedCache>;
using PgCatalogWithLimitedCachePerfTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithLimitedCache>;
using PgPreloadAdditionalCatListTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithPreloadAdditionalCatList>;
using PgPreloadAdditionalCatTablesTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithPreloadAdditionalCatTables>;
using PgPreloadAdditionalCatBothTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigWithPreloadAdditionalCatBoth>;
using PgPredictableMemoryUsageTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigPredictableMemoryUsage>;
using PgSmallPreloadTest =
    ConfigurableTest<PgCatalogPerfTestBase, kConfigSmallPreload>;

class PgCatalogWithStaleResponseCacheTest : public PgCatalogWithUnlimitedCachePerfTest {
 protected:
  void SetUp() override {
    constexpr uint64_t kHistoryCutoffInitialValue = 10000000;
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_TEST_committed_history_cutoff_initial_value_usec) = kHistoryCutoffInitialValue;
    // Substitute catalog_read_time in cached responses with value lower than history cutoff to
    // get 'Snapshot too old' error on attempt to read at this read time.
    ANNOTATE_UNPROTECTED_WRITE(
        FLAGS_TEST_pg_response_cache_catalog_read_time_usec) = kHistoryCutoffInitialValue - 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_cache_response_renew_soft_lifetime_limit_ms) = 1000;
    PgCatalogWithUnlimitedCachePerfTest::SetUp();
  }
};

constexpr uint64_t kFirstConnectionRPCCountDefault = 5;
constexpr uint64_t kFirstConnectionRPCCountWithAdditionalTables = 6;
constexpr uint64_t kFirstConnectionRPCCountWithSmallPreload = 5;
constexpr uint64_t kSubsequentConnectionRPCCount = 2;
static_assert(kFirstConnectionRPCCountDefault <= kFirstConnectionRPCCountWithAdditionalTables);

} // namespace

// Test checks the number of RPC for very first and subsequent connection to same t-server.
// Very first connection prepares local cache file while subsequent connections doesn't do this.
// As a result number of RPCs has huge difference.
// Note: Also subsequent connections doesn't preload the cache. This maybe changed in future.
//       Number of RPCs in all the tests are not the constants and they can be changed in future.
TEST_F(PgCatalogPerfTest, StartupRPCCount) {
  const auto first_connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(first_connect_rpc_count, kFirstConnectionRPCCountDefault);
  const auto subsequent_connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(subsequent_connect_rpc_count, kSubsequentConnectionRPCCount);
}

// Test checks number of RPC in case of cache refresh without partitioned tables.
TEST_F(PgCatalogPerfTest, CacheRefreshRPCCountWithoutPartitionTables) {
  const auto cache_refresh_rpc_count = ASSERT_RESULT(CacheRefreshRPCCount());
  ASSERT_EQ(cache_refresh_rpc_count, 3);
}

// Test checks number of RPC in case of cache refresh with partitioned tables.
TEST_F(PgCatalogPerfTest, CacheRefreshRPCCountWithPartitionTables) {
  auto conn = ASSERT_RESULT(Connect());
  for (size_t ti = 0; ti < 3; ++ti) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0 (r INT, v INT) PARTITION BY RANGE(r)", ti));
    for (size_t pi = 0; pi < 3; ++pi) {
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE t$0_p$1 PARTITION OF t$0 FOR VALUES FROM ($2) TO ($3)",
          ti, pi, 100 * pi + 1, 100 * (pi + 1)));
    }
  }

  constexpr auto kTableWithCastInPartitioning = "t_with_cast";
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0 (d DATE, v INT) PARTITION BY RANGE(EXTRACT(month FROM d))",
      kTableWithCastInPartitioning));
  ASSERT_OK(conn.ExecuteFormat(
      "CREATE TABLE $0_p0 PARTITION OF $0 FOR VALUES FROM (1) TO (12)",
      kTableWithCastInPartitioning));

  const auto cache_refresh_rpc_count = ASSERT_RESULT(CacheRefreshRPCCount());
  ASSERT_EQ(cache_refresh_rpc_count, 6);
}

TEST_F(PgCatalogPerfTest, AfterCacheRefreshRPCCountOnInsert) {
  TestAfterCacheRefreshRPCCountOnInsert(/*expected_master_rpc_count=*/ 1);
}

TEST_F_EX(PgCatalogPerfTest,
          AfterCacheRefreshRPCCountOnInsertMinPreload,
          PgCatalogMinPreloadTest) {
  TestAfterCacheRefreshRPCCountOnInsert(/*expected_master_rpc_count=*/ 6);
}

TEST_F(PgCatalogPerfTest, AfterCacheRefreshRPCCountOnSelect) {
  TestAfterCacheRefreshRPCCountOnSelect(/*expected_master_rpc_count=*/ 3);
}

TEST_F_EX(PgCatalogPerfTest,
          AfterCacheRefreshRPCCountOnSelectMinPreload,
          PgCatalogMinPreloadTest) {
  TestAfterCacheRefreshRPCCountOnSelect(/*expected_master_rpc_count=*/ 11);
}

// The test checks number of hits in response cache in case of multiple connections and aggressive
// sys catalog changes. Which causes catalog cache refresh in each established connection.
TEST_F_EX(PgCatalogPerfTest, ResponseCacheEfficiency, PgCatalogWithUnlimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t (r INT PRIMARY KEY)"));
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("ALTER TABLE t ADD COLUMN v INT"));
  std::vector<PGConn> conns;
  constexpr size_t kConnectionCount = 20;
  constexpr size_t kAlterTableCount = 10;
  const std::string select_all("SELECT * FROM t");
  for (size_t i = 0; i < kConnectionCount; ++i) {
    conns.push_back(ASSERT_RESULT(Connect()));
    ASSERT_RESULT(conns.back().Fetch(select_all));
  }
  ASSERT_RESULT(aux_conn.Fetch(select_all));
  const auto metrics = ASSERT_RESULT(metrics_->Delta(
      [&conn, &conns] {
        for (size_t i = 0; i < kAlterTableCount; ++i) {
          RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN v_$0 INT", i));
          TestThreadHolder holder;
          size_t conn_idx = 0;
          for (auto& c : conns) {
            holder.AddThread([&c, idx = conn_idx, i] {
              ASSERT_OK(c.ExecuteFormat("INSERT INTO t VALUES($0)", idx * 100 + i));
            });
            ++conn_idx;
          }
        }
        return static_cast<Status>(Status::OK());
      }));
  constexpr auto kExpectedRows = kAlterTableCount * kConnectionCount;
  constexpr auto kExpectedColumns = kAlterTableCount + 2;
  ASSERT_OK(conn.FetchMatrix(select_all, kExpectedRows, kExpectedColumns));
  ASSERT_OK(aux_conn.FetchMatrix(select_all, kExpectedRows, kExpectedColumns));
  constexpr size_t kUniqueQueriesPerRefresh = 3;
  constexpr auto kUniqueQueries = kAlterTableCount * kUniqueQueriesPerRefresh;
  constexpr auto kTotalQueries = kConnectionCount * kUniqueQueries;
  ASSERT_EQ(metrics.cache.queries, kTotalQueries);
  ASSERT_EQ(metrics.cache.hits, kTotalQueries - kUniqueQueries);
  ASSERT_LE(metrics.master_read_rpc, 720);
}

TEST_F_EX(PgCatalogPerfTest,
          ResponseCacheEfficiencyInConnectionStart,
          PgCatalogWithUnlimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());
  auto metrics = ASSERT_RESULT(metrics_->Delta([this] {
    RETURN_NOT_OK(Connect());
    return static_cast<Status>(Status::OK());
  }));
  ASSERT_EQ(metrics.cache.queries, 4);
  ASSERT_EQ(metrics.cache.hits, 4);
}

// The test checks response cache renewing process in case of 'Snapshot too old' error.
// This error is possible in the following situation:
//   - several days ago at time T1 first connection was established to DB
//   - multiple (due to paging) responses for YSQL sys catalog cache were cached on a local tserver
//     by the PgResponseCache. These responses has catalog_read_time equal to T1
//   - later some (but not all) of these responses were discarded from the LRU cache
//     in the PgResponseCache
//   - new connection is establishing to same DB
//   - PgResponseCache provides cached response for initial request with read time T1
//   - PgResponseCache doesn't have cached responses for further requests with read time T1 and
//     send read request to a Master
//   - Master responds with 'Snapshot too old' error on attempt to read at really old read time T1
TEST_F_EX(PgCatalogPerfTest,
          ResponseCacheWithTooOldSnapshot,
          PgCatalogWithStaleResponseCacheTest) {
  auto connector = [this] {
    RETURN_NOT_OK(Connect());
    return static_cast<Status>(Status::OK());
  };

  auto first_connection_cache_metrics = ASSERT_RESULT(metrics_->Delta(connector)).cache;
  ASSERT_EQ(first_connection_cache_metrics.renew_hard, 0);
  ASSERT_EQ(first_connection_cache_metrics.renew_soft, 0);
  ASSERT_EQ(first_connection_cache_metrics.hits, 0);
  ASSERT_EQ(first_connection_cache_metrics.queries, 4);

  std::this_thread::sleep_for(std::chrono::milliseconds(
      2 * FLAGS_pg_cache_response_renew_soft_lifetime_limit_ms));

  auto second_connection_cache_metrics = ASSERT_RESULT(metrics_->Delta(connector)).cache;
  ASSERT_EQ(second_connection_cache_metrics.renew_hard, 0);
  ASSERT_EQ(second_connection_cache_metrics.renew_soft, 1);
  ASSERT_EQ(second_connection_cache_metrics.hits, 1);
  ASSERT_EQ(second_connection_cache_metrics.queries, 6);
}

// The test checks that GC keeps response cache memory lower than limit
TEST_F_EX(PgCatalogPerfTest, ResponseCacheMemoryLimit, PgCatalogWithLimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k SERIAL PRIMARY KEY, v INT)"));
  auto aux_conn = ASSERT_RESULT(Connect());
  constexpr size_t kAlterTableCount = 10;
  const auto cache_metrics = ASSERT_RESULT(metrics_->Delta(
      [&conn, &aux_conn] {
        for (size_t i = 0; i < kAlterTableCount; ++i) {
          RETURN_NOT_OK(conn.ExecuteFormat("ALTER TABLE t ADD COLUMN v_$0 INT", i));
          RETURN_NOT_OK(aux_conn.ExecuteFormat("INSERT INTO t(v) VALUES(1)"));
        }
        return static_cast<Status>(Status::OK());
      })).cache;
  ASSERT_EQ(cache_metrics.gc_calls, 9);
  ASSERT_EQ(cache_metrics.entries_removed_by_gc, 26);
  auto response_cache_mem_tracker =
      cluster_->mini_tablet_server(0)->server()->mem_tracker()->FindChild("PgResponseCache");
  ASSERT_TRUE(response_cache_mem_tracker);
  const auto peak_consumption = response_cache_mem_tracker->peak_consumption();
  ASSERT_GT(peak_consumption, 0);
  ASSERT_LE(peak_consumption, FLAGS_pg_response_cache_size_bytes);
}

TEST_F(PgCatalogPerfTest, RPCCountAfterDdlFailure) {
  auto rpc_count_for_ddl_success = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
    return conn->Execute("CREATE TABLE mytable1 (id int)");
  }));
  auto rpc_count_for_ddl_failure = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
    RETURN_NOT_OK(conn->Execute("SET yb_test_fail_next_ddl=true"));
    if (conn->Execute("CREATE TABLE mytable (id int)").ok()) {
      return STATUS(RuntimeError, "Expected to fail Ddl");
    }
    return static_cast<Status>(Status::OK());
  }));
  // The failed DDL will trigger a lookup for the catalog version. This will result in a read call
  // to the master.
  ASSERT_EQ(rpc_count_for_ddl_failure, rpc_count_for_ddl_success + 1);
}

TEST_F(PgCatalogPerfTest, RPCCountAfterDmlFailure) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE mytable (id INT PRIMARY KEY)"));
  auto rpc_count = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
    if (conn->Execute("INSERT INTO mytable VALUES (1), (1)").ok()) {
      return STATUS(RuntimeError, "Expected to fail Insert due to violation");
    }
    return static_cast<Status>(Status::OK());
  }));
  // We expect 2 reads. One read to lookup the table in pg_class and the other to lookup the
  // pg_catalog_version to check if cache refresh is required.
  ASSERT_EQ(rpc_count, 2);
}

TEST_F(PgCatalogPerfTest, RPCCountAfterConflictError) {
  auto conn1 = ASSERT_RESULT(Connect());
  ASSERT_OK(conn1.Execute("CREATE TABLE mytable (id INT PRIMARY KEY)"));
  ASSERT_OK(conn1.Execute("SET default_transaction_isolation='repeatable read'"));
  ASSERT_OK(conn1.Execute("BEGIN"));
  ASSERT_OK(conn1.Execute("INSERT INTO mytable VALUES (1)"));
  auto rpc_count = ASSERT_RESULT(RPCCountAfterCacheRefresh([&](PGConn* conn) {
    RETURN_NOT_OK(conn->Execute("SET default_transaction_isolation='repeatable read'"));
    RETURN_NOT_OK(conn->Execute("BEGIN"));
    RETURN_NOT_OK(conn->Execute("INSERT INTO mytable VALUES (3)"));
    RETURN_NOT_OK(conn1.Execute("COMMIT"));
    if (conn->Execute("INSERT INTO mytable VALUES (1)").ok()) {
      return STATUS(RuntimeError, "Expected to fail insert with conflict");
    }
    return static_cast<Status>(Status::OK());
  }));
  // The transaction failed due to conflict. This means we would not have checked whether any
  // intervening DDL occurred, so the only read call must be the one to lookup pg_class.
  ASSERT_EQ(rpc_count, 1);
}

TEST_F_EX(PgCatalogPerfTest,
          RPCCountOnStartupAdditionalCatListPreload,
          PgPreloadAdditionalCatListTest) {
  // No failures even there are invalid PG catalog on the flag list.
  const auto rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(rpc_count, kFirstConnectionRPCCountWithAdditionalTables);
}

TEST_F_EX(PgCatalogPerfTest,
          RPCCountOnStartupAdditionalCatTablesPreload,
          PgPreloadAdditionalCatTablesTest) {
  const auto rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(rpc_count, kFirstConnectionRPCCountWithAdditionalTables);
}

TEST_F_EX(PgCatalogPerfTest,
          RPCCountOnStartupAdditionalCatBothPreload,
          PgPreloadAdditionalCatBothTest) {
  const auto rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(rpc_count, kFirstConnectionRPCCountWithAdditionalTables);
}

TEST_F_EX(PgCatalogPerfTest,
          RPCCountOnStartupSmallPreload,
          PgSmallPreloadTest) {
  const auto rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(rpc_count, kFirstConnectionRPCCountWithSmallPreload);
}

// Test checks that response cache is DB specific.
// First, start up an initial connection and measure the startup RPC count (a).
// Then, create a new database. Create a new connection to this database and
// measure the startup RPC count (b). Both (a) and (b) should be equal to the
// initial startup RPC count, because the second connection can't use the
// cached responses from the first connection. We also check that upon creating
// a subsequent connection to the new database, the startup RPC count is what we
// expect for a second connection.
TEST_F_EX(PgCatalogPerfTest, ResponseCacheIsDBSpecific, PgCatalogWithUnlimitedCachePerfTest) {
  constexpr auto* kDBName = "db1";
  auto rpc_count_checker = [this](const std::string& db_name = {}) -> Status {
    for (auto expected_rpc_count : {kFirstConnectionRPCCountWithAdditionalTables,
                                    kSubsequentConnectionRPCCount}) {
      const auto rpc_count = VERIFY_RESULT(RPCCountOnStartUp(db_name));
      SCHECK_EQ(rpc_count, expected_rpc_count, IllegalState, "Unexpected rpc count");
    }
    return Status::OK();
  };
  ASSERT_OK(rpc_count_checker());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDBName));
  ASSERT_OK(rpc_count_checker(kDBName));
}

TEST_F_EX(PgCatalogPerfTest,
          RPCCountOnStartupPredictableMemoryUsage,
          PgPredictableMemoryUsageTest) {
  const auto first_connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(first_connect_rpc_count, kFirstConnectionRPCCountWithAdditionalTables);
  const auto subsequent_connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(subsequent_connect_rpc_count, kSubsequentConnectionRPCCount);
}

// The test checks that response cache for specific DB is invalidated in case of closure of
// connection with temp tables. Response cache for other DBs is not affected.
TEST_F_EX(PgCatalogPerfTest,
          ResponseCacheInvalidationOnConnectionWithTempTableClosure,
          PgCatalogWithUnlimitedCachePerfTest) {
  constexpr auto* kDBName = "aux_db";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDBName));

  {
    auto conn_with_temp_table = ASSERT_RESULT(ConnectToDB(kDBName));
    ASSERT_OK(conn_with_temp_table.Execute("CREATE TEMP TABLE t(k INT PRIMARY KEY)"));
  }

  // Wait for cleanup completion of all closed connections.
  std::this_thread::sleep_for(RegularBuildVsDebugVsSanitizers(3s, 5s, 5s));

  const auto default_db_connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(default_db_connect_rpc_count, kSubsequentConnectionRPCCount);

  for (auto expected_rpc_count : {kFirstConnectionRPCCountWithAdditionalTables,
                                  kSubsequentConnectionRPCCount}) {
    const auto connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp(kDBName));
    ASSERT_EQ(connect_rpc_count, expected_rpc_count);
  }
}

// The test checks that response cache for specific DB is invalidated in case of discarding temp
// tables. Response cache for other DBs is not affected.
TEST_F_EX(PgCatalogPerfTest,
          ResponseCacheInvalidationOnDiscardTempTables,
          PgCatalogWithUnlimitedCachePerfTest) {
  constexpr auto* kDBName = "aux_db";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDBName));
  auto aux_conn = ASSERT_RESULT(ConnectToDB(kDBName));
  ASSERT_OK(aux_conn.Execute("CREATE TEMP TABLE t(k INT PRIMARY KEY)"));

  const auto disable_calls = ASSERT_RESULT(metrics_->Delta([&aux_conn] {
    return aux_conn.Execute("DISCARD ALL");
  })).cache.disable_calls;
  ASSERT_EQ(disable_calls, 1);

  const auto default_db_connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp());
  ASSERT_EQ(default_db_connect_rpc_count, kSubsequentConnectionRPCCount);

  for (auto expected_rpc_count : {kFirstConnectionRPCCountWithAdditionalTables,
                                  kSubsequentConnectionRPCCount}) {
    const auto connect_rpc_count = ASSERT_RESULT(RPCCountOnStartUp(kDBName));
    ASSERT_EQ(connect_rpc_count, expected_rpc_count);
  }
}

// The test checks that different connections can use same temp table names in case of temp
// namespace reusing when response cache is enabled.
TEST_F_EX(PgCatalogPerfTest,
          SameTempTableCreationWithResponseCache,
          PgCatalogWithUnlimitedCachePerfTest) {
  const auto temp_table_creator = [](PGConn* conn) {
    return conn->Execute("CREATE TEMP TABLE temptest(k INT)");
  };

  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(temp_table_creator(&conn));
    // Trigger catalog version update.
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(k INT);DROP TABLE $0", "t"));
    // New connection will reload catalog cache and fill response cache with the data.
    // Response data contains info about temporary table.
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.Fetch("SELECT 1"));
  }
  // Wait for cleanup completion of all closed connections.
  std::this_thread::sleep_for(RegularBuildVsDebugVsSanitizers(3s, 5s, 5s));
  auto conn = ASSERT_RESULT(Connect());
  const auto disable_calls = ASSERT_RESULT(metrics_->Delta([&conn, &temp_table_creator] {
    return temp_table_creator(&conn);
  })).cache.disable_calls;
  // Check that response cache has not been invalidated while temp namespace reusing.
  ASSERT_EQ(disable_calls, 0);
}

TEST_F_EX(PgCatalogPerfTest,
          OnDemandLoadingAfterCatalogCacheRefresh,
          PgCatalogWithUnlimitedCachePerfTest) {
  auto conn = ASSERT_RESULT(Connect());

  {
    auto aux_conn = ASSERT_RESULT(Connect());
    ASSERT_OK(aux_conn.Execute("CREATE TABLE t(k INT PRIMARY KEY)"));
    ASSERT_OK(aux_conn.Execute(
        "CREATE FUNCTION my_func(v int) RETURNS int AS $$ "
        "BEGIN return v; END; $$ LANGUAGE plpgsql"));

    ASSERT_OK(IncrementAllDBCatalogVersions(aux_conn));
  }

  {
    // Fill response cache with fresh catalog data after catalog version increment
    auto aux_conn = ASSERT_RESULT(Connect());
  }

  // Sleep for a while to make a little time gap between read time in response cache and allowed
  // read time due to history cutoff
  std::this_thread::sleep_for(1s);

  {
    // Cutoff catalog history for current time to avoid reading with old read time
    auto* tablet = cluster_->mini_master(0)->master()->catalog_manager()->tablet_peer()->tablet();
    auto* policy = tablet->RetentionPolicy();
    auto cutoff = policy->GetRetentionDirective().history_cutoff;
    cutoff.primary_cutoff_ht = HybridTime::FromMicros(
        implicit_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()));
    policy->UpdateCommittedHistoryCutoff(cutoff);
  }

  // Sleep for a while to make sure that new reads from catalog will use read time
  // after cuttoff bound
  std::this_thread::sleep_for(1s);

  // It is expected that next statement will refresh the cache due to catalog version bumping.
  // All the catalog data will be loaded from the response cache, because it already has data for
  // required catalog version. Also the statement will perform on-demand loading of some cache entry
  // required for usage of `my_func`. On-demand loading must use empty read time.
  // Otherwise statement will fail due to `Snapshot too old` error.
  ASSERT_OK(conn.Execute("INSERT INTO t VALUES (my_func(1))"));
}

} // namespace yb::pgwrapper
