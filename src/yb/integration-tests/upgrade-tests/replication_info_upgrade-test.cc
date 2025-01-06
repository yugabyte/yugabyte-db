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

class ReplicationInfoUpgradeTest : public UpgradeTestBase {
 public:
  ReplicationInfoUpgradeTest() : UpgradeTestBase(kBuild_2_25_0_0) {}
  virtual ~ReplicationInfoUpgradeTest() = default;
};

TEST_F(ReplicationInfoUpgradeTest, TestMasterRestart) {
  // Test that we can read a replication info protobuf from an old version during upgrades even
  // though the message was moved across files in D40826.
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(RestartAllMastersInCurrentVersion());
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE TABLESPACE ts_geo_us_east_1 WITH (replica_placement='{"
    "\"num_replicas\":3, \"placement_blocks\":["
    "{\"cloud\":\"cloud1\",\"region\":\"datacenter1\",\"zone\":\"rack1\",\"min_num_replicas\":3}"
    "]}')"));
  ASSERT_OK(conn.Execute("CREATE TABLE foo (id INT PRIMARY KEY) TABLESPACE ts_geo_us_east_1"));
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());
  ASSERT_OK(RestartAllTServersInCurrentVersion());
}

} // namespace yb
