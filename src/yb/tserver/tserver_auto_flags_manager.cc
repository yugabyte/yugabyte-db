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

#include "yb/tserver/tserver_auto_flags_manager.h"
#include "yb/common/clock.h"

DECLARE_uint64(max_clock_skew_usec);

namespace yb::tserver {

constexpr auto kYbTserverProcessName = "yb-tserver";

TserverAutoFlagsManager::TserverAutoFlagsManager(
    const scoped_refptr<ClockBase>& clock, FsManager* fs_manager)
    : AutoFlagsManagerBase(kYbTserverProcessName, clock, fs_manager) {}

Status TserverAutoFlagsManager::Init(
    const std::string& local_hosts, const server::MasterAddresses& master_addresses) {
  RETURN_NOT_OK(AutoFlagsManagerBase::Init(local_hosts));

  if (VERIFY_RESULT(LoadFromFile())) {
    return Status::OK();
  }

  return LoadFromMasterLeader(local_hosts, master_addresses);
}

Status TserverAutoFlagsManager::ProcessAutoFlagsConfigOperation(
    const AutoFlagsConfigPB new_config) {
  return STATUS(
      NotSupported, "ProcessAutoFlagsConfigOperation not supported on TserverAutoFlagsManager");
}

void TserverAutoFlagsManager::HandleMasterHeartbeatResponse(
    HybridTime heartbeat_sent_time, std::optional<AutoFlagsConfigPB> new_config) {
  if (new_config) {
    std::lock_guard l(mutex_);
    // We cannot fail to load a new config that was provided by the master.
    CHECK_OK(LoadFromConfigUnlocked(std::move(*new_config), ApplyNonRuntimeAutoFlags::kFalse));
  }
  last_config_sync_time_.store(heartbeat_sent_time, std::memory_order_release);
}

Result<uint32_t> TserverAutoFlagsManager::ValidateAndGetConfigVersion() const {
  const auto last_config_sync_time = last_config_sync_time_.load(std::memory_order_acquire);
  SCHECK(last_config_sync_time, IllegalState, "AutoFlags config is stale. No config sync time set");
  const auto apply_delay = GetApplyDelay();
  const auto max_allowed_time =
      last_config_sync_time.AddDelta(apply_delay)
          .AddDelta(MonoDelta::FromMicroseconds(-1 * FLAGS_max_clock_skew_usec));

  const auto now = clock_->Now();
  SCHECK_LT(
      now, max_allowed_time, IllegalState,
      Format(
          "AutoFlags config is stale. Last sync time: $0, Max allowed staleness: $1",
          last_config_sync_time, apply_delay));

  return GetConfigVersion();
}
} // namespace yb::tserver
