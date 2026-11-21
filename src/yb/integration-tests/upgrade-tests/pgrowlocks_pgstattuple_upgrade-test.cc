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

#include "yb/tools/tools_test_utils.h"

#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

namespace {

constexpr auto kDatabase = "ext_db";
constexpr auto kRejected = "cannot access pages of YugabyteDB relation";

// Each extension's version and the signatures of the functions it owns.
constexpr auto kExtensionFunctions =
    "SELECT e.extname || ' ' || e.extversion || ': ' || "
    "string_agg(p.oid::regprocedure::text, ', ' ORDER BY p.oid::regprocedure::text) "
    "FROM pg_extension e "
    "JOIN pg_depend d ON d.refclassid = 'pg_extension'::regclass AND d.refobjid = e.oid "
    "AND d.classid = 'pg_proc'::regclass AND d.deptype = 'e' "
    "JOIN pg_proc p ON p.oid = d.objid "
    "WHERE e.extname IN ('pgrowlocks', 'pgstattuple') "
    "GROUP BY e.extname, e.extversion ORDER BY e.extname";
constexpr auto kTableData = "SELECT count(*), sum(k), md5(string_agg(v, ',' ORDER BY k)) FROM t";

} // namespace

// pgrowlocks and pgstattuple can no longer be created, but a database that already has them must
// survive an upgrade, and backups of it must restore.
class PgrowlocksPgstattupleUpgradeTest : public UpgradeTestBase {
 public:
  PgrowlocksPgstattupleUpgradeTest() : UpgradeTestBase(kBuild_2026_1_2_0) {}

  void SetUp() override {
    TEST_SETUP_SUPER(UpgradeTestBase);
    if (!UseYbController()) {
      GTEST_SKIP() << "This test does not work without YBC";
    }
  }

 protected:
  Status StartClusterInOldVersion() {
    RETURN_NOT_OK(UpgradeTestBase::StartClusterInOldVersion());
    return cluster_->StartYbControllerServers();
  }

  // On the old version, which still offers both extensions: a table with data, and a view or
  // function over each kind of extension function.
  Status CreateDatabaseWithExtensions() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn.ExecuteFormat("CREATE DATABASE $0", kDatabase));
    conn = VERIFY_RESULT(cluster_->ConnectToDB(kDatabase));
    for (const auto* statement : {
             "CREATE EXTENSION pgrowlocks",
             "CREATE EXTENSION pgstattuple",
             "CREATE TABLE t (k int PRIMARY KEY, v text)",
             "CREATE INDEX t_v_idx ON t (v)",
             "INSERT INTO t SELECT g, 'v' || g FROM generate_series(1, 1000) g",
             "CREATE VIEW t_locks AS SELECT * FROM pgrowlocks('t')",
             "CREATE VIEW t_stats AS SELECT * FROM pgstattuple('t')",
             "CREATE FUNCTION relpages(regclass) RETURNS bigint LANGUAGE sql "
             "AS 'SELECT pg_relpages($1)'"}) {
      RETURN_NOT_OK(conn.Execute(statement));
    }
    extension_functions_ = VERIFY_RESULT(conn.FetchAllAsString(kExtensionFunctions));
    table_data_ = VERIFY_RESULT(conn.FetchAllAsString(kTableData));
    LOG(INFO) << "Extension functions: " << extension_functions_;
    SCHECK_STR_CONTAINS(extension_functions_, "pgrowlocks 1.2: ");
    SCHECK_STR_CONTAINS(extension_functions_, "pgstattuple 1.5: ");
    return Status::OK();
  }

  Status RunBackupCommand(const std::string& backup, const std::string& db_name,
                          const std::string& command) {
    return tools::RunYbControllerCommand(
        cluster_.get(), *tmp_dir_,
        {"--backup_location", tmp_dir_ / backup, "--keyspace", Format("ysql.$0", db_name),
         command});
  }

  // Runs on the current version. The database has both extensions with all their functions, and
  // the same data. The functions reject YB relations, directly or through the view or function
  // that calls them, and keep working on temp tables.
  void CheckDatabase(const std::string& db_name) {
    SCOPED_TRACE(db_name);
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB(db_name));
    ASSERT_EQ(ASSERT_RESULT(conn.FetchAllAsString(kExtensionFunctions)), extension_functions_);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchAllAsString(kTableData)), table_data_);
    // Scripts that create the extensions if missing keep working.
    ASSERT_OK(conn.Execute("CREATE EXTENSION IF NOT EXISTS pgrowlocks"));
    ASSERT_OK(conn.Execute("CREATE EXTENSION IF NOT EXISTS pgstattuple"));

    for (const auto* query : {
             "SELECT * FROM pgrowlocks('t')",
             "SELECT * FROM t_locks",
             "SELECT * FROM pgstattuple('t')",
             "SELECT * FROM pgstattuple('t_v_idx')",
             "SELECT * FROM t_stats",
             "SELECT * FROM pgstattuple_approx('t')",
             "SELECT pg_relpages('t')",
             "SELECT relpages('t')"}) {
      SCOPED_TRACE(query);
      ASSERT_NOK_STR_CONTAINS(conn.Fetch(query), kRejected);
    }

    ASSERT_OK(conn.Execute("CREATE TEMP TABLE tmp (k int PRIMARY KEY, v int)"));
    ASSERT_OK(conn.Execute("INSERT INTO tmp SELECT g, g FROM generate_series(1, 10) g"));
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT tuple_count FROM pgstattuple('tmp')")), 10);
    ASSERT_GT(ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT relpages('tmp')")), 0);
    ASSERT_OK(conn.Execute("BEGIN"));
    ASSERT_OK(conn.Fetch("SELECT * FROM tmp WHERE k <= 2 FOR UPDATE"));
    ASSERT_EQ(
        ASSERT_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM pgrowlocks('tmp')")), 2);
    ASSERT_OK(conn.Execute("COMMIT"));
    ASSERT_OK(conn.Execute("DROP TABLE tmp"));
  }

  tools::TmpDirProvider tmp_dir_;
  std::string extension_functions_;
  std::string table_data_;
};

TEST_F(PgrowlocksPgstattupleUpgradeTest, BackupRestoreAcrossUpgrade) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(CreateDatabaseWithExtensions());
  ASSERT_OK(RunBackupCommand("old_version", kDatabase, "create"));

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes));
  ASSERT_NO_FATALS(CheckDatabase(kDatabase));
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    for (const auto* extension : {"pgrowlocks", "pgstattuple"}) {
      ASSERT_NOK_STR_CONTAINS(
          conn.ExecuteFormat("CREATE EXTENSION $0", extension),
          Format("extension \"$0\" is not available", extension));
    }
  }

  // A restore recreates each extension from its functions, without the control file.
  ASSERT_OK(RunBackupCommand("old_version", "restored_old_version", "restore"));
  ASSERT_NO_FATALS(CheckDatabase("restored_old_version"));

  ASSERT_OK(RunBackupCommand("current_version", kDatabase, "create"));
  ASSERT_OK(RunBackupCommand("current_version", "restored_current_version", "restore"));
  ASSERT_NO_FATALS(CheckDatabase("restored_current_version"));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB(kDatabase));
  ASSERT_OK(conn.Execute("DROP EXTENSION pgrowlocks CASCADE"));
  ASSERT_OK(conn.Execute("DROP EXTENSION pgstattuple CASCADE"));
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(
      "SELECT count(*) FROM pg_class WHERE relname IN ('t_locks', 't_stats')")), 0);
  ASSERT_EQ(ASSERT_RESULT(conn.FetchAllAsString(kTableData)), table_data_);
}

} // namespace yb
