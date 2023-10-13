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

#include <shared_mutex>

#include "yb/cdc/cdc_types.h"
#include "yb/gutil/thread_annotations.h"
#include "yb/master/leader_epoch.h"
#include "yb/util/cow_object.h"
#include "yb/util/status_fwd.h"

namespace yb {

namespace master {

class SysCatalogTable;
class XClusterConfigInfo;
class SysXClusterConfigEntryPB;
class TSHeartbeatRequestPB;
class TSHeartbeatResponsePB;
struct PersistentXClusterConfigInfo;
class CDCStreamInfo;

// Wrapper over XClusterConfigInfo that provides safe utils to access and modify XClusterConfigInfo.
class XClusterConfig {
 public:
  explicit XClusterConfig(SysCatalogTable* sys_catalog);
  ~XClusterConfig() = default;

  void ClearState();

  void Load(const SysXClusterConfigEntryPB& metadata) EXCLUDES(mutex_);

  Status PrepareDefault(int64_t term, bool re_create) EXCLUDES(mutex_);

  Result<uint32_t> GetVersion() const EXCLUDES(mutex_);

  Result<SysXClusterConfigEntryPB> GetXClusterConfigEntryPB() const EXCLUDES(mutex_);

  Status FillHeartbeatResponse(const TSHeartbeatRequestPB& req, TSHeartbeatResponsePB* resp) const
      EXCLUDES(mutex_);

  Status RemoveStreams(const LeaderEpoch& epoch, const std::vector<CDCStreamInfo*>& streams);

  Status PauseResumeXClusterProducerStreams(
      const LeaderEpoch& epoch, std::vector<xrepl::StreamId> stream_ids, bool pause);

 private:
  Status BumpVersionUpsertAndCommit(
      const LeaderEpoch& epoch, CowWriteLock<PersistentXClusterConfigInfo>& l)
      REQUIRES_SHARED(mutex_);

  SysCatalogTable* const sys_catalog_;

  mutable std::shared_mutex mutex_;

  std::shared_ptr<XClusterConfigInfo> xcluster_config_info_ GUARDED_BY(mutex_);
};

}  // namespace master

}  // namespace yb
