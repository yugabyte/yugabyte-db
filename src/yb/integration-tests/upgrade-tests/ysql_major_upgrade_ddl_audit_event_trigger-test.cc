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

namespace {

constexpr auto kDefaultDb = "yugabyte";
constexpr auto kSecondDb = "db_with_ddl_audit";
constexpr auto kAuditSchema = "ddl_audit";
constexpr auto kAuditTable = "log";
constexpr auto kCaptureFunction = "capture_ddl";
constexpr auto kEventTrigger = "ddl_audit_trigger";

}  // namespace

// pg_restore recreates a user event trigger in the enabled state and then keeps replaying DDL, so
// any DDL replayed afterwards fires that trigger. A trigger function that writes to a user table
// then issues DML against tservers still running the old major version, which fails the restore
// and aborts the upgrade.
class YsqlMajorUpgradeDdlAuditEventTriggerTest : public YsqlMajorUpgradeTestBase {
 protected:
  void SetUpDdlAudit(const std::string& db_name) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE SCHEMA $0", kAuditSchema));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE TABLE $0.$1 (tag TEXT, ddl TEXT)", kAuditSchema, kAuditTable));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE OR REPLACE FUNCTION $0.$1() "
        "RETURNS event_trigger LANGUAGE plpgsql AS $$$$ "
        "BEGIN "
        "  INSERT INTO $0.$2(tag, ddl) VALUES (tg_tag, current_query()); "
        "END; "
        "$$$$",
        kAuditSchema, kCaptureFunction, kAuditTable));
    ASSERT_OK(conn.ExecuteFormat(
        "CREATE EVENT TRIGGER $0 ON ddl_command_end EXECUTE FUNCTION $1.$2()",
        kEventTrigger, kAuditSchema, kCaptureFunction));
    // pg_restore emits `ALTER EXTENSION pgaudit ADD EVENT TRIGGER` right after recreating the
    // event triggers, and that is the replayed DDL which fires the user trigger.
    ASSERT_OK(conn.Execute("CREATE EXTENSION pgaudit"));

    ASSERT_NO_FATALS(RunDdlAndExpectAudit(db_name, "t1"));
    // Clear with DML, not TRUNCATE, which would itself fire the trigger.
    ASSERT_OK(conn.ExecuteFormat("DELETE FROM $0.$1", kAuditSchema, kAuditTable));
  }

  // Asserts the trigger is live by running an ordinary DDL and checking that this specific
  // statement was captured.
  void RunDdlAndExpectAudit(const std::string& db_name, const std::string& table_name) {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0 (k INT PRIMARY KEY, v TEXT)", table_name));
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int64_t>(Format(
            "SELECT COUNT(*) FROM $0.$1 WHERE tag = 'CREATE TABLE' AND ddl LIKE '%$2%'",
            kAuditSchema, kAuditTable, table_name))),
        1);
  }

  Result<int64_t> GetAuditRowCount(const std::string& db_name) {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB(db_name));
    return conn.FetchRow<int64_t>(
        Format("SELECT COUNT(*) FROM $0.$1", kAuditSchema, kAuditTable));
  }
};

TEST_F(YsqlMajorUpgradeDdlAuditEventTriggerTest, EnabledDdlAuditTrigger) {
  const std::vector<std::string> db_names = {kDefaultDb, kSecondDb};

  ASSERT_NO_FATALS(SetUpDdlAudit(kDefaultDb));
  ASSERT_OK(ExecuteStatement(Format("CREATE DATABASE $0", kSecondDb)));
  ASSERT_NO_FATALS(SetUpDdlAudit(kSecondDb));

  ASSERT_OK(UpgradeClusterToMixedMode());

  // The restore replays every DDL in the database. Firing the trigger on the replay would both
  // fail the restore and leave the user's audit table full of rows for DDLs that were never run.
  for (const auto& db_name : db_names) {
    ASSERT_EQ(ASSERT_RESULT(GetAuditRowCount(db_name)), 0);
  }

  ASSERT_OK(FinalizeUpgradeFromMixedMode());

  // The suppression is scoped to the restore, so ordinary DDL must be audited again afterwards.
  for (const auto& db_name : db_names) {
    ASSERT_NO_FATALS(RunDdlAndExpectAudit(db_name, "t2"));
  }
}

}  // namespace yb
