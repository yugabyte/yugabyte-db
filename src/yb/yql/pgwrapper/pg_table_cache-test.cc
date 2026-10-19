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

#include <chrono>
#include <functional>
#include <memory>
#include <ranges>

#include "yb/common/pg_types.h"

#include "yb/gutil/dynamic_annotations.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_util.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

using namespace std::literals;

DECLARE_int32(master_inject_latency_on_tablet_lookups_ms);
DECLARE_bool(TEST_enable_pg_client_mock);
DECLARE_int32(rpc_workers_limit);

namespace yb::pgwrapper {
using tserver::PgTableCache;
using tserver::PgTablesQueryListener;
using tserver::PgTablesQueryResult;
using tserver::TabletServer;
using tserver::PgClientServiceMockImpl;

namespace {

class PgTableCacheTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override { return 1; }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_enable_pg_client_mock) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_workers_limit) = 32;
    PgMiniTestBase::SetUp();
    table_cache_ = &GetServer().TEST_GetPgClientService()->TEST_TableCache();
  }

  TabletServer& GetServer() {
    return *CHECK_NOTNULL(cluster_->mini_tablet_server(0)->server());
  }

  PgClientServiceMockImpl& ClientServiceMock() {
    return *CHECK_NOTNULL(GetServer().TEST_GetPgClientServiceMock());
  }

  PgTableCache* table_cache_{nullptr};
};

class MockPgTablesQueryListener : public PgTablesQueryListener {
 public:
  using OnReadyFunctor = std::function<void(PgTablesQueryResult&&)>;

  explicit MockPgTablesQueryListener(OnReadyFunctor&& on_ready) : on_ready_(std::move(on_ready)) {}

  void Ready(PgTablesQueryResult&& result) override { on_ready_(std::move(result)); }

 private:
  OnReadyFunctor on_ready_;
};

} // namespace

// The test checks that listener is notified if GetTables is called with empty table_ids
TEST_F(PgTableCacheTest, GetEmptyTables) {
  CountDownLatch call_latch{1};
  auto listener = std::make_shared<MockPgTablesQueryListener>(
      [&call_latch](PgTablesQueryResult&& result) {
        ScopeExit se{[&call_latch] { call_latch.CountDown(); }};
        // Check result is usable (no crash on access)
        ASSERT_NOK(result.Get(PgObjectId{1, 1}.GetYbTableId()));
      });
  table_cache_->GetTables({}, listener);
  ASSERT_TRUE(call_latch.WaitFor(15s));
}

// The test checks multiple waiters on same table cache entry will not block each other.
// I.e. system will not get stuck when the last waiter owns session lock and other waiters tries to
// take same lock.
TEST_F(PgTableCacheTest, MultipleWaitersWhenLastOwnsSessionLock) {
  constexpr auto kMaxInFlightOps = 200;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE t(k INT PRIMARY KEY, v INT)"));
  const auto table_id = PgObjectId{
      ASSERT_RESULT(conn.FetchRow<PGOid>(
          "SELECT oid FROM pg_database WHERE datname = current_database()")),
      ASSERT_RESULT(conn.FetchRow<PGOid>("SELECT 't'::regclass::oid"))}.GetYbTableId();
  auto inserter = [&conn, kMaxInFlightOps]() -> Status {
    RETURN_NOT_OK(conn.StartTransaction(IsolationLevel::SNAPSHOT_ISOLATION));
    RETURN_NOT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT s, s FROM generate_series(1, $0) AS s", kMaxInFlightOps));
    return conn.RollbackTransaction();
  };

  // Warm up cache
  ASSERT_OK(inserter());

  ASSERT_GT(kMaxInFlightOps, FLAGS_rpc_workers_limit);
  ASSERT_OK(conn.ExecuteFormat(
      "SET ysql_max_in_flight_ops = $0;"
      "SET ysql_session_max_batch_size = 1",
      kMaxInFlightOps));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_inject_latency_on_tablet_lookups_ms) =
      2000 * kTimeMultiplier;

  auto mock_perform = ClientServiceMock().MockPerformBefore(
    [table_cache = table_cache_, table_id](auto* req, auto*, const auto* ctx) -> Status {
      const auto& ops = req->ops();
      DCHECK(!ops.empty());
      const auto& op = ops.front();
      if (op.has_write() && op.write().table_id() == table_id) {
        if (!ctx->rpc) {
          // Invalidate the table and significantly slowdown shared exchange perform query to make
          // its waiter the last one in the table cache entry.
          table_cache->Invalidate(table_id);
          std::this_thread::sleep_until(
              NextDiscreteTimePoint(1000ms * kTimeMultiplier) + 500ms * kTimeMultiplier);
        } else {
          std::this_thread::sleep_for(100ms * kTimeMultiplier);
        }
      }
      return Status::OK();
    });
  for ([[maybe_unused]] auto _ : std::views::iota(1, 3)) {
    ASSERT_OK(inserter());
  }
}

} // namespace yb::pgwrapper
