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

#include <string>

#include "yb/fs/fs_manager.h"

#include "yb/server/server_base_options.h"

#include "yb/util/net/net_util.h"
#include "yb/util/status.h"

namespace yb {

class FsManager;

namespace server {
class ServerBaseOptions;
}  // namespace server

struct ProcessWrapperCommonConfig {
  std::string certs_dir;
  std::string certs_for_client_dir;
  std::string cert_base_name;
  bool enable_tls = false;

  Status SetSslConf(const server::ServerBaseOptions& options, FsManager& fs_manager);
};

}  // namespace yb
