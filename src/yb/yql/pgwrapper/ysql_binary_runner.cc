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

#include "yb/yql/pgwrapper/ysql_binary_runner.h"

#include <sstream>

#include "yb/util/env.h"
#include "yb/util/scope_exit.h"
#include "yb/util/subprocess.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

DEFINE_RUNTIME_bool(ysql_clone_disable_connections, true,
                    "Disable connections to the cloned database during the clone process.");

DEFINE_RUNTIME_bool(ysql_clone_copy_pg_statistics, true,
                    "When true, clone operations include pg_statistic data by passing "
                    "--with-statistics to ysql_dump.");

namespace yb {

// ============================================================================
//  Class YsqlBinaryRunner.
// ============================================================================

Result<std::string> YsqlBinaryRunner::Run(const std::optional<std::vector<std::string>>& args) {
  std::string kHostFlag = "--host=" + pg_host_port.host();
  std::string kPortFlag = "--port=" + std::to_string(pg_host_port.port());
  std::vector<std::string> complete_args = {ysql_binary_path, kHostFlag, kPortFlag};
  if (args) {
    complete_args.insert(complete_args.end(), args->begin(), args->end());
  }
  LOG(INFO) << "Running tool: " << AsString(complete_args);
  std::string output, error;
  auto status = Subprocess::Call(complete_args, &output, &error);
  if (!status.ok()) {
    return status.CloneAndAppend(error);
  }
  return output;
}

// ============================================================================
//  Class YsqlDumpRunner.
// ============================================================================

namespace {

// Every \connect meta-command emitted by ysql_dump starts with this literal prefix at the
// beginning of a line (see appendPsqlMetaConnect / _reconnectToDB in pg_backup_archiver.c). The
// connect-line wrap is a literal-prefix match against this string, no regex required, since
// ysql_dump (with --rename-database) is responsible for emitting the renamed database name with
// correct identifier escaping.
constexpr const char* kConnectLinePrefix = "\\connect ";

std::string MakeAllowConnectionsString(const std::string& new_db, bool allow_connections) {
  return Format(
      "SET yb_non_ddl_txn_for_sys_tables_allowed = true;\n"
      "UPDATE pg_database SET datallowconn = $0 WHERE datname = $1;",
      allow_connections ? "true" : "false", pgwrapper::PqEscapeLiteral(new_db));
}

}  // namespace

Result<std::string> YsqlDumpRunner::DumpSchemaAsOfTime(
    const std::string& db_name,
    const std::string& target_db_name,
    const std::string& target_owner,
    const std::optional<HybridTime>& read_time) {
  std::vector<std::string> args = {
      "--schema-only", "--serializable-deferrable", "--create", "--include-yb-metadata",
      "--dump-role-checks", db_name};
  if (read_time.has_value()) {
    args.push_back("--read-time=" + std::to_string(read_time->GetPhysicalValueMicros()));
  }
  if (FLAGS_ysql_clone_copy_pg_statistics) {
    args.push_back("--with-statistics");
  }
  // Identifier rewriting is performed by ysql_dump itself (via fmtId for OWNER TO clauses and
  // appendPsqlMetaConnect for \connect lines), so the values passed here are raw names. No
  // pre-quoting and no post-processing required for either rename.
  if (!target_db_name.empty()) {
    args.push_back("--rename-database=" + target_db_name);
  }
  if (!target_owner.empty()) {
    args.push_back("--rename-owner=" + target_owner);
  }
  return Run(args);
}

Result<std::string> YsqlDumpRunner::RunAndModifyForClone(
    const std::string& source_db_name, const std::string& target_db_name,
    const std::string& target_owner,
    const std::optional<HybridTime>& read_time) {
  auto dump_output = VERIFY_RESULT(DumpSchemaAsOfTime(
      source_db_name, target_db_name, target_owner, read_time));
  if (FLAGS_ysql_clone_disable_connections) {
    dump_output = WrapConnectWithAllowConnections(dump_output, target_db_name);
  }
  return dump_output;
}

std::string YsqlDumpRunner::WrapConnectWithAllowConnections(
    const std::string& dump_output, const std::string& new_db) {
  const std::string allow = MakeAllowConnectionsString(new_db, true);
  const std::string disallow = MakeAllowConnectionsString(new_db, false);
  const size_t prefix_len = std::strlen(kConnectLinePrefix);

  std::istringstream input_script_stream(dump_output);
  std::stringstream wrapped;
  std::string line;
  while (std::getline(input_script_stream, line)) {
    if (line.compare(0, prefix_len, kConnectLinePrefix) == 0) {
      wrapped << allow << "\n" << line << "\n" << disallow << "\n";
    } else {
      wrapped << line << "\n";
    }
  }
  return wrapped.str();
}

// ============================================================================
//  Class YsqlshRunner.
// ============================================================================

Result<std::string> YsqlshRunner::ExecuteSqlScript(
    const std::string& sql_script, const std::string& tmp_file_prefix,
    const std::string& connect_as_user,
    const std::string& connect_to_database) {
  // Write the dump output to a file in order to execute it using ysqlsh.
  std::unique_ptr<WritableFile> script_file;
  std::string tmp_file_name;
  RETURN_NOT_OK(Env::Default()->NewTempWritableFile(
      WritableFileOptions(), tmp_file_prefix + "_XXXXXX", &tmp_file_name, &script_file));
  RETURN_NOT_OK(script_file->Append(sql_script));
  RETURN_NOT_OK(script_file->Close());
  auto scope_exit = ScopeExit([tmp_file_name] {
    if (Env::Default()->FileExists(tmp_file_name)) {
      WARN_NOT_OK(
          Env::Default()->DeleteFile(tmp_file_name),
          Format("Failed to delete temporary sql script file $0.", tmp_file_name));
    }
  });

  std::vector<std::string> args = {"--file=" + tmp_file_name, "--set", "ON_ERROR_STOP=on"};
  if (!connect_as_user.empty()) {
    args.push_back("--username=" + connect_as_user);
  }
  if (!connect_to_database.empty()) {
    args.push_back("--dbname=" + connect_to_database);
  }
  return this->Run(args);
}

}  // namespace yb
