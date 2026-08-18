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

#include "yb/util/logging_test_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_pause_perform_with_paging_state);
DECLARE_bool(ysql_catalog_preload_additional_tables);

namespace yb::pgwrapper {

class PgReadTimeResponseCacheTest : public PgMiniTestBase {
 protected:
  void BeforePgProcessStart() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_catalog_preload_additional_tables) = true;
  }
};

// The relcache preload triggered by SET yb_read_time is a chain of multiple RPCs. Before the
// preload started to bypass the response cache for nonzero yb_read_time, each RPC was served by
// the cache independently: the cache key does not include yb_read_time, so a cache hit returned
// rows as of the fill time while a cache miss read at yb_read_time. A temp table DDL commit
// disables the cache between two RPCs of the chain (silently_altered_db path). The preload then
// observed a pg_class row of a temp table without its pg_attribute rows and failed with
// "cache lookup failed for attribute 1 of relation N".
TEST_F(PgReadTimeResponseCacheTest, ReadTimePreloadConsistentUnderCacheInvalidation) {
  auto conn = ASSERT_RESULT(Connect());
  // Push pg_attribute beyond one prefetch page (yb_fetch_row_limit rows) while pg_class stays
  // within one page. The pg_class rows of the temp tables created below are then fetched by the
  // first RPC of the preload chain while their pg_attribute rows are fetched by continuation
  // RPCs.
  std::string create_wide = "CREATE TABLE wide(c0 int";
  for (int i = 1; i < 1100; ++i) {
    create_wide += Format(", c$0 int", i);
  }
  create_wide += ")";
  ASSERT_OK(conn.Execute(create_wide));

  const auto read_time_micros = ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT (extract(epoch from now()) * 1000000)::bigint"));
  SleepFor(1s);

  // Temp tables created after the read time. The session stays open to keep them alive.
  auto holder = ASSERT_RESULT(Connect());
  ASSERT_OK(holder.Execute("CREATE TEMP TABLE det_a(a int, b text, c int, d text, e int)"));
  ASSERT_OK(holder.Execute("CREATE TEMP TABLE det_b(a int, b text, c int, d text, e int)"));

  // A new connection preloads the relcache and fills the response cache. The temp tables are
  // visible in the cached data.
  ASSERT_RESULT(Connect());

  // Connect the DDL and victim sessions while the pause flag is off, otherwise their own
  // connection-time preloads would pause.
  auto ddl_conn = ASSERT_RESULT(Connect());
  auto victim = ASSERT_RESULT(Connect());
  ASSERT_OK(victim.Execute("SET yb_disable_catalog_version_check TO true"));
  ASSERT_OK(victim.ExecuteFormat("SET yb_read_time = $0", read_time_micros));

  StringWaiterLogSink pause_waiter("Pausing due to flag");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_perform_with_paging_state) = true;
  auto se = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_perform_with_paging_state) = false;
  });

  // The statement triggers a full catalog cache refresh which preloads the relcache at
  // yb_read_time. The first (non-paged) RPC of the chain is served from the response cache,
  // continuation RPCs pause on the flag.
  Status victim_status;
  TestThreadHolder threads;
  threads.AddThread([&victim, &victim_status] {
    victim_status = ResultToStatus(victim.FetchRow<PGUint64>(
        "SELECT count(*) FROM pg_attribute"));
  });
  ASSERT_OK(pause_waiter.WaitFor(60s * kTimeMultiplier));

  // Temp table DDL commit disables the response cache group of the database. The flag stays set
  // across the DDL to keep the preload paused until the cache is disabled, otherwise the preload
  // may complete its chain of requests before that happens. The DDL itself is not affected by the
  // flag because its requests have no paging state.
  ASSERT_OK(ddl_conn.Execute("CREATE TEMP TABLE bump_tbl(a int)"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_perform_with_paging_state) = false;
  threads.JoinAll();

  ASSERT_OK(victim_status);
}

}  // namespace yb::pgwrapper
