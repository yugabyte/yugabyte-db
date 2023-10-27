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

#include <string.h>

#include "yb/gutil/ref_counted.h"

#include "yb/util/status.h"
#include "yb/util/subprocess.h"

namespace yb {
class ExternalYbController : public RefCountedThreadSafe<ExternalYbController> {
 public:
  ExternalYbController(
      const std::string& log_dir, const std::string& tmp_dir, const std::string& yb_tserver_address,
      const std::string& yb_admin, const std::string& yb_ctl, const std::string& ycqlsh,
      const std::string& ysql_dump, const std::string& ysql_dumpall, const std::string& ysqlsh,
      uint16_t server_port, uint16_t yb_master_webserver_port, uint16_t yb_tserver_webserver_port,
      const std::string& server_address, const std::string& exe,
      const std::vector<std::string>& extra_flags);

  Status Start();

  // Ping the YB Controller server.
  Status ping();

  Status Restart();

  std::string GetServerAddress() const { return server_address_; }

  uint16_t GetServerPort() const { return server_port_; }

  // Currently this uses SIGKILL for non graceful shutdown
  void Shutdown();

  // Returns true if the process isn't running
  bool IsShutdown() const;

 protected:
  friend class RefCountedThreadSafe<ExternalYbController>;
  ~ExternalYbController();

 private:
  std::unique_ptr<Subprocess> process_;
  const std::string exe_;
  const std::string log_dir_;
  const std::string tmp_dir_;
  const std::string server_address_;
  const std::string yb_tserver_address_;
  const std::string yb_admin_;
  const std::string yb_ctl_;
  const std::string ycqlsh_;
  const std::string ysql_dump_;
  const std::string ysql_dumpall_;
  const std::string ysqlsh_;
  const uint16_t server_port_;
  const uint16_t yb_master_webserver_port_;
  const uint16_t yb_tserver_webserver_port_;
  const std::vector<std::string> extra_flags_;
};
}  // namespace yb
