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

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(ysql_enable_profile);
DECLARE_int32(ysql_yb_major_version_upgrade_compatibility);
DECLARE_string(ysql_hba_conf_csv);

namespace yb {

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
    pgwrapper::PgMiniTestBase::SetUp();
  }

  size_t NumTabletServers() override { return 1; }

  // Simulates Major Upgrade by updating ysql_yb_major_version_upgrade_compatibility.
  // Relies on the fact that ysql_yb_major_version_upgrade_compatibility > 0 is used as a condition
  // to detect major version upgrade.
  Status SetMajorUpgradeCompatibility(int version) {
    LOG(INFO) << "Setting ysql_yb_major_version_upgrade_compatibility to " << version;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_major_version_upgrade_compatibility) = version;
    RETURN_NOT_OK(RestartCluster());
    // Ensure that restart has completed by making a connection which internally retries till the
    // connection is established. This is important to do here because role profile login attempts
    // in the tests are sensitive to retries as they can bump up the failed login counter more than
    // once. So they must only be attempted once we've ensured that the restart is done which is
    // what we are doing here.
    VERIFY_RESULT(Connect());
    return Status::OK();
  }
};

TEST_F(YsqlRoleProfileDuringMajorUpgradeTest, RoleProfileWritesDisabled) {
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
