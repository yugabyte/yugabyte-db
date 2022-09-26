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
#include <string>
#include <thread>
#include <unordered_map>

#include "yb/master/master.h"
#include "yb/master/mini_master.h"

#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Read);

namespace yb {
namespace pgwrapper {
namespace {

Status EnableCatcacheEventLogging(PGConn* conn) {
  return conn->Execute("SET yb_debug_log_catcache_events = ON");
}

class PgCatalogPerfTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
    read_rpc_watcher_ = std::make_unique<HistogramMetricWatcher>(
        *cluster_->mini_master()->master(),
        METRIC_handler_latency_yb_tserver_TabletServerService_Read);
  }

  Result<uint64_t> CacheRefreshRPCCount() {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableCatcacheEventLogging(&conn));
    auto conn_aux = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(EnableCatcacheEventLogging(&conn_aux));
    RETURN_NOT_OK(conn_aux.Execute("CREATE TABLE t (k INT)"));
    RETURN_NOT_OK(conn_aux.Execute("ALTER TABLE t ADD COLUMN v INT"));
    // Catalog version was increased by the conn_aux but conn may not detect this immediately.
    // So run simplest possible query which doesn't produce RPC in a loop until number of
    // RPC will be greater than 0.
    for (;;) {
      const auto result = VERIFY_RESULT(read_rpc_watcher_->Delta([&conn]() {
        return conn.Execute("ROLLBACK");
      }));
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
    return read_rpc_watcher_->Delta([&functor, &aux_conn] {
      return functor(&aux_conn);
    });
  }

  std::unique_ptr<HistogramMetricWatcher> read_rpc_watcher_;
};

} // namespace

// Test checks the number of RPC for very first and subsequent connection to same t-server.
// Very first connection prepares local cache file while subsequent connections doesn't do this.
// As a result number of RPCs has huge difference.
// Note: Also subsequent connections doesn't preload the cache. This maybe changed in future.
//       Number of RPCs in all the tests are not the constants and they can be changed in future.
TEST_F(PgCatalogPerfTest, YB_DISABLE_TEST_IN_TSAN(StartupRPCCount)) {
  const auto connector = [this]() -> Status {
    RETURN_NOT_OK(Connect());
    return Status::OK();
  };

  const auto first_connect_rpc_count = ASSERT_RESULT(read_rpc_watcher_->Delta(connector));
  ASSERT_EQ(first_connect_rpc_count, 5);
  const auto subsequent_connect_rpc_count = ASSERT_RESULT(read_rpc_watcher_->Delta(connector));
  ASSERT_EQ(subsequent_connect_rpc_count, 2);
}

// Test checks number of RPC in case of cache refresh without partitioned tables.
TEST_F(PgCatalogPerfTest, YB_DISABLE_TEST_IN_TSAN(CacheRefreshRPCCountWithoutPartitionTables)) {
  const auto cache_refresh_rpc_count = ASSERT_RESULT(CacheRefreshRPCCount());
  ASSERT_EQ(cache_refresh_rpc_count, 4);
}

// Test checks number of RPC in case of cache refresh with partitioned tables.
TEST_F(PgCatalogPerfTest, YB_DISABLE_TEST_IN_TSAN(CacheRefreshRPCCountWithPartitionTables)) {
  auto conn = ASSERT_RESULT(Connect());
  for (size_t ti = 0; ti < 3; ++ti) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE t$0 (r INT, v INT) PARTITION BY RANGE(r)", ti));
    for (size_t pi = 0; pi < 3; ++pi) {
      ASSERT_OK(conn.ExecuteFormat(
          "CREATE TABLE t$0_p$1 PARTITION OF t$0 FOR VALUES FROM ($2) TO ($3)",
          ti, pi, 100 * pi + 1, 100 * (pi + 1)));
    }
  }
  const auto cache_refresh_rpc_count = ASSERT_RESULT(CacheRefreshRPCCount());
  ASSERT_EQ(cache_refresh_rpc_count, 7);
}

// Test checks number of RPC to a master caused by the first INSERT stmt into a table with primary
// key after cache refresh.
TEST_F(PgCatalogPerfTest, YB_DISABLE_TEST_IN_TSAN(AfterCacheRefreshRPCCountOnInsert)) {
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(aux_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
  auto master_rpc_count_for_insert = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
    return conn->Execute("INSERT INTO t VALUES(0)");
  }));
  ASSERT_EQ(master_rpc_count_for_insert, 1);
}

// Test checks number of RPC to a master caused by the first SELECT stmt from a table with primary
// key after cache refresh.
TEST_F(PgCatalogPerfTest, YB_DISABLE_TEST_IN_TSAN(AfterCacheRefreshRPCCountOnSelect)) {
  auto aux_conn = ASSERT_RESULT(Connect());
  ASSERT_OK(aux_conn.Execute("CREATE TABLE t (k INT PRIMARY KEY)"));
  auto master_rpc_count_for_select = ASSERT_RESULT(RPCCountAfterCacheRefresh([](PGConn* conn) {
    VERIFY_RESULT(conn->Fetch("SELECT * FROM t"));
    return static_cast<Status>(Status::OK());
  }));
  ASSERT_EQ(master_rpc_count_for_select, 3);
}

} // namespace pgwrapper
} // namespace yb
