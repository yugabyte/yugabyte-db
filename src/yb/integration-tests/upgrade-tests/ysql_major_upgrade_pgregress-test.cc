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

#include "yb/integration-tests/upgrade-tests/ysql_major_upgrade_test_base.h"

namespace yb {

using Pg15UpgradePgRegressTest = YsqlMajorUpgradeTestBase;

// Test yb_profile_schedule
// TODO(fizaa): programatically read the schedule instead of manually copy-pasting lines from it.
// This is not going to stay in sync with the schedule.
TEST_F(Pg15UpgradePgRegressTest, YbProfileSchedule) {
  std::vector<std::string> files = {
    "yb.orig.profile.sql",
    "yb.orig.role_profile.sql",
    "yb.orig.profile_permissions.sql",
  };
  ASSERT_OK(ExecuteStatementsInFiles(files));
  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
}
} // namespace yb
