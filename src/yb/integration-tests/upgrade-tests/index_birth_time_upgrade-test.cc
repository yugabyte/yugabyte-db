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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

// Indexes created on older builds do not persist index birth time. After upgrade, reads that use
// those indexes must still succeed (birth_time == 0 skips the read_time < birth_time rejection).
class IndexBirthTimeUpgradeTest : public UpgradeTestBase {
 public:
  IndexBirthTimeUpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}
};

TEST_F(IndexBirthTimeUpgradeTest, IndexScanWorksWithoutBirthTimeAfterUpgrade) {
  ASSERT_OK(StartClusterInOldVersion());

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE TABLE test (k INT PRIMARY KEY, v INT)"));
  ASSERT_OK(conn.Execute("INSERT INTO test SELECT i, i FROM generate_series(1, 100) i"));
  ASSERT_OK(conn.Execute("CREATE INDEX test_v_idx ON test (v)"));

  const auto query = "SELECT * FROM test WHERE v = 42";
  ASSERT_TRUE(ASSERT_RESULT(conn.HasIndexScan(query)));
  ASSERT_OK(conn.FetchMatrix(query, 1 /* rows */, 2 /* columns */));

  ASSERT_OK(UpgradeClusterToCurrentVersion());

  auto conn2 = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_TRUE(ASSERT_RESULT(conn2.HasIndexScan(query)));
  ASSERT_OK(conn2.FetchMatrix(query, 1 /* rows */, 2 /* columns */));
}

}  // namespace yb
