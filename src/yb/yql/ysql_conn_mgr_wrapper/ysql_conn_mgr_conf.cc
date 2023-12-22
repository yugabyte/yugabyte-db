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

// TODO(janand): Block the client from using the user same as that of the control connection.

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_wrapper.h"

#include <fstream>
#include <regex>

#include <boost/algorithm/string.hpp>

#include "yb/util/atomic.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/string_util.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(logtostderr);
DECLARE_uint32(ysql_conn_mgr_port);
DECLARE_uint32(ysql_conn_mgr_max_client_connections);
DECLARE_uint32(ysql_conn_mgr_max_conns_per_db);
DECLARE_uint32(ysql_conn_mgr_idle_time);
DECLARE_string(ysql_conn_mgr_internal_conn_db);
DECLARE_string(pgsql_proxy_bind_address);
DECLARE_string(rpc_bind_addresses);
DECLARE_uint32(ysql_conn_mgr_num_workers);
DECLARE_uint32(ysql_conn_mgr_stats_interval);
DECLARE_uint32(ysql_conn_mgr_min_conns_per_db);
DECLARE_int32(ysql_max_connections);

namespace yb {
namespace ysql_conn_mgr_wrapper {

std::string BoolToString(bool value) {
  return value == true ? "yes" : "no";
}

std::string PutConfigValue(
    const std::string& line, const std::map<std::string, std::string>& config) {
  std::regex placeholder_regex(R"(\{%(\w+)%\})");
  std::string modified_line = line;

  std::sregex_iterator placeholder_it(line.begin(), line.end(), placeholder_regex);
  std::sregex_iterator end_it;

  if (placeholder_it != end_it) {
    const std::string placeholder = (*placeholder_it)[0].str();
    const auto it = config.find(placeholder);
    if (it != config.end()) {
      const std::string& value = it->second;
      modified_line.replace(placeholder_it->position(), placeholder_it->length(), value);
    }
  }
  return modified_line;
}

void WriteConfig(const std::string& output_path, const std::map<std::string, std::string>& config) {
  // Define the template file path and the output file path.
  std::string template_path =
      JoinPathSegments(yb::env_util::GetRootDir("share"), "share", "ysql_conn_mgr.template.conf");

  // Open the template file for reading.
  std::ifstream template_file(template_path, std::ios_base::in);
  if (!template_file.is_open()) {
    LOG(FATAL) << "Unable to read the template config file for YSQL Connection Manager. File path: "
               << template_path << ". Error details: " << std::strerror(errno);
  }

  // Open the output file for writing.
  std::ofstream output_file(output_path, std::ios_base::trunc);
  if (!output_file.is_open()) {
    LOG(FATAL) << "Unable to write the config file for YSQL Connection Manager. File path: "
               << template_path << ". Error details: " << std::strerror(errno);
  }

  std::string line;
  while (std::getline(template_file, line)) {
    // Read the template file line by line and replace placeholders with their values.
    output_file << PutConfigValue(line, config) << std::endl;
  }

  // Close the input and output files.
  template_file.close();
  output_file.close();
  LOG(INFO) << "Successfully created the configuration file for Ysql Connection Manager: "
            << output_path;
}

std::string get_num_workers(uint32_t value) {
  // If value is 0, return "auto". Odyssey config reader sets the number of workers as
  // (number of cpu cores / 2) if workers is set as "auto".
  if (!value)
    return "\"auto\"";

  return std::to_string(value);
}

std::string YsqlConnMgrConf::CreateYsqlConnMgrConfigAndGetPath() {
  const auto conf_file_path = JoinPathSegments(data_dir_, conf_file_name_);

  // Config map
  std::map<std::string, std::string> ysql_conn_mgr_configs = {
    {"{%log_file%}", log_file_},
    {"{%pid_file%}", pid_file_},
    {"{%quantiles%}", quantiles_},
    {"{%control_conn_db%}", FLAGS_ysql_conn_mgr_internal_conn_db},
    {"{%postgres_host%}", postgres_address_.host()},
    {"{%control_connection_pool_size%}", std::to_string(control_connection_pool_size_)},
    {"{%global_pool_size%}", std::to_string(global_pool_size_)},
    {"{%num_resolver_threads%}", std::to_string(num_resolver_threads_)},
    {"{%num_worker_threads%}", get_num_workers(FLAGS_ysql_conn_mgr_num_workers)},
    {"{%pool_ttl%}", std::to_string(FLAGS_ysql_conn_mgr_idle_time)},
    {"{%ysql_conn_mgr_port%}", std::to_string(FLAGS_ysql_conn_mgr_port)},
    {"{%ysql_conn_mgr_max_client_connections%}",
     std::to_string(FLAGS_ysql_conn_mgr_max_client_connections)},
    {"{%ysql_port%}", std::to_string(postgres_address_.port())},
    {"{%application_name_add_host%}", BoolToString(application_name_add_host_)},
    {"{%log_debug%}", BoolToString(log_debug_)},
    {"{%stats_interval%}", std::to_string(FLAGS_ysql_conn_mgr_stats_interval)},
    {"{%min_pool_size%}", std::to_string(FLAGS_ysql_conn_mgr_min_conns_per_db)}};

  // Create a config file.
  WriteConfig(conf_file_path, ysql_conn_mgr_configs);
  return conf_file_path;
}

int getMaxConnectionsFromYsqlPgConf(const std::string &ysqlpgconf_path) {
  std::ifstream ysql_pg_conf_file(ysqlpgconf_path, std::ios_base::in);
  if (!ysql_pg_conf_file.is_open()) {
    LOG(FATAL) << "Unable to read the ysql pg conf file. File path: "
               << ysqlpgconf_path << ". Error details: " << std::strerror(errno);
  }

  std::string line;
  std::string value("10");
  std::string max_connections_key = "max_connections";
  while (std::getline(ysql_pg_conf_file, line)) {
    if (line.length() == 0) {
      continue;
    }
    std::istringstream iss(line);
    std::string word;
    if (!StringStartsWithOrEquals(line, max_connections_key.c_str())) {
      continue;
    }
    std::vector<std::string> words = StringSplit(line, '=');

    std::string w0 = words[0];
    boost::trim(w0);
    if (words.size() > 1 && w0 == max_connections_key) {
      std::string w1 = words[1];
      boost::trim(w0);
      value = w1;
    }
  }

  // Close the input and output files.
  ysql_pg_conf_file.close();
  int max = std::atoi(value.c_str());
  if (max <= 0) {
    LOG(FATAL) << "Cannot determine the max_connection settings of the database";
  }
  LOG(INFO) << "Maximum physical connections settings found = " << max;
  return max;
}

void YsqlConnMgrConf::UpdateConfigFromGFlags() {
  // Get the max size of connections which the postgres can support. The postgres
  // instance to which this instance of ysql_conn_mgr is going to get attached.
  int maxConnections = getMaxConnectionsFromYsqlPgConf(ysql_pgconf_file_);
  // Divide the pool between the global pool and control connection pool.
  global_pool_size_ = FLAGS_ysql_conn_mgr_max_conns_per_db;
  if (global_pool_size_ == 0) {
    global_pool_size_ = maxConnections * 9 / 10;
  }
  control_connection_pool_size_ = (maxConnections) / 10;

  CHECK_OK(postgres_address_.ParseString(
      FLAGS_pgsql_proxy_bind_address, pgwrapper::PgProcessConf().kDefaultPort));

  // Use the log level of tserver flag `minloglevel`.
  log_debug_ = (GetAtomicFlag(&FLAGS_minloglevel) <= 2) ? true : false;
}

YsqlConnMgrConf::YsqlConnMgrConf(const std::string& data_path) {
  data_dir_ = JoinPathSegments(data_path, "yb-data", "tserver");
  log_file_ = JoinPathSegments(FLAGS_log_dir, "ysql-conn-mgr.log");
  pid_file_ = JoinPathSegments(data_path, "yb-data", "tserver", "ysql-conn-mgr.pid");
  ysql_pgconf_file_ = JoinPathSegments(data_path, "pg_data", "ysql_pg.conf");


  UpdateConfigFromGFlags();

  // Create the log directory if it is not present.
  // This is to handle the case while running the java tests,
  // in which log directory is not created.
  CHECK_OK(env_util::CreateDirIfMissing(Env::Default(), FLAGS_log_dir.c_str()));
}

}  // namespace ysql_conn_mgr_wrapper
}  // namespace yb
