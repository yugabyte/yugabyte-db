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

  // Dumps the schema of `db_name` as of `read_time`.
  //
  // When `target_db_name` is non-empty, ysql_dump rewrites every CREATE/ALTER/COMMENT/SECURITY
  // LABEL on DATABASE, GRANT/REVOKE on DATABASE, and \connect line to refer to that name (via the
  // --rename-database flag). Escaping is handled by ysql_dump.
  //
  // When `target_owner` is non-empty, ysql_dump rewrites every "OWNER TO X" clause whose owner
  // equals the source database owner (read from pg_database.datdba) to "OWNER TO target_owner"
  // (via --rename-owner). `target_owner` must be the raw role name as stored in
  // pg_authid.rolname; ysql_dump quotes it via fmtId() at emission time.
  Result<std::string> DumpSchemaAsOfTime(
      const std::string& db_name,
      const std::string& target_db_name = "",
      const std::string& target_owner = "",
      const std::optional<HybridTime>& read_time = std::nullopt);

  // Clone-specific entry point: dump the source DB schema with renames applied by ysql_dump,
  // then wrap every \connect line in the output with ALTER DATABASE ... datallowconn = {true,
  // false} so the script can connect to a target whose datallowconn was set to false at creation.
  // The wrap is gated by the runtime flag ysql_clone_disable_connections.
  Result<std::string> RunAndModifyForClone(
      const std::string& source_db_name, const std::string& target_db_name,
      const std::string& target_owner,
      const std::optional<HybridTime>& read_time = std::nullopt);

  // Insert a "set datallowconn = true" prologue and a matching "set datallowconn = false" epilogue
  // around every \connect line in `dump_output`. Used during DB cloning to allow the in-script
  // \connect to succeed against a freshly-created database whose connections are temporarily
  // disabled. Public so callers (e.g. tests) can invoke the post-processor independently.
  std::string WrapConnectWithAllowConnections(
      const std::string& dump_output, const std::string& new_db);

 private:
  YsqlDumpRunner(std::string tool_path, HostPort pg_host_port)
      : YsqlBinaryRunner(tool_path, pg_host_port) {}
};

class YsqlshRunner : public YsqlBinaryRunner {
 public:
  static Result<YsqlshRunner> GetYsqlshRunner(HostPort pg_host_port) {
    std::string tool_path = VERIFY_RESULT(path_utils::GetPgToolPath("ysqlsh"));
    return YsqlshRunner(tool_path, pg_host_port);
  }

  Result<std::string> ExecuteSqlScript(
      const std::string& sql_script, const std::string& tmp_file_prefix,
      const std::string& connect_as_user = "",
      const std::string& connect_to_database = "");

 private:
  YsqlshRunner(std::string tool_path, HostPort pg_host_port)
      : YsqlBinaryRunner(tool_path, pg_host_port) {}
};

}  // namespace yb
