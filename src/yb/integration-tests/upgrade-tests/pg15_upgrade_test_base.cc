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
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/util/env_util.h"

namespace yb {

void Pg15UpgradeTestBase::SetUp() {
  UpgradeTestBase::SetUp();
  if (IsTestSkipped()) {
    return;
  }
  CHECK_OK_PREPEND(StartClusterInOldVersion(), "Failed to start cluster in old version");
  CHECK(IsYsqlMajorVersionUpgrade());
  CHECK_GT(cluster_->num_tablet_servers(), 1);
}

Status Pg15UpgradeTestBase::UpgradeClusterToMixedMode() {
  LOG(INFO) << "Upgrading cluster to mixed mode";

  static const MonoDelta no_delay_between_nodes = 0s;
  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInCurrentVersion(no_delay_between_nodes), "Failed to restart masters");

  RETURN_NOT_OK_PREPEND(
      PerformYsqlMajorVersionUpgrade(), "Failed to run ysql major version upgrade");

  LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in current version";
  auto mixed_mode_pg15_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
  RETURN_NOT_OK(RestartTServerInCurrentVersion(
      *mixed_mode_pg15_tserver, /*wait_for_cluster_to_stabilize=*/true));

  return Status::OK();
}

Status Pg15UpgradeTestBase::FinalizeUpgradeFromMixedMode() {
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

  RETURN_NOT_OK(UpgradeTestBase::FinalizeUpgrade());

  return Status::OK();
}

Status Pg15UpgradeTestBase::RollbackUpgradeFromMixedMode() {
  RETURN_NOT_OK_PREPEND(RollbackVolatileAutoFlags(), "Failed to rollback Volatile AutoFlags");

  LOG(INFO) << "Restarting yb-tserver " << kMixedModeTserverPg15 << " in old version";
  auto first_tserver = cluster_->tablet_server(kMixedModeTserverPg15);
  RETURN_NOT_OK(RestartTServerInOldVersion(*first_tserver, /*wait_for_cluster_to_stabilize=*/true));

  RETURN_NOT_OK_PREPEND(RollbackYsqlMajorVersion(), "Failed to run ysql major version rollback");

  static const MonoDelta no_delay_between_nodes = 0s;
  RETURN_NOT_OK_PREPEND(
      RestartAllMastersInOldVersion(no_delay_between_nodes), "Failed to restart masters");

  return Status::OK();
}

Status Pg15UpgradeTestBase::ExecuteStatements(const std::vector<std::string>& sql_statements) {
  auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
  for (const auto& statement : sql_statements) {
    RETURN_NOT_OK(conn.Execute(statement));
  }
  return Status::OK();
}

Status Pg15UpgradeTestBase::ExecuteStatementsInFile(const std::string& file_name) {
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

Status Pg15UpgradeTestBase::ExecuteStatementsInFiles(
    const std::vector<std::string>& file_names) {
  for (const auto& file_name : file_names) {
    RETURN_NOT_OK(ExecuteStatementsInFile(file_name));
  }
  return Status::OK();
}

Result<pgwrapper::PGConn> Pg15UpgradeTestBase::CreateConnToTs(size_t ts_id) {
  return cluster_->ConnectToDB("yugabyte", ts_id);
}

Status Pg15UpgradeTestBase::ExecuteStatement(const std::string& sql_statement) {
  return ExecuteStatements({sql_statement});
}

Result<std::string> Pg15UpgradeTestBase::ExecuteViaYsqlshOnTs(
    const std::string& sql_statement, size_t ts_id, const std::string &db_name) {
  // tserver could have restarted recently. Create a connection which will wait till the pg process
  // is up.
  RETURN_NOT_OK(CreateConnToTs(ts_id));

  auto tserver = cluster_->tablet_server(ts_id);
  std::vector<std::string> args;
  args.push_back(GetPgToolPath("ysqlsh"));
  args.push_back(db_name);
  args.push_back("--host");
  args.push_back(tserver->bind_host());
  args.push_back("--port");
  args.push_back(AsString(tserver->pgsql_rpc_port()));
  args.push_back("-c");
  args.push_back(sql_statement);

  std::string output, error;
  LOG_WITH_FUNC(INFO) << "Executing on " << ts_id << ": " << AsString(args);
  auto status = Subprocess::Call(args, &output, &error);
  if (!status.ok()) {
    return status.CloneAndAppend(error);
  }
  LOG_WITH_FUNC(INFO) << "Command output: " << output;
  return output;
}

Result<std::string> Pg15UpgradeTestBase::ExecuteViaYsqlsh(const std::string& sql_statement,
                                                          const std::string &db_name) {
  auto node_index = RandomUniformInt<size_t>(0, cluster_->num_tablet_servers() - 1);
  return ExecuteViaYsqlshOnTs(sql_statement, node_index, db_name);
}

}  // namespace yb
