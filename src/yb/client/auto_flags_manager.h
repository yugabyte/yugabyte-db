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

#include "yb/common/wire_protocol.pb.h"
#include "yb/server/server_base_options.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/flags/auto_flags_util.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(ApplyNonRuntimeAutoFlags);

class FsManager;

class AutoFlagsManager {
 public:
  explicit AutoFlagsManager(const std::string& process_name, FsManager* fs_manager);

  // Returns true if the load was successful, false if the file was not found.
  // Returns true without doing any work if AutoFlags management is disabled.
  Result<bool> LoadFromFile() EXCLUDES(update_mutex_, config_mutex_);

  // local_hosts is a comma separated list of ip addresses and ports.
  // Returns Status::OK without doing any work if AutoFlags management is disabled.
  Status LoadFromMaster(
      const std::string& local_hosts, const server::MasterAddresses& master_addresses,
      ApplyNonRuntimeAutoFlags apply_non_runtime) EXCLUDES(update_mutex_, config_mutex_);

  // Returns Status::OK without doing any work if AutoFlags management is disabled.
  Status LoadFromConfig(
      const AutoFlagsConfigPB new_config, ApplyNonRuntimeAutoFlags apply_non_runtime)
      EXCLUDES(update_mutex_, config_mutex_);

  uint32_t GetConfigVersion() const EXCLUDES(config_mutex_);

  AutoFlagsConfigPB GetConfig() const EXCLUDES(config_mutex_);

  // Returns all the AutoFlags associated with this process both promoted, and non-promoted ones.
  Result<std::unordered_set<std::string>> GetAvailableAutoFlagsForServer() const;

 private:
  Status ApplyConfig(ApplyNonRuntimeAutoFlags apply_non_runtime) EXCLUDES(config_mutex_);

  const std::string process_name_;

  // FsManager is owned by the parent service, and is expected to outlive this object.
  FsManager* fs_manager_;

  // Expected to be held for a short time to either read or update current_config_.
  mutable rw_spinlock config_mutex_;
  AutoFlagsConfigPB current_config_ GUARDED_BY(config_mutex_);

  // Mutex to ensure only one thread can Update AutoFlagsConfig at a time. Expected to be held for a
  // long time as we may make RPC calls and file IOs.
  mutable std::mutex update_mutex_ ACQUIRED_BEFORE(config_mutex_);
};
}  // namespace yb
