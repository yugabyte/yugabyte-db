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

namespace {

constexpr auto kCreatedDb = "created_before_finalize";
constexpr auto kDroppedDb = "dropped_before_finalize";

} // namespace

// 2025.1 reads a per-database row of pg_yb_logical_client_version on every connection. The V100
// migration replaces those rows with a single db_oid = 0 row, so until it runs at finalize,
// CREATE/DROP DATABASE on the current version must keep the per-database rows in sync.
class LogicalClientVersionUpgradeTest : public UpgradeTestBase {
 public:
  LogicalClientVersionUpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}

 protected:
  Status CreateAndDropDatabases() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kCreatedDb));
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDroppedDb));
    return conn.ExecuteFormat("DROP DATABASE $0", kDroppedDb);
  }
};

TEST_F(LogicalClientVersionUpgradeTest, CreateDatabaseBeforeRollback) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_OK(CreateAndDropDatabases());
  ASSERT_OK(RollbackClusterToOldVersion(kNoDelayBetweenNodes));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kCreatedDb));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<pgwrapper::PGUint64>(
      "SELECT COUNT(*) FROM pg_yb_logical_client_version v FULL JOIN pg_database d "
      "ON v.db_oid = d.oid WHERE v.db_oid IS NULL OR d.oid IS NULL")), 0);
}

TEST_F(LogicalClientVersionUpgradeTest, CreateDatabaseBeforeFinalize) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_OK(CreateAndDropDatabases());
  ASSERT_OK(FinalizeUpgrade());

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kCreatedDb));
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDroppedDb));
  ASSERT_OK(conn.ExecuteFormat("DROP DATABASE $0", kDroppedDb));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchAllAsString(
      "SELECT db_oid FROM pg_yb_logical_client_version")), "0");
}

} // namespace yb
