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

#include <fstream>

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/scope_exit.h"
#include "yb/util/subprocess.h"
#include "yb/util/ysql_binary_runner.h"

DEFINE_RUNTIME_bool(ysql_clone_disable_connections, true,
                    "Disable connections to the cloned database during the clone process.");

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

Result<std::string> YsqlDumpRunner::DumpSchemaAsOfTime(
    const std::string& db_name, const std::optional<HybridTime>& read_time) {
  std::vector<std::string> args = {
      "--schema-only", "--serializable-deferrable", "--create", "--include-yb-metadata", db_name};
  if (read_time.has_value()) {
    std::string read_time_flag =
        "--read-time=" + std::to_string(read_time->GetPhysicalValueMicros());
    args.push_back(read_time_flag);
  }
  return Run(args);
}

Result<std::string> YsqlDumpRunner::RunAndModifyForClone(
    const std::string& source_db_name, const std::string& target_db_name,
    const std::string& source_owner, const std::string& target_owner,
    const std::optional<HybridTime>& read_time) {
  const auto dump_output = VERIFY_RESULT(DumpSchemaAsOfTime(source_db_name, read_time));
  // Pass 1, modify the owner of the DB.
  std::string modified_dump = ModifyDbOwnerInScript(dump_output, source_owner, target_owner);
  // Pass 2, modify the DB name in the script and disallow connection to the DB.
  std::string final_dump =
      ModifyDbNameInScript(modified_dump, target_db_name, FLAGS_ysql_clone_disable_connections);
  return final_dump;
}

namespace {
const boost::regex QUOTED_DATABASE_RE("^(.*)\\s+DATABASE\\s+\"(.+)\"\\s+(.*)$");
const boost::regex UNQUOTED_DATABASE_RE("(^.*)\\s+DATABASE\\s+(\\S+)\\s+(.*)$");
const boost::regex QUOTED_CONNECT_RE("^\\\\connect -reuse-previous=on \"dbname='(.*)'\"$");
const boost::regex UNQUOTED_CONNECT_RE("^\\\\connect\\s+(\\S+)$");
const boost::regex TABLESPACE_RE("^\\s*SET\\s+default_tablespace\\s*=.*$");

std::string MakeDisallowConnectionsString(const std::string& new_db) {
  return Format(
      "SET yb_non_ddl_txn_for_sys_tables_allowed = true;\n"
      "UPDATE pg_database SET datallowconn = false WHERE datname = '$0';", new_db);
}
}  // namespace

std::string YsqlDumpRunner::ModifyDbOwnerInScript(
    const std::string& dump_output, const std::string& source_owner,
    const std::string& target_owner) {
  std::istringstream input_script_stream(dump_output);
  std::string line;
  std::stringstream modified_dump;
  while (std::getline(input_script_stream, line)) {
    modified_dump << ModifyDbOwnerInLine(line, source_owner, target_owner) << std::endl;
  }
  return modified_dump.str();
}

std::string YsqlDumpRunner::ModifyDbOwnerInLine(
    std::string line, const std::string& source_owner, const std::string& target_owner) {
  // Used to set the owner of the created database.
  const boost::regex source_owner_re = boost::regex("OWNER TO " + source_owner);
  const std::string alter_owner = "OWNER TO " + target_owner;
  std::string modified_line = boost::regex_replace(line, source_owner_re, alter_owner);
  return modified_line;
}

std::string YsqlDumpRunner::ModifyDbNameInScript(
    const std::string& dump_output, const std::string& new_db, bool disallow_db_connections) {
  std::istringstream input_script_stream(dump_output);
  std::string line;
  std::stringstream modified_dump;
  while (std::getline(input_script_stream, line)) {
    modified_dump << ModifyDbNameInLine(line, new_db, disallow_db_connections) << std::endl;
  }
  return modified_dump.str();
}

std::string YsqlDumpRunner::ModifyDbNameInLine(
    std::string line, const std::string& new_db, bool disallow_db_connections) {
  std::string disallow_db_connections_stmt = "";
  if (disallow_db_connections) {
    disallow_db_connections_stmt = MakeDisallowConnectionsString(new_db);
  }
  std::vector<std::string> values;
  if (boost::regex_split(std::back_inserter(values), line, QUOTED_DATABASE_RE)) {
    return values[0] + " DATABASE \"" + new_db + "\" " + values[2];
  }
  values.clear();
  if (boost::regex_split(std::back_inserter(values), line, UNQUOTED_DATABASE_RE)) {
    return values[0] + " DATABASE \"" + new_db + "\" " + values[2];
  }
  values.clear();
  if (boost::regex_split(std::back_inserter(values), line, QUOTED_CONNECT_RE)) {
    std::string s = boost::replace_all_copy(new_db, "'", "\\'");
    return "\\connect -reuse-previous=on \"dbname='" + s + "'\"" + "\n" +
           disallow_db_connections_stmt;
  }
  values.clear();
  if (boost::regex_split(std::back_inserter(values), line, UNQUOTED_CONNECT_RE)) {
    std::string s = boost::replace_all_copy(new_db, "'", "\\'");
    return "\\connect -reuse-previous=on \"dbname='" + s + "'\"" + "\n" +
           disallow_db_connections_stmt;
  }
  return line;
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
