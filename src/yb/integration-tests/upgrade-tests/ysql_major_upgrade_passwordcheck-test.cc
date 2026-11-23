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

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

class YsqlMajorUpgradePasswordcheckTest : public YsqlMajorUpgradeTestBase {
 public:
  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    // The masters load passwordcheck too, because the upgrade restores the roles on a master.
    for (auto* flags : {&opts.extra_master_flags, &opts.extra_tserver_flags}) {
      flags->push_back("--ysql_pg_conf_csv=shared_preload_libraries=passwordcheck");
    }
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
  }
};

// The upgrade restores each role's password pre-hashed, so passwordcheck must accept a pre-hashed
// SCRAM password. passwordcheck must still refuse a password equal to the user name, before and
// after the upgrade.
TEST_F(YsqlMajorUpgradePasswordcheckTest, ScramPassword) {
  static constexpr auto kPasswordQuery =
      "SELECT rolpassword FROM pg_authid WHERE rolname = 'scram_user'";
  static constexpr auto kNamePasswordStatement = "CREATE USER name_user PASSWORD 'name_user'";
  static constexpr auto kNamePasswordError = "password must not contain user name";

  ASSERT_OK(ExecuteStatements({
      // yugabyte's default password is its name, which passwordcheck refuses (GH#26660).
      "ALTER USER yugabyte PASSWORD 'Upgrade#1234'",
      "SET password_encryption = 'scram-sha-256'",
      "CREATE USER scram_user PASSWORD 'Scram#1234'"}));
  std::string password;
  {
    auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
    password = ASSERT_RESULT(conn.FetchRow<std::string>(kPasswordQuery));
    ASSERT_NOK_STR_CONTAINS(conn.Execute(kNamePasswordStatement), kNamePasswordError);
  }
  ASSERT_TRUE(password.starts_with("SCRAM-SHA-256$")) << password;

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));

  auto conn = ASSERT_RESULT(CreateConnToTs(kAnyTserver));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kPasswordQuery)), password);
  ASSERT_NOK_STR_CONTAINS(conn.Execute(kNamePasswordStatement), kNamePasswordError);
}

}  // namespace yb
