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

#include <future>

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;

namespace yb {

class ObjectLockingPromotionUpgradeTest : public UpgradeTestBase {
 public:
  ObjectLockingPromotionUpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}
};

// Test for #36572 on the upgrade path itself.
//
// After an upgrade, ysql_yb_enable_object_locking_infra stays off until the AutoFlags are promoted,
// so transactions on the new binary run in legacy mode. A legacy writer that is still open when
// the flag is promoted must hold a local object lock, or the first new mode CREATE INDEX does not
// wait for it and builds an index without the writer's row.
TEST_F(ObjectLockingPromotionUpgradeTest, CreateIndexWaitsForLegacyWriterAcrossPromotion) {
#ifndef NDEBUG
  // enable_object_locking_for_table_locks is off by default in debug builds, so the current
  // version has no new mode to promote to.
  GTEST_SKIP() << "Object locking is off by default in debug builds";
#endif
  ASSERT_OK(StartClusterInOldVersion());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute("CREATE TABLE test(k INT PRIMARY KEY, v INT)"));
    ASSERT_OK(conn.Execute("INSERT INTO test SELECT generate_series(1, 10), 0"));
  }

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));

  auto legacy_conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_EQ(ASSERT_RESULT(legacy_conn.FetchRow<std::string>(
      "SHOW yb_enable_object_locking_infra")), "off");
  ASSERT_OK(legacy_conn.Execute("BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ"));
  ASSERT_OK(legacy_conn.Execute("UPDATE test SET v = 42 WHERE k = 1"));

  ASSERT_OK(PromoteAutoFlags(AutoFlagClass::kLocalPersisted));
  auto ddl_conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(LoggedWaitFor(
      [&ddl_conn]() -> Result<bool> {
        return VERIFY_RESULT(ddl_conn.FetchRow<std::string>(
                   "SHOW yb_enable_object_locking_infra")) == "on";
      },
      MonoDelta::FromSeconds(kTimeMultiplier * 60), "promotion to reach the backends"));

  auto create_index_future = std::async(std::launch::async, [&ddl_conn]() {
    return ddl_conn.Execute("CREATE INDEX test_v_idx ON test(v)");
  });
  ASSERT_EQ(create_index_future.wait_for(3s * kTimeMultiplier), std::future_status::timeout);
  ASSERT_OK(legacy_conn.Execute("COMMIT"));
  ASSERT_OK(create_index_future.get());

  ASSERT_OK(ddl_conn.Execute("SET enable_seqscan = off"));
  ASSERT_EQ(ASSERT_RESULT(ddl_conn.FetchRow<int64_t>(
      "SELECT COUNT(*) FROM test WHERE v = 42")), 1);

  // yb_index_check has the old release's catalog definition until the YSQL upgrade in finalize.
  ASSERT_OK(FinalizeUpgrade());
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Fetch("SELECT yb_index_check('test_v_idx'::regclass)"));
}

} // namespace yb
