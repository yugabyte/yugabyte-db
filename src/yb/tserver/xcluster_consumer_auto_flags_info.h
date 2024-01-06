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

#include <atomic>
#include <shared_mutex>

#include "yb/cdc/xcluster_types.h"
#include "yb/gutil/integral_types.h"
#include "yb/gutil/thread_annotations.h"

namespace yb {

namespace client {
class YBClient;
}  // namespace client

namespace tserver {

// Helper class to get the compatible target AutoFlags config version.
class AutoFlagsCompatibleVersion {
 public:
  explicit AutoFlagsCompatibleVersion(uint32 compatible_version);
  uint32 GetCompatibleVersion() const;

 protected:
  std::atomic<uint32> compatible_version_;
};

// Helper class to track the compatible target AutoFlags config version and the max reported
// version.
class AutoFlagVersionInfo : public AutoFlagsCompatibleVersion {
 public:
  explicit AutoFlagVersionInfo(uint32 compatible_version, uint32 max_reported_version);
  void SetMaxCompatibleVersion(uint32 new_version);
  void SetMaxReportedVersion(uint32 max_reported_version) EXCLUDES(version_report_mutex_);

  std::mutex version_report_mutex_;
  std::atomic<uint32> max_reported_version_;
};

// Helper class to store the compatible target AutoFlags config version and report new versions to
// the yb-master. This will ensure that each version per replication group is reported only once.
class AutoFlagsVersionHandler {
 public:
  explicit AutoFlagsVersionHandler(std::shared_ptr<client::YBClient> client);
  virtual ~AutoFlagsVersionHandler() = default;

  void InsertOrUpdate(
      const xcluster::ReplicationGroupId& replication_group_id, uint32 compatible_version,
      uint32 max_reported_version) EXCLUDES(mutex_);

  void Delete(const xcluster::ReplicationGroupId& replication_group_id) EXCLUDES(mutex_);

  std::shared_ptr<AutoFlagVersionInfo> GetAutoFlagsCompatibleVersion(
      const xcluster::ReplicationGroupId& replication_group_id) const EXCLUDES(mutex_);

  Status ReportNewAutoFlagConfigVersion(
      const xcluster::ReplicationGroupId& replication_group_id, uint32_t new_version) const
      EXCLUDES(mutex_);

 protected:
  virtual Status XClusterReportNewAutoFlagConfigVersion(
      const xcluster::ReplicationGroupId& replication_group_id, uint32 new_version) const;

 private:
  std::shared_ptr<client::YBClient> client_;

  mutable std::shared_mutex mutex_;
  std::unordered_map<xcluster::ReplicationGroupId, std::shared_ptr<AutoFlagVersionInfo>>
      version_info_map_ GUARDED_BY(mutex_);
};

}  // namespace tserver
}  // namespace yb
