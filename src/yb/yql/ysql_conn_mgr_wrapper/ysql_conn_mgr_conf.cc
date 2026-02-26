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

#include "yb/yql/ysql_conn_mgr_wrapper/ysql_conn_mgr_wrapper.h"

#include <fstream>
#include <regex>

#include <boost/algorithm/string.hpp>

#include "yb/util/atomic.h"
#include "yb/util/env_util.h"
#include "yb/util/path_util.h"
#include "yb/util/net/net_util.h"
#include "yb/util/string_trim.h"
#include "yb/util/string_util.h"
#include "yb/util/pg_util.h"

#include "yb/yql/pgwrapper/pg_wrapper.h"

DECLARE_bool(logtostderr);
DECLARE_bool(ysql_conn_mgr_use_unix_conn);
DECLARE_bool(ysql_conn_mgr_use_auth_backend);
DECLARE_bool(ysql_conn_mgr_enable_multi_route_pool);
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
DECLARE_string(ysql_conn_mgr_log_settings);
DECLARE_uint32(ysql_conn_mgr_server_lifetime);
DECLARE_uint64(ysql_conn_mgr_log_max_size);
DECLARE_uint64(ysql_conn_mgr_log_rotate_interval);
DECLARE_uint32(ysql_conn_mgr_readahead_buffer_size);
DECLARE_uint32(ysql_conn_mgr_tcp_keepalive);
DECLARE_uint32(ysql_conn_mgr_tcp_keepalive_keep_interval);
DECLARE_uint32(ysql_conn_mgr_tcp_keepalive_probes);
DECLARE_uint32(ysql_conn_mgr_tcp_keepalive_usr_timeout);
DECLARE_uint32(ysql_conn_mgr_control_connection_pool_size);
DECLARE_uint32(ysql_conn_mgr_pool_timeout);
DECLARE_bool(ysql_conn_mgr_optimized_extended_query_protocol);
DECLARE_bool(ysql_conn_mgr_optimized_session_parameters);
DECLARE_int32(ysql_conn_mgr_max_pools);
DECLARE_uint32(ysql_conn_mgr_jitter_time);
DECLARE_uint32(ysql_conn_mgr_reserve_internal_conns);
DECLARE_uint32(TEST_ysql_conn_mgr_auth_delay_ms);

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

  while (placeholder_it != end_it) {
    const std::string placeholder = (*placeholder_it)[0].str();
    const auto it = config.find(placeholder);

    if (it != config.end()) {
      const std::string& value = it->second;
      modified_line.replace(placeholder_it->position(), placeholder_it->length(), value);
    }
    placeholder_it = std::sregex_iterator(modified_line.begin(),
                                          modified_line.end(),
                                          placeholder_regex);
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

void YsqlConnMgrConf::AddSslConfig(std::map<std::string, std::string>* ysql_conn_mgr_configs) {
  std::string tls_ca_file;
  std::string tls_key_file;
  std::string tls_cert_file;

  // ssl config
  if (enable_tls && !certs_for_client_dir.empty()) {
    if (tls_cert_file.empty())
    tls_cert_file = Format("$0/node.$1.crt",
                           certs_for_client_dir,
                           cert_base_name);
    if (tls_key_file.empty())
    tls_key_file = Format("$0/node.$1.key",
                           certs_for_client_dir,
                           cert_base_name);
    if (tls_ca_file.empty())
    tls_ca_file = Format("$0/ca.crt", certs_for_client_dir);
  }

  (*ysql_conn_mgr_configs)["{%enable_tls%}"] = enable_tls ? "" : "#";
  (*ysql_conn_mgr_configs)["{%tls_ca_file%}"] = tls_ca_file;
  (*ysql_conn_mgr_configs)["{%tls_key_file%}"] = tls_key_file;
  (*ysql_conn_mgr_configs)["{%tls_cert_file%}"] = tls_cert_file;
}

void YsqlConnMgrConf::UpdateLogSettings(std::string& log_settings_str) {
  std::stringstream ss(log_settings_str);
  std::string setting;

  while (std::getline(ss, setting, ',')) {
    util::TrimStr(setting);
    if (!setting.empty()) {
      if (setting == "log_debug") {
        log_debug_ = true;
      } else if (setting == "log_config") {
        log_config_ = true;
      } else if (setting == "log_session") {
        log_session_ = true;
      } else if (setting == "log_query") {
        log_query_ = true;
      } else if (setting == "log_stats") {
        log_stats_ = true;
      }
    }
  }
}

std::string YsqlConnMgrConf::CreateYsqlConnMgrConfigAndGetPath() {
  const auto conf_file_path = JoinPathSegments(data_dir_, conf_file_name_);
  UpdateLogSettings(FLAGS_ysql_conn_mgr_log_settings);

  // Config map
  std::map<std::string, std::string> ysql_conn_mgr_configs = {
    {"{%log_dir%}", FLAGS_log_dir},
    {"{%log_max_size%}", std::to_string(FLAGS_ysql_conn_mgr_log_max_size)},
    {"{%log_rotate_interval%}", std::to_string(FLAGS_ysql_conn_mgr_log_rotate_interval)},
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
    {"{%log_debug%}", BoolToString(log_debug_)},
    {"{%log_config%}", BoolToString(log_config_)},
    {"{%log_session%}", BoolToString(log_session_)},
    {"{%log_query%}", BoolToString(log_query_)},
    {"{%log_stats%}", BoolToString(log_stats_)},
    {"{%logtostderr%}", FLAGS_logtostderr ? "yes" : "no"},
    {"{%stats_interval%}", std::to_string(FLAGS_ysql_conn_mgr_stats_interval)},
    {"{%server_lifetime%}", std::to_string(FLAGS_ysql_conn_mgr_server_lifetime)},
    {"{%min_pool_size%}", std::to_string(FLAGS_ysql_conn_mgr_min_conns_per_db)},
    {"{%yb_use_unix_socket%}", FLAGS_ysql_conn_mgr_use_unix_conn ? "" : "#"},
    {"{%yb_use_tcp_socket%}", FLAGS_ysql_conn_mgr_use_unix_conn ? "#" : ""},
    {"{%yb_use_auth_backend%}", BoolToString(FLAGS_ysql_conn_mgr_use_auth_backend)},
    {"{%readahead_buffer_size%}", std::to_string(FLAGS_ysql_conn_mgr_readahead_buffer_size)},
    {"{%tcp_keepalive%}", std::to_string(FLAGS_ysql_conn_mgr_tcp_keepalive)},
    {"{%tcp_keepalive_keep_interval%}",
     std::to_string(FLAGS_ysql_conn_mgr_tcp_keepalive_keep_interval)},
    {"{%tcp_keepalive_probes%}", std::to_string(FLAGS_ysql_conn_mgr_tcp_keepalive_probes)},
    {"{%tcp_keepalive_usr_timeout%}",
     std::to_string(FLAGS_ysql_conn_mgr_tcp_keepalive_usr_timeout)},
    {"{%pool_timeout%}", std::to_string(FLAGS_ysql_conn_mgr_pool_timeout)},
    {"{%yb_optimized_extended_query_protocol%}",
      BoolToString(FLAGS_ysql_conn_mgr_optimized_extended_query_protocol)},
    {"{%yb_enable_multi_route_pool%}", BoolToString(FLAGS_ysql_conn_mgr_enable_multi_route_pool)},
    {"{%yb_ysql_max_connections%}", std::to_string(ysql_max_connections_)},
    {"{%yb_optimized_session_parameters%}",
      BoolToString(FLAGS_ysql_conn_mgr_optimized_session_parameters)},
    {"{%yb_max_pools%}", std::to_string(FLAGS_ysql_conn_mgr_max_pools)},
    {"{%yb_jitter_time%}", std::to_string(FLAGS_ysql_conn_mgr_jitter_time)},
    {"{%TEST_yb_auth_delay_ms%}", std::to_string(FLAGS_TEST_ysql_conn_mgr_auth_delay_ms)},
    {"{%unix_socket_dir%}",
      PgDeriveSocketDir(postgres_address_)}}; // Return unix socket
            //  file path = "/tmp/.yb.host_ip:port"

  AddSslConfig(&ysql_conn_mgr_configs);

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

  // Either it's multi route pooling where yb_ysql_max_connections is relevant or
  // it's non-multi route pooling where control_connection_pool_size and global_pool_size are
  // relevant. The total number of ysql connections that connection manager can create is
  // total ysql_max_connections less FLAGS_ysql_conn_mgr_reserve_internal_conns. This ensures
  // some connections are reserved for internal operations which will bypass the
  // YSQL Connection Manager.

  CHECK_LE(FLAGS_ysql_conn_mgr_reserve_internal_conns, maxConnections)
      << "ysql_conn_mgr_reserve_internal_conns must be less than or equal to maxConnections";

  maxConnections = static_cast<int>(maxConnections - FLAGS_ysql_conn_mgr_reserve_internal_conns);

  // Divide the pool between the global pool and control connection pool.
  global_pool_size_ = FLAGS_ysql_conn_mgr_max_conns_per_db;
  if (global_pool_size_ == 0) {
    global_pool_size_ = maxConnections * 9 / 10;
  }
  control_connection_pool_size_ = FLAGS_ysql_conn_mgr_control_connection_pool_size;
  if (control_connection_pool_size_ == 0) {
    control_connection_pool_size_ = (maxConnections) / 10;
  }
  ysql_max_connections_ = maxConnections;

  auto parsed = CHECK_RESULT(
      pgwrapper::ParsePgBindAddresses(FLAGS_pgsql_proxy_bind_address, pgwrapper::PgProcessConf::kDefaultPort));
  postgres_address_ = HostPort(parsed.first_host, parsed.port);
}

YsqlConnMgrConf::YsqlConnMgrConf(const std::string& data_path) {
  data_dir_ = JoinPathSegments(data_path, "yb-data", "tserver");
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
