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

#include <shared_mutex>

#include "yb/common/hybrid_time.h"
#include "yb/common/wire_protocol.pb.h"
#include "yb/server/server_base_options.h"
#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/unique_lock.h"

namespace yb {

YB_STRONGLY_TYPED_BOOL(ApplyNonRuntimeAutoFlags);

class FsManager;

// There are two common ways in which a new config is loaded.
//
// LoadFromFile - On a process restart we load the config that was previous written to its local
// disk. Synchronously waits for the apply time to pass before applying the config.
//
// LoadFromMasterLeader - On startup of a new process which does not have any previous config
// stored in its local disk. Synchronously waits for the apply time to pass before applying the
// config.
//
// Check the derived classes TserverAutoFlagsManager and MasterAutoFlagsManager for more details on
// how they handle new config.
class AutoFlagsManagerBase {
 public:
  virtual Status ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB new_config) = 0;

  uint32_t GetConfigVersion() const EXCLUDES(mutex_);

  AutoFlagsConfigPB GetConfig() const EXCLUDES(mutex_);

  // Returns all the AutoFlags associated with this process both promoted, and non-promoted ones.
  Result<std::unordered_set<std::string>> GetAvailableAutoFlagsForServer() const;

 protected:
  explicit AutoFlagsManagerBase(
      const std::string& process_name, const scoped_refptr<ClockBase>& clock,
      FsManager* fs_manager);
  virtual ~AutoFlagsManagerBase();

  MonoDelta GetApplyDelay() const;

  Status Init(const std::string& local_hosts);

  // Returns true if the load was successful, false if the file was not found.
  // Returns true without doing any work if AutoFlags management is disabled.
  Result<bool> LoadFromFile() EXCLUDES(mutex_);

  // local_hosts is a comma separated list of ip addresses and ports.
  // Returns Status::OK without doing any work if AutoFlags management is disabled.
  Status LoadFromMasterLeader(
      const std::string& local_hosts, const server::MasterAddresses& master_addresses)
      EXCLUDES(mutex_);

  Status LoadFromConfigUnlocked(
      const AutoFlagsConfigPB new_config, ApplyNonRuntimeAutoFlags apply_non_runtime,
      bool apply_sync = false) REQUIRES(mutex_);

  // FsManager is owned by the parent service, and is expected to outlive this object.
  FsManager* fs_manager_;

  scoped_refptr<ClockBase> clock_;

  // Expected to be held for a short time to either read or update current_config_.
  mutable std::shared_mutex mutex_;
  AutoFlagsConfigPB current_config_ GUARDED_BY(mutex_);

 private:
  Result<MonoDelta> GetTimeLeftToApplyConfig() const REQUIRES_SHARED(mutex_);

  // Sets new config to current_config_ if the version is higher. Validates that all flags in the
  // new config are indeed present in the current release. Returns true if the config was set.
  Result<bool> ValidateAndSetConfig(const AutoFlagsConfigPB&& new_config) REQUIRES(mutex_);

  Status WriteConfigToDisk() REQUIRES_SHARED(mutex_);

  Status ApplyConfig(ApplyNonRuntimeAutoFlags apply_non_runtime) const REQUIRES_SHARED(mutex_);

  void AsyncApplyConfig(uint32 config_version, ApplyNonRuntimeAutoFlags apply_non_runtime)
      EXCLUDES(mutex_);

  // Get the AutoFlagConfig from master. Returns std::nullopt if master is runnning on an older
  // version that does not support AutoFlags.
  Result<std::optional<AutoFlagsConfigPB>> GetAutoFlagConfigFromMasterLeader(
      const std::string& master_addresses);

  const std::string process_name_;

  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;

  DISALLOW_COPY_AND_ASSIGN(AutoFlagsManagerBase);
};

}  // namespace yb
