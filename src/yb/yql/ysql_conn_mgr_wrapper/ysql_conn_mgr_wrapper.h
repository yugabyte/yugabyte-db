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

#pragma once

#include "yb/yql/process_wrapper/process_wrapper.h"

#include "yb/util/net/net_util.h"

namespace yb {

namespace tserver {

class TabletServerIf;

}  // namespace tserver


// YsqlConnMgrWrapper: managing one instance of a Ysql Connection Manager child process
namespace ysql_conn_mgr_wrapper {

class YsqlConnMgrConf {
 public:
  explicit YsqlConnMgrConf(const std::string& data_path);

  std::string CreateYsqlConnMgrConfigAndGetPath();

 private:
  const std::string conf_file_name_ = "ysql_conn_mgr.conf";
  std::string data_dir_;
  std::string log_file_;
  std::string pid_file_;
  std::string quantiles_ = "0.99,0.95,0.5";
  HostPort postgres_address_;

  uint16_t global_pool_size_ = 10;
  uint16_t control_connection_pool_size_;
  uint num_resolver_threads_ = 1;
  uint16_t server_lifetime_ = 3600;

  bool application_name_add_host_ = true;
  bool log_debug_ = false;

  void UpdateConfigFromGFlags();
  std::string GetBindAddress();
};

class YsqlConnMgrWrapper : public yb::ProcessWrapper {
 public:
  explicit YsqlConnMgrWrapper(const YsqlConnMgrConf& conf);
  Status PreflightCheck() override;
  Status Start() override;

 private:
  std::string GetYsqlConnMgrExecutablePath();
  YsqlConnMgrConf conf_;

  // TODO(janand) GH #17877 Support for reloading config.
  Status ReloadConfig() override {
    return STATUS(IllegalState, "Custom implementation is required");
  }

  virtual Status UpdateAndReloadConfig() override {
    return STATUS(IllegalState, "Custom implementation is required.");
  }
};

// YsqlConnMgrSupervisor: monitoring a Ysql Connection Manager child process
// and restarting if needed.
class YsqlConnMgrSupervisor : public yb::ProcessSupervisor {
 public:
  explicit YsqlConnMgrSupervisor(const YsqlConnMgrConf& conf);
  ~YsqlConnMgrSupervisor() {}

  std::shared_ptr<ProcessWrapper> CreateProcessWrapper() override;

 private:
  YsqlConnMgrConf conf_;
  std::string GetProcessName() override {
    return "Ysql Connection Manager";
  }
};

}  // namespace ysql_conn_mgr_wrapper
}  // namespace yb
