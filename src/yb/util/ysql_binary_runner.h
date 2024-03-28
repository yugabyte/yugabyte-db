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

#pragma once

#include <boost/algorithm/string/replace.hpp>
#include <boost/regex.hpp>

#include "yb/common/hybrid_time.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"

namespace yb {

// Utility class to run a ysql binary such as ysql_dump and ysqlsh that connects to postgres.
class YsqlBinaryRunner {
 public:
  YsqlBinaryRunner(const std::string& ysql_binary_path, HostPort pg_host_port)
      : ysql_binary_path(ysql_binary_path), pg_host_port(pg_host_port) {}
  // Runs the ysql binary with the additional arguments included in args. Execution output is
  // returned as string.
  Result<std::string> Run(const std::optional<std::vector<std::string>>& args);

 private:
  std::string ysql_binary_path;
  HostPort pg_host_port;
};

class YsqlDumpRunner : public YsqlBinaryRunner {
 public:
  static Result<YsqlDumpRunner> GetYsqlDumpRunner(HostPort pg_host_port) {
    std::string tool_path = VERIFY_RESULT(path_utils::GetPgToolPath("ysql_dump"));
    return YsqlDumpRunner(tool_path, pg_host_port);
  }
  Result<std::string> DumpSchemaAsOfTime(
      const std::string& db_name, const HybridTime& restore_time);
  // Change the database name to new_db_name inside sql_dump_script. The change happens in place.
  std::string ModifyDBNameInScript(
      const std::string& sql_dump_script, const std::string& new_db_name,
      bool unset_tablespaces = true);

 private:
  YsqlDumpRunner(std::string tool_path, HostPort pg_host_port)
      : YsqlBinaryRunner(tool_path, pg_host_port) {}
  // Utility methods used for replacing the DB name in the dump output (Used for cloning)
  std::string Replace(std::string& src, const std::string& new_db, bool unset_tablespaces);
};

class YsqlshRunner : public YsqlBinaryRunner {
 public:
  static Result<YsqlshRunner> GetYsqlshRunner(HostPort pg_host_port) {
    std::string tool_path = VERIFY_RESULT(path_utils::GetPgToolPath("ysqlsh"));
    return YsqlshRunner(tool_path, pg_host_port);
  }

  Result<std::string> ExecuteSqlScript(const std::string& sql_script_path);

 private:
  YsqlshRunner(std::string tool_path, HostPort pg_host_port)
      : YsqlBinaryRunner(tool_path, pg_host_port) {}
};

}  // namespace yb
