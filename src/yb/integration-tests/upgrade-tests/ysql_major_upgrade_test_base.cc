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

#include <chrono>

#include "yb/master/master_admin.pb.h"
#include "yb/master/master_admin.proxy.h"
#include "yb/master/master_defaults.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/logging_test_util.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/util/env_util.h"

using namespace std::chrono_literals;

namespace yb {

void YsqlMajorUpgradeTestBase::SetUp() {
  TEST_SETUP_SUPER(UpgradeTestBase);

  CHECK_OK_PREPEND(StartClusterInOldVersion(), "Failed to start cluster in old version");
  CHECK(IsYsqlMajorVersionUpgrade());
  CHECK_GT(cluster_->num_tablet_servers(), 1);
  // Bump catalog version to 10000 so that tservers can detect catalog version mismatch errors.
  ASSERT_OK(ExecuteStatements({
      "SET yb_non_ddl_txn_for_sys_tables_allowed TO on",
      "UPDATE pg_yb_catalog_version SET current_version = 10000, last_breaking_version = 10000",
      "RESET yb_non_ddl_txn_for_sys_tables_allowed"}));
}

void YsqlMajorUpgradeTestBase::SetUpOptions(ExternalMiniClusterOptions& opts) {
  UpgradeTestBase::SetUpOptions(opts);

  // Disable table locks to avoid issues during upgrade tests.
  // TODO(#28746): This should not be required once we switch to making table
  // locks an autoflag.
  AddUnDefOkAndSetFlag(
      opts.extra_master_flags, "enable_object_locking_for_table_locks", "false");
  AddUnDefOkAndSetFlag(
      opts.extra_tserver_flags, "enable_object_locking_for_table_locks", "false");
}

Status YsqlMajorUpgradeTestBase::ValidateUpgradeCompatibility(const std::string& user_name) {
  const auto tserver = cluster_->tablet_server(0);
  const auto data_path = JoinPathSegments(tserver->GetDataDirs().front(), "../../pg_data");

  const std::vector<std::string> args = {
    GetPgToolPath("pg_upgrade"),
    "--old-datadir", data_path,
    "--old-host", tserver->bind_host(),
    "--old-port", AsString(tserver->pgsql_rpc_port()),
    "--username", user_name,
    "--check"
  };

  LOG(INFO) << "Running " << AsString(args);

  return Subprocess::Call(args, /*log_stdout_and_stderr=*/true);
}

Status YsqlMajorUpgradeTestBase::ValidateUpgradeCompatibilityFailure(
    const std::vector<std::string>& expected_errors, const std::string& user_name) {
  std::vector<std::unique_ptr<StringWaiterLogSink>> log_waiters;
  for (const auto& expected_error : expected_errors) {
    log_waiters.emplace_back(std::make_unique<StringWaiterLogSink>(expected_error));
  }
  auto status = ValidateUpgradeCompatibility(user_name);
  SCHECK(
      !status.ok(), IllegalState,
      Format("Expected pg_upgrade to fail with error(s): $0", ToString(expected_errors)));
  SCHECK(
      status.message().Contains(kPgUpgradeFailedError), IllegalState, "Unexpected status: $0",
      status);

  for (size_t i = 0; i < expected_errors.size(); ++i) {
    SCHECK_FORMAT(
        log_waiters[i]->IsEventOccurred(), IllegalState,
        "Expected pg_upgrade to fail with error: $0", expected_errors[i]);
  }

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::ValidateUpgradeCompatibilityFailure(
    const std::string& expected_error, const std::string& user_name) {
  return ValidateUpgradeCompatibilityFailure(std::vector<std::string>{expected_error}, user_name);
}

Status YsqlMajorUpgradeTestBase::UpgradeClusterToMixedMode() {
  RETURN_NOT_OK(ValidateUpgradeCompatibility());

  LOG(INFO) << "Upgrading cluster to mixed mode";

  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInCurrentVersion(kNoDelayBetweenNodes), "Failed to restart masters");

  RETURN_NOT_OK_PREPEND(
      PerformYsqlMajorCatalogUpgrade(), "Failed to run ysql major catalog upgrade");

  LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in current version";
  auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
  RETURN_NOT_OK(RestartTServerInCurrentVersion(
      *mixed_mode_pg15_tserver, /*wait_for_cluster_to_stabilize=*/true));

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::UpgradeAllTserversFromMixedMode() {
  LOG(INFO) << "Restarting all other yb-tservers in current version";

  auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
  for (auto* tserver : cluster_->tserver_daemons()) {
    if (tserver == mixed_mode_pg15_tserver) {
      continue;
    }
    RETURN_NOT_OK(
        RestartTServerInCurrentVersion(*tserver, /*wait_for_cluster_to_stabilize=*/false));
  }

  RETURN_NOT_OK(WaitForClusterToStabilize());

  RETURN_NOT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::FinalizeUpgradeFromMixedMode() {
  RETURN_NOT_OK(UpgradeAllTserversFromMixedMode());

  RETURN_NOT_OK(UpgradeTestBase::FinalizeUpgrade());

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::RollbackUpgradeFromMixedMode() {
  RETURN_NOT_OK_PREPEND(RollbackVolatileAutoFlags(), "Failed to rollback Volatile AutoFlags");

  LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in old version";
  auto first_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
  RETURN_NOT_OK(RestartTServerInOldVersion(*first_tserver, /*wait_for_cluster_to_stabilize=*/true));

  RETURN_NOT_OK_PREPEND(
      RollbackYsqlMajorCatalogVersion(), "Failed to run ysql major catalog rollback");

  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInOldVersion(kNoDelayBetweenNodes), "Failed to restart masters");

  RETURN_NOT_OK(SetMajorUpgradeCompatibilityIfNeeded(MajorUpgradeCompatibilityType::kNone));

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::ExecuteStatements(const std::vector<std::string>& sql_statements) {
  auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
  for (const auto& statement : sql_statements) {
    RETURN_NOT_OK(conn.Execute(statement));
  }
  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::ExecuteStatementsInFile(const std::string& file_name) {
  const auto sub_dir = "postgres_build/src/test/regress/sql";
  const auto test_sql_dir = JoinPathSegments(env_util::GetRootDir(sub_dir), sub_dir);
  const auto file_path = JoinPathSegments(test_sql_dir, file_name);
  RETURN_NOT_OK(CreateConnToTs(0));
  auto tserver = cluster_->tablet_server(0);
  std::vector<std::string> args;
  args.push_back(GetPgToolPath("ysqlsh"));
  args.push_back("--host");
  args.push_back(tserver->bind_host());
  args.push_back("--port");
  args.push_back(AsString(tserver->pgsql_rpc_port()));
  args.push_back("-f");
  args.push_back(file_path);
  args.push_back("-v");
  // YSQL regress test files often include statements that are expected to error out, so don't stop
  // execution on error.
  args.push_back("ON_ERROR_STOP=0");
  std::string output, error;
  auto status = Subprocess::Call(args, &output, &error);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to execute statements in file " << file_name << ": "
        << "output - " << output << ", error - " << error;
    return status.CloneAndAppend(error);
  }
  LOG(INFO) << "Finished executing statements in file " << file_name;
  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::ExecuteStatementsInFiles(
    const std::vector<std::string>& file_names) {
  for (const auto& file_name : file_names) {
    RETURN_NOT_OK(ExecuteStatementsInFile(file_name));
  }
  return Status::OK();
}

Result<pgwrapper::PGConn> YsqlMajorUpgradeTestBase::CreateConnToTs(std::optional<size_t> ts_id,
    const std::string& user) {
  return cluster_->ConnectToDB(/* database= */ "yugabyte", ts_id,
      /*simple_query_protocol=*/ false, user);
}

Status YsqlMajorUpgradeTestBase::ExecuteStatement(const std::string& sql_statement) {
  return ExecuteStatements({sql_statement});
}

Result<std::string> YsqlMajorUpgradeTestBase::ExecuteViaYsqlsh(
    const std::string& sql_statement, std::optional<size_t> ts_id, const std::string& db_name) {
  // tserver could have restarted recently. Create a connection which will wait till the pg process
  // is up.

  auto not_null_ts_id = ts_id.value_or(
      RandomUniformInt<size_t>(0, cluster_->num_tablet_servers() - 1));

  RETURN_NOT_OK(CreateConnToTs(not_null_ts_id));

  auto tserver = cluster_->tablet_server(not_null_ts_id);
  std::vector<std::string> args;
  args.push_back(GetPgToolPath("ysqlsh"));
  args.push_back(db_name);
  args.push_back("--host");
  args.push_back(tserver->bind_host());
  args.push_back("--port");
  args.push_back(AsString(tserver->ysql_port()));
  args.push_back("-c");
  args.push_back(sql_statement);

  std::string output, error;
  LOG_WITH_FUNC(INFO) << "Executing on " << not_null_ts_id << ": " << AsString(args);
  auto status = Subprocess::Call(args, &output, &error);
  if (!status.ok()) {
    return status.CloneAndAppend(error);
  }
  LOG_WITH_FUNC(INFO) << "Command output: " << output;
  return output;
}

Status YsqlMajorUpgradeTestBase::CreateSimpleTable() {
  simple_tbl_row_count_ = 100;
  return ExecuteStatements(
      {Format("CREATE TABLE $0 (a INT) SPLIT INTO 3 TABLETS", kSimpleTableName),
       Format(
           "INSERT INTO $0 VALUES(generate_series(1, $1))", kSimpleTableName,
           simple_tbl_row_count_)});
}

Status YsqlMajorUpgradeTestBase::InsertRowInSimpleTableAndValidate(
    const std::optional<size_t> tserver) {
  auto conn = VERIFY_RESULT(cluster_->ConnectToDB("yugabyte", tserver));
  RETURN_NOT_OK(conn.Execute(
      Format("INSERT INTO $0 VALUES ($1)", kSimpleTableName, ++simple_tbl_row_count_)));

  auto actual_row_count =
      VERIFY_RESULT(conn.FetchRow<int64_t>(Format("SELECT COUNT(*) FROM $0", kSimpleTableName)));
  SCHECK_EQ(actual_row_count, simple_tbl_row_count_, IllegalState, "Unexpected row count");

  auto sum =
      VERIFY_RESULT(conn.FetchRow<int64_t>(Format("SELECT SUM(a) FROM $0", kSimpleTableName)));
  SCHECK_EQ(
      sum, (simple_tbl_row_count_ * (simple_tbl_row_count_ + 1)) / 2, IllegalState,
      "Unexpected sum of column a");

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::TestUpgradeWithSimpleTable() {
  // Create a table with 3 tablets and kRowCount rows so that each tablet has at least a few rows.
  RETURN_NOT_OK(CreateSimpleTable());

  RETURN_NOT_OK(UpgradeClusterToMixedMode());

  RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg15));
  RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg11));

  RETURN_NOT_OK(FinalizeUpgradeFromMixedMode());

  // Verify row count from a random tserver.
  RETURN_NOT_OK(InsertRowInSimpleTableAndValidate());

  return Status::OK();
}

Status YsqlMajorUpgradeTestBase::TestRollbackWithSimpleTable() {
  // Create an extra DB with a table to make sure the rollback with multiple DBs work.
  {
    RETURN_NOT_OK(ExecuteStatement("CREATE DATABASE db1"));
    auto db1_conn = VERIFY_RESULT(cluster_->ConnectToDB("db1"));
    RETURN_NOT_OK(db1_conn.Execute("CREATE TABLE t (a INT)"));
  }

  RETURN_NOT_OK(CreateSimpleTable());

  RETURN_NOT_OK(UpgradeClusterToMixedMode());

  RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg15));
  RETURN_NOT_OK(InsertRowInSimpleTableAndValidate(kMixedModeTserverPg11));

  RETURN_NOT_OK(RollbackUpgradeFromMixedMode());

  // Verify row count from a random tserver.
  RETURN_NOT_OK(InsertRowInSimpleTableAndValidate());

  return Status::OK();
}

Result<std::string> YsqlMajorUpgradeTestBase::DumpYsqlCatalogConfig() {
  master::DumpSysCatalogEntriesRequestPB req;
  master::DumpSysCatalogEntriesResponsePB resp;
  req.set_entry_type(master::SysRowEntryType::SYS_CONFIG);
  req.set_entity_id_filter(master::kYsqlCatalogConfigType);

  rpc::RpcController rpc;
  rpc.set_timeout(60s);

  auto master_admin_proxy =
      master::MasterAdminProxy(cluster_->GetLeaderMasterProxy<master::MasterAdminProxy>());
  RETURN_NOT_OK(master_admin_proxy.DumpSysCatalogEntries(req, &resp, &rpc));

  LOG(INFO) << "Dumped ysql catalog config: " << resp.DebugString();

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  SCHECK_EQ(resp.entries_size(), 1, IllegalState, "Expected exactly one entry");

  return resp.entries(0).pb_debug_string();
}

Status YsqlMajorUpgradeTestBase::WaitForState(master::YsqlMajorCatalogUpgradeInfoPB::State state) {
  auto state_str = master::YsqlMajorCatalogUpgradeInfoPB::State_Name(state);
  return LoggedWaitFor(
      [&]() -> Result<bool> {
        return VERIFY_RESULT(DumpYsqlCatalogConfig()).find(Format("state: $0", state_str)) !=
               std::string::npos;
      },
      5min, "Waiting for upgrade to reach state " + state_str);
}

Result<std::string> YsqlMajorUpgradeTestBase::ReadUpgradeCompatibilityGuc() {
  auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
  return conn.FetchRow<std::string>("SHOW yb_major_version_upgrade_compatibility");
}

}  // namespace yb
