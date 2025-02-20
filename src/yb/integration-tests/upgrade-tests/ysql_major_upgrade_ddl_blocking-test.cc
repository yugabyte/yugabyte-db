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

#include "yb/integration-tests/upgrade-tests/pg15_upgrade_test_base.h"

#include "yb/util/backoff_waiter.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

using namespace std::chrono_literals;
using namespace std::placeholders;

#define RUN_DDL_FUNC(func_name) std::bind(&YsqlMajorUpgradeDdlBlockingTest::func_name, this, _1)

YB_DEFINE_ENUM(UpgradeState, (kBeforeUpgrade)(kDuringUpgrade)(kAfterUpgrade));

namespace yb {

constexpr auto kExpectedDdlError =
    "YSQL DDLs, and catalog modifications are not allowed during a major YSQL upgrade";

class YsqlMajorUpgradeDdlBlockingTest : public Pg15UpgradeTestBase {
 public:
  YsqlMajorUpgradeDdlBlockingTest() = default;

  void SetUp() override {
    TEST_SETUP_SUPER(Pg15UpgradeTestBase);

    auto conn = ASSERT_RESULT(CreateConnToTs(std::nullopt));
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE $0(a int)", kCommentTable));
  }

 protected:
  Status SwitchToMixedMode() {
    LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in current version";
    auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
    return RestartTServerInCurrentVersion(
        *mixed_mode_pg15_tserver, /*wait_for_cluster_to_stabilize=*/true);
  }

  Status RunDdlFunctions(std::optional<size_t> node_index, bool error_expected = false) {
    static const auto func_list = std::vector<std::function<Status(std::optional<size_t>)>>{
        RUN_DDL_FUNC(RunTempTableDdls), RUN_DDL_FUNC(RunRegularTableDdls),
        RUN_DDL_FUNC(RunForceSetComment)};

    for (const auto& ddl_func : func_list) {
      auto status = ddl_func(node_index);
      if (!error_expected) {
        RETURN_NOT_OK(status);
      } else {
        SCHECK(
            !status.ok() &&
                status.message().ToString().find(kExpectedDdlError) != std::string::npos,
            IllegalState, "Unexpected status: ", status.ToString());
      }
    }
    return Status::OK();
  }

  Status RunTempTableDdls(std::optional<size_t> node_index) {
    // Run twice to force drop of the temporary table.
    for (int i = 0; i < 2; i++) {
      auto conn = VERIFY_RESULT(CreateConnToTs(node_index));
      // Needed to prevent read restart errors.
      SleepFor(550ms);

      RETURN_NOT_OK(conn.Execute("CREATE TEMP TABLE tmp_tbl (a int PRIMARY KEY)"));

      RETURN_NOT_OK(conn.Execute("INSERT INTO tmp_tbl VALUES (1)"));
      auto count = VERIFY_RESULT(conn.FetchRow<int64_t>("SELECT count(*) FROM tmp_tbl"));
      SCHECK_EQ(count, 1, IllegalState, "Unexpected count");

      RETURN_NOT_OK(conn.Execute("ALTER TABLE tmp_tbl ADD COLUMN b TEXT"));
      RETURN_NOT_OK(conn.Execute("CREATE INDEX temp_idx ON tmp_tbl (b)"));
      RETURN_NOT_OK(conn.Execute("DROP INDEX temp_idx"));

      if (i % 2) {
        RETURN_NOT_OK(conn.Execute("DROP TABLE tmp_tbl"));
      }
    }

    return Status::OK();
  }

  Status RunRegularTableDdls(std::optional<size_t> node_index) {
    const auto check_regular_tbl_stmts = {
        "CREATE TABLE tbl1(a int)", "INSERT INTO tbl1 VALUES (1)",
        "ALTER TABLE tbl1 ADD COLUMN b TEXT", "DROP TABLE tbl1"};
    const auto check_view_stmts = {"CREATE VIEW v1 AS SELECT 1", "DROP VIEW v1"};
    const auto statements_to_run = {check_regular_tbl_stmts, check_view_stmts};

    // No DDLs allowed during the catalog upgrade and monitoring phases.
    const bool expect_error = upgrade_state_ == UpgradeState::kDuringUpgrade;

    auto conn = VERIFY_RESULT(CreateConnToTs(node_index));
    for (const auto& statements : statements_to_run) {
      for (const auto& stmt : statements) {
        auto status = conn.Execute(stmt);
        if (!status.ok()) {
          LOG(INFO) << "Statement: " << stmt << ", Status: " << status;
          if (!expect_error ||
              status.message().ToString().find(kExpectedDdlError) == std::string::npos) {
            return status;
          }
          break;
        }
      }
    }
    return Status::OK();
  }

  static constexpr auto kCommentTable = "comment_tbl1";

  Status RunForceSetComment(std::optional<size_t> node_index) {
    static int count = 0;
    auto conn = VERIFY_RESULT(CreateConnToTs(node_index));

    const auto new_comment = Format("comment $0", count++);

    RETURN_NOT_OK(conn.Execute("SET yb_force_catalog_update_on_next_ddl = true"));
    RETURN_NOT_OK(conn.ExecuteFormat("COMMENT ON TABLE $0 IS '$1'", kCommentTable, new_comment));

    const auto kSelectTableComment = Format(
        "SELECT description from pg_description JOIN pg_class on pg_description.objoid = "
        "pg_class.oid WHERE relname = '$0'",
        kCommentTable);
    const auto selected_comment = VERIFY_RESULT(conn.FetchRow<std::string>(kSelectTableComment));
    SCHECK_EQ(selected_comment, new_comment, IllegalState, "Unexpected comment after DDL ran");

    return Status::OK();
  }

  UpgradeState upgrade_state_ = UpgradeState::kBeforeUpgrade;
};

TEST_F(YsqlMajorUpgradeDdlBlockingTest, TestDdlsDuringUpgrade) {
  upgrade_state_ = UpgradeState::kBeforeUpgrade;
  ASSERT_OK(RunDdlFunctions(std::nullopt));

  upgrade_state_ = UpgradeState::kDuringUpgrade;
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_OK(RunDdlFunctions(std::nullopt));

  ASSERT_OK(StartYsqlMajorCatalogUpgrade());

  ASSERT_OK(RunDdlFunctions(std::nullopt));

  ASSERT_OK(WaitForYsqlMajorCatalogUpgradeToFinish());

  ASSERT_OK(SwitchToMixedMode());

  ASSERT_OK(RunDdlFunctions(kMixedModeTserverPg15));
  ASSERT_OK(RunDdlFunctions(kMixedModeTserverPg11));

  // Finalize upgrade without upgrading all tservers
  ASSERT_OK(FinalizeUpgradeFromMixedMode());
  upgrade_state_ = UpgradeState::kAfterUpgrade;

  ASSERT_OK(RunDdlFunctions(std::nullopt));
}

// Make sure we cannot run DDLs during a failed upgrade.
TEST_F(YsqlMajorUpgradeDdlBlockingTest, TestFailedUpgrade) {
  upgrade_state_ = UpgradeState::kDuringUpgrade;
  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  auto master_leader = cluster_->GetLeaderMaster();
  ASSERT_OK(cluster_->SetFlag(
      master_leader, "TEST_fail_ysql_catalog_upgrade_state_transition_from",
      "PERFORMING_PG_UPGRADE"));

  ASSERT_OK(StartYsqlMajorCatalogUpgrade());

  ASSERT_OK(LoggedWaitFor(
      [this]() -> Result<bool> {
        return VERIFY_RESULT(DumpYsqlCatalogConfig()).find("state: PERFORMING_PG_UPGRADE") !=
               std::string::npos;
      },
      5min, "Waiting for pg_upgrade to start"));

  // The pg_upgrade takes longer than 2s in all builds.
  SleepFor(2s);

  master_leader->Shutdown();
  ASSERT_OK(WaitForClusterToStabilize());

  ASSERT_OK(RunDdlFunctions(kMixedModeTserverPg11));
}

// Make sure we can upgrade even when postgres, and system_platform databases do not exist.
// Make sure prechecks fail if yugabyte database does not exist.
// Make sure we cannot create or drop databases during the upgrade.
TEST_F(YsqlMajorUpgradeDdlBlockingTest, CreateAndDropDBs) {
  {
    auto template1_conn = ASSERT_RESULT(cluster_->ConnectToDB("template1", std::nullopt));
    ASSERT_OK(template1_conn.ExecuteFormat("DROP DATABASE postgres"));
    ASSERT_OK(template1_conn.ExecuteFormat("DROP DATABASE system_platform"));
    ASSERT_OK(template1_conn.ExecuteFormat("DROP DATABASE yugabyte"));

    ASSERT_OK(ValidateUpgradeCompatibilityFailure("Missing system database 'yugabyte'"));

    ASSERT_OK(template1_conn.ExecuteFormat("CREATE DATABASE yugabyte"));
    ASSERT_OK(ValidateUpgradeCompatibility());
  }

  ASSERT_OK(CreateSimpleTable());
  ASSERT_OK(ExecuteStatement("CREATE DATABASE new_db1"));

  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  auto validate_db_ddls_fail = [this] {
    ASSERT_NOK_STR_CONTAINS(
        ExecuteStatement("CREATE DATABASE new_db2"),
        "No new namespaces can be created during a major YSQL upgrade");
    ASSERT_NOK_STR_CONTAINS(ExecuteStatement("DROP DATABASE new_db1"), kExpectedDdlError);
  };

  ASSERT_NO_FATALS(validate_db_ddls_fail());

  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  ASSERT_NO_FATALS(validate_db_ddls_fail());

  ASSERT_OK(RestartAllTServersInCurrentVersion(kNoDelayBetweenNodes));

  ASSERT_NO_FATALS(validate_db_ddls_fail());

  ASSERT_OK(FinalizeUpgrade());

  ASSERT_OK(ExecuteStatement("CREATE DATABASE new_db2"));
  ASSERT_OK(ExecuteStatement("DROP DATABASE new_db1"));

  ASSERT_OK(InsertRowInSimpleTableAndValidate());
}

// Make sure in-flight DDL transactions are killed by the upgrade.
TEST_F(YsqlMajorUpgradeDdlBlockingTest, KillInFlightDDLs) {
  TestThreadHolder thread_holder;

  // Create table with a comment.
  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_OK(conn.Execute("CREATE TABLE tbl1 (a int)"));
  ASSERT_OK(conn.Execute("COMMENT on TABLE tbl1 IS 'Hi'"));
  auto oid = ASSERT_RESULT(
      conn.FetchRow<pgwrapper::PGOid>("SELECT oid FROM pg_class WHERE relname = 'tbl1'"));

  // Create index, but block it before indisvalid is set.
  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "postbackfill"));
  Status index_creation_status;
  std::promise<Status> index_creation_promise;
  auto index_creation_future = index_creation_promise.get_future();
  thread_holder.AddThreadFunctor([this, &index_creation_promise] {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    index_creation_promise.set_value(conn.Execute("CREATE INDEX idx1 ON tbl1 (a)"));
  });

  // Update the comment field in a transaction.
  ASSERT_OK(conn.Execute("SET yb_non_ddl_txn_for_sys_tables_allowed=true"));
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(
      conn.ExecuteFormat("UPDATE pg_description SET description = 'Bye' WHERE objoid = $0", oid));
  const auto check_comment = [&conn, oid](const std::string& expected_comment) -> Status {
    auto comment = VERIFY_RESULT(conn.FetchRow<std::string>(
        Format("SELECT description from pg_description WHERE objoid = $0", oid)));
    SCHECK_EQ(comment, expected_comment, IllegalState, "Unexpected comment");
    return Status::OK();
  };

  // Validate connections before yb-master restart.
  ASSERT_OK(check_comment("Bye"));
  ASSERT_EQ(index_creation_future.wait_for(0s), std::future_status::timeout);

  // Wait for the index creation to be blocked.
  SleepFor(10s);

  ASSERT_OK(RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes));

  // yb-master restart should not affect in-flight DDLs.
  ASSERT_OK(check_comment("Bye"));
  ASSERT_EQ(index_creation_future.wait_for(0s), std::future_status::timeout);

  // Upgrade YSQL catalog.
  ASSERT_OK(PerformYsqlMajorCatalogUpgrade());

  // DDLs should be killed by the upgrade.
  ASSERT_NOK_STR_CONTAINS(conn.Execute("COMMIT"), "current transaction is expired or aborted");

  ASSERT_OK(cluster_->SetFlagOnTServers("ysql_yb_test_block_index_phase", "none"));
  ASSERT_EQ(index_creation_future.wait_for(5min), std::future_status::ready);
  ASSERT_NOK_STR_CONTAINS(index_creation_future.get(), kExpectedDdlError);

  // Validate rollback of the DDL.
  ASSERT_OK(check_comment("Hi"));
}

}  // namespace yb
