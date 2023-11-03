// Copyright (c) YugaByte, Inc.
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

#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

#include "yb/tserver/tserver_service.pb.h"

#include "yb/util/env_util.h"
#include "yb/util/os-util.h"
#include "yb/util/path_util.h"
#include "yb/util/size_literals.h"
#include "yb/util/string_trim.h"
#include "yb/util/tostring.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

using std::unique_ptr;
using std::string;
using std::vector;

using yb::util::TrimStr;
using yb::util::TrimTrailingWhitespaceFromEveryLine;
using yb::util::LeftShiftTextBlock;

using namespace std::literals;

DECLARE_int32(replication_factor);

namespace yb {
namespace pgwrapper {

void PgWrapperTestBase::SetUp() {
  YBMiniClusterTestBase::SetUp();

  ExternalMiniClusterOptions opts;
  opts.enable_ysql = true;

  // With ysql_num_shards_per_tserver=1 and 3 tservers we'll be creating 3 tablets per table, which
  // is enough for most tests.
  opts.extra_tserver_flags.emplace_back("--ysql_num_shards_per_tserver=1");

  // Collect old records very aggressively to catch bugs with old readpoints.
  opts.extra_tserver_flags.emplace_back("--timestamp_history_retention_interval_sec=0");

  opts.extra_master_flags.emplace_back("--hide_pg_catalog_table_creation_logs");

  opts.num_masters = GetNumMasters();

  opts.num_tablet_servers = GetNumTabletServers();

  opts.extra_master_flags.emplace_back("--client_read_write_timeout_ms=120000");
  opts.extra_master_flags.emplace_back(Format("--memory_limit_hard_bytes=$0", 2_GB));
  opts.extra_master_flags.emplace_back(Format("--replication_factor=$0", FLAGS_replication_factor));

  UpdateMiniClusterOptions(&opts);

  cluster_.reset(new ExternalMiniCluster(opts));
  ASSERT_OK(cluster_->Start());

  if (cluster_->num_tablet_servers() > 0) {
    pg_ts = cluster_->tablet_server(0);
  }

  // TODO: fix cluster verification for PostgreSQL tables.
  DontVerifyClusterBeforeNextTearDown();
}

Result<TabletId> PgWrapperTestBase::GetSingleTabletId(const TableName& table_name) {
  TabletId tablet_id_to_split;
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    const auto ts = cluster_->tablet_server(i);
    const auto tablets = VERIFY_RESULT(cluster_->GetTablets(ts));
    for (const auto& tablet : tablets) {
      if (tablet.table_name() == table_name) {
        return tablet.tablet_id();
      }
    }
  }
  return STATUS(NotFound, Format("No tablet found for table $0.", table_name));
}

Result<string> PgWrapperTestBase::RunYbAdminCommand(const string& cmd) {
  const auto yb_admin = "yb-admin"s;
  auto command = GetToolPath(yb_admin) +
    " --master_addresses " + cluster_->GetMasterAddresses() +
    " " + cmd;
  LOG(INFO) << "Running " << command;
  string output;
  if (RunShellProcess(command, &output)) {
    return output;
  }
  return STATUS_FORMAT(RuntimeError, "Failed to execute $0 command", yb_admin);
}

namespace {

string TrimSqlOutput(string output) {
  return TrimStr(TrimTrailingWhitespaceFromEveryLine(LeftShiftTextBlock(output)));
}

} // namespace

Result<std::string> PgCommandTestBase::RunPsqlCommand(
    const std::string& statement, TuplesOnly tuples_only) {
  string tmp_dir;
  RETURN_NOT_OK(Env::Default()->GetTestDirectory(&tmp_dir));

  unique_ptr<WritableFile> tmp_file;
  string tmp_file_name;
  RETURN_NOT_OK(Env::Default()->NewTempWritableFile(
      WritableFileOptions(), tmp_dir + "/psql_statementXXXXXX", &tmp_file_name, &tmp_file));
  RETURN_NOT_OK(tmp_file->Append(statement));
  RETURN_NOT_OK(tmp_file->Close());

  vector<string> argv{
      GetPostgresInstallRoot() + "/bin/ysqlsh",
      "-h", pg_ts->bind_host(),
      "-p", std::to_string(pg_ts->pgsql_rpc_port()),
      "-U", "yugabyte",
      "-f", tmp_file_name
  };

  if (!db_name_.empty()) {
    argv.push_back("-d");
    argv.push_back(db_name_);
  }

  if (encrypt_connection_) {
    argv.push_back(Format(
        "sslmode=require sslcert=$0/ysql.crt sslrootcert=$0/ca.crt sslkey=$0/ysql.key",
        GetCertsDir()));
  }

  if (tuples_only) {
    argv.push_back("-t");
  }

  LOG(INFO) << "Run tool: " << yb::ToString(argv);
  Subprocess proc(argv.front(), argv);
  if (use_auth_) {
    proc.SetEnv("PGPASSWORD", "yugabyte");
  }

  string psql_stdout;
  LOG(INFO) << "Executing statement: " << statement;
  RETURN_NOT_OK(proc.Call(&psql_stdout));
  LOG(INFO) << "Output from statement {{ " << statement << " }}:\n"
            << psql_stdout;

  return TrimSqlOutput(psql_stdout);
}

void PgCommandTestBase::RunPsqlCommand(
    const string& statement, const string& expected_output, bool tuples_only) {
  string psql_stdout = ASSERT_RESULT(
      RunPsqlCommand(statement, tuples_only ? TuplesOnly::kTrue : TuplesOnly::kFalse));
  ASSERT_EQ(TrimSqlOutput(expected_output), TrimSqlOutput(psql_stdout));
}

void PgCommandTestBase::UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) {
  PgWrapperTestBase::UpdateMiniClusterOptions(options);
  if (encrypt_connection_) {
    const vector<string> common_flags{
        "--use_node_to_node_encryption=true", "--certs_dir=" + GetCertsDir()};
    for (auto flags : {&options->extra_master_flags, &options->extra_tserver_flags}) {
      flags->insert(flags->begin(), common_flags.begin(), common_flags.end());
    }
    options->extra_tserver_flags.push_back("--use_client_to_server_encryption=true");
    options->extra_tserver_flags.push_back("--allow_insecure_connections=false");
    options->use_even_ips = true;
  }

  if (use_auth_) {
    options->extra_tserver_flags.push_back("--ysql_enable_auth");
  }
}

} // namespace pgwrapper
} // namespace yb
