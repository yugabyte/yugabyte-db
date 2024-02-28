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

#include "yb/tserver/xcluster_consumer_auto_flags_info.h"
#include "yb/client/client.h"
#include "yb/gutil/map-util.h"
#include "yb/util/shared_lock.h"

namespace yb::tserver {

AutoFlagsCompatibleVersion::AutoFlagsCompatibleVersion(uint32 compatible_version)
    : compatible_version_(compatible_version) {}

uint32 AutoFlagsCompatibleVersion::GetCompatibleVersion() const {
  return compatible_version_.load(std::memory_order_acquire);
}

AutoFlagVersionInfo::AutoFlagVersionInfo(uint32 compatible_version, uint32 max_reported_version)
    : AutoFlagsCompatibleVersion(compatible_version), max_reported_version_(max_reported_version) {}

void AutoFlagVersionInfo::SetMaxCompatibleVersion(uint32 new_version) {
  compatible_version_.store(new_version, std::memory_order_release);
}

void AutoFlagVersionInfo::SetMaxReportedVersion(uint32 max_reported_version) {
  max_reported_version_.store(max_reported_version, std::memory_order_release);
}

AutoFlagsVersionHandler::AutoFlagsVersionHandler(std::shared_ptr<client::YBClient> client)
    : client_(std::move(client)) {}

void AutoFlagsVersionHandler::InsertOrUpdate(
    const xcluster::ReplicationGroupId& replication_group_id, uint32 compatible_version,
    uint32 max_reported_version) {
  std::lock_guard l(mutex_);
  auto& auto_flags_info_ptr = version_info_map_[replication_group_id];
  if (!auto_flags_info_ptr) {
    auto_flags_info_ptr =
        std::make_shared<AutoFlagVersionInfo>(compatible_version, max_reported_version);
  } else {
    auto_flags_info_ptr->SetMaxCompatibleVersion(compatible_version);
    auto_flags_info_ptr->SetMaxReportedVersion(max_reported_version);
  }
}

void AutoFlagsVersionHandler::Delete(const xcluster::ReplicationGroupId& replication_group_id) {
  std::lock_guard l(mutex_);
  version_info_map_.erase(replication_group_id);
}

std::shared_ptr<AutoFlagVersionInfo> AutoFlagsVersionHandler::GetAutoFlagsCompatibleVersion(
    const xcluster::ReplicationGroupId& replication_group_id) const {
  SharedLock l(mutex_);
  return FindPtrOrNull(version_info_map_, replication_group_id);
}

Status AutoFlagsVersionHandler::ReportNewAutoFlagConfigVersion(
    const xcluster::ReplicationGroupId& replication_group_id, uint32_t new_version) const {
  auto version_info = GetAutoFlagsCompatibleVersion(replication_group_id);
  SCHECK_FORMAT(version_info, NotFound, "Replication group $0 not found", replication_group_id);

  const auto compatible_version = version_info->GetCompatibleVersion();
  if (new_version <= compatible_version) {
    // NoOp. We already received a newer compatible version from master.
    return Status::OK();
  }

  // Get the mutex so that we only send 1 RPC to master at a time.
  std::lock_guard l(version_info->version_report_mutex_);

  auto max_reported_version = version_info->max_reported_version_.load(std::memory_order_acquire);
  if (new_version <= max_reported_version) {
    // NoOp. Master is already aware of the new version.
    return Status::OK();
  }

  LOG(INFO) << "Reporting new AutoFlag Config version " << new_version << " for replication group "
            << replication_group_id
            << " to master. Current max compatible version: " << compatible_version
            << ", max reported version: " << max_reported_version;

  RETURN_NOT_OK(XClusterReportNewAutoFlagConfigVersion(replication_group_id, new_version));

  // Heartbeat from master could have already updated the max reported version. So only update if
  // our value is still higher.
  while (!version_info->max_reported_version_.compare_exchange_weak(
      max_reported_version, new_version, std::memory_order_acq_rel)) {
    if (new_version <= max_reported_version) {
      break;
    }
  }

  return Status::OK();
}

Status AutoFlagsVersionHandler::XClusterReportNewAutoFlagConfigVersion(
    const xcluster::ReplicationGroupId& replication_group_id, uint32 new_version) const {
  return client_->XClusterReportNewAutoFlagConfigVersion(replication_group_id, new_version);
}

}  // namespace yb::tserver
