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

#include <memory>
#include <string>
#include <vector>

#include "yb/util/status_fwd.h"
#include "yb/util/net/net_fwd.h"

namespace yb {

class ExternalYbController;
class MiniClusterBase;

namespace tools {

// Runs backup command against specified cluster.
// Note: to get detailed output from 'yb_backup' tool for debug purposes add into your test:
//       ANNOTATE_UNPROTECTED_WRITE(FLAGS_verbose_yb_backup) = true;
Status RunBackupCommand(
    const HostPort& pg_hp, const std::string& master_addresses,
    const std::string& tserver_http_addresses, const std::string& tmp_dir,
    const std::vector<std::string>& extra_args);

// Runs Backup/Restore command via YB Controller
// The yb controller data dir(which has the logs) is present in the same directory as master/ts
Status RunYbControllerCommand(
    MiniClusterBase* cluster, const std::string& tmp_dir, const std::vector<std::string>& args);

// A class to manage random tmp dir for test.
class TmpDirProvider {
 public:
  ~TmpDirProvider();

  std::string operator/(const std::string& subdir);
  std::string operator*();

 private:
  std::string dir_;
};

} // namespace tools
} // namespace yb
