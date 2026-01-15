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

#include <gtest/gtest.h>

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/external_mini_cluster.h"

#include "yb/tools/admin-test-base.h"

#include "yb/util/status_format.h"
#include "yb/util/subprocess.h"

namespace yb::tools {

class YbAdminAreNodesSafeTest : public AdminTestBase {
 public:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* opts) override {
    opts->num_masters = 3;
    opts->num_tablet_servers = 3;
  }
};

TEST_F(YbAdminAreNodesSafeTest, TestAreNodesSafeToTakeDown) {
  BuildAndStart();
  auto ts0 = cluster_->tablet_server(0)->uuid();
  auto ts1 = cluster_->tablet_server(1)->uuid();
  auto master0 = cluster_->master(0)->uuid();
  auto master1 = cluster_->master(1)->uuid();

  // Should be able to take down 0 or 1 tservers and/or masters.
  // Follower lag can be explicitly specified but does not have to be.
  ASSERT_OK(CallAdmin("are_nodes_safe_to_take_down", ts0, "1000"));
  ASSERT_OK(CallAdmin("are_nodes_safe_to_take_down", master0));
  ASSERT_OK(CallAdmin("are_nodes_safe_to_take_down", ts0 + "," + master0));

  // Should not be able to take down 2 tservers.
  auto status = CallAdmin("are_nodes_safe_to_take_down", ts0 + "," + ts1);
  ASSERT_NOK_STR_CONTAINS(status, "tablet(s) would be under-replicated");

  // Should not be able to take down 2 masters.
  status = CallAdmin("are_nodes_safe_to_take_down", master0 + "," + master1);
  ASSERT_NOK_STR_CONTAINS(status, "tablet(s) would be under-replicated");

  // Should fail with invalid follower lag bound.
  ASSERT_NOK(CallAdmin("are_nodes_safe_to_take_down", ts0, "invalid"));

  status = CallAdmin("are_nodes_safe_to_take_down", "invalid_ts");
  ASSERT_NOK_STR_CONTAINS(status, "Unknown server UUID");

  status = CallAdmin("are_nodes_safe_to_take_down", "invalid_master");
  ASSERT_NOK_STR_CONTAINS(status, "Unknown server UUID");

  // Should fail with no args or empty strings.
  ASSERT_NOK(CallAdmin("are_nodes_safe_to_take_down"));
  ASSERT_NOK(CallAdmin("are_nodes_safe_to_take_down", ""));
  ASSERT_NOK(CallAdmin("are_nodes_safe_to_take_down", ","));
  ASSERT_NOK(CallAdmin("are_nodes_safe_to_take_down", ts0 + ","));
}

}  // namespace yb::tools
