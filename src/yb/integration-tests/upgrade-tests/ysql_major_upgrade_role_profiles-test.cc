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

using namespace std::literals;

namespace yb {

class YsqlMajorUpgradeTestWithRoleProfile : public YsqlMajorUpgradeTestBase {
 public:
  YsqlMajorUpgradeTestWithRoleProfile() = default;

  void SetUpOptions(ExternalMiniClusterOptions& opts) override {
    YsqlMajorUpgradeTestBase::SetUpOptions(opts);
    opts.extra_tserver_flags.push_back("--ysql_enable_profile=true");
    opts.extra_master_flags.push_back("--ysql_enable_profile=true");
    std::string hba_conf_value =
        "host all yugabyte_test 0.0.0.0/0 trust,"
        "host all yugabyte 0.0.0.0/0 trust,"
        "host all postgres 0.0.0.0/0 trust,"
        "host all all 0.0.0.0/0 md5";
    AppendCsvFlagValue(opts.extra_tserver_flags, "ysql_hba_conf_csv", hba_conf_value);
  }
};

TEST_F(YsqlMajorUpgradeTestWithRoleProfile, RoleProfile) {
  ASSERT_OK(ExecuteStatements(
    {"CREATE PROFILE profile_3_failed LIMIT FAILED_LOGIN_ATTEMPTS 3",
      "CREATE PROFILE profile_2_failed LIMIT FAILED_LOGIN_ATTEMPTS 2",
      "CREATE ROLE test_user WITH LOGIN PASSWORD 'secret'",
      "CREATE ROLE test_user2 WITH LOGIN PASSWORD 'secret'",
      "CREATE ROLE test_user3 WITH LOGIN PASSWORD 'secret'",
      "ALTER ROLE test_user PROFILE profile_3_failed",
      "ALTER ROLE test_user2 PROFILE profile_3_failed",
      "ALTER ROLE test_user3 PROFILE profile_2_failed"}));

  auto attempt_login = [this](const std::string& username, const std::string& password) {
    const auto conn_settings = pgwrapper::PGConnSettings{
        .host = cluster_->tablet_server(0)->bind_host(),
        .port = cluster_->tablet_server(0)->ysql_port(),
        .dbname = "yugabyte",
        .user = username,
        .password = password,
        .connect_timeout = 1};
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  };
  // 3 failed login attempts for test_user.
  ASSERT_NOK(attempt_login("test_user", "wrong_password"));
  ASSERT_NOK(attempt_login("test_user", "wrong_password"));
  ASSERT_NOK(attempt_login("test_user", "wrong_password"));
  // 1 failed login attempt for test_user3.
  ASSERT_NOK(attempt_login("test_user3", "wrong_password"));

  ASSERT_OK(UpgradeClusterToMixedMode());

  auto check_roleprofiles = [this](const std::optional<size_t> tserver) {
    auto conn = ASSERT_RESULT(CreateConnToTs(tserver));

    auto result = ASSERT_RESULT((conn.FetchRows<
        std::string,  // rolname
        std::string,  // prfname
        char,         // rolprfstatus
        int           // rolprffailedloginattempts
        >(
        "SELECT r.rolname, p.prfname, rp.rolprfstatus, rp.rolprffailedloginattempts "
        "FROM pg_catalog.pg_yb_role_profile rp "
        "JOIN pg_catalog.pg_roles r ON rp.rolprfrole = r.oid "
        "JOIN pg_catalog.pg_yb_profile p ON rp.rolprfprofile = p.oid")));

    // (rolname, prfname) -> (rolprfstatus, rolprffailedloginattempts)
    std::map<std::pair<std::string, std::string>, std::tuple<char, int>> role_profile_map;
    for (const auto& [rolname, prfname, status, failed_attempts] : result) {
      role_profile_map[{rolname, prfname}] = std::make_tuple(status, failed_attempts);
    }

    ASSERT_TRUE(role_profile_map.contains({"test_user", "profile_3_failed"}));
    ASSERT_TRUE(role_profile_map.contains({"test_user2", "profile_3_failed"}));
    ASSERT_TRUE(role_profile_map.contains({"test_user3", "profile_2_failed"}));

    auto user_1_status = role_profile_map[{"test_user", "profile_3_failed"}];
    ASSERT_EQ(user_1_status, std::make_tuple('l', 3));
    auto user_2_status = role_profile_map[{"test_user2", "profile_3_failed"}];
    ASSERT_EQ(user_2_status, std::make_tuple('o', 0));
    auto user_3_status = role_profile_map[{"test_user3", "profile_2_failed"}];
    ASSERT_EQ(user_3_status, std::make_tuple('o', 1));
  };

  ASSERT_NO_FATALS(check_roleprofiles(kMixedModeTserverPg15));
  ASSERT_NO_FATALS(check_roleprofiles(kMixedModeTserverPg11));

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  ASSERT_NO_FATALS(check_roleprofiles(kAnyTserver));

  // After upgrade, test_user should still be locked and not able to login even with the correct
  // password.
  ASSERT_NOK(attempt_login("test_user", "secret"));

  // test_user2 and test_user3 should be able to login.
  ASSERT_OK(attempt_login("test_user2", "secret"));
  ASSERT_OK(attempt_login("test_user3", "secret"));
}

}  // namespace yb
