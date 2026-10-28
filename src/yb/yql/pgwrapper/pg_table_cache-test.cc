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

#include "yb/common/pg_types.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/pg_client_service.h"
#include "yb/tserver/pg_table_cache.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

namespace yb::pgwrapper {
using tserver::PgTableCache;
using tserver::PgTablesQueryListener;
using tserver::PgTablesQueryResult;

namespace {

class PgTableCacheTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override { return 1; }

  void SetUp() override {
    PgMiniTestBase::SetUp();
    table_cache_ =
        &cluster_->mini_tablet_server(0)->server()->TEST_GetPgClientService()->TEST_TableCache();
  }

  PgTableCache* table_cache_{nullptr};
};

class MockPgTablesQueryListener : public PgTablesQueryListener {
 public:
  using OnReadyFunctor = std::function<void(const PgTablesQueryResult&)>;

  explicit MockPgTablesQueryListener(OnReadyFunctor&& on_ready) : on_ready_(std::move(on_ready)) {}

  void Ready(const PgTablesQueryResult& result) override { on_ready_(result); }

 private:
  OnReadyFunctor on_ready_;
};

} // namespace

// The test checks that listener is notified if GetTables is called with empty table_ids
TEST_F(PgTableCacheTest, GetEmptyTables) {
  CountDownLatch call_latch{1};
  auto listener = std::make_shared<MockPgTablesQueryListener>(
      [&call_latch](const PgTablesQueryResult& result) {
        ScopeExit se{[&call_latch] { call_latch.CountDown(); }};
        // Check result is usable (no crash on access)
        ASSERT_NOK(result.Get(PgObjectId{1, 1}.GetYbTableId()));
      });
  table_cache_->GetTables({}, listener);
  ASSERT_TRUE(call_latch.WaitFor(15s));
}

} // namespace yb::pgwrapper
