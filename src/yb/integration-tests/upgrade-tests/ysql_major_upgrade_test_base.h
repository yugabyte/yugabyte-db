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

#pragma once

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

namespace yb {

class YsqlMajorUpgradeTestBase : public UpgradeTestBase {
 public:
  YsqlMajorUpgradeTestBase() : UpgradeTestBase(kBuild_2024_2_4_0) {}
  virtual ~YsqlMajorUpgradeTestBase() override = default;

  void SetUp() override;
  void SetUpOptions(ExternalMiniClusterOptions& opts) override;

 protected:
  // UpgradeTestBase provides helper functions UpgradeClusterToCurrentVersion, FinalizeUpgrade,
  // and RollbackClusterToOldVersion. These restart all tservers to the current version. The below
  // helper functions only upgrade one tserver (id=kMixedModeTserverPg15) to the current version and
  // keep the other tservers in the old version, helping us perform validations while in mixed mode.

  static constexpr size_t kMixedModeTserverPg15 = 0;
  static constexpr size_t kMixedModeTserverPg11 = 1;
  static constexpr std::optional<size_t> kAnyTserver = std::nullopt;
  static constexpr auto kPgUpgradeFailedError = "pg_upgrade' terminated with non-zero exit status";

  // Run pg_upgrade --check
  virtual Status ValidateUpgradeCompatibility(const std::string& user_name = "yugabyte");
  Status ValidateUpgradeCompatibilityFailure(
      const std::string& expected_error, const std::string& user_name = "yugabyte");
  Status ValidateUpgradeCompatibilityFailure(
      const std::vector<std::string>& expected_errors, const std::string& user_name = "yugabyte");
  // Same as above but accepts {error1,error2}

  // Restarts all masters in the current version, runs ysql major version upgrade, and restarts
  // tserver kMixedModeTserverPg15 in the current version. Other tservers are kept in the pg11
  // version.
  Status UpgradeClusterToMixedMode();

  // Restarts all other tservers in the current version. FinalizeUpgrade is not called
  Status UpgradeAllTserversFromMixedMode();

  // Restarts all other tservers in the current version, and finalizes the upgrade.
  Status FinalizeUpgradeFromMixedMode();

  // Restarts tserver kMixedModeTserverPg15 in the old version, rolls backs the ysql major version
  // upgrade, and restarts all masters in the old version.
  Status RollbackUpgradeFromMixedMode();

  // Connects to a random tserver and executes ysql statements.
  Status ExecuteStatements(const std::vector<std::string>& sql_statements);
  Status ExecuteStatement(const std::string& sql_statement);
  Status ExecuteStatementsInFile(const std::string& file_name);
  Status ExecuteStatementsInFiles(const std::vector<std::string>& file_names);

  Result<pgwrapper::PGConn> CreateConnToTs(std::optional<size_t> ts_id,
    const std::string& user = "postgres");

  // Run a ysql statement via ysqlsh.
  Result<std::string> ExecuteViaYsqlsh(const std::string& sql_statement,
                                       std::optional<size_t> ts_id = std::nullopt,
                                       const std::string &db_name = "yugabyte");

  Status CreateSimpleTable();
  Status InsertRowInSimpleTableAndValidate(const std::optional<size_t> tserver = kAnyTserver);

  Status TestUpgradeWithSimpleTable();
  Status TestRollbackWithSimpleTable();

  Result<std::string> DumpYsqlCatalogConfig();

  Status WaitForState(master::YsqlMajorCatalogUpgradeInfoPB::State state);

  Result<std::string> ReadUpgradeCompatibilityGuc();

  constexpr static auto kSimpleTableName = "simple_tbl";
  uint32 simple_tbl_row_count_ = 0;
};

}  // namespace yb
