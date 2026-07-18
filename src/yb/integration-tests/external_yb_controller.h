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

#include "yb/integration-tests/external_daemon.h"
#include "yb/util/status.h"
#include "yb/util/subprocess.h"

namespace yb {

struct ExternalYbControllerOptions {
  size_t idx;
  std::string log_dir;
  std::string tmp_dir;
  std::string yb_tserver_address;
  uint16_t server_port;
  uint16_t yb_master_webserver_port;
  uint16_t yb_tserver_webserver_port;
  std::string server_address;
  std::vector<std::string> extra_flags{};
};

class ExternalYbController : public RefCountedThreadSafe<ExternalYbController> {
 public:
  explicit ExternalYbController(const ExternalYbControllerOptions& options);

  Status Start();

  Status RunBackupCommand(
      const std::string& backup_dir, const std::string& backup_command, const std::string& ns,
      const std::string& ns_type, const std::string& temp_dir, const bool use_tablespaces);

  // Ping the YB Controller server.
  Status ping();

  Status Restart();

  std::string GetServerAddress() const { return options_.server_address; }

  uint16_t GetServerPort() const { return options_.server_port; }

  // Gracefully shutsdown the server.
  // Sends SIGKILL if unsuccessful.
  void Shutdown();

  // Returns true if the process isn't running.
  bool IsShutdown() const;

 protected:
  friend class RefCountedThreadSafe<ExternalYbController>;
  ~ExternalYbController();

 private:
  const ExternalYbControllerOptions options_;
  std::unique_ptr<Subprocess> process_;

  std::unique_ptr<ExternalDaemon::LogTailerThread> stdout_tailer_thread_;
  std::unique_ptr<ExternalDaemon::LogTailerThread> stderr_tailer_thread_;
};
}  // namespace yb
