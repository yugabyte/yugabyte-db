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

#include <fstream>

#include "yb/util/env.h"
#include "yb/util/subprocess.h"
#include "yb/util/ysql_binary_runner.h"

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
    const std::string& db_name, const HybridTime& restore_time) {
  std::string timestamp_flag =
      "--read-time=" + std::to_string(restore_time.GetPhysicalValueMicros());
  std::vector<std::string> args = {"--schema-only", "--serializable-deferrable", "--create",
                                   timestamp_flag,  "--include-yb-metadata",     db_name};
  return Run(args);
}

Result<std::string> YsqlDumpRunner::RunAndModifyForClone(
    const std::string& source_db_name, const std::string& target_db_name,
    const std::string& source_owner, const std::string& target_owner,
    const HybridTime& restore_time) {
  const auto dump_output = VERIFY_RESULT(DumpSchemaAsOfTime(source_db_name, restore_time));

  // Used to set the owner of the created database.
  const boost::regex source_owner_re = boost::regex("OWNER TO " + source_owner);
  const std::string alter_owner = "OWNER TO " + target_owner;

  std::istringstream input_script_stream(dump_output);
  std::string line;
  std::stringstream modified_dump;
  while (std::getline(input_script_stream, line)) {
    modified_dump << ModifyLine(line, target_db_name, source_owner_re, alter_owner) << std::endl;
  }
  return modified_dump.str();
}

namespace {
const boost::regex QUOTED_DATABASE_RE("^(.*)\\s+DATABASE\\s+\"(.+)\"\\s+(.*)$");
const boost::regex UNQUOTED_DATABASE_RE("(^.*)\\s+DATABASE\\s+(\\S+)\\s+(.*)$");
const boost::regex QUOTED_CONNECT_RE("^\\\\connect -reuse-previous=on \"dbname='(.*)'\"$");
const boost::regex UNQUOTED_CONNECT_RE("^\\\\connect\\s+(\\S+)$");
const boost::regex TABLESPACE_RE("^\\s*SET\\s+default_tablespace\\s*=.*$");
}  // namespace

std::string YsqlDumpRunner::ModifyLine(
    const std::string& line, const std::string& new_db, const boost::regex& owner_regex,
    const std::string& alter_owner) {
  std::string modified_line = boost::regex_replace(line, owner_regex, alter_owner);
  std::vector<std::string> values;
  if (boost::regex_split(std::back_inserter(values), modified_line, QUOTED_DATABASE_RE)) {
    return values[0] + " DATABASE \"" + new_db + "\" " + values[2];
  }
  values.clear();
  if (boost::regex_split(std::back_inserter(values), modified_line, UNQUOTED_DATABASE_RE)) {
    return values[0] + " DATABASE \"" + new_db + "\" " + values[2];
  }
  values.clear();
  if (boost::regex_split(std::back_inserter(values), modified_line, QUOTED_CONNECT_RE)) {
    std::string s = boost::replace_all_copy(new_db, "'", "\\'");
    return "\\connect -reuse-previous=on \"dbname='" + s + "'\"";
  }
  values.clear();
  if (boost::regex_split(std::back_inserter(values), modified_line, UNQUOTED_CONNECT_RE)) {
    std::string s = boost::replace_all_copy(new_db, "'", "\\'");
    return "\\connect -reuse-previous=on \"dbname='" + s + "'\"";
  }
  return modified_line;
}

// ============================================================================
//  Class YsqlshRunner.
// ============================================================================

Result<std::string> YsqlshRunner::ExecuteSqlScript(const std::string& sql_script_path) {
  std::vector<std::string> args = {"--file=" + sql_script_path};
  return VERIFY_RESULT(this->Run(args));
}

}  // namespace yb
