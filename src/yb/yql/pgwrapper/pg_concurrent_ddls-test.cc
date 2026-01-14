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

#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

DECLARE_bool(enable_object_locking_for_table_locks);
DECLARE_bool(ysql_yb_ddl_transaction_block_enabled);
DECLARE_bool(ysql_enable_auto_analyze);

using namespace std::literals;

namespace yb::pgwrapper {

class PgConcurrentDDLsTest : public LibPqTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    LibPqTestBase::UpdateMiniClusterOptions(opts);
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--enable_object_locking_for_table_locks=$0", EnableTableLocks()));
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--ysql_yb_ddl_transaction_block_enabled=$0", EnableTransactionalDdl()));
    opts->extra_tserver_flags.emplace_back(
        yb::Format("--ysql_pg_conf_csv=$0", "yb_fallback_to_legacy_catalog_read_time=false"));
  }

  int GetNumTabletServers() const override {
    return 3;
  }

  virtual bool EnableTableLocks() const {
    return true;
  }

  virtual bool EnableTransactionalDdl() const {
    return true;
  }
};

TEST_F(PgConcurrentDDLsTest, CreateIndexAndConcurrentAnalyze) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(0, 10), 0"));
  yb::TestThreadHolder thread_holder;
  thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag()] {
    auto analyze_conn = ASSERT_RESULT(Connect());
    while (!stop.load()) {
      ASSERT_OK(analyze_conn.Execute("ANALYZE test"));
    }
  });

  for (int i = 0; i < 10; i++) {
    LOG(INFO) << "Creating index " << i;
    ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx_$0 ON test(k)", i));
  }

  thread_holder.Stop();
  thread_holder.JoinAll();
}

TEST_F(PgConcurrentDDLsTest, ConcurrentCreateIndex) {
  auto kNumTables = 2;
  // TODO(#30015): If multiple threads create indexes on the same table, the "only a single oid is
  // allowed in BACKFILL INDEX" error is thrown.
  auto kNumThreads = 2;
  auto kNumIndexesPerThread = 4;

  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i < kNumTables; i++) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE test$0(k INT PRIMARY KEY, v INT)", i));
  }

  std::vector<yb::TestThreadHolder> thread_holders(kNumThreads);
  for (int i = 0; i < kNumThreads; i++) {
    thread_holders[i].AddThreadFunctor([this, i, kNumTables, kNumIndexesPerThread] {
      auto conn = ASSERT_RESULT(Connect());
      for (int j = 0; j < kNumIndexesPerThread; j++) {
        ASSERT_OK(conn.ExecuteFormat("CREATE INDEX idx_$1_$2 ON test$0(k)", i%kNumTables, i, j));
      }
    });
  }

  for (int i = 0; i < kNumThreads; i++) {
    thread_holders[i].JoinAll();
  }
}

}  // namespace yb::pgwrapper
