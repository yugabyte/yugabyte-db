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

class YsqlMajorUpgradeTestWithConnMgr : public YsqlMajorUpgradeTestBase {
 public:
  YsqlMajorUpgradeTestWithConnMgr() = default;

  void SetUp() override {
    TEST_SETUP_SUPER(YsqlMajorUpgradeTestBase);

#ifdef __APPLE__
    GTEST_SKIP() << "Connection Manager is not available in mac builds";
#endif
  }

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    opts.enable_ysql_conn_mgr = true;
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
  }
};

// YB_TODO: Enable after previous version has been updated.
TEST_F(YsqlMajorUpgradeTestWithConnMgr, YB_DISABLE_TEST(SimpleTableUpgrade)) {
  ASSERT_OK(TestUpgradeWithSimpleTable());
}

// YB_TODO: Enable after previous version has been updated.
TEST_F(YsqlMajorUpgradeTestWithConnMgr, YB_DISABLE_TEST(SimpleTableRollback)) {
  ASSERT_OK(TestRollbackWithSimpleTable());
}

}  // namespace yb
