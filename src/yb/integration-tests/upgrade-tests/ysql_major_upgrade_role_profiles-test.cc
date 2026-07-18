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
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(ysql_enable_profile);
DECLARE_int32(ysql_yb_major_version_upgrade_compatibility);
DECLARE_string(ysql_hba_conf_csv);

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

class YsqlRoleProfileDuringMajorUpgradeTest : public pgwrapper::PgMiniTestBase {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_profile) = true;

    std::string hba_conf_value =
        "host all yugabyte_test 0.0.0.0/0 trust,"
        "host all yugabyte 0.0.0.0/0 trust,"
        "host all postgres 0.0.0.0/0 trust,"
        "host all all 0.0.0.0/0 md5";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_hba_conf_csv) = hba_conf_value;
    TEST_SETUP_SUPER(pgwrapper::PgMiniTestBase);
  }

  // Simulates Major Upgrade by updating ysql_yb_major_version_upgrade_compatibility.
  // Relies on the fact that YBCPgYsqlMajorVersionUpgradeInProgress() function in ybc_pggate.cc uses
  // ysql_yb_major_version_upgrade_compatibility > 0 as a condition to detect major version upgrade.
  Status SetMajorUpgradeCompatibility(int version) {
    LOG(INFO) << "Setting ysql_yb_major_version_upgrade_compatibility to " << version;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_major_version_upgrade_compatibility) = version;
    return RestartCluster();
  }

  Result<pgwrapper::PGConn> ConnectToPGWithTimeout(size_t timeout) {
    auto conn_settings = pgwrapper::PgMiniTestBase::MakeConnSettings("yugabyte");
    conn_settings.connect_timeout = 20;
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  }
};

TEST_F(YsqlRoleProfileDuringMajorUpgradeTest, RoleProfileWritesDisabled) {
  // Additional time to wait to establish a SQL connection after cluster restart.
  constexpr size_t kConnectionAfterRestartTimeout = 10;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE PROFILE profile_5_failed LIMIT FAILED_LOGIN_ATTEMPTS 5"));
  ASSERT_OK(conn.Execute("CREATE ROLE non_reset_user WITH LOGIN PASSWORD 'secret'"));
  ASSERT_OK(conn.Execute("CREATE ROLE non_increment_user WITH LOGIN PASSWORD 'secret'"));
  ASSERT_OK(conn.Execute("ALTER ROLE non_reset_user PROFILE profile_5_failed"));
  ASSERT_OK(conn.Execute("ALTER ROLE non_increment_user PROFILE profile_5_failed"));

  auto attempt_login = [this](const std::string& username, const std::string& password) {
    auto conn_settings = pgwrapper::PgMiniTestBase::MakeConnSettings("yugabyte");
    conn_settings.user = username;
    conn_settings.password = password;
    conn_settings.connect_timeout = 1;
    return pgwrapper::PGConnBuilder(conn_settings).Connect();
  };

  ASSERT_NOK(attempt_login("non_reset_user", "wrong_password"));
  ASSERT_NOK(attempt_login("non_reset_user", "wrong_password"));
  ASSERT_NOK(attempt_login("non_increment_user", "wrong_password"));

  auto fetch_failed_attempts = [this]() -> Result<std::map<std::string, int>> {
    auto conn = VERIFY_RESULT(Connect());
    auto result = VERIFY_RESULT((conn.FetchRows<
        std::string,  // rolname
        int           // rolprffailedloginattempts
        >(
        "SELECT r.rolname, rp.rolprffailedloginattempts "
        "FROM pg_catalog.pg_yb_role_profile rp "
        "JOIN pg_catalog.pg_roles r ON rp.rolprfrole = r.oid "
        "WHERE r.rolname IN ('non_reset_user', 'non_increment_user')")));

    std::map<std::string, int> failed_attempts_map;
    for (const auto& [rolname, failed_attempts] : result) {
      failed_attempts_map[rolname] = failed_attempts;
    }
    return failed_attempts_map;
  };

  auto failed_attempts_map = ASSERT_RESULT(fetch_failed_attempts());
  ASSERT_EQ(failed_attempts_map["non_reset_user"], 2);
  ASSERT_EQ(failed_attempts_map["non_increment_user"], 1);

  // Simulate major version upgrade by setting ysql_yb_major_version_upgrade_compatibility to 11.
  ASSERT_OK(SetMajorUpgradeCompatibility(11));
  // Use a long timeout to ensure the tserver running pg has time to acquire a ysql lease.
  ASSERT_OK(ConnectToPGWithTimeout(kConnectionAfterRestartTimeout * kTimeMultiplier));

  // non_reset_user should successfully log in with the correct password. It should not lead to the
  // failed login counter getting reset to 0.
  // If an attempt was made to reset the counter, this login will fail as we have safeguards in
  // yb-master preventing the writes.
  ASSERT_OK(attempt_login("non_reset_user", "secret"));
  // Attempt one more failed login. It should not lead to an increment in the failed login counter.
  ASSERT_NOK(attempt_login("non_increment_user", "wrong_password"));

  failed_attempts_map = ASSERT_RESULT(fetch_failed_attempts());
  // non_reset_user's counter was not reset to 0 after a successful login.
  ASSERT_EQ(failed_attempts_map["non_reset_user"], 2);
  ASSERT_OK(attempt_login("non_reset_user", "secret"));
  // non_increment_user's counter did not increase after a failed login.
  ASSERT_EQ(failed_attempts_map["non_increment_user"], 1);
  ASSERT_OK(attempt_login("non_increment_user", "secret"));

  // Set major version upgrade as completed now.
  ASSERT_OK(SetMajorUpgradeCompatibility(0));
  // Use a long timeout to ensure the tserver running pg has time to acquire a ysql lease.
  ASSERT_OK(ConnectToPGWithTimeout(kConnectionAfterRestartTimeout * kTimeMultiplier));

  ASSERT_OK(attempt_login("non_reset_user", "secret"));
  ASSERT_NOK(attempt_login("non_increment_user", "wrong_password"));
  failed_attempts_map = ASSERT_RESULT(fetch_failed_attempts());
  // non_reset_user's counter was reset to 0 after a successful login now that version upgrade is
  // over.
  ASSERT_EQ(failed_attempts_map["non_reset_user"], 0);
  // non_increment_user's counter increased after a failed login now that version upgrade is over.
  ASSERT_EQ(failed_attempts_map["non_increment_user"], 2);
}

}  // namespace yb
