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

#include "yb/server/auto_flags_manager_base.h"

namespace yb {
namespace tserver {

// There are three ways in which a new config is loaded on the yb-tservers.
//
// LoadFromFile and LoadFromMasterLeader as described in AutoFlagsManagerBase.
//
// HandleMasterHeartbeatResponse - A running tserver process that has already applied one version of
// the config has received a new config. Stores the new config to local disk immediately, and
// asynchronously applies the config at the provided config_apply_time.
class TserverAutoFlagsManager : public AutoFlagsManagerBase {
 public:
  explicit TserverAutoFlagsManager(const scoped_refptr<ClockBase>& clock, FsManager* fs_manager);

  virtual ~TserverAutoFlagsManager() {}

  Status Init(const std::string& local_hosts, const server::MasterAddresses& master_addresses);

  Status ProcessAutoFlagsConfigOperation(const AutoFlagsConfigPB new_config) override;

  void HandleMasterHeartbeatResponse(
      HybridTime heartbeat_sent_time, std::optional<AutoFlagsConfigPB> new_config);

  // Same as GetConfigVersion but makes sure the config is not stale. Config is stale if the tserver
  // has not heartbeated to the master in auto_flags_apply_delay_ms ms.
  Result<uint32_t> ValidateAndGetConfigVersion() const EXCLUDES(mutex_);

 private:
  std::atomic<HybridTime> last_config_sync_time_{HybridTime::kInvalid};
};

}  // namespace tserver
}  // namespace yb
