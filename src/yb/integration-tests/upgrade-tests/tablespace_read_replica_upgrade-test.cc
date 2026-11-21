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

#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

namespace {

constexpr auto kPlacement =
    R"({"num_replicas":1,"placement_blocks":[)"
    R"({"cloud":"cloud1","region":"datacenter1","zone":"rack1","min_num_replicas":1}]})";

constexpr auto kNotFinalizedError =
    "read_replica_placement cannot be set until the upgrade is finalized";

std::string CreateTablespaceCmd(const std::string& name, bool with_read_replicas) {
  return Format(
      "CREATE TABLESPACE $0 WITH (replica_placement='$1'$2)", name, kPlacement,
      with_read_replicas ? Format(", read_replica_placement='$0'", kPlacement) : "");
}

std::string AddReadReplicasCmd(const std::string& name) {
  return Format("ALTER TABLESPACE $0 SET (read_replica_placement='$1')", name, kPlacement);
}

}  // namespace

// 2025.1.1.0 does not support read_replica_placement in tablespaces. Its master fails to parse such
// a tablespace and drops its transaction status table, so the option must not be settable while
// the cluster can still be rolled back.
class TablespaceReadReplicaUpgradeTest : public UpgradeTestBase {
 public:
  TablespaceReadReplicaUpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}
};

TEST_F(TablespaceReadReplicaUpgradeTest, BlockedUntilFinalize) {
  ASSERT_OK(StartClusterInOldVersion());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_OK(conn.Execute(CreateTablespaceCmd("ts_live", /* with_read_replicas= */ false)));
  }

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /* auto_finalize= */ false));
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NOK_STR_CONTAINS(
        conn.Execute(CreateTablespaceCmd("ts_rr", /* with_read_replicas= */ true)),
        kNotFinalizedError);
    ASSERT_NOK_STR_CONTAINS(conn.Execute(AddReadReplicasCmd("ts_live")), kNotFinalizedError);
    ASSERT_OK(conn.Execute(CreateTablespaceCmd("ts_live2", /* with_read_replicas= */ false)));
    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY) TABLESPACE ts_live2"));
  }

  ASSERT_OK(FinalizeUpgrade());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    // Promoted AutoFlags reach postgres through an asynchronous config reload.
    ASSERT_OK(WaitFor(
        [&conn]() -> Result<bool> {
          return VERIFY_RESULT(conn.FetchRow<std::string>(
                     "SHOW yb_enable_tablespace_read_replica_placement")) == "on";
        },
        30s * kTimeMultiplier, "Wait for the AutoFlag to reach postgres"));
    ASSERT_OK(conn.Execute(CreateTablespaceCmd("ts_rr", /* with_read_replicas= */ true)));
    ASSERT_OK(conn.Execute(AddReadReplicasCmd("ts_live")));
  }
}

}  // namespace yb
